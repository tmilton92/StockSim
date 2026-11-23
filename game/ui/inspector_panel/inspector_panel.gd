class_name InspectorPanel
extends PanelContainer
## Details of the selected order, account or price level, with its recent history.
##
## Display only: the parent gathers the data (core queries and event-log filtering) and calls
## one of the show_* methods. Links inside the details emit [signal entity_selected].

## The user clicked an order, account or price link in the details.
signal entity_selected(ref: EntityRef)

## History lines shown for the selection.
const HISTORY_LINES: int = 12

@onready var _title: Label = %Title
@onready var _details: RichTextLabel = %Details
var _palette: NarrationPalette = NarrationPalette.new()
var _names: AccountNames = null


func _ready() -> void:
	_palette = NarrationPalette.from_theme(self)
	_details.meta_clicked.connect(_on_meta_clicked)
	clear()
	if SceneUtil.is_standalone(self):
		var market: MockMarket = MockMarket.build()
		show_price(market.last_price, 0, 0, 0, 0, market.trades())


## Shows order [param order_id]. [param live] is its [method Simulation.get_order] row (empty if
## the order is no longer live); [param history] its events, oldest first.
func show_order(order_id: int, live: PackedInt64Array, history: Array[SimEvent]) -> void:
	_title.text = "ORDER #%d" % order_id
	var lines: PackedStringArray = []
	if live.is_empty():
		lines.append("Not live: filled, cancelled, rejected, or not yet processed.")
	else:
		var side: Simulation.Side = live[Simulation.ORDER_FIELD_SIDE] as Simulation.Side
		var type: Simulation.OrderType = live[Simulation.ORDER_FIELD_TYPE] as Simulation.OrderType
		var status: Simulation.OrderStatus = \
				live[Simulation.ORDER_FIELD_STATUS] as Simulation.OrderStatus
		lines.append("%s %s by %s" % [PriceFormat.order_type(type), PriceFormat.side(side),
				Narrator.account_link(live[Simulation.ORDER_FIELD_ACCOUNT], _names)])
		lines.append("Status: %s" % PriceFormat.order_status(status))
		if live[Simulation.ORDER_FIELD_LIMIT_PRICE] != 0:
			lines.append("Limit: %s"
					% Narrator.price_link(live[Simulation.ORDER_FIELD_LIMIT_PRICE]))
		if live[Simulation.ORDER_FIELD_STOP_PRICE] != 0:
			lines.append("Stop: %s" % Narrator.price_link(live[Simulation.ORDER_FIELD_STOP_PRICE]))
		lines.append("Open: %s   Filled: %s" % [
				PriceFormat.qty(live[Simulation.ORDER_FIELD_REMAINING]),
				PriceFormat.qty(live[Simulation.ORDER_FIELD_FILLED])])
	_show(lines, history)


## Shows account [param account_id]: its live orders ([method Simulation.get_order] rows) and
## its recent events. Balances and positions arrive with accounts in core (TODO section 7).
func show_account(account_id: int, live_orders: Array[PackedInt64Array],
		history: Array[SimEvent]) -> void:
	_title.text = "ACCOUNT %d" % account_id
	if _names != null and _names.has_name(account_id):
		_title.text = "%s (ACCOUNT %d)" % [_names.label(account_id).to_upper(), account_id]
	var lines: PackedStringArray = []
	lines.append("Live orders: %d" % live_orders.size())
	for order: PackedInt64Array in live_orders:
		lines.append("  %s %s %s @ %s" % [
				Narrator.order_link(order[Simulation.ORDER_FIELD_ID]),
				PriceFormat.side(order[Simulation.ORDER_FIELD_SIDE] as Simulation.Side),
				PriceFormat.qty(order[Simulation.ORDER_FIELD_REMAINING]),
				Narrator.price_link(order[Simulation.ORDER_FIELD_LIMIT_PRICE])])
	_show(lines, history)


## Shows the price level [param price]: resting quantity and orders on each side, and recent
## trades at that price.
func show_price(price: int, bid_qty: int, bid_orders: int, ask_qty: int, ask_orders: int,
		trades: Array[SimEvent]) -> void:
	_title.text = "PRICE %s" % PriceFormat.price(price)
	var lines: PackedStringArray = []
	lines.append("Bids resting: %s in %s" % [PriceFormat.qty(bid_qty), _orders_text(bid_orders)])
	lines.append("Asks resting: %s in %s" % [PriceFormat.qty(ask_qty), _orders_text(ask_orders)])
	lines.append("Trades at this price: %d" % trades.size())
	_show(lines, trades)


## Shows accounts by these names (lesson participants); null shows "acct N".
func set_account_names(names: AccountNames) -> void:
	_names = names


func clear() -> void:
	_title.text = "INSPECTOR"
	_details.clear()
	_details.append_text(
			"Click an order, account or price in the book, tape or narration to inspect it.")


func _show(lines: PackedStringArray, history: Array[SimEvent]) -> void:
	_details.clear()
	_details.append_text("\n".join(lines))
	if history.is_empty():
		return
	_details.append_text("\n\n[color=#%s]Recent events[/color]\n" % _palette.muted.to_html(false))
	var recent: Array[SimEvent] = history.slice(maxi(0, history.size() - HISTORY_LINES))
	var described: PackedStringArray = []
	for event: SimEvent in recent:
		described.append(Narrator.describe(event, _palette, _names))
	_details.append_text("\n".join(described))


static func _orders_text(count: int) -> String:
	return "1 order" if count == 1 else "%d orders" % count


func _on_meta_clicked(meta: Variant) -> void:
	var ref: EntityRef = EntityRef.from_meta(str(meta))
	if ref != null:
		entity_selected.emit(ref)
