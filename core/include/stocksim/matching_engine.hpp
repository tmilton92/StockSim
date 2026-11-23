#pragma once

#include "stocksim/account_ledger.hpp"
#include "stocksim/event.hpp"
#include "stocksim/order.hpp"
#include "stocksim/order_book.hpp"
#include "stocksim/result.hpp"
#include "stocksim/serialization.hpp"
#include "stocksim/strong_type.hpp"
#include "stocksim/types.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace stocksim {

/// Upper bound on the events emitted by one `MatchingEngine::step_event()` (a match emits
/// Match, Fill, Fill, PriceChange). Lets a producer reserve buffer space before stepping.
inline constexpr std::size_t kMaxEventsPerStep = 4;

struct EngineConfig {
    /// Capacity hints; the engine still grows beyond them.
    std::size_t expected_orders = 1024;
    std::size_t expected_events = 4096;
};

/// Single-instrument matching engine: price-time priority, market/limit/stop/stop-limit
/// orders, cancel and modify, all driven by explicit steps.
///
/// Requests (`submit`, `cancel`, `modify`) are queued, never applied immediately. Work is then
/// performed one *step* at a time; each step is atomic and emits at least one event:
///   - accept/reject one request (OrderPlaced, OrderRejected, OrderCancelled, ...);
///   - one execution of the active order (Match, Fill x2, and PriceChange if the price moved);
///   - rest or cancel the active order's remainder (OrderRested / OrderCancelled);
///   - activate one triggered stop (StopTriggered).
/// Engine state after a step is always consistent with the events emitted so far, which is
/// what lets the UI step through a sweep one fill at a time.
///
/// Ordering rules (all deterministic):
///   - Requests are processed in arrival order.
///   - Executions happen at the resting (maker) order's price, best price first, then
///     earliest arrival within a price.
///   - Stops are checked after the active order finishes (not mid-sweep). Triggered stops are
///     queued buy-side first (lowest stop price first), then sell-side (highest first), ties
///     by placement order, and all run before the next request.
///   - Self-matching is allowed: self-trade prevention is a safeguard (TODO §11), and wash
///     trading (TODO §8) depends on it being possible.
class MatchingEngine {
public:
    explicit MatchingEngine(const EngineConfig& config = {});

    /// Queues a new order and returns the id it will carry in every event. The order is
    /// validated when processed (OrderPlaced or OrderRejected).
    OrderId submit(const NewOrder& order);

    /// Queues a cancel of a resting or pending-stop order owned by `requester`.
    void cancel(AccountId requester, OrderId id);

    /// Queues a modify of a resting order owned by `requester`. Reducing quantity at the same
    /// price keeps time priority; any price change or quantity increase re-queues the order
    /// at the back (and it may trade immediately if the new price crosses).
    void modify(AccountId requester, OrderId id, PriceTicks new_limit_price, Qty new_qty);

    /// Performs one step. Returns false (and emits nothing) when there is no work.
    bool step_event();

    /// Runs steps until idle, then advances to the next tick.
    void step_tick();

    /// Advances to the next tick without performing any steps. Requires `idle()`.
    void advance_tick();

    /// True when no requests, active order, or triggered stops are waiting.
    [[nodiscard]] bool idle() const noexcept;

    [[nodiscard]] Tick current_tick() const noexcept { return m_tick; }
    [[nodiscard]] std::optional<PriceTicks> last_price() const noexcept { return m_last_price; }
    [[nodiscard]] std::optional<PriceTicks> best_bid() const;
    [[nodiscard]] std::optional<PriceTicks> best_ask() const;

    /// Cumulative traded quantity since construction.
    [[nodiscard]] Qty total_volume() const noexcept { return m_total_volume; }
    /// Traded quantity during the current tick.
    [[nodiscard]] Qty tick_volume() const noexcept { return m_tick_volume; }
    /// Traded quantity during the previous (completed) tick.
    [[nodiscard]] Qty previous_tick_volume() const noexcept { return m_previous_tick_volume; }
    [[nodiscard]] std::uint64_t trade_count() const noexcept { return m_trade_count; }
    /// Quantity bought / sold by takers (aggressors) during the previous tick: the crowd's
    /// net demand, which herding traders react to.
    [[nodiscard]] Qty previous_tick_taker_buy_volume() const noexcept {
        return m_previous_taker_buy;
    }
    [[nodiscard]] Qty previous_tick_taker_sell_volume() const noexcept {
        return m_previous_taker_sell;
    }

    /// Top `out.size()` price levels on `side`, best first. Returns the number written.
    [[nodiscard]] std::size_t depth(Side side, std::span<DepthLevel> out) const;

    [[nodiscard]] std::optional<OrderView> find_order(OrderId id) const;
    [[nodiscard]] const OrderBook& book() const noexcept { return m_book; }

    /// Opens a limited account (see AccountLedger for the rules). Returns false if it already
    /// exists (opened or traded) or the values are invalid.
    bool open_account(AccountId account, Money cash, Qty shares, std::int32_t leverage_percent);
    [[nodiscard]] std::optional<AccountView> account(AccountId id) const {
        return m_ledger.find(id);
    }
    [[nodiscard]] const AccountLedger& ledger() const noexcept { return m_ledger; }

    /// Events emitted since the last `clear_events()`, oldest first.
    [[nodiscard]] std::span<const Event> events() const noexcept { return m_events; }
    /// Discards drained events. Capacity is kept, so steady-state stepping does not allocate.
    void clear_events() noexcept { m_events.clear(); }

    /// Sequence number of the most recent event emitted (0 before the first event).
    [[nodiscard]] SequenceNumber last_sequence() const noexcept {
        return SequenceNumber{m_next_sequence - 1};
    }

    /// Writes the complete engine state (book, pending stops, queued requests, the active
    /// order, counters, market data). Undrained events are not state and are not written.
    void save_state(StateWriter& out) const;

    /// Rebuilds an engine from `save_state` output. The restored engine produces exactly the
    /// same future events as the original given the same inputs. Fails with kCorruptState on
    /// truncated or inconsistent data.
    [[nodiscard]] static Result<MatchingEngine> load_state(StateReader& in,
                                                           const EngineConfig& config = {});

private:
    enum class CommandKind : std::uint8_t { kNew, kCancel, kModify };

    struct Command {
        CommandKind kind = CommandKind::kNew;
        OrderId id{};
        NewOrder order{};
        AccountId requester{};
        PriceTicks new_limit_price{};
        Qty new_qty{};
    };

    /// The order currently taking liquidity.
    struct ActiveOrder {
        OrderId id{};
        AccountId account{};
        Side side = Side::kBuy;
        OrderType type = OrderType::kMarket;
        PriceTicks limit_price{};
        PriceTicks stop_price{};
        Qty remaining{};
        Qty filled{};
    };

    struct StopOrder {
        OrderId id{};
        AccountId account{};
        Side side = Side::kBuy;
        OrderType type = OrderType::kStop;
        PriceTicks limit_price{};
        PriceTicks stop_price{};
        Qty qty{};
    };

    struct StopKey {
        PriceTicks stop_price{};
        std::uint64_t sequence = 0;
    };

    /// Buy stops trigger as the price rises: lowest stop first.
    struct BuyStopOrder {
        bool operator()(const StopKey& lhs, const StopKey& rhs) const noexcept {
            return lhs.stop_price != rhs.stop_price ? lhs.stop_price < rhs.stop_price
                                                    : lhs.sequence < rhs.sequence;
        }
    };

    /// Sell stops trigger as the price falls: highest stop first.
    struct SellStopOrder {
        bool operator()(const StopKey& lhs, const StopKey& rhs) const noexcept {
            return lhs.stop_price != rhs.stop_price ? lhs.stop_price > rhs.stop_price
                                                    : lhs.sequence < rhs.sequence;
        }
    };

    Event& emit(EventType type, Reason reason);
    static void describe(Event& event, const ActiveOrder& order) noexcept;
    static void describe(Event& event, const StopOrder& order) noexcept;
    static void describe(Event& event, const RestingOrder& order) noexcept;
    [[nodiscard]] static Reason validate(const NewOrder& order) noexcept;

    void process_command(const Command& command);
    void process_new(const Command& command);
    void process_cancel(const Command& command);
    void process_modify(const Command& command);
    void advance_active(ActiveOrder& active);
    void execute_match(ActiveOrder& taker, const RestingOrder& maker);
    void finish_active(ActiveOrder order);
    void activate_triggered_stop();
    void collect_triggered_stops();
    [[nodiscard]] static bool crosses(const ActiveOrder& order, PriceTicks maker_price) noexcept;

    /// Account check for an order about to become active (market, limit, or triggered stop).
    [[nodiscard]] Reason check_active_order(AccountId account, Side side, OrderType type,
                                            PriceTicks limit_price, Qty qty) const;
    /// Reserves / releases what a resting limit order commits in its account.
    void reserve_resting(const RestingOrder& order, Qty qty);
    void release_resting(const RestingOrder& order, Qty qty);

    // State loading, one section each (see save_state for the layout).
    void load_counters(StateReader& in);
    void load_book(StateReader& in);
    void load_active(StateReader& in);
    void load_stops(StateReader& in);
    void load_commands(StateReader& in);
    [[nodiscard]] static StopOrder read_stop(StateReader& in);

    OrderBook m_book{};
    AccountLedger m_ledger{};
    std::deque<Command> m_commands{};
    std::optional<ActiveOrder> m_active{};
    std::deque<StopOrder> m_triggered{};
    std::map<StopKey, StopOrder, BuyStopOrder> m_buy_stops{};
    std::map<StopKey, StopOrder, SellStopOrder> m_sell_stops{};
    /// Lookup only; never iterated.
    std::unordered_map<OrderId, std::pair<Side, StopKey>, StrongHash> m_stop_index{};
    std::vector<Event> m_events{};

    Tick m_tick{};
    std::uint64_t m_next_order_id = 1;
    std::uint64_t m_next_sequence = 1;
    std::uint64_t m_next_stop_sequence = 1;
    std::optional<PriceTicks> m_last_price{};
    Qty m_total_volume{};
    Qty m_tick_volume{};
    Qty m_previous_tick_volume{};
    Qty m_taker_buy{};
    Qty m_taker_sell{};
    Qty m_previous_taker_buy{};
    Qty m_previous_taker_sell{};
    std::uint64_t m_trade_count = 0;
};

} // namespace stocksim
