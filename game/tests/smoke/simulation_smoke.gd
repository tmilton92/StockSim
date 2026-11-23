extends SceneTree
## Headless smoke test for the Simulation GDExtension node (TODO section 3).
##
## Run by tools/godot_check.py:
##   godot --headless --path game --script res://tests/smoke/simulation_smoke.gd -- SCENARIO
## Before each call that is expected to log an engine error it prints "EXPECT_ERROR: <text>",
## so the checker can tell expected errors from real ones. Exits with the number of failures.

const TIMEOUT_MSEC: int = 10000

var _checks: int = 0
var _failures: int = 0
var _rewound_to: int = -1


func _initialize() -> void:
	var args: PackedStringArray = OS.get_cmdline_user_args()
	if args.size() != 1:
		print("FAIL: expected one argument: the sandbox scenario path")
		quit(1)
		return
	_test_defaults()
	_test_scenarios(args[0])
	_test_trading()
	_test_time_travel()
	_test_persistence()
	_test_worker()
	_test_backpressure()
	print("SMOKE: %d checks, %d failures" % [_checks, _failures])
	quit(_failures)


func _test_defaults() -> void:
	var sim: Simulation = _make()
	_check(sim.get_scenario_name() == "Default", "default scenario name")
	_check(sim.get_current_tick() == 0, "starts at tick 0")
	_check(not sim.has_last_price(), "no last price before trading")
	_check(not sim.can_step_back(), "nothing to undo at start")
	_check(sim.get_order(1).is_empty(), "no orders at start")
	_check(sim.drain_events().is_empty(), "no events at start")
	_check(sim.get_account(1).is_empty(), "no accounts at start")
	_check(Simulation.get_event_schema_version() == 2, "event schema version")
	_check(Simulation.event_type_name(Simulation.EVENT_MATCH) == "Match", "event type name")
	_check(not Simulation.reason_text(Simulation.REASON_TRADE).is_empty(), "reason text")
	_check(Simulation.EVENT_STRIDE == 15, "event stride")
	sim.free()


func _test_scenarios(sandbox_path: String) -> void:
	var sim: Simulation = _make()
	_check(sim.load_scenario(sandbox_path) == OK, "sandbox scenario loads")
	_check(sim.get_scenario_name() == "Sandbox", "scenario name applied")

	var valid: Dictionary = {
		"format": "stocksim-scenario",
		"format_version": 1,
		"simulation": {"snapshot_interval_ticks": 8},
		"runtime": {"event_buffer_capacity": 128},
	}
	_check(sim.configure(valid) == OK, "configure accepts integer values")

	var unknown_key: Dictionary = valid.duplicate(true)
	unknown_key["bogus"] = 1
	_expect_error("unknown key \"bogus\"")
	_check(sim.configure(unknown_key) == ERR_INVALID_DATA, "unknown key rejected")

	var future: Dictionary = valid.duplicate(true)
	future["format_version"] = 3
	_expect_error("Unsupported scenario format_version 3")
	_check(sim.configure(future) == ERR_FILE_UNRECOGNIZED, "future format rejected")

	var lesson_v2: Dictionary = valid.duplicate(true)
	lesson_v2["format_version"] = 2
	lesson_v2["lesson"] = {"title": "parsed by the game layer, not the extension"}
	_check(sim.configure(lesson_v2) == OK, "version 2 accepts a lesson object")

	var lesson_v1: Dictionary = lesson_v2.duplicate(true)
	lesson_v1["format_version"] = 1
	_expect_error("unknown key \"lesson\"")
	_check(sim.configure(lesson_v1) == ERR_INVALID_DATA, "version 1 has no lesson")

	var lesson_not_object: Dictionary = lesson_v2.duplicate(true)
	lesson_not_object["lesson"] = "chapter one"
	_expect_error("scenario.lesson must be an object")
	_check(sim.configure(lesson_not_object) == ERR_INVALID_DATA, "lesson must be an object")

	var fractional: Dictionary = valid.duplicate(true)
	fractional["simulation"] = {"snapshot_interval_ticks": 2.5}
	_expect_error("snapshot_interval_ticks must be an integer")
	_check(sim.configure(fractional) == ERR_INVALID_DATA, "fractional value rejected")

	var too_small: Dictionary = valid.duplicate(true)
	too_small["runtime"] = {"event_buffer_capacity": 8}
	_expect_error("event_buffer_capacity must be an integer between 64")
	_check(sim.configure(too_small) == ERR_INVALID_DATA, "buffer below minimum rejected")

	var bad_json_path: String = "user://smoke_bad_scenario.json"
	var file: FileAccess = FileAccess.open(bad_json_path, FileAccess.WRITE)
	file.store_string("{ \"format\": ")
	file.close()
	_expect_error("Invalid JSON in scenario")
	_check(sim.load_scenario(bad_json_path) == ERR_PARSE_ERROR, "malformed JSON rejected")
	DirAccess.remove_absolute(ProjectSettings.globalize_path(bad_json_path))

	_expect_error("Cannot read scenario file")
	_check(sim.load_scenario("user://does_not_exist.json") != OK, "missing file rejected")
	sim.free()


func _test_trading() -> void:
	var sim: Simulation = _make()
	var ask: int = sim.submit_order(1, Simulation.SIDE_SELL, Simulation.ORDER_TYPE_LIMIT, 5, 100)
	var taker: int = sim.submit_order(2, Simulation.SIDE_BUY, Simulation.ORDER_TYPE_MARKET, 3)
	_check(ask == 1 and taker == 2, "order ids assigned in submission order")
	_check(sim.step_tick(), "tick completes")

	var rows: PackedInt64Array = sim.drain_events()
	var expected: Array[int] = [
		Simulation.EVENT_ORDER_PLACED, Simulation.EVENT_ORDER_RESTED,
		Simulation.EVENT_ORDER_PLACED, Simulation.EVENT_MATCH, Simulation.EVENT_FILL,
		Simulation.EVENT_FILL, Simulation.EVENT_PRICE_CHANGE,
	]
	_check(_types(rows) == expected, "event sequence for a resting ask hit by a market buy")
	var match_row: int = 3 * Simulation.EVENT_STRIDE
	_check(rows[match_row + Simulation.EVENT_FIELD_PRICE] == 100, "trade at the maker's price")
	_check(rows[match_row + Simulation.EVENT_FIELD_QTY] == 3, "trade quantity")
	_check(rows[match_row + Simulation.EVENT_FIELD_ORDER_ID] == taker, "taker is the primary order")
	_check(rows[match_row + Simulation.EVENT_FIELD_COUNTER_ORDER_ID] == ask, "maker is the counter")
	_check(_sequences_contiguous(rows, 1), "sequence numbers start at 1 and are contiguous")

	_check(sim.has_last_price() and sim.get_last_price() == 100, "last price")
	_check(sim.has_best_ask() and sim.get_best_ask() == 100, "best ask")
	_check(not sim.has_best_bid(), "no bids")
	_check(sim.get_total_volume() == 3 and sim.get_trade_count() == 1, "volume and trade count")
	_check(sim.get_current_tick() == 1, "tick advanced")

	var depth: PackedInt64Array = sim.get_depth(Simulation.SIDE_SELL, 10)
	_check(depth.size() == Simulation.DEPTH_STRIDE, "one ask level")
	_check(depth[Simulation.DEPTH_FIELD_PRICE] == 100 and depth[Simulation.DEPTH_FIELD_QTY] == 2,
		"ask level price and remaining quantity")

	var order: PackedInt64Array = sim.get_order(ask)
	_check(order.size() == Simulation.ORDER_STRIDE, "resting order is visible")
	_check(order[Simulation.ORDER_FIELD_STATUS] == Simulation.ORDER_STATUS_RESTING,
		"status resting")
	_check(order[Simulation.ORDER_FIELD_REMAINING] == 2, "remaining after partial fill")
	_check(order[Simulation.ORDER_FIELD_FILLED] == 3, "filled quantity")

	var buyer: PackedInt64Array = sim.get_account(2)
	_check(buyer.size() == Simulation.ACCOUNT_STRIDE, "trading account is visible")
	_check(buyer[Simulation.ACCOUNT_FIELD_POSITION] == 3, "buyer holds the shares")
	_check(buyer[Simulation.ACCOUNT_FIELD_CASH] == -300, "buyer paid price x quantity")
	_check(buyer[Simulation.ACCOUNT_FIELD_LIMITED] == 0, "unopened accounts are unlimited")
	var seller: PackedInt64Array = sim.get_account(1)
	_check(seller[Simulation.ACCOUNT_FIELD_POSITION] == -3, "seller is short the shares")
	_check(seller[Simulation.ACCOUNT_FIELD_COMMITTED_SELL] == 2, "resting ask reserves its shares")
	_check(sim.get_account(-1).is_empty(), "negative account id is unknown")

	var invalid: int = sim.submit_order(3, Simulation.SIDE_BUY, Simulation.ORDER_TYPE_LIMIT, 0, 99)
	sim.step_event()
	rows = sim.drain_events()
	_check(rows.size() == Simulation.EVENT_STRIDE, "one event for a rejected order")
	_check(rows[Simulation.EVENT_FIELD_TYPE] == Simulation.EVENT_ORDER_REJECTED, "rejected")
	_check(rows[Simulation.EVENT_FIELD_ORDER_ID] == invalid, "rejection names the order")
	_check(rows[Simulation.EVENT_FIELD_REASON] == Simulation.REASON_INVALID_QUANTITY, "reason")
	_check(not Simulation.reason_text(Simulation.REASON_INSUFFICIENT_SHARES).is_empty(),
		"reason text for the newest reason")

	_expect_error("account must be in [0, 4294967295]")
	_check(sim.submit_order(-1, Simulation.SIDE_BUY, Simulation.ORDER_TYPE_MARKET, 1) == 0,
		"negative account refused")
	sim.free()


func _test_time_travel() -> void:
	var sim: Simulation = _make()
	sim.rewound.connect(_on_rewound)
	sim.submit_order(1, Simulation.SIDE_SELL, Simulation.ORDER_TYPE_LIMIT, 5, 100)
	sim.submit_order(2, Simulation.SIDE_BUY, Simulation.ORDER_TYPE_LIMIT, 5, 100)
	sim.step_tick()
	var before: PackedInt64Array = sim.drain_events()
	var last: int = sim.get_last_sequence()
	_check(last == _types(before).size(), "last sequence matches drained events")

	# The first step back undoes the tick advance (no events); the second undoes the trade.
	_check(sim.step_back(), "step back over the tick advance")
	_check(_rewound_to == last, "undoing a tick advance removes no events")
	_check(sim.step_back(), "step back over the trade")
	_check(_rewound_to == sim.get_last_sequence(), "rewound signal carries the last sequence")
	_check(_rewound_to < last, "state is earlier than before")
	_check(sim.has_redo(), "redo available")
	_check(sim.get_pending_event_count() == 0, "no events pending after rewind")

	while sim.has_redo():
		sim.step_event()
	var redone: PackedInt64Array = sim.drain_events()
	_check(sim.get_last_sequence() == last, "redo reaches the same point")
	_check(_sequences_contiguous(redone, _rewound_to + 1), "redo re-emits the undone events")
	var tail: PackedInt64Array = before.slice(before.size() - redone.size())
	_check(redone == tail, "redone events are identical to the originals")
	sim.free()


func _test_persistence() -> void:
	var sim: Simulation = _make()
	sim.submit_order(1, Simulation.SIDE_SELL, Simulation.ORDER_TYPE_LIMIT, 7, 105)
	sim.submit_order(2, Simulation.SIDE_BUY, Simulation.ORDER_TYPE_LIMIT, 4, 101)
	sim.step_tick()
	var saved: PackedByteArray = sim.save_state()
	_check(not saved.is_empty(), "state saved")

	var copy: Simulation = _make()
	_check(copy.load_state(saved) == OK, "state loads")
	_check(copy.save_state() == saved, "loaded state is identical")
	_check(copy.get_best_bid() == 101 and copy.get_best_ask() == 105, "book restored")

	_expect_error("Cannot load simulation state")
	_check(copy.load_state(PackedByteArray([1, 2, 3])) == ERR_FILE_UNRECOGNIZED,
		"garbage state rejected")
	_check(copy.save_state() == saved, "failed load leaves the state unchanged")
	sim.free()
	copy.free()


func _test_worker() -> void:
	var sim: Simulation = _make()
	var order_count: int = 200
	for i: int in range(order_count):
		var side: Simulation.Side = Simulation.SIDE_BUY if i % 2 == 0 else Simulation.SIDE_SELL
		sim.submit_order(i % 5 + 1, side, Simulation.ORDER_TYPE_LIMIT, 10, 100 + (i % 7) - 3)

	sim.run()
	_check(sim.is_running(), "running")
	var rows: PackedInt64Array = PackedInt64Array()
	var accepted: int = 0
	var started: int = Time.get_ticks_msec()
	while accepted < order_count and Time.get_ticks_msec() - started < TIMEOUT_MSEC:
		var batch: PackedInt64Array = sim.drain_events()
		rows.append_array(batch)
		for type: int in _types(batch):
			if type == Simulation.EVENT_ORDER_PLACED:
				accepted += 1
		OS.delay_msec(1)
	sim.pause()
	rows.append_array(sim.drain_events())
	_check(not sim.is_running(), "paused")
	_check(accepted == order_count, "worker processed every order")
	_check(_sequences_contiguous(rows, 1), "no event lost or reordered across threads")
	_check(sim.get_trade_count() > 0, "crossing orders traded")

	# Paced running: an empty market only advances ticks, one per step.
	var paced: Simulation = _make()
	paced.run(50.0)
	OS.delay_msec(300)
	paced.pause()
	var ticks: int = paced.get_current_tick()
	_check(ticks >= 3 and ticks <= 40, "paced run advances about 50 steps/s (got %d)" % ticks)
	sim.free()
	paced.free()


func _test_backpressure() -> void:
	var sim: Simulation = _make()
	var config: Dictionary = {
		"format": "stocksim-scenario",
		"format_version": 1,
		"runtime": {"event_buffer_capacity": 64},
	}
	_check(sim.configure(config) == OK, "small buffer configured")
	var order_count: int = 100
	for i: int in range(order_count):
		sim.submit_order(1, Simulation.SIDE_BUY, Simulation.ORDER_TYPE_LIMIT, 1, 50 + i)

	sim.run()
	OS.delay_msec(200)
	var pending: int = sim.get_pending_event_count()
	_check(pending > 56 and pending <= 64, "worker stops at a full buffer (pending %d)" % pending)
	sim.pause()
	_expect_error("Event buffer is full")
	_check(not sim.step_event(), "manual step refused while full")

	sim.run()
	var rows: PackedInt64Array = PackedInt64Array()
	var started: int = Time.get_ticks_msec()
	var expected_events: int = order_count * 2  # placed + rested for each
	while rows.size() < expected_events * Simulation.EVENT_STRIDE \
			and Time.get_ticks_msec() - started < TIMEOUT_MSEC:
		rows.append_array(sim.drain_events(16))
		OS.delay_msec(1)
	sim.pause()
	_check(rows.size() == expected_events * Simulation.EVENT_STRIDE, "every event delivered")
	_check(_sequences_contiguous(rows, 1), "delivered in order without loss")
	sim.free()


func _make() -> Simulation:
	var sim: Simulation = Simulation.new()
	root.add_child(sim)
	return sim


func _check(condition: bool, label: String) -> void:
	_checks += 1
	if not condition:
		_failures += 1
		print("FAIL: " + label)


func _expect_error(text: String) -> void:
	print("EXPECT_ERROR: " + text)


func _types(rows: PackedInt64Array) -> Array[int]:
	var types: Array[int] = []
	for base: int in range(0, rows.size(), Simulation.EVENT_STRIDE):
		types.append(rows[base + Simulation.EVENT_FIELD_TYPE])
	return types


func _sequences_contiguous(rows: PackedInt64Array, first: int) -> bool:
	var expected: int = first
	for base: int in range(0, rows.size(), Simulation.EVENT_STRIDE):
		if rows[base + Simulation.EVENT_FIELD_SEQ] != expected:
			return false
		expected += 1
	return true


func _on_rewound(last_sequence: int) -> void:
	_rewound_to = last_sequence
