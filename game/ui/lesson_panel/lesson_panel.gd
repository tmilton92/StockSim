class_name LessonPanel
extends PanelContainer
## The lesson text beside the market: chapter and progress, the current beat's explanation, what
## the learner should do next, and (in comparison beats) a table of recorded results.
##
## Display only; it reports button presses and the parent drives the [LessonRunner].

## The learner pressed Next (or Finish on the last beat).
signal next_pressed
## The learner pressed "Do it for me" on a beat that asks for an order.
signal do_it_pressed
## The learner pressed "Back to chapters" after finishing.
signal chapters_pressed

@onready var _chapter_label: Label = %ChapterLabel
@onready var _progress_label: Label = %ProgressLabel
@onready var _beat_title: Label = %BeatTitle
@onready var _text: RichTextLabel = %Text
@onready var _comparison: Tree = %Comparison
@onready var _hint: Label = %Hint
@onready var _do_it_button: Button = %DoItButton
@onready var _next_button: Button = %NextButton
@onready var _chapters_button: Button = %ChaptersButton


func _ready() -> void:
	_next_button.pressed.connect(next_pressed.emit)
	_do_it_button.pressed.connect(do_it_pressed.emit)
	_chapters_button.pressed.connect(chapters_pressed.emit)
	_comparison.visible = false
	_chapters_button.visible = false
	if SceneUtil.is_standalone(self):
		var beat: BeatDef = BeatDef.new()
		beat.title = "A sample beat"
		beat.text = "Lesson text appears here, with [b]emphasis[/b] where it matters."
		beat.wait = LessonWait.new()
		show_beat("Chapter 1: Market creation", 0, 5, beat)
		set_waiting(beat, true)


## Shows beat [param index] (0-based) of [param total].
func show_beat(chapter_title: String, index: int, total: int, beat: BeatDef) -> void:
	_chapter_label.text = chapter_title.to_upper()
	_progress_label.text = "Step %d of %d" % [index + 1, total]
	_beat_title.text = beat.title
	_beat_title.visible = not beat.title.is_empty()
	_text.clear()
	_text.append_text(beat.text)
	_comparison.visible = false
	_chapters_button.visible = false
	_next_button.visible = true
	_next_button.text = "Finish" if index + 1 == total else "Next"


## Updates the controls for the beat's wait: Next is enabled once [param satisfied]; while
## waiting, the hint says what to do and "Do it for me" offers the suggested order.
func set_waiting(beat: BeatDef, satisfied: bool) -> void:
	_next_button.disabled = not satisfied
	var waiting_for_learner: bool = not satisfied and beat.wait.kind != LessonWait.Kind.NEXT
	_hint.text = beat.wait.hint
	_hint.visible = waiting_for_learner and not beat.wait.hint.is_empty()
	_do_it_button.visible = waiting_for_learner \
			and beat.wait.kind == LessonWait.Kind.LEARNER_ORDER and beat.wait.suggested != null


## Shows recorded results side by side.
func show_comparison(records: Array[ImpactRecord]) -> void:
	_comparison.clear()
	_comparison.columns = records.size() + 1
	_comparison.hide_root = true
	_comparison.column_titles_visible = true
	_comparison.set_column_title(0, "")
	_comparison.set_column_expand(0, true)
	_comparison.set_column_custom_minimum_width(0, 150)
	for i: int in range(records.size()):
		_comparison.set_column_title(i + 1, records[i].label)
		_comparison.set_column_expand(i + 1, false)
		_comparison.set_column_custom_minimum_width(i + 1, 84)
	var root: TreeItem = _comparison.create_item()
	var captions: PackedStringArray = [
		"Shares filled", "Best quote before", "Average price paid", "Worst price paid",
		"Price levels used", "Last price after", "Price moved by", "Extra cost per share",
	]
	var items: Array[TreeItem] = []
	for caption: String in captions:
		var item: TreeItem = _comparison.create_item(root)
		item.set_text(0, caption)
		items.append(item)
	for column: int in range(1, records.size() + 1):
		var values: PackedStringArray = _values(records[column - 1])
		for row: int in range(values.size()):
			items[row].set_text(column, values[row])
			items[row].set_text_alignment(column, HORIZONTAL_ALIGNMENT_RIGHT)
	_comparison.visible = true


## Shows the end of the chapter.
func show_finished() -> void:
	_progress_label.text = "Chapter complete"
	_next_button.visible = false
	_do_it_button.visible = false
	_hint.visible = false
	_chapters_button.visible = true


## One record's column, in the order of the captions in [method show_comparison].
static func _values(record: ImpactRecord) -> PackedStringArray:
	return [
		PriceFormat.qty(record.filled_qty),
		PriceFormat.price(record.reference_price),
		record.average_fill_text(),
		PriceFormat.price(record.worst_fill),
		str(record.levels),
		PriceFormat.price(record.last_price_after),
		PriceFormat.price(record.price_move()),
		record.slippage_text(),
	]
