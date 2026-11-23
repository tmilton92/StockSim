#pragma once

#include "stocksim/event.hpp"

#include <cstdint>
#include <span>

namespace stocksim {

/// Running 64-bit FNV-1a hash of an event log, used by determinism tests and CI: the same
/// seed and inputs must give the same digest on every platform.
///
/// Each field is hashed as fixed-width little-endian bytes (never the raw struct, whose padding
/// is unspecified), together with `kEventSchemaVersion`, so a schema change changes the digest.
class EventLogHasher {
public:
    EventLogHasher() noexcept;

    void add(const Event& event) noexcept;
    void add(std::span<const Event> events) noexcept;

    [[nodiscard]] std::uint64_t digest() const noexcept { return m_state; }
    [[nodiscard]] std::uint64_t event_count() const noexcept { return m_count; }

private:
    void mix(std::uint64_t value, unsigned width) noexcept;

    std::uint64_t m_state;
    std::uint64_t m_count = 0;
};

} // namespace stocksim
