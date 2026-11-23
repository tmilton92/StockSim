// Simulation journal, snapshots, step_back and redo (TODO §2).

#include "random_flow.hpp"
#include "stocksim/simulation.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <vector>

using namespace stocksim;
using namespace stocksim::test;

namespace {

/// A recorded live run: the state after every journal position, and every event emitted.
struct Recording {
    std::map<std::size_t, std::vector<std::byte>> state_at;
    std::vector<Event> events;
};

void drain(Simulation& sim, std::vector<Event>& into) {
    into.insert(into.end(), sim.events().begin(), sim.events().end());
    sim.clear_events();
}

/// Drives `sim` with random flow using only operations that each append exactly one journal
/// entry (single inputs, single steps, and step_tick on an idle engine), so the state after
/// every position can be recorded.
Recording record_run(Simulation& sim, std::uint64_t seed, int rounds) {
    Recording recording;
    recording.state_at[sim.position()] = sim.save();
    RandomFlow flow{seed};
    for (int round = 0; round < rounds; ++round) {
        for (std::uint64_t n = flow.rng().uniform_below(3); n > 0; --n) {
            flow.issue_request(sim);
            recording.state_at[sim.position()] = sim.save();
        }
        // Process everything one step at a time, then advance the (now idle) tick.
        const bool finish_tick = flow.rng().chance(2, 3);
        std::uint64_t budget =
            finish_tick ? std::numeric_limits<std::uint64_t>::max() : flow.rng().uniform_below(4);
        while (budget > 0 && sim.step_event()) {
            --budget;
            recording.state_at[sim.position()] = sim.save();
        }
        if (finish_tick) {
            REQUIRE(sim.engine().idle());
            const std::size_t before = sim.position();
            sim.step_tick();
            REQUIRE(sim.position() == before + 1);
            recording.state_at[sim.position()] = sim.save();
        }
        drain(sim, recording.events);
    }
    return recording;
}

} // namespace

TEST_CASE("step_back at the start does nothing", "[simulation]") {
    Simulation sim;
    CHECK_FALSE(sim.can_step_back());
    CHECK_FALSE(sim.step_back());
    CHECK(sim.position() == 0);
}

TEST_CASE("step_back undoes one step and redo replays it", "[simulation]") {
    Simulation sim;
    sim.submit(limit(kAlice, Side::kSell, 100, 5));
    sim.step_tick();
    sim.submit(market(kBob, Side::kBuy, 5));
    REQUIRE(sim.step_event()); // accepted
    sim.clear_events();
    const std::vector<std::byte> before_fill = sim.save();
    REQUIRE(sim.step_event()); // fill
    const std::vector<Event> fill_events(sim.events().begin(), sim.events().end());
    REQUIRE_FALSE(fill_events.empty());
    REQUIRE(sim.engine().last_price() == PriceTicks{100});
    sim.clear_events();

    REQUIRE(sim.step_back());
    CHECK(sim.save() == before_fill);
    CHECK(sim.events().empty());
    CHECK(sim.has_redo());
    CHECK_FALSE(sim.engine().last_price().has_value());
    CHECK(sim.last_sequence() < fill_events.front().seq);

    REQUIRE(sim.step_event()); // redo
    CHECK(std::vector<Event>(sim.events().begin(), sim.events().end()) == fill_events);
    CHECK_FALSE(sim.has_redo());
}

TEST_CASE("step_back also undoes inputs given after the last step", "[simulation]") {
    Simulation sim;
    sim.submit(limit(kAlice, Side::kSell, 100, 5));
    sim.step_tick();
    const std::vector<std::byte> after_tick = sim.save();
    const OrderId late = sim.submit(limit(kBob, Side::kBuy, 90, 1));
    REQUIRE(sim.step_back());
    // Back to before the tick advance, so the earlier order is accepted but the late one gone.
    CHECK(sim.engine().current_tick() == Tick{0});
    REQUIRE(sim.step_event()); // redo the tick advance
    CHECK(sim.save() == after_tick);
    REQUIRE(sim.step_event()); // redo replays the late submit, then its first step
    REQUIRE(sim.engine().find_order(late).has_value());
}

TEST_CASE("stepping back through a whole run revisits every recorded state",
          "[simulation][snapshots]") {
    const std::int64_t interval = GENERATE(1, 3, 0);
    const std::uint64_t seed = GENERATE(11ULL, 222ULL);
    INFO("snapshot interval " << interval << ", seed " << seed);

    Simulation sim{SimulationConfig{.snapshot_interval_ticks = interval}};
    const Recording recording = record_run(sim, seed, 120);
    const std::size_t end = sim.position();
    REQUIRE(end > 100);

    // All the way back, checking the exact state at each stop.
    std::size_t backs = 0;
    while (sim.step_back()) {
        ++backs;
        const auto it = recording.state_at.find(sim.position());
        REQUIRE(it != recording.state_at.end());
        REQUIRE(sim.save() == it->second);
        REQUIRE(sim.events().empty());
    }
    CHECK(sim.position() == 0);
    CHECK(backs > 0);

    // And all the way forward again: same states, and the same events, re-emitted in order.
    std::vector<Event> redone;
    while (sim.has_redo()) {
        REQUIRE(sim.step_event());
        REQUIRE(sim.save() == recording.state_at.at(sim.position()));
        drain(sim, redone);
    }
    CHECK(sim.position() == end);
    CHECK(redone == recording.events);
}

TEST_CASE("snapshots are taken every N ticks", "[simulation][snapshots]") {
    Simulation sim{SimulationConfig{.snapshot_interval_ticks = 4}};
    CHECK(sim.snapshot_count() == 1);
    for (int i = 0; i < 20; ++i) {
        sim.step_tick();
    }
    CHECK(sim.snapshot_count() == 1 + 5);

    // Redoing over existing snapshots does not duplicate them.
    for (int i = 0; i < 10; ++i) {
        sim.step_back();
    }
    while (sim.has_redo()) {
        sim.step_event();
    }
    CHECK(sim.snapshot_count() == 1 + 5);

    Simulation none{SimulationConfig{.snapshot_interval_ticks = 0}};
    for (int i = 0; i < 20; ++i) {
        none.step_tick();
    }
    CHECK(none.snapshot_count() == 1);
}

TEST_CASE("new input after step_back discards the recorded future", "[simulation]") {
    const std::uint64_t seed = GENERATE(5ULL, 66ULL);
    INFO("seed " << seed);
    Simulation sim{SimulationConfig{.snapshot_interval_ticks = 2}};
    record_run(sim, seed, 60);
    const std::size_t snapshots_at_end = sim.snapshot_count();
    for (int i = 0; i < 40; ++i) {
        sim.step_back();
    }
    const std::size_t branch_point = sim.position();
    REQUIRE(sim.has_redo());

    // Reference: the same history up to the branch point, built without time travel.
    Simulation reference{SimulationConfig{.snapshot_interval_ticks = 2}};
    record_run(reference, seed, 60);
    while (reference.position() > branch_point) {
        reference.step_back();
    }
    REQUIRE(reference.save() == sim.save());

    const OrderId branched = sim.submit(market(kDave, Side::kSell, 3));
    CHECK_FALSE(sim.has_redo());
    CHECK(sim.journal_size() == branch_point + 1);
    CHECK(sim.snapshot_count() <= snapshots_at_end);
    sim.step_tick();
    CHECK_FALSE(sim.engine().find_order(branched).has_value()); // market order finished

    // The branch keeps working with time travel of its own.
    sim.submit(limit(kAlice, Side::kBuy, 500, 1)); // far below the market: accept, rest
    sim.step_tick();
    const std::vector<std::byte> branch_end = sim.save();
    // Undo the tick advance and the "rest" step: the order is accepted but not yet resting.
    REQUIRE(sim.step_back());
    REQUIRE(sim.step_back());
    const std::optional<OrderView> pending = sim.engine().find_order(OrderId{branched.value() + 1});
    REQUIRE(pending);
    CHECK(pending->status == OrderStatus::kMatching);
    while (sim.has_redo()) {
        REQUIRE(sim.step_event());
    }
    CHECK(sim.save() == branch_end);
}
