extends GdUnitTestSuite
## The app shell, chapter list and lesson screen, driven through their own buttons and signals.

const CHAPTER_2: String = "res://scenarios/chapters/02_first_trade.json"

## Concrete type: awaiting simulate_frames through the GdUnitSceneRunner interface looks
## redundant to the analyzer even though the implementation awaits (see test_main_screen.gd).
var _runner: GdUnitSceneRunnerImpl


func _app() -> App:
	_runner = scene_runner("res://app/app.tscn") as GdUnitSceneRunnerImpl
	return _runner.scene() as App


func test_app_starts_at_the_chapter_list() -> void:
	var app: App = _app()
	await _runner.simulate_frames(2, 16)
	var menu: ChapterSelect = app.current_screen() as ChapterSelect
	assert_object(menu).is_not_null()
	assert_int(menu.chapter_count()).is_greater_equal(3)


func test_chapter_plays_through_the_lesson_panel() -> void:
	var app: App = _app()
	app.show_lesson(CHAPTER_2)
	await _runner.simulate_frames(2, 16)
	var lesson: LessonScreen = app.current_screen() as LessonScreen
	assert_object(lesson).is_not_null()
	var panel: LessonPanel = lesson.find_child("LessonPanel") as LessonPanel
	var next: Button = panel.find_child("NextButton") as Button
	var do_it: Button = panel.find_child("DoItButton") as Button
	var market: MainScreen = lesson.find_child("MainScreen") as MainScreen
	var controls: SimControls = market.find_child("SimControls") as SimControls

	# Chapter 2 enables the ticket but not the sandbox-only seed button or continuous running.
	assert_bool((market.find_child("OrderTicket") as Control).visible).is_true()
	assert_bool((market.find_child("SeedButton") as Control).visible).is_false()
	assert_bool((market.find_child("RunButton") as Control).visible).is_false()

	var steps: int = 0
	while not lesson.runner().is_finished() and steps < 200:
		steps += 1
		if not next.disabled:
			next.pressed.emit()
		elif do_it.visible:
			do_it.pressed.emit()
		else:
			controls.step_event_pressed.emit()
		await _runner.simulate_frames(1, 16)
	assert_bool(lesson.runner().is_finished()).is_true()
	assert_bool((panel.find_child("ChaptersButton") as Control).visible).is_true()
	assert_str((panel.find_child("ProgressLabel") as Label).text).is_equal("Chapter complete")

	# Participants appear by name in the narration.
	var narration: RichTextLabel = market.find_child("NarrationPanel").find_child("Text") \
			as RichTextLabel
	assert_str(narration.get_parsed_text()).contains("Ledger Lane Markets")
	assert_str(narration.get_parsed_text()).contains("You")


func test_menu_and_sandbox_navigation() -> void:
	var app: App = _app()
	app.show_sandbox()
	await _runner.simulate_frames(2, 16)
	var sandbox: MainScreen = app.current_screen() as MainScreen
	assert_object(sandbox).is_not_null()
	assert_bool((sandbox.find_child("SeedButton") as Control).visible).is_true()
	assert_bool((sandbox.find_child("RunButton") as Control).visible).is_true()
	sandbox.menu_requested.emit()
	await _runner.simulate_frames(2, 16)
	assert_object(app.current_screen() as ChapterSelect).is_not_null()


func test_comparison_table_appears_in_chapter_3() -> void:
	var app: App = _app()
	app.show_lesson("res://scenarios/chapters/03_thin_vs_deep.json")
	await _runner.simulate_frames(2, 16)
	var lesson: LessonScreen = app.current_screen() as LessonScreen
	var panel: LessonPanel = lesson.find_child("LessonPanel") as LessonPanel
	var next: Button = panel.find_child("NextButton") as Button
	var controls: SimControls = lesson.find_child("SimControls") as SimControls
	var steps: int = 0
	while lesson.runner().current_beat().id != "compare" and steps < 200:
		steps += 1
		if not next.disabled:
			next.pressed.emit()
		else:
			controls.step_tick_pressed.emit()
		await _runner.simulate_frames(1, 16)
	var table: Tree = panel.find_child("Comparison") as Tree
	assert_bool(table.visible).is_true()
	assert_int(table.columns).is_equal(3)
	assert_str(table.get_column_title(1)).is_equal("Thin book")
	assert_str(table.get_column_title(2)).is_equal("Deep book")
