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

/// Upper bounds accepted for a single order. They keep price x quantity and cumulative
/// volume far from int64 overflow.
inline constexpr PriceTicks kMaxPrice{1'000'000'000'000};
inline constexpr Qty kMaxOrderQty{1'000'000'000'000};

} // namespace stocksim
