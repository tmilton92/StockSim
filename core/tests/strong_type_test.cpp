#include "stocksim/strong_type.hpp"
#include "stocksim/types.hpp"

#include <catch2/catch_test_macros.hpp>

#include <type_traits>
#include <unordered_set>

using namespace stocksim;

// Distinct id and unit types must not convert into each other.
static_assert(!std::is_convertible_v<AccountId, OwnerId>);
static_assert(!std::is_convertible_v<OrderId, AccountId>);
static_assert(!std::is_convertible_v<PriceTicks, Qty>);
static_assert(!std::is_convertible_v<std::int64_t, Qty>);
static_assert(!std::is_constructible_v<Qty, PriceTicks>);
static_assert(std::is_trivially_copyable_v<PriceTicks>);
static_assert(sizeof(PriceTicks) == sizeof(std::int64_t));

TEST_CASE("strong quantities add, subtract and compare within their type", "[types]") {
    Qty qty{10};
    qty += Qty{5};
    CHECK(qty == Qty{15});
    CHECK(qty - Qty{20} == Qty{-5});
    CHECK(-qty == Qty{-15});
    CHECK(Qty{1} < Qty{2});
    CHECK(PriceTicks{100} + PriceTicks{1} == PriceTicks{101});
    CHECK(Qty{} == Qty{0});
}

TEST_CASE("strong ids compare and hash by value", "[types]") {
    CHECK(OrderId{3} == OrderId{3});
    CHECK(OrderId{3} < OrderId{4});
    const std::unordered_set<OrderId, StrongHash> ids{OrderId{1}, OrderId{2}, OrderId{1}};
    CHECK(ids.size() == 2);
    CHECK(kInvalidOrderId == OrderId{0});
}
