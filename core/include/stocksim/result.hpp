#pragma once

#include <cassert>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

namespace stocksim {

enum class ErrorCode : std::uint8_t {
    /// The data does not start with the stocksim state magic number.
    kBadMagic,
    /// The data was written by an incompatible state format or event schema version.
    kUnsupportedVersion,
    /// The data is truncated or contains values that cannot describe a valid state.
    kCorruptState,
    /// A configuration value is out of its allowed range.
    kInvalidConfig,
};

/// An error from a fallible core operation. The message is static text; no allocation.
struct Error {
    ErrorCode code = ErrorCode::kCorruptState;
    std::string_view message;
};

/// Value-or-error return type for fallible core operations (stand-in for C++23
/// `std::expected`, which C++20 does not have). Core never throws across its API.
template <typename T>
class [[nodiscard]] Result {
public:
    // Implicit by design, so functions can `return value;` or `return Error{...};`.
    Result(T value) : m_value(std::move(value)) {}
    Result(Error error) noexcept : m_error(error) {}

    [[nodiscard]] bool has_value() const noexcept { return m_value.has_value(); }
    [[nodiscard]] explicit operator bool() const noexcept { return has_value(); }

    /// Requires `has_value()`.
    [[nodiscard]] T& value() & {
        assert(m_value);
        return *m_value;
    }
    [[nodiscard]] const T& value() const& {
        assert(m_value);
        return *m_value;
    }
    [[nodiscard]] T&& value() && {
        assert(m_value);
        return std::move(*m_value);
    }

    /// Requires `!has_value()`.
    [[nodiscard]] const Error& error() const noexcept {
        assert(!m_value);
        return m_error;
    }

private:
    std::optional<T> m_value;
    Error m_error{};
};

} // namespace stocksim
