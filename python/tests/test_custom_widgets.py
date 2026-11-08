"""Custom-widget trampoline: Python classes subclassing slikui.Widget.

Layout participation, painting, events, exception propagation, identity,
anchor lifetime, root widgets, builder integration — plus the acceptance
test: a pure-Python Swatch renders pixel-identically to the C++ Swatch.
"""

import gc

import numpy as np
import pytest

import slikui


def abgr(color):
    """Pack a slikui.Color into the framebuffer's 0xAABBGGRR encoding."""
    return (color.a << 24) | (color.b << 16) | (color.g << 8) | color.r


class Swatch(slikui.Widget):
    """Python twin of the C++ Swatch: a 22x22 clickable color chip."""

    def __init__(self, color, selected=False):
        super().__init__()
        self.color = color
        self.selected = selected

    def measure(self, constraints):
        return constraints.clamp(slikui.Size(22, 22))

    def stretch_cross(self):
        return False

    def paint(self, env):
        b = self.bounds
        env.list.rect(b, self.color, 4)
        env.list.hollow_rect(b, env.theme.border, 1, 4)
        if self.selected:
            env.list.hollow_rect(b.inflate(2, 2), env.theme.accent, 2, 6)

    def on_event(self, event):
        if (event.type == slikui.EventType.MousePress and self.enabled
                and self.bounds.contains(event.pos)):
            self.selected = not self.selected
            return True
        return False


RED = slikui.Color(232, 64, 64)


class Fill(slikui.Widget):
    """Paints its assigned frame with one color."""

    def __init__(self, color):
        super().__init__()
        self.color = color

    def paint(self, env):
        env.list.rect(self.bounds, self.color)


def make_ui(w=200, h=200, **kw):
    ui = slikui.App(w, h, **kw)
    return ui, ui.set_root(slikui.Column, padding=10)


def test_layout_and_paint():
    ui, root = make_ui()
    row = root.add(slikui.Row, gap=8)
    sw = row.add(Swatch, RED)  # builder fallback route
    ui.tick(0.0, 1 / 60)
    assert (sw.bounds.w, sw.bounds.h) == (22, 22)
    assert sw.type == "Widget"  # no widget_type override → base name
    frame = ui.render()
    assert frame[21][21] == abgr(RED)  # center of the 22x22 chip at (10, 10)


def test_widget_type_override():
    class Named(slikui.Widget):
        def widget_type(self):
            return "Named"

    ui, root = make_ui()
    w = root.add(Named)
    assert w.type == "Named"
    assert w.type == "Named"  # second read: served from the trampoline cache


def test_events():
    ui, root = make_ui()
    row = root.add(slikui.Row)
    sw = row.add(Swatch, RED)
    ui.tick(0.0, 1 / 60)
    assert sw.selected is False
    ui.handle(slikui.mouse_press((21, 21)))  # inside the chip
    assert sw.selected is True
    ui.handle(slikui.mouse_press((199, 199)))  # outside → not consumed
    assert sw.selected is True


def test_anchor_keeps_widget_alive():
    ui, root = make_ui()
    row = root.add(slikui.Row)
    row.add(Swatch, RED)  # deliberately drop every Python reference
    gc.collect()
    ui.tick(0.0, 1 / 60)
    frame = ui.render()  # tree-side virtual call with no wrapper alive
    assert frame[21][21] == abgr(RED)
    ui.close()


def test_identity():
    ui, root = make_ui()
    row = root.add(slikui.Row)
    sw = row.add(Swatch, RED)
    sw.id = "picker"
    ui.tick(0.0, 1 / 60)
    assert row.children[0] is sw
    assert ui.ctx.find("picker") is sw
    assert isinstance(ui.ctx.find("picker"), Swatch)


def test_exceptions_propagate():
    class Broken(slikui.Widget):
        def paint(self, env):
            raise RuntimeError("boom")

    ui, root = make_ui()
    root.add(Broken)
    ui.tick(0.0, 1 / 60)
    with pytest.raises(RuntimeError, match="boom"):
        ui.render()
    ui.close()


def test_skipped_super_init_rejected():
    class Bad(slikui.Widget):
        def __init__(self):
            self.calls = 0  # no super().__init__() — C++ object uninitialized

    ui, root = make_ui()
    with pytest.raises(TypeError):
        root.add(Bad)
    ui.close()


def test_double_add_rejected():
    ui, root = make_ui()
    sw = root.add(Swatch, RED)
    with pytest.raises(TypeError, match="already in a widget tree"):
        root.add_widget(sw)
    ui.close()


def test_subclassed_bound_widget_rejected():
    class MySwatch2(slikui.Swatch):  # bound C++ type without a trampoline
        pass

    ui, root = make_ui()
    with pytest.raises(TypeError, match="cannot dispatch overrides"):
        root.add(MySwatch2, RED, lambda: False, lambda: None)
    ui.close()


def test_root_widget():
    ui = slikui.App(100, 80)
    ui.set_root(Fill, RED)  # builder fallback for set_root
    ui.tick(0.0, 1 / 60)
    frame = ui.render()  # default measure clamps to the frame under tight constraints
    assert frame[40][50] == abgr(RED)
    ui.close()


def test_save_image(tmp_path):
    """save_image writes a Pillow-loadable PNG with the frame's exact pixels."""
    from PIL import Image

    ui = slikui.App(120, 90)
    ui.set_root(Fill, RED)
    ui.tick(0.0, 1 / 60)
    frame = ui.render()

    out = tmp_path / "frame.png"
    assert slikui.save_image(frame, out) == out
    with Image.open(out) as im:
        assert im.format == "PNG" and im.size == (120, 90)
        px = im.convert("RGBA").getpixel((60, 45))  # (r, g, b, a)
        assert px == (RED.r, RED.g, RED.b, 255)
    ui.close()


def test_python_swatch_matches_cpp_swatch():
    """Acceptance: the Python Swatch renders pixel-identically to the C++ one."""
    colors = [slikui.Color(232, 64, 64), slikui.Color(96, 211, 148),
              slikui.Color(98, 160, 234)]

    def build(ui, add_swatch):
        root = ui.set_root(slikui.Column, padding=16, gap=8)
        row = root.add(slikui.Row, gap=8)
        row.add(slikui.Button, "Pick a color")
        for c in colors:
            add_swatch(row, c)

    ui_cpp = slikui.App(300, 120)
    build(ui_cpp, lambda row, c: row.add(slikui.Swatch, c, lambda: False, lambda: None))
    ui_py = slikui.App(300, 120)
    build(ui_py, lambda row, c: row.add(Swatch, c))

    assert np.array_equal(ui_cpp.render(), ui_py.render())
    ui_cpp.close()
    ui_py.close()
