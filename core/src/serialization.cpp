#include "stocksim/serialization.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>

namespace stocksim {

namespace {

constexpr unsigned kBitsPerByte = 8;

void append_le(std::vector<std::byte>& out, std::uint64_t value, std::size_t width) {
    for (std::size_t i = 0; i < width; ++i) {
        out.push_back(static_cast<std::byte>((value >> (i * kBitsPerByte)) & 0xFFU));
    }
}

} // namespace

void StateWriter::u8(std::uint8_t value) {
    append_le(m_bytes, value, 1);
}

void StateWriter::u32(std::uint32_t value) {
    append_le(m_bytes, value, 4);
}

void StateWriter::u64(std::uint64_t value) {
    append_le(m_bytes, value, 8);
}

void StateWriter::i64(std::int64_t value) {
    append_le(m_bytes, std::bit_cast<std::uint64_t>(value), 8);
}

std::uint64_t StateReader::read_le(std::size_t width) noexcept {
    if (m_failed || remaining() < width) {
        m_failed = true;
        return 0;
    }
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < width; ++i) {
        value |= std::to_integer<std::uint64_t>(m_bytes[m_offset + i]) << (i * kBitsPerByte);
    }
    m_offset += width;
    return value;
}

std::uint8_t StateReader::u8() noexcept {
    return static_cast<std::uint8_t>(read_le(1));
}

std::uint32_t StateReader::u32() noexcept {
    return static_cast<std::uint32_t>(read_le(4));
}

std::uint64_t StateReader::u64() noexcept {
    return read_le(8);
}

std::int64_t StateReader::i64() noexcept {
    return std::bit_cast<std::int64_t>(read_le(8));
}

bool StateReader::boolean() noexcept {
    const std::uint8_t value = u8();
    if (value > 1) {
        fail();
    }
    return value == 1;
}

std::uint64_t StateReader::count(std::size_t min_element_bytes) noexcept {
    const std::uint64_t value = u64();
    if (min_element_bytes > 0 && value > remaining() / min_element_bytes) {
        fail();
        return 0;
    }
    return value;
}

} // namespace stocksim
