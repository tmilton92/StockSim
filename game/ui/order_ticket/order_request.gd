class_name OrderRequest
extends RefCounted
## An order as entered in the order ticket, before it is submitted to the simulation.

var account: int = 1
var side: Simulation.Side = Simulation.SIDE_BUY
var type: Simulation.OrderType = Simulation.ORDER_TYPE_LIMIT
var qty: int = 10
## Ticks; used by limit and stop-limit orders, otherwise 0.
var limit_price: int = 0
## Ticks; used by stop and stop-limit orders, otherwise 0.
var stop_price: int = 0


static func uses_limit_price(order_type: Simulation.OrderType) -> bool:
	return order_type == Simulation.ORDER_TYPE_LIMIT \
			or order_type == Simulation.ORDER_TYPE_STOP_LIMIT


static func uses_stop_price(order_type: Simulation.OrderType) -> bool:
	return order_type == Simulation.ORDER_TYPE_STOP \
			or order_type == Simulation.ORDER_TYPE_STOP_LIMIT
