class_name PriceChart
extends Control
## Candlestick chart of trades, with a price axis and a line marking the last trade price.
##
## Candles group trades into fixed ranges of [member ticks_per_candle] ticks ([CandleSeries]).
## The chart shows the most recent candles that fit; drawing is custom ([method _draw]).

## Ticks grouped into each candle. Read when the chart enters the tree.
@export_range(1, 1000) var ticks_per_candle: int = 5

const CANDLE_WIDTH: float = 7.0
const CANDLE_GAP: float = 3.0
const AXIS_WIDTH: float = 64.0
const GRID_LINES: int = 4
const MARGIN: float = 8.0
## Minimum visible price range, in ticks, so a flat market is not zoomed to a single tick.
const MIN_RANGE_TICKS: int = 20

const _THEME_TYPE: StringName = &"PriceChart"

var _series: CandleSeries = CandleSeries.new()
var _highlighted_price: int = 0
var _has_highlight: bool = false


func _ready() -> void:
	_series = CandleSeries.new(ticks_per_candle)
	if SceneUtil.is_standalone(self):
		rebuild(MockMarket.build().trades())


func _draw() -> void:
	var font: Font = get_theme_font(&"font", _THEME_TYPE)
	var font_size: int = get_theme_font_size(&"font_size", _THEME_TYPE)
	draw_rect(Rect2(Vector2.ZERO, size), _color(&"background"))
	var plot: Rect2 = Rect2(MARGIN, MARGIN, size.x - AXIS_WIDTH - MARGIN,
			size.y - 2.0 * MARGIN)
	if plot.size.x <= CANDLE_WIDTH or plot.size.y <= 0.0:
		return
	if not _series.has_trades:
		draw_string(font, Vector2(0.0, size.y / 2.0), "No trades yet",
				HORIZONTAL_ALIGNMENT_CENTER, size.x, font_size, _color(&"axis_text"))
		return

	var capacity: int = maxi(1, floori(plot.size.x / (CANDLE_WIDTH + CANDLE_GAP)))
	var candles: Array[Candle] = _series.latest(capacity)
	var price_range: PackedInt64Array = _visible_range(candles)
	_draw_grid(plot, price_range, font, font_size)

	var x: float = plot.end.x - candles.size() * (CANDLE_WIDTH + CANDLE_GAP)
	for candle: Candle in candles:
		var color: Color = _color(&"up") if candle.is_up() else _color(&"down")
		var center: float = x + CANDLE_WIDTH / 2.0
		draw_line(Vector2(center, _y(candle.high, plot, price_range)),
				Vector2(center, _y(candle.low, plot, price_range)), color, 1.0)
		var body_top: float = _y(maxi(candle.open, candle.close), plot, price_range)
		var body_bottom: float = _y(mini(candle.open, candle.close), plot, price_range)
		draw_rect(Rect2(x, body_top, CANDLE_WIDTH, maxf(1.0, body_bottom - body_top)), color)
		x += CANDLE_WIDTH + CANDLE_GAP

	var highlight_visible: bool = _has_highlight and _highlighted_price >= price_range[0] \
			and _highlighted_price <= price_range[1]
	if highlight_visible:
		var highlight_y: float = _y(_highlighted_price, plot, price_range)
		draw_line(Vector2(plot.position.x, highlight_y), Vector2(plot.end.x, highlight_y),
				_color(&"highlight"), 1.0)
	_draw_last_price(plot, price_range, font, font_size)


func _get_minimum_size() -> Vector2:
	return Vector2(240.0, 160.0)


## Adds new trades (match events) in time order.
func add_trades(trades: Array[SimEvent]) -> void:
	for trade: SimEvent in trades:
		_series.add_trade(trade.tick, trade.price, trade.qty)
	queue_redraw()


## Replaces all candles with ones built from [param trades] (e.g. after a step back).
func rebuild(trades: Array[SimEvent]) -> void:
	_series.rebuild(trades)
	queue_redraw()


func clear() -> void:
	_series.clear()
	queue_redraw()


func set_highlighted_price(price: int) -> void:
	_highlighted_price = price
	_has_highlight = true
	queue_redraw()


func clear_highlight() -> void:
	_has_highlight = false
	queue_redraw()


## The candles currently held (for tests and inspection).
func series() -> CandleSeries:
	return _series


func _draw_grid(plot: Rect2, price_range: PackedInt64Array, font: Font, font_size: int) -> void:
	for line: int in range(GRID_LINES + 1):
		var fraction: float = float(line) / float(GRID_LINES)
		var price: int = price_range[0] + roundi((price_range[1] - price_range[0]) * fraction)
		var y: float = _y(price, plot, price_range)
		draw_line(Vector2(plot.position.x, y), Vector2(plot.end.x, y), _color(&"grid"), 1.0)
		draw_string(font, Vector2(plot.end.x + 6.0, y + font.get_ascent(font_size) / 2.0),
				PriceFormat.price(price), HORIZONTAL_ALIGNMENT_LEFT, AXIS_WIDTH - 6.0,
				font_size, _color(&"axis_text"))


func _draw_last_price(plot: Rect2, price_range: PackedInt64Array, font: Font,
		font_size: int) -> void:
	var y: float = _y(_series.last_price, plot, price_range)
	var color: Color = _color(&"last_price")
	draw_dashed_line(Vector2(plot.position.x, y), Vector2(plot.end.x, y), color, 1.0, 4.0)
	var tag_height: float = font.get_height(font_size) + 2.0
	draw_rect(Rect2(plot.end.x + 2.0, y - tag_height / 2.0, AXIS_WIDTH - 2.0, tag_height), color)
	draw_string(font, Vector2(plot.end.x + 6.0, y + font.get_ascent(font_size) / 2.0 - 1.0),
			PriceFormat.price(_series.last_price), HORIZONTAL_ALIGNMENT_LEFT, AXIS_WIDTH - 6.0,
			font_size, _color(&"last_price_text"))


## [lowest, highest] price to show (ticks), padded and never narrower than MIN_RANGE_TICKS.
## Plain 64-bit ints rather than Vector2i, which is 32-bit.
func _visible_range(candles: Array[Candle]) -> PackedInt64Array:
	var low: int = _series.last_price
	var high: int = _series.last_price
	for candle: Candle in candles:
		low = mini(low, candle.low)
		high = maxi(high, candle.high)
	var padding: int = maxi(1, (high - low) >> 3)
	low -= padding
	high += padding
	var missing: int = MIN_RANGE_TICKS - (high - low)
	if missing > 0:
		low -= missing >> 1
		high += missing - (missing >> 1)
	return PackedInt64Array([low, high])


static func _y(price: int, plot: Rect2, price_range: PackedInt64Array) -> float:
	var span: float = maxf(1.0, float(price_range[1] - price_range[0]))
	return plot.end.y - (float(price - price_range[0]) / span) * plot.size.y


func _color(color_name: StringName) -> Color:
	return get_theme_color(color_name, _THEME_TYPE)
