"""Headless checks for python/examples/glfw_demo.py — mesh math + UI build.

GLFW/GL are imported (they must resolve) but never initialized: every call
here runs without a window or GL context.
"""

import math

import numpy as np
import pytest

import slikui

pytest.importorskip("glfw", reason="pip install slikui-python[demo] for the glfw tests")
pytest.importorskip("OpenGL", reason="pip install slikui-python[demo] for the glfw tests")
from examples_glfw_demo_support import load_demo

demo = load_demo()


def test_meshes_are_wellformed():
    for gen, vcount in ((demo.make_cube, 24), (demo.make_sphere, 23 * 37),
                        (demo.make_torus, 45 * 23)):
        verts, idx = gen()
        structured = verts.reshape(-1, 6)
        assert verts.dtype == np.float32 and verts.size % 6 == 0
        assert structured.shape[0] == vcount
        assert idx.dtype == np.uint16
        assert idx.size % 3 == 0 and idx.max() < vcount
        # every triangle's geometric normal agrees with its vertex normals
        # (the sphere's pole triangles are degenerate — zero area — so allow
        # a tiny negative tolerance there)
        t = idx.reshape(-1, 3).astype(np.int64)
        e1 = structured[t[:, 1], :3] - structured[t[:, 0], :3]
        e2 = structured[t[:, 2], :3] - structured[t[:, 0], :3]
        g = np.cross(e1, e2)
        d = (structured[t[:, 0], 3:] + structured[t[:, 1], 3:] + structured[t[:, 2], 3:]) / 3.0
        assert ((g * d).sum(axis=1) > -1e-6).all()
        # unit-length analytic normals
        n = structured[:, 3:]
        assert np.allclose(np.linalg.norm(n, axis=1), 1.0, atol=1e-5)


def test_mat4_math():
    p = demo.perspective(90 * math.pi / 180, 1.0, 0.1, 100.0)
    # column-major: f = 1/tan(fov/2) = 1 at 90deg, z terms in slots 10/11/14
    assert math.isclose(p[0], 1.0) and math.isclose(p[5], 1.0)
    assert math.isclose(p[10], (100.0 + 0.1) / (0.1 - 100.0), rel_tol=1e-5)
    assert math.isclose(p[11], -1.0)
    assert math.isclose(p[14], 2.0 * 100.0 * 0.1 / (0.1 - 100.0), rel_tol=1e-5)
    v = demo.look_at((0, 0, 5), (0, 0, 0))
    # eye on +Z looking at origin: translation column carries -5*z
    assert math.isclose(v[14], -5.0)


def test_ui_builds_headless():
    app = slikui.App(960, 640)
    state = demo.State()
    panel = demo.build_ui(app, state)
    app.tick(0.0, 1 / 60)
    assert panel.bounds.w > 0 and panel.bounds.h > 0  # docked top-right, sized
    assert app.ctx.popup_count == 0
    # shape dropdown drives state.shape through the real widget path
    assert state.shape == 0
    state.shape = 2  # torus selected (what on_select would set)
    assert list(demo.SKY_PRESETS)[0] == "Day"
    # swatch pick edits the target vector in place
    color = slikui.Color.from_hex(0x4E9E58)
    demo.set_vec3(state.albedo, color)
    assert demo.vec3_close(state.albedo, color)
    # sliders clamp and report through set_value
    state.reset_camera()
    assert (state.yaw, state.pitch, state.distance) == (0.6, 0.5, 4.6)


def test_over_ui_uses_popup_bounds():
    app = slikui.App(400, 300)
    state = demo.State()
    panel = demo.build_ui(app, state)
    app.tick(0.0, 1 / 60)
    b = panel.bounds
    assert demo.over_ui(app, panel, b.x + 5, b.y + 5)
    assert not demo.over_ui(app, panel, 5, 5)
