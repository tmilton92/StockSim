class_name ChapterLoader
extends RefCounted
## Builds a [ChapterDef] from a scenario file's "lesson" object (format version 2). Strict like
## the extension's scenario parser: unknown keys, wrong types, fractional numbers and invalid
## references are errors, and every error is reported with its path.

const SIDES: PackedStringArray = ["buy", "sell"]
const ORDER_TYPES: PackedStringArray = ["market", "limit", "stop", "stop_limit"]
const ACTIONS: PackedStringArray = [
	"submit", "ladder", "tick", "step", "reset", "highlight", "record", "show_comparison",
]
const WAITS: PackedStringArray = ["next", "event", "learner_order"]
const EVENTS: PackedStringArray = [
	"order_placed", "order_rejected", "order_rested", "order_cancelled", "order_modified",
	"command_rejected", "stop_triggered", "match", "fill", "price_change",
]
const MAX_ACCOUNT: int = 4294967295
const MAX_QTY: int = 1000000000000
const MAX_COUNT: int = 10000


## Result of loading: [member chapter] is null when [member errors] is not empty.
class LoadResult:
	var chapter: ChapterDef = null
	var errors: PackedStringArray = []


## Loads the chapter in the scenario file at [param path].
static func load_file(path: String) -> LoadResult:
	var result: LoadResult = LoadResult.new()
	var text: String = FileAccess.get_file_as_string(path)
	if text.is_empty() and FileAccess.get_open_error() != OK:
		result.errors.append("%s: cannot read the file" % path)
		return result
	var json: JSON = JSON.new()
	if json.parse(text) != OK:
		result.errors.append("%s: invalid JSON at line %d: %s"
				% [path, json.get_error_line(), json.get_error_message()])
		return result
	if typeof(json.data) != TYPE_DICTIONARY:
		result.errors.append("%s: must contain a JSON object" % path)
		return result
	var scenario: Dictionary = json.data
	return from_scenario(scenario, path)


## Builds a chapter from an already-parsed scenario object.
static func from_scenario(scenario: Dictionary, path: String) -> LoadResult:
	var result: LoadResult = LoadResult.new()
	var reader: JsonReader = JsonReader.new()
	if reader.integer(scenario, "format_version", "scenario", 2, 2) != 2 or not reader.ok():
		result.errors.append("%s: lessons need scenario format_version 2" % path)
		return result
	var lesson: Dictionary = reader.object(scenario, "lesson", "scenario")
	var chapter: ChapterDef = _parse_lesson(lesson, reader)
	chapter.source_path = path
	if reader.ok():
		result.chapter = chapter
	else:
		for message: String in reader.errors:
			result.errors.append("%s: %s" % [path, message])
	return result


static func _parse_lesson(lesson: Dictionary, reader: JsonReader) -> ChapterDef:
	var path: String = "lesson"
	reader.check_keys(lesson, ["chapter", "title", "summary", "features", "learner_account",
			"participants", "setup", "beats"], path)
	var chapter: ChapterDef = ChapterDef.new()
	chapter.chapter = reader.integer(lesson, "chapter", path, 1, 999)
	chapter.title = reader.string(lesson, "title", path)
	chapter.summary = reader.string(lesson, "summary", path)
	chapter.learner_account = reader.integer(lesson, "learner_account", path, 0, MAX_ACCOUNT)
	chapter.features = reader.strings(lesson, "features", path)
	for feature: String in chapter.features:
		if not ChapterDef.FEATURES.has(feature):
			reader.error(path + ".features", "unknown feature \"%s\" (known: %s)"
					% [feature, ", ".join(ChapterDef.FEATURES)])

	var participants: Array[Dictionary] = reader.objects(lesson, "participants", path, false)
	for i: int in range(participants.size()):
		chapter.participants.append(
				_parse_participant(participants[i], reader, "%s.participants[%d]" % [path, i]))

	var setup: Array[Dictionary] = reader.objects(lesson, "setup", path, false)
	for i: int in range(setup.size()):
		chapter.setup.append(_parse_action(setup[i], reader, "%s.setup[%d]" % [path, i]))

	var beats: Array[Dictionary] = reader.objects(lesson, "beats", path)
	if beats.is_empty() and reader.ok():
		reader.error(path + ".beats", "must contain at least one beat")
	var ids: PackedStringArray = []
	for i: int in range(beats.size()):
		var beat: BeatDef = _parse_beat(beats[i], reader, "%s.beats[%d]" % [path, i])
		if ids.has(beat.id):
			reader.error("%s.beats[%d].id" % [path, i], "duplicate id \"%s\"" % beat.id)
		ids.append(beat.id)
		chapter.beats.append(beat)
	_check_tags(chapter, reader)
	return chapter


static func _parse_participant(data: Dictionary, reader: JsonReader,
		path: String) -> ParticipantDef:
	reader.check_keys(data, ["account", "name", "role"], path)
	var participant: ParticipantDef = ParticipantDef.new()
	participant.account = reader.integer(data, "account", path, 0, MAX_ACCOUNT)
	participant.name = reader.string(data, "name", path)
	participant.role = reader.string(data, "role", path, false)
	return participant


static func _parse_beat(data: Dictionary, reader: JsonReader, path: String) -> BeatDef:
	reader.check_keys(data, ["id", "title", "text", "actions", "wait"], path)
	var beat: BeatDef = BeatDef.new()
	beat.id = reader.string(data, "id", path)
	beat.title = reader.string(data, "title", path, false)
	beat.text = reader.string(data, "text", path)
	var actions: Array[Dictionary] = reader.objects(data, "actions", path, false)
	for i: int in range(actions.size()):
		beat.actions.append(_parse_action(actions[i], reader, "%s.actions[%d]" % [path, i]))
	beat.wait = _parse_wait(reader.object(data, "wait", path, false), reader, path + ".wait")
	return beat


static func _parse_wait(data: Dictionary, reader: JsonReader, path: String) -> LessonWait:
	var wait: LessonWait = LessonWait.new()
	if data.is_empty():
		return wait  # next
	reader.check_keys(data, ["kind", "event", "tag", "hint", "suggested"], path)
	var kind: int = reader.choice(data, "kind", path, WAITS)
	wait.kind = maxi(kind, 0) as LessonWait.Kind
	wait.hint = reader.string(data, "hint", path, false)
	wait.tag = reader.string(data, "tag", path, false)
	if wait.kind == LessonWait.Kind.EVENT:
		var event: int = reader.choice(data, "event", path, EVENTS)
		wait.event_type = maxi(event, 0) as Simulation.EventType
	elif data.has("event"):
		reader.error(path + ".event", "only used by kind \"event\"")
	if wait.kind == LessonWait.Kind.LEARNER_ORDER:
		var suggested: Dictionary = reader.object(data, "suggested", path)
		wait.suggested = _parse_order(suggested, reader, path + ".suggested", false)
	elif data.has("suggested"):
		reader.error(path + ".suggested", "only used by kind \"learner_order\"")
	return wait


static func _parse_action(data: Dictionary, reader: JsonReader, path: String) -> LessonAction:
	var index: int = reader.choice(data, "do", path, ACTIONS)
	var action: LessonAction = LessonAction.new()
	match index:
		0:  # submit
			reader.check_keys(data,
					["do", "account", "side", "type", "qty", "price", "stop", "tag"], path)
			action = _parse_order(data, reader, path, true)
		1:  # ladder
			reader.check_keys(data,
					["do", "account", "side", "from", "step", "levels", "qty", "qty_step"], path)
			action.type = LessonAction.Type.LADDER
			action.account = reader.integer(data, "account", path, 0, MAX_ACCOUNT)
			action.side = maxi(reader.choice(data, "side", path, SIDES), 0) as Simulation.Side
			action.price = reader.price(data, "from", path)
			action.price_step = reader.price(data, "step", path)
			action.levels = reader.integer(data, "levels", path, 1, 200)
			action.qty = reader.integer(data, "qty", path, 1, MAX_QTY)
			action.qty_step = reader.integer(data, "qty_step", path, 0, MAX_QTY, false)
		2, 3:  # tick, step
			reader.check_keys(data, ["do", "count"], path)
			action.type = LessonAction.Type.TICK if index == 2 else LessonAction.Type.STEP
			action.count = reader.integer(data, "count", path, 1, MAX_COUNT, false, 1)
		4:  # reset
			reader.check_keys(data, ["do"], path)
			action.type = LessonAction.Type.RESET
		5:  # highlight
			reader.check_keys(data, ["do", "price", "account", "tag"], path)
			action.type = LessonAction.Type.HIGHLIGHT
			var targets: int = int(data.has("price")) + int(data.has("account")) \
					+ int(data.has("tag"))
			if targets != 1:
				reader.error(path, "highlight needs exactly one of \"price\", \"account\", \"tag\"")
			action.price = reader.price(data, "price", path, false)
			action.account = reader.integer(data, "account", path, 0, MAX_ACCOUNT, false)
			action.tag = reader.string(data, "tag", path, false)
		6:  # record
			reader.check_keys(data, ["do", "tag", "label"], path)
			action.type = LessonAction.Type.RECORD
			action.tag = reader.string(data, "tag", path)
			action.label = reader.string(data, "label", path)
		7:  # show_comparison
			reader.check_keys(data, ["do"], path)
			action.type = LessonAction.Type.SHOW_COMPARISON
	return action


## Parses an order (a "submit" action, or a learner_order suggestion when
## [param needs_account] is false: the learner's account is used).
static func _parse_order(data: Dictionary, reader: JsonReader, path: String,
		needs_account: bool) -> LessonAction:
	if not needs_account:
		reader.check_keys(data, ["side", "type", "qty", "price", "stop", "tag"], path)
	var action: LessonAction = LessonAction.new()
	action.type = LessonAction.Type.SUBMIT
	if needs_account:
		action.account = reader.integer(data, "account", path, 0, MAX_ACCOUNT)
	action.side = maxi(reader.choice(data, "side", path, SIDES), 0) as Simulation.Side
	var type_index: int = reader.choice(data, "type", path, ORDER_TYPES)
	action.order_type = maxi(type_index, 0) as Simulation.OrderType
	action.qty = reader.integer(data, "qty", path, 1, MAX_QTY)
	action.tag = reader.string(data, "tag", path, false)
	var needs_price: bool = OrderRequest.uses_limit_price(action.order_type)
	var needs_stop: bool = OrderRequest.uses_stop_price(action.order_type)
	if needs_price:
		action.price = reader.price(data, "price", path)
	elif data.has("price"):
		reader.error(path + ".price", "not used by this order type")
	if needs_stop:
		action.stop_price = reader.price(data, "stop", path)
	elif data.has("stop"):
		reader.error(path + ".stop", "not used by this order type")
	return action


## Every tag that is waited on, highlighted or recorded must be set by an earlier submit (or a
## learner_order suggestion).
static func _check_tags(chapter: ChapterDef, reader: JsonReader) -> void:
	var known: PackedStringArray = []
	for action: LessonAction in chapter.setup:
		_collect_tag(action, known)
	for i: int in range(chapter.beats.size()):
		var beat: BeatDef = chapter.beats[i]
		for action: LessonAction in beat.actions:
			var uses_tag: bool = action.type == LessonAction.Type.RECORD \
					or (action.type == LessonAction.Type.HIGHLIGHT and not action.tag.is_empty())
			if uses_tag and not known.has(action.tag):
				reader.error("lesson.beats[%d]" % i, "tag \"%s\" is used before any order sets it"
						% action.tag)
			_collect_tag(action, known)
		if beat.wait.kind == LessonWait.Kind.LEARNER_ORDER and beat.wait.suggested != null:
			_collect_tag(beat.wait.suggested, known)
		if not beat.wait.tag.is_empty() and not known.has(beat.wait.tag):
			reader.error("lesson.beats[%d].wait" % i, "tag \"%s\" is not set by any earlier order"
					% beat.wait.tag)


static func _collect_tag(action: LessonAction, known: PackedStringArray) -> void:
	if action.type == LessonAction.Type.SUBMIT and not action.tag.is_empty():
		known.append(action.tag)
