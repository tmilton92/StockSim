#pragma once

#include "stocksim/event.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace stocksim {

/// Fixed-capacity, lock-free, single-producer/single-consumer FIFO of events: the hand-off
/// between the thread that steps the simulation and the thread that renders it.
///
/// Exactly one thread may act as producer and one as consumer at any moment. The producer role
/// may move between threads only if those threads synchronize with each other (e.g. both push
/// while holding the same mutex); the same holds for the consumer role.
///
/// Pushes are all-or-nothing, so a step's events are never split; the producer is expected to
/// check `free_space()` before stepping and wait (backpressure) rather than drop events.
class EventRing {
public:
    /// Capacity is `min_capacity` rounded up to a power of two (at least 1).
    explicit EventRing(std::size_t min_capacity);

    EventRing(const EventRing&) = delete;
    EventRing& operator=(const EventRing&) = delete;
    EventRing(EventRing&&) = delete;
    EventRing& operator=(EventRing&&) = delete;
    ~EventRing() = default;

    [[nodiscard]] std::size_t capacity() const noexcept { return m_slots.size(); }

    /// Producer: free slots. Exact for the producer (it can only grow concurrently).
    [[nodiscard]] std::size_t free_space() const noexcept;

    /// Producer: appends all of `events`, or nothing if they do not fit. Returns success.
    bool try_push(std::span<const Event> events) noexcept;

    /// Consumer: moves up to `out.size()` of the oldest events into `out`; returns the count.
    std::size_t pop(std::span<Event> out) noexcept;

    /// Number of queued events. Exact for the consumer (it can only grow concurrently).
    [[nodiscard]] std::size_t size() const noexcept;

private:
    std::vector<Event> m_slots;
    std::uint64_t m_mask;
    /// Total events ever pushed / popped; indices are counters masked by capacity - 1.
    std::atomic<std::uint64_t> m_head{0};
    std::atomic<std::uint64_t> m_tail{0};
};

} // namespace stocksim
