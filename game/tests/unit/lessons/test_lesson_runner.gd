extends GdUnitTestSuite
## LessonRunner and the chapter files: every chapter loads, plays headless to completion, and
## teaches what its text claims (checked against the core's actual output).

const CHAPTER_DIR: String = "res://scenarios/chapters"
## Safety bound on learner actions per chapter.
const MAX_ACTIONS: int = 500

var _runner: LessonRunner
var _comparison: Array[ImpactRecord] = []


func before_test() -> void:
	_runner = auto_free(LessonRunner.new())
	add_child(_runner)
	_comparison = []
	_runner.comparison_shown.connect(_on_comparison_shown)


func _chapter_paths() -> PackedStringArray:
	var paths: PackedStringArray = []
	for file: String in DirAccess.get_files_at(CHAPTER_DIR):
		if file.ends_with(".json"):
			paths.append(CHAPTER_DIR.path_join(file))
	paths.sort()
	return paths


func _load(path: String) -> ChapterDef:
	var result: ChapterLoader.LoadResult = ChapterLoader.load_file(path)
	assert_bool(result.errors.is_empty()).override_failure_message(
			"\n".join(result.errors)).is_true()
	return result.chapter


## Acts as the learner until the chapter finishes: Next when allowed, Step while waiting for an
## event, "Do it for me" when an order is asked for.
func _play(chapter: ChapterDef) -> int:
	assert_int(_runner.start(chapter)).is_equal(OK)
	var actions: int = 0
	while not _runner.is_finished() and actions < MAX_ACTIONS:
		actions += 1
		if _runner.is_satisfied():
			_runner.advance()
		elif _runner.current_beat().wait.kind == LessonWait.Kind.LEARNER_ORDER:
			_runner.do_suggested()
		else:
			SimBridge.simulation.step_event()
			SimBridge.poll()
	return actions


func test_chapters_exist_and_are_numbered_in_order() -> void:
	var paths: PackedStringArray = _chapter_paths()
	assert_int(paths.size()).is_greater_equal(3)
	for i: int in range(paths.size()):
		assert_int(_load(paths[i]).chapter).is_equal(i + 1)


func test_every_chapter_plays_to_completion() -> void:
	for path: String in _chapter_paths():
		var chapter: ChapterDef = _load(path)
		var actions: int = _play(chapter)
		assert_bool(_runner.is_finished()).override_failure_message(
				"%s did not finish within %d learner actions" % [path, actions]).is_true()


func test_chapter_1_builds_a_quoted_market_without_a_price() -> void:
	_play(_load(CHAPTER_DIR.path_join("01_market_creation.json")))
	var simulation: Simulation = SimBridge.simulation
	assert_int(simulation.get_best_bid()).is_equal(9990)
	assert_int(simulation.get_best_ask()).is_equal(10010)
	assert_bool(simulation.has_last_price()).is_false()  # nothing has traded
	assert_int(simulation.get_trade_count()).is_equal(0)


func test_chapter_2_price_is_the_last_trade() -> void:
	_play(_load(CHAPTER_DIR.path_join("02_first_trade.json")))
	var trades: Array[SimEvent] = SimBridge.event_log.trades()
	assert_int(trades.size()).is_equal(2)
	assert_int(trades[0].price).is_equal(10010)  # the learner bought at the ask
	assert_int(trades[0].account).is_equal(10)
	assert_int(trades[1].price).is_equal(9990)  # the seller hit the bid
	assert_int(SimBridge.simulation.get_last_price()).is_equal(9990)


func test_chapter_3_compares_thin_and_deep_books() -> void:
	_play(_load(CHAPTER_DIR.path_join("03_thin_vs_deep.json")))
	assert_int(_comparison.size()).is_equal(2)
	var thin: ImpactRecord = _comparison[0]
	var deep: ImpactRecord = _comparison[1]
	assert_str(thin.label).is_equal("Thin book")
	assert_int(thin.filled_qty).is_equal(60)
	assert_int(thin.levels).is_equal(5)
	assert_int(thin.reference_price).is_equal(10010)
	assert_int(thin.worst_fill).is_equal(10200)
	assert_int(thin.price_move()).is_equal(190)
	assert_int(deep.filled_qty).is_equal(60)
	assert_int(deep.levels).is_equal(1)
	assert_int(deep.price_move()).is_equal(0)
	assert_bool(thin.notional > deep.notional).is_true()  # the thin book cost more


func test_event_wait_blocks_until_the_event_happens() -> void:
	var chapter: ChapterDef = _load(CHAPTER_DIR.path_join("02_first_trade.json"))
	assert_int(_runner.start(chapter)).is_equal(OK)
	_runner.advance()  # recap -> learner order
	assert_bool(_runner.is_satisfied()).is_false()
	_runner.advance()  # ignored while waiting
	assert_str(_runner.current_beat().id).is_equal("learner_buys")
	_runner.do_suggested()
	assert_bool(_runner.is_satisfied()).is_true()
	_runner.advance()  # -> process (waits for the trade)
	assert_bool(_runner.is_satisfied()).is_false()
	SimBridge.simulation.step_event()  # accepted
	SimBridge.poll()
	assert_bool(_runner.is_satisfied()).is_false()
	SimBridge.simulation.step_event()  # trades
	SimBridge.poll()
	assert_bool(_runner.is_satisfied()).is_true()


func test_learner_order_from_the_ticket_counts_and_is_tagged() -> void:
	var chapter: ChapterDef = _load(CHAPTER_DIR.path_join("02_first_trade.json"))
	_runner.start(chapter)
	_runner.advance()
	var order_id: int = SimBridge.simulation.submit_order(10, Simulation.SIDE_BUY,
			Simulation.ORDER_TYPE_MARKET, 10)
	_runner.learner_submitted(order_id, Simulation.SIDE_BUY, 10,
			SimBridge.best_opposite_quote(Simulation.SIDE_BUY))
	assert_bool(_runner.is_satisfied()).is_true()
	assert_int(_runner.order_for_tag("first")).is_equal(order_id)


func _on_comparison_shown(records: Array[ImpactRecord]) -> void:
	_comparison = records.duplicate()
