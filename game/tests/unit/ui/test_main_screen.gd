extends GdUnitTestSuite
## MainScreen end to end: seed the book, trade, step back, and select entities, driving the UI
## through the panels' own signals and checking every panel against the core.

## Concrete type: the interface declares simulate_frames() abstract, so awaiting it through
## GdUnitSceneRunner looks redundant to the analyzer even though the implementation awaits.
var _runner: GdUnitSceneRunnerImpl
var _screen: MainScreen
var _controls: SimControls
var _ticket: OrderTicket
var _tape: TradeTape
var _narration: NarrationPanel
var _chart: PriceChart
var _ladder: OrderBookLadder


func before_test() -> void:
	var fresh: Simulation = auto_free(Simulation.new())
	assert_int(SimBridge.load_state(fresh.save_state())).is_equal(OK)
	_runner = scene_runner("res://main/main_screen.tscn") as GdUnitSceneRunnerImpl
	_screen = _runner.scene() as MainScreen
	_controls = _screen.find_child("SimControls") as SimControls
	_ticket = _screen.find_child("OrderTicket") as OrderTicket
	_tape = _screen.find_child("TradeTape") as TradeTape
	_narration = _screen.find_child("NarrationPanel") as NarrationPanel
	_chart = _screen.find_child("PriceChart") as PriceChart
	_ladder = _screen.find_child("OrderBookLadder") as OrderBookLadder


func _market_order(account: int, side: Simulation.Side, qty: int) -> void:
	var request: OrderRequest = OrderRequest.new()
	request.account = account
	request.side = side
	request.type = Simulation.ORDER_TYPE_MARKET
	request.qty = qty
	_ticket.order_submitted.emit(request)


func test_seed_trade_and_step_back() -> void:
	_ticket.seed_requested.emit()
	_controls.step_tick_pressed.emit()
	await _runner.simulate_frames(30, 16)
	var book_seed: OrderLadderSeed = _screen.book_seed
	var best_bid: int = book_seed.mid_price - book_seed.spacing
	assert_float(_ladder.shown_qty(Simulation.SIDE_BUY, best_bid)).is_equal(
			float(book_seed.base_qty))
	assert_int(_tape.row_count()).is_equal(0)
	assert_int(_narration.line_count()).is_equal(SimBridge.event_log.size())

	_market_order(7, Simulation.SIDE_BUY, book_seed.base_qty + 3)  # takes 1 level and a bit
	_controls.step_tick_pressed.emit()
	await _runner.simulate_frames(30, 16)
	assert_int(_tape.row_count()).is_equal(2)
	assert_bool(_chart.series().has_trades).is_true()
	assert_int(_chart.series().last_price).is_equal(SimBridge.simulation.get_last_price())
	assert_int(_narration.line_count()).is_equal(SimBridge.event_log.size())

	# Undo the tick advance and the final trade step: the tape and chart are rebuilt.
	_controls.step_back_pressed.emit()
	_controls.step_back_pressed.emit()
	await _runner.simulate_frames(5, 16)
	assert_int(_tape.row_count()).is_equal(1)
	assert_int(_narration.line_count()).is_equal(SimBridge.event_log.size())
	assert_int(SimBridge.event_log.last_sequence()).is_equal(
			SimBridge.simulation.get_last_sequence())


func test_selecting_an_order_shows_it_in_the_inspector() -> void:
	_ticket.seed_requested.emit()
	_controls.step_tick_pressed.emit()
	await _runner.simulate_frames(5, 16)
	_screen.select(EntityRef.make(EntityRef.Kind.ORDER, 1))
	var title: Label = _screen.find_child("InspectorPanel").find_child("Title") as Label
	assert_str(title.text).is_equal("ORDER #1")
	_screen.select(EntityRef.make(EntityRef.Kind.ACCOUNT, _screen.book_seed.bid_account))
	assert_str(title.text).is_equal("ACCOUNT %d" % _screen.book_seed.bid_account)


func test_run_and_pause_drive_the_worker() -> void:
	_ticket.seed_requested.emit()
	_controls.run_toggled.emit(true)
	var started: int = Time.get_ticks_msec()
	while SimBridge.simulation.get_current_tick() < 3 and Time.get_ticks_msec() - started < 5000:
		await _runner.simulate_frames(1, 16)
	_controls.run_toggled.emit(false)
	assert_bool(SimBridge.simulation.is_running()).is_false()
	assert_int(SimBridge.simulation.get_current_tick()).is_greater_equal(3)
	await _runner.simulate_frames(5, 16)
	assert_int(_narration.line_count()).is_equal(SimBridge.event_log.size())
