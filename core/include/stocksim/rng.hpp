#pragma once

#include <array>
#include <cstdint>

namespace stocksim {

/// Seeded pseudo-random generator used for all randomness in the simulation.
///
/// xoshiro256** seeded through SplitMix64. Every operation is defined bit-exactly here
/// (no `std::*_distribution`, whose output differs between standard libraries), so the same
/// seed gives the same sequence on every platform and compiler.
class Rng {
public:
    using State = std::array<std::uint64_t, 4>;

    /// Creates a generator whose whole sequence is determined by `seed`.
    explicit Rng(std::uint64_t seed) noexcept;

    /// Restores a generator from a state previously returned by `state()`.
    /// An all-zero state is invalid for xoshiro and is replaced by the state for seed 0.
    [[nodiscard]] static Rng from_state(const State& state) noexcept;

    /// The full internal state, for snapshots.
    [[nodiscard]] const State& state() const noexcept { return m_state; }

    /// Next raw 64-bit output.
    [[nodiscard]] std::uint64_t next_u64() noexcept;

    /// Uniform integer in [0, bound). Unbiased (rejection sampling). Requires `bound > 0`.
    [[nodiscard]] std::uint64_t uniform_below(std::uint64_t bound) noexcept;

    /// Uniform integer in [lo, hi], inclusive. Requires `lo <= hi`.
    [[nodiscard]] std::int64_t uniform_int(std::int64_t lo, std::int64_t hi) noexcept;

    /// Uniform double in [0, 1) with 53 random bits. For agent heuristics only: never use the
    /// result for prices, balances, P&L, or ordering ties.
    [[nodiscard]] double uniform_unit() noexcept;

    /// Returns true with probability `numerator / denominator`.
    /// Requires `denominator > 0` and `numerator <= denominator`.
    [[nodiscard]] bool chance(std::uint64_t numerator, std::uint64_t denominator) noexcept;

    /// Derives an independent child generator for a numbered stream (e.g. one per agent
    /// partition), without advancing this generator. Same parent state + stream id gives the
    /// same child, which keeps multithreaded phases deterministic.
    [[nodiscard]] Rng derive(std::uint64_t stream_id) const noexcept;

private:
    Rng() noexcept = default;

    State m_state{};
};

} // namespace stocksim
