#include "stocksim/agents/agent_config.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace stocksim {

namespace {

constexpr Error invalid(std::string_view message) noexcept {
    return Error{.code = ErrorCode::kInvalidConfig, .message = message};
}

std::optional<Error> validate_style(const StyleConfig& style) noexcept {
    if (style.capital_min < Money{0} || style.capital_max < style.capital_min) {
        return invalid("Agent capital range must be non-negative with min <= max.");
    }
    if (style.shares_min < Qty{0} || style.shares_max < style.shares_min ||
        style.shares_max > kMaxOrderQty) {
        return invalid("Agent share range must be non-negative with min <= max.");
    }
    if (style.leverage_percent_min < 100 ||
        style.leverage_percent_max < style.leverage_percent_min ||
        style.leverage_percent_max > 10'000) {
        return invalid("Agent leverage must be between 100 and 10000 percent with min <= max.");
    }
    const bool permille_ok =
        style.attention_permille_max <= 1'000 &&
        style.attention_permille_min <= style.attention_permille_max &&
        style.risk_permille_max <= 1'000 && style.risk_permille_min <= style.risk_permille_max &&
        style.stop_habit_permille <= 1'000 && style.social_permille_max <= 1'000 &&
        style.social_permille_min <= style.social_permille_max;
    if (!permille_ok) {
        return invalid("Agent per-mille parameters must be 0-1000 with min <= max.");
    }
    if (style.stop_distance_bps_min < 1 ||
        style.stop_distance_bps_max < style.stop_distance_bps_min ||
        style.stop_distance_bps_max > 9'000) {
        return invalid("Agent stop distance must be 1-9000 basis points with min <= max.");
    }
    if (style.reaction_delay_max > kMaxHistoryTicks) {
        return invalid("Agent reaction delay must not exceed the history length limit.");
    }
    return std::nullopt;
}

} // namespace

std::string_view to_string(TraderStyle style) noexcept {
    switch (style) {
    case TraderStyle::kNoise:
        return "noise";
    case TraderStyle::kMomentum:
        return "momentum";
    case TraderStyle::kMeanReversion:
        return "mean-reversion";
    case TraderStyle::kHerding:
        return "herding";
    case TraderStyle::kPanicSeller:
        return "panic-seller";
    case TraderStyle::kLeverageGambler:
        return "leverage-gambler";
    case TraderStyle::kValue:
        return "value";
    }
    return "unknown";
}

std::uint64_t agent_count(const AgentWorldConfig& config) noexcept {
    std::uint64_t total = 0;
    for (const StyleConfig& style : config.styles) {
        total += style.count;
    }
    return total;
}

std::optional<Error> validate(const AgentWorldConfig& config) noexcept {
    if (agent_count(config) > kMaxAgents) {
        return invalid("Too many agents (the limit is 1,000,000).");
    }
    for (const StyleConfig& style : config.styles) {
        if (std::optional<Error> problem = validate_style(style)) {
            return problem;
        }
    }
    const ValueProcessConfig& value = config.value;
    if (value.initial_value <= PriceTicks{0} || value.initial_value > kMaxPrice ||
        value.step < PriceTicks{0} || value.jump < PriceTicks{0} || value.move_permille > 1'000 ||
        value.jump_per_million > 1'000'000) {
        return invalid("Value process parameters are out of range.");
    }
    const MarketMakerConfig& maker = config.market_maker;
    if (maker.enabled &&
        (maker.capital < Money{0} || maker.inventory < Qty{0} ||
         maker.half_spread < PriceTicks{1} || maker.levels < 1 || maker.levels > 50 ||
         maker.level_step < PriceTicks{1} || maker.quote_size < Qty{1} ||
         maker.skew_per_tick < Qty{1} || maker.opening_price <= PriceTicks{0} ||
         maker.opening_price > kMaxPrice)) {
        return invalid("Market maker parameters are out of range.");
    }
    const std::uint64_t last_account =
        std::uint64_t{config.first_agent_account.value()} + agent_count(config);
    if (last_account > UINT32_MAX) {
        return invalid("Agent account ids would overflow.");
    }
    if (maker.enabled && maker.account.value() >= config.first_agent_account.value() &&
        maker.account.value() < last_account) {
        return invalid("The market maker account overlaps the agent accounts.");
    }
    if (config.history_ticks < 2 || config.history_ticks > kMaxHistoryTicks) {
        return invalid("history_ticks must be between 2 and 4096.");
    }
    return std::nullopt;
}

AgentWorldConfig sample_world(std::uint64_t seed, std::uint32_t agents_per_style) {
    AgentWorldConfig config;
    config.seed = seed;
    config.market_maker.enabled = true;
    for (std::size_t i = 0; i < kTraderStyleCount; ++i) {
        config.styles[i].count = agents_per_style;
    }
    auto& momentum = config.styles[static_cast<std::size_t>(TraderStyle::kMomentum)];
    momentum.reaction_delay_max = 3;
    momentum.stop_habit_permille = 400;
    auto& herding = config.styles[static_cast<std::size_t>(TraderStyle::kHerding)];
    herding.social_permille_min = 300;
    herding.social_permille_max = 900;
    herding.stop_habit_permille = 200;
    auto& panic = config.styles[static_cast<std::size_t>(TraderStyle::kPanicSeller)];
    panic.shares_min = Qty{20};
    panic.shares_max = Qty{200};
    panic.attention_permille_min = 300;
    panic.attention_permille_max = 800;
    auto& gambler = config.styles[static_cast<std::size_t>(TraderStyle::kLeverageGambler)];
    gambler.leverage_percent_min = 300;
    gambler.leverage_percent_max = 1'000;
    gambler.risk_permille_min = 200;
    gambler.risk_permille_max = 600;
    gambler.stop_habit_permille = 1'000;
    gambler.stop_distance_bps_min = 30;
    gambler.stop_distance_bps_max = 100;
    auto& value = config.styles[static_cast<std::size_t>(TraderStyle::kValue)];
    value.shares_min = Qty{50};
    value.shares_max = Qty{300};
    value.capital_min = Money{500'000};
    value.capital_max = Money{5'000'000};
    return config;
}

} // namespace stocksim
