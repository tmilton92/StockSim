class_name Narrator
extends RefCounted
## Writes one line of plain-English narration (BBCode) per simulation event.
##
## Orders, accounts and prices are links ([EntityRef] meta strings) so panels can highlight
## and inspect them. Every line ends with the core's fixed explanation of the event's reason.


## BBCode line for [param event]. Accounts are shown by name when [param names] has one.
static func describe(event: SimEvent, palette: NarrationPalette,
		names: AccountNames = null) -> String:
	var body: String = _body(event, palette, names)
	var reason: String = Simulation.reason_text(event.reason)
	var line: String = "%s %s" % [_colored("t%d" % event.tick, palette.muted), body]
	if not reason.is_empty():
		line += " " + _colored("- " + reason, palette.muted)
	return line


static func order_link(id: int) -> String:
	return _link(EntityRef.make(EntityRef.Kind.ORDER, id), "#%d" % id)


static func account_link(id: int, names: AccountNames = null) -> String:
	var text: String = names.label(id) if names != null else "acct %d" % id
	return _link(EntityRef.make(EntityRef.Kind.ACCOUNT, id), text)


static func price_link(ticks: int) -> String:
	return _link(EntityRef.make(EntityRef.Kind.PRICE, ticks), PriceFormat.price(ticks))


static func _body(event: SimEvent, palette: NarrationPalette, names: AccountNames) -> String:
	var order: String = order_link(event.order_id)
	var account: String = account_link(event.account, names)
	var side: String = _colored(PriceFormat.side(event.side), palette.for_side(event.side))
	match event.type:
		Simulation.EVENT_ORDER_PLACED:
			return "%s placed by %s: %s %s %s%s" % [
				order, account, PriceFormat.order_type(event.order_type), side,
				PriceFormat.qty(event.qty), _order_prices(event)]
		Simulation.EVENT_ORDER_REJECTED:
			return "%s from %s rejected." % [order, account]
		Simulation.EVENT_ORDER_RESTED:
			return "%s rests on the book: %s %s @ %s." % [
				order, side, PriceFormat.qty(event.qty), price_link(event.price)]
		Simulation.EVENT_ORDER_CANCELLED:
			return "%s cancelled: %s removed." % [order, PriceFormat.qty(event.qty)]
		Simulation.EVENT_ORDER_MODIFIED:
			return "%s modified to %s @ %s (was %s @ %s)." % [
				order, PriceFormat.qty(event.remaining), price_link(event.price),
				PriceFormat.qty(event.qty), price_link(event.aux_price)]
		Simulation.EVENT_COMMAND_REJECTED:
			return "Request on %s from %s rejected." % [order, account]
		Simulation.EVENT_STOP_TRIGGERED:
			return "%s stop at %s reached by a trade at %s." % [
				order, price_link(event.aux_price), price_link(event.price)]
		Simulation.EVENT_MATCH:
			var verb: String = "bought" if event.side == Simulation.SIDE_BUY else "sold"
			return "%s %s (%s) %s %s @ %s from %s (%s)." % [
				_colored("Trade:", palette.emphasis), order, account,
				_colored(verb, palette.for_side(event.side)), PriceFormat.qty(event.qty),
				price_link(event.price), order_link(event.counter_order_id),
				account_link(event.counter_account, names)]
		Simulation.EVENT_FILL:
			return "%s filled %s @ %s, %s left." % [
				order, PriceFormat.qty(event.qty), price_link(event.price),
				PriceFormat.qty(event.remaining)]
		Simulation.EVENT_PRICE_CHANGE:
			if event.aux_price == 0:
				return "First trade sets the price at %s." % price_link(event.price)
			return "Last price %s -> %s." % [price_link(event.aux_price), price_link(event.price)]
	return Simulation.event_type_name(event.type)


static func _order_prices(event: SimEvent) -> String:
	var text: String = ""
	if event.price != 0:
		text += " @ " + price_link(event.price)
	if event.aux_price != 0:
		text += " (stop " + price_link(event.aux_price) + ")"
	return text + "."


static func _link(ref: EntityRef, text: String) -> String:
	return "[url=%s]%s[/url]" % [ref.to_meta(), text]


static func _colored(text: String, color: Color) -> String:
	return "[color=#%s]%s[/color]" % [color.to_html(false), text]
