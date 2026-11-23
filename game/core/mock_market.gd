class_name MockMarket
extends RefCounted
## Sample market data for scenes run on their own (F6) and for tests.
##
## Not hand-made: it runs a private [Simulation] with the default seeded book and a few market
## orders, so every event and depth level is genuine core output.

const DEPTH_LEVELS: int = 20

var events: Array[SimEvent] = []
var bids: PackedInt64Array = PackedInt64Array()
var asks: PackedInt64Array = PackedInt64Array()
var last_price: int = 0


static func build() -> MockMarket:
	var market: MockMarket = MockMarket.new()
	var simulation: Simulation = Simulation.new()
	var book_seed: OrderLadderSeed = OrderLadderSeed.new()
	for order: OrderLadderSeed.SeedOrder in book_seed.orders():
		simulation.submit_order(order.account, order.side, Simulation.ORDER_TYPE_LIMIT, order.qty,
				order.price)
	simulation.step_tick()
	# Market orders as [account, side, qty].
	var takers: Array[PackedInt64Array] = [
		PackedInt64Array([1, Simulation.SIDE_BUY, 12]),
		PackedInt64Array([2, Simulation.SIDE_SELL, 20]),
		PackedInt64Array([3, Simulation.SIDE_BUY, 30]),
		PackedInt64Array([1, Simulation.SIDE_SELL, 8]),
		PackedInt64Array([4, Simulation.SIDE_BUY, 18]),
	]
	for taker: PackedInt64Array in takers:
		simulation.submit_order(taker[0], taker[1] as Simulation.Side,
				Simulation.ORDER_TYPE_MARKET, taker[2])
		simulation.step_tick()
	market.events = SimEvent.from_rows(simulation.drain_events())
	market.bids = simulation.get_depth(Simulation.SIDE_BUY, DEPTH_LEVELS)
	market.asks = simulation.get_depth(Simulation.SIDE_SELL, DEPTH_LEVELS)
	market.last_price = simulation.get_last_price()
	simulation.free()
	return market


## The sample's trades, oldest first.
func trades() -> Array[SimEvent]:
	var result: Array[SimEvent] = []
	for event: SimEvent in events:
		if event.is_trade():
			result.append(event)
	return result
