extends GdUnitTestSuite
## EventLog: append, truncate after a rewind, and filtering.


func _event(seq: int, type: Simulation.EventType, order_id: int, account: int,
		counter_order_id: int = 0, counter_account: int = 0) -> SimEvent:
	var event: SimEvent = SimEvent.new()
	event.seq = seq
	event.type = type
	event.order_id = order_id
	event.account = account
	event.counter_order_id = counter_order_id
	event.counter_account = counter_account
	return event


func _sample() -> EventLog:
	var event_log: EventLog = EventLog.new()
	var batch: Array[SimEvent] = [
		_event(1, Simulation.EVENT_ORDER_PLACED, 1, 10),
		_event(2, Simulation.EVENT_ORDER_RESTED, 1, 10),
		_event(3, Simulation.EVENT_ORDER_PLACED, 2, 20),
		_event(4, Simulation.EVENT_MATCH, 2, 20, 1, 10),
		_event(5, Simulation.EVENT_ORDER_PLACED, 3, 10),
	]
	event_log.append(batch)
	return event_log


func test_append_keeps_order_and_last_sequence() -> void:
	var event_log: EventLog = _sample()
	assert_int(event_log.size()).is_equal(5)
	assert_int(event_log.last_sequence()).is_equal(5)
	assert_int(event_log.last(2)[0].seq).is_equal(4)
	assert_int(event_log.last(100).size()).is_equal(5)


func test_truncate_after_removes_the_undone_future() -> void:
	var event_log: EventLog = _sample()
	event_log.truncate_after(3)
	assert_int(event_log.size()).is_equal(3)
	assert_int(event_log.last_sequence()).is_equal(3)
	event_log.truncate_after(10)  # nothing newer: no change
	assert_int(event_log.size()).is_equal(3)
	event_log.truncate_after(0)
	assert_bool(event_log.is_empty()).is_true()
	assert_int(event_log.last_sequence()).is_equal(0)


func test_filters() -> void:
	var event_log: EventLog = _sample()
	assert_int(event_log.trades().size()).is_equal(1)
	# Placed, rested, and the maker side of the trade.
	assert_int(event_log.for_order(1).size()).is_equal(3)
	assert_int(event_log.for_account(10).size()).is_equal(4)
	var placed_by_10: Array[int] = event_log.orders_of_account(10)
	assert_bool(placed_by_10 == [1, 3]).is_true()
