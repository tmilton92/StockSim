#pragma once

#include "stocksim/agents/agent_config.hpp"
#include "stocksim/agents/agent_population.hpp"
#include "stocksim/agents/market_maker.hpp"
#include "stocksim/agents/value_process.hpp"
#include "stocksim/result.hpp"
#include "stocksim/rng.hpp"
#include "stocksim/serialization.hpp"
#include "stocksim/types.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace stocksim {

class MatchingEngine;

/// The simulated crowd: the hidden value process, the trader population, and the market maker.
///
/// It acts once per tick ([on_tick_start]), after the tick advances and before any of the new
/// tick's steps: the value moves, the market maker re-quotes, then every agent decides from the
/// same market view and its orders are submitted in agent order. Each agent draws from its own
/// random stream derived from that tick's seed, so its decision does not depend on how many
/// other agents exist or in what order they are evaluated. Agents are ordinary limited accounts:
/// they submit and cancel orders, and the exchange checks them like anyone else.
class AgentWorld {
public:
    explicit AgentWorld(const AgentWorldConfig& config);

    /// Opens every agent's (and the market maker's) account. Call once, before any trading.
    void open_accounts(MatchingEngine& engine) const;

    /// The crowd's turn at the start of a tick. Submits and cancels orders on [engine].
    void on_tick_start(MatchingEngine& engine);

    [[nodiscard]] PriceTicks true_value() const noexcept { return m_value.value(); }
    [[nodiscard]] const AgentPopulation& agents() const noexcept { return m_agents; }
    [[nodiscard]] const std::optional<MarketMaker>& market_maker() const noexcept {
        return m_maker;
    }
    /// Reference price at the end of each recent tick, oldest first.
    [[nodiscard]] std::span<const PriceTicks> history() const noexcept { return m_history; }

    void save_state(StateWriter& out) const;
    [[nodiscard]] static Result<AgentWorld> load_state(StateReader& in);

private:
    AgentWorld() = default;

    /// Last trade price, else the mid of the best quotes, else the opening reference.
    [[nodiscard]] PriceTicks reference_price(const MatchingEngine& engine) const;
    void run_market_maker(MatchingEngine& engine);
    void run_agent(MatchingEngine& engine, std::size_t index, const MarketView& view,
                   const Rng& tick_rng);

    /// Static settings kept after construction (the population holds the per-agent ones).
    std::uint64_t m_seed = 0;
    ValueProcessConfig m_value_config{};
    MarketMakerConfig m_maker_config{};
    std::uint32_t m_history_ticks = 64;

    Rng m_rng{0};
    ValueProcess m_value{ValueProcessConfig{}};
    AgentPopulation m_agents{};
    std::optional<MarketMaker> m_maker;
    std::vector<PriceTicks> m_history;
};

} // namespace stocksim
