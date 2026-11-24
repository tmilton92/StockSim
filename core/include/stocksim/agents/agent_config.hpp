#pragma once

#include "stocksim/result.hpp"
#include "stocksim/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace stocksim {

/// How a simulated trader decides. Every style is an ordinary client of the exchange: it only
/// submits and cancels orders, within its account's limits.
enum class TraderStyle : std::uint8_t {
    /// Trades at random around the price: the background order flow.
    kNoise,
    /// Buys after the price has risen, sells after it has fallen.
    kMomentum,
    /// Buys below its recent average price, sells above it.
    kMeanReversion,
    /// Follows the crowd: buys when other takers were buying (FOMO), sells when they sold.
    kHerding,
    /// Holds shares and dumps them all when the price drops far from its recent high.
    kPanicSeller,
    /// Bets big with leverage on the latest move and protects itself with tight stops.
    kLeverageGambler,
    /// Compares the price to its (noisy) estimate of the hidden true value.
    kValue,
};

inline constexpr std::size_t kTraderStyleCount = 7;

[[nodiscard]] std::string_view to_string(TraderStyle style) noexcept;

/// Hidden "true value" of the company: a random walk with rare jumps, in ticks. Only value
/// traders see it, and only through noise.
struct ValueProcessConfig {
    PriceTicks initial_value{10'000};
    /// Chance per tick (per mille) that the value moves by one step, up or down.
    std::uint32_t move_permille = 300;
    PriceTicks step{1};
    /// Chance per tick (per million) of a jump (news), up or down.
    std::uint32_t jump_per_million = 500;
    PriceTicks jump{100};
};

/// Number of agents of one style and the ranges their parameters are drawn from (uniformly,
/// per agent, from the world seed). All values are integers.
struct StyleConfig {
    std::uint32_t count = 0;
    Money capital_min{100'000};
    Money capital_max{1'000'000};
    Qty shares_min{0};
    Qty shares_max{0};
    /// Leverage as a percentage (100 = 1x).
    std::int32_t leverage_percent_min = 100;
    std::int32_t leverage_percent_max = 100;
    /// Chance per tick (per mille) that the agent considers trading at all.
    std::uint32_t attention_permille_min = 50;
    std::uint32_t attention_permille_max = 200;
    /// How many ticks old the price the agent reacts to is.
    std::uint32_t reaction_delay_max = 0;
    /// Share of equity (per mille) put into one order.
    std::uint32_t risk_permille_min = 20;
    std::uint32_t risk_permille_max = 100;
    /// Chance (per mille) that the agent protects a new position with a stop order.
    std::uint32_t stop_habit_permille = 0;
    /// Stop distance below the entry price, in basis points.
    std::uint32_t stop_distance_bps_min = 100;
    std::uint32_t stop_distance_bps_max = 300;
    /// Herding: how strongly (per mille) the agent follows the crowd's net buying.
    std::uint32_t social_permille_min = 0;
    std::uint32_t social_permille_max = 0;
};

/// A market maker quoting both sides around the last price, skewing its quotes against its
/// inventory so it does not end up holding too much or too little.
struct MarketMakerConfig {
    bool enabled = false;
    AccountId account{90'000};
    Money capital{1'000'000'000};
    Qty inventory{50'000};
    /// Distance from the reference price to the best bid and best ask.
    PriceTicks half_spread{5};
    std::uint32_t levels = 3;
    PriceTicks level_step{5};
    Qty quote_size{200};
    /// Quotes shift by one tick (against the inventory) per this many shares away from the
    /// starting inventory.
    Qty skew_per_tick{1'000};
    /// Price to quote around before the first trade.
    PriceTicks opening_price{10'000};
};

/// The agent world: value process, trader populations, and the market maker.
struct AgentWorldConfig {
    /// Seed of every random draw agents make. Same seed and inputs, same run.
    std::uint64_t seed = 1;
    ValueProcessConfig value{};
    /// Indexed by TraderStyle.
    std::array<StyleConfig, kTraderStyleCount> styles{};
    MarketMakerConfig market_maker{};
    /// Agents get consecutive accounts starting here.
    AccountId first_agent_account{100'000};
    /// Closing prices remembered for momentum, averages and highs.
    std::uint32_t history_ticks = 64;
};

/// Limits accepted by validate(AgentWorldConfig).
inline constexpr std::uint32_t kMaxAgents = 1'000'000;
inline constexpr std::uint32_t kMaxHistoryTicks = 4'096;

/// The first problem with [config], or nullopt if it is valid.
[[nodiscard]] std::optional<Error> validate(const AgentWorldConfig& config) noexcept;

/// Total agents across all styles.
[[nodiscard]] std::uint64_t agent_count(const AgentWorldConfig& config) noexcept;

/// A small, varied population for tests, demos and the sandbox: some of every style, a
/// market maker, and shares held by panic sellers and value traders.
[[nodiscard]] AgentWorldConfig sample_world(std::uint64_t seed, std::uint32_t agents_per_style);

} // namespace stocksim
