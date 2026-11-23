#include "stocksim/rng.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <limits>

using stocksim::Rng;

// Golden values come from an independent Python implementation of SplitMix64-seeded
// xoshiro256** (whose SplitMix64 output for seed 0, 0xE220A8397B1DCDAF, matches the published
// reference). They pin the sequence across compilers and platforms.
TEST_CASE("Rng matches reference xoshiro256** output", "[rng]") {
    SECTION("seed 0") {
        Rng rng{0};
        CHECK(rng.next_u64() == 0x99EC5F36CB75F2B4ULL);
        CHECK(rng.next_u64() == 0xBF6E1F784956452AULL);
        CHECK(rng.next_u64() == 0x1A5F849D4933E6E0ULL);
        CHECK(rng.next_u64() == 0x6AA594F1262D2D2CULL);
    }
    SECTION("seed 42") {
        Rng rng{42};
        CHECK(rng.next_u64() == 0x15780B2E0C2EC716ULL);
        CHECK(rng.next_u64() == 0x6104D9866D113A7EULL);
        CHECK(rng.next_u64() == 0xAE17533239E499A1ULL);
        CHECK(rng.next_u64() == 0xECB8AD4703B360A1ULL);
    }
}

TEST_CASE("Rng with the same seed produces the same sequence", "[rng]") {
    Rng first{12345};
    Rng second{12345};
    for (int i = 0; i < 1000; ++i) {
        REQUIRE(first.next_u64() == second.next_u64());
    }
}

TEST_CASE("Rng with different seeds diverges", "[rng]") {
    Rng first{1};
    Rng second{2};
    CHECK(first.next_u64() != second.next_u64());
}

TEST_CASE("Rng state round-trips for snapshots", "[rng]") {
    Rng original{777};
    for (int i = 0; i < 10; ++i) {
        static_cast<void>(original.next_u64());
    }
    Rng restored = Rng::from_state(original.state());
    for (int i = 0; i < 100; ++i) {
        REQUIRE(original.next_u64() == restored.next_u64());
    }
}

TEST_CASE("Rng rejects the invalid all-zero state", "[rng]") {
    const Rng restored = Rng::from_state(Rng::State{});
    const Rng seed_zero{0};
    CHECK(restored.state() == seed_zero.state());
}

TEST_CASE("Rng uniform_below stays in range and covers it", "[rng]") {
    Rng rng{99};
    std::array<int, 7> counts{};
    for (int i = 0; i < 7000; ++i) {
        const std::uint64_t value = rng.uniform_below(7);
        REQUIRE(value < 7);
        ++counts.at(value);
    }
    for (const int count : counts) {
        // Expected 1000 each; a fair generator stays well within these loose bounds.
        CHECK(count > 850);
        CHECK(count < 1150);
    }
}

TEST_CASE("Rng uniform_int is inclusive and handles extreme ranges", "[rng]") {
    Rng rng{5};
    bool saw_lo = false;
    bool saw_hi = false;
    for (int i = 0; i < 2000; ++i) {
        const std::int64_t value = rng.uniform_int(-3, 3);
        REQUIRE(value >= -3);
        REQUIRE(value <= 3);
        saw_lo = saw_lo || value == -3;
        saw_hi = saw_hi || value == 3;
    }
    CHECK(saw_lo);
    CHECK(saw_hi);

    CHECK(rng.uniform_int(10, 10) == 10);
    constexpr std::int64_t kMin = std::numeric_limits<std::int64_t>::min();
    constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
    static_cast<void>(rng.uniform_int(kMin, kMax)); // full range must not hang or assert
}

TEST_CASE("Rng uniform_unit is in [0, 1)", "[rng]") {
    Rng rng{8};
    for (int i = 0; i < 10000; ++i) {
        const double value = rng.uniform_unit();
        REQUIRE(value >= 0.0);
        REQUIRE(value < 1.0);
    }
}

TEST_CASE("Rng chance honours its edge probabilities", "[rng]") {
    Rng rng{3};
    for (int i = 0; i < 100; ++i) {
        REQUIRE_FALSE(rng.chance(0, 10));
        REQUIRE(rng.chance(10, 10));
    }
}

TEST_CASE("Rng derive is deterministic, independent, and does not advance the parent", "[rng]") {
    const Rng parent{2024};
    const Rng::State before = parent.state();
    Rng child_a = parent.derive(1);
    Rng child_a_again = parent.derive(1);
    Rng child_b = parent.derive(2);
    CHECK(parent.state() == before);
    const std::uint64_t a = child_a.next_u64();
    CHECK(a == child_a_again.next_u64());
    CHECK(a != child_b.next_u64());
}
