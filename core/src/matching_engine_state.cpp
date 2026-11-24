// Full-state serialization of MatchingEngine (TODO §2). Layout (all little-endian, see
// docs/adr/0006 and 0010): counters and market data, resting orders per side in priority order,
// the active order, triggered stops, pending stops per side in trigger order, queued requests,
// and the account ledger.

#include "stocksim/matching_engine.hpp"
#include "stocksim/order.hpp"
#include "stocksim/result.hpp"
#include "stocksim/serialization.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace stocksim {

namespace {

// Lower bounds on encoded record sizes, used to reject corrupt element counts early.
constexpr std::size_t kMinRestingBytes = 8 + 4 + 1 + 8 + 8 + 8;
constexpr std::size_t kMinStopBytes = 8 + 4 + 1 + 1 + 8 + 8 + 8;
constexpr std::size_t kMinCommandBytes = 1 + 8 + 4 + 1 + 1 + 8 + 8 + 8 + 4 + 8 + 8;

constexpr Error kCorrupt{.code = ErrorCode::kCorruptState,
                         .message = "Engine state is truncated or inconsistent."};

void write_side(StateWriter& out, Side side) {
    out.u8(static_cast<std::uint8_t>(side));
}

void write_type(StateWriter& out, OrderType type) {
    out.u8(static_cast<std::uint8_t>(type));
}

Side read_side(StateReader& in) {
    const std::uint8_t raw = in.u8();
    if (raw > static_cast<std::uint8_t>(Side::kSell)) {
        in.fail();
        return Side::kBuy;
    }
    return static_cast<Side>(raw);
}

OrderType read_type(StateReader& in) {
    const std::uint8_t raw = in.u8();
    if (raw > static_cast<std::uint8_t>(OrderType::kStopLimit)) {
        in.fail();
        return OrderType::kMarket;
    }
    return static_cast<OrderType>(raw);
}

void write_optional_price(StateWriter& out, const std::optional<PriceTicks>& price) {
    out.boolean(price.has_value());
    out.i64(price.value_or(PriceTicks{}).value());
}

std::optional<PriceTicks> read_optional_price(StateReader& in) {
    const bool present = in.boolean();
    const PriceTicks value{in.i64()};
    return present ? std::optional{value} : std::nullopt;
}

void write_new_order(StateWriter& out, const NewOrder& order) {
    out.u32(order.account.value());
    write_side(out, order.side);
    write_type(out, order.type);
    out.i64(order.qty.value());
    out.i64(order.limit_price.value());
    out.i64(order.stop_price.value());
}

NewOrder read_new_order(StateReader& in) {
    NewOrder order;
    order.account = AccountId{in.u32()};
    order.side = read_side(in);
    order.type = read_type(in);
    order.qty = Qty{in.i64()};
    order.limit_price = PriceTicks{in.i64()};
    order.stop_price = PriceTicks{in.i64()};
    return order;
}

void write_resting(StateWriter& out, const RestingOrder& order) {
    out.u64(order.id.value());
    out.u32(order.account.value());
    write_type(out, order.type);
    out.i64(order.price.value());
    out.i64(order.remaining.value());
    out.i64(order.filled.value());
}

RestingOrder read_resting(StateReader& in, Side side) {
    RestingOrder order;
    order.id = OrderId{in.u64()};
    order.account = AccountId{in.u32()};
    order.side = side;
    order.type = read_type(in);
    order.price = PriceTicks{in.i64()};
    order.remaining = Qty{in.i64()};
    order.filled = Qty{in.i64()};
    const bool valid = has_limit_price(order.type) && order.price > PriceTicks{0} &&
                       order.price <= kMaxPrice && order.remaining > Qty{0} &&
                       order.filled >= Qty{0};
    if (!valid) {
        in.fail();
    }
    return order;
}

} // namespace

void MatchingEngine::save_state(StateWriter& out) const {
    out.i64(m_tick.value());
    out.u64(m_next_order_id);
    out.u64(m_next_sequence);
    out.u64(m_next_stop_sequence);
    write_optional_price(out, m_last_price);
    out.i64(m_total_volume.value());
    out.i64(m_tick_volume.value());
    out.i64(m_previous_tick_volume.value());
    out.u64(m_trade_count);
    out.i64(m_taker_buy.value());
    out.i64(m_taker_sell.value());
    out.i64(m_previous_taker_buy.value());
    out.i64(m_previous_taker_sell.value());

    for (const Side side : {Side::kBuy, Side::kSell}) {
        std::vector<RestingOrder> orders;
        m_book.for_each_in_priority(side,
                                    [&](const RestingOrder& order) { orders.push_back(order); });
        out.u64(orders.size());
        for (const RestingOrder& order : orders) {
            write_resting(out, order);
        }
    }

    const auto write_stop = [&](const StopOrder& stop) {
        out.u64(stop.id.value());
        out.u32(stop.account.value());
        write_side(out, stop.side);
        write_type(out, stop.type);
        out.i64(stop.limit_price.value());
        out.i64(stop.stop_price.value());
        out.i64(stop.qty.value());
    };

    out.boolean(m_active.has_value());
    if (m_active) {
        const ActiveOrder& active = *m_active;
        out.u64(active.id.value());
        out.u32(active.account.value());
        write_side(out, active.side);
        write_type(out, active.type);
        out.i64(active.limit_price.value());
        out.i64(active.stop_price.value());
        out.i64(active.remaining.value());
        out.i64(active.filled.value());
    }

    out.u64(m_triggered.size());
    for (const StopOrder& stop : m_triggered) {
        write_stop(stop);
    }

    const auto write_stop_map = [&](const auto& stops) {
        out.u64(stops.size());
        for (const auto& [key, stop] : stops) {
            out.u64(key.sequence);
            write_stop(stop);
        }
    };
    write_stop_map(m_buy_stops);
    write_stop_map(m_sell_stops);

    out.u64(m_commands.size());
    for (const Command& command : m_commands) {
        out.u8(static_cast<std::uint8_t>(command.kind));
        out.u64(command.id.value());
        write_new_order(out, command.order);
        out.u32(command.requester.value());
        out.i64(command.new_limit_price.value());
        out.i64(command.new_qty.value());
    }

    m_ledger.save_state(out);
}

Result<MatchingEngine> MatchingEngine::load_state(StateReader& in, const EngineConfig& config) {
    MatchingEngine engine{config};
    engine.load_counters(in);
    engine.load_book(in);
    engine.load_active(in);
    engine.load_stops(in);
    engine.load_commands(in);
    engine.m_ledger.load_state(in);
    if (!in.ok()) {
        return kCorrupt;
    }
    return engine;
}

void MatchingEngine::load_counters(StateReader& in) {
    m_tick = Tick{in.i64()};
    m_next_order_id = in.u64();
    m_next_sequence = in.u64();
    m_next_stop_sequence = in.u64();
    m_last_price = read_optional_price(in);
    m_total_volume = Qty{in.i64()};
    m_tick_volume = Qty{in.i64()};
    m_previous_tick_volume = Qty{in.i64()};
    m_trade_count = in.u64();
    m_taker_buy = Qty{in.i64()};
    m_taker_sell = Qty{in.i64()};
    m_previous_taker_buy = Qty{in.i64()};
    m_previous_taker_sell = Qty{in.i64()};
    if (m_next_order_id == 0 || m_next_sequence == 0) {
        in.fail();
    }
}

void MatchingEngine::load_book(StateReader& in) {
    for (const Side side : {Side::kBuy, Side::kSell}) {
        const std::uint64_t count = in.count(kMinRestingBytes);
        for (std::uint64_t i = 0; i < count && in.ok(); ++i) {
            const RestingOrder order = read_resting(in, side);
            if (in.ok() && !m_book.add(order)) {
                in.fail(); // duplicate id
            }
        }
    }
    if (m_book.is_crossed()) {
        in.fail();
    }
}

void MatchingEngine::load_active(StateReader& in) {
    if (!in.boolean()) {
        return;
    }
    ActiveOrder active;
    active.id = OrderId{in.u64()};
    active.account = AccountId{in.u32()};
    active.side = read_side(in);
    active.type = read_type(in);
    active.limit_price = PriceTicks{in.i64()};
    active.stop_price = PriceTicks{in.i64()};
    active.remaining = Qty{in.i64()};
    active.filled = Qty{in.i64()};
    if (active.remaining <= Qty{0}) {
        in.fail();
    }
    m_active = active;
}

MatchingEngine::StopOrder MatchingEngine::read_stop(StateReader& in) {
    StopOrder stop;
    stop.id = OrderId{in.u64()};
    stop.account = AccountId{in.u32()};
    stop.side = read_side(in);
    stop.type = read_type(in);
    stop.limit_price = PriceTicks{in.i64()};
    stop.stop_price = PriceTicks{in.i64()};
    stop.qty = Qty{in.i64()};
    if (!has_stop_price(stop.type) || stop.qty <= Qty{0}) {
        in.fail();
    }
    return stop;
}

void MatchingEngine::load_stops(StateReader& in) {
    const std::uint64_t triggered_count = in.count(kMinStopBytes);
    for (std::uint64_t i = 0; i < triggered_count && in.ok(); ++i) {
        m_triggered.push_back(read_stop(in));
    }

    const auto load_side = [&](auto& stops, Side expected_side) {
        const std::uint64_t count = in.count(kMinStopBytes + 8);
        for (std::uint64_t i = 0; i < count && in.ok(); ++i) {
            const std::uint64_t sequence = in.u64();
            const StopOrder stop = read_stop(in);
            const StopKey key{.stop_price = stop.stop_price, .sequence = sequence};
            const bool valid = in.ok() && stop.side == expected_side &&
                               !m_stop_index.contains(stop.id) && stops.emplace(key, stop).second;
            if (!valid) {
                in.fail();
                return;
            }
            m_stop_index.emplace(stop.id, std::pair{stop.side, key});
        }
    };
    load_side(m_buy_stops, Side::kBuy);
    load_side(m_sell_stops, Side::kSell);
}

void MatchingEngine::load_commands(StateReader& in) {
    const std::uint64_t count = in.count(kMinCommandBytes);
    for (std::uint64_t i = 0; i < count && in.ok(); ++i) {
        Command command;
        const std::uint8_t kind = in.u8();
        if (kind > static_cast<std::uint8_t>(CommandKind::kModify)) {
            in.fail();
        }
        command.kind = static_cast<CommandKind>(kind);
        command.id = OrderId{in.u64()};
        command.order = read_new_order(in);
        command.requester = AccountId{in.u32()};
        command.new_limit_price = PriceTicks{in.i64()};
        command.new_qty = Qty{in.i64()};
        m_commands.push_back(command);
    }
}

} // namespace stocksim
