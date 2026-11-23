extends GdUnitTestSuite
## OrderTicket: building and validating order requests.

## Concrete type: the interface declares simulate_frames() abstract, so awaiting it through
## GdUnitSceneRunner looks redundant to the analyzer even though the implementation awaits.
var _runner: GdUnitSceneRunnerImpl
var _ticket: OrderTicket


func before_test() -> void:
	_runner = scene_runner("res://ui/order_ticket/order_ticket.tscn") as GdUnitSceneRunnerImpl
	_ticket = _runner.scene() as OrderTicket


func _set_type(type: Simulation.OrderType) -> void:
	var option: OptionButton = _ticket.find_child("TypeOption") as OptionButton
	option.select(option.get_item_index(type))
	option.item_selected.emit(option.selected)


func _set_text(field: String, text: String) -> void:
	(_ticket.find_child(field) as LineEdit).text = text


func test_limit_order_request_uses_exact_ticks() -> void:
	_set_type(Simulation.ORDER_TYPE_LIMIT)
	_set_text("LimitEdit", "100.07")
	var request: OrderRequest = _ticket.build_request()
	assert_object(request).is_not_null()
	assert_int(request.type).is_equal(Simulation.ORDER_TYPE_LIMIT)
	assert_int(request.limit_price).is_equal(10007)
	assert_int(request.stop_price).is_equal(0)  # not used by a limit order


func test_invalid_price_is_rejected_with_a_message() -> void:
	_set_type(Simulation.ORDER_TYPE_LIMIT)
	_set_text("LimitEdit", "abc")
	assert_object(_ticket.build_request()).is_null()
	assert_str((_ticket.find_child("ErrorLabel") as Label).text).contains("Limit price")


func test_market_order_ignores_price_fields() -> void:
	_set_type(Simulation.ORDER_TYPE_MARKET)
	_set_text("LimitEdit", "not a price")
	var request: OrderRequest = _ticket.build_request()
	assert_object(request).is_not_null()
	assert_int(request.limit_price).is_equal(0)
	assert_bool((_ticket.find_child("LimitEdit") as LineEdit).editable).is_false()


func test_stop_limit_needs_both_prices() -> void:
	_set_type(Simulation.ORDER_TYPE_STOP_LIMIT)
	_set_text("LimitEdit", "99.50")
	_set_text("StopEdit", "")
	assert_object(_ticket.build_request()).is_null()
	_set_text("StopEdit", "99.80")
	var request: OrderRequest = _ticket.build_request()
	assert_int(request.limit_price).is_equal(9950)
	assert_int(request.stop_price).is_equal(9980)
