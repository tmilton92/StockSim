#pragma once

#include "stocksim/order.hpp"
#include "stocksim/types.hpp"

#include <cstdint>
#include <string_view>
#include <type_traits>

namespace stocksim {

/// Version of the `Event` layout and semantics. Bump (with an ADR, see docs/adr/0005 and 0010)
/// whenever a field, type, or reason is added, removed, or reinterpreted.
inline constexpr std::uint32_t kEventSchemaVersion = 2;

enum class EventType : std::uint8_t {
    /// An order passed validation and was accepted.
    kOrderPlaced,
    /// An order failed validation; it never existed on the book.
    kOrderRejected,
    /// An order's unfilled remainder joined the book.
    kOrderRested,
    /// An order's remainder was removed (by its owner, or for lack of liquidity).
    kOrderCancelled,
    /// A resting order's price and/or quantity was changed by its owner.
    kOrderModified,
    /// A cancel or modify request could not be applied.
    kCommandRejected,
    /// A stop order's stop price was reached; it is about to become active.
    kStopTriggered,
    /// One execution between an aggressing (taker) and a resting (maker) order.
    kMatch,
    /// One side of a match, from one order's point of view. Every kMatch is followed by
    /// exactly two kFill events: taker first, then maker.
    kFill,
    /// The last trade price changed.
    kPriceChange,
};

/// Why an event happened. Each code has fixed human-readable text (`reason_text`) for the
/// narration layer, so emitting an event never allocates.
enum class Reason : std::uint8_t {
    kNone,
    kOrderAccepted,
    kInvalidQuantity,
    kInvalidLimitPrice,
    kInvalidStopPrice,
    kRestingOnBook,
    kCancelledByOwner,
    kNoLiquidity,
    kUnknownOrder,
    kNotOrderOwner,
    kNotModifiable,
    kModifiedKeptPriority,
    kModifiedLostPriority,
    kStopPriceReached,
    kTrade,
    kTakerFill,
    kMakerFill,
    kLastTradePrice,
    // Schema v2 (ADR 0010): account limits.
    kInsufficientFunds,
    kInsufficientShares,
};

/// What a broker meant to achieve with an action (TODO §9). Non-broker events use kNone.
enum class BrokerIntent : std::uint8_t {
    kNone,
};

/// One state change. Flat, trivially copyable, fixed size: suitable for a ring buffer and for
/// hashing in determinism tests. Field meaning per type is documented in docs/adr/0005.
struct Event {
    SequenceNumber seq{};
    Tick tick{};
    EventType type = EventType::kOrderPlaced;
    Reason reason = Reason::kNone;
    BrokerIntent intent = BrokerIntent::kNone;
    Side side = Side::kBuy;
    OrderType order_type = OrderType::kLimit;
    OrderId order_id{};
    OrderId counter_order_id{};
    AccountId account{};
    AccountId counter_account{};
    PriceTicks price{};
    PriceTicks aux_price{};
    Qty qty{};
    Qty remaining{};

    friend bool operator==(const Event&, const Event&) = default;
};

static_assert(std::is_trivially_copyable_v<Event>, "events are copied into ring buffers");

[[nodiscard]] std::string_view to_string(EventType type) noexcept;
[[nodiscard]] std::string_view to_string(BrokerIntent intent) noexcept;

/// Fixed human-readable explanation of a reason code.
[[nodiscard]] std::string_view reason_text(Reason reason) noexcept;

} // namespace stocksim
