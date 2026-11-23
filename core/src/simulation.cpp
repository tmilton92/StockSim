#include "stocksim/simulation.hpp"

#include "stocksim/serialization.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace stocksim {

namespace {

constexpr Error kBadMagic{.code = ErrorCode::kBadMagic,
                          .message = "Not a stocksim state file (bad magic number)."};
constexpr Error kUnsupportedVersion{
    .code = ErrorCode::kUnsupportedVersion,
    .message = "State was saved by an incompatible state format or event schema version."};
constexpr Error kTrailingBytes{.code = ErrorCode::kCorruptState,
                               .message = "State has unexpected data after the engine state."};
constexpr Error kCorruptAgents{.code = ErrorCode::kCorruptState,
                               .message = "State is missing or has an invalid agent section."};

} // namespace

std::optional<Error> validate(const SimulationConfig& config) noexcept {
    if (config.engine.expected_orders > kMaxCapacityHint) {
        return Error{.code = ErrorCode::kInvalidConfig,
                     .message = "expected_orders exceeds the maximum capacity hint (2^24)."};
    }
    if (config.engine.expected_events > kMaxCapacityHint) {
        return Error{.code = ErrorCode::kInvalidConfig,
                     .message = "expected_events exceeds the maximum capacity hint (2^24)."};
    }
    if (config.snapshot_interval_ticks < 0 ||
        config.snapshot_interval_ticks > kMaxSnapshotIntervalTicks) {
        return Error{.code = ErrorCode::kInvalidConfig,
                     .message = "snapshot_interval_ticks must be between 0 and 1000000."};
    }
    if (config.agents) {
        return validate(*config.agents);
    }
    return std::nullopt;
}

namespace {

std::optional<AgentWorld> make_world(const SimulationConfig& config, MatchingEngine& engine) {
    if (!config.agents) {
        return std::nullopt;
    }
    assert(!validate(*config.agents) && "validate(SimulationConfig) before constructing");
    AgentWorld world{*config.agents};
    world.open_accounts(engine);
    return world;
}

} // namespace

Simulation::Simulation(const SimulationConfig& config) : m_config(config), m_engine(config.engine) {
    m_world = make_world(config, m_engine);
    StateWriter writer;
    write_state(writer);
    m_snapshots.push_back(Snapshot{.position = 0, .state = writer.take()});
}

Simulation::Simulation(const SimulationConfig& config, MatchingEngine engine,
                       std::optional<AgentWorld> world)
    : m_config(config), m_engine(std::move(engine)), m_world(std::move(world)) {
    StateWriter writer;
    write_state(writer);
    m_snapshots.push_back(Snapshot{.position = 0, .state = writer.take()});
}

void Simulation::advance_tick() {
    m_engine.advance_tick();
    if (m_world) {
        m_world->on_tick_start(m_engine);
    }
}

void Simulation::write_state(StateWriter& out) const {
    m_engine.save_state(out);
    out.boolean(m_world.has_value());
    if (m_world) {
        m_world->save_state(out);
    }
}

void Simulation::read_state(StateReader& in) {
    Result<MatchingEngine> engine = MatchingEngine::load_state(in, m_config.engine);
    if (!engine) {
        in.fail();
        return;
    }
    std::optional<AgentWorld> world;
    if (in.boolean()) {
        Result<AgentWorld> loaded = AgentWorld::load_state(in);
        if (!loaded) {
            in.fail();
            return;
        }
        world = std::move(loaded).value();
    }
    if (in.ok()) {
        m_engine = std::move(engine).value();
        m_world = std::move(world);
    }
}

OrderId Simulation::submit(const NewOrder& order) {
    discard_future();
    const OrderId id = m_engine.submit(order);
    record(JournalEntry{.op = JournalOp::kSubmit, .id = id, .order = order});
    return id;
}

void Simulation::cancel(AccountId requester, OrderId id) {
    discard_future();
    m_engine.cancel(requester, id);
    record(JournalEntry{.op = JournalOp::kCancel, .id = id, .requester = requester});
}

void Simulation::modify(AccountId requester, OrderId id, PriceTicks new_limit_price, Qty new_qty) {
    discard_future();
    m_engine.modify(requester, id, new_limit_price, new_qty);
    record(JournalEntry{.op = JournalOp::kModify,
                        .id = id,
                        .requester = requester,
                        .new_limit_price = new_limit_price,
                        .new_qty = new_qty});
}

bool Simulation::step_event() {
    // Redo: replay recorded entries up to and including the next step.
    while (has_redo()) {
        const JournalOp op = m_journal[m_cursor].op;
        apply_next();
        if (is_step(op)) {
            return true;
        }
    }
    if (!m_engine.step_event()) {
        return false;
    }
    record(JournalEntry{.op = JournalOp::kStep});
    return true;
}

void Simulation::step_tick() {
    const Tick start = m_engine.current_tick();
    while (m_engine.current_tick() == start) {
        if (has_redo()) {
            apply_next();
        } else if (m_engine.step_event()) {
            record(JournalEntry{.op = JournalOp::kStep});
        } else {
            advance_tick();
            record(JournalEntry{.op = JournalOp::kAdvanceTick});
            take_snapshot_if_due();
        }
    }
}

AdvanceResult Simulation::advance() {
    const Tick before = m_engine.current_tick();
    if (!step_event()) {
        // No redo left and the engine is idle, so this only advances the tick.
        step_tick();
    }
    return m_engine.current_tick() == before ? AdvanceResult::kStepped
                                             : AdvanceResult::kTickAdvanced;
}

bool Simulation::step_back() {
    if (m_cursor == 0) {
        return false;
    }
    // Undo back to just before the most recent step; inputs after it are undone with it.
    std::size_t target = 0;
    for (std::size_t index = m_cursor; index > 0; --index) {
        if (is_step(m_journal[index - 1].op)) {
            target = index - 1;
            break;
        }
    }
    rewind_to(target);
    return true;
}

void Simulation::record(const JournalEntry& entry) {
    assert(!has_redo() && "recording requires the cursor at the end of the journal");
    m_journal.push_back(entry);
    ++m_cursor;
}

void Simulation::discard_future() {
    if (!has_redo()) {
        return;
    }
    m_journal.resize(m_cursor);
    std::erase_if(m_snapshots,
                  [&](const Snapshot& snapshot) { return snapshot.position > m_cursor; });
}

void Simulation::apply_next() {
    const JournalEntry entry = m_journal[m_cursor];
    ++m_cursor;
    switch (entry.op) {
    case JournalOp::kSubmit: {
        [[maybe_unused]] const OrderId id = m_engine.submit(entry.order);
        assert(id == entry.id && "replay must reproduce the original order ids");
        break;
    }
    case JournalOp::kCancel:
        m_engine.cancel(entry.requester, entry.id);
        break;
    case JournalOp::kModify:
        m_engine.modify(entry.requester, entry.id, entry.new_limit_price, entry.new_qty);
        break;
    case JournalOp::kStep: {
        [[maybe_unused]] const bool stepped = m_engine.step_event();
        assert(stepped && "replay must reproduce the original steps");
        break;
    }
    case JournalOp::kAdvanceTick:
        advance_tick();
        take_snapshot_if_due();
        break;
    }
}

void Simulation::take_snapshot_if_due() {
    const std::int64_t interval = m_config.snapshot_interval_ticks;
    if (interval <= 0 || m_engine.current_tick().value() % interval != 0) {
        return;
    }
    // During redo the snapshot for this position (and later ones) already exists.
    if (m_snapshots.back().position >= m_cursor) {
        return;
    }
    StateWriter writer;
    write_state(writer);
    m_snapshots.push_back(Snapshot{.position = m_cursor, .state = writer.take()});
}

void Simulation::rewind_to(std::size_t target) {
    auto snapshot = std::ranges::upper_bound(m_snapshots, target, {}, &Snapshot::position);
    assert(snapshot != m_snapshots.begin() && "position 0 is always snapshotted");
    --snapshot;

    StateReader reader{snapshot->state};
    read_state(reader);
    assert(reader.ok() && "a snapshot written by this process must load");
    m_cursor = snapshot->position;
    while (m_cursor < target) {
        apply_next();
    }
    m_engine.clear_events(); // already delivered before the rewind
}

std::vector<std::byte> Simulation::save() const {
    StateWriter out;
    out.u32(kStateMagic);
    out.u32(kStateFormatVersion);
    out.u32(kEventSchemaVersion);
    write_state(out);
    return out.take();
}

Result<Simulation> Simulation::load(std::span<const std::byte> bytes,
                                    const SimulationConfig& config) {
    StateReader in{bytes};
    if (in.u32() != kStateMagic) {
        return kBadMagic;
    }
    const std::uint32_t format_version = in.u32();
    const std::uint32_t schema_version = in.u32();
    if (!in.ok() || format_version != kStateFormatVersion ||
        schema_version != kEventSchemaVersion) {
        return kUnsupportedVersion;
    }
    Result<MatchingEngine> engine = MatchingEngine::load_state(in, config.engine);
    if (!engine) {
        return engine.error();
    }
    std::optional<AgentWorld> world;
    if (in.boolean()) {
        Result<AgentWorld> loaded = AgentWorld::load_state(in);
        if (!loaded) {
            return loaded.error();
        }
        world = std::move(loaded).value();
    }
    if (!in.ok()) {
        return kCorruptAgents;
    }
    if (in.remaining() != 0) {
        return kTrailingBytes;
    }
    return Simulation{config, std::move(engine).value(), std::move(world)};
}

} // namespace stocksim
