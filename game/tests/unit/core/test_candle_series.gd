extends GdUnitTestSuite
## CandleSeries: grouping trades into fixed tick ranges.


func test_trades_in_one_range_form_one_candle() -> void:
	var series: CandleSeries = CandleSeries.new(5)
	series.add_trade(0, 100, 2)
	series.add_trade(1, 104, 3)
	series.add_trade(3, 98, 1)
	series.add_trade(4, 101, 4)
	assert_int(series.candles.size()).is_equal(1)
	var candle: Candle = series.candles[0]
	assert_int(candle.start_tick).is_equal(0)
	assert_int(candle.open).is_equal(100)
	assert_int(candle.high).is_equal(104)
	assert_int(candle.low).is_equal(98)
	assert_int(candle.close).is_equal(101)
	assert_int(candle.volume).is_equal(10)
	assert_int(candle.trade_count).is_equal(4)
	assert_bool(candle.is_up()).is_true()
	assert_int(series.last_price).is_equal(101)


func test_new_range_starts_a_new_candle_and_gaps_are_skipped() -> void:
	var series: CandleSeries = CandleSeries.new(5)
	series.add_trade(2, 100, 1)
	series.add_trade(5, 99, 1)
	series.add_trade(17, 97, 1)
	assert_int(series.candles.size()).is_equal(3)
	assert_int(series.candles[1].start_tick).is_equal(5)
	assert_int(series.candles[2].start_tick).is_equal(15)
	assert_bool(series.candles[1].is_up()).is_true()  # open == close counts as up


func test_rebuild_matches_incremental() -> void:
	var market: MockMarket = MockMarket.build()
	var incremental: CandleSeries = CandleSeries.new(2)
	for trade: SimEvent in market.trades():
		incremental.add_trade(trade.tick, trade.price, trade.qty)
	var rebuilt: CandleSeries = CandleSeries.new(2)
	rebuilt.add_trade(0, 1, 1)  # stale content must be discarded
	rebuilt.rebuild(market.trades())
	assert_int(rebuilt.candles.size()).is_equal(incremental.candles.size())
	for i: int in range(rebuilt.candles.size()):
		assert_int(rebuilt.candles[i].close).is_equal(incremental.candles[i].close)
		assert_int(rebuilt.candles[i].volume).is_equal(incremental.candles[i].volume)
	assert_int(rebuilt.last_price).is_equal(market.last_price)


func test_latest_returns_the_newest_candles() -> void:
	var series: CandleSeries = CandleSeries.new(1)
	for tick: int in range(10):
		series.add_trade(tick, 100 + tick, 1)
	var latest: Array[Candle] = series.latest(3)
	assert_int(latest.size()).is_equal(3)
	assert_int(latest[0].start_tick).is_equal(7)
	assert_int(series.latest(50).size()).is_equal(10)
