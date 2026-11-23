extends Node
## Visual check: drives the main screen through a short scripted session and saves a screenshot.
##
## Run as a normal scene (so autoloads exist), with a window:
##   godot --path game --resolution 1440x900 res://tests/visual/main_screen_capture.tscn -- OUT.png
## Inputs only (seed book, market orders, a stop); every price and trade comes from the core.

const FRAMES_BEFORE_CAPTURE: int = 45

@onready var _screen: MainScreen = %MainScreen
var _frames: int = 0


func _ready() -> void:
	var simulation: Simulation = SimBridge.simulation
	for order: OrderLadderSeed.SeedOrder in _screen.book_seed.orders():
		simulation.submit_order(order.account, order.side, Simulation.ORDER_TYPE_LIMIT, order.qty,
				order.price)
	simulation.step_tick()
	# Market orders as [account, side, qty], each followed by a few ticks.
	var takers: Array[PackedInt64Array] = [
		PackedInt64Array([1, Simulation.SIDE_BUY, 14]),
		PackedInt64Array([2, Simulation.SIDE_SELL, 22]),
		PackedInt64Array([3, Simulation.SIDE_BUY, 35]),
		PackedInt64Array([1, Simulation.SIDE_SELL, 9]),
		PackedInt64Array([4, Simulation.SIDE_BUY, 18]),
		PackedInt64Array([2, Simulation.SIDE_SELL, 30]),
	]
	for taker: PackedInt64Array in takers:
		simulation.submit_order(taker[0], taker[1] as Simulation.Side,
				Simulation.ORDER_TYPE_MARKET, taker[2])
		for i: int in range(3):
			simulation.step_tick()
	simulation.submit_order(5, Simulation.SIDE_SELL, Simulation.ORDER_TYPE_STOP, 10, 0, 9990)
	simulation.step_tick()


func _process(_delta: float) -> void:
	_frames += 1
	if _frames == 5:
		_screen.select(EntityRef.make(EntityRef.Kind.PRICE, SimBridge.simulation.get_last_price()))
	if _frames < FRAMES_BEFORE_CAPTURE:
		return
	var args: PackedStringArray = OS.get_cmdline_user_args()
	if args.size() == 1:
		var error: Error = get_viewport().get_texture().get_image().save_png(args[0])
		print("CAPTURE_SAVED" if error == OK else "CAPTURE_FAILED %d" % error)
	get_tree().quit()
