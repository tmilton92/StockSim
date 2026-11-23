#include "stocksim/matching_engine.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <optional>
#include <vector>

using namespace stocksim;
using namespace stocksim::test;

using ET = EventType;

// ---------------------------------------------------------------------------------------------
// Resting and crossing
// ---------------------------------------------------------------------------------------------

TEST_CASE("non-crossing limit orders rest on the book", "[matching]") {
    MatchingEngine engine;
    const OrderId bid = engine.submit(limit(kAlice, Side::kBuy, 100, 10));
    const OrderId ask = engine.submit(limit(kBob, Side::kSell, 101, 5));
    const std::vector<Event> events = run_tick(engine);

    CHECK(types_of(events) ==
          std::vector{ET::kOrderPlaced, ET::kOrderRested, ET::kOrderPlaced, ET::kOrderRested});
    CHECK(events[1].order_id == bid);
    CHECK(events[1].price == PriceTicks{100});
    CHECK(events[1].qty == Qty{10});
    CHECK(events[3].order_id == ask);
    CHECK(engine.best_bid() == PriceTicks{100});
    CHECK(engine.best_ask() == PriceTicks{101});
    CHECK_FALSE(engine.last_price().has_value());
    CHECK(engine.total_volume() == Qty{0});
}

TEST_CASE("a crossing limit order trades at the resting price", "[matching]") {
    MatchingEngine engine;
    const OrderId maker = engine.submit(limit(kAlice, Side::kSell, 100, 10));
    run_tick(engine);
    const OrderId taker = engine.submit(limit(kBob, Side::kBuy, 102, 10));
    const std::vector<Event> events = run_tick(engine);

    REQUIRE(types_of(events) ==
            std::vector{ET::kOrderPlaced, ET::kMatch, ET::kFill, ET::kFill, ET::kPriceChange});
    const Event& match = events[1];
    CHECK(match.order_id == taker);
    CHECK(match.counter_order_id == maker);
    CHECK(match.account == kBob);
    CHECK(match.counter_account == kAlice);
    CHECK(match.side == Side::kBuy);
    CHECK(match.price == PriceTicks{100}); // maker's price, not the taker's 102
    CHECK(match.qty == Qty{10});

    const Event& taker_fill = events[2];
    CHECK(taker_fill.order_id == taker);
    CHECK(taker_fill.reason == Reason::kTakerFill);
    CHECK(taker_fill.remaining == Qty{0});
    const Event& maker_fill = events[3];
    CHECK(maker_fill.order_id == maker);
    CHECK(maker_fill.side == Side::kSell);
    CHECK(maker_fill.reason == Reason::kMakerFill);
    CHECK(maker_fill.remaining == Qty{0});

    CHECK(events[4].price == PriceTicks{100});
    CHECK(events[4].aux_price == PriceTicks{0}); // no previous trade
    CHECK(engine.last_price() == PriceTicks{100});
    CHECK(engine.book().empty());
    CHECK(engine.total_volume() == Qty{10});
    CHECK(engine.trade_count() == 1);
}

TEST_CASE("a larger taker fills what it can and rests the remainder", "[matching]") {
    MatchingEngine engine;
    engine.submit(limit(kAlice, Side::kSell, 100, 4));
    const OrderId taker = engine.submit(limit(kBob, Side::kBuy, 101, 10));
    const std::vector<Event> events = run_tick(engine);

    const std::vector<Event> rested = of_type(events, ET::kOrderRested);
    REQUIRE(rested.size() == 2);
    CHECK(rested[1].order_id == taker);
    CHECK(rested[1].qty == Qty{6});
    CHECK(rested[1].price == PriceTicks{101});
    CHECK(engine.best_bid() == PriceTicks{101});
    CHECK_FALSE(engine.best_ask().has_value());

    const std::optional<OrderView> view = engine.find_order(taker);
    REQUIRE(view);
    CHECK(view->status == OrderStatus::kResting);
    CHECK(view->remaining == Qty{6});
    CHECK(view->filled == Qty{4});
}

TEST_CASE("a partially filled maker keeps its queue position", "[matching]") {
    MatchingEngine engine;
    const OrderId first = engine.submit(limit(kAlice, Side::kSell, 100, 10));
    const OrderId second = engine.submit(limit(kCarol, Side::kSell, 100, 5));
    run_tick(engine);

    engine.submit(market(kBob, Side::kBuy, 4));
    std::vector<Event> matches = of_type(run_tick(engine), ET::kMatch);
    REQUIRE(matches.size() == 1);
    CHECK(matches[0].counter_order_id == first);

    engine.submit(market(kBob, Side::kBuy, 7));
    matches = of_type(run_tick(engine), ET::kMatch);
    REQUIRE(matches.size() == 2);
    CHECK(matches[0].counter_order_id == first);
    CHECK(matches[0].qty == Qty{6});
    CHECK(matches[1].counter_order_id == second);
    CHECK(matches[1].qty == Qty{1});
}

// ---------------------------------------------------------------------------------------------
// Priority
// ---------------------------------------------------------------------------------------------

TEST_CASE("a sweep takes the best price first, within the limit", "[matching][priority]") {
    MatchingEngine engine;
    const OrderId at_101 = engine.submit(limit(kAlice, Side::kSell, 101, 5));
    const OrderId at_100 = engine.submit(limit(kCarol, Side::kSell, 100, 5));
    engine.submit(limit(kDave, Side::kSell, 102, 5));
    run_tick(engine);

    engine.submit(limit(kBob, Side::kBuy, 101, 12));
    const std::vector<Event> events = run_tick(engine);
    const std::vector<Event> matches = of_type(events, ET::kMatch);
    REQUIRE(matches.size() == 2);
    CHECK(matches[0].counter_order_id == at_100);
    CHECK(matches[0].price == PriceTicks{100});
    CHECK(matches[1].counter_order_id == at_101);
    CHECK(matches[1].price == PriceTicks{101});

    const std::vector<Event> changes = of_type(events, ET::kPriceChange);
    REQUIRE(changes.size() == 2);
    CHECK(changes[1].aux_price == PriceTicks{100});
    CHECK(changes[1].price == PriceTicks{101});

    CHECK(engine.best_bid() == PriceTicks{101}); // 2 left over, rests at its limit
    CHECK(engine.best_ask() == PriceTicks{102}); // 102 was beyond the limit, untouched
}

TEST_CASE("earlier orders at the same price fill first", "[matching][priority]") {
    MatchingEngine engine;
    const OrderId early = engine.submit(limit(kAlice, Side::kSell, 100, 5));
    engine.submit(limit(kCarol, Side::kSell, 100, 5));
    run_tick(engine);
    engine.submit(limit(kBob, Side::kBuy, 100, 5));
    const std::vector<Event> matches = of_type(run_tick(engine), ET::kMatch);
    REQUIRE(matches.size() == 1);
    CHECK(matches[0].counter_order_id == early);
}

TEST_CASE("a sell market order sweeps bids from the highest down", "[matching][priority]") {
    MatchingEngine engine;
    const OrderId low = engine.submit(limit(kAlice, Side::kBuy, 98, 5));
    const OrderId high = engine.submit(limit(kCarol, Side::kBuy, 99, 5));
    run_tick(engine);
    engine.submit(market(kBob, Side::kSell, 8));
    const std::vector<Event> matches = of_type(run_tick(engine), ET::kMatch);
    REQUIRE(matches.size() == 2);
    CHECK(matches[0].counter_order_id == high);
    CHECK(matches[1].counter_order_id == low);
    CHECK(matches[1].qty == Qty{3});
    CHECK(engine.last_price() == PriceTicks{98});
}

// ---------------------------------------------------------------------------------------------
// Market orders and liquidity
// ---------------------------------------------------------------------------------------------

TEST_CASE("a market order cancels whatever it cannot fill", "[matching]") {
    MatchingEngine engine;
    engine.submit(limit(kAlice, Side::kSell, 100, 3));
    run_tick(engine);
    const OrderId taker = engine.submit(market(kBob, Side::kBuy, 10));
    const std::vector<Event> events = run_tick(engine);
    const std::vector<Event> cancels = of_type(events, ET::kOrderCancelled);
    REQUIRE(cancels.size() == 1);
    CHECK(cancels[0].order_id == taker);
    CHECK(cancels[0].reason == Reason::kNoLiquidity);
    CHECK(cancels[0].qty == Qty{7});
    CHECK(cancels[0].remaining == Qty{0});
    CHECK_FALSE(engine.find_order(taker).has_value());
}

TEST_CASE("a market order into an empty book is cancelled in full", "[matching]") {
    MatchingEngine engine;
    engine.submit(market(kBob, Side::kSell, 10));
    const std::vector<Event> events = run_tick(engine);
    REQUIRE(types_of(events) == std::vector{ET::kOrderPlaced, ET::kOrderCancelled});
    CHECK(events[1].qty == Qty{10});
    CHECK_FALSE(engine.last_price().has_value());
}

TEST_CASE("market orders ignore price fields", "[matching]") {
    MatchingEngine engine;
    NewOrder order = market(kBob, Side::kBuy, 1);
    order.limit_price = PriceTicks{-5};
    order.stop_price = PriceTicks{-5};
    engine.submit(order);
    const std::vector<Event> events = run_tick(engine);
    REQUIRE(events[0].type == ET::kOrderPlaced);
    CHECK(events[0].price == PriceTicks{0});
    CHECK(events[0].aux_price == PriceTicks{0});
}

// ---------------------------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------------------------

TEST_CASE("invalid orders are rejected with a reason", "[matching][validation]") {
    struct Case {
        NewOrder order;
        Reason reason;
    };
    const std::vector<Case> cases{
        {.order = limit(kAlice, Side::kBuy, 100, 0), .reason = Reason::kInvalidQuantity},
        {.order = limit(kAlice, Side::kBuy, 100, -3), .reason = Reason::kInvalidQuantity},
        {.order = limit(kAlice, Side::kBuy, 100, kMaxOrderQty.value() + 1),
         .reason = Reason::kInvalidQuantity},
        {.order = limit(kAlice, Side::kBuy, 0, 5), .reason = Reason::kInvalidLimitPrice},
        {.order = limit(kAlice, Side::kBuy, kMaxPrice.value() + 1, 5),
         .reason = Reason::kInvalidLimitPrice},
        {.order = stop(kAlice, Side::kBuy, 0, 5), .reason = Reason::kInvalidStopPrice},
        {.order = stop_limit(kAlice, Side::kBuy, 100, 0, 5), .reason = Reason::kInvalidLimitPrice},
        {.order = stop_limit(kAlice, Side::kBuy, -1, 100, 5), .reason = Reason::kInvalidStopPrice},
    };
    for (const Case& test_case : cases) {
        MatchingEngine engine;
        const OrderId id = engine.submit(test_case.order);
        const std::vector<Event> events = run_tick(engine);
        REQUIRE(events.size() == 1);
        CHECK(events[0].type == ET::kOrderRejected);
        CHECK(events[0].order_id == id);
        CHECK(events[0].reason == test_case.reason);
        CHECK(engine.book().empty());
        CHECK_FALSE(engine.find_order(id).has_value());
    }
}

// ---------------------------------------------------------------------------------------------
// Stops
// ---------------------------------------------------------------------------------------------

TEST_CASE("a buy stop waits, then becomes a market order when the price rises to it",
          "[matching][stops]") {
    MatchingEngine engine;
    engine.submit(limit(kAlice, Side::kSell, 100, 5));
    const OrderId upper = engine.submit(limit(kAlice, Side::kSell, 105, 10));
    const OrderId stop_order = engine.submit(stop(kDave, Side::kBuy, 100, 5));
    run_tick(engine);

    std::optional<OrderView> view = engine.find_order(stop_order);
    REQUIRE(view);
    CHECK(view->status == OrderStatus::kPendingStop);

    engine.submit(market(kBob, Side::kBuy, 5));
    const std::vector<Event> events = run_tick(engine);
    CHECK(types_of(events) == std::vector{ET::kOrderPlaced, ET::kMatch, ET::kFill, ET::kFill,
                                          ET::kPriceChange, ET::kStopTriggered, ET::kMatch,
                                          ET::kFill, ET::kFill, ET::kPriceChange});
    const Event& trigger = events[5];
    CHECK(trigger.order_id == stop_order);
    CHECK(trigger.price == PriceTicks{100});     // last trade that fired it
    CHECK(trigger.aux_price == PriceTicks{100}); // its stop price
    CHECK(events[6].order_id == stop_order);
    CHECK(events[6].counter_order_id == upper);
    CHECK(events[6].price == PriceTicks{105});
    CHECK(engine.last_price() == PriceTicks{105});
    CHECK_FALSE(engine.find_order(stop_order).has_value());
}

TEST_CASE("a sell stop-limit rests at its limit when it cannot fill", "[matching][stops]") {
    MatchingEngine engine;
    engine.submit(limit(kAlice, Side::kBuy, 100, 5));
    engine.submit(limit(kAlice, Side::kBuy, 95, 5));
    const OrderId stop_order = engine.submit(stop_limit(kDave, Side::kSell, 100, 98, 8));
    run_tick(engine);

    engine.submit(market(kBob, Side::kSell, 5));
    const std::vector<Event> events = run_tick(engine);
    REQUIRE(of_type(events, ET::kStopTriggered).size() == 1);
    const std::vector<Event> rested = of_type(events, ET::kOrderRested);
    REQUIRE(rested.size() == 1);
    CHECK(rested[0].order_id == stop_order);
    CHECK(rested[0].order_type == OrderType::kStopLimit);
    CHECK(rested[0].price == PriceTicks{98});
    CHECK(engine.best_ask() == PriceTicks{98});
    CHECK(engine.best_bid() == PriceTicks{95});
    const std::optional<OrderView> view = engine.find_order(stop_order);
    REQUIRE(view);
    CHECK(view->status == OrderStatus::kResting);
    CHECK(view->type == OrderType::kStopLimit);
}

TEST_CASE("stops trigger only when the price reaches them", "[matching][stops]") {
    MatchingEngine engine;
    engine.submit(limit(kAlice, Side::kSell, 100, 1));
    engine.submit(market(kBob, Side::kBuy, 1));
    run_tick(engine);
    REQUIRE(engine.last_price() == PriceTicks{100});

    SECTION("not yet reached") {
        const OrderId id = engine.submit(stop(kDave, Side::kBuy, 101, 1));
        CHECK(of_type(run_tick(engine), ET::kStopTriggered).empty());
        CHECK(engine.find_order(id)->status == OrderStatus::kPendingStop);
    }
    SECTION("already reached when placed triggers immediately") {
        const OrderId id = engine.submit(stop(kDave, Side::kBuy, 99, 1));
        const std::vector<Event> events = run_tick(engine);
        REQUIRE(of_type(events, ET::kStopTriggered).size() == 1);
        CHECK(of_type(events, ET::kStopTriggered)[0].order_id == id);
    }
    SECTION("sell stop at exactly the last price triggers") {
        const OrderId id = engine.submit(stop(kDave, Side::kSell, 100, 1));
        const std::vector<Event> triggers = of_type(run_tick(engine), ET::kStopTriggered);
        REQUIRE(triggers.size() == 1);
        CHECK(triggers[0].order_id == id);
    }
}

TEST_CASE("stops do not trigger before any trade", "[matching][stops]") {
    MatchingEngine engine;
    const OrderId id = engine.submit(stop(kDave, Side::kSell, 1'000'000, 1));
    CHECK(of_type(run_tick(engine), ET::kStopTriggered).empty());
    CHECK(engine.find_order(id)->status == OrderStatus::kPendingStop);
}

TEST_CASE("one stop's fills can trigger the next (cascade)", "[matching][stops]") {
    MatchingEngine engine;
    engine.submit(limit(kAlice, Side::kBuy, 100, 1));
    engine.submit(limit(kAlice, Side::kBuy, 99, 5));
    engine.submit(limit(kAlice, Side::kBuy, 90, 100));
    const OrderId first = engine.submit(stop(kDave, Side::kSell, 100, 5));
    const OrderId second = engine.submit(stop(kCarol, Side::kSell, 99, 5));
    run_tick(engine);

    engine.submit(market(kBob, Side::kSell, 1));
    const std::vector<Event> events = run_tick(engine);
    const std::vector<Event> triggers = of_type(events, ET::kStopTriggered);
    REQUIRE(triggers.size() == 2);
    CHECK(triggers[0].order_id == first);
    CHECK(triggers[0].price == PriceTicks{100});
    CHECK(triggers[1].order_id == second);
    CHECK(triggers[1].price == PriceTicks{99});
    CHECK(engine.last_price() == PriceTicks{90});
}

TEST_CASE("simultaneously triggered buy stops run lowest stop first", "[matching][stops]") {
    MatchingEngine engine;
    engine.submit(limit(kAlice, Side::kSell, 102, 1));
    engine.submit(limit(kAlice, Side::kSell, 110, 100));
    const OrderId higher = engine.submit(stop(kDave, Side::kBuy, 101, 1));
    const OrderId lower = engine.submit(stop(kCarol, Side::kBuy, 100, 1));
    run_tick(engine);

    engine.submit(market(kBob, Side::kBuy, 1));
    const std::vector<Event> triggers = of_type(run_tick(engine), ET::kStopTriggered);
    REQUIRE(triggers.size() == 2);
    CHECK(triggers[0].order_id == lower);
    CHECK(triggers[1].order_id == higher);
}

// ---------------------------------------------------------------------------------------------
// Cancel
// ---------------------------------------------------------------------------------------------

TEST_CASE("cancel", "[matching][cancel]") {
    MatchingEngine engine;
    const OrderId resting = engine.submit(limit(kAlice, Side::kBuy, 100, 10));
    const OrderId pending = engine.submit(stop(kAlice, Side::kSell, 90, 4));
    run_tick(engine);

    SECTION("the owner can cancel a resting order") {
        engine.cancel(kAlice, resting);
        const std::vector<Event> events = run_tick(engine);
        REQUIRE(events.size() == 1);
        CHECK(events[0].type == ET::kOrderCancelled);
        CHECK(events[0].reason == Reason::kCancelledByOwner);
        CHECK(events[0].qty == Qty{10});
        CHECK(events[0].price == PriceTicks{100});
        CHECK_FALSE(engine.best_bid().has_value());
    }
    SECTION("the owner can cancel a pending stop") {
        engine.cancel(kAlice, pending);
        const std::vector<Event> events = run_tick(engine);
        REQUIRE(events.size() == 1);
        CHECK(events[0].type == ET::kOrderCancelled);
        CHECK(events[0].qty == Qty{4});
        CHECK(events[0].aux_price == PriceTicks{90});
        CHECK_FALSE(engine.find_order(pending).has_value());
    }
    SECTION("another account cannot cancel it") {
        engine.cancel(kBob, resting);
        engine.cancel(kBob, pending);
        const std::vector<Event> events = run_tick(engine);
        REQUIRE(events.size() == 2);
        CHECK(events[0].type == ET::kCommandRejected);
        CHECK(events[0].reason == Reason::kNotOrderOwner);
        CHECK(events[1].reason == Reason::kNotOrderOwner);
        CHECK(engine.find_order(resting).has_value());
        CHECK(engine.find_order(pending).has_value());
    }
    SECTION("unknown and already-finished orders are rejected") {
        engine.cancel(kAlice, OrderId{999});
        engine.cancel(kAlice, resting);
        engine.cancel(kAlice, resting);
        const std::vector<Event> events = run_tick(engine);
        REQUIRE(events.size() == 3);
        CHECK(events[0].reason == Reason::kUnknownOrder);
        CHECK(events[1].type == ET::kOrderCancelled);
        CHECK(events[2].reason == Reason::kUnknownOrder);
    }
}

// ---------------------------------------------------------------------------------------------
// Modify
// ---------------------------------------------------------------------------------------------

TEST_CASE("modify", "[matching][modify]") {
    MatchingEngine engine;
    const OrderId first = engine.submit(limit(kAlice, Side::kSell, 100, 10));
    const OrderId second = engine.submit(limit(kCarol, Side::kSell, 100, 10));
    run_tick(engine);

    SECTION("reducing quantity at the same price keeps priority") {
        engine.modify(kAlice, first, PriceTicks{100}, Qty{4});
        const std::vector<Event> events = run_tick(engine);
        REQUIRE(events.size() == 1);
        CHECK(events[0].type == ET::kOrderModified);
        CHECK(events[0].reason == Reason::kModifiedKeptPriority);
        CHECK(events[0].qty == Qty{10});
        CHECK(events[0].remaining == Qty{4});
        engine.submit(market(kBob, Side::kBuy, 4));
        const std::vector<Event> matches = of_type(run_tick(engine), ET::kMatch);
        REQUIRE(matches.size() == 1);
        CHECK(matches[0].counter_order_id == first);
        CHECK(engine.book().total_quantity(Side::kSell) == Qty{10});
    }
    SECTION("increasing quantity loses priority") {
        engine.modify(kAlice, first, PriceTicks{100}, Qty{20});
        const std::vector<Event> events = run_tick(engine);
        REQUIRE(types_of(events) == std::vector{ET::kOrderModified, ET::kOrderRested});
        CHECK(events[0].reason == Reason::kModifiedLostPriority);
        engine.submit(market(kBob, Side::kBuy, 5));
        const std::vector<Event> matches = of_type(run_tick(engine), ET::kMatch);
        REQUIRE(matches.size() == 1);
        CHECK(matches[0].counter_order_id == second);
    }
    SECTION("a new price that crosses trades immediately") {
        const OrderId bid = engine.submit(limit(kBob, Side::kBuy, 99, 5));
        run_tick(engine);
        engine.modify(kBob, bid, PriceTicks{100}, Qty{5});
        const std::vector<Event> events = run_tick(engine);
        REQUIRE(types_of(events) == std::vector{ET::kOrderModified, ET::kMatch, ET::kFill,
                                                ET::kFill, ET::kPriceChange});
        CHECK(events[0].aux_price == PriceTicks{99});
        CHECK(events[0].price == PriceTicks{100});
        CHECK(events[1].counter_order_id == first);
    }
    SECTION("filled quantity carries across a re-priced modify") {
        engine.submit(market(kBob, Side::kBuy, 3));
        run_tick(engine);
        engine.modify(kAlice, first, PriceTicks{101}, Qty{7});
        run_tick(engine);
        const std::optional<OrderView> view = engine.find_order(first);
        REQUIRE(view);
        CHECK(view->filled == Qty{3});
        CHECK(view->remaining == Qty{7});
        CHECK(view->limit_price == PriceTicks{101});
    }
    SECTION("invalid modifies are rejected and change nothing") {
        const OrderId pending = engine.submit(stop(kAlice, Side::kBuy, 150, 1));
        run_tick(engine);
        engine.modify(kBob, first, PriceTicks{100}, Qty{5});
        engine.modify(kAlice, first, PriceTicks{100}, Qty{0});
        engine.modify(kAlice, first, PriceTicks{0}, Qty{5});
        engine.modify(kAlice, pending, PriceTicks{100}, Qty{5});
        engine.modify(kAlice, OrderId{999}, PriceTicks{100}, Qty{5});
        const std::vector<Event> events = run_tick(engine);
        REQUIRE(events.size() == 5);
        for (const Event& event : events) {
            CHECK(event.type == ET::kCommandRejected);
        }
        CHECK(events[0].reason == Reason::kNotOrderOwner);
        CHECK(events[1].reason == Reason::kInvalidQuantity);
        CHECK(events[2].reason == Reason::kInvalidLimitPrice);
        CHECK(events[3].reason == Reason::kNotModifiable);
        CHECK(events[4].reason == Reason::kUnknownOrder);
        CHECK(engine.find_order(first)->remaining == Qty{10});
    }
}

// ---------------------------------------------------------------------------------------------
// Stepping, ticks, sequencing
// ---------------------------------------------------------------------------------------------

TEST_CASE("step_event advances one atomic step at a time", "[matching][stepping]") {
    MatchingEngine engine;
    engine.submit(limit(kAlice, Side::kSell, 100, 5));
    engine.submit(limit(kCarol, Side::kSell, 101, 5));
    run_tick(engine);

    const OrderId taker = engine.submit(market(kBob, Side::kBuy, 10));
    CHECK_FALSE(engine.idle());
    CHECK(types_of(run_step(engine)) == std::vector{ET::kOrderPlaced});

    CHECK(types_of(run_step(engine)) ==
          std::vector{ET::kMatch, ET::kFill, ET::kFill, ET::kPriceChange});
    const std::optional<OrderView> mid = engine.find_order(taker);
    REQUIRE(mid);
    CHECK(mid->status == OrderStatus::kMatching);
    CHECK(mid->remaining == Qty{5});
    CHECK(engine.best_ask() == PriceTicks{101}); // state matches events so far

    CHECK(types_of(run_step(engine)) ==
          std::vector{ET::kMatch, ET::kFill, ET::kFill, ET::kPriceChange});
    CHECK(engine.idle());
    CHECK_FALSE(engine.step_event());
    CHECK(engine.events().empty());
}

TEST_CASE("ticks stamp events and roll tick volume", "[matching][stepping]") {
    MatchingEngine engine;
    CHECK(engine.current_tick() == Tick{0});
    engine.submit(limit(kAlice, Side::kSell, 100, 10));
    engine.submit(market(kBob, Side::kBuy, 4));
    const std::vector<Event> first = run_tick(engine);
    for (const Event& event : first) {
        CHECK(event.tick == Tick{0});
    }
    CHECK(engine.current_tick() == Tick{1});
    CHECK(engine.previous_tick_volume() == Qty{4});
    CHECK(engine.tick_volume() == Qty{0});

    engine.submit(market(kBob, Side::kBuy, 2));
    const std::vector<Event> second = run_tick(engine);
    for (const Event& event : second) {
        CHECK(event.tick == Tick{1});
    }
    CHECK(engine.previous_tick_volume() == Qty{2});
    CHECK(engine.total_volume() == Qty{6});

    run_tick(engine); // an empty tick still advances time
    CHECK(engine.current_tick() == Tick{3});
    CHECK(engine.previous_tick_volume() == Qty{0});
}

TEST_CASE("order ids and sequence numbers are strictly increasing", "[matching][stepping]") {
    MatchingEngine engine;
    const OrderId a = engine.submit(limit(kAlice, Side::kSell, 100, 10));
    const OrderId b = engine.submit(limit(kBob, Side::kBuy, 100, 3));
    const OrderId c = engine.submit(limit(kBob, Side::kBuy, 0, 3)); // rejected, still numbered
    CHECK(a == OrderId{1});
    CHECK(b == OrderId{2});
    CHECK(c == OrderId{3});
    const std::vector<Event> events = run_tick(engine);
    for (std::size_t i = 0; i < events.size(); ++i) {
        CHECK(events[i].seq == SequenceNumber{i + 1});
    }
}

TEST_CASE("self-matching is allowed until safeguards exist (TODO section 11)", "[matching]") {
    MatchingEngine engine;
    engine.submit(limit(kAlice, Side::kSell, 100, 5));
    engine.submit(limit(kAlice, Side::kBuy, 100, 5));
    const std::vector<Event> matches = of_type(run_tick(engine), ET::kMatch);
    REQUIRE(matches.size() == 1);
    CHECK(matches[0].account == matches[0].counter_account);
}
