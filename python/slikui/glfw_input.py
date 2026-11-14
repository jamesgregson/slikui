"""GLFW -> slikui event translation for YOUR window (pyGLFW, ``import glfw``).

Nothing here creates a window or touches GL, and ``import slikui`` never
imports glfw (it is loaded on first use). Three levels, pick one:

1. Event builders - you own every GLFW callback and forward what you want::

       def on_button(win, button, action, mods):
           if not my_scene_wants_it():
               app.handle(slikui.glfw_input.mouse_button_event(win, button, action, mods))
       glfw.set_mouse_button_callback(win, on_button)

2. GlfwInput - installs the callbacks on an existing window, CHAINING to any
   you installed before (yours still run, first), and delivers events to a
   target (App, GlApp, Context - anything with ``handle(event)``) or queues
   them::

       input = slikui.GlfwInput(win, app)      # after your own callbacks
       ...
       glfw.poll_events()                      # events reach `app` as they arrive

       input = slikui.GlfwInput(win)           # no target: queue instead
       glfw.poll_events()
       for e in input.take():                  # route them yourself
           ...

3. The ``--window`` mode of the shipped examples (slikui/examples/_window.py)
   creates a window and uses GlfwInput.

``attach_services(ctx, win)`` wires the system clipboard and cursor shapes.

Coordinates: positions are GLFW cursor coordinates (window points, the
logical space Context lays out in); ``framebuffer_size_event`` carries pixels.
GLFW numbers mouse buttons LEFT=0, RIGHT=1, MIDDLE=2; slikui uses
MouseLeft=0, MouseMiddle=1, MouseRight=2 - always go through map_button.
"""

from __future__ import annotations

import slikui
from slikui._py import events as _ev

_glfw = None


def _g():
    global _glfw
    if _glfw is None:
        try:
            import glfw
        except ImportError as e:  # pragma: no cover - depends on environment
            raise ImportError("slikui.glfw_input needs pyGLFW: pip install glfw") from e
        _glfw = glfw
    return _glfw


# -- mapping ------------------------------------------------------------------

# slikui.Key name -> GLFW KEY_<NAME>, where the spelling differs.
_KEY_NAMES = {"PageUp": "PAGE_UP", "PageDown": "PAGE_DOWN",
              "CapsLock": "CAPS_LOCK", **{f"Num{d}": str(d) for d in range(10)}}
_key_map = None


def map_key(glfw_key):
    """GLFW key code -> slikui.Key (Key.Unknown when slikui has none)."""
    global _key_map
    if _key_map is None:
        glfw = _g()
        m = {}
        for name in dir(slikui.Key):
            if name.startswith("_") or name == "Unknown":
                continue
            code = getattr(glfw, "KEY_" + _KEY_NAMES.get(name, name.upper()), None)
            if isinstance(code, int):
                m[code] = getattr(slikui.Key, name)
        m[glfw.KEY_KP_ENTER] = slikui.Key.Enter
        _key_map = m
    return _key_map.get(glfw_key, slikui.Key.Unknown)


def map_button(glfw_button):
    """GLFW mouse button -> slikui.MouseButton value. GLFW's RIGHT (1) and
    MIDDLE (2) are the other way round from slikui's; extra buttons (3..7)
    pass through unchanged."""
    glfw = _g()
    if glfw_button == glfw.MOUSE_BUTTON_LEFT:
        return int(slikui.MouseButton.MouseLeft)
    if glfw_button == glfw.MOUSE_BUTTON_RIGHT:
        return int(slikui.MouseButton.MouseRight)
    if glfw_button == glfw.MOUSE_BUTTON_MIDDLE:
        return int(slikui.MouseButton.MouseMiddle)
    return int(glfw_button)


def map_mods(glfw_mods):
    """GLFW modifier bits -> slikui.Mod bits. Shift/Ctrl/Alt/Super share bit
    values; Caps/Num Lock (GLFW_LOCK_KEY_MODS) are dropped so exact-match
    shortcuts still fire."""
    return int(glfw_mods) & 0xF


def current_mods(window):
    """Held modifiers from the key state (cursor/scroll callbacks carry none)."""
    glfw = _g()
    m = 0
    for keys, bit in (((glfw.KEY_LEFT_SHIFT, glfw.KEY_RIGHT_SHIFT), slikui.Mod.ModShift),
                      ((glfw.KEY_LEFT_CONTROL, glfw.KEY_RIGHT_CONTROL), slikui.Mod.ModCtrl),
                      ((glfw.KEY_LEFT_ALT, glfw.KEY_RIGHT_ALT), slikui.Mod.ModAlt),
                      ((glfw.KEY_LEFT_SUPER, glfw.KEY_RIGHT_SUPER), slikui.Mod.ModSuper)):
        if any(glfw.get_key(window, k) == glfw.PRESS for k in keys):
            m |= int(bit)
    return m


# -- event builders: one per GLFW callback signature ------------------------------

def mouse_button_event(window, button, action, mods):
    glfw = _g()
    pos = glfw.get_cursor_pos(window)
    make = _ev.mouse_press if action == glfw.PRESS else _ev.mouse_release
    return make(pos, map_button(button), map_mods(mods))


def cursor_pos_event(window, x, y, last=None):
    """`last`: previous (x, y) for the delta, or None for no delta."""
    lx, ly = last if last is not None else (x, y)
    e = _ev.mouse_move((x, y), (x - lx, y - ly))
    e.mods = current_mods(window)
    return e


def scroll_event(window, dx, dy):
    e = _ev.scroll(_g().get_cursor_pos(window), (dx, dy))
    e.mods = current_mods(window)
    return e


def key_event(key, scancode, action, mods):
    """GLFW REPEAT is an auto-repeat press. Returns None for keys slikui has
    no Key for (the C++ builder sends Key.Unknown; widgets ignore both)."""
    glfw = _g()
    k = map_key(key)
    if k == slikui.Key.Unknown:
        return None
    if action == glfw.RELEASE:
        return _ev.key_release(k, map_mods(mods))
    return _ev.key_press(k, map_mods(mods), repeat=action == glfw.REPEAT)


def char_event(codepoint):
    return _ev.text_input(chr(codepoint))


def framebuffer_size_event(width, height):
    """FrameResize in framebuffer PIXELS."""
    return _ev.frame_resize(width, height)


def focus_event(focused):
    """Focus out -> FocusLost (drags, hover, held keys cancel); focus in -> None."""
    return None if focused else _ev.focus_lost()


# -- host services -------------------------------------------------------------------

_CURSOR_SHAPES = None


def attach_services(ctx, window):
    """System clipboard + cursor shapes for `window` on `ctx` (a Context)."""
    global _CURSOR_SHAPES
    glfw = _g()
    if _CURSOR_SHAPES is None:
        C = slikui.Cursor
        _CURSOR_SHAPES = {C.Arrow: glfw.ARROW_CURSOR, C.IBeam: glfw.IBEAM_CURSOR,
                          C.Hand: glfw.HAND_CURSOR, C.Grab: glfw.HAND_CURSOR,
                          C.Crosshair: glfw.CROSSHAIR_CURSOR,
                          C.ResizeH: glfw.HRESIZE_CURSOR, C.ResizeV: glfw.VRESIZE_CURSOR}
    cursors = {}

    def on_cursor_shape(shape):
        kind = _CURSOR_SHAPES.get(shape, glfw.ARROW_CURSOR)
        if kind == glfw.ARROW_CURSOR:
            glfw.set_cursor(window, None)
            return
        if kind not in cursors:
            cursors[kind] = glfw.create_standard_cursor(kind)
        glfw.set_cursor(window, cursors[kind])

    # The clipboard is global in GLFW (pyGLFW deprecates the window argument).
    def clipboard_text():
        raw = glfw.get_clipboard_string(None)  # bytes, or None when empty
        return raw.decode("utf-8", "replace") if raw else ""

    ctx.set_cursor_host(on_cursor_shape)
    ctx.set_clipboard_source(clipboard_text)
    ctx.set_clipboard_sink(lambda text: glfw.set_clipboard_string(None, text))


# -- GlfwInput -------------------------------------------------------------------------

_SETTERS = ("framebuffer_size", "window_focus", "mouse_button", "cursor_pos",
            "scroll", "key", "char")


class GlfwInput:
    """slikui's GLFW callbacks on an existing window, chained to yours."""

    def __init__(self, window, target=None):
        """`target`: App / GlApp / Context (anything with handle(event)) to
        deliver events to as GLFW reports them; None queues them (take())."""
        glfw = _g()
        self.window = window
        self.target = target
        self.events = []
        self._last = glfw.get_cursor_pos(window)
        self._prev = {}
        for name in _SETTERS:
            setter = getattr(glfw, f"set_{name}_callback")
            self._prev[name] = setter(window, getattr(self, f"_on_{name}"))

    def take(self):
        """Queued events (target=None), oldest first; empties the queue."""
        out, self.events = self.events, []
        return out

    def dispatch(self, target=None):
        """Deliver queued events to `target` (default: the constructor's)."""
        t = target if target is not None else self.target
        for e in self.take():
            t.handle(e)

    def close(self):
        """Restore the callbacks that were installed before. Call while the
        window is alive; safe twice."""
        if self._prev is None:
            return
        glfw = _g()
        for name, prev in self._prev.items():
            getattr(glfw, f"set_{name}_callback")(self.window, prev)
        self._prev = None

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()
        return False

    # -- callbacks --

    def _emit(self, e):
        if e is None:
            return
        if self.target is not None:
            self.target.handle(e)
        else:
            self.events.append(e)

    def _chain(self, name, *args):
        prev = self._prev.get(name) if self._prev else None
        if prev is not None:
            prev(*args)

    def _on_framebuffer_size(self, w, width, height):
        self._chain("framebuffer_size", w, width, height)
        self._emit(framebuffer_size_event(width, height))

    def _on_window_focus(self, w, focused):
        self._chain("window_focus", w, focused)
        self._emit(focus_event(focused))

    def _on_mouse_button(self, w, button, action, mods):
        self._chain("mouse_button", w, button, action, mods)
        self._emit(mouse_button_event(w, button, action, mods))

    def _on_cursor_pos(self, w, x, y):
        self._chain("cursor_pos", w, x, y)
        e = cursor_pos_event(w, x, y, self._last)
        self._last = (x, y)
        self._emit(e)

    def _on_scroll(self, w, dx, dy):
        self._chain("scroll", w, dx, dy)
        self._emit(scroll_event(w, dx, dy))

    def _on_key(self, w, key, scancode, action, mods):
        self._chain("key", w, key, scancode, action, mods)
        self._emit(key_event(key, scancode, action, mods))

    def _on_char(self, w, codepoint):
        self._chain("char", w, codepoint)
        self._emit(char_event(codepoint))
