"""Shared scenes and helpers for the GL sink tests (test_gl_sink.py,
test_moderngl_sink.py). Not a test module."""

import math

import numpy as np
import pytest

import slikui
from slikui._slikui import Context, Sink, Target


def hidden_gl_window():
    """Hidden GLFW window with a current GL 3.3 core context, or skip."""
    glfw = pytest.importorskip("glfw")
    if not glfw.init():
        pytest.skip("glfw.init failed (no display)")
    glfw.window_hint(glfw.VISIBLE, False)
    glfw.window_hint(glfw.CONTEXT_VERSION_MAJOR, 3)
    glfw.window_hint(glfw.CONTEXT_VERSION_MINOR, 3)
    glfw.window_hint(glfw.OPENGL_PROFILE, glfw.OPENGL_CORE_PROFILE)
    glfw.window_hint(glfw.OPENGL_FORWARD_COMPAT, True)
    win = glfw.create_window(64, 64, "slikui GL test", None, None)
    if not win:
        glfw.terminate()
        pytest.skip("no GL 3.3 core context")
    glfw.make_context_current(win)
    return win


def close_gl_window(win):
    import glfw
    glfw.destroy_window(win)
    glfw.terminate()


# -- scenes ------------------------------------------------------------------

class Shapes(slikui.Widget):
    """Fractional SDF geometry, borders, angled lines, nested clips. Kept on
    whole pixels where the software sink has documented quirks (radius-0
    fills, clips) - see tests/gl_parity.cpp."""

    def paint(self, env):
        L, C, R, V = env.list, slikui.Color, slikui.Rect, slikui.Vec2
        L.rect(R(10.3, 8.6, 60.4, 30.2), C(70, 130, 230), 6.0)
        L.rect(R(80, 8, 40, 30), C(230, 90, 60, 160), 0.0)
        L.hollow_rect(R(130.25, 8.75, 50, 30), C(240, 240, 240), 2.0, 8.0)
        L.hollow_rect(R(190, 8, 50, 30), C(120, 220, 120), 1.0, 0.0)
        L.circle(V(30.5, 70.25), 14.3, C(250, 200, 60))
        L.circle(V(70, 70), 9.0, C(250, 200, 60, 128))
        for i in range(8):
            a = i * 0.39
            L.line(V(150, 75), V(150 + 45 * math.cos(a), 75 + 45 * math.sin(a)),
                   C(200, 120, 255), 0.75 + 0.5 * i)
        L.push_clip(R(10, 100, 90, 40))
        L.rect(R(0, 95, 200, 30), C(60, 200, 200, 200), 0.0)
        L.rect(R(20.4, 112.3, 50, 20), C(250, 250, 120, 180), 9.0)
        L.push_clip(R(40, 0, 200, 300))
        L.line(V(0, 140), V(95, 110), C(255, 80, 120), 3.0)
        L.pop_clip()
        L.pop_clip()


def shapes_tree(ctx):
    ctx.set_root_widget(Shapes())


def widgets_tree(ctx):
    root = ctx.set_root(slikui.Column, padding=10, gap=6)
    root.add(slikui.Label, "GLSink parity - glyphs, shapes, clips")
    row = root.add(slikui.Row, gap=4)
    row.add(slikui.Button, "Primary", variant=slikui.ButtonVariant.Primary)
    row.add(slikui.Button, "Cancel")
    root.add(slikui.Checkbox, "Option", checked=True)
    root.add(slikui.Slider, 0.4)
    root.add(slikui.ProgressBar).set_value(0.65)
    scroll = root.add(slikui.ScrollArea)
    for i in range(12):
        scroll.add(slikui.Label, f"scrolled line {i}")
    y, x = np.mgrid[0:16, 0:24]
    img = np.stack([np.where((x // 4 + y // 4) % 2, 230, 30), x * 10, y * 16,
                    np.where((x + y) % 3, 255, 128)], -1).astype(np.uint8)
    root.add(slikui.Image, "img").set_pixels(img).set_size(48, 32)


def _drive(sink, w, h, build):
    ctx = Context(sink)
    ctx.set_frame_size(float(w), float(h))
    ctx.load_default_font()
    build(ctx)
    for i in range(2):
        ctx.tick(i / 60, 1 / 60)
        ctx.render()
    return ctx


def render_software(w, h, scale, build):
    target = Target(int(w * scale), int(h * scale))
    sink = Sink(target)
    sink.set_scale(scale)
    ctx = _drive(sink, w, h, build)
    ctx.clear_tree()
    return target.pixels().copy()


def max_err(a, b):
    a = a.view(np.uint8).reshape(*a.shape, 4).astype(int)
    b = b.view(np.uint8).reshape(*b.shape, 4).astype(int)
    return np.abs(a - b).max(-1)
