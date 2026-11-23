extends GdUnitTestSuite
## OrderLadderSeed: the "Seed book" ladder.


func test_ladder_is_symmetric_around_the_mid() -> void:
	var book_seed: OrderLadderSeed = OrderLadderSeed.new()
	book_seed.mid_price = 1000
	book_seed.levels = 3
	book_seed.spacing = 10
	book_seed.base_qty = 5
	book_seed.qty_step = 2
	var orders: Array[OrderLadderSeed.SeedOrder] = book_seed.orders()
	assert_int(orders.size()).is_equal(6)
	assert_int(orders[0].side).is_equal(Simulation.SIDE_BUY)
	assert_int(orders[0].price).is_equal(990)
	assert_int(orders[1].side).is_equal(Simulation.SIDE_SELL)
	assert_int(orders[1].price).is_equal(1010)
	assert_int(orders[4].price).is_equal(970)
	assert_int(orders[4].qty).is_equal(9)
	assert_int(orders[0].account).is_equal(book_seed.bid_account)
	assert_int(orders[1].account).is_equal(book_seed.ask_account)


func test_seeded_book_rests_without_trading() -> void:
	var simulation: Simulation = auto_free(Simulation.new())
	var book_seed: OrderLadderSeed = load("res://data/default_book_seed.tres") as OrderLadderSeed
	for order: OrderLadderSeed.SeedOrder in book_seed.orders():
		simulation.submit_order(order.account, order.side, Simulation.ORDER_TYPE_LIMIT, order.qty,
				order.price)
	assert_bool(simulation.step_tick()).is_true()
	assert_int(simulation.get_trade_count()).is_equal(0)
	assert_int(simulation.get_best_bid()).is_equal(book_seed.mid_price - book_seed.spacing)
	assert_int(simulation.get_best_ask()).is_equal(book_seed.mid_price + book_seed.spacing)
	var bids: PackedInt64Array = simulation.get_depth(Simulation.SIDE_BUY, 100)
	assert_int(bids.size()).is_equal(book_seed.levels * Simulation.DEPTH_STRIDE)
