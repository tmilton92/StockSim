class_name SceneUtil
extends RefCounted
## Helpers shared by UI scenes.


## True when [param node] is the scene being run on its own (F6), so it should show mock data.
static func is_standalone(node: Node) -> bool:
	return node.is_inside_tree() and node.get_tree().current_scene == node
