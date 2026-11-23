extends GdUnitTestSuite
## ChapterLoader: strict parsing of the scenario "lesson" object (format version 2).


func _scenario(lesson: Dictionary) -> Dictionary:
	return {"format": "stocksim-scenario", "format_version": 2, "lesson": lesson}


func _minimal_lesson() -> Dictionary:
	return {
		"chapter": 1,
		"title": "Test",
		"summary": "A test lesson.",
		"learner_account": 10,
		"features": ["ladder", "narration"],
		"participants": [{"account": 1, "name": "Northwind Robotics", "role": "Issuer"}],
		"setup": [
			{"do": "submit", "account": 1, "side": "sell", "type": "limit", "qty": 100,
				"price": "100.50", "tag": "supply"},
			{"do": "tick"},
		],
		"beats": [
			{"id": "intro", "text": "Hello."},
			{"id": "wait_trade", "text": "Step until a trade.",
				"wait": {"kind": "event", "event": "match", "tag": "supply", "hint": "Step"}},
			{"id": "you_buy", "text": "Buy something.",
				"wait": {"kind": "learner_order",
					"suggested": {"side": "buy", "type": "market", "qty": 5, "tag": "mine"}}},
			{"id": "compare", "text": "Compare.", "actions": [
				{"do": "record", "tag": "mine", "label": "Mine"},
				{"do": "highlight", "price": "100.50"},
				{"do": "ladder", "account": 1, "side": "buy", "from": "99.00", "step": "0.10",
					"levels": 3, "qty": 10, "qty_step": 5},
				{"do": "show_comparison"},
			]},
		],
	}


## The dictionary at [param index] of the array stored under [param key].
func _item(container: Dictionary, key: String, index: int) -> Dictionary:
	var items: Array = container[key]
	return items[index]


func _errors(lesson: Dictionary) -> String:
	return "\n".join(ChapterLoader.from_scenario(_scenario(lesson), "res://test.json").errors)


func test_valid_lesson_builds_a_chapter() -> void:
	var result: ChapterLoader.LoadResult = ChapterLoader.from_scenario(
			_scenario(_minimal_lesson()), "res://test.json")
	assert_bool(result.errors.is_empty()).override_failure_message(
			"\n".join(result.errors)).is_true()
	var chapter: ChapterDef = result.chapter
	assert_int(chapter.chapter).is_equal(1)
	assert_str(chapter.source_path).is_equal("res://test.json")
	assert_bool(chapter.has_feature("ladder")).is_true()
	assert_bool(chapter.has_feature("ticket")).is_false()
	assert_int(chapter.setup.size()).is_equal(2)
	assert_int(chapter.setup[0].price).is_equal(10050)
	assert_int(chapter.setup[0].side).is_equal(Simulation.SIDE_SELL)
	assert_int(chapter.beats.size()).is_equal(4)
	assert_int(chapter.beats[0].wait.kind).is_equal(LessonWait.Kind.NEXT)
	assert_int(chapter.beats[1].wait.kind).is_equal(LessonWait.Kind.EVENT)
	assert_int(chapter.beats[1].wait.event_type).is_equal(Simulation.EVENT_MATCH)
	var suggested: LessonAction = chapter.beats[2].wait.suggested
	assert_int(suggested.order_type).is_equal(Simulation.ORDER_TYPE_MARKET)
	assert_int(suggested.qty).is_equal(5)
	var ladder: LessonAction = chapter.beats[3].actions[2]
	assert_int(ladder.type).is_equal(LessonAction.Type.LADDER)
	assert_int(ladder.price_step).is_equal(10)
	assert_str(chapter.account_names().label(1)).is_equal("Northwind Robotics")
	assert_str(chapter.account_names().label(10)).is_equal("You")
	assert_str(chapter.account_names().label(7)).is_equal("acct 7")


func test_unknown_and_missing_keys_are_reported_with_paths() -> void:
	var lesson: Dictionary = _minimal_lesson()
	lesson["colour"] = "blue"
	lesson.erase("title")
	_item(lesson, "beats", 0)["txt"] = "typo"
	var errors: String = _errors(lesson)
	assert_str(errors).contains("lesson: unknown key \"colour\"")
	assert_str(errors).contains("lesson: missing \"title\"")
	assert_str(errors).contains("lesson.beats[0]: unknown key \"txt\"")


func test_prices_must_be_exact_decimal_strings() -> void:
	var lesson: Dictionary = _minimal_lesson()
	_item(lesson, "setup", 0)["price"] = 100.5
	assert_str(_errors(lesson)).contains("lesson.setup[0].price: must be a positive price string")
	_item(lesson, "setup", 0)["price"] = "100.505"
	assert_str(_errors(lesson)).is_not_empty()


func test_order_fields_must_match_the_order_type() -> void:
	var lesson: Dictionary = _minimal_lesson()
	_item(lesson, "setup", 0)["type"] = "market"  # still has a price
	assert_str(_errors(lesson)).contains("lesson.setup[0].price: not used by this order type")


func test_tags_must_be_set_before_use() -> void:
	var lesson: Dictionary = _minimal_lesson()
	_item(_item(lesson, "beats", 3), "actions", 0)["tag"] = "never_set"
	assert_str(_errors(lesson)).contains("tag \"never_set\" is used before")


func test_unknown_values_are_rejected() -> void:
	var lesson: Dictionary = _minimal_lesson()
	lesson["features"] = ["ladder", "hologram"]
	var wait: Dictionary = _item(lesson, "beats", 1)["wait"]
	wait["event"] = "explosion"
	var actions: Array = _item(lesson, "beats", 3)["actions"]
	actions[1] = {"do": "dance"}
	var errors: String = _errors(lesson)
	assert_str(errors).contains("unknown feature \"hologram\"")
	assert_str(errors).contains("lesson.beats[1].wait.event: must be one of")
	assert_str(errors).contains("lesson.beats[3].actions[1].do: must be one of")


func test_version_1_scenarios_have_no_lesson() -> void:
	var scenario: Dictionary = _scenario(_minimal_lesson())
	scenario["format_version"] = 1
	var result: ChapterLoader.LoadResult = ChapterLoader.from_scenario(scenario, "res://x.json")
	assert_object(result.chapter).is_null()
	assert_str("\n".join(result.errors)).contains("format_version 2")


func test_integers_reject_fractions() -> void:
	var lesson: Dictionary = _minimal_lesson()
	_item(lesson, "setup", 0)["qty"] = 2.5
	assert_str(_errors(lesson)).contains("lesson.setup[0].qty: must be an integer")


func test_errors_are_all_reported_at_once() -> void:
	var lesson: Dictionary = _minimal_lesson()
	lesson.erase("summary")
	_item(lesson, "setup", 0)["qty"] = -1
	_item(lesson, "beats", 0).erase("text")
	var errors: PackedStringArray = ChapterLoader.from_scenario(
			_scenario(lesson), "res://test.json").errors
	assert_int(errors.size()).is_greater_equal(3)
