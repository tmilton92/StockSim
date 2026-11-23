// Full-state serialization of MatchingEngine and Simulation (TODO §2).

#include "random_flow.hpp"
#include "stocksim/matching_engine.hpp"
#include "stocksim/serialization.hpp"
#include "stocksim/simulation.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

using namespace stocksim;
using namespace stocksim::test;

namespace {

std::vector<std::byte> state_of(const MatchingEngine& engine) {
    StateWriter out;
    engine.save_state(out);
    return out.take();
}

Result<MatchingEngine> restore(std::span<const std::byte> bytes) {
    StateReader in{bytes};
    return MatchingEngine::load_state(in);
}

/// An engine with every kind of state populated: multi-order levels on both sides, pending
/// stops on both sides, an active (just-triggered) stop with another triggered stop queued
/// behind it, a last price, and queued requests.
MatchingEngine busy_engine() {
    MatchingEngine engine;
    engine.submit(limit(kAlice, Side::kSell, 101, 5));
    engine.submit(limit(kCarol, Side::kSell, 101, 7));
    engine.submit(limit(kAlice, Side::kSell, 103, 9));
    engine.submit(limit(kBob, Side::kBuy, 99, 4));
    engine.submit(limit(kDave, Side::kBuy, 99, 6));
    engine.submit(stop(kDave, Side::kBuy, 101, 3));
    engine.submit(stop(kCarol, Side::kBuy, 100, 2));
    engine.submit(stop(kDave, Side::kBuy, 150, 3));
    engine.submit(stop_limit(kCarol, Side::kSell, 90, 88, 2));
    engine.step_tick();
    engine.submit(market(kBob, Side::kBuy, 6));
    engine.submit(limit(kAlice, Side::kBuy, 95, 1));
    engine.cancel(kAlice, OrderId{3});
    engine.modify(kBob, OrderId{4}, PriceTicks{98}, Qty{2});
    engine.step_event(); // market order accepted
    engine.step_event(); // fills 5 at 101; still active
    engine.step_event(); // fills 1 at 101 and finishes; buy stops at 100 and 101 trigger
    engine.step_event(); // stop at 100 becomes the active order; stop at 101 still queued
    engine.clear_events();
    return engine;
}

} // namespace

TEST_CASE("engine state round-trips byte-for-byte", "[serialization][engine]") {
    const MatchingEngine original = busy_engine();
    REQUIRE_FALSE(original.idle());
    REQUIRE(original.find_order(OrderId{7})->status == OrderStatus::kMatching);
    REQUIRE(original.find_order(OrderId{6})->status == OrderStatus::kTriggered);
    REQUIRE(original.find_order(OrderId{8})->status == OrderStatus::kPendingStop);
    REQUIRE(original.find_order(OrderId{9})->status == OrderStatus::kPendingStop);
    const std::vector<std::byte> bytes = state_of(original);

    Result<MatchingEngine> restored = restore(bytes);
    REQUIRE(restored);
    CHECK(state_of(restored.value()) == bytes);
    CHECK(restored.value().last_sequence() == original.last_sequence());
    CHECK(restored.value().current_tick() == original.current_tick());
    CHECK(restored.value().best_bid() == original.best_bid());
    CHECK(restored.value().best_ask() == original.best_ask());
    CHECK(restored.value().total_volume() == original.total_volume());
}

TEST_CASE("a restored engine produces exactly the same future", "[serialization][engine]") {
    const std::uint64_t seed = GENERATE(3ULL, 17ULL, 4242ULL);
    INFO("seed " << seed);

    RandomFlow warmup{seed};
    MatchingEngine original;
    for (int i = 0; i < 300; ++i) {
        warmup.issue_request(original);
        if (warmup.rng().chance(1, 2)) {
            original.step_tick();
        } else {
            original.step_event();
        }
    }
    original.clear_events();

    Result<MatchingEngine> restored = restore(state_of(original));
    REQUIRE(restored);
    MatchingEngine& copy = restored.value();

    // Drive both with identical inputs (two flows from the same seed) and compare everything.
    RandomFlow flow_a{seed + 1};
    RandomFlow flow_b{seed + 1};
    for (int i = 0; i < 300; ++i) {
        flow_a.issue_request(original);
        flow_b.issue_request(copy);
        const bool tick = flow_a.rng().chance(1, 3);
        REQUIRE(tick == flow_b.rng().chance(1, 3));
        if (tick) {
            original.step_tick();
            copy.step_tick();
        } else {
            REQUIRE(original.step_event() == copy.step_event());
        }
        REQUIRE(std::vector<Event>(original.events().begin(), original.events().end()) ==
                std::vector<Event>(copy.events().begin(), copy.events().end()));
        original.clear_events();
        copy.clear_events();
    }
    CHECK(state_of(original) == state_of(copy));
}

TEST_CASE("truncated engine state is rejected, never misread", "[serialization][engine]") {
    const std::vector<std::byte> bytes = state_of(busy_engine());
    for (std::size_t length = 0; length < bytes.size(); ++length) {
        Result<MatchingEngine> restored = restore(std::span{bytes}.first(length));
        INFO("length " << length);
        REQUIRE_FALSE(restored);
        CHECK(restored.error().code == ErrorCode::kCorruptState);
    }
}

TEST_CASE("out-of-range values in engine state are rejected", "[serialization][engine]") {
    MatchingEngine engine;
    engine.submit(limit(kAlice, Side::kSell, 101, 5));
    engine.step_tick();
    std::vector<std::byte> bytes = state_of(engine);

    // Counters: tick(8) next_order_id(8) next_sequence(8) next_stop_sequence(8)
    // last_price(1+8) volumes(3*8) trade_count(8) = 73 bytes; then bid count(8) = 0, ask
    // count(8) = 1, then the resting order: id(8) account(4) type(1) ...
    constexpr std::size_t kLastPriceFlagOffset = 32;
    constexpr std::size_t kAskTypeOffset = 73 + 8 + 8 + 8 + 4;
    REQUIRE(bytes.size() > kAskTypeOffset);
    REQUIRE(bytes[kAskTypeOffset] == std::byte{static_cast<std::uint8_t>(OrderType::kLimit)});

    SECTION("unknown order type") {
        bytes[kAskTypeOffset] = std::byte{0x7F};
    }
    SECTION("a market order cannot rest on the book") {
        bytes[kAskTypeOffset] = std::byte{static_cast<std::uint8_t>(OrderType::kMarket)};
    }
    SECTION("last-price presence flag must be 0 or 1") {
        bytes[kLastPriceFlagOffset] = std::byte{2};
    }
    Result<MatchingEngine> restored = restore(bytes);
    REQUIRE_FALSE(restored);
    CHECK(restored.error().code == ErrorCode::kCorruptState);
}

TEST_CASE("Simulation save/load restores state and continues identically", "[serialization]") {
    Simulation original;
    RandomFlow flow{77};
    for (int i = 0; i < 200; ++i) {
        flow.issue_request(original);
        original.step_tick();
    }
    original.clear_events();
    const std::vector<std::byte> saved = original.save();

    Result<Simulation> loaded = Simulation::load(saved);
    REQUIRE(loaded);
    Simulation& copy = loaded.value();
    CHECK(copy.save() == saved);
    CHECK(copy.position() == 0); // history is not saved
    CHECK_FALSE(copy.can_step_back());

    original.submit(market(kBob, Side::kBuy, 30));
    copy.submit(market(kBob, Side::kBuy, 30));
    original.step_tick();
    copy.step_tick();
    CHECK(std::vector<Event>(original.events().begin(), original.events().end()) ==
          std::vector<Event>(copy.events().begin(), copy.events().end()));
}

TEST_CASE("Simulation load reports header problems", "[serialization]") {
    const std::vector<std::byte> good = Simulation{}.save();

    SECTION("empty input") {
        const Result<Simulation> result = Simulation::load({});
        REQUIRE_FALSE(result);
        CHECK(result.error().code == ErrorCode::kBadMagic);
    }
    SECTION("wrong magic") {
        std::vector<std::byte> bad = good;
        bad[0] = std::byte{'X'};
        const Result<Simulation> result = Simulation::load(bad);
        REQUIRE_FALSE(result);
        CHECK(result.error().code == ErrorCode::kBadMagic);
    }
    SECTION("future format version") {
        std::vector<std::byte> bad = good;
        bad[4] = std::byte{static_cast<std::uint8_t>(kStateFormatVersion + 1)};
        const Result<Simulation> result = Simulation::load(bad);
        REQUIRE_FALSE(result);
        CHECK(result.error().code == ErrorCode::kUnsupportedVersion);
    }
    SECTION("different event schema version") {
        std::vector<std::byte> bad = good;
        bad[8] = std::byte{static_cast<std::uint8_t>(kEventSchemaVersion + 1)};
        const Result<Simulation> result = Simulation::load(bad);
        REQUIRE_FALSE(result);
        CHECK(result.error().code == ErrorCode::kUnsupportedVersion);
    }
    SECTION("trailing bytes") {
        std::vector<std::byte> bad = good;
        bad.push_back(std::byte{0});
        const Result<Simulation> result = Simulation::load(bad);
        REQUIRE_FALSE(result);
        CHECK(result.error().code == ErrorCode::kCorruptState);
    }
    CHECK(Simulation::load(good).has_value());
}
