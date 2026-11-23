class_name EventLog
extends RefCounted
## Every event received from the simulation in this session, oldest first.
##
## The source of truth the views are rebuilt from after a step back. Filtering only: it never
## derives market state (that is the core's job).

var _events: Array[SimEvent] = []


## Appends a batch received from [method Simulation.drain_events].
func append(batch: Array[SimEvent]) -> void:
	_events.append_array(batch)


## Removes every event with a sequence number above [param sequence] (after a rewind).
func truncate_after(sequence: int) -> void:
	var keep: int = _events.size()
	while keep > 0 and _events[keep - 1].seq > sequence:
		keep -= 1
	_events.resize(keep)


func clear() -> void:
	_events.clear()


func size() -> int:
	return _events.size()


func is_empty() -> bool:
	return _events.is_empty()


## All events, oldest first. Do not modify the returned array.
func all() -> Array[SimEvent]:
	return _events


## The most recent [param count] events, oldest first.
func last(count: int) -> Array[SimEvent]:
	var result: Array[SimEvent] = []
	for i: int in range(maxi(0, _events.size() - count), _events.size()):
		result.append(_events[i])
	return result


## Sequence number of the newest event, or 0 if empty.
func last_sequence() -> int:
	return 0 if _events.is_empty() else _events[_events.size() - 1].seq


## Trades (match events), oldest first.
func trades() -> Array[SimEvent]:
	var result: Array[SimEvent] = []
	for event: SimEvent in _events:
		if event.is_trade():
			result.append(event)
	return result


## Events involving order [param order_id], oldest first.
func for_order(order_id: int) -> Array[SimEvent]:
	var result: Array[SimEvent] = []
	for event: SimEvent in _events:
		if event.involves_order(order_id):
			result.append(event)
	return result


## Events involving account [param account_id], oldest first.
func for_account(account_id: int) -> Array[SimEvent]:
	var result: Array[SimEvent] = []
	for event: SimEvent in _events:
		if event.involves_account(account_id):
			result.append(event)
	return result


## Ids of orders placed by [param account_id], in placement order.
func orders_of_account(account_id: int) -> Array[int]:
	var ids: Array[int] = []
	for event: SimEvent in _events:
		if event.type == Simulation.EVENT_ORDER_PLACED and event.account == account_id:
			ids.append(event.order_id)
	return ids
