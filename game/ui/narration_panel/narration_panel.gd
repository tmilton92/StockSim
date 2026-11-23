class_name NarrationPanel
extends PanelContainer
## Running plain-English explanation of every event, newest at the bottom.
##
## Orders, accounts and prices in the text are links; clicking one emits
## [signal entity_selected] so the rest of the UI can highlight and inspect it.

## The user clicked an order, account or price link.
signal entity_selected(ref: EntityRef)

## Lines kept in the panel; older events remain in the event log.
const MAX_LINES: int = 400

@onready var _text: RichTextLabel = %Text
var _palette: NarrationPalette = NarrationPalette.new()
var _lines: int = 0


func _ready() -> void:
	_palette = NarrationPalette.from_theme(self)
	_text.meta_clicked.connect(_on_meta_clicked)
	if SceneUtil.is_standalone(self):
		rebuild(MockMarket.build().events)


## Appends one line per event (oldest first).
func add_events(events: Array[SimEvent]) -> void:
	for event: SimEvent in events:
		if _lines > 0:
			_text.newline()
		_text.append_text(Narrator.describe(event, _palette))
		_lines += 1
	while _lines > MAX_LINES:
		_text.remove_paragraph(0)
		_lines -= 1


## Replaces the text with lines for the newest [constant MAX_LINES] of [param events].
func rebuild(events: Array[SimEvent]) -> void:
	clear()
	add_events(events.slice(maxi(0, events.size() - MAX_LINES)))


func clear() -> void:
	_text.clear()
	_lines = 0


func line_count() -> int:
	return _lines


func _on_meta_clicked(meta: Variant) -> void:
	var ref: EntityRef = EntityRef.from_meta(str(meta))
	if ref != null:
		entity_selected.emit(ref)
