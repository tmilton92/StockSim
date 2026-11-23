class_name ImpactRecord
extends RefCounted
## How one order filled: what the best quote was when it was sent, and what it actually paid.
##
## Built from the order's fill events (core output). Totals are exact integers; averages are
## only computed for display.

var label: String = ""
var side: Simulation.Side = Simulation.SIDE_BUY
var ordered_qty: int = 0
var filled_qty: int = 0
## Best opposite quote (ticks) when the order was submitted; 0 if that side was empty.
var reference_price: int = 0
## Sum of fill price x quantity (ticks x lots).
var notional: int = 0
var best_fill: int = 0
var worst_fill: int = 0
## Distinct prices the order filled at.
var levels: int = 0
## Last trade price (ticks) when the record was taken.
var last_price_after: int = 0


## Builds a record from [param fills] (FILL events of one order, oldest first).
static func from_fills(record_label: String, order_side: Simulation.Side, ordered: int,
		reference_quote: int, fills: Array[SimEvent], last_price: int) -> ImpactRecord:
	var record: ImpactRecord = ImpactRecord.new()
	record.label = record_label
	record.side = order_side
	record.ordered_qty = ordered
	record.reference_price = reference_quote
	record.last_price_after = last_price
	var prices: Dictionary[int, bool] = {}
	for fill: SimEvent in fills:
		record.filled_qty += fill.qty
		record.notional += fill.price * fill.qty
		prices[fill.price] = true
		if record.levels == 0 or record._is_better(fill.price, record.best_fill):
			record.best_fill = fill.price
		if record.levels == 0 or record._is_better(record.worst_fill, fill.price):
			record.worst_fill = fill.price
		record.levels = prices.size()
	return record


## Average fill price as text with three decimals (display only).
func average_fill_text() -> String:
	if filled_qty == 0:
		return "-"
	return "%.3f" % (float(notional) / float(filled_qty) / 100.0)


## Average distance from the reference quote against the trader, in price units (display only).
## Positive means the order paid more (buy) or received less (sell) than the best quote.
func slippage_text() -> String:
	if filled_qty == 0 or reference_price == 0:
		return "-"
	var average_ticks: float = float(notional) / float(filled_qty)
	var against: float = average_ticks - reference_price if side == Simulation.SIDE_BUY \
			else reference_price - average_ticks
	return "%.3f" % (against / 100.0)


## Price moved by the order: last price after minus the reference quote (ticks).
func price_move() -> int:
	return last_price_after - reference_price if reference_price != 0 else 0


## True if [param a] is a better price than [param b] for this record's side.
func _is_better(a: int, b: int) -> bool:
	return a < b if side == Simulation.SIDE_BUY else a > b
