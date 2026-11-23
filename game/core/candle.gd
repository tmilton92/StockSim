class_name Candle
extends RefCounted
## Open/high/low/close and volume of the trades in one fixed range of ticks.

## First tick covered by this candle.
var start_tick: int = 0
var open: int = 0
var high: int = 0
var low: int = 0
var close: int = 0
var volume: int = 0
var trade_count: int = 0


## Creates a candle from its first trade.
static func starting_with(first_tick: int, price: int, qty: int) -> Candle:
	var candle: Candle = Candle.new()
	candle.start_tick = first_tick
	candle.open = price
	candle.high = price
	candle.low = price
	candle.close = price
	candle.volume = qty
	candle.trade_count = 1
	return candle


## Adds a later trade in the same tick range.
func add(price: int, qty: int) -> void:
	high = maxi(high, price)
	low = mini(low, price)
	close = price
	volume += qty
	trade_count += 1


## True if the close is at or above the open.
func is_up() -> bool:
	return close >= open
