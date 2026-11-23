class_name OrderLadderSeed
extends Resource
## A ladder of resting limit orders on both sides of a mid price, placed by the order ticket's
## "Seed book" button so there is a market to look at before agents exist (TODO section 6).
##
## Pure input data: the orders are submitted to the simulation like any other client orders.

## Price (ticks) halfway between the best bid and best ask.
@export var mid_price: int = 10000
## Price levels per side.
@export_range(1, 50) var levels: int = 5
## Ticks between neighbouring levels; also the distance from the mid to the best bid/ask.
@export_range(1, 1000) var spacing: int = 5
## Quantity at the best level; each level further out adds [member qty_step].
@export_range(1, 1000000) var base_qty: int = 10
@export_range(0, 1000000) var qty_step: int = 5
## Accounts that own the seeded bids and asks.
@export_range(0, 4294967295) var bid_account: int = 101
@export_range(0, 4294967295) var ask_account: int = 102


## The orders to submit, best levels first.
func orders() -> Array[SeedOrder]:
	var result: Array[SeedOrder] = []
	for level: int in range(levels):
		var offset: int = spacing * (level + 1)
		var qty: int = base_qty + qty_step * level
		result.append(SeedOrder.make(bid_account, Simulation.SIDE_BUY, mid_price - offset, qty))
		result.append(SeedOrder.make(ask_account, Simulation.SIDE_SELL, mid_price + offset, qty))
	return result


## One limit order of the seed ladder.
class SeedOrder:
	var account: int = 0
	var side: Simulation.Side = Simulation.SIDE_BUY
	var price: int = 0
	var qty: int = 0

	static func make(order_account: int, order_side: Simulation.Side, order_price: int,
			order_qty: int) -> SeedOrder:
		var order: SeedOrder = SeedOrder.new()
		order.account = order_account
		order.side = order_side
		order.price = order_price
		order.qty = order_qty
		return order
