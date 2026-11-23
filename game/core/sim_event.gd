class_name SimEvent
extends RefCounted
## One simulation event, decoded from a row of [method Simulation.drain_events].
##
## Field meanings follow event schema v1 (docs/adr/0005). Prices are integer ticks and
## quantities integer lots; see [PriceFormat] for display.

var seq: int = 0
var tick: int = 0
var type: Simulation.EventType = Simulation.EVENT_ORDER_PLACED
var reason: Simulation.Reason = Simulation.REASON_NONE
var intent: int = 0
var side: Simulation.Side = Simulation.SIDE_BUY
var order_type: Simulation.OrderType = Simulation.ORDER_TYPE_LIMIT
var order_id: int = 0
var counter_order_id: int = 0
var account: int = 0
var counter_account: int = 0
var price: int = 0
var aux_price: int = 0
var qty: int = 0
var remaining: int = 0


## Decodes the event row starting at [param base] in [param rows].
static func from_row(rows: PackedInt64Array, base: int) -> SimEvent:
	var event: SimEvent = SimEvent.new()
	event.seq = rows[base + Simulation.EVENT_FIELD_SEQ]
	event.tick = rows[base + Simulation.EVENT_FIELD_TICK]
	event.type = rows[base + Simulation.EVENT_FIELD_TYPE] as Simulation.EventType
	event.reason = rows[base + Simulation.EVENT_FIELD_REASON] as Simulation.Reason
	event.intent = rows[base + Simulation.EVENT_FIELD_INTENT]
	event.side = rows[base + Simulation.EVENT_FIELD_SIDE] as Simulation.Side
	event.order_type = rows[base + Simulation.EVENT_FIELD_ORDER_TYPE] as Simulation.OrderType
	event.order_id = rows[base + Simulation.EVENT_FIELD_ORDER_ID]
	event.counter_order_id = rows[base + Simulation.EVENT_FIELD_COUNTER_ORDER_ID]
	event.account = rows[base + Simulation.EVENT_FIELD_ACCOUNT]
	event.counter_account = rows[base + Simulation.EVENT_FIELD_COUNTER_ACCOUNT]
	event.price = rows[base + Simulation.EVENT_FIELD_PRICE]
	event.aux_price = rows[base + Simulation.EVENT_FIELD_AUX_PRICE]
	event.qty = rows[base + Simulation.EVENT_FIELD_QTY]
	event.remaining = rows[base + Simulation.EVENT_FIELD_REMAINING]
	return event


## Decodes every row of a [method Simulation.drain_events] result, oldest first.
static func from_rows(rows: PackedInt64Array) -> Array[SimEvent]:
	var events: Array[SimEvent] = []
	if rows.size() % Simulation.EVENT_STRIDE != 0:
		push_error("SimEvent.from_rows: %d values is not a whole number of event rows (stride %d)."
				% [rows.size(), Simulation.EVENT_STRIDE])
		return events
	for base: int in range(0, rows.size(), Simulation.EVENT_STRIDE):
		events.append(from_row(rows, base))
	return events


## True for [constant Simulation.EVENT_MATCH]: one trade between a taker and a maker.
func is_trade() -> bool:
	return type == Simulation.EVENT_MATCH


## True if [param id] is the primary or counter order of this event.
func involves_order(id: int) -> bool:
	return id != 0 and (order_id == id or counter_order_id == id)


## True if [param id] is the primary or counter account of this event. Only meaningful when
## the event has the corresponding order (account 0 is a valid account id).
func involves_account(id: int) -> bool:
	return (order_id != 0 and account == id) or (counter_order_id != 0 and counter_account == id)
