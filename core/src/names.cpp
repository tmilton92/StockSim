#include "stocksim/event.hpp"
#include "stocksim/order.hpp"

#include <string_view>

namespace stocksim {

std::string_view to_string(Side side) noexcept {
    switch (side) {
    case Side::kBuy:
        return "buy";
    case Side::kSell:
        return "sell";
    }
    return "unknown";
}

std::string_view to_string(OrderType type) noexcept {
    switch (type) {
    case OrderType::kMarket:
        return "market";
    case OrderType::kLimit:
        return "limit";
    case OrderType::kStop:
        return "stop";
    case OrderType::kStopLimit:
        return "stop-limit";
    }
    return "unknown";
}

std::string_view to_string(OrderStatus status) noexcept {
    switch (status) {
    case OrderStatus::kPendingStop:
        return "pending-stop";
    case OrderStatus::kTriggered:
        return "triggered";
    case OrderStatus::kMatching:
        return "matching";
    case OrderStatus::kResting:
        return "resting";
    }
    return "unknown";
}

std::string_view to_string(EventType type) noexcept {
    switch (type) {
    case EventType::kOrderPlaced:
        return "OrderPlaced";
    case EventType::kOrderRejected:
        return "OrderRejected";
    case EventType::kOrderRested:
        return "OrderRested";
    case EventType::kOrderCancelled:
        return "OrderCancelled";
    case EventType::kOrderModified:
        return "OrderModified";
    case EventType::kCommandRejected:
        return "CommandRejected";
    case EventType::kStopTriggered:
        return "StopTriggered";
    case EventType::kMatch:
        return "Match";
    case EventType::kFill:
        return "Fill";
    case EventType::kPriceChange:
        return "PriceChange";
    }
    return "Unknown";
}

std::string_view to_string(BrokerIntent intent) noexcept {
    switch (intent) {
    case BrokerIntent::kNone:
        return "none";
    }
    return "unknown";
}

std::string_view reason_text(Reason reason) noexcept {
    switch (reason) {
    case Reason::kNone:
        return "";
    case Reason::kOrderAccepted:
        return "Order accepted by the exchange.";
    case Reason::kInvalidQuantity:
        return "Rejected: quantity must be positive and within the order size limit.";
    case Reason::kInvalidLimitPrice:
        return "Rejected: limit price must be positive and within the price limit.";
    case Reason::kInvalidStopPrice:
        return "Rejected: stop price must be positive and within the price limit.";
    case Reason::kRestingOnBook:
        return "Unfilled quantity now waits on the order book for a counterparty.";
    case Reason::kCancelledByOwner:
        return "Cancelled by the account that placed it.";
    case Reason::kNoLiquidity:
        return "Remaining quantity cancelled: no more orders on the other side to trade with.";
    case Reason::kUnknownOrder:
        return "No live order with this id.";
    case Reason::kNotOrderOwner:
        return "Only the account that placed an order may change or cancel it.";
    case Reason::kNotModifiable:
        return "Only orders resting on the book can be modified; cancel and re-place instead.";
    case Reason::kModifiedKeptPriority:
        return "Quantity reduced at the same price: the order keeps its place in the queue.";
    case Reason::kModifiedLostPriority:
        return "Price changed or quantity increased: the order goes to the back of the queue.";
    case Reason::kStopPriceReached:
        return "The last trade reached the stop price, so the stop order is now active.";
    case Reason::kTrade:
        return "An incoming order traded with an order resting on the book at the resting price.";
    case Reason::kTakerFill:
        return "Filled as the taker: this order crossed the spread and took liquidity.";
    case Reason::kMakerFill:
        return "Filled as the maker: this resting order provided the liquidity.";
    case Reason::kLastTradePrice:
        return "The market price is the price of the last trade, not a measure of value.";
    }
    return "Unknown reason.";
}

} // namespace stocksim
