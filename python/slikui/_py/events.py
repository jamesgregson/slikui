"""Friendly slikui::Event constructors — for tests, apps and window bridges.

`Context.handle()` consumes these directly. Positions are logical points
(Vec2-compatible pairs); `mods` accepts an int mask or a string like
"ctrl+shift". A typical click is `mouse_press(p)` then `mouse_release(p)`.
"""

from slikui._slikui import Event, EventType, Key, Mod, MouseButton, Vec2

_MOD_NAMES = {
    "shift": int(Mod.ModShift),
    "ctrl": int(Mod.ModCtrl),
    "control": int(Mod.ModCtrl),
    "alt": int(Mod.ModAlt),
    "option": int(Mod.ModAlt),
    "super": int(Mod.ModSuper),
    "cmd": int(Mod.ModSuper),
    "meta": int(Mod.ModSuper),
}


def mods(value) -> int:
    """Int mask, or a string like "ctrl+shift" / "alt|cmd"."""
    if value is None:
        return 0
    if isinstance(value, int):
        return value
    mask = 0
    for part in str(value).replace("|", "+").split("+"):
        name = part.strip().lower()
        if name:
            mask |= _MOD_NAMES[name]
    return mask


def mouse_move(pos, delta=(0.0, 0.0)):
    e = Event(EventType.MouseMove)
    e.pos = Vec2(*pos)
    e.delta = Vec2(*delta)
    return e


def mouse_press(pos, button=MouseButton.MouseLeft, mods_=0):
    e = Event(EventType.MousePress)
    e.pos = Vec2(*pos)
    e.button = int(button)
    e.mods = mods(mods_)
    return e


def mouse_release(pos, button=MouseButton.MouseLeft, mods_=0):
    e = Event(EventType.MouseRelease)
    e.pos = Vec2(*pos)
    e.button = int(button)
    e.mods = mods(mods_)
    return e


def mouse_click(pos, button=MouseButton.MouseLeft, mods_=0):
    """Press + release pair at `pos`."""
    return (mouse_press(pos, button, mods_), mouse_release(pos, button, mods_))


def scroll(pos, ticks):
    e = Event(EventType.Scroll)
    e.pos = Vec2(*pos)
    e.scroll = Vec2(*ticks)
    return e


def key_press(key, mods_=0):
    e = Event(EventType.KeyPress)
    e.key = key
    e.mods = mods(mods_)
    return e


def key_release(key, mods_=0):
    e = Event(EventType.KeyRelease)
    e.key = key
    e.mods = mods(mods_)
    return e


def text_input(char: str):
    e = Event(EventType.TextInput)
    e.codepoint = ord(char)
    return e


def text_input_str(text: str):
    """One TextInput event per Unicode scalar."""
    return [text_input(ch) for ch in text]


def focus_lost():
    return Event(EventType.FocusLost)


def frame_resize(width, height):
    e = Event(EventType.FrameResize)
    e.pos = Vec2(float(width), float(height))
    return e
