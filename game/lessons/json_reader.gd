class_name JsonReader
extends RefCounted
## Strict, typed access to parsed JSON (Dictionaries from [JSON]). Every problem is recorded with
## its path, e.g. "lesson.beats[2].actions[0].price", and reading continues so one load reports
## every error in a file.

var errors: PackedStringArray = []


func ok() -> bool:
	return errors.is_empty()


func error(path: String, message: String) -> void:
	errors.append("%s: %s" % [path, message])


## Records an error for every key of [param dict] not in [param allowed].
func check_keys(dict: Dictionary, allowed: PackedStringArray, path: String) -> void:
	for key: Variant in dict.keys():
		if typeof(key) != TYPE_STRING or not allowed.has(str(key)):
			error(path, "unknown key \"%s\"" % str(key))


## Reads an object (Dictionary) or records an error and returns an empty one.
func object(dict: Dictionary, key: String, path: String, required: bool = true) -> Dictionary:
	if not dict.has(key):
		if required:
			error(path, "missing \"%s\"" % key)
		return {}
	var value: Variant = dict[key]
	if typeof(value) != TYPE_DICTIONARY:
		error("%s.%s" % [path, key], "must be an object")
		return {}
	return value


## Reads an array of objects; non-object elements are reported and skipped.
func objects(dict: Dictionary, key: String, path: String,
		required: bool = true) -> Array[Dictionary]:
	var result: Array[Dictionary] = []
	if not dict.has(key):
		if required:
			error(path, "missing \"%s\"" % key)
		return result
	var value: Variant = dict[key]
	if typeof(value) != TYPE_ARRAY:
		error("%s.%s" % [path, key], "must be an array")
		return result
	var items: Array = value
	for i: int in range(items.size()):
		var item: Variant = items[i]
		if typeof(item) != TYPE_DICTIONARY:
			error("%s.%s[%d]" % [path, key, i], "must be an object")
		else:
			result.append(item)
	return result


func string(dict: Dictionary, key: String, path: String, required: bool = true,
		fallback: String = "") -> String:
	if not dict.has(key):
		if required:
			error(path, "missing \"%s\"" % key)
		return fallback
	var value: Variant = dict[key]
	if typeof(value) != TYPE_STRING:
		error("%s.%s" % [path, key], "must be a string")
		return fallback
	return value


func strings(dict: Dictionary, key: String, path: String) -> PackedStringArray:
	var result: PackedStringArray = []
	if not dict.has(key):
		return result
	var value: Variant = dict[key]
	if typeof(value) != TYPE_ARRAY:
		error("%s.%s" % [path, key], "must be an array of strings")
		return result
	var items: Array = value
	for item: Variant in items:
		if typeof(item) != TYPE_STRING:
			error("%s.%s" % [path, key], "must contain only strings")
		else:
			result.append(str(item))
	return result


## Reads an integer in [min, max]. JSON numbers arrive as floats, so integral floats are
## accepted; fractions, other types and out-of-range values are errors.
func integer(dict: Dictionary, key: String, path: String, minimum: int, maximum: int,
		required: bool = true, fallback: int = 0) -> int:
	if not dict.has(key):
		if required:
			error(path, "missing \"%s\"" % key)
		return fallback
	var value: Variant = dict[key]
	var parsed: int = 0
	var valid: bool = false
	if typeof(value) == TYPE_INT:
		parsed = value
		valid = true
	elif typeof(value) == TYPE_FLOAT:
		var number: float = value
		if is_finite(number) and number == floorf(number) and absf(number) < 9.0e15:
			parsed = int(number)
			valid = true
	if not valid or parsed < minimum or parsed > maximum:
		error("%s.%s" % [path, key], "must be an integer between %d and %d" % [minimum, maximum])
		return fallback
	return parsed


## Reads a price written as a decimal string ("100.25") into ticks, exactly.
func price(dict: Dictionary, key: String, path: String, required: bool = true) -> int:
	if not dict.has(key):
		if required:
			error(path, "missing \"%s\"" % key)
		return 0
	var value: Variant = dict[key]
	var ticks: int = PriceFormat.parse_price(str(value)) if typeof(value) == TYPE_STRING else -1
	if ticks <= 0:
		error("%s.%s" % [path, key], "must be a positive price string such as \"100.25\"")
		return 0
	return ticks


## Reads one of [param choices] (a string) and returns its index, or -1 with an error.
func choice(dict: Dictionary, key: String, path: String, choices: PackedStringArray,
		required: bool = true) -> int:
	if not dict.has(key):
		if required:
			error(path, "missing \"%s\"" % key)
		return -1
	var value: Variant = dict[key]
	var index: int = choices.find(str(value)) if typeof(value) == TYPE_STRING else -1
	if index < 0:
		error("%s.%s" % [path, key], "must be one of: %s" % ", ".join(choices))
	return index
