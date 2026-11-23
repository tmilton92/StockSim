#include "simulation_node.hpp"

#include "scenario_config.hpp"
#include "stocksim/event.hpp"
#include "stocksim/event_ring.hpp"
#include "stocksim/matching_engine.hpp"
#include "stocksim/order.hpp"
#include "stocksim/result.hpp"
#include "stocksim/simulation.hpp"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/core/property_info.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace stocksim::gdext {

namespace {

using godot::String;

// The bound enums are a byte-for-byte mirror of the core enums.
static_assert(Simulation::SIDE_BUY == static_cast<int>(Side::kBuy));
static_assert(Simulation::SIDE_SELL == static_cast<int>(Side::kSell));
static_assert(Simulation::ORDER_TYPE_MARKET == static_cast<int>(OrderType::kMarket));
static_assert(Simulation::ORDER_TYPE_LIMIT == static_cast<int>(OrderType::kLimit));
static_assert(Simulation::ORDER_TYPE_STOP == static_cast<int>(OrderType::kStop));
static_assert(Simulation::ORDER_TYPE_STOP_LIMIT == static_cast<int>(OrderType::kStopLimit));
static_assert(Simulation::ORDER_STATUS_PENDING_STOP == static_cast<int>(OrderStatus::kPendingStop));
static_assert(Simulation::ORDER_STATUS_TRIGGERED == static_cast<int>(OrderStatus::kTriggered));
static_assert(Simulation::ORDER_STATUS_MATCHING == static_cast<int>(OrderStatus::kMatching));
static_assert(Simulation::ORDER_STATUS_RESTING == static_cast<int>(OrderStatus::kResting));
static_assert(Simulation::EVENT_ORDER_PLACED == static_cast<int>(EventType::kOrderPlaced));
static_assert(Simulation::EVENT_ORDER_REJECTED == static_cast<int>(EventType::kOrderRejected));
static_assert(Simulation::EVENT_ORDER_RESTED == static_cast<int>(EventType::kOrderRested));
static_assert(Simulation::EVENT_ORDER_CANCELLED == static_cast<int>(EventType::kOrderCancelled));
static_assert(Simulation::EVENT_ORDER_MODIFIED == static_cast<int>(EventType::kOrderModified));
static_assert(Simulation::EVENT_COMMAND_REJECTED == static_cast<int>(EventType::kCommandRejected));
static_assert(Simulation::EVENT_STOP_TRIGGERED == static_cast<int>(EventType::kStopTriggered));
static_assert(Simulation::EVENT_MATCH == static_cast<int>(EventType::kMatch));
static_assert(Simulation::EVENT_FILL == static_cast<int>(EventType::kFill));
static_assert(Simulation::EVENT_PRICE_CHANGE == static_cast<int>(EventType::kPriceChange));
static_assert(Simulation::REASON_NONE == static_cast<int>(Reason::kNone));
static_assert(Simulation::REASON_ORDER_ACCEPTED == static_cast<int>(Reason::kOrderAccepted));
static_assert(Simulation::REASON_INVALID_QUANTITY == static_cast<int>(Reason::kInvalidQuantity));
static_assert(Simulation::REASON_INVALID_LIMIT_PRICE ==
              static_cast<int>(Reason::kInvalidLimitPrice));
static_assert(Simulation::REASON_INVALID_STOP_PRICE == static_cast<int>(Reason::kInvalidStopPrice));
static_assert(Simulation::REASON_RESTING_ON_BOOK == static_cast<int>(Reason::kRestingOnBook));
static_assert(Simulation::REASON_CANCELLED_BY_OWNER == static_cast<int>(Reason::kCancelledByOwner));
static_assert(Simulation::REASON_NO_LIQUIDITY == static_cast<int>(Reason::kNoLiquidity));
static_assert(Simulation::REASON_UNKNOWN_ORDER == static_cast<int>(Reason::kUnknownOrder));
static_assert(Simulation::REASON_NOT_ORDER_OWNER == static_cast<int>(Reason::kNotOrderOwner));
static_assert(Simulation::REASON_NOT_MODIFIABLE == static_cast<int>(Reason::kNotModifiable));
static_assert(Simulation::REASON_MODIFIED_KEPT_PRIORITY ==
              static_cast<int>(Reason::kModifiedKeptPriority));
static_assert(Simulation::REASON_MODIFIED_LOST_PRIORITY ==
              static_cast<int>(Reason::kModifiedLostPriority));
static_assert(Simulation::REASON_STOP_PRICE_REACHED == static_cast<int>(Reason::kStopPriceReached));
static_assert(Simulation::REASON_TRADE == static_cast<int>(Reason::kTrade));
static_assert(Simulation::REASON_TAKER_FILL == static_cast<int>(Reason::kTakerFill));
static_assert(Simulation::REASON_MAKER_FILL == static_cast<int>(Reason::kMakerFill));
static_assert(Simulation::REASON_LAST_TRADE_PRICE == static_cast<int>(Reason::kLastTradePrice));
static_assert(Simulation::REASON_INSUFFICIENT_FUNDS ==
              static_cast<int>(Reason::kInsufficientFunds));
static_assert(Simulation::REASON_INSUFFICIENT_SHARES ==
              static_cast<int>(Reason::kInsufficientShares));
static_assert(kEventSchemaVersion == 2, "update the EVENT_* / REASON_* mirrors and docs");

constexpr int64_t kMaxDepthLevels = 4096;
/// Steps taken per lock hold when running unthrottled, so the main thread is never starved.
constexpr int kUnthrottledBatch = 256;
/// After a stall longer than this, paced running resumes from "now" instead of bursting.
constexpr std::chrono::milliseconds kMaxPacingLag{250};

String to_godot(std::string_view text) {
    return String::utf8(text.data(), static_cast<int64_t>(text.size()));
}

void write_event(std::span<int64_t> row, const Event& event) {
    row[Simulation::EVENT_FIELD_SEQ] = static_cast<int64_t>(event.seq.value());
    row[Simulation::EVENT_FIELD_TICK] = event.tick.value();
    row[Simulation::EVENT_FIELD_TYPE] = static_cast<int64_t>(event.type);
    row[Simulation::EVENT_FIELD_REASON] = static_cast<int64_t>(event.reason);
    row[Simulation::EVENT_FIELD_INTENT] = static_cast<int64_t>(event.intent);
    row[Simulation::EVENT_FIELD_SIDE] = static_cast<int64_t>(event.side);
    row[Simulation::EVENT_FIELD_ORDER_TYPE] = static_cast<int64_t>(event.order_type);
    row[Simulation::EVENT_FIELD_ORDER_ID] = static_cast<int64_t>(event.order_id.value());
    row[Simulation::EVENT_FIELD_COUNTER_ORDER_ID] =
        static_cast<int64_t>(event.counter_order_id.value());
    row[Simulation::EVENT_FIELD_ACCOUNT] = event.account.value();
    row[Simulation::EVENT_FIELD_COUNTER_ACCOUNT] = event.counter_account.value();
    row[Simulation::EVENT_FIELD_PRICE] = event.price.value();
    row[Simulation::EVENT_FIELD_AUX_PRICE] = event.aux_price.value();
    row[Simulation::EVENT_FIELD_QTY] = event.qty.value();
    row[Simulation::EVENT_FIELD_REMAINING] = event.remaining.value();
}

bool valid_account(int64_t account) {
    return account >= 0 && account <= std::numeric_limits<std::uint32_t>::max();
}

} // namespace

Simulation::Simulation() {
    replace(ScenarioSettings{}, stocksim::Simulation{ScenarioSettings{}.simulation});
}

Simulation::~Simulation() {
    stop_worker();
}

// --- Configuration -----------------------------------------------------------------------------

godot::Error Simulation::load_scenario(const String& path) {
    const String text = godot::FileAccess::get_file_as_string(path);
    const godot::Error open_error = godot::FileAccess::get_open_error();
    ERR_FAIL_COND_V_MSG(open_error != godot::OK, open_error,
                        "Cannot read scenario file \"" + path + "\".");

    const godot::Ref<godot::JSON> json = memnew(godot::JSON);
    const godot::Error parse_error = json->parse(text);
    ERR_FAIL_COND_V_MSG(parse_error != godot::OK, godot::ERR_PARSE_ERROR,
                        "Invalid JSON in scenario \"" + path + "\" at line " +
                            String::num_int64(json->get_error_line()) + ": " +
                            json->get_error_message());
    const godot::Variant data = json->get_data();
    ERR_FAIL_COND_V_MSG(data.get_type() != godot::Variant::DICTIONARY, godot::ERR_INVALID_DATA,
                        "Scenario \"" + path + "\" must contain a JSON object.");
    return configure(data);
}

godot::Error Simulation::configure(const godot::Dictionary& scenario) {
    ScenarioSettings settings;
    String message;
    const godot::Error error = parse_scenario(scenario, settings, message);
    ERR_FAIL_COND_V_MSG(error != godot::OK, error, "Invalid scenario: " + message);
    replace(settings, stocksim::Simulation{settings.simulation});
    return godot::OK;
}

String Simulation::get_scenario_name() const {
    return m_settings.name;
}

void Simulation::replace(const ScenarioSettings& settings, stocksim::Simulation simulation) {
    {
        const std::lock_guard lock{m_mutex};
        m_running = false;
        m_simulation = std::make_unique<stocksim::Simulation>(std::move(simulation));
        // Safe without draining: this is the consumer thread and the producer needs the lock.
        m_ring = std::make_unique<EventRing>(settings.event_buffer_capacity);
    }
    m_settings = settings;
    m_carry.clear();
    wake_worker();
}

// --- Inputs ------------------------------------------------------------------------------------

int64_t Simulation::submit_order(int64_t account, Side side, OrderType type, int64_t qty,
                                 int64_t limit_price, int64_t stop_price) {
    ERR_FAIL_COND_V_MSG(!valid_account(account), 0, "account must be in [0, 4294967295].");
    ERR_FAIL_COND_V_MSG(side > SIDE_SELL, 0, "side must be a Simulation.Side value.");
    ERR_FAIL_COND_V_MSG(type > ORDER_TYPE_STOP_LIMIT, 0,
                        "type must be a Simulation.OrderType value.");
    const NewOrder order{.account = AccountId{static_cast<std::uint32_t>(account)},
                         .side = static_cast<stocksim::Side>(side),
                         .type = static_cast<stocksim::OrderType>(type),
                         .qty = Qty{qty},
                         .limit_price = PriceTicks{limit_price},
                         .stop_price = PriceTicks{stop_price}};
    const std::lock_guard lock{m_mutex};
    return static_cast<int64_t>(m_simulation->submit(order).value());
}

void Simulation::cancel_order(int64_t account, int64_t order_id) {
    ERR_FAIL_COND_MSG(!valid_account(account), "account must be in [0, 4294967295].");
    ERR_FAIL_COND_MSG(order_id < 0, "order_id must not be negative.");
    const std::lock_guard lock{m_mutex};
    m_simulation->cancel(AccountId{static_cast<std::uint32_t>(account)},
                         OrderId{static_cast<std::uint64_t>(order_id)});
}

void Simulation::modify_order(int64_t account, int64_t order_id, int64_t new_limit_price,
                              int64_t new_qty) {
    ERR_FAIL_COND_MSG(!valid_account(account), "account must be in [0, 4294967295].");
    ERR_FAIL_COND_MSG(order_id < 0, "order_id must not be negative.");
    const std::lock_guard lock{m_mutex};
    m_simulation->modify(AccountId{static_cast<std::uint32_t>(account)},
                         OrderId{static_cast<std::uint64_t>(order_id)}, PriceTicks{new_limit_price},
                         Qty{new_qty});
}

// --- Stepping ----------------------------------------------------------------------------------

bool Simulation::has_step_space_locked() const {
    return m_ring->free_space() >= kMaxEventsPerStep;
}

void Simulation::pump_events_locked() {
    [[maybe_unused]] const bool pushed = m_ring->try_push(m_simulation->events());
    // Space for kMaxEventsPerStep is reserved before every step, so this cannot fail.
    ERR_FAIL_COND_MSG(!pushed, "Event buffer overflow: this is a bug in the Simulation node.");
    m_simulation->clear_events();
}

bool Simulation::step_event() {
    const std::lock_guard lock{m_mutex};
    ERR_FAIL_COND_V_MSG(!has_step_space_locked(), false,
                        "Event buffer is full: call drain_events() before stepping.");
    const bool stepped = m_simulation->step_event();
    pump_events_locked();
    return stepped;
}

bool Simulation::step_tick() {
    const std::lock_guard lock{m_mutex};
    while (has_step_space_locked()) {
        const AdvanceResult result = m_simulation->advance();
        pump_events_locked();
        if (result == AdvanceResult::kTickAdvanced) {
            return true;
        }
    }
    return false; // buffer full mid-tick: drain_events() and call again
}

bool Simulation::step_back() {
    int64_t last_sequence = 0;
    bool stepped = false;
    {
        const std::lock_guard lock{m_mutex};
        stepped = m_simulation->step_back();
        last_sequence = static_cast<int64_t>(m_simulation->last_sequence().value());
        // This is the consumer thread: move undelivered events out of the ring, keeping only
        // those that still happened in the rewound timeline.
        std::vector<Event> undelivered(m_ring->size());
        undelivered.resize(m_ring->pop(undelivered));
        m_carry.insert(m_carry.end(), undelivered.begin(), undelivered.end());
        std::erase_if(m_carry, [&](const Event& event) {
            return static_cast<int64_t>(event.seq.value()) > last_sequence;
        });
    }
    wake_worker();
    if (stepped) {
        emit_signal("rewound", last_sequence);
    }
    return stepped;
}

bool Simulation::can_step_back() const {
    const std::lock_guard lock{m_mutex};
    return m_simulation->can_step_back();
}

bool Simulation::has_redo() const {
    const std::lock_guard lock{m_mutex};
    return m_simulation->has_redo();
}

// --- Worker thread -----------------------------------------------------------------------------

void Simulation::run(double steps_per_second) {
    ERR_FAIL_COND_MSG(!std::isfinite(steps_per_second) || steps_per_second < 0.0,
                      "steps_per_second must be finite and >= 0 (0 = as fast as possible).");
    {
        const std::lock_guard lock{m_mutex};
        m_steps_per_second = steps_per_second;
        m_running = true;
        m_pacing_reset = true;
    }
    start_worker();
    wake_worker();
}

void Simulation::pause() {
    {
        const std::lock_guard lock{m_mutex};
        m_running = false;
    }
    wake_worker();
}

bool Simulation::is_running() const {
    const std::lock_guard lock{m_mutex};
    return m_running;
}

double Simulation::get_steps_per_second() const {
    const std::lock_guard lock{m_mutex};
    return m_steps_per_second;
}

void Simulation::start_worker() {
    if (m_worker.joinable()) {
        return;
    }
    {
        const std::lock_guard lock{m_mutex};
        m_stop_requested = false;
    }
    m_worker = std::thread{[this] { worker_main(); }};
}

void Simulation::stop_worker() {
    if (!m_worker.joinable()) {
        return;
    }
    {
        const std::lock_guard lock{m_mutex};
        m_stop_requested = true;
        m_running = false;
    }
    m_wake.notify_all();
    m_worker.join();
}

void Simulation::wake_worker() {
    // Taking the lock (even briefly) before notifying closes the window in which the worker has
    // evaluated its wait predicate but not yet blocked, so the wake-up cannot be lost.
    {
        const std::lock_guard lock{m_mutex};
    }
    m_wake.notify_all();
}

void Simulation::worker_main() {
    using Clock = std::chrono::steady_clock;
    std::unique_lock lock{m_mutex};
    Clock::time_point next_due = Clock::now();
    for (;;) {
        m_wake.wait(lock,
                    [this] { return m_stop_requested || (m_running && has_step_space_locked()); });
        if (m_stop_requested) {
            return;
        }

        if (m_steps_per_second <= 0.0) {
            // Unthrottled: a batch per lock hold, then let the main thread in.
            for (int i = 0; i < kUnthrottledBatch && m_running && has_step_space_locked(); ++i) {
                m_simulation->advance();
                pump_events_locked();
            }
            lock.unlock();
            std::this_thread::yield();
            lock.lock();
            continue;
        }

        // Paced. Wall-clock time only decides *when* steps happen, never what they do, so
        // determinism is unaffected.
        const Clock::time_point now = Clock::now();
        if (m_pacing_reset || now - next_due > kMaxPacingLag) {
            next_due = now;
            m_pacing_reset = false;
        }
        if (now < next_due) {
            m_wake.wait_until(lock, next_due);
            continue; // re-check stop/pause/speed changes
        }
        m_simulation->advance();
        pump_events_locked();
        next_due += std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>{1.0 / m_steps_per_second});
    }
}

void Simulation::_exit_tree() {
    stop_worker();
}

// --- Events ------------------------------------------------------------------------------------

godot::PackedInt64Array Simulation::drain_events(int64_t max_events) {
    const std::size_t available = m_carry.size() + m_ring->size();
    const std::size_t wanted =
        max_events < 0 ? available : std::min(available, static_cast<std::size_t>(max_events));

    const std::size_t from_carry = std::min(wanted, m_carry.size());
    m_drain_scratch.assign(m_carry.begin(),
                           m_carry.begin() + static_cast<std::ptrdiff_t>(from_carry));
    m_carry.erase(m_carry.begin(), m_carry.begin() + static_cast<std::ptrdiff_t>(from_carry));

    std::size_t popped = 0;
    if (wanted > from_carry) {
        m_drain_scratch.resize(wanted);
        popped = m_ring->pop(std::span{m_drain_scratch}.subspan(from_carry));
        m_drain_scratch.resize(from_carry + popped);
    }
    if (popped > 0) {
        wake_worker(); // ring space was freed
    }

    godot::PackedInt64Array rows;
    rows.resize(static_cast<int64_t>(m_drain_scratch.size()) * EVENT_STRIDE);
    const std::span<int64_t> out{rows.ptrw(), static_cast<std::size_t>(rows.size())};
    for (std::size_t i = 0; i < m_drain_scratch.size(); ++i) {
        write_event(out.subspan(i * EVENT_STRIDE, EVENT_STRIDE), m_drain_scratch[i]);
    }
    return rows;
}

int64_t Simulation::get_pending_event_count() const {
    return static_cast<int64_t>(m_carry.size() + m_ring->size());
}

// --- Queries -----------------------------------------------------------------------------------

int64_t Simulation::get_last_sequence() const {
    const std::lock_guard lock{m_mutex};
    return static_cast<int64_t>(m_simulation->last_sequence().value());
}

int64_t Simulation::get_current_tick() const {
    const std::lock_guard lock{m_mutex};
    return m_simulation->engine().current_tick().value();
}

bool Simulation::has_last_price() const {
    const std::lock_guard lock{m_mutex};
    return m_simulation->engine().last_price().has_value();
}

int64_t Simulation::get_last_price() const {
    const std::lock_guard lock{m_mutex};
    return m_simulation->engine().last_price().value_or(PriceTicks{}).value();
}

bool Simulation::has_best_bid() const {
    const std::lock_guard lock{m_mutex};
    return m_simulation->engine().best_bid().has_value();
}

int64_t Simulation::get_best_bid() const {
    const std::lock_guard lock{m_mutex};
    return m_simulation->engine().best_bid().value_or(PriceTicks{}).value();
}

bool Simulation::has_best_ask() const {
    const std::lock_guard lock{m_mutex};
    return m_simulation->engine().best_ask().has_value();
}

int64_t Simulation::get_best_ask() const {
    const std::lock_guard lock{m_mutex};
    return m_simulation->engine().best_ask().value_or(PriceTicks{}).value();
}

int64_t Simulation::get_total_volume() const {
    const std::lock_guard lock{m_mutex};
    return m_simulation->engine().total_volume().value();
}

int64_t Simulation::get_tick_volume() const {
    const std::lock_guard lock{m_mutex};
    return m_simulation->engine().tick_volume().value();
}

int64_t Simulation::get_trade_count() const {
    const std::lock_guard lock{m_mutex};
    return static_cast<int64_t>(m_simulation->engine().trade_count());
}

godot::PackedInt64Array Simulation::get_depth(Side side, int64_t max_levels) const {
    godot::PackedInt64Array rows;
    ERR_FAIL_COND_V_MSG(side > SIDE_SELL, rows, "side must be a Simulation.Side value.");
    ERR_FAIL_COND_V_MSG(max_levels < 0 || max_levels > kMaxDepthLevels, rows,
                        "max_levels must be in [0, 4096].");
    std::vector<DepthLevel> levels(static_cast<std::size_t>(max_levels));
    std::size_t count = 0;
    {
        const std::lock_guard lock{m_mutex};
        count = m_simulation->engine().depth(static_cast<stocksim::Side>(side), levels);
    }
    rows.resize(static_cast<int64_t>(count) * DEPTH_STRIDE);
    const std::span<int64_t> out{rows.ptrw(), static_cast<std::size_t>(rows.size())};
    for (std::size_t i = 0; i < count; ++i) {
        const std::span<int64_t> row = out.subspan(i * DEPTH_STRIDE, DEPTH_STRIDE);
        row[DEPTH_FIELD_PRICE] = levels[i].price.value();
        row[DEPTH_FIELD_QTY] = levels[i].qty.value();
        row[DEPTH_FIELD_ORDER_COUNT] = levels[i].order_count;
    }
    return rows;
}

godot::PackedInt64Array Simulation::get_order(int64_t order_id) const {
    godot::PackedInt64Array row;
    if (order_id <= 0) {
        return row;
    }
    std::optional<OrderView> view;
    {
        const std::lock_guard lock{m_mutex};
        view = m_simulation->engine().find_order(OrderId{static_cast<std::uint64_t>(order_id)});
    }
    if (!view) {
        return row;
    }
    row.resize(ORDER_STRIDE);
    const std::span<int64_t> out{row.ptrw(), static_cast<std::size_t>(ORDER_STRIDE)};
    out[ORDER_FIELD_ID] = static_cast<int64_t>(view->id.value());
    out[ORDER_FIELD_ACCOUNT] = view->account.value();
    out[ORDER_FIELD_SIDE] = static_cast<int64_t>(view->side);
    out[ORDER_FIELD_TYPE] = static_cast<int64_t>(view->type);
    out[ORDER_FIELD_STATUS] = static_cast<int64_t>(view->status);
    out[ORDER_FIELD_LIMIT_PRICE] = view->limit_price.value();
    out[ORDER_FIELD_STOP_PRICE] = view->stop_price.value();
    out[ORDER_FIELD_REMAINING] = view->remaining.value();
    out[ORDER_FIELD_FILLED] = view->filled.value();
    return row;
}

godot::PackedInt64Array Simulation::get_account(int64_t account) const {
    godot::PackedInt64Array row;
    if (account < 0 || account > static_cast<int64_t>(UINT32_MAX)) {
        return row;
    }
    std::optional<AccountView> view;
    {
        const std::lock_guard lock{m_mutex};
        view = m_simulation->engine().account(AccountId{static_cast<std::uint32_t>(account)});
    }
    if (!view) {
        return row;
    }
    row.resize(ACCOUNT_STRIDE);
    const std::span<int64_t> out{row.ptrw(), static_cast<std::size_t>(ACCOUNT_STRIDE)};
    out[ACCOUNT_FIELD_ID] = view->id.value();
    out[ACCOUNT_FIELD_CASH] = view->cash.value();
    out[ACCOUNT_FIELD_POSITION] = view->position.value();
    out[ACCOUNT_FIELD_LIMITED] = view->limited ? 1 : 0;
    out[ACCOUNT_FIELD_LEVERAGE_PERCENT] = view->leverage_percent;
    out[ACCOUNT_FIELD_COMMITTED_BUY] = view->committed_buy.value();
    out[ACCOUNT_FIELD_COMMITTED_SELL] = view->committed_sell.value();
    return row;
}

// --- Persistence -------------------------------------------------------------------------------

godot::PackedByteArray Simulation::save_state() const {
    std::vector<std::byte> bytes;
    {
        const std::lock_guard lock{m_mutex};
        bytes = m_simulation->save();
    }
    godot::PackedByteArray data;
    data.resize(static_cast<int64_t>(bytes.size()));
    if (!bytes.empty()) {
        std::memcpy(data.ptrw(), bytes.data(), bytes.size());
    }
    return data;
}

godot::Error Simulation::load_state(const godot::PackedByteArray& data) {
    const std::span<const std::uint8_t> raw{data.ptr(), static_cast<std::size_t>(data.size())};
    Result<stocksim::Simulation> loaded =
        stocksim::Simulation::load(std::as_bytes(raw), m_settings.simulation);
    if (!loaded) {
        const godot::Error code = loaded.error().code == ErrorCode::kCorruptState
                                      ? godot::ERR_INVALID_DATA
                                      : godot::ERR_FILE_UNRECOGNIZED;
        ERR_FAIL_V_MSG(code, "Cannot load simulation state: " + to_godot(loaded.error().message));
    }
    replace(m_settings, std::move(loaded).value());
    return godot::OK;
}

// --- Static helpers ----------------------------------------------------------------------------

String Simulation::reason_text(int64_t reason) {
    ERR_FAIL_COND_V_MSG(reason < REASON_NONE || reason > REASON_INSUFFICIENT_SHARES, String(),
                        "reason must be a Simulation.Reason value.");
    return to_godot(stocksim::reason_text(static_cast<stocksim::Reason>(reason)));
}

String Simulation::event_type_name(int64_t type) {
    ERR_FAIL_COND_V_MSG(type < EVENT_ORDER_PLACED || type > EVENT_PRICE_CHANGE, String(),
                        "type must be a Simulation.EventType value.");
    return to_godot(stocksim::to_string(static_cast<stocksim::EventType>(type)));
}

int64_t Simulation::get_event_schema_version() {
    return kEventSchemaVersion;
}

// --- Bindings ----------------------------------------------------------------------------------

void Simulation::_bind_methods() {
    using godot::ClassDB;
    using godot::D_METHOD;

    ClassDB::bind_method(D_METHOD("load_scenario", "path"), &Simulation::load_scenario);
    ClassDB::bind_method(D_METHOD("configure", "scenario"), &Simulation::configure);
    ClassDB::bind_method(D_METHOD("get_scenario_name"), &Simulation::get_scenario_name);

    ClassDB::bind_method(
        D_METHOD("submit_order", "account", "side", "type", "qty", "limit_price", "stop_price"),
        &Simulation::submit_order, DEFVAL(0), DEFVAL(0));
    ClassDB::bind_method(D_METHOD("cancel_order", "account", "order_id"),
                         &Simulation::cancel_order);
    ClassDB::bind_method(
        D_METHOD("modify_order", "account", "order_id", "new_limit_price", "new_qty"),
        &Simulation::modify_order);

    ClassDB::bind_method(D_METHOD("step_event"), &Simulation::step_event);
    ClassDB::bind_method(D_METHOD("step_tick"), &Simulation::step_tick);
    ClassDB::bind_method(D_METHOD("step_back"), &Simulation::step_back);
    ClassDB::bind_method(D_METHOD("can_step_back"), &Simulation::can_step_back);
    ClassDB::bind_method(D_METHOD("has_redo"), &Simulation::has_redo);

    ClassDB::bind_method(D_METHOD("run", "steps_per_second"), &Simulation::run, DEFVAL(0.0));
    ClassDB::bind_method(D_METHOD("pause"), &Simulation::pause);
    ClassDB::bind_method(D_METHOD("is_running"), &Simulation::is_running);
    ClassDB::bind_method(D_METHOD("get_steps_per_second"), &Simulation::get_steps_per_second);

    ClassDB::bind_method(D_METHOD("drain_events", "max_events"), &Simulation::drain_events,
                         DEFVAL(-1));
    ClassDB::bind_method(D_METHOD("get_pending_event_count"), &Simulation::get_pending_event_count);

    ClassDB::bind_method(D_METHOD("get_last_sequence"), &Simulation::get_last_sequence);
    ClassDB::bind_method(D_METHOD("get_current_tick"), &Simulation::get_current_tick);
    ClassDB::bind_method(D_METHOD("has_last_price"), &Simulation::has_last_price);
    ClassDB::bind_method(D_METHOD("get_last_price"), &Simulation::get_last_price);
    ClassDB::bind_method(D_METHOD("has_best_bid"), &Simulation::has_best_bid);
    ClassDB::bind_method(D_METHOD("get_best_bid"), &Simulation::get_best_bid);
    ClassDB::bind_method(D_METHOD("has_best_ask"), &Simulation::has_best_ask);
    ClassDB::bind_method(D_METHOD("get_best_ask"), &Simulation::get_best_ask);
    ClassDB::bind_method(D_METHOD("get_total_volume"), &Simulation::get_total_volume);
    ClassDB::bind_method(D_METHOD("get_tick_volume"), &Simulation::get_tick_volume);
    ClassDB::bind_method(D_METHOD("get_trade_count"), &Simulation::get_trade_count);
    ClassDB::bind_method(D_METHOD("get_depth", "side", "max_levels"), &Simulation::get_depth);
    ClassDB::bind_method(D_METHOD("get_order", "order_id"), &Simulation::get_order);
    ClassDB::bind_method(D_METHOD("get_account", "account"), &Simulation::get_account);

    ClassDB::bind_method(D_METHOD("save_state"), &Simulation::save_state);
    ClassDB::bind_method(D_METHOD("load_state", "data"), &Simulation::load_state);

    ClassDB::bind_static_method(get_class_static(), D_METHOD("reason_text", "reason"),
                                &Simulation::reason_text);
    ClassDB::bind_static_method(get_class_static(), D_METHOD("event_type_name", "type"),
                                &Simulation::event_type_name);
    ClassDB::bind_static_method(get_class_static(), D_METHOD("get_event_schema_version"),
                                &Simulation::get_event_schema_version);

    ADD_SIGNAL(
        godot::MethodInfo("rewound", godot::PropertyInfo(godot::Variant::INT, "last_sequence")));

    BIND_ENUM_CONSTANT(SIDE_BUY);
    BIND_ENUM_CONSTANT(SIDE_SELL);

    BIND_ENUM_CONSTANT(ORDER_TYPE_MARKET);
    BIND_ENUM_CONSTANT(ORDER_TYPE_LIMIT);
    BIND_ENUM_CONSTANT(ORDER_TYPE_STOP);
    BIND_ENUM_CONSTANT(ORDER_TYPE_STOP_LIMIT);

    BIND_ENUM_CONSTANT(ORDER_STATUS_PENDING_STOP);
    BIND_ENUM_CONSTANT(ORDER_STATUS_TRIGGERED);
    BIND_ENUM_CONSTANT(ORDER_STATUS_MATCHING);
    BIND_ENUM_CONSTANT(ORDER_STATUS_RESTING);

    BIND_ENUM_CONSTANT(EVENT_ORDER_PLACED);
    BIND_ENUM_CONSTANT(EVENT_ORDER_REJECTED);
    BIND_ENUM_CONSTANT(EVENT_ORDER_RESTED);
    BIND_ENUM_CONSTANT(EVENT_ORDER_CANCELLED);
    BIND_ENUM_CONSTANT(EVENT_ORDER_MODIFIED);
    BIND_ENUM_CONSTANT(EVENT_COMMAND_REJECTED);
    BIND_ENUM_CONSTANT(EVENT_STOP_TRIGGERED);
    BIND_ENUM_CONSTANT(EVENT_MATCH);
    BIND_ENUM_CONSTANT(EVENT_FILL);
    BIND_ENUM_CONSTANT(EVENT_PRICE_CHANGE);

    BIND_ENUM_CONSTANT(REASON_NONE);
    BIND_ENUM_CONSTANT(REASON_ORDER_ACCEPTED);
    BIND_ENUM_CONSTANT(REASON_INVALID_QUANTITY);
    BIND_ENUM_CONSTANT(REASON_INVALID_LIMIT_PRICE);
    BIND_ENUM_CONSTANT(REASON_INVALID_STOP_PRICE);
    BIND_ENUM_CONSTANT(REASON_RESTING_ON_BOOK);
    BIND_ENUM_CONSTANT(REASON_CANCELLED_BY_OWNER);
    BIND_ENUM_CONSTANT(REASON_NO_LIQUIDITY);
    BIND_ENUM_CONSTANT(REASON_UNKNOWN_ORDER);
    BIND_ENUM_CONSTANT(REASON_NOT_ORDER_OWNER);
    BIND_ENUM_CONSTANT(REASON_NOT_MODIFIABLE);
    BIND_ENUM_CONSTANT(REASON_MODIFIED_KEPT_PRIORITY);
    BIND_ENUM_CONSTANT(REASON_MODIFIED_LOST_PRIORITY);
    BIND_ENUM_CONSTANT(REASON_STOP_PRICE_REACHED);
    BIND_ENUM_CONSTANT(REASON_TRADE);
    BIND_ENUM_CONSTANT(REASON_TAKER_FILL);
    BIND_ENUM_CONSTANT(REASON_MAKER_FILL);
    BIND_ENUM_CONSTANT(REASON_LAST_TRADE_PRICE);
    BIND_ENUM_CONSTANT(REASON_INSUFFICIENT_FUNDS);
    BIND_ENUM_CONSTANT(REASON_INSUFFICIENT_SHARES);

    BIND_ENUM_CONSTANT(EVENT_FIELD_SEQ);
    BIND_ENUM_CONSTANT(EVENT_FIELD_TICK);
    BIND_ENUM_CONSTANT(EVENT_FIELD_TYPE);
    BIND_ENUM_CONSTANT(EVENT_FIELD_REASON);
    BIND_ENUM_CONSTANT(EVENT_FIELD_INTENT);
    BIND_ENUM_CONSTANT(EVENT_FIELD_SIDE);
    BIND_ENUM_CONSTANT(EVENT_FIELD_ORDER_TYPE);
    BIND_ENUM_CONSTANT(EVENT_FIELD_ORDER_ID);
    BIND_ENUM_CONSTANT(EVENT_FIELD_COUNTER_ORDER_ID);
    BIND_ENUM_CONSTANT(EVENT_FIELD_ACCOUNT);
    BIND_ENUM_CONSTANT(EVENT_FIELD_COUNTER_ACCOUNT);
    BIND_ENUM_CONSTANT(EVENT_FIELD_PRICE);
    BIND_ENUM_CONSTANT(EVENT_FIELD_AUX_PRICE);
    BIND_ENUM_CONSTANT(EVENT_FIELD_QTY);
    BIND_ENUM_CONSTANT(EVENT_FIELD_REMAINING);
    BIND_ENUM_CONSTANT(EVENT_STRIDE);

    BIND_ENUM_CONSTANT(ORDER_FIELD_ID);
    BIND_ENUM_CONSTANT(ORDER_FIELD_ACCOUNT);
    BIND_ENUM_CONSTANT(ORDER_FIELD_SIDE);
    BIND_ENUM_CONSTANT(ORDER_FIELD_TYPE);
    BIND_ENUM_CONSTANT(ORDER_FIELD_STATUS);
    BIND_ENUM_CONSTANT(ORDER_FIELD_LIMIT_PRICE);
    BIND_ENUM_CONSTANT(ORDER_FIELD_STOP_PRICE);
    BIND_ENUM_CONSTANT(ORDER_FIELD_REMAINING);
    BIND_ENUM_CONSTANT(ORDER_FIELD_FILLED);
    BIND_ENUM_CONSTANT(ORDER_STRIDE);

    BIND_ENUM_CONSTANT(DEPTH_FIELD_PRICE);
    BIND_ENUM_CONSTANT(DEPTH_FIELD_QTY);
    BIND_ENUM_CONSTANT(DEPTH_FIELD_ORDER_COUNT);
    BIND_ENUM_CONSTANT(DEPTH_STRIDE);

    BIND_ENUM_CONSTANT(ACCOUNT_FIELD_ID);
    BIND_ENUM_CONSTANT(ACCOUNT_FIELD_CASH);
    BIND_ENUM_CONSTANT(ACCOUNT_FIELD_POSITION);
    BIND_ENUM_CONSTANT(ACCOUNT_FIELD_LIMITED);
    BIND_ENUM_CONSTANT(ACCOUNT_FIELD_LEVERAGE_PERCENT);
    BIND_ENUM_CONSTANT(ACCOUNT_FIELD_COMMITTED_BUY);
    BIND_ENUM_CONSTANT(ACCOUNT_FIELD_COMMITTED_SELL);
    BIND_ENUM_CONSTANT(ACCOUNT_STRIDE);
}

} // namespace stocksim::gdext
