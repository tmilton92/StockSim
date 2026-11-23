extends GdUnitTestSuite
## SimBridge autoload: draining, logging, rewinds and resets.

var _received: Array[SimEvent] = []
var _rewound_to: int = -1
var _resets: int = 0


func before_test() -> void:
	# Start every test from a fresh simulation with an empty log.
	assert_int(SimBridge.load_state(_empty_state())).is_equal(OK)
	_received.clear()
	_rewound_to = -1
	_resets = 0
	SimBridge.events_received.connect(_on_events_received)
	SimBridge.rewound.connect(_on_rewound)
	SimBridge.simulation_reset.connect(_on_reset)


func after_test() -> void:
	SimBridge.events_received.disconnect(_on_events_received)
	SimBridge.rewound.disconnect(_on_rewound)
	SimBridge.simulation_reset.disconnect(_on_reset)


func test_poll_logs_and_broadcasts_events() -> void:
	var simulation: Simulation = SimBridge.simulation
	simulation.submit_order(1, Simulation.SIDE_SELL, Simulation.ORDER_TYPE_LIMIT, 5, 10000)
	simulation.submit_order(2, Simulation.SIDE_BUY, Simulation.ORDER_TYPE_MARKET, 5)
	simulation.step_tick()
	SimBridge.poll()
	assert_int(_received.size()).is_equal(7)
	assert_int(SimBridge.event_log.size()).is_equal(7)
	assert_int(SimBridge.event_log.trades().size()).is_equal(1)
	SimBridge.poll()  # nothing new: no second broadcast
	assert_int(_received.size()).is_equal(7)


func test_step_back_truncates_the_log() -> void:
	var simulation: Simulation = SimBridge.simulation
	simulation.submit_order(1, Simulation.SIDE_SELL, Simulation.ORDER_TYPE_LIMIT, 5, 10000)
	simulation.submit_order(2, Simulation.SIDE_BUY, Simulation.ORDER_TYPE_MARKET, 5)
	simulation.step_tick()
	SimBridge.poll()
	simulation.step_back()  # undo the tick advance
	simulation.step_back()  # undo the trade
	assert_int(_rewound_to).is_equal(simulation.get_last_sequence())
	assert_int(SimBridge.event_log.last_sequence()).is_equal(_rewound_to)
	assert_int(SimBridge.event_log.trades().size()).is_equal(0)


func test_load_state_resets_the_log() -> void:
	var simulation: Simulation = SimBridge.simulation
	simulation.submit_order(1, Simulation.SIDE_SELL, Simulation.ORDER_TYPE_LIMIT, 5, 10000)
	simulation.step_tick()
	SimBridge.poll()
	assert_bool(SimBridge.event_log.is_empty()).is_false()
	assert_int(SimBridge.load_state(_empty_state())).is_equal(OK)
	assert_bool(SimBridge.event_log.is_empty()).is_true()
	assert_int(_resets).is_equal(1)


func _empty_state() -> PackedByteArray:
	var fresh: Simulation = auto_free(Simulation.new())
	return fresh.save_state()


func _on_events_received(events: Array[SimEvent]) -> void:
	_received.append_array(events)


func _on_rewound(last_sequence: int) -> void:
	_rewound_to = last_sequence


func _on_reset() -> void:
	_resets += 1
