class_name EntityRef
extends RefCounted
## A clickable reference to something shown in the UI (an order, an account, or a price level),
## encoded in RichTextLabel meta links as "kind:id".

enum Kind { ORDER, ACCOUNT, PRICE }

var kind: Kind = Kind.ORDER
var id: int = 0


static func make(ref_kind: Kind, ref_id: int) -> EntityRef:
	var ref: EntityRef = EntityRef.new()
	ref.kind = ref_kind
	ref.id = ref_id
	return ref


## Meta string for a link to this entity.
func to_meta() -> String:
	return "%s:%d" % [_kind_name(kind), id]


## Parses a meta string written by [method to_meta]; returns null if it is not one.
static func from_meta(meta: String) -> EntityRef:
	var parts: PackedStringArray = meta.split(":")
	if parts.size() != 2 or not parts[1].is_valid_int():
		return null
	for candidate: Kind in [Kind.ORDER, Kind.ACCOUNT, Kind.PRICE]:
		if parts[0] == _kind_name(candidate):
			return make(candidate, parts[1].to_int())
	return null


func equals(other: EntityRef) -> bool:
	return other != null and other.kind == kind and other.id == id


static func _kind_name(value: Kind) -> String:
	match value:
		Kind.ORDER:
			return "order"
		Kind.ACCOUNT:
			return "account"
		Kind.PRICE:
			return "price"
	return "unknown"
