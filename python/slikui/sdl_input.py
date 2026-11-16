"""SDL3 -> slikui event translation for YOUR window (PySDL3, ``import sdl3``).

Nothing here creates a window or touches GL, and ``import slikui`` never
imports sdl3 (it is loaded on first use). SDL hands events to the app through
its queue (``SDL_PollEvent``) rather than callbacks, so the levels are:

1. ``translate(ev)`` - one SDL_Event -> list of slikui events (empty when
   slikui has no use for it). You own the loop and forward what you want::

       ev = sdl3.SDL_Event()
       while sdl3.SDL_PollEvent(ctypes.byref(ev)):
           for e in slikui.sdl_input.translate(ev):
               if app.ctx.wants_mouse(e.pos) or not slikui.sdl_input.is_pointer(e):
                   app.handle(e)
               else:
                   my_scene(e)

2. ``SdlInput(window, target)`` - bound to one window: filters by window id,
   enables SDL text input on it, delivers to ``target`` (App, GlApp, Context
   - anything with ``handle(event)``) or queues (``take()``)::

       inp = slikui.SdlInput(window, app)
       while sdl3.SDL_PollEvent(ctypes.byref(ev)):
           if not inp.process(ev):
               my_app(ev)            # quit, gamepads, other windows, ...

``attach_services(ctx)`` wires the system clipboard and cursor shapes.

Coordinates: SDL3 reports mouse positions in window coordinates (points),
the logical space Context lays out in; FrameResize carries framebuffer
pixels. Keys map by KEYCODE (layout-aware). SDL numbers mouse buttons from
1 (LEFT=1, MIDDLE=2, RIGHT=3); map_button converts to slikui's 0-based ones.

PySDL3 note: by default it may download SDL binaries and check PyPI on
import. To use a system SDL3 (e.g. Homebrew) instead, set before importing:
``SDL_DOWNLOAD_BINARIES=0 SDL_CHECK_VERSION=0 SDL_FIND_BINARIES=1
SDL_DISABLE_METADATA=1``.
"""

from __future__ import annotations

import slikui
from slikui._py import events as _ev
from slikui._slikui import EventType

_sdl = None


def _s():
    global _sdl
    if _sdl is None:
        try:
            import sdl3
        except ImportError as e:  # pragma: no cover - depends on environment
            raise ImportError("slikui.sdl_input needs PySDL3: pip install pysdl3") from e
        _sdl = sdl3
    return _sdl


# -- mapping ------------------------------------------------------------------

_KEYS = None


def map_key(keycode):
    """SDL_Keycode -> slikui.Key (Key.Unknown when slikui has none)."""
    global _KEYS
    if _KEYS is None:
        s, K = _s(), slikui.Key
        named = {
            "RETURN": K.Enter, "KP_ENTER": K.Enter, "TAB": K.Tab,
            "BACKSPACE": K.Backspace, "DELETE": K.Delete, "ESCAPE": K.Escape,
            "SPACE": K.Space, "APOSTROPHE": K.Apostrophe, "COMMA": K.Comma,
            "MINUS": K.Minus, "PERIOD": K.Period, "SLASH": K.Slash,
            "EQUALS": K.Equal, "LEFTBRACKET": K.LeftBracket,
            "RIGHTBRACKET": K.RightBracket, "BACKSLASH": K.Backslash,
            "SEMICOLON": K.Semicolon, "GRAVE": K.Grave,
            "LEFT": K.Left, "RIGHT": K.Right, "UP": K.Up, "DOWN": K.Down,
            "HOME": K.Home, "END": K.End, "PAGEUP": K.PageUp,
            "PAGEDOWN": K.PageDown, "INSERT": K.Insert, "CAPSLOCK": K.CapsLock,
            **{f"F{i}": getattr(K, f"F{i}") for i in range(1, 13)},
            **{c: getattr(K, c) for c in "ABCDEFGHIJKLMNOPQRSTUVWXYZ"},
            **{str(d): getattr(K, f"Num{d}") for d in range(10)},
        }
        _KEYS = {int(getattr(s, f"SDLK_{n}")): k for n, k in named.items()}
    return _KEYS.get(int(keycode), slikui.Key.Unknown)


def map_mods(mod):
    """SDL_Keymod -> slikui.Mod bits (GUI = Cmd/Windows key -> Super; lock
    keys dropped so exact-match shortcuts still fire)."""
    s, M = _s(), slikui.Mod
    m = 0
    if mod & s.SDL_KMOD_SHIFT:
        m |= int(M.ModShift)
    if mod & s.SDL_KMOD_CTRL:
        m |= int(M.ModCtrl)
    if mod & s.SDL_KMOD_ALT:
        m |= int(M.ModAlt)
    if mod & s.SDL_KMOD_GUI:
        m |= int(M.ModSuper)
    return m


def map_button(button):
    """SDL button (LEFT=1, MIDDLE=2, RIGHT=3, X1=4, X2=5) -> slikui 0-based."""
    s, B = _s(), slikui.MouseButton
    if button == s.SDL_BUTTON_LEFT:
        return int(B.MouseLeft)
    if button == s.SDL_BUTTON_MIDDLE:
        return int(B.MouseMiddle)
    if button == s.SDL_BUTTON_RIGHT:
        return int(B.MouseRight)
    return int(button) - 1


def is_pointer(e):
    """True for slikui pointer events (route them with ctx.wants_mouse)."""
    return e.type in (EventType.MouseMove, EventType.MousePress,
                      EventType.MouseRelease, EventType.Scroll)


def event_window(ev):
    """The SDL window id an event belongs to (0 for global events)."""
    s, t = _s(), ev.type
    if t == s.SDL_EVENT_MOUSE_MOTION:
        return ev.motion.windowID
    if t in (s.SDL_EVENT_MOUSE_BUTTON_DOWN, s.SDL_EVENT_MOUSE_BUTTON_UP):
        return ev.button.windowID
    if t == s.SDL_EVENT_MOUSE_WHEEL:
        return ev.wheel.windowID
    if t in (s.SDL_EVENT_KEY_DOWN, s.SDL_EVENT_KEY_UP):
        return ev.key.windowID
    if t == s.SDL_EVENT_TEXT_INPUT:
        return ev.text.windowID
    if s.SDL_EVENT_WINDOW_FIRST <= t <= s.SDL_EVENT_WINDOW_LAST:
        return ev.window.windowID
    return 0


# -- translation -----------------------------------------------------------------

def translate(ev):
    """One SDL_Event -> list of slikui events ([] when slikui has no use for
    it). TEXT_INPUT yields one TextInput per codepoint."""
    s, t = _s(), ev.type
    if t == s.SDL_EVENT_MOUSE_MOTION:
        m = ev.motion
        e = _ev.mouse_move((m.x, m.y), (m.xrel, m.yrel))
        e.mods = map_mods(s.SDL_GetModState())
        return [e]
    if t in (s.SDL_EVENT_MOUSE_BUTTON_DOWN, s.SDL_EVENT_MOUSE_BUTTON_UP):
        b = ev.button
        make = _ev.mouse_press if b.down else _ev.mouse_release
        return [make((b.x, b.y), map_button(b.button), map_mods(s.SDL_GetModState()))]
    if t == s.SDL_EVENT_MOUSE_WHEEL:
        w = ev.wheel
        flip = -1.0 if w.direction == s.SDL_MOUSEWHEEL_FLIPPED else 1.0
        e = _ev.scroll((w.mouse_x, w.mouse_y), (w.x * flip, w.y * flip))
        e.mods = map_mods(s.SDL_GetModState())
        return [e]
    if t in (s.SDL_EVENT_KEY_DOWN, s.SDL_EVENT_KEY_UP):
        k = ev.key
        key = map_key(k.key)
        if key == slikui.Key.Unknown:
            return []
        if k.down:
            return [_ev.key_press(key, map_mods(k.mod), repeat=bool(k.repeat))]
        return [_ev.key_release(key, map_mods(k.mod))]
    if t == s.SDL_EVENT_TEXT_INPUT:
        raw = ev.text.text
        if not raw:
            return []
        text = raw.decode("utf-8", "replace") if isinstance(raw, bytes) else str(raw)
        return [_ev.text_input(ch) for ch in text]
    if t == s.SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        return [_ev.frame_resize(ev.window.data1, ev.window.data2)]
    if t == s.SDL_EVENT_WINDOW_FOCUS_LOST:
        return [_ev.focus_lost()]
    return []


# -- host services -------------------------------------------------------------------

_CURSORS = {}


def attach_services(ctx):
    """System clipboard + cursor shapes on `ctx` (SDL's are global)."""
    s, C = _s(), slikui.Cursor
    shapes = {C.IBeam: s.SDL_SYSTEM_CURSOR_TEXT, C.Hand: s.SDL_SYSTEM_CURSOR_POINTER,
              C.Grab: s.SDL_SYSTEM_CURSOR_MOVE, C.Crosshair: s.SDL_SYSTEM_CURSOR_CROSSHAIR,
              C.ResizeH: s.SDL_SYSTEM_CURSOR_EW_RESIZE, C.ResizeV: s.SDL_SYSTEM_CURSOR_NS_RESIZE}

    def on_cursor_shape(shape):
        kind = shapes.get(shape)
        if kind is None:
            s.SDL_SetCursor(s.SDL_GetDefaultCursor())
            return
        if kind not in _CURSORS:
            _CURSORS[kind] = s.SDL_CreateSystemCursor(kind)
        s.SDL_SetCursor(_CURSORS[kind])

    def clipboard_text():
        raw = s.SDL_GetClipboardText()
        if not raw:
            return ""
        return raw.decode("utf-8", "replace") if isinstance(raw, bytes) else str(raw)

    def set_clipboard(text):
        s.SDL_SetClipboardText(text.encode("utf-8"))

    ctx.set_cursor_host(on_cursor_shape)
    ctx.set_clipboard_source(clipboard_text)
    ctx.set_clipboard_sink(set_clipboard)


# -- SdlInput ---------------------------------------------------------------------------

class SdlInput:
    """One SDL window's events, translated for slikui."""

    def __init__(self, window, target=None):
        """`window`: an SDL_Window pointer. `target`: App / GlApp / Context
        (anything with handle(event)) to deliver to; None queues (take())."""
        s = _s()
        self.window = window
        self.window_id = s.SDL_GetWindowID(window)
        self.target = target
        self.events = []
        s.SDL_StartTextInput(window)  # SDL3 sends TEXT_INPUT only while active
        self._open = True

    def process(self, ev):
        """Translate one polled event if it belongs to this window. Returns
        whether slikui used it (False: another window's, or not UI input)."""
        w = event_window(ev)
        if w and w != self.window_id:
            return False
        out = translate(ev)
        for e in out:
            if self.target is not None:
                self.target.handle(e)
            else:
                self.events.append(e)
        return bool(out)

    def take(self):
        """Queued events (target=None), oldest first; empties the queue."""
        out, self.events = self.events, []
        return out

    def dispatch(self, target=None):
        t = target if target is not None else self.target
        for e in self.take():
            t.handle(e)

    def close(self):
        """Stop the SDL text input this started. Call while the window is
        alive; safe twice."""
        if self._open:
            _s().SDL_StopTextInput(self.window)
            self._open = False

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()
        return False
