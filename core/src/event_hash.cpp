#include "stocksim/event_hash.hpp"

#include <bit>
#include <cstdint>
#include <span>

namespace stocksim {

namespace {

constexpr std::uint64_t kFnvOffsetBasis = 0xCBF29CE484222325ULL;
constexpr std::uint64_t kFnvPrime = 0x00000100000001B3ULL;
constexpr unsigned kBitsPerByte = 8;

} // namespace

EventLogHasher::EventLogHasher() noexcept : m_state(kFnvOffsetBasis) {
    mix(kEventSchemaVersion, 4);
}

void EventLogHasher::mix(std::uint64_t value, unsigned width) noexcept {
    for (unsigned i = 0; i < width; ++i) {
        m_state ^= (value >> (i * kBitsPerByte)) & 0xFFU;
        m_state *= kFnvPrime;
    }
}

void EventLogHasher::add(const Event& event) noexcept {
    mix(event.seq.value(), 8);
    mix(std::bit_cast<std::uint64_t>(event.tick.value()), 8);
    mix(static_cast<std::uint8_t>(event.type), 1);
    mix(static_cast<std::uint8_t>(event.reason), 1);
    mix(static_cast<std::uint8_t>(event.intent), 1);
    mix(static_cast<std::uint8_t>(event.side), 1);
    mix(static_cast<std::uint8_t>(event.order_type), 1);
    mix(event.order_id.value(), 8);
    mix(event.counter_order_id.value(), 8);
    mix(event.account.value(), 4);
    mix(event.counter_account.value(), 4);
    mix(std::bit_cast<std::uint64_t>(event.price.value()), 8);
    mix(std::bit_cast<std::uint64_t>(event.aux_price.value()), 8);
    mix(std::bit_cast<std::uint64_t>(event.qty.value()), 8);
    mix(std::bit_cast<std::uint64_t>(event.remaining.value()), 8);
    ++m_count;
}

void EventLogHasher::add(std::span<const Event> events) noexcept {
    for (const Event& event : events) {
        add(event);
    }
}

} // namespace stocksim
