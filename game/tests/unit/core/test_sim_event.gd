extends GdUnitTestSuite
## SimEvent: decoding drain_events() rows, using real events from a private simulation.

var _simulation: Simulation


func before_test() -> void:
	_simulation = auto_free(Simulation.new())


func test_decodes_a_trade_exactly() -> void:
	var maker: int = _simulation.submit_order(1, Simulation.SIDE_SELL, Simulation.ORDER_TYPE_LIMIT,
			5, 10100)
	var taker: int = _simulation.submit_order(2, Simulation.SIDE_BUY, Simulation.ORDER_TYPE_MARKET,
			3)
	_simulation.step_tick()
	var rows: PackedInt64Array = _simulation.drain_events()
	var events: Array[SimEvent] = SimEvent.from_rows(rows)

	assert_int(events.size() * Simulation.EVENT_STRIDE).is_equal(rows.size())
	var trade: SimEvent = events[3]
	assert_bool(trade.is_trade()).is_true()
	assert_int(trade.seq).is_equal(4)
	assert_int(trade.tick).is_equal(0)
	assert_int(trade.order_id).is_equal(taker)
	assert_int(trade.counter_order_id).is_equal(maker)
	assert_int(trade.account).is_equal(2)
	assert_int(trade.counter_account).is_equal(1)
	assert_int(trade.side).is_equal(Simulation.SIDE_BUY)
	assert_int(trade.price).is_equal(10100)
	assert_int(trade.qty).is_equal(3)
	assert_int(trade.reason).is_equal(Simulation.REASON_TRADE)


func test_every_field_round_trips() -> void:
	var row: PackedInt64Array = PackedInt64Array()
	row.resize(Simulation.EVENT_STRIDE)
	for i: int in range(Simulation.EVENT_STRIDE):
		row[i] = 100 + i
	row[Simulation.EVENT_FIELD_TYPE] = Simulation.EVENT_STOP_TRIGGERED
	row[Simulation.EVENT_FIELD_REASON] = Simulation.REASON_STOP_PRICE_REACHED
	row[Simulation.EVENT_FIELD_SIDE] = Simulation.SIDE_SELL
	row[Simulation.EVENT_FIELD_ORDER_TYPE] = Simulation.ORDER_TYPE_STOP
	var event: SimEvent = SimEvent.from_row(row, 0)
	assert_int(event.seq).is_equal(100 + Simulation.EVENT_FIELD_SEQ)
	assert_int(event.tick).is_equal(100 + Simulation.EVENT_FIELD_TICK)
	assert_int(event.type).is_equal(Simulation.EVENT_STOP_TRIGGERED)
	assert_int(event.reason).is_equal(Simulation.REASON_STOP_PRICE_REACHED)
	assert_int(event.intent).is_equal(100 + Simulation.EVENT_FIELD_INTENT)
	assert_int(event.side).is_equal(Simulation.SIDE_SELL)
	assert_int(event.order_type).is_equal(Simulation.ORDER_TYPE_STOP)
	assert_int(event.order_id).is_equal(100 + Simulation.EVENT_FIELD_ORDER_ID)
	assert_int(event.counter_order_id).is_equal(100 + Simulation.EVENT_FIELD_COUNTER_ORDER_ID)
	assert_int(event.account).is_equal(100 + Simulation.EVENT_FIELD_ACCOUNT)
	assert_int(event.counter_account).is_equal(100 + Simulation.EVENT_FIELD_COUNTER_ACCOUNT)
	assert_int(event.price).is_equal(100 + Simulation.EVENT_FIELD_PRICE)
	assert_int(event.aux_price).is_equal(100 + Simulation.EVENT_FIELD_AUX_PRICE)
	assert_int(event.qty).is_equal(100 + Simulation.EVENT_FIELD_QTY)
	assert_int(event.remaining).is_equal(100 + Simulation.EVENT_FIELD_REMAINING)


func test_involvement_checks_both_sides() -> void:
	var event: SimEvent = SimEvent.new()
	event.order_id = 7
	event.counter_order_id = 9
	event.account = 3
	event.counter_account = 4
	assert_bool(event.involves_order(7)).is_true()
	assert_bool(event.involves_order(9)).is_true()
	assert_bool(event.involves_order(8)).is_false()
	assert_bool(event.involves_order(0)).is_false()
	assert_bool(event.involves_account(4)).is_true()
	event.counter_order_id = 0
	assert_bool(event.involves_account(4)).is_false()  # no counter order, so no counter account
