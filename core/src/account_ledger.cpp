#include "stocksim/account_ledger.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <optional>

namespace stocksim {

namespace {

constexpr std::int64_t kInt64Max = std::numeric_limits<std::int64_t>::max();
constexpr std::size_t kMinAccountBytes = 4 + 8 + 8 + 1 + 4 + 8 + 8;

/// value x percent / 100, saturating at int64 max instead of overflowing.
constexpr std::int64_t scale_percent(std::int64_t value, std::int32_t percent) noexcept {
    if (value <= 0) {
        return value; // negative equity stays negative: no buying power
    }
    const std::int64_t whole = value / 100;
    const std::int64_t part = value % 100;
    if (whole > kInt64Max / percent) {
        return kInt64Max;
    }
    return (whole * percent) + ((part * percent) / 100);
}

/// a + b, saturating at int64 max (both non-negative).
constexpr std::int64_t add_saturating(std::int64_t a, std::int64_t b) noexcept {
    return a > kInt64Max - b ? kInt64Max : a + b;
}

} // namespace

AccountLedger::Entry& AccountLedger::entry(AccountId account) {
    return m_accounts[account];
}

AccountView AccountLedger::view(AccountId account, const Entry& entry) noexcept {
    return AccountView{.id = account,
                       .cash = entry.cash,
                       .position = entry.position,
                       .limited = entry.limited,
                       .leverage_percent = entry.leverage_percent,
                       .committed_buy = entry.committed_buy,
                       .committed_sell = entry.committed_sell};
}

bool AccountLedger::open(AccountId account, Money cash, Qty shares, std::int32_t leverage_percent) {
    const bool valid = !m_accounts.contains(account) && cash >= Money{0} && shares >= Qty{0} &&
                       leverage_percent >= kMinLeveragePercent &&
                       leverage_percent <= kMaxLeveragePercent;
    if (!valid) {
        return false;
    }
    m_accounts.emplace(account, Entry{.cash = cash,
                                      .position = shares,
                                      .limited = true,
                                      .leverage_percent = leverage_percent});
    m_opened_cash += cash;
    m_opened_shares += shares;
    return true;
}

std::optional<AccountView> AccountLedger::find(AccountId account) const {
    const auto it = m_accounts.find(account);
    if (it == m_accounts.end()) {
        return std::nullopt;
    }
    return view(account, it->second);
}

void AccountLedger::apply_fill(AccountId buyer, AccountId seller, PriceTicks price, Qty qty) {
    const Money value = notional(price, qty);
    Entry& buy = entry(buyer);
    buy.cash -= value;
    buy.position += qty;
    Entry& sell = entry(seller);
    sell.cash += value;
    sell.position -= qty;
}

void AccountLedger::reserve_buy(AccountId account, Money amount) {
    entry(account).committed_buy += amount;
}

void AccountLedger::release_buy(AccountId account, Money amount) {
    Entry& e = entry(account);
    e.committed_buy -= amount;
    assert(e.committed_buy >= Money{0} && "released more buy commitment than reserved");
}

void AccountLedger::reserve_sell(AccountId account, Qty qty) {
    entry(account).committed_sell += qty;
}

void AccountLedger::release_sell(AccountId account, Qty qty) {
    Entry& e = entry(account);
    e.committed_sell -= qty;
    assert(e.committed_sell >= Qty{0} && "released more sell commitment than reserved");
}

Reason AccountLedger::check_buy(AccountId account, Money cost,
                                std::optional<PriceTicks> mark) const {
    const auto it = m_accounts.find(account);
    if (it == m_accounts.end() || !it->second.limited) {
        return Reason::kNone;
    }
    const Entry& e = it->second;
    const std::int64_t mark_ticks = mark.value_or(PriceTicks{}).value();
    const std::int64_t long_value = std::max<std::int64_t>(e.position.value(), 0) * mark_ticks;
    const std::int64_t equity = e.cash.value() + long_value;
    const std::int64_t limit = scale_percent(equity, e.leverage_percent);
    const std::int64_t exposure =
        add_saturating(add_saturating(long_value, e.committed_buy.value()), cost.value());
    return exposure <= limit ? Reason::kNone : Reason::kInsufficientFunds;
}

Reason AccountLedger::check_sell(AccountId account, Qty qty) const {
    const auto it = m_accounts.find(account);
    if (it == m_accounts.end() || !it->second.limited) {
        return Reason::kNone;
    }
    const Entry& e = it->second;
    return e.position - e.committed_sell >= qty ? Reason::kNone : Reason::kInsufficientShares;
}

Money AccountLedger::total_cash() const noexcept {
    Money total{};
    for (const auto& [id, e] : m_accounts) {
        total += e.cash;
    }
    return total;
}

Qty AccountLedger::total_position() const noexcept {
    Qty total{};
    for (const auto& [id, e] : m_accounts) {
        total += e.position;
    }
    return total;
}

bool AccountLedger::check_conservation() const noexcept {
    return total_cash() == m_opened_cash && total_position() == m_opened_shares;
}

void AccountLedger::save_state(StateWriter& out) const {
    out.i64(m_opened_cash.value());
    out.i64(m_opened_shares.value());
    out.u64(m_accounts.size());
    for (const auto& [id, e] : m_accounts) {
        out.u32(id.value());
        out.i64(e.cash.value());
        out.i64(e.position.value());
        out.boolean(e.limited);
        out.u32(static_cast<std::uint32_t>(e.leverage_percent));
        out.i64(e.committed_buy.value());
        out.i64(e.committed_sell.value());
    }
}

void AccountLedger::load_state(StateReader& in) {
    m_accounts.clear();
    m_opened_cash = Money{in.i64()};
    m_opened_shares = Qty{in.i64()};
    const std::uint64_t count = in.count(kMinAccountBytes);
    for (std::uint64_t i = 0; i < count && in.ok(); ++i) {
        const AccountId id{in.u32()};
        Entry e;
        e.cash = Money{in.i64()};
        e.position = Qty{in.i64()};
        e.limited = in.boolean();
        const std::uint32_t leverage = in.u32();
        e.committed_buy = Money{in.i64()};
        e.committed_sell = Qty{in.i64()};
        const bool valid = leverage >= kMinLeveragePercent && leverage <= kMaxLeveragePercent &&
                           e.committed_buy >= Money{0} && e.committed_sell >= Qty{0} &&
                           m_accounts.emplace(id, e).second;
        if (!valid) {
            in.fail();
            return;
        }
        m_accounts.at(id).leverage_percent = static_cast<std::int32_t>(leverage);
    }
    if (in.ok() && !check_conservation()) {
        in.fail();
    }
}

} // namespace stocksim
