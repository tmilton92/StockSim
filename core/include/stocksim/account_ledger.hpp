#pragma once

#include "stocksim/event.hpp"
#include "stocksim/result.hpp"
#include "stocksim/serialization.hpp"
#include "stocksim/strong_type.hpp"
#include "stocksim/types.hpp"

#include <cstdint>
#include <map>
#include <optional>

namespace stocksim {

/// Leverage written as a percentage: 100 is 1x (cash only), 500 is 5x.
inline constexpr std::int32_t kMinLeveragePercent = 100;
inline constexpr std::int32_t kMaxLeveragePercent = 10'000;

/// Read-only snapshot of one account.
struct AccountView {
    AccountId id{};
    Money cash{};
    Qty position{};
    /// Opened with limits: orders are checked against buying power and shares held.
    bool limited = false;
    std::int32_t leverage_percent = kMinLeveragePercent;
    /// Notional reserved by resting buy limit orders.
    Money committed_buy{};
    /// Shares reserved by resting sell orders and pending sell stops.
    Qty committed_sell{};
};

/// Cash and positions of every account that has traded or been opened.
///
/// Every account is tracked; only *opened* accounts are limited. Limits (until margin arrives in
/// TODO section 7) are long-only with leverage:
///   - a buy needs `exposure + cost <= equity x leverage`, where
///     `equity = cash + max(position, 0) x mark`, `exposure = max(position, 0) x mark +
///     committed_buy`, and mark is the last trade price (0 before the first trade);
///   - a sell needs `position - committed_sell >= qty` (no short selling).
/// Unopened accounts (scripted participants, the sandbox ticket) are unlimited, so existing
/// lessons behave exactly as before. Cash and shares only move between accounts, so the totals
/// never change: [check_conservation] verifies that.
class AccountLedger {
public:
    /// Opens a limited account with starting cash and shares. Fails (returns false) if the
    /// account has already been opened or has traded, or the leverage is out of range.
    bool open(AccountId account, Money cash, Qty shares, std::int32_t leverage_percent);

    [[nodiscard]] std::optional<AccountView> find(AccountId account) const;

    /// Moves cash and shares for one fill.
    void apply_fill(AccountId buyer, AccountId seller, PriceTicks price, Qty qty);

    void reserve_buy(AccountId account, Money amount);
    void release_buy(AccountId account, Money amount);
    void reserve_sell(AccountId account, Qty qty);
    void release_sell(AccountId account, Qty qty);

    /// Reason::kNone if [account] may buy for [cost] more, else kInsufficientFunds.
    [[nodiscard]] Reason check_buy(AccountId account, Money cost,
                                   std::optional<PriceTicks> mark) const;
    /// Reason::kNone if [account] may sell [qty] more, else kInsufficientShares.
    [[nodiscard]] Reason check_sell(AccountId account, Qty qty) const;

    [[nodiscard]] Money total_cash() const noexcept;
    [[nodiscard]] Qty total_position() const noexcept;
    /// True when total cash and shares still equal what was put in when accounts were opened.
    [[nodiscard]] bool check_conservation() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return m_accounts.size(); }

    void save_state(StateWriter& out) const;
    /// Fails the reader on invalid data.
    void load_state(StateReader& in);

private:
    struct Entry {
        Money cash{};
        Qty position{};
        bool limited = false;
        std::int32_t leverage_percent = kMinLeveragePercent;
        Money committed_buy{};
        Qty committed_sell{};
    };

    Entry& entry(AccountId account);
    [[nodiscard]] static AccountView view(AccountId account, const Entry& entry) noexcept;

    /// Ordered by id, so iteration (serialization, totals) is deterministic.
    std::map<AccountId, Entry> m_accounts;
    Money m_opened_cash{};
    Qty m_opened_shares{};
};

} // namespace stocksim
