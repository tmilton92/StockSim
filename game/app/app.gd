class_name App
extends Control
## The application shell: shows the chapter list, a lesson, or the sandbox, one at a time.

const CHAPTER_SELECT: PackedScene = preload("res://ui/chapter_select/chapter_select.tscn")
const LESSON_SCREEN: PackedScene = preload("res://main/lesson_screen.tscn")
const SANDBOX_SCREEN: PackedScene = preload("res://main/main_screen.tscn")
const SANDBOX_SCENARIO: String = "res://scenarios/sandbox.json"

@onready var _screens: Control = %Screens


func _ready() -> void:
	show_menu()


## The screen currently shown.
func current_screen() -> Control:
	return _screens.get_child(0) as Control if _screens.get_child_count() > 0 else null


func show_menu() -> void:
	var menu: ChapterSelect = CHAPTER_SELECT.instantiate() as ChapterSelect
	_replace_screen(menu)
	menu.chapter_chosen.connect(show_lesson)
	menu.sandbox_chosen.connect(show_sandbox)


func show_lesson(path: String) -> void:
	var lesson: LessonScreen = LESSON_SCREEN.instantiate() as LessonScreen
	_replace_screen(lesson)
	lesson.menu_requested.connect(show_menu)
	if lesson.start(path) != OK:
		show_menu()


func show_sandbox() -> void:
	var sandbox: MainScreen = SANDBOX_SCREEN.instantiate() as MainScreen
	if SimBridge.load_scenario(SANDBOX_SCENARIO) != OK:
		push_error("App: cannot load %s" % SANDBOX_SCENARIO)
	_replace_screen(sandbox)
	sandbox.apply_features(PackedStringArray())
	sandbox.set_account_names(null)
	sandbox.menu_requested.connect(show_menu)


func _replace_screen(screen: Control) -> void:
	SimBridge.simulation.pause()
	for child: Node in _screens.get_children():
		_screens.remove_child(child)
		child.queue_free()
	_screens.add_child(screen)
