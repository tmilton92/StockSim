#include "stocksim/event_ring.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

namespace stocksim {

EventRing::EventRing(std::size_t min_capacity)
    : m_slots(std::bit_ceil(std::max<std::size_t>(min_capacity, 1))),
      m_mask(static_cast<std::uint64_t>(m_slots.size()) - 1) {}

std::size_t EventRing::free_space() const noexcept {
    const std::uint64_t head = m_head.load(std::memory_order_relaxed);
    const std::uint64_t tail = m_tail.load(std::memory_order_acquire);
    return m_slots.size() - static_cast<std::size_t>(head - tail);
}

bool EventRing::try_push(std::span<const Event> events) noexcept {
    const std::uint64_t head = m_head.load(std::memory_order_relaxed);
    const std::uint64_t tail = m_tail.load(std::memory_order_acquire);
    const auto used = static_cast<std::size_t>(head - tail);
    if (events.size() > m_slots.size() - used) {
        return false;
    }
    for (std::size_t i = 0; i < events.size(); ++i) {
        m_slots[static_cast<std::size_t>((head + i) & m_mask)] = events[i];
    }
    m_head.store(head + events.size(), std::memory_order_release);
    return true;
}

std::size_t EventRing::pop(std::span<Event> out) noexcept {
    const std::uint64_t tail = m_tail.load(std::memory_order_relaxed);
    const std::uint64_t head = m_head.load(std::memory_order_acquire);
    const std::size_t count = std::min(static_cast<std::size_t>(head - tail), out.size());
    for (std::size_t i = 0; i < count; ++i) {
        out[i] = m_slots[static_cast<std::size_t>((tail + i) & m_mask)];
    }
    m_tail.store(tail + count, std::memory_order_release);
    return count;
}

std::size_t EventRing::size() const noexcept {
    const std::uint64_t tail = m_tail.load(std::memory_order_acquire);
    const std::uint64_t head = m_head.load(std::memory_order_acquire);
    return static_cast<std::size_t>(head - tail);
}

} // namespace stocksim
