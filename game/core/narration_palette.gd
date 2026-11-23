class_name NarrationPalette
extends RefCounted
## Colors used by [Narrator] when writing BBCode. Panels fill it from the project theme.

var buy: Color = Color("4caf50")
var sell: Color = Color("ef5350")
var muted: Color = Color("8a8f98")
var emphasis: Color = Color("e6e6e6")


## Reads the palette from the "Narration" theme type of [param control].
static func from_theme(control: Control) -> NarrationPalette:
	var palette: NarrationPalette = NarrationPalette.new()
	palette.buy = control.get_theme_color(&"buy", &"Narration")
	palette.sell = control.get_theme_color(&"sell", &"Narration")
	palette.muted = control.get_theme_color(&"muted", &"Narration")
	palette.emphasis = control.get_theme_color(&"emphasis", &"Narration")
	return palette


func for_side(side: Simulation.Side) -> Color:
	return buy if side == Simulation.SIDE_BUY else sell
