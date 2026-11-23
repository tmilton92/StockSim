class_name ChapterSelect
extends Control
## The chapter list: every lesson in [constant CHAPTER_DIR], in chapter order, plus the sandbox.
##
## A chapter that fails to load is listed as unavailable and its errors are logged, so a broken
## file is visible instead of silently missing.

## The learner picked the chapter in the scenario file at [param path].
signal chapter_chosen(path: String)
## The learner picked the free-play sandbox.
signal sandbox_chosen

const CHAPTER_DIR: String = "res://scenarios/chapters"

@onready var _chapters: VBoxContainer = %Chapters
@onready var _sandbox_button: Button = %SandboxButton


func _ready() -> void:
	_sandbox_button.pressed.connect(sandbox_chosen.emit)
	populate()


## (Re)builds the list from the chapter files.
func populate() -> void:
	for child: Node in _chapters.get_children():
		child.queue_free()
	var loaded: Array[ChapterDef] = []
	for file: String in DirAccess.get_files_at(CHAPTER_DIR):
		if not file.ends_with(".json"):
			continue
		var path: String = CHAPTER_DIR.path_join(file)
		var result: ChapterLoader.LoadResult = ChapterLoader.load_file(path)
		if result.chapter == null:
			for message: String in result.errors:
				push_error(message)
			_add_unavailable(path)
		else:
			loaded.append(result.chapter)
	loaded.sort_custom(func(a: ChapterDef, b: ChapterDef) -> bool: return a.chapter < b.chapter)
	for chapter: ChapterDef in loaded:
		_add_entry(chapter)


## Number of chapters listed as playable.
func chapter_count() -> int:
	var count: int = 0
	for child: Node in _chapters.get_children():
		var entry: Button = child.find_child("Open", true, false) as Button
		if entry != null and not entry.disabled and not child.is_queued_for_deletion():
			count += 1
	return count


func _add_entry(chapter: ChapterDef) -> void:
	var card: PanelContainer = PanelContainer.new()
	var column: VBoxContainer = VBoxContainer.new()
	var open: Button = Button.new()
	open.name = "Open"
	open.text = "%d.  %s" % [chapter.chapter, chapter.title]
	open.alignment = HORIZONTAL_ALIGNMENT_LEFT
	open.pressed.connect(chapter_chosen.emit.bind(chapter.source_path))
	var summary: Label = Label.new()
	summary.text = chapter.summary
	summary.theme_type_variation = &"ChapterSummary"
	summary.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(open)
	column.add_child(summary)
	card.add_child(column)
	_chapters.add_child(card)


func _add_unavailable(path: String) -> void:
	var card: PanelContainer = PanelContainer.new()
	var open: Button = Button.new()
	open.name = "Open"
	open.text = "%s could not be loaded (see the Output panel)" % path.get_file()
	open.disabled = true
	card.add_child(open)
	_chapters.add_child(card)
