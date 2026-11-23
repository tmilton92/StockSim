#pragma once

#include <compare>
#include <cstddef>
#include <functional>

namespace stocksim {

/// Opaque identifier wrapping an integer. Comparable and hashable; no arithmetic, and no
/// implicit conversion to or from other id types, so `AccountId` and `OrderId` cannot mix.
template <typename Tag, typename Rep>
class StrongId {
public:
    using RepType = Rep;

    constexpr StrongId() noexcept = default;
    constexpr explicit StrongId(Rep value) noexcept : m_value(value) {}

    [[nodiscard]] constexpr Rep value() const noexcept { return m_value; }

    friend constexpr auto operator<=>(const StrongId&, const StrongId&) noexcept = default;

private:
    Rep m_value{};
};

/// Additive integer quantity (prices in ticks, quantities in lots, ticks of time). Supports
/// addition and subtraction with the same type only; scaling by other units is deliberately
/// not provided so units cannot be mixed by accident.
template <typename Tag, typename Rep>
class StrongQuantity {
public:
    using RepType = Rep;

    constexpr StrongQuantity() noexcept = default;
    constexpr explicit StrongQuantity(Rep value) noexcept : m_value(value) {}

    [[nodiscard]] constexpr Rep value() const noexcept { return m_value; }

    constexpr StrongQuantity& operator+=(StrongQuantity other) noexcept {
        m_value += other.m_value;
        return *this;
    }

    constexpr StrongQuantity& operator-=(StrongQuantity other) noexcept {
        m_value -= other.m_value;
        return *this;
    }

    friend constexpr StrongQuantity operator+(StrongQuantity lhs, StrongQuantity rhs) noexcept {
        lhs += rhs;
        return lhs;
    }

    friend constexpr StrongQuantity operator-(StrongQuantity lhs, StrongQuantity rhs) noexcept {
        lhs -= rhs;
        return lhs;
    }

    friend constexpr StrongQuantity operator-(StrongQuantity operand) noexcept {
        return StrongQuantity{static_cast<Rep>(-operand.m_value)};
    }

    friend constexpr auto operator<=>(const StrongQuantity&,
                                      const StrongQuantity&) noexcept = default;

private:
    Rep m_value{};
};

/// Hash functor for strong types, for use as the `Hash` parameter of unordered containers.
/// Unordered containers are for lookup only: never iterate them where order affects results.
struct StrongHash {
    template <typename T>
        requires requires(const T& strong) { strong.value(); }
    [[nodiscard]] std::size_t operator()(const T& strong) const noexcept {
        return std::hash<typename T::RepType>{}(strong.value());
    }
};

} // namespace stocksim
