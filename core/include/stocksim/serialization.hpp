#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace stocksim {

/// "SSIM" as a little-endian u32; first field of every saved simulation state.
inline constexpr std::uint32_t kStateMagic = 0x4D495353;

/// Version of the binary state format. Bump when the layout written by `save_state` changes.
inline constexpr std::uint32_t kStateFormatVersion = 2;

/// Appends fixed-width little-endian integers to a byte buffer. The encoding is defined
/// byte-by-byte, so output is identical on every platform.
class StateWriter {
public:
    void u8(std::uint8_t value);
    void u32(std::uint32_t value);
    void u64(std::uint64_t value);
    void i64(std::int64_t value);
    void boolean(bool value) { u8(value ? 1U : 0U); }

    [[nodiscard]] const std::vector<std::byte>& bytes() const noexcept { return m_bytes; }
    [[nodiscard]] std::vector<std::byte> take() noexcept { return std::move(m_bytes); }

private:
    std::vector<std::byte> m_bytes;
};

/// Reads what `StateWriter` wrote. Reading past the end, or calling `fail()`, puts the reader
/// in a failed state: later reads return 0 and `ok()` is false. Callers check `ok()` once
/// after a group of reads instead of after every field.
class StateReader {
public:
    explicit StateReader(std::span<const std::byte> bytes) noexcept : m_bytes(bytes) {}

    [[nodiscard]] std::uint8_t u8() noexcept;
    [[nodiscard]] std::uint32_t u32() noexcept;
    [[nodiscard]] std::uint64_t u64() noexcept;
    [[nodiscard]] std::int64_t i64() noexcept;
    /// Reads a u8 that must be 0 or 1; anything else fails the reader.
    [[nodiscard]] bool boolean() noexcept;

    /// Marks the data as invalid (e.g. an out-of-range enum value).
    void fail() noexcept { m_failed = true; }
    [[nodiscard]] bool ok() const noexcept { return !m_failed; }
    [[nodiscard]] std::size_t remaining() const noexcept { return m_bytes.size() - m_offset; }

    /// Reads an element count and fails the reader if `count * min_element_bytes` could not
    /// fit in the remaining data, so corrupt counts cannot trigger huge allocations.
    [[nodiscard]] std::uint64_t count(std::size_t min_element_bytes) noexcept;

private:
    [[nodiscard]] std::uint64_t read_le(std::size_t width) noexcept;

    std::span<const std::byte> m_bytes;
    std::size_t m_offset = 0;
    bool m_failed = false;
};

} // namespace stocksim
