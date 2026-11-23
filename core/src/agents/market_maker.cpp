#include "stocksim/agents/market_maker.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace stocksim {

MarketMaker::MarketMaker(const MarketMakerConfig& config) : m_config(config) {}

std::int64_t MarketMaker::skew_ticks(const AccountView& account) const noexcept {
    const std::int64_t excess = (account.position - m_config.inventory).value();
    return -(excess / m_config.skew_per_tick.value());
}

std::vector<Quote> MarketMaker::quotes(std::optional<PriceTicks> last_price,
                                       const AccountView& account) const {
    const PriceTicks reference = last_price.value_or(m_config.opening_price);
    const PriceTicks shift{skew_ticks(account)};
    std::vector<Quote> result;
    result.reserve(static_cast<std::size_t>(m_config.levels) * 2);
    for (std::uint32_t level = 0; level < m_config.levels; ++level) {
        const PriceTicks away =
            m_config.half_spread + PriceTicks{m_config.level_step.value() * level};
        const PriceTicks bid = reference - away + shift;
        const PriceTicks ask = reference + away + shift;
        if (bid >= PriceTicks{1}) {
            result.push_back(Quote{.side = Side::kBuy, .price = bid, .qty = m_config.quote_size});
        }
        if (ask <= kMaxPrice) {
            result.push_back(Quote{.side = Side::kSell, .price = ask, .qty = m_config.quote_size});
        }
    }
    return result;
}

void MarketMaker::save_state(StateWriter& out) const {
    out.u64(m_live_quotes.size());
    for (const OrderId id : m_live_quotes) {
        out.u64(id.value());
    }
}

void MarketMaker::load_state(StateReader& in) {
    m_live_quotes.clear();
    const std::uint64_t count = in.count(8);
    for (std::uint64_t i = 0; i < count && in.ok(); ++i) {
        m_live_quotes.emplace_back(in.u64());
    }
}

} // namespace stocksim
