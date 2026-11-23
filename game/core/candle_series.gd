class_name CandleSeries
extends RefCounted
## Trades grouped into candles of a fixed number of ticks, for the price chart.
##
## Aggregation of already-executed trades for display; it does not compute prices.

## Ticks covered by each candle.
var ticks_per_candle: int = 1
## Candles in time order. Ranges with no trades have no candle.
var candles: Array[Candle] = []
## Price of the most recent trade, if any trade was added.
var last_price: int = 0
var has_trades: bool = false


func _init(candle_ticks: int = 5) -> void:
	ticks_per_candle = maxi(1, candle_ticks)


## Adds one trade at [param tick]. Trades must arrive in non-decreasing tick order.
func add_trade(tick: int, price: int, qty: int) -> void:
	var start: int = bucket_start(tick)
	if not candles.is_empty() and candles[candles.size() - 1].start_tick == start:
		candles[candles.size() - 1].add(price, qty)
	else:
		candles.append(Candle.starting_with(start, price, qty))
	last_price = price
	has_trades = true


## Rebuilds the series from trade events (e.g. after a step back).
func rebuild(trades: Array[SimEvent]) -> void:
	clear()
	for trade: SimEvent in trades:
		add_trade(trade.tick, trade.price, trade.qty)


func clear() -> void:
	candles.clear()
	last_price = 0
	has_trades = false


## First tick of the candle containing [param tick].
func bucket_start(tick: int) -> int:
	return tick - posmod(tick, ticks_per_candle)


## The newest [param count] candles, oldest first.
func latest(count: int) -> Array[Candle]:
	var result: Array[Candle] = []
	for i: int in range(maxi(0, candles.size() - count), candles.size()):
		result.append(candles[i])
	return result
