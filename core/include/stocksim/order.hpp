#pragma once

#include "stocksim/types.hpp"

#include <cstdint>
#include <string_view>

namespace stocksim {

enum class Side : std::uint8_t {
    kBuy,
    kSell,
};

[[nodiscard]] constexpr Side opposite(Side side) noexcept {
    return side == Side::kBuy ? Side::kSell : Side::kBuy;
}

enum class OrderType : std::uint8_t {
    /// Executes immediately against the book; any unfilled remainder is cancelled.
    kMarket,
    /// Executes at `limit_price` or better; any unfilled remainder rests on the book.
    kLimit,
    /// Waits until the last trade price reaches `stop_price`, then becomes a market order.
    kStop,
    /// Waits until the last trade price reaches `stop_price`, then becomes a limit order at
    /// `limit_price`.
    kStopLimit,
};

/// True for order types that rest on the book at a limit price once active.
[[nodiscard]] constexpr bool has_limit_price(OrderType type) noexcept {
    return type == OrderType::kLimit || type == OrderType::kStopLimit;
}

/// True for order types that wait for a stop trigger before becoming active.
[[nodiscard]] constexpr bool has_stop_price(OrderType type) noexcept {
    return type == OrderType::kStop || type == OrderType::kStopLimit;
}

/// A client request to place an order. Fields not used by `type` are ignored.
struct NewOrder {
    AccountId account{};
    Side side = Side::kBuy;
    OrderType type = OrderType::kLimit;
    Qty qty{};
    /// Used by kLimit and kStopLimit.
    PriceTicks limit_price{};
    /// Used by kStop and kStopLimit. A buy stop triggers when last >= stop, a sell stop when
    /// last <= stop.
    PriceTicks stop_price{};
};

/// Where a live order currently is.
enum class OrderStatus : std::uint8_t {
    /// Waiting for its stop price; not on the book.
    kPendingStop,
    /// Stop price reached; queued to become active.
    kTriggered,
    /// Currently being matched (the incoming/aggressing order).
    kMatching,
    /// Resting on the book.
    kResting,
};

/// Read-only snapshot of a live order.
struct OrderView {
    OrderId id{};
    AccountId account{};
    Side side = Side::kBuy;
    OrderType type = OrderType::kLimit;
    OrderStatus status = OrderStatus::kResting;
    PriceTicks limit_price{};
    PriceTicks stop_price{};
    Qty remaining{};
    Qty filled{};
};

/// Aggregated quantity at one price level.
struct DepthLevel {
    PriceTicks price{};
    Qty qty{};
    std::uint32_t order_count = 0;
};

[[nodiscard]] std::string_view to_string(Side side) noexcept;
[[nodiscard]] std::string_view to_string(OrderType type) noexcept;
[[nodiscard]] std::string_view to_string(OrderStatus status) noexcept;

} // namespace stocksim
