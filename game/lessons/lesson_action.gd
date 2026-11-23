class_name LessonAction
extends Resource
## One scripted step of a lesson beat. Actions are inputs to the simulation (orders, steps,
## resets) or instructions to the UI (highlight, comparison); they never decide market outcomes.

enum Type {
	## Place one order ([member account], [member side], [member order_type], [member qty],
	## [member price], [member stop_price]); optionally remember its id under [member tag].
	SUBMIT,
	## Place [member levels] limit orders from [member price], [member price_step] apart,
	## [member qty] each plus [member qty_step] per level.
	LADDER,
	## Run [member count] ticks.
	TICK,
	## Run [member count] single steps.
	STEP,
	## Replace the market with a fresh one from the same scenario.
	RESET,
	## Highlight [member price], [member account], or the order tagged [member tag].
	HIGHLIGHT,
	## Record how the order tagged [member tag] filled, under [member label].
	RECORD,
	## Show every record taken so far side by side.
	SHOW_COMPARISON,
}

@export var type: Type = Type.TICK
@export var account: int = 0
@export var side: Simulation.Side = Simulation.SIDE_BUY
@export var order_type: Simulation.OrderType = Simulation.ORDER_TYPE_LIMIT
@export var qty: int = 0
## Ticks; 0 when unused.
@export var price: int = 0
## Ticks; 0 when unused.
@export var stop_price: int = 0
@export var tag: String = ""
@export var label: String = ""
@export var count: int = 1
@export var levels: int = 0
## Ticks between ladder levels (always positive; asks go up, bids go down).
@export var price_step: int = 0
@export var qty_step: int = 0
