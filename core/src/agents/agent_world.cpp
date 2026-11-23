#include "stocksim/agents/agent_world.hpp"

#include "stocksim/matching_engine.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace stocksim {

namespace {

/// Limit orders older than this many ticks are cancelled by their owner (a day-order habit),
/// so the book does not fill up with forgotten quotes.
constexpr std::int64_t kStaleOrderTicks = 20;

/// Random stream used to draw the population, kept apart from the per-tick streams.
constexpr std::uint64_t kPopulationStream = 0x506F70756C6174ULL; // "Populat"

bool is_live(const MatchingEngine& engine, OrderId id) {
    return id != OrderId{} && engine.find_order(id).has_value();
}

constexpr Error kCorruptWorld{.code = ErrorCode::kCorruptState,
                              .message = "Agent world state is truncated or inconsistent."};

void write_value_config(StateWriter& out, const ValueProcessConfig& c) {
    out.i64(c.initial_value.value());
    out.u32(c.move_permille);
    out.i64(c.step.value());
    out.u32(c.jump_per_million);
    out.i64(c.jump.value());
}

ValueProcessConfig read_value_config(StateReader& in) {
    ValueProcessConfig c;
    c.initial_value = PriceTicks{in.i64()};
    c.move_permille = in.u32();
    c.step = PriceTicks{in.i64()};
    c.jump_per_million = in.u32();
    c.jump = PriceTicks{in.i64()};
    return c;
}

void write_maker_config(StateWriter& out, const MarketMakerConfig& c) {
    out.boolean(c.enabled);
    out.u32(c.account.value());
    out.i64(c.capital.value());
    out.i64(c.inventory.value());
    out.i64(c.half_spread.value());
    out.u32(c.levels);
    out.i64(c.level_step.value());
    out.i64(c.quote_size.value());
    out.i64(c.skew_per_tick.value());
    out.i64(c.opening_price.value());
}

MarketMakerConfig read_maker_config(StateReader& in) {
    MarketMakerConfig c;
    c.enabled = in.boolean();
    c.account = AccountId{in.u32()};
    c.capital = Money{in.i64()};
    c.inventory = Qty{in.i64()};
    c.half_spread = PriceTicks{in.i64()};
    c.levels = in.u32();
    c.level_step = PriceTicks{in.i64()};
    c.quote_size = Qty{in.i64()};
    c.skew_per_tick = Qty{in.i64()};
    c.opening_price = PriceTicks{in.i64()};
    return c;
}

} // namespace

AgentWorld::AgentWorld(const AgentWorldConfig& config)
    : m_seed(config.seed), m_value_config(config.value), m_maker_config(config.market_maker),
      m_history_ticks(config.history_ticks), m_rng(config.seed), m_value(config.value) {
    Rng population_rng = Rng{config.seed}.derive(kPopulationStream);
    m_agents = AgentPopulation::generate(config, population_rng);
    if (config.market_maker.enabled) {
        m_maker.emplace(config.market_maker);
    }
}

void AgentWorld::open_accounts(MatchingEngine& engine) const {
    if (m_maker) {
        [[maybe_unused]] const bool opened = engine.open_account(
            m_maker_config.account, m_maker_config.capital, m_maker_config.inventory, 100);
        assert(opened && "market maker account must be new");
    }
    for (std::size_t i = 0; i < m_agents.size(); ++i) {
        [[maybe_unused]] const bool opened =
            engine.open_account(m_agents.account[i], m_agents.capital[i],
                                m_agents.initial_shares[i], m_agents.leverage_percent[i]);
        assert(opened && "agent accounts must be new");
    }
}

PriceTicks AgentWorld::reference_price(const MatchingEngine& engine) const {
    if (const std::optional<PriceTicks> last = engine.last_price()) {
        return *last;
    }
    const std::optional<PriceTicks> bid = engine.best_bid();
    const std::optional<PriceTicks> ask = engine.best_ask();
    if (bid && ask) {
        return PriceTicks{(bid->value() + ask->value()) / 2};
    }
    return m_maker ? m_maker_config.opening_price : m_value_config.initial_value;
}

void AgentWorld::on_tick_start(MatchingEngine& engine) {
    m_value.advance(m_rng);
    const PriceTicks reference = reference_price(engine);
    m_history.push_back(reference);
    if (m_history.size() > m_history_ticks) {
        m_history.erase(m_history.begin());
    }
    const MarketView view{.tick = engine.current_tick(),
                          .reference = reference,
                          .best_bid = engine.best_bid(),
                          .best_ask = engine.best_ask(),
                          .history = m_history,
                          .taker_buy_volume = engine.previous_tick_taker_buy_volume(),
                          .taker_sell_volume = engine.previous_tick_taker_sell_volume(),
                          .true_value = m_value.value()};
    // One draw from the world stream seeds this tick; every agent derives its own stream from it.
    const Rng tick_rng{m_rng.next_u64()};
    run_market_maker(engine);
    for (std::size_t i = 0; i < m_agents.size(); ++i) {
        run_agent(engine, i, view, tick_rng);
    }
}

void AgentWorld::run_market_maker(MatchingEngine& engine) {
    if (!m_maker) {
        return;
    }
    const AccountId account = m_maker_config.account;
    for (const OrderId id : m_maker->live_quotes()) {
        if (is_live(engine, id)) {
            engine.cancel(account, id);
        }
    }
    m_maker->set_live_quotes({});
    const std::optional<AccountView> view = engine.account(account);
    if (!view) {
        return;
    }
    std::vector<OrderId> placed;
    for (const Quote& quote : m_maker->quotes(engine.last_price(), *view)) {
        placed.push_back(engine.submit(NewOrder{.account = account,
                                                .side = quote.side,
                                                .type = OrderType::kLimit,
                                                .qty = quote.qty,
                                                .limit_price = quote.price}));
    }
    m_maker->set_live_quotes(std::move(placed));
}

void AgentWorld::run_agent(MatchingEngine& engine, std::size_t i, const MarketView& view,
                           const Rng& tick_rng) {
    AgentPopulation& a = m_agents;
    const std::optional<AccountView> account = engine.account(a.account[i]);
    if (!account) {
        return;
    }
    // Housekeeping: forget orders that have finished, drop stale working orders, and note the
    // price whenever the position grew (stops are placed relative to it).
    if (a.stop_order[i] != OrderId{} && !is_live(engine, a.stop_order[i])) {
        a.stop_order[i] = OrderId{};
        a.stop_covered[i] = Qty{};
    }
    bool working_live = is_live(engine, a.working_order[i]);
    if (working_live && (view.tick - a.working_since[i]).value() > kStaleOrderTicks) {
        engine.cancel(a.account[i], a.working_order[i]);
        working_live = false;
    }
    if (account->position > a.last_position[i]) {
        a.entry_price[i] = view.reference;
    }
    a.last_position[i] = account->position;

    Rng rng = tick_rng.derive(i);
    const AgentDecision decision = decide(a, i, view, *account, working_live, rng);

    if (decision.cancel_working && working_live) {
        engine.cancel(a.account[i], a.working_order[i]);
    }
    if (decision.cancel_stop && a.stop_order[i] != OrderId{}) {
        engine.cancel(a.account[i], a.stop_order[i]);
        a.stop_order[i] = OrderId{};
        a.stop_covered[i] = Qty{};
    }
    if (decision.order) {
        const OrderId id = engine.submit(*decision.order);
        if (decision.order->type == OrderType::kLimit) {
            a.working_order[i] = id;
            a.working_since[i] = view.tick;
        }
    }
    if (decision.stop) {
        a.stop_order[i] = engine.submit(*decision.stop);
        a.stop_covered[i] = decision.stop->qty;
    }
}

void AgentWorld::save_state(StateWriter& out) const {
    out.u64(m_seed);
    write_value_config(out, m_value_config);
    write_maker_config(out, m_maker_config);
    out.u32(m_history_ticks);
    for (const std::uint64_t word : m_rng.state()) {
        out.u64(word);
    }
    m_value.save_state(out);
    out.u64(m_history.size());
    for (const PriceTicks price : m_history) {
        out.i64(price.value());
    }
    m_agents.save_state(out);
    if (m_maker) {
        m_maker->save_state(out);
    }
}

Result<AgentWorld> AgentWorld::load_state(StateReader& in) {
    AgentWorld world;
    world.m_seed = in.u64();
    world.m_value_config = read_value_config(in);
    world.m_maker_config = read_maker_config(in);
    world.m_history_ticks = in.u32();
    Rng::State state{};
    for (std::uint64_t& word : state) {
        word = in.u64();
    }
    world.m_rng = Rng::from_state(state);
    world.m_value = ValueProcess{world.m_value_config};
    world.m_value.load_state(in);
    const std::uint64_t history = in.count(8);
    if (history > kMaxHistoryTicks) {
        in.fail();
    }
    for (std::uint64_t i = 0; i < history && in.ok(); ++i) {
        world.m_history.emplace_back(in.i64());
    }
    world.m_agents.load_state(in);
    if (world.m_maker_config.enabled) {
        world.m_maker.emplace(world.m_maker_config);
        world.m_maker->load_state(in);
    }
    if (world.m_history_ticks < 2 || world.m_history_ticks > kMaxHistoryTicks) {
        in.fail();
    }
    if (!in.ok()) {
        return kCorruptWorld;
    }
    return world;
}

} // namespace stocksim
