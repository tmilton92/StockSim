class_name LessonRunner
extends Node
## Plays a [ChapterDef]: loads its scenario, runs the setup, then walks through the beats.
##
## Actions are ordinary simulation inputs (orders, steps, ticks, resets) sent through
## [code]SimBridge[/code], or UI requests (highlights, the comparison card) emitted as signals.
## A beat can be left when its wait is satisfied: always for "next"; after a matching event for
## "event"; after the learner places an order for "learner_order".

## A beat began; its actions have not run yet.
signal beat_started(index: int, beat: BeatDef)
## The current beat's wait became satisfied (or not).
signal wait_state_changed(satisfied: bool)
## A highlight action asks the UI to select something.
signal highlight_requested(ref: EntityRef)
## A show_comparison action: every record taken so far.
signal comparison_shown(records: Array[ImpactRecord])
## The last beat was left.
signal finished

var chapter: ChapterDef = null
var _index: int = -1
var _satisfied: bool = false
var _finished: bool = false
## Order ids, sides, quantities and reference quotes by tag.
var _tags: Dictionary[String, int] = {}
var _tag_sides: Dictionary[String, Simulation.Side] = {}
var _tag_qty: Dictionary[String, int] = {}
var _tag_references: Dictionary[String, int] = {}
var _records: Array[ImpactRecord] = []


func _ready() -> void:
	SimBridge.events_received.connect(_on_events_received)


## Starts [param definition] from its first beat on a fresh simulation.
func start(definition: ChapterDef) -> Error:
	chapter = definition
	_index = -1
	_satisfied = false
	_finished = false
	_clear_tags()
	_records.clear()
	var error: Error = SimBridge.load_scenario(definition.source_path)
	if error != OK:
		push_error("LessonRunner: cannot load scenario %s (%s)"
				% [definition.source_path, error_string(error)])
		return error
	_run_actions(definition.setup)
	_enter_beat(0)
	return OK


## Moves to the next beat, or finishes. Does nothing while the current wait is unsatisfied.
func advance() -> void:
	if chapter == null or _finished or not _satisfied:
		return
	if _index + 1 >= chapter.beats.size():
		_finished = true
		finished.emit()
		return
	_enter_beat(_index + 1)


func current_index() -> int:
	return _index


func current_beat() -> BeatDef:
	if chapter == null or _index < 0 or _index >= chapter.beats.size():
		return null
	return chapter.beats[_index]


func is_satisfied() -> bool:
	return _satisfied


func is_finished() -> bool:
	return _finished


func records() -> Array[ImpactRecord]:
	return _records


## Order id remembered under [param tag], or 0.
func order_for_tag(tag: String) -> int:
	return _tags[tag] if _tags.has(tag) else 0


## Tells the runner the learner placed order [param order_id] (from the order ticket).
func learner_submitted(order_id: int, side: Simulation.Side, qty: int,
		reference_price: int) -> void:
	var beat: BeatDef = current_beat()
	if beat == null or beat.wait.kind != LessonWait.Kind.LEARNER_ORDER or _satisfied:
		return
	if beat.wait.suggested != null and not beat.wait.suggested.tag.is_empty():
		_remember(beat.wait.suggested.tag, order_id, side, qty, reference_price)
	_set_satisfied(true)


## Places the current beat's suggested order as the learner ("Do it for me").
func do_suggested() -> int:
	var beat: BeatDef = current_beat()
	if beat == null or beat.wait.kind != LessonWait.Kind.LEARNER_ORDER \
			or beat.wait.suggested == null or _satisfied:
		return 0
	var order: LessonAction = beat.wait.suggested
	var reference: int = SimBridge.best_opposite_quote(order.side)
	var order_id: int = SimBridge.simulation.submit_order(chapter.learner_account, order.side,
			order.order_type, order.qty, order.price, order.stop_price)
	learner_submitted(order_id, order.side, order.qty, reference)
	return order_id


func _enter_beat(index: int) -> void:
	_index = index
	var beat: BeatDef = chapter.beats[index]
	var log_start: int = SimBridge.event_log.size()
	_satisfied = beat.wait.kind == LessonWait.Kind.NEXT
	beat_started.emit(index, beat)
	_run_actions(beat.actions)
	if not _satisfied and beat.wait.kind == LessonWait.Kind.EVENT:
		# The beat's own actions may already have produced the awaited event.
		_check_events(SimBridge.event_log.all().slice(log_start))
	wait_state_changed.emit(_satisfied)


func _run_actions(actions: Array[LessonAction]) -> void:
	var simulation: Simulation = SimBridge.simulation
	for action: LessonAction in actions:
		match action.type:
			LessonAction.Type.SUBMIT:
				_submit(action, action.price, action.qty)
			LessonAction.Type.LADDER:
				for level: int in range(action.levels):
					var offset: int = action.price_step * level
					var level_price: int = action.price + offset \
							if action.side == Simulation.SIDE_SELL else action.price - offset
					_submit(action, level_price, action.qty + action.qty_step * level)
			LessonAction.Type.TICK:
				for i: int in range(action.count):
					simulation.step_tick()
					SimBridge.poll()
			LessonAction.Type.STEP:
				for i: int in range(action.count):
					simulation.step_event()
					SimBridge.poll()
			LessonAction.Type.RESET:
				SimBridge.load_scenario(chapter.source_path)
				simulation = SimBridge.simulation
				_clear_tags()
			LessonAction.Type.HIGHLIGHT:
				highlight_requested.emit(_highlight_target(action))
			LessonAction.Type.RECORD:
				_records.append(_record(action))
			LessonAction.Type.SHOW_COMPARISON:
				comparison_shown.emit(_records)
	SimBridge.poll()


func _submit(action: LessonAction, price: int, qty: int) -> void:
	var reference: int = SimBridge.best_opposite_quote(action.side)
	var order_id: int = SimBridge.simulation.submit_order(action.account, action.side,
			action.order_type, qty, price, action.stop_price)
	if not action.tag.is_empty():
		_remember(action.tag, order_id, action.side, qty, reference)


func _remember(tag: String, order_id: int, side: Simulation.Side, qty: int,
		reference: int) -> void:
	_tags[tag] = order_id
	_tag_sides[tag] = side
	_tag_qty[tag] = qty
	_tag_references[tag] = reference


func _clear_tags() -> void:
	_tags.clear()
	_tag_sides.clear()
	_tag_qty.clear()
	_tag_references.clear()


func _highlight_target(action: LessonAction) -> EntityRef:
	if not action.tag.is_empty():
		return EntityRef.make(EntityRef.Kind.ORDER, order_for_tag(action.tag))
	if action.price != 0:
		return EntityRef.make(EntityRef.Kind.PRICE, action.price)
	return EntityRef.make(EntityRef.Kind.ACCOUNT, action.account)


func _record(action: LessonAction) -> ImpactRecord:
	var order_id: int = order_for_tag(action.tag)
	var fills: Array[SimEvent] = []
	for event: SimEvent in SimBridge.event_log.for_order(order_id):
		if event.type == Simulation.EVENT_FILL and event.order_id == order_id:
			fills.append(event)
	var simulation: Simulation = SimBridge.simulation
	var last_price: int = simulation.get_last_price() if simulation.has_last_price() else 0
	return ImpactRecord.from_fills(action.label, _tag_sides[action.tag], _tag_qty[action.tag],
			_tag_references[action.tag], fills, last_price)


func _on_events_received(events: Array[SimEvent]) -> void:
	var beat: BeatDef = current_beat()
	if beat == null or _finished or _satisfied or beat.wait.kind != LessonWait.Kind.EVENT:
		return
	_check_events(events)


func _check_events(events: Array[SimEvent]) -> void:
	var wait: LessonWait = current_beat().wait
	var order_id: int = order_for_tag(wait.tag)
	for event: SimEvent in events:
		var for_tagged_order: bool = wait.tag.is_empty() or event.involves_order(order_id)
		if event.type == wait.event_type and for_tagged_order:
			_set_satisfied(true)
			return


func _set_satisfied(value: bool) -> void:
	if _satisfied == value:
		return
	_satisfied = value
	wait_state_changed.emit(value)
