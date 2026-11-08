"""Phase C data display: Plot (+bars/fill), Vec2Pad, TreeView, Table,
PropertyGrid. Mirrors tests/test_data.cpp."""

import numpy as np
import pytest

import slikui


def make_ui(w=320, h=240):
    ui = slikui.App(w, h)
    return ui, ui.set_root(slikui.Column)


def center(w):
    r = w.bounds
    return (r.x + r.w / 2, r.y + r.h / 2)


def click(ui, pos):
    ui.handle(slikui.mouse_press(pos))
    ui.handle(slikui.mouse_release(pos))


def count_ink(frame, r, pred):
    x0, y0 = int(r.x), int(r.y)
    n = 0
    for y in range(y0, int(r.y + r.h)):
        for x in range(x0, int(r.x + r.w)):
            if pred(int(frame[y, x])):
                n += 1
    return n


def bright(p):
    r, g = p & 0xFF, (p >> 8) & 0xFF
    return r > 90 or g > 90


def test_plot_autoscale_lines_and_bars():
    ui, root = make_ui()
    plot = root.add(slikui.Plot)
    ys = np.array([i % 2 for i in range(11)], dtype=np.float32)
    plot.set_series(ys, slikui.Color.from_hex(0x4C8DFF))
    plot.set_markers(True)
    ui.tick(0.0, 1 / 60)
    ui.render()
    assert plot.bounds.w >= 150 and plot.bounds.h >= 90
    assert count_ink(ui.render(), plot.bounds, bright) > 40

    # Bars mode paints orange columns.
    plot.clear_series()
    plot.set_bars(True)
    plot.set_series(np.arange(1, 9, dtype=np.float32),
                    slikui.Color.from_hex(0xF5A623))
    ui.tick(1 / 60, 1 / 60)
    ui.render()
    orange = lambda p: (p & 0xFF) > 150 and ((p >> 8) & 0xFF) > 90
    assert count_ink(ui.render(), plot.bounds, orange) > 60


def test_plot_fixed_range_and_multi_series():
    ui, root = make_ui()
    plot = root.add(slikui.Plot)
    plot.set_y_range(0.0, 1.0)
    plot.set_series(np.ones(5, dtype=np.float32), slikui.Color.from_hex(0x4C8DFF))
    xs = np.arange(5, dtype=np.float32)
    plot.add_series(xs, np.zeros(5, dtype=np.float32),
                    slikui.Color.from_hex(0xE5484D))
    ui.tick(0.0, 1 / 60)
    frame = ui.render()
    # y=1 pins to the top of the plot area, y=0 to the bottom: ink high and low.
    top = count_ink(frame,
                    slikui.Rect(plot.bounds.x, plot.bounds.y, plot.bounds.w, 12),
                    bright)
    assert top > 0


def test_vec2pad_drag_and_clamp():
    ui, root = make_ui()
    changes = []
    pad = root.add(slikui.Vec2Pad)
    pad.on_change(lambda v: changes.append((v.x, v.y)))
    ui.tick(0.0, 1 / 60)

    c = center(pad)
    ui.handle(slikui.mouse_press(c))
    far = (pad.bounds.x + pad.bounds.w + 30, pad.bounds.y - 30)
    ui.handle(slikui.mouse_move(far))
    ui.handle(slikui.mouse_release(far))
    assert (pad.value.x, pad.value.y) == (1.0, 1.0)
    assert len(changes) == 1

    pad.set_value(slikui.Vec2(5, -5), fire_callback=False)
    assert (pad.value.x, pad.value.y) == (1.0, 0.0)
    assert len(changes) == 1

    pad.set_range(slikui.Vec2(-1, -1), slikui.Vec2(1, 1))
    pad.set_value(slikui.Vec2(0, 0), fire_callback=False)
    assert (pad.value.x, pad.value.y) == (0.0, 0.0)


def test_treeview_expand_collapse_select():
    ui, root = make_ui()
    tree = root.add(slikui.TreeView)
    tree.set_rows([
        slikui.TreeView.Row("Scene", 0, True),
        slikui.TreeView.Row("Mesh", 1),
        slikui.TreeView.Row("Light", 1),
        slikui.TreeView.Row("Camera", 0),
    ])
    ui.tick(0.0, 1 / 60)
    assert tree.bounds.h == 96
    assert tree.visible_count == 4

    picked = []
    tree.on_select(lambda i: picked.append(i))

    tree.set_expanded(0, False)
    ui.tick(1 / 60, 1 / 60)
    assert tree.bounds.h == 48
    assert tree.visible_count == 2

    # Chevron click re-expands.
    click(ui, (tree.bounds.x + 8, tree.bounds.y + 12))
    ui.tick(1 / 60, 1 / 60)
    assert tree.expanded(0)
    assert tree.visible_count == 4

    # Row click selects.
    click(ui, (tree.bounds.x + 60, tree.bounds.y + 36))
    assert picked == [1]
    assert tree.selected == 1


def test_table_header_rows_selection():
    ui, root = make_ui()
    table = root.add(slikui.Table)
    table.set_columns([
        slikui.Table.ColumnSpec("Name", 120),
        slikui.Table.ColumnSpec("Tris", 70, right_align=True),
        slikui.Table.ColumnSpec("Passes", 70, right_align=True),
    ])
    table.set_rows([["Dragon", "870k", "4"], ["Suzanne", "50k", "2"],
                    ["Bunny", "69k", "2"]])
    ui.tick(0.0, 1 / 60)
    assert table.bounds.w >= 260
    assert table.bounds.h == 96

    picked = []
    table.on_select(lambda r: picked.append(r))
    click(ui, (table.bounds.x + 40, table.bounds.y + 24 + 36))
    assert picked == [1]
    assert table.selected == 1

    # Header clicks don't select.
    click(ui, (table.bounds.x + 40, table.bounds.y + 12))
    assert picked == [1]


def test_propertygrid_rows_and_sections():
    ui, root = make_ui()
    grid = root.add(slikui.PropertyGrid)
    rough = grid.add_property("Roughness")
    rough.add(slikui.Slider, 0, 1)
    grid.add_property("Samples").add(slikui.DragValue, 64)
    mat = grid.add_section("Material", open=True)
    mat.add_property("Metallic").add(slikui.Slider, 0, 1)
    grid.add_section("Advanced")
    ui.tick(0.0, 1 / 60)
    ui.render()

    assert rough.editor().bounds.x >= rough.bounds.x + 110
    assert rough.label().bounds.w <= 112
    assert rough.editor().bounds.w > 40
    assert mat.bounds.h > 0
    assert grid.bounds.h >= 72
