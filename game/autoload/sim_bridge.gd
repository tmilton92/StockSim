extends Node
## Global access to the running simulation (autoload "SimBridge").
##
## Owns the [Simulation] node, drains its events once per frame into [member event_log], and
## re-broadcasts them as typed signals. Views never talk to the simulation's event buffer
## directly; they listen here and rebuild from [member event_log] after [signal rewound].

## Emitted once per frame with the events drained that frame, oldest first.
signal events_received(events: Array[SimEvent])
## Emitted after a step back. [member event_log] is already truncated to [param last_sequence].
signal rewound(last_sequence: int)
## Emitted after the simulation was replaced (new scenario or loaded state); views clear.
signal simulation_reset

## The simulation node. Call its methods for inputs, stepping and queries.
var simulation: Simulation = null
## Every event received this session.
var event_log: EventLog = EventLog.new()


func _ready() -> void:
	simulation = Simulation.new()
	simulation.name = "Simulation"
	add_child(simulation)
	simulation.rewound.connect(_on_simulation_rewound)


func _process(_delta: float) -> void:
	poll()


## Drains pending events now and emits [signal events_received] if there were any. Called every
## frame; call it directly to see the result of a manual step in the same frame.
func poll() -> void:
	var rows: PackedInt64Array = simulation.drain_events()
	if rows.is_empty():
		return
	var events: Array[SimEvent] = SimEvent.from_rows(rows)
	event_log.append(events)
	events_received.emit(events)


## Loads a scenario JSON file into a fresh simulation.
func load_scenario(path: String) -> Error:
	var error: Error = simulation.load_scenario(path)
	if error == OK:
		_reset()
	return error


## Restores a saved simulation state (history is not restored).
func load_state(data: PackedByteArray) -> Error:
	var error: Error = simulation.load_state(data)
	if error == OK:
		_reset()
	return error


func _reset() -> void:
	event_log.clear()
	simulation_reset.emit()


func _on_simulation_rewound(last_sequence: int) -> void:
	event_log.truncate_after(last_sequence)
	rewound.emit(last_sequence)
