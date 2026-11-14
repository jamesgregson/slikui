"""slikui.sdl_input: SDL3 -> slikui events through SDL's real queue (PySDL3).

Events go in with SDL_PushEvent and come back out of SDL_PollEvent, as in a
real app. Dummy video driver: no display or GL needed. Skipped without
PySDL3 or an SDL3 library.
"""

import ctypes
import os

import pytest

import slikui
from slikui import EventType, Key, Mod, MouseButton
from slikui import sdl_input as si

# Use the system SDL3 (no binary download, no PyPI check at import).
for _k, _v in (("SDL_DOWNLOAD_BINARIES", "0"), ("SDL_CHECK_VERSION", "0"),
               ("SDL_FIND_BINARIES", "1"), ("SDL_DISABLE_METADATA", "1"),
               ("SDL_LOG_LEVEL", "3")):
    os.environ.setdefault(_k, _v)
sdl3 = pytest.importorskip("sdl3")


@pytest.fixture(scope="module")
def windows():
    sdl3.SDL_SetHint(b"SDL_VIDEO_DRIVER", b"dummy")
    if not sdl3.SDL_Init(sdl3.SDL_INIT_VIDEO):
        pytest.skip(f"SDL video init failed: {sdl3.SDL_GetError()}")
    a = sdl3.SDL_CreateWindow(b"a", 300, 200, sdl3.SDL_WINDOW_HIDDEN)
    b = sdl3.SDL_CreateWindow(b"b", 100, 100, sdl3.SDL_WINDOW_HIDDEN)
    assert a and b
    yield a, b
    sdl3.SDL_DestroyWindow(b)
    sdl3.SDL_DestroyWindow(a)
    sdl3.SDL_Quit()


def drain():
    sdl3.SDL_PumpEvents()
    sdl3.SDL_FlushEvents(sdl3.SDL_EVENT_FIRST, sdl3.SDL_EVENT_LAST)


def push(*events):
    for e in events:
        sdl3.SDL_PushEvent(ctypes.byref(e))


def pump(inp):
    ev = sdl3.SDL_Event()
    while sdl3.SDL_PollEvent(ctypes.byref(ev)):
        inp.process(ev)


def button(win, b, down, x, y):
    e = sdl3.SDL_Event()
    e.type = sdl3.SDL_EVENT_MOUSE_BUTTON_DOWN if down else sdl3.SDL_EVENT_MOUSE_BUTTON_UP
    e.button.windowID, e.button.button, e.button.down = win, b, down
    e.button.clicks, e.button.x, e.button.y = 1, x, y
    return e


def motion(win, x, y, dx=0.0, dy=0.0):
    e = sdl3.SDL_Event()
    e.type = sdl3.SDL_EVENT_MOUSE_MOTION
    e.motion.windowID, e.motion.x, e.motion.y = win, x, y
    e.motion.xrel, e.motion.yrel = dx, dy
    return e


def key(win, k, down, mod=0, repeat=False):
    e = sdl3.SDL_Event()
    e.type = sdl3.SDL_EVENT_KEY_DOWN if down else sdl3.SDL_EVENT_KEY_UP
    e.key.windowID, e.key.key, e.key.mod = win, k, mod
    e.key.down, e.key.repeat = down, repeat
    return e


class Sink:
    def __init__(self):
        self.events = []

    def handle(self, e):
        self.events.append(e)


# -- mapping -------------------------------------------------------------------

def test_buttons_are_one_based_in_sdl():
    assert (sdl3.SDL_BUTTON_LEFT, sdl3.SDL_BUTTON_MIDDLE, sdl3.SDL_BUTTON_RIGHT) == (1, 2, 3)
    assert si.map_button(sdl3.SDL_BUTTON_LEFT) == int(MouseButton.MouseLeft)
    assert si.map_button(sdl3.SDL_BUTTON_MIDDLE) == int(MouseButton.MouseMiddle)
    assert si.map_button(sdl3.SDL_BUTTON_RIGHT) == int(MouseButton.MouseRight)
    assert si.map_button(sdl3.SDL_BUTTON_X1) == 3


def test_keys_and_mods():
    assert si.map_key(sdl3.SDLK_A) == Key.A and si.map_key(sdl3.SDLK_Z) == Key.Z
    assert si.map_key(sdl3.SDLK_7) == Key.Num7
    assert si.map_key(sdl3.SDLK_KP_ENTER) == Key.Enter
    assert si.map_key(sdl3.SDLK_F13) == Key.Unknown
    assert si.map_mods(sdl3.SDL_KMOD_LCTRL | sdl3.SDL_KMOD_RGUI) == \
        int(Mod.ModCtrl) | int(Mod.ModSuper)
    assert si.map_mods(sdl3.SDL_KMOD_CAPS | sdl3.SDL_KMOD_NUM) == 0


def test_translate_wheel_and_text():
    w = sdl3.SDL_Event()
    w.type = sdl3.SDL_EVENT_MOUSE_WHEEL
    w.wheel.x, w.wheel.y, w.wheel.mouse_x, w.wheel.mouse_y = 1.5, -2.0, 10.0, 20.0
    w.wheel.direction = sdl3.SDL_MOUSEWHEEL_FLIPPED
    (e,) = si.translate(w)
    assert (e.scroll.x, e.scroll.y, e.pos.y) == (-1.5, 2.0, 20.0)

    t = sdl3.SDL_Event()
    t.type = sdl3.SDL_EVENT_TEXT_INPUT
    t.text.text = "aé☺".encode()
    out = si.translate(t)
    assert [e.codepoint for e in out] == [ord("a"), 0xE9, 0x263A]

    q = sdl3.SDL_Event()
    q.type = sdl3.SDL_EVENT_QUIT
    assert si.translate(q) == []


# -- SdlInput through SDL's queue -----------------------------------------------------

def test_sdl_input_filters_and_maps(windows):
    a, b = windows
    ida, idb = sdl3.SDL_GetWindowID(a), sdl3.SDL_GetWindowID(b)
    drain()
    with slikui.SdlInput(a) as inp:
        assert sdl3.SDL_TextInputActive(a)
        push(motion(ida, 10, 20, 3, 4), button(ida, sdl3.SDL_BUTTON_RIGHT, True, 10, 20),
             button(ida, sdl3.SDL_BUTTON_MIDDLE, False, 10, 20),
             key(ida, sdl3.SDLK_Z, True, sdl3.SDL_KMOD_LCTRL, True),
             motion(idb, 1, 1), key(idb, sdl3.SDLK_A, True))   # other window
        pump(inp)
        ev = inp.take()
        assert [e.type for e in ev] == [EventType.MouseMove, EventType.MousePress,
                                        EventType.MouseRelease, EventType.KeyPress]
        assert (ev[0].delta.x, ev[0].delta.y) == (3.0, 4.0)
        assert ev[1].button == int(MouseButton.MouseRight)
        assert ev[2].button == int(MouseButton.MouseMiddle)
        assert ev[3].key == Key.Z and ev[3].repeat and ev[3].mods == int(Mod.ModCtrl)
    assert not sdl3.SDL_TextInputActive(a)


def test_drives_a_real_context(windows):
    a, _ = windows
    ida = sdl3.SDL_GetWindowID(a)
    drain()
    clicks = []
    app = slikui.App(300, 200)
    try:
        root = app.set_root(slikui.Column, padding=10)
        go = root.add(slikui.Button, "Go", on_click=lambda: clicks.append(1))
        menu = slikui.Menu()
        menu.add_item("Copy", lambda: None)
        go.set_context_menu(menu)
        app.tick(0.0, 1 / 60)
        b = go.bounds
        cx, cy = b.x + b.w / 2, b.y + b.h / 2
        with slikui.SdlInput(a, app) as inp:
            push(motion(ida, cx, cy), button(ida, sdl3.SDL_BUTTON_RIGHT, True, cx, cy),
                 button(ida, sdl3.SDL_BUTTON_RIGHT, False, cx, cy))
            pump(inp)
            assert app.ctx.popup_count == 1 and clicks == []   # right: context menu
            app.ctx.close_popups()
            app.tick(0.1, 1 / 60)
            push(button(ida, sdl3.SDL_BUTTON_LEFT, True, cx, cy),
                 button(ida, sdl3.SDL_BUTTON_LEFT, False, cx, cy))
            pump(inp)
            assert clicks == [1]                                  # left: click
    finally:
        app.close()


# -- host routing (backend-independent) ------------------------------------------------

class LayoutOnly(slikui.Widget):
    def mouse_transparent(self):
        return True


def test_wants_mouse_and_keyboard():
    app = slikui.App(300, 200)
    try:
        root = app.set_root(slikui.Row, padding=10)
        root.add(slikui.Spacer)
        panel = root.add(slikui.Panel)
        field = panel.add(slikui.TextField)
        app.tick(0.0, 1 / 60)
        p = panel.bounds
        assert not app.ctx.wants_mouse(5, 5)                  # spacer: the scene's
        assert app.ctx.wants_mouse(p.x + 2, p.y + 2)          # panel: the UI's
        assert not app.ctx.wants_keyboard()
        app.ctx.set_focus(field)
        assert app.ctx.wants_keyboard()
        assert slikui.Row.mouse_transparent(root)
        assert not slikui.Panel.mouse_transparent(panel)
    finally:
        app.close()


def test_python_widget_can_be_mouse_transparent():
    app = slikui.App(200, 100)
    try:
        app.set_root(slikui.Column).add(LayoutOnly)
        app.tick(0.0, 1 / 60)
        assert not app.ctx.wants_mouse(50, 50)
    finally:
        app.close()


def test_sdl_demo_smoke():
    """python/examples/sdl_demo.py: 5 hidden frames through a real SDL GL
    window, GlApp, SdlInput routing. Skips where SDL can't open GL."""
    import pathlib
    import subprocess
    import sys

    pytest.importorskip("OpenGL")
    demo = pathlib.Path(__file__).resolve().parents[1] / "examples" / "sdl_demo.py"
    r = subprocess.run([sys.executable, str(demo), "--hidden", "--frames", "5"],
                       capture_output=True, text=True, timeout=120, env=dict(os.environ))
    if r.returncode != 0 and ("SDL_Init failed" in r.stdout + r.stderr
                              or "failed:" in r.stdout + r.stderr):
        pytest.skip(f"no SDL GL window here: {(r.stdout + r.stderr).strip()[-200:]}")
    assert r.returncode == 0, r.stdout + r.stderr
