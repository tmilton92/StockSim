#pragma once

#include "stocksim/account_ledger.hpp"
#include "stocksim/agents/agent_config.hpp"
#include "stocksim/order.hpp"
#include "stocksim/serialization.hpp"
#include "stocksim/types.hpp"

#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace stocksim {

/// One quote the market maker wants on the book.
struct Quote {
    Side side = Side::kBuy;
    PriceTicks price{};
    Qty qty{};
};

/// A market maker: quotes [levels] prices on each side of the reference, [half_spread] away
/// and [level_step] apart, and moves all its quotes against its inventory (holding more than it
/// started with lowers its prices to sell; holding less raises them to buy). It is an ordinary
/// limited account: it re-quotes by cancelling and placing orders like anyone else.
class MarketMaker {
public:
    explicit MarketMaker(const MarketMakerConfig& config);

    /// Quotes for this tick, given the last trade price (or none yet) and the maker's account.
    [[nodiscard]] std::vector<Quote> quotes(std::optional<PriceTicks> last_price,
                                            const AccountView& account) const;

    /// Ticks the quotes are shifted by (negative = lower), for the inventory in [account].
    [[nodiscard]] std::int64_t skew_ticks(const AccountView& account) const noexcept;

    [[nodiscard]] const MarketMakerConfig& config() const noexcept { return m_config; }

    /// Ids of the quotes placed last tick (cancelled before re-quoting).
    [[nodiscard]] std::span<const OrderId> live_quotes() const noexcept { return m_live_quotes; }
    void set_live_quotes(std::vector<OrderId> ids) noexcept { m_live_quotes = std::move(ids); }

    void save_state(StateWriter& out) const;
    void load_state(StateReader& in);

private:
    MarketMakerConfig m_config;
    std::vector<OrderId> m_live_quotes;
};

} // namespace stocksim
