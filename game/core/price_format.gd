class_name PriceFormat
extends RefCounted
## Display formatting for integer market values. Presentation only: prices stay integer ticks
## everywhere else.

## Displayed decimal places: one tick is 0.01 of a currency unit.
const DECIMALS: int = 2


## Formats a price in ticks as a decimal string, e.g. 10050 -> "100.50".
static func price(ticks: int) -> String:
	# Split the digit string rather than dividing, so the result is exact for any int64.
	var digits: String = str(absi(ticks)).pad_zeros(DECIMALS + 1)
	var text: String = "%s.%s" % [digits.left(-DECIMALS), digits.right(DECIMALS)]
	return "-" + text if ticks < 0 else text


## Parses a decimal price such as "100.25", "100.2" or "100" into ticks, exactly (no floating
## point). Returns -1 if [param text] is not a non-negative price with at most [constant DECIMALS]
## decimal places.
static func parse_price(text: String) -> int:
	var trimmed: String = text.strip_edges()
	var parts: PackedStringArray = trimmed.split(".")
	if trimmed.is_empty() or parts.size() > 2:
		return -1
	var whole: String = parts[0] if not parts[0].is_empty() else "0"
	var has_point: bool = parts.size() == 2
	var fraction: String = parts[1] if has_point else ""
	if not _is_digits(whole) or whole.length() > 15:
		return -1
	if has_point and (not _is_digits(fraction) or fraction.length() > DECIMALS):
		return -1  # also rejects "1." (a point with no digits after it)
	return (whole + fraction.rpad(DECIMALS, "0")).to_int()


## Formats a quantity with thousands separators, e.g. 12500 -> "12,500".
static func qty(lots: int) -> String:
	var digits: String = str(absi(lots))
	var grouped: String = ""
	var count: int = 0
	for i: int in range(digits.length() - 1, -1, -1):
		if count > 0 and count % 3 == 0:
			grouped = "," + grouped
		grouped = digits[i] + grouped
		count += 1
	return ("-" if lots < 0 else "") + grouped


## Display name of a side.
static func side(value: Simulation.Side) -> String:
	return "buy" if value == Simulation.SIDE_BUY else "sell"


## Display name of an order type.
static func order_type(value: Simulation.OrderType) -> String:
	match value:
		Simulation.ORDER_TYPE_MARKET:
			return "market"
		Simulation.ORDER_TYPE_LIMIT:
			return "limit"
		Simulation.ORDER_TYPE_STOP:
			return "stop"
		Simulation.ORDER_TYPE_STOP_LIMIT:
			return "stop-limit"
	return "unknown"


## Display name of an order status.
static func order_status(value: Simulation.OrderStatus) -> String:
	match value:
		Simulation.ORDER_STATUS_PENDING_STOP:
			return "waiting for stop price"
		Simulation.ORDER_STATUS_TRIGGERED:
			return "stop triggered"
		Simulation.ORDER_STATUS_MATCHING:
			return "trading"
		Simulation.ORDER_STATUS_RESTING:
			return "resting on the book"
	return "unknown"


static func _is_digits(text: String) -> bool:
	if text.is_empty():
		return false
	for character: String in text:
		if character < "0" or character > "9":
			return false
	return true
