// Account limits enforced by the matching engine (TODO section 6a, ADR 0010).

#include "stocksim/matching_engine.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace stocksim;
using namespace stocksim::test;

using ET = EventType;

TEST_CASE("a limited buyer cannot commit more than it has", "[accounts]") {
    MatchingEngine engine;
    REQUIRE(engine.open_account(kAlice, Money{1'000}, Qty{0}, 100));
    engine.submit(limit(kAlice, Side::kBuy, 100, 10)); // commits all 1000
    engine.submit(limit(kAlice, Side::kBuy, 100, 1));
    const std::vector<Event> events = run_tick(engine);
    const std::vector<Event> rejected = of_type(events, ET::kOrderRejected);
    REQUIRE(rejected.size() == 1);
    CHECK(rejected[0].reason == Reason::kInsufficientFunds);
    CHECK(engine.account(kAlice)->committed_buy == Money{1'000});

    // Cancelling the resting order releases the commitment.
    engine.cancel(kAlice, OrderId{1});
    engine.submit(limit(kAlice, Side::kBuy, 100, 10));
    CHECK(of_type(run_tick(engine), ET::kOrderRejected).empty());
}

TEST_CASE("market buys are checked against the cost of sweeping the book", "[accounts]") {
    MatchingEngine engine;
    engine.submit(limit(kBob, Side::kSell, 100, 5));
    engine.submit(limit(kBob, Side::kSell, 200, 5));
    run_tick(engine);
    REQUIRE(engine.open_account(kAlice, Money{1'000}, Qty{0}, 100));

    engine.submit(market(kAlice, Side::kBuy, 10)); // would cost 500 + 1000
    std::vector<Event> events = run_tick(engine);
    REQUIRE(of_type(events, ET::kOrderRejected).size() == 1);
    CHECK(of_type(events, ET::kOrderRejected)[0].reason == Reason::kInsufficientFunds);

    engine.submit(market(kAlice, Side::kBuy, 6)); // 500 + 200
    events = run_tick(engine);
    CHECK(of_type(events, ET::kMatch).size() == 2);
    CHECK(engine.account(kAlice)->cash == Money{300});
    CHECK(engine.account(kAlice)->position == Qty{6});
    CHECK(engine.ledger().check_conservation());
}

TEST_CASE("limited accounts cannot sell shares they do not have", "[accounts]") {
    MatchingEngine engine;
    REQUIRE(engine.open_account(kAlice, Money{0}, Qty{10}, 100));
    engine.submit(limit(kAlice, Side::kSell, 100, 11));
    engine.submit(stop(kAlice, Side::kSell, 90, 6));   // reserves 6 shares
    engine.submit(limit(kAlice, Side::kSell, 100, 5)); // only 4 left unreserved
    engine.submit(limit(kAlice, Side::kSell, 100, 4));
    const std::vector<Event> events = run_tick(engine);
    const std::vector<Event> rejected = of_type(events, ET::kOrderRejected);
    REQUIRE(rejected.size() == 2);
    CHECK(rejected[0].reason == Reason::kInsufficientShares);
    CHECK(rejected[1].reason == Reason::kInsufficientShares);
    CHECK(engine.account(kAlice)->committed_sell == Qty{10});

    engine.cancel(kAlice, OrderId{2}); // the stop
    run_tick(engine);
    CHECK(engine.account(kAlice)->committed_sell == Qty{4});
}

TEST_CASE("fills release the maker's commitment exactly", "[accounts]") {
    MatchingEngine engine;
    REQUIRE(engine.open_account(kAlice, Money{10'000}, Qty{0}, 100));
    REQUIRE(engine.open_account(kBob, Money{0}, Qty{50}, 100));
    engine.submit(limit(kAlice, Side::kBuy, 100, 30));
    run_tick(engine);
    engine.submit(market(kBob, Side::kSell, 12));
    run_tick(engine);
    const AccountView alice = *engine.account(kAlice);
    CHECK(alice.committed_buy == Money{1'800}); // 18 still resting at 100
    CHECK(alice.position == Qty{12});
    CHECK(alice.cash == Money{8'800});
    CHECK(engine.account(kBob)->cash == Money{1'200});
    CHECK(engine.ledger().check_conservation());
}

TEST_CASE("a modify that would exceed buying power is rejected and leaves the order",
          "[accounts]") {
    MatchingEngine engine;
    REQUIRE(engine.open_account(kAlice, Money{1'000}, Qty{0}, 100));
    const OrderId id = engine.submit(limit(kAlice, Side::kBuy, 50, 10)); // commits 500
    run_tick(engine);
    engine.modify(kAlice, id, PriceTicks{50}, Qty{21}); // would commit 1050
    const std::vector<Event> events = run_tick(engine);
    REQUIRE(events.size() == 1);
    CHECK(events[0].type == ET::kCommandRejected);
    CHECK(events[0].reason == Reason::kInsufficientFunds);
    CHECK(engine.find_order(id)->remaining == Qty{10});
    CHECK(engine.account(kAlice)->committed_buy == Money{500});

    engine.modify(kAlice, id, PriceTicks{50}, Qty{20}); // exactly affordable
    CHECK(of_type(run_tick(engine), ET::kOrderModified).size() == 1);
    CHECK(engine.account(kAlice)->committed_buy == Money{1'000});
}

TEST_CASE("an unaffordable buy stop is cancelled when it fires", "[accounts]") {
    MatchingEngine engine;
    engine.submit(limit(kBob, Side::kSell, 100, 1));
    engine.submit(limit(kBob, Side::kSell, 100, 50));
    run_tick(engine);
    REQUIRE(engine.open_account(kAlice, Money{1'000}, Qty{0}, 100));
    const OrderId stop_order = engine.submit(stop(kAlice, Side::kBuy, 100, 20)); // 2000 needed
    run_tick(engine);
    engine.submit(market(kCarol, Side::kBuy, 1)); // trades at 100, firing the stop
    const std::vector<Event> events = run_tick(engine);
    const std::vector<Event> cancelled = of_type(events, ET::kOrderCancelled);
    REQUIRE(of_type(events, ET::kStopTriggered).size() == 1);
    REQUIRE(cancelled.size() == 1);
    CHECK(cancelled[0].order_id == stop_order);
    CHECK(cancelled[0].reason == Reason::kInsufficientFunds);
    CHECK(engine.account(kAlice)->position == Qty{0});
}

TEST_CASE("unopened accounts trade without limits, as before", "[accounts]") {
    MatchingEngine engine;
    engine.submit(limit(kAlice, Side::kSell, 100, 1'000)); // no shares, no account: allowed
    engine.submit(market(kBob, Side::kBuy, 10));
    run_tick(engine);
    CHECK(engine.account(kAlice)->position == Qty{-10});
    CHECK(engine.account(kBob)->cash == Money{-1'000});
    CHECK(engine.ledger().check_conservation());
}
