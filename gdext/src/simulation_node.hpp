#pragma once

#include "scenario_config.hpp"
#include "stocksim/event.hpp"
#include "stocksim/event_ring.hpp"
#include "stocksim/simulation.hpp"

#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace stocksim::gdext {

/// Godot node wrapping `stocksim::Simulation`. Translation only: every market decision is made
/// by the core. See doc_classes/Simulation.xml for the scripting documentation and
/// docs/adr/0007 for the threading model.
///
/// Threading: all public methods must be called from the main thread. `run()` starts a worker
/// thread that advances the simulation; every access to the core simulation (worker or main
/// thread) holds `m_mutex`. Events flow core -> `m_ring` (producer: whoever steps, always under
/// the mutex) -> `drain_events()` (consumer: main thread only). The producer reserves
/// `kMaxEventsPerStep` slots before every step, so events are never dropped; when the ring is
/// full the worker waits for the main thread to drain.
class Simulation : public godot::Node {
    // modernize-use-auto fires inside the godot-cpp macro body, which we cannot change.
    GDCLASS(Simulation, godot::Node) // NOLINT(modernize-use-auto)

public:
    // Enum values mirror the core enums exactly (static_asserts in the .cpp).
    enum Side : std::uint8_t { SIDE_BUY, SIDE_SELL };
    enum OrderType : std::uint8_t {
        ORDER_TYPE_MARKET,
        ORDER_TYPE_LIMIT,
        ORDER_TYPE_STOP,
        ORDER_TYPE_STOP_LIMIT,
    };
    enum OrderStatus : std::uint8_t {
        ORDER_STATUS_PENDING_STOP,
        ORDER_STATUS_TRIGGERED,
        ORDER_STATUS_MATCHING,
        ORDER_STATUS_RESTING,
    };
    enum EventType : std::uint8_t {
        EVENT_ORDER_PLACED,
        EVENT_ORDER_REJECTED,
        EVENT_ORDER_RESTED,
        EVENT_ORDER_CANCELLED,
        EVENT_ORDER_MODIFIED,
        EVENT_COMMAND_REJECTED,
        EVENT_STOP_TRIGGERED,
        EVENT_MATCH,
        EVENT_FILL,
        EVENT_PRICE_CHANGE,
    };
    enum Reason : std::uint8_t {
        REASON_NONE,
        REASON_ORDER_ACCEPTED,
        REASON_INVALID_QUANTITY,
        REASON_INVALID_LIMIT_PRICE,
        REASON_INVALID_STOP_PRICE,
        REASON_RESTING_ON_BOOK,
        REASON_CANCELLED_BY_OWNER,
        REASON_NO_LIQUIDITY,
        REASON_UNKNOWN_ORDER,
        REASON_NOT_ORDER_OWNER,
        REASON_NOT_MODIFIABLE,
        REASON_MODIFIED_KEPT_PRIORITY,
        REASON_MODIFIED_LOST_PRIORITY,
        REASON_STOP_PRICE_REACHED,
        REASON_TRADE,
        REASON_TAKER_FILL,
        REASON_MAKER_FILL,
        REASON_LAST_TRADE_PRICE,
    };
    /// Column index of each field in a `drain_events()` row; EVENT_STRIDE is the row width.
    enum EventField : std::uint8_t {
        EVENT_FIELD_SEQ,
        EVENT_FIELD_TICK,
        EVENT_FIELD_TYPE,
        EVENT_FIELD_REASON,
        EVENT_FIELD_INTENT,
        EVENT_FIELD_SIDE,
        EVENT_FIELD_ORDER_TYPE,
        EVENT_FIELD_ORDER_ID,
        EVENT_FIELD_COUNTER_ORDER_ID,
        EVENT_FIELD_ACCOUNT,
        EVENT_FIELD_COUNTER_ACCOUNT,
        EVENT_FIELD_PRICE,
        EVENT_FIELD_AUX_PRICE,
        EVENT_FIELD_QTY,
        EVENT_FIELD_REMAINING,
        EVENT_STRIDE,
    };
    /// Column index of each field returned by `get_order()`.
    enum OrderField : std::uint8_t {
        ORDER_FIELD_ID,
        ORDER_FIELD_ACCOUNT,
        ORDER_FIELD_SIDE,
        ORDER_FIELD_TYPE,
        ORDER_FIELD_STATUS,
        ORDER_FIELD_LIMIT_PRICE,
        ORDER_FIELD_STOP_PRICE,
        ORDER_FIELD_REMAINING,
        ORDER_FIELD_FILLED,
        ORDER_STRIDE,
    };
    /// Column index of each field in a `get_depth()` row.
    enum DepthField : std::uint8_t {
        DEPTH_FIELD_PRICE,
        DEPTH_FIELD_QTY,
        DEPTH_FIELD_ORDER_COUNT,
        DEPTH_STRIDE,
    };

    Simulation();
    ~Simulation() override;
    // GDCLASS declares the (private, unusable) copy assignment; nodes are never copied or moved.
    Simulation(const Simulation&) = delete;
    Simulation(Simulation&&) = delete;
    Simulation& operator=(Simulation&&) = delete;

    // Configuration
    godot::Error load_scenario(const godot::String& path);
    godot::Error configure(const godot::Dictionary& scenario);
    [[nodiscard]] godot::String get_scenario_name() const;

    // Inputs
    int64_t submit_order(int64_t account, Side side, OrderType type, int64_t qty,
                         int64_t limit_price, int64_t stop_price);
    void cancel_order(int64_t account, int64_t order_id);
    void modify_order(int64_t account, int64_t order_id, int64_t new_limit_price, int64_t new_qty);

    // Stepping and time travel
    bool step_event();
    bool step_tick();
    bool step_back();
    [[nodiscard]] bool can_step_back() const;
    [[nodiscard]] bool has_redo() const;

    // Continuous running on the worker thread
    void run(double steps_per_second);
    void pause();
    [[nodiscard]] bool is_running() const;
    [[nodiscard]] double get_steps_per_second() const;

    // Events
    godot::PackedInt64Array drain_events(int64_t max_events);
    [[nodiscard]] int64_t get_pending_event_count() const;

    // Market and order queries
    [[nodiscard]] int64_t get_last_sequence() const;
    [[nodiscard]] int64_t get_current_tick() const;
    [[nodiscard]] bool has_last_price() const;
    [[nodiscard]] int64_t get_last_price() const;
    [[nodiscard]] bool has_best_bid() const;
    [[nodiscard]] int64_t get_best_bid() const;
    [[nodiscard]] bool has_best_ask() const;
    [[nodiscard]] int64_t get_best_ask() const;
    [[nodiscard]] int64_t get_total_volume() const;
    [[nodiscard]] int64_t get_tick_volume() const;
    [[nodiscard]] int64_t get_trade_count() const;
    [[nodiscard]] godot::PackedInt64Array get_depth(Side side, int64_t max_levels) const;
    [[nodiscard]] godot::PackedInt64Array get_order(int64_t order_id) const;

    // Persistence
    [[nodiscard]] godot::PackedByteArray save_state() const;
    godot::Error load_state(const godot::PackedByteArray& data);

    // Static helpers for narration
    static godot::String reason_text(int64_t reason);
    static godot::String event_type_name(int64_t type);
    static int64_t get_event_schema_version();

    void _exit_tree() override;

protected:
    static void _bind_methods();

private:
    /// Swaps in a new core simulation (stopping any run) and clears all buffered events.
    void replace(const ScenarioSettings& settings, stocksim::Simulation simulation);
    /// Moves the core's buffered events into the ring. Requires the lock and enough space.
    void pump_events_locked();
    [[nodiscard]] bool has_step_space_locked() const;
    void start_worker();
    void stop_worker();
    void worker_main();
    /// Wakes the worker after the consumer freed ring space or run state changed.
    void wake_worker();

    // Main-thread-only state.
    ScenarioSettings m_settings;
    std::vector<stocksim::Event> m_carry; ///< Events kept across a step_back, delivered first.
    std::vector<stocksim::Event> m_drain_scratch;
    std::thread m_worker;

    // Shared state, guarded by m_mutex.
    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    std::unique_ptr<stocksim::Simulation> m_simulation;
    std::unique_ptr<stocksim::EventRing> m_ring; ///< Pointer changes only under the lock.
    double m_steps_per_second = 0.0;
    bool m_running = false;
    bool m_stop_requested = false;
    bool m_pacing_reset = false;
};

} // namespace stocksim::gdext

VARIANT_ENUM_CAST(stocksim::gdext::Simulation::Side);
VARIANT_ENUM_CAST(stocksim::gdext::Simulation::OrderType);
VARIANT_ENUM_CAST(stocksim::gdext::Simulation::OrderStatus);
VARIANT_ENUM_CAST(stocksim::gdext::Simulation::EventType);
VARIANT_ENUM_CAST(stocksim::gdext::Simulation::Reason);
VARIANT_ENUM_CAST(stocksim::gdext::Simulation::EventField);
VARIANT_ENUM_CAST(stocksim::gdext::Simulation::OrderField);
VARIANT_ENUM_CAST(stocksim::gdext::Simulation::DepthField);
