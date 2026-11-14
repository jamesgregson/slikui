#!/usr/bin/env python3
"""Render benchmark: ms/frame for each sink on the shipped widget gallery.

Times `ctx.render()` only (layout + paint + the sink's render()), after the
tree has settled. Sinks:

    cpp          C++ software.Sink (the fast CPU reference)
    canvas       slikui.CanvasSink (pure numpy rasterizer)
    py-iter      Python RenderSink that only walks `for c in cmds` and reads
                 each command's fields: the cost of getting the draw list
                 into Python, with no drawing
    py-array     Python RenderSink reading `list.as_array()` (zero-copy view)
    gl           slikui.GLSink (PyOpenGL) into an offscreen FBO, glFinish()
                 included - needs glfw + PyOpenGL + a display
    mgl          slikui.ModernGLSink, same setup - needs moderngl

Run:  python python/benchmarks/bench_render.py [--frames N] [--scale S]
"""

from __future__ import annotations

import argparse
import statistics
import time

import slikui
from slikui._py.builder import context_set_root
from slikui._slikui import Context, Sink, Target
from slikui.examples import gallery


class _Host:
    """The slice of slikui.App that example build() functions use."""

    def __init__(self, ctx):
        self.ctx = ctx

    def set_root(self, cls, *args, **props):
        return context_set_root(self.ctx, cls, *args, **props)


class IterSink(slikui.RenderSink):
    """Walks every command and reads its fields; draws nothing."""

    def upload(self, up):
        pass

    def render(self, cmds, framebuffer_size):
        n = 0
        for c in cmds:
            r = c.rect
            col = c.color
            n += (r.x + r.y + r.w + r.h + c.radius + c.thickness
                  + col.r + col.a + c.tex_id) > -1
        self.count = n


class ArraySink(slikui.RenderSink):
    """Reads the zero-copy structured view; draws nothing."""

    def upload(self, up):
        pass

    def render(self, cmds, framebuffer_size):
        a = cmds.as_array()
        self.count = int((a["rect"].sum(axis=1) + a["radius"]).size)


def _make(kind, w, h, scale):
    pw, ph = int(w * scale), int(h * scale)
    keep = []
    if kind == "cpp":
        target = Target(pw, ph)
        sink = Sink(target)
        sink.set_scale(scale)
        keep.append(target)
    elif kind == "canvas":
        sink = slikui.CanvasSink(pw, ph, scale=scale)
    elif kind == "py-iter":
        sink = IterSink()
    elif kind == "py-array":
        if not hasattr(slikui.DrawList, "as_array"):
            return None
        sink = ArraySink()
    elif kind == "gl":
        if not _gl_context(pw, ph):
            return None
        sink = slikui.GLSink(scale=scale).init()
        keep.append(sink)
    elif kind == "mgl":
        try:
            import moderngl
        except ImportError:
            return None
        if not _gl_context(pw, ph):
            return None
        sink = slikui.ModernGLSink(moderngl.create_context(), scale=scale).init()
        keep.append(sink)
    else:
        raise ValueError(kind)
    ctx = Context(sink)
    ctx.set_frame_size(float(w), float(h))
    ctx.load_default_font()  # identical glyphs on every machine
    return ctx, sink, keep


_GL = {}


def _gl_context(w, h):
    """Hidden GLFW window + an FBO of the frame size, bound. False if no GL."""
    try:
        import glfw
        from OpenGL import GL
    except ImportError:
        return False
    if "win" not in _GL:
        if not glfw.init():
            return False
        glfw.window_hint(glfw.VISIBLE, False)
        glfw.window_hint(glfw.CONTEXT_VERSION_MAJOR, 3)
        glfw.window_hint(glfw.CONTEXT_VERSION_MINOR, 3)
        glfw.window_hint(glfw.OPENGL_PROFILE, glfw.OPENGL_CORE_PROFILE)
        glfw.window_hint(glfw.OPENGL_FORWARD_COMPAT, True)
        win = glfw.create_window(64, 64, "bench", None, None)
        if not win:
            return False
        glfw.make_context_current(win)
        _GL["win"] = win
    fbo = GL.glGenFramebuffers(1)
    tex = GL.glGenTextures(1)
    GL.glBindFramebuffer(GL.GL_FRAMEBUFFER, fbo)
    GL.glBindTexture(GL.GL_TEXTURE_2D, tex)
    GL.glTexImage2D(GL.GL_TEXTURE_2D, 0, GL.GL_RGBA8, w, h, 0, GL.GL_RGBA,
                    GL.GL_UNSIGNED_BYTE, None)
    GL.glFramebufferTexture2D(GL.GL_FRAMEBUFFER, GL.GL_COLOR_ATTACHMENT0,
                              GL.GL_TEXTURE_2D, tex, 0)
    _GL["finish"] = GL.glFinish
    return True


def bench(kind, frames, scale):
    w, h = gallery.SIZE
    made = _make(kind, w, h, scale)
    if made is None:
        return None
    ctx, sink, keep = made
    try:
        gallery.build(_Host(ctx))
        for i in range(3):  # settle layout, atlas, animations
            ctx.tick(i / 60, 1 / 60)
            ctx.render()
        times = []
        for i in range(frames):
            ctx.tick((i + 3) / 60, 1 / 60)
            t0 = time.perf_counter()
            ctx.render()
            if kind in ("gl", "mgl"):
                _GL["finish"]()  # count the GPU work, not just submission
            times.append((time.perf_counter() - t0) * 1e3)
        return times
    finally:
        ctx.clear_tree()
        if kind in ("gl", "mgl"):
            sink.shutdown()


def command_count(scale):
    w, h = gallery.SIZE
    ctx, sink, keep = _make("py-iter", w, h, scale)
    try:
        gallery.build(_Host(ctx))
        ctx.tick(0.0, 1 / 60)
        ctx.render()
        return sink.count
    finally:
        ctx.clear_tree()


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--frames", type=int, default=30)
    ap.add_argument("--scale", type=float, default=1.0)
    ap.add_argument("--sinks", default="cpp,canvas,py-iter,py-array,gl,mgl")
    args = ap.parse_args(argv)

    w, h = gallery.SIZE
    print(f"gallery {w}x{h} @ scale {args.scale}, "
          f"{command_count(args.scale)} draw commands, {args.frames} frames")
    print(f"{'sink':<10} {'median ms':>10} {'min ms':>8} {'max ms':>8}")
    for kind in args.sinks.split(","):
        frames = max(3, args.frames // 10) if kind == "canvas" else args.frames
        times = bench(kind, frames, args.scale)
        if times is None:
            print(f"{kind:<10} {'(not available in this build)':>28}")
            continue
        print(f"{kind:<10} {statistics.median(times):>10.3f} "
              f"{min(times):>8.3f} {max(times):>8.3f}")


if __name__ == "__main__":
    main()
