#include "stocksim/agents/agent_population.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace stocksim {

namespace {

// Heuristic constants (basis points, ticks). Behaviour, not market rules: tuning them changes
// how agents act, never how the exchange works.
constexpr std::int64_t kBasisPoints = 10'000;
constexpr std::size_t kMomentumLookback = 10;
constexpr std::int64_t kMomentumThresholdBps = 30;
constexpr std::size_t kAverageWindow = 20;
constexpr std::int64_t kReversionBandBps = 40;
constexpr std::int64_t kHerdImbalancePermille = 200;
constexpr std::size_t kHighWindow = 20;
constexpr std::int64_t kValueMarginBps = 100;
constexpr std::int64_t kValueNoiseBps = 50;
constexpr std::int64_t kNoiseMaxOffsetTicks = 20;
constexpr std::uint64_t kNoiseMarketPermille = 300;

std::uint32_t draw_u32(Rng& rng, std::uint32_t lo, std::uint32_t hi) {
    return static_cast<std::uint32_t>(rng.uniform_int(lo, hi));
}

/// The price the agent reacts to: the reference [delay] ticks ago (clamped to the history).
PriceTicks seen_price(const MarketView& view, std::uint32_t delay) {
    const std::size_t back = std::min<std::size_t>(delay, view.history.size() - 1);
    return view.history[view.history.size() - 1 - back];
}

/// Price [back] ticks before the one at [delay], or nullopt if the history is too short.
std::optional<PriceTicks> earlier_price(const MarketView& view, std::uint32_t delay,
                                        std::size_t back) {
    const std::size_t index_from_end = std::size_t{delay} + back;
    if (index_from_end >= view.history.size()) {
        return std::nullopt;
    }
    return view.history[view.history.size() - 1 - index_from_end];
}

/// (a - b) / b in basis points; 0 if b is not positive.
std::int64_t change_bps(PriceTicks a, PriceTicks b) {
    return b > PriceTicks{0} ? (a - b).value() * kBasisPoints / b.value() : 0;
}

PriceTicks scale_bps(PriceTicks price, std::int64_t bps) {
    return PriceTicks{price.value() * bps / kBasisPoints};
}

Money equity(const AccountView& account, PriceTicks mark) {
    return account.cash + notional(mark, std::max(account.position, Qty{0}));
}

/// Shares affordable with [permille] of equity (times [leverage_percent]) at [price].
Qty affordable(const AccountView& account, PriceTicks price, std::int64_t permille,
               std::int64_t leverage_percent = 100) {
    if (price <= PriceTicks{0}) {
        return Qty{0};
    }
    const std::int64_t budget =
        equity(account, price).value() / 1'000 * permille / 100 * leverage_percent;
    return Qty{std::clamp<std::int64_t>(budget / price.value(), 0, kMaxOrderQty.value())};
}

NewOrder market(AccountId account, Side side, Qty qty) {
    return NewOrder{.account = account, .side = side, .type = OrderType::kMarket, .qty = qty};
}

NewOrder limit(AccountId account, Side side, Qty qty, PriceTicks price) {
    return NewOrder{.account = account,
                    .side = side,
                    .type = OrderType::kLimit,
                    .qty = qty,
                    .limit_price = std::clamp(price, PriceTicks{1}, kMaxPrice)};
}

/// Sell everything at market, dropping the protective stop and any working order first (both
/// may reserve the shares). Cancelling a working order that already finished is skipped.
void sell_all(AgentDecision& decision, AccountId account, Qty position, bool has_stop) {
    decision.cancel_stop = has_stop;
    decision.cancel_working = true;
    decision.order = market(account, Side::kSell, position);
}

void decide_noise(AgentDecision& d, const AgentPopulation& a, std::size_t i, const MarketView& view,
                  const AccountView& account, Rng& rng) {
    const PriceTicks price = view.reference;
    bool buy = rng.chance(1, 2);
    const bool use_market = rng.chance(kNoiseMarketPermille, 1'000);
    const PriceTicks offset{1 + rng.uniform_int(0, kNoiseMaxOffsetTicks - 1)};
    const Qty budget = affordable(account, price, a.risk_permille[i]);
    const Qty size{1 + rng.uniform_int(0, std::max<std::int64_t>(budget.value() / 4, 0))};
    if (!buy && account.position - account.committed_sell <= Qty{0}) {
        buy = true; // long-only: nothing to sell
    }
    if (buy && budget <= Qty{0}) {
        return;
    }
    const Side side = buy ? Side::kBuy : Side::kSell;
    const Qty qty = buy ? std::min(size, budget) : std::min(size, account.position);
    if (use_market) {
        d.order = market(a.account[i], side, qty);
    } else {
        d.order = limit(a.account[i], side, qty, buy ? price - offset : price + offset);
    }
}

void decide_momentum(AgentDecision& d, const AgentPopulation& a, std::size_t i,
                     const MarketView& view, const AccountView& account, bool has_stop) {
    const PriceTicks now = seen_price(view, a.reaction_delay[i]);
    const std::optional<PriceTicks> then =
        earlier_price(view, a.reaction_delay[i], kMomentumLookback);
    if (!then) {
        return;
    }
    const std::int64_t move = change_bps(now, *then);
    if (move > kMomentumThresholdBps) {
        const Qty qty = affordable(account, view.reference, a.risk_permille[i]);
        if (qty > Qty{0}) {
            d.order = market(a.account[i], Side::kBuy, qty);
        }
    } else if (move < -kMomentumThresholdBps && account.position > Qty{0}) {
        sell_all(d, a.account[i], account.position, has_stop);
    }
}

void decide_mean_reversion(AgentDecision& d, const AgentPopulation& a, std::size_t i,
                           const MarketView& view, const AccountView& account, bool working_live) {
    const std::size_t window = std::min(kAverageWindow, view.history.size());
    std::int64_t sum = 0;
    for (std::size_t k = view.history.size() - window; k < view.history.size(); ++k) {
        sum += view.history[k].value();
    }
    const PriceTicks average{sum / static_cast<std::int64_t>(window)};
    const PriceTicks now = seen_price(view, a.reaction_delay[i]);
    const std::int64_t deviation = change_bps(now, average);
    if (deviation < -kReversionBandBps) {
        const Qty qty = affordable(account, now, a.risk_permille[i]);
        if (qty > Qty{0}) {
            d.cancel_working = working_live;
            d.order = limit(a.account[i], Side::kBuy, qty, now);
        }
    } else if (deviation > kReversionBandBps) {
        const Qty free = account.position - account.committed_sell;
        if (free > Qty{0}) {
            d.cancel_working = working_live;
            d.order = limit(a.account[i], Side::kSell, Qty{(free.value() / 2) + 1}, now);
        }
    }
}

void decide_herding(AgentDecision& d, const AgentPopulation& a, std::size_t i,
                    const MarketView& view, const AccountView& account, bool has_stop) {
    const std::int64_t total = (view.taker_buy_volume + view.taker_sell_volume).value();
    if (total == 0) {
        return;
    }
    const std::int64_t imbalance =
        (view.taker_buy_volume - view.taker_sell_volume).value() * 1'000 / total;
    const std::int64_t felt = imbalance * a.social_permille[i] / 1'000;
    if (felt > kHerdImbalancePermille) {
        const Qty qty = affordable(account, view.reference, a.risk_permille[i]);
        if (qty > Qty{0}) {
            d.order = market(a.account[i], Side::kBuy, qty);
        }
    } else if (felt < -kHerdImbalancePermille && account.position > Qty{0}) {
        sell_all(d, a.account[i], account.position, has_stop);
    }
}

void decide_panic(AgentDecision& d, const AgentPopulation& a, std::size_t i, const MarketView& view,
                  const AccountView& account, bool has_stop) {
    const PriceTicks now = seen_price(view, a.reaction_delay[i]);
    if (account.position > Qty{0}) {
        const std::size_t window = std::min(kHighWindow, view.history.size());
        PriceTicks high{};
        for (std::size_t k = view.history.size() - window; k < view.history.size(); ++k) {
            high = std::max(high, view.history[k]);
        }
        if (change_bps(high, now) > static_cast<std::int64_t>(a.stop_distance_bps[i])) {
            sell_all(d, a.account[i], account.position, has_stop);
        }
        return;
    }
    // Out of the market: drift back in once the price is rising again.
    const std::optional<PriceTicks> then = earlier_price(view, a.reaction_delay[i], 5);
    if (then && now > *then) {
        const Qty qty = affordable(account, view.reference, a.risk_permille[i]);
        if (qty > Qty{0}) {
            d.order = market(a.account[i], Side::kBuy, qty);
        }
    }
}

void decide_gambler(AgentDecision& d, const AgentPopulation& a, std::size_t i,
                    const MarketView& view, const AccountView& account, bool has_stop) {
    const PriceTicks now = seen_price(view, a.reaction_delay[i]);
    if (account.position > Qty{0}) {
        const PriceTicks target =
            a.entry_price[i] +
            scale_bps(a.entry_price[i], std::int64_t{2} * a.stop_distance_bps[i]);
        if (now >= target) {
            sell_all(d, a.account[i], account.position, has_stop); // take the profit
        }
        return;
    }
    const std::optional<PriceTicks> previous = earlier_price(view, a.reaction_delay[i], 1);
    if (previous && now > *previous) {
        const Qty qty =
            affordable(account, view.reference, a.risk_permille[i], a.leverage_percent[i]);
        if (qty > Qty{0}) {
            d.order = market(a.account[i], Side::kBuy, qty);
        }
    }
}

void decide_value(AgentDecision& d, const AgentPopulation& a, std::size_t i, const MarketView& view,
                  const AccountView& account, bool working_live, Rng& rng) {
    const std::int64_t noise_ticks = scale_bps(view.true_value, kValueNoiseBps).value();
    const PriceTicks estimate =
        view.true_value + PriceTicks{rng.uniform_int(-noise_ticks, noise_ticks)};
    const PriceTicks margin = scale_bps(estimate, kValueMarginBps);
    const PriceTicks now = view.reference;
    if (now < estimate - margin) {
        const Qty qty = affordable(account, now, a.risk_permille[i]);
        if (qty > Qty{0}) {
            d.cancel_working = working_live;
            d.order = limit(a.account[i], Side::kBuy, qty, estimate - margin);
        }
    } else if (now > estimate + margin) {
        const Qty free = account.position - account.committed_sell;
        if (free > Qty{0}) {
            d.cancel_working = working_live;
            d.order =
                limit(a.account[i], Side::kSell, Qty{(free.value() / 2) + 1}, estimate + margin);
        }
    }
}

/// Keeps a protective stop covering the whole position, at the agent's stop distance below
/// the entry price.
void manage_stop(AgentDecision& d, const AgentPopulation& a, std::size_t i,
                 const AccountView& account, bool has_stop) {
    if (a.uses_stops[i] == 0 || d.order) {
        return;
    }
    if (account.position <= Qty{0}) {
        d.cancel_stop = has_stop;
        return;
    }
    if (has_stop && a.stop_covered[i] == account.position) {
        return;
    }
    const PriceTicks entry = a.entry_price[i];
    const PriceTicks trigger = entry - scale_bps(entry, a.stop_distance_bps[i]);
    if (trigger < PriceTicks{1}) {
        return;
    }
    d.cancel_stop = has_stop;
    d.stop = NewOrder{.account = a.account[i],
                      .side = Side::kSell,
                      .type = OrderType::kStop,
                      .qty = account.position,
                      .stop_price = trigger};
}

} // namespace

AgentPopulation AgentPopulation::generate(const AgentWorldConfig& config, Rng& rng) {
    AgentPopulation a;
    const auto total = static_cast<std::size_t>(agent_count(config));
    a.style.reserve(total);
    std::uint32_t next_account = config.first_agent_account.value();
    for (std::size_t s = 0; s < kTraderStyleCount; ++s) {
        const StyleConfig& style = config.styles[s];
        for (std::uint32_t n = 0; n < style.count; ++n) {
            a.style.push_back(static_cast<TraderStyle>(s));
            a.account.emplace_back(next_account++);
            a.capital.emplace_back(
                rng.uniform_int(style.capital_min.value(), style.capital_max.value()));
            a.initial_shares.emplace_back(
                rng.uniform_int(style.shares_min.value(), style.shares_max.value()));
            a.leverage_percent.push_back(static_cast<std::int32_t>(
                rng.uniform_int(style.leverage_percent_min, style.leverage_percent_max)));
            a.attention_permille.push_back(static_cast<std::uint16_t>(
                draw_u32(rng, style.attention_permille_min, style.attention_permille_max)));
            a.risk_permille.push_back(static_cast<std::uint16_t>(
                draw_u32(rng, style.risk_permille_min, style.risk_permille_max)));
            a.social_permille.push_back(static_cast<std::uint16_t>(
                draw_u32(rng, style.social_permille_min, style.social_permille_max)));
            a.reaction_delay.push_back(draw_u32(rng, 0, style.reaction_delay_max));
            a.stop_distance_bps.push_back(
                draw_u32(rng, style.stop_distance_bps_min, style.stop_distance_bps_max));
            a.uses_stops.push_back(rng.chance(style.stop_habit_permille, 1'000) ? 1 : 0);
        }
    }
    a.working_order.assign(total, OrderId{});
    a.working_since.assign(total, Tick{});
    a.stop_order.assign(total, OrderId{});
    a.stop_covered.assign(total, Qty{});
    a.entry_price.assign(total, config.value.initial_value);
    a.last_position.assign(a.initial_shares.begin(), a.initial_shares.end());
    return a;
}

AgentDecision decide(const AgentPopulation& agents, std::size_t index, const MarketView& view,
                     const AccountView& account, bool working_order_live, Rng& rng) {
    AgentDecision decision;
    const bool has_stop = agents.stop_order[index] != OrderId{};
    // Attention is drawn first and always, so every agent consumes its stream the same way.
    const bool attentive = rng.chance(agents.attention_permille[index], 1'000);
    if (attentive) {
        switch (agents.style[index]) {
        case TraderStyle::kNoise:
            decide_noise(decision, agents, index, view, account, rng);
            break;
        case TraderStyle::kMomentum:
            decide_momentum(decision, agents, index, view, account, has_stop);
            break;
        case TraderStyle::kMeanReversion:
            decide_mean_reversion(decision, agents, index, view, account, working_order_live);
            break;
        case TraderStyle::kHerding:
            decide_herding(decision, agents, index, view, account, has_stop);
            break;
        case TraderStyle::kPanicSeller:
            decide_panic(decision, agents, index, view, account, has_stop);
            break;
        case TraderStyle::kLeverageGambler:
            decide_gambler(decision, agents, index, view, account, has_stop);
            break;
        case TraderStyle::kValue:
            decide_value(decision, agents, index, view, account, working_order_live, rng);
            break;
        }
    }
    manage_stop(decision, agents, index, account, has_stop);
    return decision;
}

void AgentPopulation::save_state(StateWriter& out) const {
    out.u64(size());
    for (std::size_t i = 0; i < size(); ++i) {
        out.u8(static_cast<std::uint8_t>(style[i]));
        out.u32(account[i].value());
        out.i64(capital[i].value());
        out.i64(initial_shares[i].value());
        out.u32(static_cast<std::uint32_t>(leverage_percent[i]));
        out.u32(attention_permille[i]);
        out.u32(risk_permille[i]);
        out.u32(social_permille[i]);
        out.u32(reaction_delay[i]);
        out.u32(stop_distance_bps[i]);
        out.u8(uses_stops[i]);
        out.u64(working_order[i].value());
        out.i64(working_since[i].value());
        out.u64(stop_order[i].value());
        out.i64(stop_covered[i].value());
        out.i64(entry_price[i].value());
        out.i64(last_position[i].value());
    }
}

void AgentPopulation::load_state(StateReader& in) {
    constexpr std::size_t kMinAgentBytes = 1 + 4 + 8 + 8 + (4 * 6) + 1 + 8 + 8 + 8 + 8 + 8 + 8;
    *this = AgentPopulation{};
    const std::uint64_t count = in.count(kMinAgentBytes);
    for (std::uint64_t n = 0; n < count && in.ok(); ++n) {
        const std::uint8_t raw_style = in.u8();
        if (raw_style >= kTraderStyleCount) {
            in.fail();
            return;
        }
        style.push_back(static_cast<TraderStyle>(raw_style));
        account.emplace_back(in.u32());
        capital.emplace_back(in.i64());
        initial_shares.emplace_back(in.i64());
        leverage_percent.push_back(static_cast<std::int32_t>(in.u32()));
        attention_permille.push_back(static_cast<std::uint16_t>(std::min(in.u32(), 1'000U)));
        risk_permille.push_back(static_cast<std::uint16_t>(std::min(in.u32(), 1'000U)));
        social_permille.push_back(static_cast<std::uint16_t>(std::min(in.u32(), 1'000U)));
        reaction_delay.push_back(in.u32());
        stop_distance_bps.push_back(in.u32());
        uses_stops.push_back(in.u8());
        working_order.emplace_back(in.u64());
        working_since.emplace_back(in.i64());
        stop_order.emplace_back(in.u64());
        stop_covered.emplace_back(in.i64());
        entry_price.emplace_back(in.i64());
        last_position.emplace_back(in.i64());
        const bool valid = leverage_percent.back() >= 100 && leverage_percent.back() <= 10'000 &&
                           reaction_delay.back() <= kMaxHistoryTicks &&
                           stop_distance_bps.back() >= 1 && stop_distance_bps.back() <= 9'000 &&
                           uses_stops.back() <= 1 && entry_price.back() > PriceTicks{0};
        if (!valid) {
            in.fail();
            return;
        }
    }
}

} // namespace stocksim
