class_name BeatDef
extends Resource
## One screen of a lesson: explanatory text, actions run when it starts, and what it waits for.

@export var id: String = ""
@export var title: String = ""
## Shown in the lesson panel (RichTextLabel BBCode).
@export_multiline var text: String = ""
@export var actions: Array[LessonAction] = []
@export var wait: LessonWait = null
