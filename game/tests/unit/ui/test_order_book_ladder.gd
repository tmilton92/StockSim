extends GdUnitTestSuite
## OrderBookLadder: animated depth, fading levels, flashes, and price picking.

## Concrete type: the interface declares simulate_frames() abstract, so awaiting it through
## GdUnitSceneRunner looks redundant to the analyzer even though the implementation awaits.
var _runner: GdUnitSceneRunnerImpl
var _ladder: OrderBookLadder


func before_test() -> void:
	_runner = scene_runner("res://ui/order_book_ladder/order_book_ladder.tscn") \
			as GdUnitSceneRunnerImpl
	_ladder = _runner.scene() as OrderBookLadder
	_ladder.clear()
	# The scene's root fills its parent; give it a fixed rect for deterministic row positions.
	_ladder.set_anchors_and_offsets_preset(Control.PRESET_TOP_LEFT)
	_ladder.size = Vector2(300.0, 400.0)


func _depth(levels: Array[PackedInt64Array]) -> PackedInt64Array:
	var rows: PackedInt64Array = PackedInt64Array()
	for level: PackedInt64Array in levels:
		rows.append_array(PackedInt64Array([level[0], level[1], 1]))
	return rows


func test_bars_animate_to_their_target_quantities_then_stop() -> void:
	_ladder.set_depth(_depth([PackedInt64Array([9990, 40])]),
			_depth([PackedInt64Array([10010, 25])]))
	assert_float(_ladder.shown_qty(Simulation.SIDE_BUY, 9990)).is_equal(0.0)  # grows from zero
	assert_bool(_ladder.is_animating()).is_true()
	await _runner.simulate_frames(60, 16)
	assert_float(_ladder.shown_qty(Simulation.SIDE_BUY, 9990)).is_equal(40.0)
	assert_float(_ladder.shown_qty(Simulation.SIDE_SELL, 10010)).is_equal(25.0)
	assert_bool(_ladder.is_animating()).is_false()


func test_removed_levels_fade_out() -> void:
	_ladder.set_depth(_depth([PackedInt64Array([9990, 40])]), PackedInt64Array())
	await _runner.simulate_frames(60, 16)
	_ladder.set_depth(PackedInt64Array(), PackedInt64Array())
	await _runner.simulate_frames(1, 16)
	var fading: float = _ladder.shown_qty(Simulation.SIDE_BUY, 9990)
	assert_float(fading).is_greater(0.0)
	assert_float(fading).is_less(40.0)
	await _runner.simulate_frames(60, 16)
	assert_float(_ladder.shown_qty(Simulation.SIDE_BUY, 9990)).is_equal(0.0)
	assert_bool(_ladder.is_animating()).is_false()


func test_flash_keeps_processing_until_it_fades() -> void:
	_ladder.flash(10000)
	assert_bool(_ladder.is_animating()).is_true()
	await _runner.simulate_frames(70, 16)  # > FLASH_SECONDS
	assert_bool(_ladder.is_animating()).is_false()


func test_price_at_maps_drawn_rows_and_ignores_empty_space() -> void:
	_ladder.set_depth(
			_depth([PackedInt64Array([9990, 10]), PackedInt64Array([9985, 20])]),
			_depth([PackedInt64Array([10010, 5])]))
	await _runner.simulate_frames(5, 16)
	assert_int(_ladder.price_at(Vector2(150.0, 1.0))).is_equal(0)  # header row
	var prices: Array[int] = []
	for y: int in range(0, 400, 2):
		var price: int = _ladder.price_at(Vector2(150.0, float(y)))
		if price != 0 and not prices.has(price):
			prices.append(price)
	# Top to bottom: the ask, then bids from best to worst.
	assert_bool(prices == [10010, 9990, 9985]).is_true()
