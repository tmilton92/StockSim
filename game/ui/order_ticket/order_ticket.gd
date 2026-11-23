class_name OrderTicket
extends PanelContainer
## Debug order entry: place an order as any account, or seed the book with a ladder of limit
## orders. A stand-in for the player's trading UI (TODO section 10); it only creates requests.

## The user submitted a valid order.
signal order_submitted(request: OrderRequest)
## The user asked to seed the book with the default ladder.
signal seed_requested

@onready var _account_spin: SpinBox = %AccountSpin
@onready var _side_option: OptionButton = %SideOption
@onready var _type_option: OptionButton = %TypeOption
@onready var _qty_spin: SpinBox = %QtySpin
@onready var _limit_edit: LineEdit = %LimitEdit
@onready var _stop_edit: LineEdit = %StopEdit
@onready var _submit_button: Button = %SubmitButton
@onready var _seed_button: Button = %SeedButton
@onready var _error_label: Label = %ErrorLabel


func _ready() -> void:
	_side_option.add_item("Buy", Simulation.SIDE_BUY)
	_side_option.add_item("Sell", Simulation.SIDE_SELL)
	_type_option.add_item("Limit", Simulation.ORDER_TYPE_LIMIT)
	_type_option.add_item("Market", Simulation.ORDER_TYPE_MARKET)
	_type_option.add_item("Stop", Simulation.ORDER_TYPE_STOP)
	_type_option.add_item("Stop-limit", Simulation.ORDER_TYPE_STOP_LIMIT)
	_type_option.item_selected.connect(_on_type_selected)
	_submit_button.pressed.connect(_on_submit_pressed)
	_seed_button.pressed.connect(seed_requested.emit)
	_update_price_fields()
	_error_label.text = ""


## Sets the account orders are placed as (the learner's account in lessons).
func set_account(account: int) -> void:
	_account_spin.value = account


## Shows or hides the "Seed book" button (hidden in lessons).
func set_seed_visible(seed_visible: bool) -> void:
	_seed_button.visible = seed_visible


## Builds the request from the form, or returns null and shows why it is invalid.
func build_request() -> OrderRequest:
	var request: OrderRequest = OrderRequest.new()
	request.account = int(_account_spin.value)
	request.side = _side_option.get_selected_id() as Simulation.Side
	request.type = _type_option.get_selected_id() as Simulation.OrderType
	request.qty = int(_qty_spin.value)
	if OrderRequest.uses_limit_price(request.type):
		request.limit_price = PriceFormat.parse_price(_limit_edit.text)
		if request.limit_price <= 0:
			_show_error("Limit price must be a positive price like 100.25.")
			return null
	if OrderRequest.uses_stop_price(request.type):
		request.stop_price = PriceFormat.parse_price(_stop_edit.text)
		if request.stop_price <= 0:
			_show_error("Stop price must be a positive price like 99.50.")
			return null
	_error_label.text = ""
	return request


func _on_submit_pressed() -> void:
	var request: OrderRequest = build_request()
	if request != null:
		order_submitted.emit(request)


func _on_type_selected(_index: int) -> void:
	_update_price_fields()


func _update_price_fields() -> void:
	var type: Simulation.OrderType = _type_option.get_selected_id() as Simulation.OrderType
	_limit_edit.editable = OrderRequest.uses_limit_price(type)
	_stop_edit.editable = OrderRequest.uses_stop_price(type)


func _show_error(message: String) -> void:
	_error_label.text = message
