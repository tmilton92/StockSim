#include "stocksim/rng.hpp"

#include <bit>
#include <cassert>
#include <cstdint>

namespace stocksim {

namespace {

constexpr std::uint64_t kGoldenGamma = 0x9E3779B97F4A7C15ULL;

/// SplitMix64 step: advances `state` and returns the next output.
constexpr std::uint64_t splitmix64(std::uint64_t& state) noexcept {
    state += kGoldenGamma;
    std::uint64_t mixed = state;
    mixed = (mixed ^ (mixed >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    mixed = (mixed ^ (mixed >> 27U)) * 0x94D049BB133111EBULL;
    return mixed ^ (mixed >> 31U);
}

constexpr Rng::State seed_state(std::uint64_t seed) noexcept {
    Rng::State state{};
    for (std::uint64_t& word : state) {
        word = splitmix64(seed);
    }
    return state;
}

} // namespace

Rng::Rng(std::uint64_t seed) noexcept : m_state(seed_state(seed)) {}

Rng Rng::from_state(const State& state) noexcept {
    Rng rng;
    const bool all_zero = state[0] == 0 && state[1] == 0 && state[2] == 0 && state[3] == 0;
    rng.m_state = all_zero ? seed_state(0) : state;
    return rng;
}

std::uint64_t Rng::next_u64() noexcept {
    const std::uint64_t result = std::rotl(m_state[1] * 5U, 7) * 9U;
    const std::uint64_t shifted = m_state[1] << 17U;
    m_state[2] ^= m_state[0];
    m_state[3] ^= m_state[1];
    m_state[1] ^= m_state[2];
    m_state[0] ^= m_state[3];
    m_state[2] ^= shifted;
    m_state[3] = std::rotl(m_state[3], 45);
    return result;
}

std::uint64_t Rng::uniform_below(std::uint64_t bound) noexcept {
    assert(bound > 0);
    // Reject the low `2^64 mod bound` values so every residue is equally likely.
    const std::uint64_t threshold = (0U - bound) % bound;
    for (;;) {
        const std::uint64_t value = next_u64();
        if (value >= threshold) {
            return value % bound;
        }
    }
}

std::int64_t Rng::uniform_int(std::int64_t lo, std::int64_t hi) noexcept {
    assert(lo <= hi);
    const std::uint64_t span =
        std::bit_cast<std::uint64_t>(hi) - std::bit_cast<std::uint64_t>(lo) + 1U;
    // span == 0 means the full 64-bit range was requested.
    const std::uint64_t offset = span == 0 ? next_u64() : uniform_below(span);
    return std::bit_cast<std::int64_t>(std::bit_cast<std::uint64_t>(lo) + offset);
}

double Rng::uniform_unit() noexcept {
    constexpr double kTwoPowMinus53 = 1.0 / static_cast<double>(std::uint64_t{1} << 53U);
    return static_cast<double>(next_u64() >> 11U) * kTwoPowMinus53;
}

bool Rng::chance(std::uint64_t numerator, std::uint64_t denominator) noexcept {
    assert(denominator > 0 && numerator <= denominator);
    return uniform_below(denominator) < numerator;
}

Rng Rng::derive(std::uint64_t stream_id) const noexcept {
    // Fold the parent state and stream id through SplitMix64 to get a well-mixed child seed.
    std::uint64_t mix = stream_id;
    std::uint64_t seed = splitmix64(mix);
    for (const std::uint64_t word : m_state) {
        mix ^= word;
        seed ^= splitmix64(mix);
    }
    return Rng{seed};
}

} // namespace stocksim
