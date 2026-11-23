#include "stocksim/order_book.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <optional>

using namespace stocksim;
using stocksim::test::kAlice;

namespace {

RestingOrder resting(std::uint64_t id, Side side, std::int64_t price, std::int64_t qty) {
    return RestingOrder{.id = OrderId{id},
                        .account = kAlice,
                        .side = side,
                        .price = PriceTicks{price},
                        .remaining = Qty{qty}};
}

} // namespace

TEST_CASE("empty book has no prices", "[order_book]") {
    const OrderBook book;
    CHECK(book.empty());
    CHECK_FALSE(book.best_price(Side::kBuy).has_value());
    CHECK_FALSE(book.best_price(Side::kSell).has_value());
    CHECK_FALSE(book.front(Side::kBuy).has_value());
    CHECK_FALSE(book.is_crossed());
}

TEST_CASE("best bid is the highest buy, best ask the lowest sell", "[order_book]") {
    OrderBook book;
    REQUIRE(book.add(resting(1, Side::kBuy, 99, 5)));
    REQUIRE(book.add(resting(2, Side::kBuy, 101, 5)));
    REQUIRE(book.add(resting(3, Side::kBuy, 100, 5)));
    REQUIRE(book.add(resting(4, Side::kSell, 105, 5)));
    REQUIRE(book.add(resting(5, Side::kSell, 103, 5)));
    CHECK(book.best_price(Side::kBuy) == PriceTicks{101});
    CHECK(book.best_price(Side::kSell) == PriceTicks{103});
    CHECK(book.front(Side::kBuy)->id == OrderId{2});
    CHECK(book.front(Side::kSell)->id == OrderId{5});
    CHECK(book.total_quantity(Side::kBuy) == Qty{15});
    CHECK(book.order_count() == 5);
    CHECK_FALSE(book.is_crossed());
}

TEST_CASE("orders at one price keep arrival order", "[order_book]") {
    OrderBook book;
    book.add(resting(1, Side::kSell, 100, 5));
    book.add(resting(2, Side::kSell, 100, 7));
    book.add(resting(3, Side::kSell, 100, 9));
    CHECK(book.front(Side::kSell)->id == OrderId{1});
    book.remove(OrderId{1});
    CHECK(book.front(Side::kSell)->id == OrderId{2});
    // Removing from the middle keeps the remaining order intact.
    book.add(resting(4, Side::kSell, 100, 1));
    book.remove(OrderId{3});
    CHECK(book.front(Side::kSell)->id == OrderId{2});
    book.remove(OrderId{2});
    CHECK(book.front(Side::kSell)->id == OrderId{4});
}

TEST_CASE("duplicate ids and non-positive quantities are refused", "[order_book]") {
    OrderBook book;
    REQUIRE(book.add(resting(1, Side::kBuy, 100, 5)));
    CHECK_FALSE(book.add(resting(1, Side::kBuy, 101, 5)));
    CHECK_FALSE(book.add(resting(2, Side::kBuy, 101, 0)));
    CHECK_FALSE(book.add(resting(3, Side::kBuy, 101, -1)));
    CHECK(book.order_count() == 1);
}

TEST_CASE("fill_front reduces then removes the front order", "[order_book]") {
    OrderBook book;
    book.add(resting(1, Side::kSell, 100, 10));
    book.add(resting(2, Side::kSell, 100, 4));

    const std::optional<RestingOrder> partial = book.fill_front(Side::kSell, Qty{6});
    REQUIRE(partial);
    CHECK(partial->remaining == Qty{4});
    CHECK(partial->filled == Qty{6});
    CHECK(book.front(Side::kSell)->id == OrderId{1});
    CHECK(book.total_quantity(Side::kSell) == Qty{8});

    const std::optional<RestingOrder> full = book.fill_front(Side::kSell, Qty{4});
    REQUIRE(full);
    CHECK(full->remaining == Qty{0});
    CHECK_FALSE(book.find(OrderId{1}).has_value());
    CHECK(book.front(Side::kSell)->id == OrderId{2});

    CHECK_FALSE(book.fill_front(Side::kSell, Qty{5}).has_value()); // more than remaining
    CHECK_FALSE(book.fill_front(Side::kBuy, Qty{1}).has_value());  // empty side
}

TEST_CASE("reduce keeps priority and rejects non-reductions", "[order_book]") {
    OrderBook book;
    book.add(resting(1, Side::kBuy, 100, 10));
    book.add(resting(2, Side::kBuy, 100, 10));
    CHECK(book.reduce(OrderId{1}, Qty{3}));
    CHECK(book.front(Side::kBuy)->id == OrderId{1});
    CHECK(book.front(Side::kBuy)->remaining == Qty{3});
    CHECK(book.total_quantity(Side::kBuy) == Qty{13});
    CHECK_FALSE(book.reduce(OrderId{1}, Qty{3}));
    CHECK_FALSE(book.reduce(OrderId{1}, Qty{4}));
    CHECK_FALSE(book.reduce(OrderId{1}, Qty{0}));
    CHECK_FALSE(book.reduce(OrderId{9}, Qty{1}));
}

TEST_CASE("depth aggregates levels best-first", "[order_book]") {
    OrderBook book;
    book.add(resting(1, Side::kBuy, 100, 5));
    book.add(resting(2, Side::kBuy, 100, 3));
    book.add(resting(3, Side::kBuy, 98, 2));
    book.add(resting(4, Side::kBuy, 99, 1));
    book.add(resting(5, Side::kSell, 102, 4));
    book.add(resting(6, Side::kSell, 101, 6));

    std::array<DepthLevel, 2> bids{};
    REQUIRE(book.depth(Side::kBuy, bids) == 2);
    CHECK(bids[0].price == PriceTicks{100});
    CHECK(bids[0].qty == Qty{8});
    CHECK(bids[0].order_count == 2);
    CHECK(bids[1].price == PriceTicks{99});

    std::array<DepthLevel, 5> asks{};
    REQUIRE(book.depth(Side::kSell, asks) == 2);
    CHECK(asks[0].price == PriceTicks{101});
    CHECK(asks[1].price == PriceTicks{102});
    CHECK(book.level_count(Side::kBuy) == 3);
}

TEST_CASE("node slots are reused after removal", "[order_book]") {
    OrderBook book{4};
    for (std::uint64_t round = 0; round < 100; ++round) {
        REQUIRE(book.add(resting(round + 1, Side::kBuy, 100, 1)));
        REQUIRE(book.remove(OrderId{round + 1}).has_value());
    }
    CHECK(book.empty());
    CHECK(book.level_count(Side::kBuy) == 0);
    CHECK(book.total_quantity(Side::kBuy) == Qty{0});
}

TEST_CASE("is_crossed detects bid at or above ask", "[order_book]") {
    OrderBook book;
    book.add(resting(1, Side::kBuy, 100, 1));
    book.add(resting(2, Side::kSell, 100, 1));
    CHECK(book.is_crossed());
}
