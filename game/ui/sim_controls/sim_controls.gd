class_name SimControls
extends PanelContainer
## Transport controls: run/pause, single steps, step back, speed, and a status readout.
##
## Emits requests only; the parent decides what they do to the simulation.

## Run was toggled on or off.
signal run_toggled(running: bool)
## One step was requested.
signal step_event_pressed
## Steps up to the next tick were requested.
signal step_tick_pressed
## Undoing the last step was requested.
signal step_back_pressed
## The speed slider moved. 0 means as fast as possible.
signal speed_changed(steps_per_second: float)

@onready var _run_button: Button = %RunButton
@onready var _step_event_button: Button = %StepEventButton
@onready var _step_tick_button: Button = %StepTickButton
@onready var _step_back_button: Button = %StepBackButton
@onready var _speed_slider: HSlider = %SpeedSlider
@onready var _speed_label: Label = %SpeedLabel
@onready var _status_label: Label = %StatusLabel


func _ready() -> void:
	_run_button.toggled.connect(_on_run_toggled)
	_step_event_button.pressed.connect(step_event_pressed.emit)
	_step_tick_button.pressed.connect(step_tick_pressed.emit)
	_step_back_button.pressed.connect(step_back_pressed.emit)
	_speed_slider.value_changed.connect(_on_speed_slider_changed)
	_update_speed_label()
	if SceneUtil.is_standalone(self):
		set_status(42, 318, false, true)


## Steps per second for the slider position: 10^value, or 0 (unthrottled) at the far right.
func get_steps_per_second() -> float:
	if _speed_slider.value >= _speed_slider.max_value:
		return 0.0
	return pow(10.0, _speed_slider.value)


## Shows the simulation state and enables the buttons that make sense for it.
func set_status(tick: int, last_sequence: int, running: bool, can_step_back: bool) -> void:
	_run_button.set_pressed_no_signal(running)
	_run_button.text = "Pause" if running else "Run"
	_step_event_button.disabled = running
	_step_tick_button.disabled = running
	_step_back_button.disabled = running or not can_step_back
	_status_label.text = "tick %d   event #%d" % [tick, last_sequence]


func _on_run_toggled(running: bool) -> void:
	_run_button.text = "Pause" if running else "Run"
	run_toggled.emit(running)


func _on_speed_slider_changed(_value: float) -> void:
	_update_speed_label()
	speed_changed.emit(get_steps_per_second())


func _update_speed_label() -> void:
	var speed: float = get_steps_per_second()
	_speed_label.text = "max" if speed == 0.0 else "%d/s" % roundi(speed)
