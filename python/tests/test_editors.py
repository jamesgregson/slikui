"""Phase A editors: DragValue, VecNEdit, ColorEdit, Segmented, Slider
upgrades, clipboard + TextField selection. Mirrors tests/test_editors.cpp."""

import math

import numpy as np
import pytest

import slikui

CTRL = int(slikui.Mod.ModCtrl)
SHIFT = int(slikui.Mod.ModShift)


def make_ui(w=320, h=240, **kw):
    ui = slikui.App(w, h, **kw)
    return ui, ui.set_root(slikui.Column, padding=10, gap=8)


def center(w):
    r = w.bounds
    return (r.x + r.w / 2, r.y + r.h / 2)


def click(ui, pos):
    ui.handle(slikui.mouse_press(pos))
    ui.handle(slikui.mouse_release(pos))


def drag(ui, start, end):
    ui.handle(slikui.mouse_press(start))
    ui.handle(slikui.mouse_move(end))
    ui.handle(slikui.mouse_release(end))


def test_hsv_roundtrip():
    for rgb in (0x000000, 0xFFFFFF, 0x808080, 0xFF0000, 0x00FF00, 0x0000FF,
                0xFFFF00, 0x00FFFF, 0xFF00FF):
        c = slikui.Color.from_hex(rgb)
        h, s, v = c.to_hsv()
        assert slikui.Color.from_hsv(h, s, v) == c
    for rgb in (0xBF5C38, 0x4E9E58, 0x3F6FBF, 0x29529E, 0xF08C47):
        c = slikui.Color.from_hex(rgb)
        h, s, v = c.to_hsv()
        back = slikui.Color.from_hsv(h, s, v)
        assert all(abs(getattr(back, k) - getattr(c, k)) <= 1 for k in "rgb")
    # hue wraps, s/v clamp
    assert slikui.Color.from_hsv(-0.25, 1, 1) == slikui.Color.from_hsv(0.75, 1, 1)
    assert slikui.Color.from_hsv(0.3, 2, -1) == slikui.Color.black()


def test_dragvalue_scrub_and_type():
    ui, root = make_ui()
    changes = []
    dv = root.add(slikui.DragValue, 10.0)
    dv.on_change(lambda v: changes.append(v))
    ui.tick(0.0, 1 / 60)
    ui.render()

    drag(ui, center(dv), (center(dv)[0] + 50, center(dv)[1]))  # 50px * 0.02 = +1
    ui.tick(1 / 60, 1 / 60)
    assert dv.value == pytest.approx(11.0, abs=0.01)
    assert changes and changes[-1] == pytest.approx(11.0, abs=0.01)

    # A click (no drag) enters typed mode; typing replaces the entry.
    click(ui, center(dv))
    for e in slikui.text_input_str("42"):
        ui.handle(e)
    ui.handle(slikui.key_press(slikui.Key.Enter))
    ui.tick(1 / 60, 1 / 60)
    assert dv.value == 42.0
    assert len(changes) == 2


def test_dragvalue_escape_cancels():
    ui, root = make_ui()
    dv = root.add(slikui.DragValue, 1.5)
    ui.tick(0.0, 1 / 60)

    click(ui, center(dv))  # click → typing mode
    for e in slikui.text_input_str("999"):
        ui.handle(e)
    ui.handle(slikui.key_press(slikui.Key.Escape))  # Context drops focus
    ui.tick(1 / 60, 1 / 60)
    assert dv.value == pytest.approx(1.5)  # edit abandoned, not committed


def test_dragvalue_range_step_int():
    ui, root = make_ui()
    dv = root.add(slikui.DragValue, 0.0)
    dv.set_range(0.0, 10.0)
    dv.set_step(2.0)
    ui.tick(0.0, 1 / 60)
    dv.set_value(7.0)  # quantizes to the next multiple of 2
    assert dv.value == 8.0
    dv.set_value(99.0)  # clamps to max
    assert dv.value == 10.0

    dv.set_int_mode(True)
    dv.set_step(0.0)
    dv.set_range(0.0, 255.0)
    dv.set_value(0.0)
    ui.handle(slikui.key_press(slikui.Key.Tab))  # focus first focusable
    ui.handle(slikui.key_press(slikui.Key.Up))
    ui.handle(slikui.key_press(slikui.Key.Up))
    assert dv.value == 2.0


def test_vec3edit_borrowed_buffer():
    ui, root = make_ui()
    buf = np.zeros(3, dtype=np.float32)
    buf[:] = (1, 2, 3)
    changes = []
    vec = root.add(slikui.Vec3Edit, buf)
    vec.on_change(lambda: changes.append(True))
    ui.tick(0.0, 1 / 60)
    ui.render()
    assert vec.bounds.h > 0

    xfield = vec.children[0].children[0]
    drag(ui, center(xfield), (center(xfield)[0] + 60, center(xfield)[1]))  # +1.2
    ui.tick(1 / 60, 1 / 60)
    assert buf[0] == pytest.approx(2.2, abs=0.05)
    assert buf[1] == pytest.approx(2.0, abs=1e-6)
    assert len(changes) == 1

    # External writes to the array are mirrored on the next tick.
    buf[2] = 9.5
    ui.tick(1 / 60, 1 / 60)
    zfield = vec.children[0].children[2]
    assert zfield.value == pytest.approx(9.5, abs=1e-6)


def test_coloredit_popup_and_sv_drag():
    ui, root = make_ui()
    picked = []
    ce = root.add(slikui.ColorEdit, slikui.Color.from_hex(0xBF5C38))  # red hue
    ce.on_change(lambda c: picked.append(c))
    ui.tick(0.0, 1 / 60)
    ui.render()

    click(ui, center(ce))
    assert ui.ctx.popup_count == 1
    p = ui.ctx.popup(0)
    ui.tick(1 / 60, 1 / 60)

    # Drag inside the SV square toward its top-right (s→1, v→1).
    sx, sy = p.bounds.x + 8, p.bounds.y + 8
    drag(ui, (sx + 20, sy + 20), (sx + 150, sy + 2))
    ui.tick(1 / 60, 1 / 60)
    assert picked and picked[-1].r > 200  # strong red
    assert picked[-1].g < 80
    assert ce.color.r == picked[-1].r

    # Click outside dismisses.
    click(ui, (5, 5))
    assert ui.ctx.popup_count == 0


def test_segmented_select():
    ui, root = make_ui()
    picked = []
    seg = root.add(slikui.Segmented, items=["Cube", "Sphere", "Torus"])
    seg.on_select(lambda i: picked.append(i))
    ui.tick(0.0, 1 / 60)
    ui.render()
    assert seg.bounds.w > 0

    click(ui, center(seg))  # second segment sits at the horizontal midpoint
    assert seg.selected == 1
    assert picked == [1]

    seg.set_selected(2, fire_callback=False)
    assert seg.selected == 2
    assert picked == [1]


def test_slider_label_format_log_step():
    ui, root = make_ui()
    plain = root.add(slikui.Slider, 0.0, 100.0)
    labeled = root.add(slikui.Slider, 0.0, 1.0, label="Roughness", format="%.2f")
    log = root.add(slikui.Slider, 1.0, 100.0, log_scale=True)
    stepped = root.add(slikui.Slider, 0.0, 10.0, step=5.0)
    ui.tick(0.0, 1 / 60)
    ui.render()

    assert plain.bounds.h == 22
    assert labeled.bounds.h == 37  # track + label/value row

    click(ui, center(log))  # middle of a log slider = geometric mean
    assert log.value == pytest.approx(10.0, abs=0.5)

    click(ui, (stepped.bounds.x + 3, stepped.bounds.y + stepped.bounds.h / 2))
    assert stepped.value == 0.0
    click(ui, (stepped.bounds.x + 150, stepped.bounds.y + stepped.bounds.h / 2))
    assert stepped.value == 5.0


def test_clipboard_textfield():
    ui, root = make_ui(320, 120)
    stored = []
    ui.ctx.set_clipboard_sink(stored.append)
    ui.ctx.set_clipboard_source(lambda: stored[-1] if stored else "")

    tf = root.add(slikui.TextField)
    tf.set_text("hello world")
    ui.tick(0.0, 1 / 60)
    ui.handle(slikui.key_press(slikui.Key.Tab))  # focus

    ui.handle(slikui.key_press(slikui.Key.A, mods_=CTRL))
    ui.handle(slikui.key_press(slikui.Key.C, mods_=CTRL))
    assert stored == ["hello world"]

    # Drag-select from the caret to the end: still the full text.
    ui.handle(slikui.key_press(slikui.Key.Home))
    b = tf.bounds
    y = b.y + b.h / 2
    ui.handle(slikui.mouse_press((b.x + 8, y)))
    ui.handle(slikui.mouse_move((b.x + 250, y)))
    ui.handle(slikui.mouse_release((b.x + 250, y)))
    ui.handle(slikui.key_press(slikui.Key.C, mods_=CTRL))
    assert stored[-1] == "hello world"

    # Shift+Right selects exactly one character; cut it; paste twice.
    ui.handle(slikui.key_press(slikui.Key.Home))
    ui.handle(slikui.key_press(slikui.Key.Right, mods_=SHIFT))
    ui.handle(slikui.key_press(slikui.Key.X, mods_=CTRL))
    assert tf.text == "ello world"
    assert stored[-1] == "h"
    ui.handle(slikui.key_press(slikui.Key.V, mods_=CTRL))
    ui.handle(slikui.key_press(slikui.Key.V, mods_=CTRL))
    assert tf.text == "hhello world"


def test_clipboard_absent_is_silent():
    ui, root = make_ui()  # no clipboard callbacks installed
    tf = root.add(slikui.TextField)
    tf.set_text("data")
    ui.tick(0.0, 1 / 60)
    ui.handle(slikui.key_press(slikui.Key.Tab))
    ui.handle(slikui.key_press(slikui.Key.A, mods_=CTRL))
    ui.handle(slikui.key_press(slikui.Key.X, mods_=CTRL))  # no-op, no crash
    ui.tick(1 / 60, 1 / 60)
    assert tf.text == ""
