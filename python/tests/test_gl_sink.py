"""slikui.GLSink (PyOpenGL) against the software rasterizer, offscreen.

Needs glfw + PyOpenGL and a display; skipped otherwise (headless CI). Same
tolerances and reasoning as tests/gl_parity.cpp: the shapes scene must match
to 2/255; the widget scene carries a pixel budget for the software sink's
glyph-grid quirk (its glyph quads start at int(x)/int(y), painting rows whose
pixel centers are outside the quad).
"""

import math

import numpy as np
import pytest

import slikui
from slikui._slikui import Context, Sink, Target

glfw = pytest.importorskip("glfw")
GL = pytest.importorskip("OpenGL.GL")


@pytest.fixture(scope="module")
def gl_context():
    if not glfw.init():
        pytest.skip("glfw.init failed (no display)")
    glfw.window_hint(glfw.VISIBLE, False)
    glfw.window_hint(glfw.CONTEXT_VERSION_MAJOR, 3)
    glfw.window_hint(glfw.CONTEXT_VERSION_MINOR, 3)
    glfw.window_hint(glfw.OPENGL_PROFILE, glfw.OPENGL_CORE_PROFILE)
    glfw.window_hint(glfw.OPENGL_FORWARD_COMPAT, True)
    win = glfw.create_window(64, 64, "slikui GLSink test", None, None)
    if not win:
        glfw.terminate()
        pytest.skip("no GL 3.3 core context")
    glfw.make_context_current(win)
    yield win
    glfw.destroy_window(win)
    glfw.terminate()


class Fbo:
    """Offscreen RGBA8 target, bound while in use."""

    def __init__(self, w, h):
        self.w, self.h = w, h
        self.fbo = GL.glGenFramebuffers(1)
        self.tex = GL.glGenTextures(1)
        GL.glBindFramebuffer(GL.GL_FRAMEBUFFER, self.fbo)
        GL.glBindTexture(GL.GL_TEXTURE_2D, self.tex)
        GL.glTexImage2D(GL.GL_TEXTURE_2D, 0, GL.GL_RGBA8, w, h, 0, GL.GL_RGBA,
                        GL.GL_UNSIGNED_BYTE, None)
        GL.glFramebufferTexture2D(GL.GL_FRAMEBUFFER, GL.GL_COLOR_ATTACHMENT0,
                                  GL.GL_TEXTURE_2D, self.tex, 0)
        assert (GL.glCheckFramebufferStatus(GL.GL_FRAMEBUFFER)
                == GL.GL_FRAMEBUFFER_COMPLETE)
        GL.glDisable(GL.GL_SCISSOR_TEST)
        GL.glClearColor(0, 0, 0, 0)
        GL.glClear(GL.GL_COLOR_BUFFER_BIT)

    def read(self):
        return slikui.read_framebuffer(self.w, self.h)

    def close(self):
        GL.glBindFramebuffer(GL.GL_FRAMEBUFFER, 0)
        GL.glDeleteTextures([self.tex])
        GL.glDeleteFramebuffers(1, [self.fbo])


# -- scenes ------------------------------------------------------------------

class Shapes(slikui.Widget):
    """Fractional SDF geometry, borders, angled lines, nested clips. Kept on
    whole pixels where the software sink has documented quirks (radius-0
    fills, clips) — see tests/gl_parity.cpp."""

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
    root.add(slikui.Label, "GLSink parity — glyphs, shapes, clips")
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


def render_gl(w, h, scale, build, **kw):
    fbo = Fbo(int(w * scale), int(h * scale))
    sink = slikui.GLSink(scale=scale, **kw).init()
    try:
        ctx = _drive(sink, w, h, build)
        GL.glFinish()
        out = fbo.read()
        ctx.clear_tree()
        return out, sink
    finally:
        sink.shutdown()
        fbo.close()


def max_err(a, b):
    a = a.view(np.uint8).reshape(*a.shape, 4).astype(int)
    b = b.view(np.uint8).reshape(*b.shape, 4).astype(int)
    return np.abs(a - b).max(-1)


# -- parity --------------------------------------------------------------------

@pytest.mark.parametrize("scale", [1.0, 2.0])
def test_shapes_match_software(gl_context, scale):
    sw = render_software(260, 150, scale, shapes_tree)
    gl, _ = render_gl(260, 150, scale, shapes_tree)
    e = max_err(gl, sw)
    assert e.max() <= 2, f"max channel error {e.max()}"


@pytest.mark.parametrize("scale", [1.0, 2.0])
def test_widgets_match_software(gl_context, scale):
    sw = render_software(260, 360, scale, widgets_tree)
    gl, _ = render_gl(260, 360, scale, widgets_tree)
    e = max_err(gl, sw)
    assert (e > 2).mean() <= 0.02


# -- lifecycle -------------------------------------------------------------------

def test_release_deletes_on_next_render(gl_context):
    fbo = Fbo(120, 90)
    sink = slikui.GLSink().init()
    ctx = Context(sink)
    try:
        ctx.set_frame_size(120.0, 90.0)
        root = ctx.set_root(slikui.Column)
        root.add(slikui.Image, "img").set_pixels(np.full((4, 4, 4), 200, np.uint8))
        ctx.tick(0.0, 1 / 60)
        ctx.render()
        (img_tex,) = [t[0] for t in sink._textures.values() if t[3]]
        assert GL.glIsTexture(img_tex)
        ctx.set_root(slikui.Column)       # destroys the Image -> release()
        assert img_tex in sink._pending_delete
        assert GL.glIsTexture(img_tex)    # not yet: queued
        ctx.render()
        assert not GL.glIsTexture(img_tex)
        assert sink._pending_delete == []
    finally:
        ctx.clear_tree()
        sink.shutdown()
        fbo.close()


def test_reupload_same_size_reuses_texture(gl_context):
    fbo = Fbo(120, 90)
    sink = slikui.GLSink().init()
    ctx = Context(sink)
    try:
        ctx.set_frame_size(120.0, 90.0)
        img = ctx.set_root(slikui.Column).add(slikui.Image, "img")
        img.set_pixels(np.full((4, 4, 4), 10, np.uint8))
        ctx.tick(0.0, 1 / 60)
        ctx.render()
        before = dict(sink._textures)
        img.set_pixels(np.full((4, 4, 4), 99, np.uint8))
        ctx.render()
        assert sink._textures == before   # same GL name, same size
        img.set_pixels(np.full((8, 4, 4), 99, np.uint8))
        ctx.render()
        (entry,) = [t for t in sink._textures.values() if t[3]]
        assert entry[1:3] == (4, 8)
    finally:
        ctx.clear_tree()
        sink.shutdown()
        fbo.close()


def test_reset_reuploads_and_renders_the_same(gl_context):
    fbo = Fbo(260, 150)
    sink = slikui.GLSink().init()
    ctx = Context(sink)
    try:
        ctx.set_frame_size(260.0, 150.0)
        ctx.load_default_font()
        widgets_tree(ctx)
        ctx.tick(0.0, 1 / 60)
        ctx.render()
        first = fbo.read().copy()
        n_tex = len(sink._textures)
        # Simulate a lost context: the old names are gone, not deleted by us.
        old = [t[0] for t in sink._textures.values()]
        sink.reset(ctx)
        assert sink._textures == {}
        GL.glDeleteTextures(old)          # what the driver did for us
        GL.glClear(GL.GL_COLOR_BUFFER_BIT)
        ctx.render()
        assert len(sink._textures) == n_tex
        assert np.array_equal(fbo.read(), first)
    finally:
        ctx.clear_tree()
        sink.shutdown()
        fbo.close()


def test_shutdown_frees_everything(gl_context):
    fbo = Fbo(120, 90)
    sink = slikui.GLSink().init()
    ctx = Context(sink)
    ctx.set_frame_size(120.0, 90.0)
    ctx.load_default_font()
    ctx.set_root(slikui.Column).add(slikui.Label, "x")
    ctx.tick(0.0, 1 / 60)
    ctx.render()
    names = [t[0] for t in sink._textures.values()]
    prog, vao, vbo = sink._program, sink._vao, sink._vbo
    ctx.clear_tree()
    sink.shutdown()
    sink.shutdown()  # idempotent
    assert not sink.initialized
    assert not any(GL.glIsTexture(n) for n in names)
    assert not GL.glIsProgram(prog)
    assert not GL.glIsVertexArray(vao)
    assert not GL.glIsBuffer(vbo)
    fbo.close()


def test_render_before_init_raises(gl_context):
    sink = slikui.GLSink()
    ctx = Context(sink)
    ctx.set_frame_size(50.0, 50.0)
    with pytest.raises(RuntimeError, match="before init"):
        ctx.render()


# -- GL state contract -------------------------------------------------------------

def _state():
    return {
        "depth": bool(GL.glIsEnabled(GL.GL_DEPTH_TEST)),
        "cull": bool(GL.glIsEnabled(GL.GL_CULL_FACE)),
        "blend": bool(GL.glIsEnabled(GL.GL_BLEND)),
        "scissor": bool(GL.glIsEnabled(GL.GL_SCISSOR_TEST)),
        "viewport": [int(v) for v in GL.glGetIntegerv(GL.GL_VIEWPORT)],
        "program": int(GL.glGetIntegerv(GL.GL_CURRENT_PROGRAM)),
    }


def _host_state():
    GL.glEnable(GL.GL_DEPTH_TEST)
    GL.glEnable(GL.GL_CULL_FACE)
    GL.glDisable(GL.GL_BLEND)
    GL.glDisable(GL.GL_SCISSOR_TEST)
    GL.glViewport(3, 4, 50, 60)
    GL.glUseProgram(0)


@pytest.mark.parametrize("preserve", [False, True])
def test_state_contract(gl_context, preserve):
    fbo = Fbo(120, 90)
    sink = slikui.GLSink(preserve_state=preserve).init()
    ctx = Context(sink)
    try:
        ctx.set_frame_size(120.0, 90.0)
        ctx.set_root(slikui.Column)
        ctx.tick(0.0, 1 / 60)
        _host_state()
        before = _state()
        ctx.render()
        after = _state()
        if preserve:
            assert after == before
        else:
            assert after["depth"] is False and after["cull"] is False
            assert after["blend"] is True and after["scissor"] is True
            assert after["viewport"] == [0, 0, 120, 90]
    finally:
        GL.glDisable(GL.GL_DEPTH_TEST)
        GL.glDisable(GL.GL_CULL_FACE)
        ctx.clear_tree()
        sink.shutdown()
        fbo.close()


# -- GlApp -----------------------------------------------------------------------

def test_gl_app_renders_like_app(gl_context):
    def build(app):
        root = app.set_root(slikui.Column, padding=8, gap=4)
        root.add(slikui.Button, "OK")
        root.add(slikui.ProgressBar).set_value(0.5)

    ref = slikui.App(160, 100)
    build(ref)
    ref.tick(0.0, 1 / 60)
    sw = ref.render().copy()
    ref.close()

    fbo = Fbo(160, 100)
    app = slikui.GlApp(160, 100, font=None)
    try:
        build(app)
        app.tick(0.0, 1 / 60)
        app.render()
        e = max_err(fbo.read(), sw)
        assert (e > 2).mean() <= 0.02
        app.set_scale(2.0)
        assert app.sink.scale == 2.0 and app.ctx.theme.scale == 2.0
    finally:
        app.close()
        app.close()  # idempotent
        fbo.close()
    assert not app.sink.initialized


def test_gl_app_exit_hook_makes_no_gl_calls(gl_context, monkeypatch):
    app = slikui.GlApp(80, 60)
    app.set_root(slikui.Column).add(slikui.Label, "bye")
    calls = []
    monkeypatch.setattr(app.sink, "shutdown", lambda: calls.append("shutdown"))
    app._close_at_exit()
    assert calls == [] and not app.sink.initialized
    assert app.ctx.root is None
