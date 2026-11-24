#include "stocksim/account_ledger.hpp"
#include "stocksim/serialization.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <vector>

using namespace stocksim;
using stocksim::test::kAlice;
using stocksim::test::kBob;
using stocksim::test::kCarol;

TEST_CASE("opening accounts validates and refuses duplicates", "[ledger]") {
    AccountLedger ledger;
    CHECK(ledger.open(kAlice, Money{10'000}, Qty{5}, 100));
    CHECK_FALSE(ledger.open(kAlice, Money{1}, Qty{0}, 100)); // already open
    CHECK_FALSE(ledger.open(kBob, Money{-1}, Qty{0}, 100));
    CHECK_FALSE(ledger.open(kBob, Money{0}, Qty{-1}, 100));
    CHECK_FALSE(ledger.open(kBob, Money{0}, Qty{0}, kMinLeveragePercent - 1));
    CHECK_FALSE(ledger.open(kBob, Money{0}, Qty{0}, kMaxLeveragePercent + 1));
    const std::optional<AccountView> alice = ledger.find(kAlice);
    REQUIRE(alice);
    CHECK(alice->limited);
    CHECK(alice->cash == Money{10'000});
    CHECK(alice->position == Qty{5});
}

TEST_CASE("fills move cash and shares and conserve totals", "[ledger]") {
    AccountLedger ledger;
    ledger.open(kAlice, Money{10'000}, Qty{0}, 100);
    ledger.open(kBob, Money{0}, Qty{20}, 100);
    ledger.apply_fill(kAlice, kBob, PriceTicks{150}, Qty{4});
    CHECK(ledger.find(kAlice)->cash == Money{10'000 - 600});
    CHECK(ledger.find(kAlice)->position == Qty{4});
    CHECK(ledger.find(kBob)->cash == Money{600});
    CHECK(ledger.find(kBob)->position == Qty{16});
    CHECK(ledger.check_conservation());

    // Unopened accounts are tracked too, and totals still hold.
    ledger.apply_fill(kCarol, kBob, PriceTicks{150}, Qty{1});
    CHECK_FALSE(ledger.find(kCarol)->limited);
    CHECK(ledger.find(kCarol)->cash == Money{-150});
    CHECK(ledger.check_conservation());
}

TEST_CASE("buy checks use equity, leverage and commitments", "[ledger]") {
    AccountLedger ledger;
    ledger.open(kAlice, Money{1'000}, Qty{0}, 100);
    CHECK(ledger.check_buy(kAlice, Money{1'000}, std::nullopt) == Reason::kNone);
    CHECK(ledger.check_buy(kAlice, Money{1'001}, std::nullopt) == Reason::kInsufficientFunds);
    ledger.reserve_buy(kAlice, Money{400});
    CHECK(ledger.check_buy(kAlice, Money{600}, std::nullopt) == Reason::kNone);
    CHECK(ledger.check_buy(kAlice, Money{601}, std::nullopt) == Reason::kInsufficientFunds);
    ledger.release_buy(kAlice, Money{400});

    ledger.open(kBob, Money{1'000}, Qty{0}, 300); // 3x leverage
    CHECK(ledger.check_buy(kBob, Money{3'000}, std::nullopt) == Reason::kNone);
    CHECK(ledger.check_buy(kBob, Money{3'001}, std::nullopt) == Reason::kInsufficientFunds);

    // A held position counts as equity and as exposure at the mark.
    ledger.open(kCarol, Money{0}, Qty{10}, 200); // equity 10 x 100 = 1000, limit 2000
    CHECK(ledger.check_buy(kCarol, Money{1'000}, PriceTicks{100}) == Reason::kNone);
    CHECK(ledger.check_buy(kCarol, Money{1'001}, PriceTicks{100}) == Reason::kInsufficientFunds);
    CHECK(ledger.check_buy(kCarol, Money{1}, std::nullopt) == Reason::kInsufficientFunds);
}

TEST_CASE("sell checks are long-only and respect reserved shares", "[ledger]") {
    AccountLedger ledger;
    ledger.open(kAlice, Money{0}, Qty{10}, 100);
    CHECK(ledger.check_sell(kAlice, Qty{10}) == Reason::kNone);
    CHECK(ledger.check_sell(kAlice, Qty{11}) == Reason::kInsufficientShares);
    ledger.reserve_sell(kAlice, Qty{4});
    CHECK(ledger.check_sell(kAlice, Qty{6}) == Reason::kNone);
    CHECK(ledger.check_sell(kAlice, Qty{7}) == Reason::kInsufficientShares);
}

TEST_CASE("unopened accounts are unlimited", "[ledger]") {
    const AccountLedger ledger;
    CHECK(ledger.check_buy(kAlice, Money{1'000'000'000}, std::nullopt) == Reason::kNone);
    CHECK(ledger.check_sell(kAlice, Qty{1'000'000}) == Reason::kNone);
}

TEST_CASE("huge equity and leverage saturate instead of overflowing", "[ledger]") {
    AccountLedger ledger;
    ledger.open(kAlice, Money{4'000'000'000'000'000'000}, Qty{0}, kMaxLeveragePercent);
    CHECK(ledger.check_buy(kAlice, Money{9'000'000'000'000'000'000}, std::nullopt) ==
          Reason::kNone);
}

TEST_CASE("ledger state round-trips and rejects inconsistent totals", "[ledger]") {
    AccountLedger ledger;
    ledger.open(kAlice, Money{5'000}, Qty{3}, 250);
    ledger.open(kBob, Money{0}, Qty{9}, 100);
    ledger.apply_fill(kAlice, kBob, PriceTicks{120}, Qty{2});
    ledger.reserve_buy(kAlice, Money{240});
    ledger.reserve_sell(kBob, Qty{1});
    StateWriter out;
    ledger.save_state(out);

    StateReader in{out.bytes()};
    AccountLedger copy;
    copy.load_state(in);
    REQUIRE(in.ok());
    StateWriter again;
    copy.save_state(again);
    CHECK(again.bytes() == out.bytes());

    // Change one account's cash so the totals no longer balance.
    std::vector<std::byte> bytes = out.bytes();
    constexpr std::size_t kFirstCashOffset = 8 + 8 + 8 + 4;
    bytes[kFirstCashOffset] ^= std::byte{1};
    StateReader corrupt{bytes};
    AccountLedger rejected;
    rejected.load_state(corrupt);
    CHECK_FALSE(corrupt.ok());
}
