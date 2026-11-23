class_name ChapterDef
extends Resource
## A lesson chapter, built by [ChapterLoader] from the "lesson" object of a scenario file
## (format version 2, docs/scenario-format.md).

## UI features a chapter can enable. Panels not listed are hidden.
const FEATURES: PackedStringArray = [
	"chart", "ladder", "tape", "narration", "inspector", "ticket", "seed", "run",
]

@export var chapter: int = 0
@export var title: String = ""
@export_multiline var summary: String = ""
@export var features: PackedStringArray = []
## The account the learner trades as.
@export var learner_account: int = 0
@export var participants: Array[ParticipantDef] = []
## Actions run before the first beat (building the starting market).
@export var setup: Array[LessonAction] = []
@export var beats: Array[BeatDef] = []
## res:// path of the scenario file; the simulation is loaded (and reset) from it.
@export var source_path: String = ""


func has_feature(feature: String) -> bool:
	return features.has(feature)


## Display names by account id, including the learner ("You").
func account_names() -> AccountNames:
	var names: AccountNames = AccountNames.new()
	for participant: ParticipantDef in participants:
		names.set_name_for(participant.account, participant.name)
	names.set_name_for(learner_account, "You")
	return names
