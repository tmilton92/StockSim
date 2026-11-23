#pragma once

// Seeded random order flow shared by property, snapshot, and determinism tests. Works with any
// target exposing submit/cancel/modify (MatchingEngine, Simulation).

#include "stocksim/order.hpp"
#include "stocksim/rng.hpp"
#include "stocksim/types.hpp"

#include <cstdint>
#include <vector>

namespace stocksim::test {

class RandomFlow {
public:
    static constexpr std::int64_t kMidPrice = 1000;
    static constexpr std::int64_t kPriceSpread = 15;
    static constexpr std::uint32_t kAccountCount = 6;

    explicit RandomFlow(std::uint64_t seed) : m_rng(seed) {}

    /// Issues one random request: usually a new order (all types, occasionally invalid),
    /// otherwise a cancel or modify of a previously issued id, sometimes from the wrong account.
    template <typename Target>
    void issue_request(Target& target) {
        const std::uint64_t roll = m_rng.uniform_below(100);
        if (roll < 75 || m_issued.empty()) {
            m_issued.push_back(target.submit(random_order()));
            return;
        }
        const OrderId id = m_issued[m_rng.uniform_below(m_issued.size())];
        const AccountId requester{static_cast<std::uint32_t>(m_rng.uniform_below(2)) + 1};
        if (roll < 90) {
            target.cancel(requester, id);
        } else {
            // Draw into locals: the evaluation order of function arguments is unspecified, so
            // two draws in one argument list run in different orders on different compilers
            // (it differed between MSVC/GCC and Clang, breaking the golden digest on macOS).
            const PriceTicks price = random_price();
            const Qty qty{m_rng.uniform_int(1, 60)};
            target.modify(requester, id, price, qty);
        }
    }

    /// The flow's generator, for test-side choices (e.g. how many steps to take next).
    [[nodiscard]] Rng& rng() noexcept { return m_rng; }

private:
    PriceTicks random_price() {
        return PriceTicks{m_rng.uniform_int(kMidPrice - kPriceSpread, kMidPrice + kPriceSpread)};
    }

    NewOrder random_order() {
        const AccountId account{static_cast<std::uint32_t>(m_rng.uniform_below(kAccountCount)) + 1};
        const Side side = m_rng.chance(1, 2) ? Side::kBuy : Side::kSell;
        const Qty qty{m_rng.uniform_int(1, 50)};
        const PriceTicks price = random_price();
        const PriceTicks other = random_price();
        const std::uint64_t roll = m_rng.uniform_below(100);
        NewOrder order{.account = account, .side = side, .qty = qty};
        if (roll < 60) {
            order.type = OrderType::kLimit;
            order.limit_price = price;
        } else if (roll < 75) {
            order.type = OrderType::kMarket;
        } else if (roll < 88) {
            order.type = OrderType::kStop;
            order.stop_price = price;
        } else if (roll < 97) {
            order.type = OrderType::kStopLimit;
            order.stop_price = price;
            order.limit_price = other;
        } else {
            order.type = OrderType::kLimit;
            order.qty = Qty{m_rng.uniform_int(-2, 0)}; // occasionally invalid
            order.limit_price = price;
        }
        return order;
    }

    Rng m_rng;
    std::vector<OrderId> m_issued;
};

} // namespace stocksim::test
