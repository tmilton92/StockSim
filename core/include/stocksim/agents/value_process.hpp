#pragma once

#include "stocksim/agents/agent_config.hpp"
#include "stocksim/rng.hpp"
#include "stocksim/serialization.hpp"
#include "stocksim/types.hpp"

namespace stocksim {

/// The hidden true value of the company, in ticks: a bounded random walk with rare jumps.
/// Nothing in the market sees it directly; it is what value traders estimate (with noise) and
/// what lessons compare the price against.
class ValueProcess {
public:
    explicit ValueProcess(const ValueProcessConfig& config);

    /// Advances one tick using [rng]. The value never drops below one tick.
    void advance(Rng& rng);

    [[nodiscard]] PriceTicks value() const noexcept { return m_value; }

    void save_state(StateWriter& out) const;
    void load_state(StateReader& in);

private:
    ValueProcessConfig m_config;
    PriceTicks m_value;
};

} // namespace stocksim
