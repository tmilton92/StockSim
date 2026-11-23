class_name MainScreen
extends Control
## The market screen: chart, order book ladder, trade tape, narration, inspector, transport
## controls and the debug order ticket, wired to the simulation through [code]SimBridge[/code].
##
## Calls down into the panels and listens to their signals. Holds no market state of its own:
## the order book and orders come from core queries, history from [member SimBridge.event_log].

## Price levels requested per side for the ladder.
const DEPTH_LEVELS: int = 40

## Ladder placed by the order ticket's "Seed book" button.
@export var book_seed: OrderLadderSeed = preload("res://data/default_book_seed.tres")

@onready var _controls: SimControls = %SimControls
@onready var _chart: PriceChart = %PriceChart
@onready var _ladder: OrderBookLadder = %OrderBookLadder
@onready var _tape: TradeTape = %TradeTape
@onready var _narration: NarrationPanel = %NarrationPanel
@onready var _inspector: InspectorPanel = %InspectorPanel
@onready var _ticket: OrderTicket = %OrderTicket

var _selection: EntityRef = null


func _ready() -> void:
	_controls.run_toggled.connect(_on_run_toggled)
	_controls.step_event_pressed.connect(_on_step_event_pressed)
	_controls.step_tick_pressed.connect(_on_step_tick_pressed)
	_controls.step_back_pressed.connect(_on_step_back_pressed)
	_controls.speed_changed.connect(_on_speed_changed)
	_ticket.order_submitted.connect(_on_order_submitted)
	_ticket.seed_requested.connect(_on_seed_requested)
	_ladder.price_selected.connect(_on_price_selected)
	_tape.entity_selected.connect(select)
	_narration.entity_selected.connect(select)
	_inspector.entity_selected.connect(select)
	SimBridge.events_received.connect(_on_events_received)
	SimBridge.rewound.connect(_on_history_changed)
	SimBridge.simulation_reset.connect(_on_simulation_reset)
	set_process(false)
	_rebuild()


func _process(_delta: float) -> void:
	# Only while running: the tick advances even when no events arrive.
	_update_status()


## Selects an order, account or price: highlights it and shows it in the inspector.
func select(ref: EntityRef) -> void:
	_selection = ref
	_refresh_selection()


func _on_events_received(events: Array[SimEvent]) -> void:
	var trades: Array[SimEvent] = []
	for event: SimEvent in events:
		if event.is_trade():
			trades.append(event)
	_chart.add_trades(trades)
	_tape.add_trades(trades)
	_narration.add_events(events)
	for trade: SimEvent in trades:
		_ladder.flash(trade.price)
	_refresh_market()
	_refresh_selection()
	_update_status()


func _on_history_changed(_last_sequence: int) -> void:
	_rebuild()


func _on_simulation_reset() -> void:
	_selection = null
	_rebuild()


## Redraws everything from the event log and core queries (start, step back, reset).
func _rebuild() -> void:
	var trades: Array[SimEvent] = SimBridge.event_log.trades()
	_chart.rebuild(trades)
	_tape.rebuild(trades)
	_narration.rebuild(SimBridge.event_log.all())
	_refresh_market()
	_refresh_selection()
	_update_status()


func _refresh_market() -> void:
	var simulation: Simulation = SimBridge.simulation
	_ladder.set_depth(simulation.get_depth(Simulation.SIDE_BUY, DEPTH_LEVELS),
			simulation.get_depth(Simulation.SIDE_SELL, DEPTH_LEVELS))
	if simulation.has_last_price():
		_ladder.set_last_price(simulation.get_last_price())


func _refresh_selection() -> void:
	_ladder.clear_highlight()
	_chart.clear_highlight()
	if _selection == null:
		_inspector.clear()
		return
	var simulation: Simulation = SimBridge.simulation
	var event_log: EventLog = SimBridge.event_log
	match _selection.kind:
		EntityRef.Kind.ORDER:
			var live: PackedInt64Array = simulation.get_order(_selection.id)
			if not live.is_empty() and live[Simulation.ORDER_FIELD_LIMIT_PRICE] != 0:
				_highlight_price(live[Simulation.ORDER_FIELD_LIMIT_PRICE])
			_inspector.show_order(_selection.id, live, event_log.for_order(_selection.id))
		EntityRef.Kind.ACCOUNT:
			var live_orders: Array[PackedInt64Array] = []
			for order_id: int in event_log.orders_of_account(_selection.id):
				var order: PackedInt64Array = simulation.get_order(order_id)
				if not order.is_empty():
					live_orders.append(order)
			_inspector.show_account(_selection.id, live_orders,
					event_log.for_account(_selection.id))
		EntityRef.Kind.PRICE:
			_highlight_price(_selection.id)
			_show_price_level(_selection.id)


func _show_price_level(price: int) -> void:
	var simulation: Simulation = SimBridge.simulation
	var bid: PackedInt64Array = _level_at(simulation.get_depth(Simulation.SIDE_BUY, DEPTH_LEVELS),
			price)
	var ask: PackedInt64Array = _level_at(simulation.get_depth(Simulation.SIDE_SELL, DEPTH_LEVELS),
			price)
	var trades_at_price: Array[SimEvent] = []
	for trade: SimEvent in SimBridge.event_log.trades():
		if trade.price == price:
			trades_at_price.append(trade)
	_inspector.show_price(price, bid[Simulation.DEPTH_FIELD_QTY],
			bid[Simulation.DEPTH_FIELD_ORDER_COUNT], ask[Simulation.DEPTH_FIELD_QTY],
			ask[Simulation.DEPTH_FIELD_ORDER_COUNT], trades_at_price)


func _highlight_price(price: int) -> void:
	_ladder.set_highlighted_price(price)
	_chart.set_highlighted_price(price)


func _update_status() -> void:
	var simulation: Simulation = SimBridge.simulation
	_controls.set_status(simulation.get_current_tick(), simulation.get_last_sequence(),
			simulation.is_running(), simulation.can_step_back())


func _on_run_toggled(running: bool) -> void:
	if running:
		SimBridge.simulation.run(_controls.get_steps_per_second())
	else:
		SimBridge.simulation.pause()
	set_process(running)
	_update_status()


func _on_speed_changed(steps_per_second: float) -> void:
	if SimBridge.simulation.is_running():
		SimBridge.simulation.run(steps_per_second)


func _on_step_event_pressed() -> void:
	SimBridge.simulation.step_event()
	SimBridge.poll()
	_update_status()


func _on_step_tick_pressed() -> void:
	SimBridge.simulation.step_tick()
	SimBridge.poll()
	_update_status()


func _on_step_back_pressed() -> void:
	SimBridge.simulation.step_back()  # SimBridge.rewound triggers the rebuild
	_update_status()


func _on_order_submitted(request: OrderRequest) -> void:
	SimBridge.simulation.submit_order(request.account, request.side, request.type, request.qty,
			request.limit_price, request.stop_price)


func _on_seed_requested() -> void:
	for order: OrderLadderSeed.SeedOrder in book_seed.orders():
		SimBridge.simulation.submit_order(order.account, order.side, Simulation.ORDER_TYPE_LIMIT,
				order.qty, order.price)


func _on_price_selected(price: int) -> void:
	select(EntityRef.make(EntityRef.Kind.PRICE, price))


## The depth row at [param price], or an all-zero row if there is no resting quantity there.
static func _level_at(depth: PackedInt64Array, price: int) -> PackedInt64Array:
	for base: int in range(0, depth.size(), Simulation.DEPTH_STRIDE):
		if depth[base + Simulation.DEPTH_FIELD_PRICE] == price:
			return depth.slice(base, base + Simulation.DEPTH_STRIDE)
	var empty: PackedInt64Array = PackedInt64Array()
	empty.resize(Simulation.DEPTH_STRIDE)
	return empty
