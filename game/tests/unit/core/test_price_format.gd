extends GdUnitTestSuite
## PriceFormat: display of integer ticks and lots.


func test_price_has_two_decimals() -> void:
	assert_str(PriceFormat.price(10050)).is_equal("100.50")
	assert_str(PriceFormat.price(100)).is_equal("1.00")
	assert_str(PriceFormat.price(7)).is_equal("0.07")
	assert_str(PriceFormat.price(0)).is_equal("0.00")


func test_price_handles_negatives_and_extremes() -> void:
	assert_str(PriceFormat.price(-250)).is_equal("-2.50")
	assert_str(PriceFormat.price(9223372036854775807)).is_equal("92233720368547758.07")


func test_qty_groups_thousands() -> void:
	assert_str(PriceFormat.qty(0)).is_equal("0")
	assert_str(PriceFormat.qty(999)).is_equal("999")
	assert_str(PriceFormat.qty(1000)).is_equal("1,000")
	assert_str(PriceFormat.qty(1234567)).is_equal("1,234,567")
	assert_str(PriceFormat.qty(-12500)).is_equal("-12,500")


func test_names() -> void:
	assert_str(PriceFormat.side(Simulation.SIDE_SELL)).is_equal("sell")
	assert_str(PriceFormat.order_type(Simulation.ORDER_TYPE_STOP_LIMIT)).is_equal("stop-limit")
	assert_str(PriceFormat.order_status(Simulation.ORDER_STATUS_RESTING)).is_equal(
			"resting on the book")


func test_parse_price_is_exact() -> void:
	assert_int(PriceFormat.parse_price("100.25")).is_equal(10025)
	assert_int(PriceFormat.parse_price("100.2")).is_equal(10020)
	assert_int(PriceFormat.parse_price("100")).is_equal(10000)
	assert_int(PriceFormat.parse_price(" 0.07 ")).is_equal(7)
	assert_int(PriceFormat.parse_price(".5")).is_equal(50)
	# 100.07 * 100 is not exact in floating point; string parsing is.
	assert_int(PriceFormat.parse_price("100.07")).is_equal(10007)


func test_parse_price_rejects_invalid_text() -> void:
	var invalid: Array[String] = [
		"", "abc", "1.2.3", "1.234", "-5", "1e3", "12a.00", "1.", "9999999999999999",
	]
	for text: String in invalid:
		assert_int(PriceFormat.parse_price(text)).override_failure_message(text).is_equal(-1)
