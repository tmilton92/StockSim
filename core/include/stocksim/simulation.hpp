#pragma once

#include "stocksim/event.hpp"
#include "stocksim/matching_engine.hpp"
#include "stocksim/order.hpp"
#include "stocksim/result.hpp"
#include "stocksim/types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace stocksim {

/// Limits accepted by `validate(SimulationConfig)`.
inline constexpr std::size_t kMaxCapacityHint = std::size_t{1} << 24;
inline constexpr std::int64_t kMaxSnapshotIntervalTicks = 1'000'000;

struct SimulationConfig {
    EngineConfig engine{};
    /// A snapshot of the full state is kept every this many ticks (and always at the start).
    /// Bounds the replay cost of `step_back()` to about this many ticks of work. Zero or less
    /// disables periodic snapshots, so `step_back()` replays from the start.
    std::int64_t snapshot_interval_ticks = 64;
};

/// Checks every field of `config` against its allowed range. Returns the first problem found,
/// or nullopt if the configuration is valid.
[[nodiscard]] std::optional<Error> validate(const SimulationConfig& config) noexcept;

/// What `Simulation::advance()` did.
enum class AdvanceResult : std::uint8_t {
    /// One engine step was performed (or redone).
    kStepped,
    /// The tick advanced (live, or redone from the journal).
    kTickAdvanced,
};

/// The simulation as driven by the game: a `MatchingEngine` plus a journal of every input and
/// step, periodic snapshots, and time travel.
///
/// - `submit`/`cancel`/`modify`/`step_event`/`step_tick` behave like the engine's and are
///   recorded in the journal.
/// - `step_back()` undoes the most recent step (and any inputs given after it) by restoring
///   the nearest snapshot at or before that point and replaying the journal up to it.
/// - After stepping back, `step_event`/`step_tick` *redo* the recorded future, re-emitting the
///   same events with the same sequence numbers. Any new input (`submit`, `cancel`, `modify`)
///   while recorded future exists discards that future first, like undo history in an editor.
///
/// Events emitted while rebuilding state are discarded; after `step_back()` the event buffer
/// is empty, and consumers should drop anything they hold with `seq > last_sequence()`.
class Simulation {
public:
    explicit Simulation(const SimulationConfig& config = {});

    OrderId submit(const NewOrder& order);
    void cancel(AccountId requester, OrderId id);
    void modify(AccountId requester, OrderId id, PriceTicks new_limit_price, Qty new_qty);

    /// Performs (or redoes) one step. Returns false when there is nothing to do.
    bool step_event();
    /// Performs (or redoes) steps until the tick advances.
    void step_tick();
    /// Performs exactly one unit of work: one step if there is one, otherwise the tick advance.
    /// Never emits more than `kMaxEventsPerStep` events. This is what continuous runners use.
    AdvanceResult advance();
    /// Undoes the most recent step. Returns false if already at the beginning.
    bool step_back();

    [[nodiscard]] bool can_step_back() const noexcept { return m_cursor > 0; }
    /// True when `step_back()` has left recorded future that `step_event`/`step_tick` redo.
    [[nodiscard]] bool has_redo() const noexcept { return m_cursor < m_journal.size(); }

    [[nodiscard]] const MatchingEngine& engine() const noexcept { return m_engine; }
    [[nodiscard]] std::span<const Event> events() const noexcept { return m_engine.events(); }
    void clear_events() noexcept { m_engine.clear_events(); }
    [[nodiscard]] SequenceNumber last_sequence() const noexcept { return m_engine.last_sequence(); }

    /// Number of journal entries applied to reach the current state.
    [[nodiscard]] std::size_t position() const noexcept { return m_cursor; }
    [[nodiscard]] std::size_t journal_size() const noexcept { return m_journal.size(); }
    [[nodiscard]] std::size_t snapshot_count() const noexcept { return m_snapshots.size(); }

    /// Serializes the current state (header + engine state). History is not included: a
    /// loaded simulation starts with an empty journal at the saved state.
    [[nodiscard]] std::vector<std::byte> save() const;

    /// Restores a simulation saved with `save()`.
    [[nodiscard]] static Result<Simulation> load(std::span<const std::byte> bytes,
                                                 const SimulationConfig& config = {});

private:
    enum class JournalOp : std::uint8_t { kSubmit, kCancel, kModify, kStep, kAdvanceTick };

    struct JournalEntry {
        JournalOp op = JournalOp::kStep;
        OrderId id{};
        NewOrder order{};
        AccountId requester{};
        PriceTicks new_limit_price{};
        Qty new_qty{};
    };

    struct Snapshot {
        std::size_t position = 0;
        std::vector<std::byte> state{};
    };

    Simulation(const SimulationConfig& config, MatchingEngine engine);

    [[nodiscard]] static bool is_step(JournalOp op) noexcept {
        return op == JournalOp::kStep || op == JournalOp::kAdvanceTick;
    }

    /// Records an entry for work just performed at the end of the journal.
    void record(const JournalEntry& entry);
    /// Drops recorded future (and snapshots taken in it) before a new input is applied.
    void discard_future();
    /// Applies the journal entry at the cursor without recording it.
    void apply_next();
    void take_snapshot_if_due();
    void rewind_to(std::size_t target);

    SimulationConfig m_config;
    MatchingEngine m_engine;
    std::vector<JournalEntry> m_journal;
    std::size_t m_cursor = 0;
    /// Sorted by position; always contains position 0.
    std::vector<Snapshot> m_snapshots;
};

} // namespace stocksim
