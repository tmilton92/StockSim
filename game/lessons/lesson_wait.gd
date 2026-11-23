class_name LessonWait
extends Resource
## What a beat waits for before the learner can continue.

enum Kind {
	## The learner clicks Next.
	NEXT,
	## An event of [member event_type] happens (for the order tagged [member tag], if set). The
	## learner makes it happen with Step, Tick or Run.
	EVENT,
	## The learner places an order with the order ticket (or "Do it for me", which places
	## [member suggested]).
	LEARNER_ORDER,
}

@export var kind: Kind = Kind.NEXT
@export var event_type: Simulation.EventType = Simulation.EVENT_MATCH
@export var tag: String = ""
## Shown while waiting, e.g. "Press Step until the first trade happens".
@export var hint: String = ""
## For [constant LEARNER_ORDER]: the order "Do it for me" places (a SUBMIT action; the account
## is the learner's).
@export var suggested: LessonAction = null
