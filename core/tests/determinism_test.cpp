// Determinism (TODO §2): the same seed and inputs must produce the same event log, bit for bit,
// on every platform. CI runs this on Linux, Windows and macOS, so the pinned digests below
// also catch cross-platform divergence.

#include "random_flow.hpp"
#include "stocksim/event_hash.hpp"
#include "stocksim/simulation.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstdint>
#include <ios>

using namespace stocksim;
using namespace stocksim::test;

namespace {

// Pinned results of run_session(2024); see the golden test below. Re-pinned for event schema v2
// (ADR 0010): same 1435 events, different digest because the schema version is hashed. Also
// reflects RandomFlow drawing modify() arguments in a fixed order (it varied by compiler).
constexpr std::uint64_t kGoldenEventCount = 1435;
constexpr std::uint64_t kGoldenDigest = 0xE612B6E34B22DB5BULL;

struct SessionDigest {
    std::uint64_t digest = 0;
    std::uint64_t events = 0;
};

/// A seeded session mixing inputs, single steps, whole ticks and time travel.
SessionDigest run_session(std::uint64_t seed) {
    Simulation sim{SimulationConfig{.snapshot_interval_ticks = 8}};
    RandomFlow flow{seed};
    EventLogHasher hasher;
    for (int round = 0; round < 400; ++round) {
        flow.issue_request(sim);
        const std::uint64_t action = flow.rng().uniform_below(10);
        if (action < 6) {
            sim.step_tick();
        } else if (action < 9) {
            sim.step_event();
        } else {
            // Rewind a little, then continue; events already hashed are not hashed again.
            sim.step_back();
            sim.step_back();
            sim.clear_events();
            while (sim.has_redo()) {
                sim.step_event();
                sim.clear_events();
            }
        }
        hasher.add(sim.events());
        sim.clear_events();
    }
    sim.step_tick();
    hasher.add(sim.events());
    return SessionDigest{.digest = hasher.digest(), .events = hasher.event_count()};
}

} // namespace

TEST_CASE("event hashing matches the reference FNV-1a encoding", "[determinism]") {
    // Reference digests computed by an independent Python implementation of the documented
    // encoding (schema version, then each field as fixed-width little-endian bytes). Schema
    // v2 (ADR 0010) changed the version mixed in first, and so every digest.
    const EventLogHasher empty;
    CHECK(empty.digest() == 0x8D1ACE904A398D17ULL);

    Event event;
    event.seq = SequenceNumber{1};
    event.tick = Tick{2};
    event.type = EventType::kMatch;
    event.reason = Reason::kTrade;
    event.intent = BrokerIntent::kNone;
    event.side = Side::kSell;
    event.order_type = OrderType::kLimit;
    event.order_id = OrderId{5};
    event.counter_order_id = OrderId{3};
    event.account = AccountId{7};
    event.counter_account = AccountId{9};
    event.price = PriceTicks{1000};
    event.aux_price = PriceTicks{-1};
    event.qty = Qty{25};
    event.remaining = Qty{0};
    EventLogHasher one;
    one.add(event);
    CHECK(one.digest() == 0xC0BFA36134C79141ULL);
    CHECK(one.event_count() == 1);
}

TEST_CASE("event hashing is order sensitive", "[determinism]") {
    Event first;
    first.seq = SequenceNumber{1};
    Event second;
    second.seq = SequenceNumber{2};
    EventLogHasher forward;
    forward.add(first);
    forward.add(second);
    EventLogHasher reverse;
    reverse.add(second);
    reverse.add(first);
    CHECK(forward.digest() != reverse.digest());
}

TEST_CASE("same seed gives the same event log hash", "[determinism]") {
    const std::uint64_t seed = GENERATE(1ULL, 2ULL, 31337ULL);
    INFO("seed " << seed);
    const SessionDigest first = run_session(seed);
    const SessionDigest second = run_session(seed);
    CHECK(first.events > 1000);
    CHECK(first.digest == second.digest);
    CHECK(first.events == second.events);
}

TEST_CASE("different seeds give different event logs", "[determinism]") {
    CHECK(run_session(1).digest != run_session(2).digest);
}

TEST_CASE("event log hash is pinned across platforms and builds", "[determinism][golden]") {
    // If this fails after an intentional behaviour or schema change, update the pinned values
    // in the same change and say so in the commit message. If it fails on only one platform,
    // something nondeterministic has crept in: that is a bug.
    const SessionDigest session = run_session(2024);
    INFO("digest 0x" << std::hex << session.digest << std::dec << ", events " << session.events);
    CHECK(session.events == kGoldenEventCount);
    CHECK(session.digest == kGoldenDigest);
}
