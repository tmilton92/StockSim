#include "stocksim/matching_engine.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <optional>
#include <span>

namespace stocksim {

MatchingEngine::MatchingEngine(const EngineConfig& config) : m_book(config.expected_orders) {
    m_events.reserve(config.expected_events);
    m_stop_index.reserve(config.expected_orders);
}

OrderId MatchingEngine::submit(const NewOrder& order) {
    const OrderId id{m_next_order_id++};
    m_commands.push_back(Command{.kind = CommandKind::kNew, .id = id, .order = order});
    return id;
}

void MatchingEngine::cancel(AccountId requester, OrderId id) {
    m_commands.push_back(Command{.kind = CommandKind::kCancel, .id = id, .requester = requester});
}

void MatchingEngine::modify(AccountId requester, OrderId id, PriceTicks new_limit_price,
                            Qty new_qty) {
    m_commands.push_back(Command{.kind = CommandKind::kModify,
                                 .id = id,
                                 .requester = requester,
                                 .new_limit_price = new_limit_price,
                                 .new_qty = new_qty});
}

bool MatchingEngine::step_event() {
    [[maybe_unused]] const std::size_t events_before = m_events.size();
    if (m_active) {
        advance_active(*m_active);
    } else if (!m_triggered.empty()) {
        activate_triggered_stop();
    } else if (!m_commands.empty()) {
        const Command command = m_commands.front();
        m_commands.pop_front();
        process_command(command);
    } else {
        return false;
    }
    assert(m_events.size() > events_before && "every step must emit at least one event");
    assert(m_events.size() - events_before <= kMaxEventsPerStep && "step emitted too many events");
    assert(!m_book.is_crossed() && "book must never be crossed between steps");
    return true;
}

void MatchingEngine::step_tick() {
    while (step_event()) {
    }
    advance_tick();
}

void MatchingEngine::advance_tick() {
    assert(idle() && "advance_tick requires all queued work to be finished");
    m_previous_tick_volume = m_tick_volume;
    m_tick_volume = Qty{0};
    m_tick += Tick{1};
}

bool MatchingEngine::idle() const noexcept {
    return !m_active && m_triggered.empty() && m_commands.empty();
}

std::optional<PriceTicks> MatchingEngine::best_bid() const {
    return m_book.best_price(Side::kBuy);
}

std::optional<PriceTicks> MatchingEngine::best_ask() const {
    return m_book.best_price(Side::kSell);
}

std::size_t MatchingEngine::depth(Side side, std::span<DepthLevel> out) const {
    return m_book.depth(side, out);
}

std::optional<OrderView> MatchingEngine::find_order(OrderId id) const {
    if (const std::optional<RestingOrder> resting = m_book.find(id)) {
        return OrderView{.id = resting->id,
                         .account = resting->account,
                         .side = resting->side,
                         .type = resting->type,
                         .status = OrderStatus::kResting,
                         .limit_price = resting->price,
                         .remaining = resting->remaining,
                         .filled = resting->filled};
    }
    if (m_active && m_active->id == id) {
        return OrderView{.id = m_active->id,
                         .account = m_active->account,
                         .side = m_active->side,
                         .type = m_active->type,
                         .status = OrderStatus::kMatching,
                         .limit_price = m_active->limit_price,
                         .stop_price = m_active->stop_price,
                         .remaining = m_active->remaining,
                         .filled = m_active->filled};
    }
    const auto make_stop_view = [](const StopOrder& stop, OrderStatus status) {
        return OrderView{.id = stop.id,
                         .account = stop.account,
                         .side = stop.side,
                         .type = stop.type,
                         .status = status,
                         .limit_price = stop.limit_price,
                         .stop_price = stop.stop_price,
                         .remaining = stop.qty,
                         .filled = Qty{0}};
    };
    if (const auto it = m_stop_index.find(id); it != m_stop_index.end()) {
        const auto& [side, key] = it->second;
        const StopOrder& stop = side == Side::kBuy ? m_buy_stops.at(key) : m_sell_stops.at(key);
        return make_stop_view(stop, OrderStatus::kPendingStop);
    }
    const auto triggered = std::ranges::find(m_triggered, id, &StopOrder::id);
    if (triggered != m_triggered.end()) {
        return make_stop_view(*triggered, OrderStatus::kTriggered);
    }
    return std::nullopt;
}

Event& MatchingEngine::emit(EventType type, Reason reason) {
    Event& event = m_events.emplace_back();
    event.seq = SequenceNumber{m_next_sequence++};
    event.tick = m_tick;
    event.type = type;
    event.reason = reason;
    return event;
}

void MatchingEngine::describe(Event& event, const ActiveOrder& order) noexcept {
    event.order_id = order.id;
    event.account = order.account;
    event.side = order.side;
    event.order_type = order.type;
    event.price = order.limit_price;
    event.aux_price = order.stop_price;
    event.remaining = order.remaining;
}

void MatchingEngine::describe(Event& event, const StopOrder& order) noexcept {
    event.order_id = order.id;
    event.account = order.account;
    event.side = order.side;
    event.order_type = order.type;
    event.price = order.limit_price;
    event.aux_price = order.stop_price;
    event.qty = order.qty;
    event.remaining = order.qty;
}

void MatchingEngine::describe(Event& event, const RestingOrder& order) noexcept {
    event.order_id = order.id;
    event.account = order.account;
    event.side = order.side;
    event.order_type = order.type;
    event.price = order.price;
    event.remaining = order.remaining;
}

Reason MatchingEngine::validate(const NewOrder& order) noexcept {
    if (order.qty <= Qty{0} || order.qty > kMaxOrderQty) {
        return Reason::kInvalidQuantity;
    }
    if (has_limit_price(order.type) &&
        (order.limit_price <= PriceTicks{0} || order.limit_price > kMaxPrice)) {
        return Reason::kInvalidLimitPrice;
    }
    if (has_stop_price(order.type) &&
        (order.stop_price <= PriceTicks{0} || order.stop_price > kMaxPrice)) {
        return Reason::kInvalidStopPrice;
    }
    return Reason::kNone;
}

void MatchingEngine::process_command(const Command& command) {
    switch (command.kind) {
    case CommandKind::kNew:
        process_new(command);
        return;
    case CommandKind::kCancel:
        process_cancel(command);
        return;
    case CommandKind::kModify:
        process_modify(command);
        return;
    }
}

void MatchingEngine::process_new(const Command& command) {
    const NewOrder& order = command.order;
    // Unused price fields are normalized to zero so events never carry stale values.
    const PriceTicks limit_price = has_limit_price(order.type) ? order.limit_price : PriceTicks{};
    const PriceTicks stop_price = has_stop_price(order.type) ? order.stop_price : PriceTicks{};

    const Reason invalid = validate(order);
    const Reason reason = invalid == Reason::kNone ? Reason::kOrderAccepted : invalid;
    Event& event = emit(
        invalid == Reason::kNone ? EventType::kOrderPlaced : EventType::kOrderRejected, reason);
    event.order_id = command.id;
    event.account = order.account;
    event.side = order.side;
    event.order_type = order.type;
    event.price = limit_price;
    event.aux_price = stop_price;
    event.qty = order.qty;
    event.remaining = invalid == Reason::kNone ? order.qty : Qty{0};
    if (invalid != Reason::kNone) {
        return;
    }

    if (!has_stop_price(order.type)) {
        m_active = ActiveOrder{.id = command.id,
                               .account = order.account,
                               .side = order.side,
                               .type = order.type,
                               .limit_price = limit_price,
                               .remaining = order.qty};
        return;
    }

    const StopOrder stop{.id = command.id,
                         .account = order.account,
                         .side = order.side,
                         .type = order.type,
                         .limit_price = limit_price,
                         .stop_price = stop_price,
                         .qty = order.qty};
    const StopKey key{.stop_price = stop_price, .sequence = m_next_stop_sequence++};
    if (order.side == Side::kBuy) {
        m_buy_stops.emplace(key, stop);
    } else {
        m_sell_stops.emplace(key, stop);
    }
    m_stop_index.emplace(command.id, std::pair{order.side, key});
    // A stop whose price has already been reached triggers right away.
    collect_triggered_stops();
}

void MatchingEngine::process_cancel(const Command& command) {
    const auto reject = [&](Reason reason) {
        Event& event = emit(EventType::kCommandRejected, reason);
        event.order_id = command.id;
        event.account = command.requester;
    };

    if (const std::optional<RestingOrder> resting = m_book.find(command.id)) {
        if (resting->account != command.requester) {
            reject(Reason::kNotOrderOwner);
            return;
        }
        m_book.remove(command.id);
        Event& event = emit(EventType::kOrderCancelled, Reason::kCancelledByOwner);
        describe(event, *resting);
        event.qty = resting->remaining;
        event.remaining = Qty{0};
        return;
    }

    if (const auto it = m_stop_index.find(command.id); it != m_stop_index.end()) {
        const auto [side, key] = it->second;
        const StopOrder stop = side == Side::kBuy ? m_buy_stops.at(key) : m_sell_stops.at(key);
        if (stop.account != command.requester) {
            reject(Reason::kNotOrderOwner);
            return;
        }
        if (side == Side::kBuy) {
            m_buy_stops.erase(key);
        } else {
            m_sell_stops.erase(key);
        }
        m_stop_index.erase(it);
        Event& event = emit(EventType::kOrderCancelled, Reason::kCancelledByOwner);
        describe(event, stop);
        event.remaining = Qty{0};
        return;
    }

    reject(Reason::kUnknownOrder);
}

void MatchingEngine::process_modify(const Command& command) {
    const auto reject = [&](Reason reason) {
        Event& event = emit(EventType::kCommandRejected, reason);
        event.order_id = command.id;
        event.account = command.requester;
        event.price = command.new_limit_price;
        event.qty = command.new_qty;
    };

    const std::optional<RestingOrder> resting = m_book.find(command.id);
    if (!resting) {
        reject(m_stop_index.contains(command.id) ? Reason::kNotModifiable : Reason::kUnknownOrder);
        return;
    }
    if (resting->account != command.requester) {
        reject(Reason::kNotOrderOwner);
        return;
    }
    if (command.new_qty <= Qty{0} || command.new_qty > kMaxOrderQty) {
        reject(Reason::kInvalidQuantity);
        return;
    }
    if (command.new_limit_price <= PriceTicks{0} || command.new_limit_price > kMaxPrice) {
        reject(Reason::kInvalidLimitPrice);
        return;
    }

    const bool keeps_priority =
        command.new_limit_price == resting->price && command.new_qty <= resting->remaining;
    if (keeps_priority) {
        if (command.new_qty < resting->remaining) {
            m_book.reduce(command.id, command.new_qty);
        }
        Event& event = emit(EventType::kOrderModified, Reason::kModifiedKeptPriority);
        describe(event, *resting);
        event.aux_price = resting->price;
        event.qty = resting->remaining;
        event.remaining = command.new_qty;
        return;
    }

    m_book.remove(command.id);
    Event& event = emit(EventType::kOrderModified, Reason::kModifiedLostPriority);
    describe(event, *resting);
    event.price = command.new_limit_price;
    event.aux_price = resting->price;
    event.qty = resting->remaining;
    event.remaining = command.new_qty;
    m_active = ActiveOrder{.id = resting->id,
                           .account = resting->account,
                           .side = resting->side,
                           .type = resting->type,
                           .limit_price = command.new_limit_price,
                           .remaining = command.new_qty,
                           .filled = resting->filled};
}

bool MatchingEngine::crosses(const ActiveOrder& order, PriceTicks maker_price) noexcept {
    if (!has_limit_price(order.type)) {
        return true;
    }
    return order.side == Side::kBuy ? maker_price <= order.limit_price
                                    : maker_price >= order.limit_price;
}

void MatchingEngine::advance_active(ActiveOrder& active) {
    const std::optional<RestingOrder> maker = m_book.front(opposite(active.side));
    if (maker && crosses(active, maker->price)) {
        execute_match(active, *maker);
        if (active.remaining == Qty{0}) {
            m_active.reset(); // `active` is dangling from here on
            collect_triggered_stops();
        }
        return;
    }
    finish_active(active);
}

void MatchingEngine::execute_match(ActiveOrder& taker, const RestingOrder& maker) {
    const Qty qty = std::min(taker.remaining, maker.remaining);
    const PriceTicks price = maker.price;
    const std::optional<RestingOrder> maker_after = m_book.fill_front(maker.side, qty);
    assert(maker_after && maker_after->id == maker.id);
    taker.remaining -= qty;
    taker.filled += qty;
    m_total_volume += qty;
    m_tick_volume += qty;
    ++m_trade_count;

    Event& match = emit(EventType::kMatch, Reason::kTrade);
    match.order_id = taker.id;
    match.counter_order_id = maker.id;
    match.account = taker.account;
    match.counter_account = maker.account;
    match.side = taker.side;
    match.order_type = taker.type;
    match.price = price;
    match.qty = qty;

    Event& taker_fill = emit(EventType::kFill, Reason::kTakerFill);
    describe(taker_fill, taker);
    taker_fill.counter_order_id = maker.id;
    taker_fill.counter_account = maker.account;
    taker_fill.price = price;
    taker_fill.aux_price = PriceTicks{};
    taker_fill.qty = qty;

    Event& maker_fill = emit(EventType::kFill, Reason::kMakerFill);
    describe(maker_fill, *maker_after);
    maker_fill.counter_order_id = taker.id;
    maker_fill.counter_account = taker.account;
    maker_fill.qty = qty;

    if (m_last_price != price) {
        Event& change = emit(EventType::kPriceChange, Reason::kLastTradePrice);
        change.price = price;
        change.aux_price = m_last_price.value_or(PriceTicks{});
        change.order_id = taker.id;
        change.counter_order_id = maker.id;
        m_last_price = price;
    }
}

// Takes the order by value: it is copied out of m_active before m_active is reset.
void MatchingEngine::finish_active(const ActiveOrder order) {
    m_active.reset();
    if (has_limit_price(order.type)) {
        [[maybe_unused]] const bool added = m_book.add(RestingOrder{.id = order.id,
                                                                    .account = order.account,
                                                                    .side = order.side,
                                                                    .type = order.type,
                                                                    .price = order.limit_price,
                                                                    .remaining = order.remaining,
                                                                    .filled = order.filled});
        assert(added);
        Event& event = emit(EventType::kOrderRested, Reason::kRestingOnBook);
        describe(event, order);
        event.aux_price = PriceTicks{};
        event.qty = order.remaining;
    } else {
        Event& event = emit(EventType::kOrderCancelled, Reason::kNoLiquidity);
        describe(event, order);
        event.qty = order.remaining;
        event.remaining = Qty{0};
    }
    collect_triggered_stops();
}

void MatchingEngine::activate_triggered_stop() {
    const StopOrder stop = m_triggered.front();
    m_triggered.pop_front();
    assert(m_last_price);
    Event& event = emit(EventType::kStopTriggered, Reason::kStopPriceReached);
    describe(event, stop);
    // For a trigger, `price` is the last trade price that fired it; the stop's own limit
    // price is visible again once it rests (OrderRested).
    event.price = m_last_price.value_or(PriceTicks{});
    m_active = ActiveOrder{.id = stop.id,
                           .account = stop.account,
                           .side = stop.side,
                           .type = stop.type,
                           .limit_price = stop.limit_price,
                           .stop_price = stop.stop_price,
                           .remaining = stop.qty};
}

void MatchingEngine::collect_triggered_stops() {
    if (!m_last_price) {
        return;
    }
    const PriceTicks last = *m_last_price;
    while (!m_buy_stops.empty() && m_buy_stops.begin()->first.stop_price <= last) {
        const auto it = m_buy_stops.begin();
        m_triggered.push_back(it->second);
        m_stop_index.erase(it->second.id);
        m_buy_stops.erase(it);
    }
    while (!m_sell_stops.empty() && m_sell_stops.begin()->first.stop_price >= last) {
        const auto it = m_sell_stops.begin();
        m_triggered.push_back(it->second);
        m_stop_index.erase(it->second.id);
        m_sell_stops.erase(it);
    }
}

} // namespace stocksim
