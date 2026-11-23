// Randomized invariant tests for the matching engine. All randomness comes from the seeded
// project Rng, so every run (and every failure) is reproducible from the seed shown by Catch2.

#include "random_flow.hpp"
#include "stocksim/matching_engine.hpp"
#include "stocksim/rng.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>

using namespace stocksim;

namespace {

/// Per-order quantity ledger rebuilt purely from events.
struct Ledger {
    Qty placed;
    Qty modified_delta;
    Qty filled;
    Qty cancelled;
};

/// Feeds a random stream of requests, interleaving single steps and whole ticks, and returns
/// every event emitted. Invariants that must hold between steps are checked as it runs.
std::vector<Event> run_session(std::uint64_t seed, int request_count) {
    test::RandomFlow flow{seed};
    Rng& rng = flow.rng();
    MatchingEngine engine;
    std::vector<Event> all_events;

    const auto drain = [&] {
        const std::span<const Event> events = engine.events();
        all_events.insert(all_events.end(), events.begin(), events.end());
        engine.clear_events();
    };

    for (int request = 0; request < request_count; ++request) {
        flow.issue_request(engine);

        if (rng.chance(1, 3)) {
            engine.step_tick();
        } else {
            for (std::uint64_t steps = rng.uniform_below(4); steps > 0; --steps) {
                engine.step_event();
                REQUIRE_FALSE(engine.book().is_crossed());
            }
        }
        REQUIRE_FALSE(engine.book().is_crossed());
        drain();
    }
    engine.step_tick();
    drain();
    REQUIRE(engine.idle());

    // --- End-of-session invariants -------------------------------------------------------

    // Book totals agree with depth and with individually resting orders.
    for (const Side side : {Side::kBuy, Side::kSell}) {
        std::vector<DepthLevel> levels(engine.book().level_count(side));
        REQUIRE(engine.depth(side, levels) == levels.size());
        Qty depth_total;
        for (const DepthLevel& level : levels) {
            REQUIRE(level.qty > Qty{0});
            depth_total += level.qty;
        }
        CHECK(depth_total == engine.book().total_quantity(side));
    }

    // Quantity conservation per order: placed + modifications = filled + cancelled + live.
    std::map<OrderId, Ledger> ledgers;
    Qty match_volume;
    for (std::size_t i = 0; i < all_events.size(); ++i) {
        const Event& event = all_events[i];
        if (i > 0) {
            REQUIRE(event.seq.value() == all_events[i - 1].seq.value() + 1);
        }
        switch (event.type) {
        case EventType::kOrderPlaced:
            ledgers[event.order_id].placed += event.qty;
            break;
        case EventType::kOrderModified:
            ledgers[event.order_id].modified_delta += event.remaining - event.qty;
            break;
        case EventType::kOrderCancelled:
            ledgers[event.order_id].cancelled += event.qty;
            break;
        case EventType::kFill:
            ledgers[event.order_id].filled += event.qty;
            break;
        case EventType::kMatch: {
            // Every match is followed by exactly two fills: taker, then maker.
            REQUIRE(i + 2 < all_events.size());
            const Event& taker = all_events[i + 1];
            const Event& maker = all_events[i + 2];
            REQUIRE(taker.type == EventType::kFill);
            REQUIRE(maker.type == EventType::kFill);
            CHECK(taker.order_id == event.order_id);
            CHECK(maker.order_id == event.counter_order_id);
            CHECK(taker.side == event.side);
            CHECK(maker.side == opposite(event.side));
            CHECK(taker.qty == event.qty);
            CHECK(maker.qty == event.qty);
            CHECK(taker.price == event.price);
            CHECK(maker.price == event.price);
            CHECK(event.qty > Qty{0});
            match_volume += event.qty;
            break;
        }
        default:
            break;
        }
    }
    CHECK(match_volume == engine.total_volume());

    for (const auto& [id, ledger] : ledgers) {
        const std::optional<OrderView> live = engine.find_order(id);
        const Qty live_remaining = live ? live->remaining : Qty{0};
        INFO("order " << id.value());
        CHECK(ledger.placed + ledger.modified_delta ==
              ledger.filled + ledger.cancelled + live_remaining);
        if (live) {
            CHECK(live->filled == ledger.filled);
        }
    }
    return all_events;
}

} // namespace

TEST_CASE("random sessions preserve matching invariants", "[matching][property]") {
    const std::uint64_t seed = GENERATE(range<std::uint64_t>(1, 21));
    INFO("seed " << seed);
    const std::vector<Event> events = run_session(seed, 1500);
    CHECK_FALSE(events.empty());
}

TEST_CASE("same seed produces an identical event stream", "[matching][property][determinism]") {
    const std::uint64_t seed = GENERATE(7ULL, 99ULL, 123456789ULL);
    INFO("seed " << seed);
    const std::vector<Event> first = run_session(seed, 1000);
    const std::vector<Event> second = run_session(seed, 1000);
    REQUIRE(first.size() == second.size());
    CHECK(first == second);
}
