class_name LessonScreen
extends Control
## A chapter: the lesson panel beside the market screen, driven by a [LessonRunner].
##
## Wires the three together: runner signals update the panel and highlight in the market;
## panel buttons and learner orders feed back into the runner.

## The learner wants to return to the chapter list.
signal menu_requested

@onready var _panel: LessonPanel = %LessonPanel
@onready var _market: MainScreen = %MainScreen
@onready var _runner: LessonRunner = %LessonRunner
var _chapter: ChapterDef = null


func _ready() -> void:
	_runner.beat_started.connect(_on_beat_started)
	_runner.wait_state_changed.connect(_on_wait_state_changed)
	_runner.highlight_requested.connect(_market.select)
	_runner.comparison_shown.connect(_panel.show_comparison)
	_runner.finished.connect(_panel.show_finished)
	_panel.next_pressed.connect(_runner.advance)
	_panel.do_it_pressed.connect(_on_do_it_pressed)
	_panel.chapters_pressed.connect(menu_requested.emit)
	_market.learner_order_submitted.connect(_runner.learner_submitted)
	_market.menu_requested.connect(menu_requested.emit)


## Loads and starts the chapter in the scenario file at [param path].
func start(path: String) -> Error:
	var result: ChapterLoader.LoadResult = ChapterLoader.load_file(path)
	if result.chapter == null:
		for message: String in result.errors:
			push_error(message)
		return ERR_INVALID_DATA
	_chapter = result.chapter
	_market.apply_features(_chapter.features)
	_market.set_learner_account(_chapter.learner_account)
	var error: Error = _runner.start(_chapter)
	# Names are applied after the scenario loads, so the rebuilt narration uses them.
	_market.set_account_names(_chapter.account_names())
	return error


func runner() -> LessonRunner:
	return _runner


func _on_beat_started(index: int, beat: BeatDef) -> void:
	var title: String = "Chapter %d: %s" % [_chapter.chapter, _chapter.title]
	_panel.show_beat(title, index, _chapter.beats.size(), beat)


func _on_wait_state_changed(satisfied: bool) -> void:
	var beat: BeatDef = _runner.current_beat()
	if beat != null:
		_panel.set_waiting(beat, satisfied)


func _on_do_it_pressed() -> void:
	_runner.do_suggested()
