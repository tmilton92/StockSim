extends Node
## Visual check of the app shell: the chapter list, or a chapter played to a given beat.
##
## Run as a scene, with a window:
##   godot --path game --resolution 1440x900 res://tests/visual/app_capture.tscn -- OUT.png
##   godot ... res://tests/visual/app_capture.tscn -- OUT.png CHAPTER_JSON BEAT_ID
## where CHAPTER_JSON is e.g. res://scenarios/chapters/03_thin_vs_deep.json.
## Plays like a learner: Next when allowed, "Do it for me" for orders, Tick while waiting.

const SETTLE_FRAMES: int = 40
const MAX_ACTIONS: int = 300

@onready var _app: App = %App
var _frames: int = 0
var _output: String = ""


func _ready() -> void:
	var args: PackedStringArray = OS.get_cmdline_user_args()
	if args.is_empty():
		push_error("app_capture: pass the output PNG path after --")
		get_tree().quit(1)
		return
	_output = args[0]
	if args.size() == 3:
		_app.show_lesson(args[1])
		_play_to(args[2])


func _process(_delta: float) -> void:
	_frames += 1
	if _frames < SETTLE_FRAMES:
		return
	var error: Error = get_viewport().get_texture().get_image().save_png(_output)
	print("CAPTURE_SAVED" if error == OK else "CAPTURE_FAILED %d" % error)
	get_tree().quit()


func _play_to(beat_id: String) -> void:
	var lesson: LessonScreen = _app.current_screen() as LessonScreen
	if lesson == null:
		push_error("app_capture: the chapter did not open")
		return
	var runner: LessonRunner = lesson.runner()
	var actions: int = 0
	while runner.current_beat() != null and runner.current_beat().id != beat_id \
			and not runner.is_finished() and actions < MAX_ACTIONS:
		actions += 1
		if runner.is_satisfied():
			runner.advance()
		elif runner.current_beat().wait.kind == LessonWait.Kind.LEARNER_ORDER:
			runner.do_suggested()
		else:
			SimBridge.simulation.step_tick()
			SimBridge.poll()
	if runner.current_beat() == null or runner.current_beat().id != beat_id:
		push_error("app_capture: beat \"%s\" was not reached" % beat_id)
