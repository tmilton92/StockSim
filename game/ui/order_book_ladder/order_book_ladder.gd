class_name OrderBookLadder
extends Control
## Price ladder of the order book: asks above, bids below, depth bars sized by resting quantity.
##
## Bars animate toward new quantities (levels that disappear fade out), trades flash their price
## row, and a price can be highlighted. Clicking a row emits [signal price_selected]. Drawing is
## custom ([method _draw]); per-frame processing runs only while something is animating.

## A price row was clicked.
signal price_selected(price: int)

## Seconds a fill flash takes to fade.
const FLASH_SECONDS: float = 0.8
## How quickly bars approach their target size (fraction of the gap closed per second, approx.).
const BAR_RATE: float = 10.0
## Bars within this many lots of their target snap to it.
const SNAP_QTY: float = 0.05

const _THEME_TYPE: StringName = &"OrderBookLadder"

var _bid_target: Dictionary[int, int] = {}
var _ask_target: Dictionary[int, int] = {}
var _bid_shown: Dictionary[int, float] = {}
var _ask_shown: Dictionary[int, float] = {}
var _flashes: Dictionary[int, float] = {}
var _last_price: int = 0
var _has_last_price: bool = false
var _highlighted_price: int = 0
var _has_highlight: bool = false
## Price and top edge of each drawn row, top to bottom (for click handling).
var _row_prices: PackedInt64Array = PackedInt64Array()
var _row_tops: PackedFloat32Array = PackedFloat32Array()


func _ready() -> void:
	set_process(false)
	mouse_filter = Control.MOUSE_FILTER_STOP
	if SceneUtil.is_standalone(self):
		var market: MockMarket = MockMarket.build()
		set_depth(market.bids, market.asks)
		set_last_price(market.last_price)
		flash(market.last_price)


func _process(delta: float) -> void:
	var animating: bool = _animate_side(_bid_shown, _bid_target, delta)
	animating = _animate_side(_ask_shown, _ask_target, delta) or animating
	for price: int in _flashes.keys():
		_flashes[price] -= delta
		if _flashes[price] <= 0.0:
			_flashes.erase(price)
	animating = animating or not _flashes.is_empty()
	queue_redraw()
	if not animating:
		set_process(false)


func _draw() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), _color(&"background"))
	_row_prices.clear()
	_row_tops.clear()
	var row_height: float = _row_height()
	if row_height <= 0.0 or size.x <= 0.0:
		return
	_draw_header(row_height)
	var rows: int = maxi(2, floori(size.y / row_height) - 1)  # one row is the header
	var ask_rows: int = rows >> 1  # half the rows (rounded down) for asks, the rest for bids
	var asks: Array[int] = _sorted_prices(_ask_shown, true, ask_rows)
	asks.reverse()  # highest ask at the top, best ask just above the spread
	var bids: Array[int] = _sorted_prices(_bid_shown, false, rows - ask_rows)
	var max_qty: float = maxf(1.0, maxf(_max_value(_bid_shown), _max_value(_ask_shown)))

	# Asks fill their half from the spread upwards, so the spread stays in the same place.
	var top: float = row_height * (1 + ask_rows - asks.size())
	for price: int in asks:
		_draw_row(top, row_height, price, 0.0, _ask_shown[price], max_qty)
		top += row_height
	draw_line(Vector2(0.0, top), Vector2(size.x, top), _color(&"spread"), 1.0)
	for price: int in bids:
		_draw_row(top, row_height, price, _bid_shown[price], 0.0, max_qty)
		top += row_height


func _gui_input(event: InputEvent) -> void:
	var click: InputEventMouseButton = event as InputEventMouseButton
	if click == null or not click.pressed or click.button_index != MOUSE_BUTTON_LEFT:
		return
	var price: int = price_at(click.position)
	if price > 0:
		price_selected.emit(price)
		accept_event()


func _get_minimum_size() -> Vector2:
	return Vector2(240.0, _row_height() * 4.0)


## Replaces the depth shown. [param bids] and [param asks] are [method Simulation.get_depth]
## results (rows of [constant Simulation.DEPTH_STRIDE]).
func set_depth(bids: PackedInt64Array, asks: PackedInt64Array) -> void:
	_set_targets(_bid_target, _bid_shown, bids)
	_set_targets(_ask_target, _ask_shown, asks)
	set_process(true)
	queue_redraw()


## Flashes the row at [param price] (a trade happened there).
func flash(price: int) -> void:
	_flashes[price] = FLASH_SECONDS
	set_process(true)


func set_last_price(price: int) -> void:
	_last_price = price
	_has_last_price = true
	queue_redraw()


func set_highlighted_price(price: int) -> void:
	_highlighted_price = price
	_has_highlight = true
	queue_redraw()


func clear_highlight() -> void:
	_has_highlight = false
	queue_redraw()


## Removes everything, immediately (no animation).
func clear() -> void:
	for dictionary: Dictionary in [_bid_target, _ask_target, _bid_shown, _ask_shown, _flashes]:
		dictionary.clear()
	_has_last_price = false
	_has_highlight = false
	set_process(false)
	queue_redraw()


## Price of the row at [param point] (local coordinates), or 0 if there is none.
func price_at(point: Vector2) -> int:
	var row_height: float = _row_height()
	for row: int in range(_row_tops.size()):
		if point.y >= _row_tops[row] and point.y < _row_tops[row] + row_height:
			return _row_prices[row]
	return 0


## Quantity currently displayed for [param price] on [param side] (for tests and inspection).
func shown_qty(side: Simulation.Side, price: int) -> float:
	var shown: Dictionary[int, float] = _bid_shown if side == Simulation.SIDE_BUY else _ask_shown
	return shown[price] if shown.has(price) else 0.0


func is_animating() -> bool:
	return is_processing()


func _draw_row(top: float, height: float, price: int, bid_qty: float, ask_qty: float,
		max_qty: float) -> void:
	var font: Font = get_theme_font(&"font", _THEME_TYPE)
	var font_size: int = get_theme_font_size(&"font_size", _THEME_TYPE)
	var column: float = size.x / 3.0
	var row_rect: Rect2 = Rect2(0.0, top, size.x, height)
	_row_prices.append(price)
	_row_tops.append(top)

	if _flashes.has(price):
		var flash_color: Color = _color(&"flash")
		flash_color.a *= clampf(_flashes[price] / FLASH_SECONDS, 0.0, 1.0)
		draw_rect(row_rect, flash_color)
	if bid_qty > 0.0:
		var width: float = column * bid_qty / max_qty
		draw_rect(Rect2(column - width, top + 1.0, width, height - 2.0), _color(&"bid_bar"))
	if ask_qty > 0.0:
		var width: float = column * ask_qty / max_qty
		draw_rect(Rect2(column * 2.0, top + 1.0, width, height - 2.0), _color(&"ask_bar"))
	if _has_highlight and price == _highlighted_price:
		draw_rect(row_rect.grow(-1.0), _color(&"highlight"), false, 1.0)

	var baseline: float = top + (height - font.get_height(font_size)) / 2.0 \
			+ font.get_ascent(font_size)
	var pad: float = float(get_theme_constant(&"row_padding", _THEME_TYPE))
	if bid_qty > 0.0:
		draw_string(font, Vector2(pad, baseline), PriceFormat.qty(roundi(bid_qty)),
				HORIZONTAL_ALIGNMENT_RIGHT, column - pad * 2.0, font_size, _color(&"bid_text"))
	if ask_qty > 0.0:
		draw_string(font, Vector2(column * 2.0 + pad, baseline), PriceFormat.qty(roundi(ask_qty)),
				HORIZONTAL_ALIGNMENT_LEFT, column - pad * 2.0, font_size, _color(&"ask_text"))
	var is_last: bool = _has_last_price and price == _last_price
	var price_color: Color = _color(&"last_price") if is_last else _color(&"price_text")
	draw_string(font, Vector2(column, baseline), PriceFormat.price(price),
			HORIZONTAL_ALIGNMENT_CENTER, column, font_size, price_color)


func _draw_header(height: float) -> void:
	var font: Font = get_theme_font(&"font", _THEME_TYPE)
	var font_size: int = get_theme_font_size(&"font_size", _THEME_TYPE)
	var column: float = size.x / 3.0
	var baseline: float = (height - font.get_height(font_size)) / 2.0 + font.get_ascent(font_size)
	var color: Color = _color(&"header")
	draw_string(font, Vector2(0.0, baseline), "BIDS", HORIZONTAL_ALIGNMENT_CENTER, column,
			font_size, color)
	draw_string(font, Vector2(column, baseline), "PRICE", HORIZONTAL_ALIGNMENT_CENTER, column,
			font_size, color)
	draw_string(font, Vector2(column * 2.0, baseline), "ASKS", HORIZONTAL_ALIGNMENT_CENTER,
			column, font_size, color)
	draw_line(Vector2(0.0, height), Vector2(size.x, height), _color(&"spread"), 1.0)


func _row_height() -> float:
	var font: Font = get_theme_font(&"font", _THEME_TYPE)
	var font_size: int = get_theme_font_size(&"font_size", _THEME_TYPE)
	if font == null:
		return 0.0
	return font.get_height(font_size) + 2.0 * float(get_theme_constant(&"row_padding", _THEME_TYPE))


func _color(color_name: StringName) -> Color:
	return get_theme_color(color_name, _THEME_TYPE)


static func _set_targets(target: Dictionary[int, int], shown: Dictionary[int, float],
		depth: PackedInt64Array) -> void:
	target.clear()
	for base: int in range(0, depth.size(), Simulation.DEPTH_STRIDE):
		var price: int = depth[base + Simulation.DEPTH_FIELD_PRICE]
		target[price] = depth[base + Simulation.DEPTH_FIELD_QTY]
		if not shown.has(price):
			shown[price] = 0.0  # new level grows in from zero


## Moves shown quantities toward their targets; returns true while any is still moving.
static func _animate_side(shown: Dictionary[int, float], target: Dictionary[int, int],
		delta: float) -> bool:
	var moving: bool = false
	var step: float = clampf(BAR_RATE * delta, 0.0, 1.0)
	for price: int in shown.keys():
		var goal: float = float(target[price]) if target.has(price) else 0.0
		var value: float = lerpf(shown[price], goal, step)
		if absf(value - goal) <= SNAP_QTY:
			value = goal
		else:
			moving = true
		if value == 0.0 and not target.has(price):
			shown.erase(price)  # faded out completely
		else:
			shown[price] = value
	return moving


## Up to [param limit] prices of [param shown], best first (lowest asks / highest bids).
static func _sorted_prices(shown: Dictionary[int, float], ascending: bool,
		limit: int) -> Array[int]:
	var prices: Array[int] = []
	prices.assign(shown.keys())
	prices.sort()
	if not ascending:
		prices.reverse()
	if prices.size() > limit:
		prices.resize(limit)
	return prices


static func _max_value(values: Dictionary[int, float]) -> float:
	var result: float = 0.0
	for price: int in values:
		result = maxf(result, values[price])
	return result
