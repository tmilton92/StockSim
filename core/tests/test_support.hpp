#pragma once

// Shared helpers for core tests: readable Catch2 output for strong types and enums, order
// builders, and event filters.

#include "stocksim/event.hpp"
#include "stocksim/matching_engine.hpp"
#include "stocksim/order.hpp"
#include "stocksim/strong_type.hpp"
#include "stocksim/types.hpp"

#include <catch2/catch_tostring.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Catch {

template <typename Tag, typename Rep>
struct StringMaker<stocksim::StrongId<Tag, Rep>> {
    static std::string convert(const stocksim::StrongId<Tag, Rep>& id) {
        return std::to_string(id.value());
    }
};

template <typename Tag, typename Rep>
struct StringMaker<stocksim::StrongQuantity<Tag, Rep>> {
    static std::string convert(const stocksim::StrongQuantity<Tag, Rep>& quantity) {
        return std::to_string(quantity.value());
    }
};

template <>
struct StringMaker<stocksim::EventType> {
    static std::string convert(stocksim::EventType type) {
        return std::string{stocksim::to_string(type)};
    }
};

template <>
struct StringMaker<stocksim::Side> {
    static std::string convert(stocksim::Side side) {
        return std::string{stocksim::to_string(side)};
    }
};

template <>
struct StringMaker<stocksim::OrderType> {
    static std::string convert(stocksim::OrderType type) {
        return std::string{stocksim::to_string(type)};
    }
};

template <>
struct StringMaker<stocksim::OrderStatus> {
    static std::string convert(stocksim::OrderStatus status) {
        return std::string{stocksim::to_string(status)};
    }
};

template <>
struct StringMaker<stocksim::Reason> {
    static std::string convert(stocksim::Reason reason) {
        return "Reason(" + std::to_string(static_cast<int>(reason)) + ")";
    }
};

} // namespace Catch

namespace stocksim::test {

inline constexpr AccountId kAlice{1};
inline constexpr AccountId kBob{2};
inline constexpr AccountId kCarol{3};
inline constexpr AccountId kDave{4};

inline NewOrder limit(AccountId account, Side side, std::int64_t price, std::int64_t qty) {
    return NewOrder{.account = account,
                    .side = side,
                    .type = OrderType::kLimit,
                    .qty = Qty{qty},
                    .limit_price = PriceTicks{price}};
}

inline NewOrder market(AccountId account, Side side, std::int64_t qty) {
    return NewOrder{.account = account, .side = side, .type = OrderType::kMarket, .qty = Qty{qty}};
}

inline NewOrder stop(AccountId account, Side side, std::int64_t stop_price, std::int64_t qty) {
    return NewOrder{.account = account,
                    .side = side,
                    .type = OrderType::kStop,
                    .qty = Qty{qty},
                    .stop_price = PriceTicks{stop_price}};
}

inline NewOrder stop_limit(AccountId account, Side side, std::int64_t stop_price,
                           std::int64_t limit_price, std::int64_t qty) {
    return NewOrder{.account = account,
                    .side = side,
                    .type = OrderType::kStopLimit,
                    .qty = Qty{qty},
                    .limit_price = PriceTicks{limit_price},
                    .stop_price = PriceTicks{stop_price}};
}

/// Runs the engine to idle (one tick) and returns, then clears, everything it emitted.
inline std::vector<Event> run_tick(MatchingEngine& engine) {
    engine.step_tick();
    std::vector<Event> events{engine.events().begin(), engine.events().end()};
    engine.clear_events();
    return events;
}

/// Performs one step and returns, then clears, the events it emitted.
inline std::vector<Event> run_step(MatchingEngine& engine) {
    engine.step_event();
    std::vector<Event> events{engine.events().begin(), engine.events().end()};
    engine.clear_events();
    return events;
}

inline std::vector<EventType> types_of(std::span<const Event> events) {
    std::vector<EventType> types;
    types.reserve(events.size());
    for (const Event& event : events) {
        types.push_back(event.type);
    }
    return types;
}

inline std::vector<Event> of_type(std::span<const Event> events, EventType type) {
    std::vector<Event> selected;
    for (const Event& event : events) {
        if (event.type == type) {
            selected.push_back(event);
        }
    }
    return selected;
}

} // namespace stocksim::test
