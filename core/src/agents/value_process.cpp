#include "stocksim/agents/value_process.hpp"

#include <algorithm>

namespace stocksim {

ValueProcess::ValueProcess(const ValueProcessConfig& config)
    : m_config(config), m_value(config.initial_value) {}

void ValueProcess::advance(Rng& rng) {
    // Always draw the same number of random values per tick, whatever happens, so the stream
    // stays aligned and runs remain comparable when parameters change.
    const bool moves = rng.chance(m_config.move_permille, 1'000);
    const bool up = rng.chance(1, 2);
    const bool jumps = rng.chance(m_config.jump_per_million, 1'000'000);
    const bool jump_up = rng.chance(1, 2);
    PriceTicks delta{};
    if (moves) {
        delta += up ? m_config.step : -m_config.step;
    }
    if (jumps) {
        delta += jump_up ? m_config.jump : -m_config.jump;
    }
    m_value = std::clamp(m_value + delta, PriceTicks{1}, kMaxPrice);
}

void ValueProcess::save_state(StateWriter& out) const {
    out.i64(m_value.value());
}

void ValueProcess::load_state(StateReader& in) {
    m_value = PriceTicks{in.i64()};
    if (m_value < PriceTicks{1} || m_value > kMaxPrice) {
        in.fail();
    }
}

} // namespace stocksim
