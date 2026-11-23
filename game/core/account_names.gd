class_name AccountNames
extends RefCounted
## Display names for accounts (lesson participants). Accounts without a name show as "acct N".

var _names: Dictionary[int, String] = {}


func set_name_for(account: int, display_name: String) -> void:
	_names[account] = display_name


func has_name(account: int) -> bool:
	return _names.has(account)


## The display name of [param account], or "acct N".
func label(account: int) -> String:
	return _names[account] if _names.has(account) else "acct %d" % account
