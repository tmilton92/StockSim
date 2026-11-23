// Agents (TODO section 6a): the hidden value process, population generation, each trading
// style's decision rule, the market maker, and agents running inside a Simulation.

#include "stocksim/agents/agent_config.hpp"
#include "stocksim/agents/agent_population.hpp"
#include "stocksim/agents/agent_world.hpp"
#include "stocksim/agents/market_maker.hpp"
#include "stocksim/agents/value_process.hpp"
#include "stocksim/event_hash.hpp"
#include "stocksim/rng.hpp"
#include "stocksim/simulation.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

using namespace stocksim;

namespace {

constexpr AccountId kAgent{100'000};

/// One agent of [style] that always pays attention and reacts to the latest price.
AgentPopulation single_agent(TraderStyle style, std::uint32_t stop_habit_permille = 0) {
    AgentWorldConfig config;
    StyleConfig& s = config.styles[static_cast<std::size_t>(style)];
    s.count = 1;
    s.attention_permille_min = 1'000;
    s.attention_permille_max = 1'000;
    s.risk_permille_min = 100;
    s.risk_permille_max = 100;
    s.social_permille_min = 1'000;
    s.social_permille_max = 1'000;
    s.stop_distance_bps_min = 200;
    s.stop_distance_bps_max = 200;
    s.stop_habit_permille = stop_habit_permille;
    s.leverage_percent_min = 500;
    s.leverage_percent_max = 500;
    Rng rng{7};
    return AgentPopulation::generate(config, rng);
}

AccountView account_with(Money cash, Qty position) {
    return AccountView{
        .id = kAgent, .cash = cash, .position = position, .limited = true, .leverage_percent = 100};
}

/// A view whose reference is the last history entry.
MarketView view_of(std::span<const PriceTicks> history) {
    return MarketView{.tick = Tick{static_cast<std::int64_t>(history.size())},
                      .reference = history.back(),
                      .best_bid = std::nullopt,
                      .best_ask = std::nullopt,
                      .history = history,
                      .taker_buy_volume = Qty{},
                      .taker_sell_volume = Qty{},
                      .true_value = PriceTicks{10'000}};
}

std::vector<PriceTicks> ramp(std::int64_t from, std::int64_t to, std::size_t length) {
    std::vector<PriceTicks> prices;
    const auto steps = static_cast<std::int64_t>(length) - 1;
    for (std::int64_t k = 0; k <= steps; ++k) {
        prices.emplace_back(from + ((to - from) * k / steps));
    }
    return prices;
}

AgentDecision decide_once(const AgentPopulation& agents, const MarketView& view,
                          const AccountView& account) {
    Rng rng{99};
    return decide(agents, 0, view, account, false, rng);
}

/// Runs [ticks] ticks of a simulation with the sample world and returns the event-log hash.
std::uint64_t agent_session_digest(std::uint64_t seed, int ticks, std::uint64_t* events = nullptr) {
    Simulation sim{
        SimulationConfig{.snapshot_interval_ticks = 16, .agents = sample_world(seed, 10)}};
    EventLogHasher hasher;
    for (int t = 0; t < ticks; ++t) {
        sim.step_tick();
        hasher.add(sim.events());
        sim.clear_events();
    }
    if (events != nullptr) {
        *events = hasher.event_count();
    }
    return hasher.digest();
}

} // namespace

TEST_CASE("value process is reproducible and stays positive", "[agents]") {
    ValueProcessConfig config;
    config.initial_value = PriceTicks{50};
    config.move_permille = 1'000;
    config.step = PriceTicks{10};
    config.jump_per_million = 100'000;
    config.jump = PriceTicks{40};
    ValueProcess a{config};
    ValueProcess b{config};
    Rng rng_a{5};
    Rng rng_b{5};
    for (int t = 0; t < 2'000; ++t) {
        a.advance(rng_a);
        b.advance(rng_b);
        REQUIRE(a.value() == b.value());
        REQUIRE(a.value() >= PriceTicks{1});
    }
}

TEST_CASE("value process makes four draws per tick whatever happens", "[agents]") {
    ValueProcessConfig still;
    still.move_permille = 0;
    still.jump_per_million = 0;
    ValueProcess process{still};
    Rng used{11};
    Rng reference{11};
    for (int t = 0; t < 10; ++t) {
        process.advance(used);
        for (int draw = 0; draw < 4; ++draw) {
            static_cast<void>(reference.next_u64());
        }
    }
    CHECK(process.value() == still.initial_value);
    CHECK(used.next_u64() == reference.next_u64());
}

TEST_CASE("sample world is valid and invalid worlds are rejected", "[agents]") {
    CHECK_FALSE(validate(sample_world(1, 50)).has_value());
    CHECK(agent_count(sample_world(1, 50)) == 50 * kTraderStyleCount);

    AgentWorldConfig overlap = sample_world(1, 10);
    overlap.market_maker.account = overlap.first_agent_account;
    CHECK(validate(overlap).has_value());

    AgentWorldConfig short_history = sample_world(1, 10);
    short_history.history_ticks = 1;
    CHECK(validate(short_history).has_value());

    AgentWorldConfig low_leverage = sample_world(1, 10);
    low_leverage.styles[0].leverage_percent_min = 50;
    CHECK(validate(low_leverage).has_value());

    AgentWorldConfig too_many = sample_world(1, 10);
    too_many.styles[0].count = kMaxAgents + 1;
    CHECK(validate(too_many).has_value());

    SimulationConfig sim_config;
    sim_config.agents = short_history;
    CHECK(validate(sim_config).has_value());
}

TEST_CASE("population draws parameters within the configured ranges", "[agents]") {
    const AgentWorldConfig config = sample_world(3, 25);
    Rng rng{3};
    const AgentPopulation agents = AgentPopulation::generate(config, rng);
    REQUIRE(agents.size() == 25 * kTraderStyleCount);
    for (std::size_t i = 0; i < agents.size(); ++i) {
        const auto style = static_cast<std::size_t>(agents.style[i]);
        const StyleConfig& s = config.styles[style];
        CHECK(style == i / 25);
        CHECK(agents.account[i] ==
              AccountId{config.first_agent_account.value() + static_cast<std::uint32_t>(i)});
        CHECK(agents.capital[i] >= s.capital_min);
        CHECK(agents.capital[i] <= s.capital_max);
        CHECK(agents.initial_shares[i] >= s.shares_min);
        CHECK(agents.initial_shares[i] <= s.shares_max);
        CHECK(agents.leverage_percent[i] >= s.leverage_percent_min);
        CHECK(agents.leverage_percent[i] <= s.leverage_percent_max);
        CHECK(agents.stop_distance_bps[i] >= s.stop_distance_bps_min);
        CHECK(agents.stop_distance_bps[i] <= s.stop_distance_bps_max);
        CHECK(agents.last_position[i] == agents.initial_shares[i]);
    }
}

TEST_CASE("an inattentive agent does nothing", "[agents]") {
    AgentPopulation agents = single_agent(TraderStyle::kNoise);
    agents.attention_permille[0] = 0;
    const std::vector<PriceTicks> history = ramp(10'000, 10'000, 5);
    const AgentDecision d =
        decide_once(agents, view_of(history), account_with(Money{1'000'000}, Qty{10}));
    CHECK_FALSE(d.order.has_value());
    CHECK_FALSE(d.stop.has_value());
}

TEST_CASE("noise trader without shares only buys", "[agents]") {
    const AgentPopulation agents = single_agent(TraderStyle::kNoise);
    const std::vector<PriceTicks> history = ramp(10'000, 10'000, 5);
    for (std::uint64_t seed = 0; seed < 50; ++seed) {
        Rng rng{seed};
        const AgentDecision d =
            decide(agents, 0, view_of(history), account_with(Money{1'000'000}, Qty{0}), false, rng);
        REQUIRE(d.order.has_value());
        CHECK(d.order->side == Side::kBuy);
        CHECK(d.order->qty >= Qty{1});
    }
}

TEST_CASE("momentum trader buys a rise and sells a fall", "[agents]") {
    const AgentPopulation agents = single_agent(TraderStyle::kMomentum);
    const std::vector<PriceTicks> rising = ramp(10'000, 10'200, 12);
    const AgentDecision buy =
        decide_once(agents, view_of(rising), account_with(Money{1'000'000}, Qty{0}));
    REQUIRE(buy.order.has_value());
    CHECK(buy.order->side == Side::kBuy);
    CHECK(buy.order->type == OrderType::kMarket);

    const std::vector<PriceTicks> falling = ramp(10'200, 10'000, 12);
    const AgentDecision sell =
        decide_once(agents, view_of(falling), account_with(Money{0}, Qty{40}));
    REQUIRE(sell.order.has_value());
    CHECK(sell.order->side == Side::kSell);
    CHECK(sell.order->qty == Qty{40});
    CHECK(sell.cancel_working);

    const std::vector<PriceTicks> flat = ramp(10'000, 10'000, 12);
    CHECK_FALSE(decide_once(agents, view_of(flat), account_with(Money{1'000'000}, Qty{0})).order);
}

TEST_CASE("mean reversion buys below the average and sells above it", "[agents]") {
    const AgentPopulation agents = single_agent(TraderStyle::kMeanReversion);
    std::vector<PriceTicks> dip(20, PriceTicks{10'000});
    dip.back() = PriceTicks{9'800};
    const AgentDecision buy =
        decide_once(agents, view_of(dip), account_with(Money{1'000'000}, Qty{0}));
    REQUIRE(buy.order.has_value());
    CHECK(buy.order->side == Side::kBuy);
    CHECK(buy.order->type == OrderType::kLimit);
    CHECK(buy.order->limit_price == PriceTicks{9'800});

    std::vector<PriceTicks> spike(20, PriceTicks{10'000});
    spike.back() = PriceTicks{10'200};
    const AgentDecision sell = decide_once(agents, view_of(spike), account_with(Money{0}, Qty{30}));
    REQUIRE(sell.order.has_value());
    CHECK(sell.order->side == Side::kSell);
    CHECK(sell.order->qty == Qty{16});
}

TEST_CASE("herding trader follows the crowd's taker flow", "[agents]") {
    const AgentPopulation agents = single_agent(TraderStyle::kHerding);
    const std::vector<PriceTicks> history = ramp(10'000, 10'000, 5);
    MarketView buying = view_of(history);
    buying.taker_buy_volume = Qty{900};
    buying.taker_sell_volume = Qty{100};
    const AgentDecision buy = decide_once(agents, buying, account_with(Money{1'000'000}, Qty{0}));
    REQUIRE(buy.order.has_value());
    CHECK(buy.order->side == Side::kBuy);

    MarketView selling = view_of(history);
    selling.taker_buy_volume = Qty{100};
    selling.taker_sell_volume = Qty{900};
    const AgentDecision sell = decide_once(agents, selling, account_with(Money{0}, Qty{25}));
    REQUIRE(sell.order.has_value());
    CHECK(sell.order->side == Side::kSell);
    CHECK(sell.order->qty == Qty{25});

    MarketView balanced = view_of(history);
    balanced.taker_buy_volume = Qty{500};
    balanced.taker_sell_volume = Qty{500};
    CHECK_FALSE(decide_once(agents, balanced, account_with(Money{1'000'000}, Qty{0})).order);
}

TEST_CASE("panic seller dumps everything after a drop from the high", "[agents]") {
    const AgentPopulation agents = single_agent(TraderStyle::kPanicSeller);
    // 200 bps stop distance: a 3% drop from the high triggers the dump, a 1% drop does not.
    const std::vector<PriceTicks> crash = ramp(10'000, 9'700, 10);
    const AgentDecision dump =
        decide_once(agents, view_of(crash), account_with(Money{0}, Qty{120}));
    REQUIRE(dump.order.has_value());
    CHECK(dump.order->side == Side::kSell);
    CHECK(dump.order->type == OrderType::kMarket);
    CHECK(dump.order->qty == Qty{120});

    const std::vector<PriceTicks> dip = ramp(10'000, 9'900, 10);
    CHECK_FALSE(decide_once(agents, view_of(dip), account_with(Money{0}, Qty{120})).order);
}

TEST_CASE("leverage gambler bets bigger than its cash allows", "[agents]") {
    const AgentPopulation gambler = single_agent(TraderStyle::kLeverageGambler);
    const AgentPopulation momentum = single_agent(TraderStyle::kMomentum);
    const std::vector<PriceTicks> rising = ramp(10'000, 10'200, 12);
    const AccountView flat = account_with(Money{1'000'000}, Qty{0});
    const AgentDecision bet = decide_once(gambler, view_of(rising), flat);
    const AgentDecision plain = decide_once(momentum, view_of(rising), flat);
    REQUIRE(bet.order.has_value());
    REQUIRE(plain.order.has_value());
    // Same risk share of equity; 5x leverage makes the order five times larger (up to the
    // rounding of whole shares).
    CHECK(bet.order->qty >= Qty{plain.order->qty.value() * 5});
    CHECK(bet.order->qty < Qty{(plain.order->qty.value() + 1) * 5});
}

TEST_CASE("value trader buys well below its value estimate", "[agents]") {
    const AgentPopulation agents = single_agent(TraderStyle::kValue);
    const std::vector<PriceTicks> cheap = ramp(9'000, 9'000, 5);
    const AgentDecision buy =
        decide_once(agents, view_of(cheap), account_with(Money{1'000'000}, Qty{0}));
    REQUIRE(buy.order.has_value());
    CHECK(buy.order->side == Side::kBuy);
    CHECK(buy.order->type == OrderType::kLimit);

    const std::vector<PriceTicks> dear = ramp(11'000, 11'000, 5);
    const AgentDecision sell = decide_once(agents, view_of(dear), account_with(Money{0}, Qty{40}));
    REQUIRE(sell.order.has_value());
    CHECK(sell.order->side == Side::kSell);

    const std::vector<PriceTicks> fair = ramp(10'000, 10'000, 5);
    CHECK_FALSE(decide_once(agents, view_of(fair), account_with(Money{1'000'000}, Qty{0})).order);
}

TEST_CASE("agents with a stop habit protect their position", "[agents]") {
    AgentPopulation agents = single_agent(TraderStyle::kMomentum, 1'000);
    REQUIRE(agents.uses_stops[0] == 1);
    agents.entry_price[0] = PriceTicks{10'000};
    const std::vector<PriceTicks> flat = ramp(10'000, 10'000, 12);
    const AgentDecision d = decide_once(agents, view_of(flat), account_with(Money{0}, Qty{50}));
    REQUIRE(d.stop.has_value());
    CHECK(d.stop->type == OrderType::kStop);
    CHECK(d.stop->side == Side::kSell);
    CHECK(d.stop->qty == Qty{50});
    CHECK(d.stop->stop_price == PriceTicks{9'800});

    // Already covered: nothing to do. Flat: drop the stop.
    agents.stop_order[0] = OrderId{77};
    agents.stop_covered[0] = Qty{50};
    CHECK_FALSE(decide_once(agents, view_of(flat), account_with(Money{0}, Qty{50})).stop);
    const AgentDecision flat_out =
        decide_once(agents, view_of(flat), account_with(Money{0}, Qty{0}));
    CHECK(flat_out.cancel_stop);
    CHECK_FALSE(flat_out.stop);
}

TEST_CASE("market maker quotes both sides and leans against its inventory", "[agents]") {
    MarketMakerConfig config;
    config.enabled = true;
    config.half_spread = PriceTicks{5};
    config.levels = 2;
    config.level_step = PriceTicks{3};
    config.quote_size = Qty{100};
    config.inventory = Qty{1'000};
    config.skew_per_tick = Qty{100};
    config.opening_price = PriceTicks{500};
    const MarketMaker maker{config};

    const AccountView neutral{
        .id = config.account, .cash = Money{1'000'000}, .position = Qty{1'000}};
    const std::vector<Quote> opening = maker.quotes(std::nullopt, neutral);
    REQUIRE(opening.size() == 4);
    CHECK(opening[0].side == Side::kBuy);
    CHECK(opening[0].price == PriceTicks{495});
    CHECK(opening[1].side == Side::kSell);
    CHECK(opening[1].price == PriceTicks{505});
    CHECK(opening[2].price == PriceTicks{492});
    CHECK(opening[3].price == PriceTicks{508});

    AccountView long_maker = neutral;
    long_maker.position = Qty{1'300};
    CHECK(maker.skew_ticks(long_maker) == -3);
    const std::vector<Quote> leaning = maker.quotes(PriceTicks{600}, long_maker);
    CHECK(leaning[0].price == PriceTicks{592});
    CHECK(leaning[1].price == PriceTicks{602});

    AccountView short_maker = neutral;
    short_maker.position = Qty{800};
    CHECK(maker.skew_ticks(short_maker) == 2);

    // Bids that would fall below one tick are not quoted.
    const std::vector<Quote> low = maker.quotes(PriceTicks{6}, neutral);
    for (const Quote& quote : low) {
        CHECK(quote.price >= PriceTicks{1});
    }
}

TEST_CASE("agents trade inside a simulation without breaking the books", "[agents]") {
    Simulation sim{SimulationConfig{.agents = sample_world(42, 20)}};
    REQUIRE(sim.world().has_value());
    for (int t = 0; t < 300; ++t) {
        sim.step_tick();
        sim.clear_events();
        REQUIRE(sim.engine().ledger().check_conservation());
    }
    CHECK(sim.engine().trade_count() > 100);
    const AgentPopulation& agents = sim.world()->agents();
    for (std::size_t i = 0; i < agents.size(); ++i) {
        const std::optional<AccountView> account = sim.engine().account(agents.account[i]);
        REQUIRE(account.has_value());
        CHECK(account->limited);
        CHECK(account->position >= Qty{0});
        CHECK(account->committed_sell <= account->position);
    }
    const std::optional<AccountView> maker =
        sim.engine().account(sim.world()->market_maker()->config().account);
    REQUIRE(maker.has_value());
    CHECK(maker->position >= Qty{0});
}

TEST_CASE("agent sessions are deterministic per seed", "[agents][determinism]") {
    CHECK(agent_session_digest(5, 150) == agent_session_digest(5, 150));
    CHECK(agent_session_digest(5, 150) != agent_session_digest(6, 150));
}

TEST_CASE("stepping back and redoing replays agents exactly", "[agents]") {
    Simulation live{SimulationConfig{.snapshot_interval_ticks = 16, .agents = sample_world(8, 10)}};
    EventLogHasher straight;
    for (int t = 0; t < 120; ++t) {
        live.step_tick();
        straight.add(live.events());
        live.clear_events();
    }

    Simulation travelled{
        SimulationConfig{.snapshot_interval_ticks = 16, .agents = sample_world(8, 10)}};
    EventLogHasher replayed;
    for (int t = 0; t < 120; ++t) {
        travelled.step_tick();
        replayed.add(travelled.events());
        travelled.clear_events();
        if (t % 25 == 24) {
            // Go back a few steps (across a snapshot boundary at times), then redo them.
            for (int back = 0; back < 40 && travelled.step_back(); ++back) {
            }
            travelled.clear_events();
            while (travelled.has_redo()) {
                travelled.step_event();
            }
            travelled.clear_events();
        }
    }
    CHECK(replayed.digest() == straight.digest());
    CHECK(travelled.engine().trade_count() == live.engine().trade_count());
    CHECK(travelled.world()->true_value() == live.world()->true_value());
}

TEST_CASE("a saved agent simulation continues identically after loading", "[agents]") {
    const SimulationConfig config{.agents = sample_world(13, 10)};
    Simulation original{config};
    for (int t = 0; t < 60; ++t) {
        original.step_tick();
    }
    original.clear_events();
    const std::vector<std::byte> saved = original.save();

    Result<Simulation> loaded = Simulation::load(saved, config);
    REQUIRE(loaded.has_value());
    Simulation& copy = loaded.value();
    REQUIRE(copy.world().has_value());
    CHECK(copy.world()->true_value() == original.world()->true_value());

    EventLogHasher a;
    EventLogHasher b;
    for (int t = 0; t < 80; ++t) {
        original.step_tick();
        copy.step_tick();
        a.add(original.events());
        b.add(copy.events());
        original.clear_events();
        copy.clear_events();
    }
    CHECK(a.event_count() > 0);
    CHECK(a.digest() == b.digest());
}

TEST_CASE("truncated agent state is rejected", "[agents]") {
    const SimulationConfig config{.agents = sample_world(13, 3)};
    Simulation sim{config};
    sim.step_tick();
    const std::vector<std::byte> saved = sim.save();
    for (const std::size_t cut : {saved.size() - 1, saved.size() - 9, saved.size() / 2}) {
        const std::span<const std::byte> part{saved.data(), cut};
        CHECK_FALSE(Simulation::load(part, config).has_value());
    }
}

TEST_CASE("agent session hash is pinned across platforms and builds",
          "[agents][determinism][golden]") {
    // Agents add many draws per tick, so this catches platform differences that the
    // input-driven golden in determinism_test.cpp cannot. Update together with intentional
    // changes to agent behaviour, and say so in the commit message.
    std::uint64_t events = 0;
    const std::uint64_t digest = agent_session_digest(2024, 200, &events);
    INFO("digest 0x" << std::hex << digest << std::dec << ", events " << events);
    CHECK(events == 5444);
    CHECK(digest == 0x213466937DCD61AFULL);
}
