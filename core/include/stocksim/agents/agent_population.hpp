#pragma once

#include "stocksim/account_ledger.hpp"
#include "stocksim/agents/agent_config.hpp"
#include "stocksim/order.hpp"
#include "stocksim/rng.hpp"
#include "stocksim/serialization.hpp"
#include "stocksim/types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace stocksim {

/// Every agent, stored as a structure of arrays (one vector per field, indexed by agent):
/// cache-friendly for large populations and cheap to serialize. Static parameters are drawn once
/// from the world seed; the dynamic fields track the agent's own orders between ticks.
struct AgentPopulation {
    // Static parameters.
    std::vector<TraderStyle> style;
    std::vector<AccountId> account;
    std::vector<Money> capital;
    std::vector<Qty> initial_shares;
    std::vector<std::int32_t> leverage_percent;
    std::vector<std::uint16_t> attention_permille;
    std::vector<std::uint16_t> risk_permille;
    std::vector<std::uint16_t> social_permille;
    std::vector<std::uint32_t> reaction_delay;
    std::vector<std::uint32_t> stop_distance_bps;
    /// 1 if the agent protects new positions with a stop order.
    std::vector<std::uint8_t> uses_stops;

    // Dynamic state.
    /// The agent's latest limit order (it may since have filled or been cancelled).
    std::vector<OrderId> working_order;
    std::vector<Tick> working_since;
    /// The agent's protective sell stop, if any, and how many shares it covers.
    std::vector<OrderId> stop_order;
    std::vector<Qty> stop_covered;
    /// Reference price when the agent last increased its position.
    std::vector<PriceTicks> entry_price;
    /// Position seen at the agent's previous decision.
    std::vector<Qty> last_position;

    /// Draws a population from [config] (styles in TraderStyle order, accounts consecutive).
    [[nodiscard]] static AgentPopulation generate(const AgentWorldConfig& config, Rng& rng);

    [[nodiscard]] std::size_t size() const noexcept { return style.size(); }

    void save_state(StateWriter& out) const;
    /// Fails the reader on invalid data.
    void load_state(StateReader& in);
};

/// What one agent sees at the start of a tick. The same view is given to every agent.
struct MarketView {
    Tick tick{};
    /// Last trade price, or the mid of the best quotes, or the opening reference.
    PriceTicks reference{};
    std::optional<PriceTicks> best_bid{};
    std::optional<PriceTicks> best_ask{};
    /// Reference price at the end of each recent tick, oldest first (at least one entry).
    std::span<const PriceTicks> history{};
    Qty taker_buy_volume{};
    Qty taker_sell_volume{};
    /// The hidden true value. Only value traders read it, through their own noise.
    PriceTicks true_value{};
};

/// What an agent wants to do this tick. The world applies it in this order: cancel the working
/// order, cancel the stop, submit [order], submit [stop].
struct AgentDecision {
    bool cancel_working = false;
    bool cancel_stop = false;
    std::optional<NewOrder> order{};
    /// A protective sell stop covering the agent's whole position.
    std::optional<NewOrder> stop{};
};

/// Decides agent [index]'s action. Pure: reads only its arguments, so every style can be
/// tested in isolation and decisions can later run in parallel (TODO section 6b).
/// [working_order_live] says whether the agent's working order is still on the book.
[[nodiscard]] AgentDecision decide(const AgentPopulation& agents, std::size_t index,
                                   const MarketView& view, const AccountView& account,
                                   bool working_order_live, Rng& rng);

} // namespace stocksim
