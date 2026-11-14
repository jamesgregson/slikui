"""slikui.ModernGLSink against the software rasterizer, offscreen.

Needs glfw + moderngl and a display; skipped otherwise. Same scenes and
tolerances as test_gl_sink.py (see gl_scenes.py).
"""

import numpy as np
import pytest

import slikui
from slikui._slikui import Context

moderngl = pytest.importorskip("moderngl")
from gl_scenes import (_drive, close_gl_window, hidden_gl_window, max_err,  # noqa: E402
                       render_software, shapes_tree, widgets_tree)


@pytest.fixture(scope="module")
def mgl():
    win = hidden_gl_window()
    ctx = moderngl.create_context()
    yield ctx
    ctx.release()
    close_gl_window(win)


class Target:
    """moderngl RGBA8 framebuffer, in use while alive."""

    def __init__(self, mgl, w, h):
        self.w, self.h = w, h
        self.tex = mgl.texture((w, h), 4)
        self.fbo = mgl.framebuffer(color_attachments=[self.tex])
        self.fbo.use()
        self.fbo.clear(0, 0, 0, 0)

    def read(self):
        raw = self.fbo.read(components=4, alignment=1)
        px = np.frombuffer(raw, np.uint8).reshape(self.h, self.w, 4)[::-1]
        return np.ascontiguousarray(px).view(np.uint32).reshape(self.h, self.w)

    def close(self, mgl):
        mgl.screen.use()
        self.fbo.release()
        self.tex.release()


def render_mgl(mgl, w, h, scale, build):
    tgt = Target(mgl, int(w * scale), int(h * scale))
    sink = slikui.ModernGLSink(mgl, scale=scale).init()
    try:
        ctx = _drive(sink, w, h, build)
        mgl.finish()
        out = tgt.read()
        ctx.clear_tree()
        return out
    finally:
        sink.shutdown()
        tgt.close(mgl)


@pytest.mark.parametrize("scale", [1.0, 2.0])
def test_shapes_match_software(mgl, scale):
    sw = render_software(260, 150, scale, shapes_tree)
    gl = render_mgl(mgl, 260, 150, scale, shapes_tree)
    e = max_err(gl, sw)
    assert e.max() <= 2, f"max channel error {e.max()}"


@pytest.mark.parametrize("scale", [1.0, 2.0])
def test_widgets_match_software(mgl, scale):
    sw = render_software(260, 360, scale, widgets_tree)
    gl = render_mgl(mgl, 260, 360, scale, widgets_tree)
    e = max_err(gl, sw)
    assert e.max() <= 2, f"max channel error {e.max()}"


def test_release_and_reupload(mgl):
    tgt = Target(mgl, 120, 90)
    sink = slikui.ModernGLSink(mgl).init()
    ctx = Context(sink)
    try:
        ctx.set_frame_size(120.0, 90.0)
        img = ctx.set_root(slikui.Column).add(slikui.Image, "img")
        img.set_pixels(np.full((4, 4, 4), 10, np.uint8))
        ctx.tick(0.0, 1 / 60)
        ctx.render()
        (tex,) = sink._textures.values()
        img.set_pixels(np.full((4, 4, 4), 99, np.uint8))
        ctx.render()
        assert list(sink._textures.values()) == [tex]   # same size: written in place
        ctx.set_root(slikui.Column)                     # Image destroyed
        assert sink._textures == {} and sink._pending_release == [tex]
        ctx.render()
        assert sink._pending_release == []
    finally:
        ctx.clear_tree()
        sink.shutdown()
        tgt.close(mgl)
    assert not sink.initialized


def test_reset_reuploads(mgl):
    tgt = Target(mgl, 260, 150)
    sink = slikui.ModernGLSink(mgl).init()
    ctx = Context(sink)
    try:
        ctx.set_frame_size(260.0, 150.0)
        ctx.load_default_font()
        widgets_tree(ctx)
        ctx.tick(0.0, 1 / 60)
        ctx.render()
        first = tgt.read().copy()
        n = len(sink._textures)
        sink.reset(ctx, mgl)
        assert sink._textures == {}
        tgt.fbo.clear(0, 0, 0, 0)
        ctx.render()
        assert len(sink._textures) == n
        assert np.array_equal(tgt.read(), first)
    finally:
        ctx.clear_tree()
        sink.shutdown()
        tgt.close(mgl)


def test_state_after_render(mgl):
    tgt = Target(mgl, 120, 90)
    sink = slikui.ModernGLSink(mgl).init()
    ctx = Context(sink)
    try:
        ctx.set_frame_size(120.0, 90.0)
        ctx.set_root(slikui.Column)
        ctx.tick(0.0, 1 / 60)
        mgl.enable(moderngl.DEPTH_TEST | moderngl.CULL_FACE)
        ctx.render()
        assert tgt.fbo.viewport == (0, 0, 120, 90)
        assert tgt.fbo.scissor == (0, 0, 120, 90)   # scissor test released
    finally:
        ctx.clear_tree()
        sink.shutdown()
        tgt.close(mgl)


def test_render_before_init_raises(mgl):
    sink = slikui.ModernGLSink(mgl)
    ctx = Context(sink)
    ctx.set_frame_size(50.0, 50.0)
    with pytest.raises(RuntimeError, match="before init"):
        ctx.render()
