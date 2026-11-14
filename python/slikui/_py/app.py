"""Headless app shell: Target + Sink + Context with conveniences.

`App` holds every owner (target, sink, context), so the C++ side never sees a
dangling backend even though the bindings are non-owning.
"""

import atexit
import weakref

from slikui._py.builder import context_set_root
from slikui._slikui import Context, Sink, Target, find_system_font

# Weakrefs of Apps waiting for an atexit close; pruned as refs die.
_exit_close_refs = []


def _register_exit_close(app, method="close"):
    """Run app.close() during interpreter shutdown, before module teardown.

    Python-level atexit callbacks run while every module (slikui's extension
    included) is still healthy. Waiting for App.__del__ is too late: nanobind
    starts releasing its internals as soon as its module is freed during
    module clearing, before __main__'s globals are collected. The weakref
    keeps the App collectable; close() unregisters.
    """
    _exit_close_refs[:] = [r for r in _exit_close_refs if r() is not None]
    ref = weakref.ref(app)
    _exit_close_refs.append(ref)

    def _close():
        a = ref()
        if a is not None:
            try:
                getattr(a, method)()
            except Exception:
                pass

    app._exit_close = _close
    atexit.register(_close)


class App:
    def __init__(self, width, height, *, font=None, scale=1.0, theme=None):
        self.width = int(width)
        self.height = int(height)
        self.scale = float(scale)
        # Logical frame is width x height; the framebuffer carries the scale.
        self.target = Target(int(self.width * self.scale), int(self.height * self.scale))
        self.sink = Sink(self.target)
        self.sink.set_scale(self.scale)
        self.ctx = Context(self.sink)
        self.ctx.set_frame_size(float(self.width), float(self.height))
        if font is not None:
            self.ctx.load_font_file(font)
        elif not self.ctx.load_font_file(find_system_font()):
            # No system font (minimal containers, e.g. manylinux): fall back
            # to the bundled DejaVu Sans subset so text still renders.
            self.ctx.load_default_font()
        if theme is not None:
            self.ctx.theme = theme
        _register_exit_close(self)

    def set_root(self, cls, *args, **props):
        return context_set_root(self.ctx, cls, *args, **props)

    def resize(self, width, height):
        self.width, self.height = int(width), int(height)
        self.target.resize(int(self.width * self.scale), int(self.height * self.scale))
        self.ctx.set_frame_size(float(self.width), float(self.height))

    def tick(self, now=0.0, dt=0.0):
        self.ctx.tick(now, dt)

    def handle(self, event):
        self.ctx.handle(event)

    def render(self, clear=True):
        self.ctx.render(clear)
        return self.target.pixels()  # zero-copy (H, W) uint32, ABGR packing

    def close(self):
        """Destroy the widget tree now, releasing the Python callbacks it holds.

        A callback capturing a widget wrapper closes a cycle through C++
        ownership that the GC cannot see; destroying the tree while the
        interpreter is alive is what makes shutdown leak-free. Called
        automatically by __exit__/__del__/atexit; safe to call twice.
        """
        self.ctx.clear_tree()
        ec = getattr(self, "_exit_close", None)
        if ec is not None:
            atexit.unregister(ec)
            self._exit_close = None

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()
        return False

    def __del__(self):
        # Best effort: App is not part of any reference cycle (the cycle lives
        # between the context wrapper and callbacks inside the C++ tree), so in
        # the normal case this runs mid-interpreter and releases everything.
        # At interpreter shutdown the same call is harmless but too late; GUI
        # apps that keep their UI until exit can silence the report with
        # slikui.set_leak_warnings(False).
        try:
            self.close()
        except Exception:
            pass


class GlApp:
    """App over slikui.GLSink: draws the UI into the current OpenGL context.

    Same surface as App (set_root, handle, tick, ctx) for hosts that own a
    GL 3.3 window (glfw, pygame, Qt, ...). render() draws into whatever
    framebuffer is bound — no CPU framebuffer, no texture upload.

        app = slikui.GlApp(960, 640, scale=fb_width / win_width)  # context current
        ...
        app.tick(now, dt); app.render(); swap_buffers()
        ...
        app.close()          # BEFORE destroying the window: frees GL objects

    `resize(w, h)` takes logical points; `set_scale(s)` maps them to
    framebuffer pixels (HiDPI). If the GL context is lost or recreated, call
    `reset()` with the new context current.
    """

    def __init__(self, width, height, *, font=None, scale=1.0, theme=None,
                 preserve_state=False):
        from slikui._py.gl import GLSink

        self.width = int(width)
        self.height = int(height)
        self.scale = float(scale)
        self.sink = GLSink(scale=self.scale, preserve_state=preserve_state).init()
        self.ctx = Context(self.sink)
        self.ctx.set_frame_size(float(self.width), float(self.height))
        if font is not None:
            self.ctx.load_font_file(font)
        elif not self.ctx.load_font_file(find_system_font()):
            self.ctx.load_default_font()
        if theme is not None:
            self.ctx.theme = theme
        self.ctx.theme.scale = self.scale
        _register_exit_close(self, "_close_at_exit")

    def set_root(self, cls, *args, **props):
        return context_set_root(self.ctx, cls, *args, **props)

    def resize(self, width, height):
        """New logical size (points)."""
        self.width, self.height = int(width), int(height)
        self.ctx.set_frame_size(float(self.width), float(self.height))

    def set_scale(self, scale):
        """Framebuffer pixels per logical point (glfw: fb width / window width)."""
        self.scale = float(scale)
        self.sink.scale = self.scale
        self.ctx.theme.scale = self.scale

    def tick(self, now=0.0, dt=0.0):
        self.ctx.tick(now, dt)

    def handle(self, event):
        self.ctx.handle(event)

    def render(self, clear=True):
        """Draw into the bound framebuffer. clear=False: draw over the scene."""
        self.ctx.render(clear)

    def reset(self):
        """GL context lost/recreated: rebuild GL objects and re-upload."""
        self.sink.reset(self.ctx)

    def close(self):
        """Destroy the widget tree and free the GL objects. Needs the GL
        context current — call it before destroying the window. Safe twice."""
        self.ctx.clear_tree()
        if self.sink.initialized:
            self.sink.shutdown()
        self._unregister_exit()

    def _close_at_exit(self):
        # Interpreter shutdown: the window (and its GL context) may already
        # be gone, so no GL calls — the driver frees them with the context.
        self.ctx.clear_tree()
        self.sink._forget()
        self._unregister_exit()

    def _unregister_exit(self):
        ec = getattr(self, "_exit_close", None)
        if ec is not None:
            atexit.unregister(ec)
            self._exit_close = None

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()
        return False
