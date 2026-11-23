class_name TradeTape
extends PanelContainer
## Time-and-sales list of trades, newest first, colored by the aggressor's side.
##
## Selecting a row emits [signal entity_selected]: the maker column selects the maker order,
## the price column the price level, anything else the taker order.

## The user selected an order or a price in the tape.
signal entity_selected(ref: EntityRef)

## Rows kept; older trades remain in the event log and chart.
const MAX_ROWS: int = 200

enum Column { TICK, SIDE, PRICE, QTY, TAKER, MAKER }

@onready var _tree: Tree = %Tree
var _root: TreeItem = null


func _ready() -> void:
	var titles: PackedStringArray = ["Tick", "Side", "Price", "Qty", "Taker", "Maker"]
	_tree.columns = titles.size()
	_tree.column_titles_visible = true
	_tree.hide_root = true
	var min_widths: PackedInt32Array = [44, 48, 72, 56, 56, 56]
	for column: int in range(titles.size()):
		_tree.set_column_title(column, titles[column])
		_tree.set_column_expand(column, column == Column.PRICE)
		_tree.set_column_custom_minimum_width(column, min_widths[column])
	_root = _tree.create_item()
	_tree.item_selected.connect(_on_item_selected)
	if SceneUtil.is_standalone(self):
		rebuild(MockMarket.build().trades())


## Adds trades (match events, oldest first) at the top of the tape.
func add_trades(trades: Array[SimEvent]) -> void:
	for trade: SimEvent in trades:
		_add_row(trade)
	while _root.get_child_count() > MAX_ROWS:
		_root.get_child(_root.get_child_count() - 1).free()


## Replaces the tape with the newest [constant MAX_ROWS] of [param trades].
func rebuild(trades: Array[SimEvent]) -> void:
	clear()
	add_trades(trades.slice(maxi(0, trades.size() - MAX_ROWS)))


func clear() -> void:
	for child: TreeItem in _root.get_children():
		child.free()


func row_count() -> int:
	return _root.get_child_count()


func _add_row(trade: SimEvent) -> void:
	var item: TreeItem = _tree.create_item(_root, 0)
	var side_color: Color = get_theme_color(
			&"buy" if trade.side == Simulation.SIDE_BUY else &"sell", &"Narration")
	item.set_text(Column.TICK, str(trade.tick))
	item.set_text(Column.SIDE, PriceFormat.side(trade.side))
	item.set_text(Column.PRICE, PriceFormat.price(trade.price))
	item.set_text(Column.QTY, PriceFormat.qty(trade.qty))
	item.set_text(Column.TAKER, "#%d" % trade.order_id)
	item.set_text(Column.MAKER, "#%d" % trade.counter_order_id)
	for column: int in [Column.SIDE, Column.PRICE]:
		item.set_custom_color(column, side_color)
	for column: int in [Column.PRICE, Column.QTY]:
		item.set_text_alignment(column, HORIZONTAL_ALIGNMENT_RIGHT)
	item.set_metadata(Column.PRICE, trade.price)
	item.set_metadata(Column.TAKER, trade.order_id)
	item.set_metadata(Column.MAKER, trade.counter_order_id)


func _on_item_selected() -> void:
	var item: TreeItem = _tree.get_selected()
	if item == null:
		return
	var column: int = _tree.get_selected_column()
	if column == Column.PRICE:
		var price: int = item.get_metadata(Column.PRICE)
		entity_selected.emit(EntityRef.make(EntityRef.Kind.PRICE, price))
	else:
		var order_column: int = Column.MAKER if column == Column.MAKER else Column.TAKER
		var order_id: int = item.get_metadata(order_column)
		entity_selected.emit(EntityRef.make(EntityRef.Kind.ORDER, order_id))
