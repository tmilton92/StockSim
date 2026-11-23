#pragma once

#include "stocksim/order.hpp"
#include "stocksim/strong_type.hpp"
#include "stocksim/types.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace stocksim {

/// An order resting on the book.
struct RestingOrder {
    OrderId id{};
    AccountId account{};
    Side side = Side::kBuy;
    /// kLimit, or kStopLimit once triggered.
    OrderType type = OrderType::kLimit;
    PriceTicks price{};
    Qty remaining{};
    Qty filled{};
};

/// Limit order book with price-time priority.
///
/// Pure data structure: no events, no matching decisions (see `MatchingEngine`). Within a
/// price level orders are kept in arrival order in an intrusive list over a node pool, so
/// add/cancel/fill are O(log levels) with no per-order allocation once the pool has grown.
class OrderBook {
public:
    explicit OrderBook(std::size_t expected_orders = 0);

    /// Appends `order` at the back of its price level (lowest time priority).
    /// Returns false (and changes nothing) if the id is already present or remaining <= 0.
    bool add(const RestingOrder& order);

    /// Removes an order, returning its final state, or nullopt if absent.
    std::optional<RestingOrder> remove(OrderId id);

    /// Reduces a resting order's quantity in place, keeping its time priority.
    /// Requires 0 < new_remaining < current remaining; returns false otherwise.
    bool reduce(OrderId id, Qty new_remaining);

    /// Executes `qty` against the highest-priority order on `side` and returns its state after
    /// the fill. The order is removed when its remaining quantity reaches zero.
    /// Requires 0 < qty <= that order's remaining quantity.
    std::optional<RestingOrder> fill_front(Side side, Qty qty);

    [[nodiscard]] std::optional<RestingOrder> find(OrderId id) const;

    /// Highest-priority order on `side` (best price, earliest arrival).
    [[nodiscard]] std::optional<RestingOrder> front(Side side) const;

    [[nodiscard]] std::optional<PriceTicks> best_price(Side side) const;

    /// Fills `out` with up to `out.size()` levels from the best price outward; returns the
    /// number written.
    [[nodiscard]] std::size_t depth(Side side, std::span<DepthLevel> out) const;

    [[nodiscard]] Qty total_quantity(Side side) const noexcept;
    [[nodiscard]] std::size_t order_count() const noexcept { return m_index.size(); }
    [[nodiscard]] std::size_t level_count(Side side) const noexcept;
    [[nodiscard]] bool empty() const noexcept { return m_index.empty(); }

    /// True if best bid >= best ask. Never true after the matching engine finishes a step.
    [[nodiscard]] bool is_crossed() const;

    /// Calls `visit(const RestingOrder&)` for every order on `side` in priority order: best
    /// price first, then arrival order within a price. Re-adding orders in this order rebuilds
    /// an equivalent book (used by serialization).
    template <typename Visitor>
    void for_each_in_priority(Side side, const Visitor& visit) const {
        const auto visit_level = [&](const Level& level) {
            for (std::uint32_t index = level.head; index != kNil; index = m_nodes[index].next) {
                visit(m_nodes[index].order);
            }
        };
        const LevelMap& map = levels(side);
        if (side == Side::kBuy) {
            for (auto it = map.rbegin(); it != map.rend(); ++it) {
                visit_level(it->second);
            }
        } else {
            for (const auto& entry : map) {
                visit_level(entry.second);
            }
        }
    }

private:
    static constexpr std::uint32_t kNil = std::numeric_limits<std::uint32_t>::max();

    struct Node {
        RestingOrder order{};
        std::uint32_t prev = kNil;
        std::uint32_t next = kNil;
    };

    struct Level {
        std::uint32_t head = kNil;
        std::uint32_t tail = kNil;
        Qty total{};
        std::uint32_t count = 0;
    };

    /// Both sides are keyed ascending; bids are read from the back (highest first).
    using LevelMap = std::map<PriceTicks, Level>;

    [[nodiscard]] LevelMap& levels(Side side) noexcept;
    [[nodiscard]] const LevelMap& levels(Side side) const noexcept;
    [[nodiscard]] const Level* best_level(Side side) const;

    std::uint32_t allocate(const RestingOrder& order);
    /// Unlinks a node from its level (erasing the level if it empties) and frees it.
    void erase_node(std::uint32_t index);

    std::vector<Node> m_nodes{};
    std::vector<std::uint32_t> m_free{};
    /// Lookup only; never iterated.
    std::unordered_map<OrderId, std::uint32_t, StrongHash> m_index{};
    LevelMap m_bids{};
    LevelMap m_asks{};
    Qty m_bid_total{};
    Qty m_ask_total{};
};

} // namespace stocksim
