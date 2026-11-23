extends GdUnitTestSuite
## Narrator and EntityRef: narration lines and their clickable links.


func test_every_event_type_produces_a_line_with_its_reason() -> void:
	var market: MockMarket = MockMarket.build()
	var palette: NarrationPalette = NarrationPalette.new()
	var seen: Dictionary[int, bool] = {}
	for event: SimEvent in market.events:
		var line: String = Narrator.describe(event, palette)
		assert_str(line).is_not_empty()
		assert_str(line).contains(Simulation.reason_text(event.reason))
		seen[event.type] = true
	# The sample market exercises placing, resting, trading, filling and price changes.
	for type: int in [Simulation.EVENT_ORDER_PLACED, Simulation.EVENT_ORDER_RESTED,
			Simulation.EVENT_MATCH, Simulation.EVENT_FILL, Simulation.EVENT_PRICE_CHANGE]:
		assert_bool(seen.has(type)).is_true()


func test_trade_line_links_both_orders_accounts_and_price() -> void:
	var trade: SimEvent = SimEvent.new()
	trade.type = Simulation.EVENT_MATCH
	trade.reason = Simulation.REASON_TRADE
	trade.side = Simulation.SIDE_SELL
	trade.order_id = 12
	trade.account = 3
	trade.counter_order_id = 7
	trade.counter_account = 9
	trade.price = 10025
	trade.qty = 1500
	var line: String = Narrator.describe(trade, NarrationPalette.new())
	assert_str(line).contains("[url=order:12]#12[/url]")
	assert_str(line).contains("[url=order:7]#7[/url]")
	assert_str(line).contains("[url=account:3]acct 3[/url]")
	assert_str(line).contains("[url=account:9]acct 9[/url]")
	assert_str(line).contains("[url=price:10025]100.25[/url]")
	assert_str(line).contains("sold")
	assert_str(line).contains("1,500")


func test_entity_ref_round_trips_and_rejects_garbage() -> void:
	var kinds: Array[EntityRef.Kind] = [
		EntityRef.Kind.ORDER, EntityRef.Kind.ACCOUNT, EntityRef.Kind.PRICE,
	]
	for kind: EntityRef.Kind in kinds:
		var ref: EntityRef = EntityRef.make(kind, 42)
		assert_bool(ref.equals(EntityRef.from_meta(ref.to_meta()))).is_true()
	assert_object(EntityRef.from_meta("order")).is_null()
	assert_object(EntityRef.from_meta("order:abc")).is_null()
	assert_object(EntityRef.from_meta("planet:4")).is_null()
