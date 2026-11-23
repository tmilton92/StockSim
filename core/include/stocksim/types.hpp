#pragma once

#include "stocksim/strong_type.hpp"

#include <cstdint>

namespace stocksim {

/// A trading account. The player is an ordinary account (no special cases in core).
using AccountId = StrongId<struct AccountIdTag, std::uint32_t>;

/// The true (beneficial) owner of one or more accounts.
using OwnerId = StrongId<struct OwnerIdTag, std::uint32_t>;

/// Engine-assigned order identifier. Assigned sequentially from 1; 0 is never a valid order.
using OrderId = StrongId<struct OrderIdTag, std::uint64_t>;

/// Global, strictly increasing event sequence number.
using SequenceNumber = StrongId<struct SequenceNumberTag, std::uint64_t>;

/// Simulation time step. Additive so that tick arithmetic stays typed.
using Tick = StrongQuantity<struct TickTag, std::int64_t>;

/// Price as a fixed-point integer number of instrument ticks. Never floating point.
using PriceTicks = StrongQuantity<struct PriceTicksTag, std::int64_t>;

/// Quantity in the instrument's smallest tradable unit (lots/shares).
using Qty = StrongQuantity<struct QtyTag, std::int64_t>;

/// Never assigned to an order.
inline constexpr OrderId kInvalidOrderId{0};

/// Cash in the smallest currency unit. One price tick per lot is one unit of money, so the
/// value of a fill is price ticks x quantity ([notional]).
using Money = StrongQuantity<struct MoneyTag, std::int64_t>;

/// Upper bounds accepted for a single order. Their product (10^18) keeps any fill's notional
/// below the int64 limit (about 9.2 x 10^18).
inline constexpr PriceTicks kMaxPrice{1'000'000'000};
inline constexpr Qty kMaxOrderQty{1'000'000'000};

/// Value of [qty] at [price]. Within the order limits this cannot overflow.
[[nodiscard]] constexpr Money notional(PriceTicks price, Qty qty) noexcept {
    return Money{price.value() * qty.value()};
}

} // namespace stocksim
