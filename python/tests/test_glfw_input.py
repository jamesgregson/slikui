"""slikui.glfw_input: GLFW -> slikui event translation on a host-owned window.

GLFW cannot inject synthetic OS events, so the tests fetch the callback
GlfwInput installed (pyGLFW's setters return the previous one) and call it
exactly as GLFW would. Hidden window; skipped without glfw or a display.
"""

import sys

import pytest

import slikui
from slikui import EventType, Key, Mod, MouseButton
from slikui import glfw_input as gi

glfw = pytest.importorskip("glfw")


@pytest.fixture(scope="module")
def window():
    if not glfw.init():
        pytest.skip("glfw.init failed (no display)")
    glfw.window_hint(glfw.VISIBLE, False)
    win = glfw.create_window(64, 64, "slikui glfw_input test", None, None)
    if not win:
        glfw.terminate()
        pytest.skip("no window")
    yield win
    glfw.destroy_window(win)
    glfw.terminate()


def installed(win, name):
    setter = getattr(glfw, f"set_{name}_callback")
    cb = setter(win, None)
    setter(win, cb)
    return cb


class Sink:
    def __init__(self):
        self.events = []

    def handle(self, e):
        self.events.append(e)


# -- mapping (no window) ----------------------------------------------------

def test_buttons_map_glfw_order_to_slikui():
    # GLFW: LEFT=0 RIGHT=1 MIDDLE=2 — slikui: Left=0 Middle=1 Right=2.
    assert (glfw.MOUSE_BUTTON_LEFT, glfw.MOUSE_BUTTON_RIGHT, glfw.MOUSE_BUTTON_MIDDLE) == (0, 1, 2)
    assert gi.map_button(glfw.MOUSE_BUTTON_LEFT) == int(MouseButton.MouseLeft)
    assert gi.map_button(glfw.MOUSE_BUTTON_RIGHT) == int(MouseButton.MouseRight)
    assert gi.map_button(glfw.MOUSE_BUTTON_MIDDLE) == int(MouseButton.MouseMiddle)
    assert gi.map_button(glfw.MOUSE_BUTTON_4) == 3


def test_mods_and_keys():
    assert gi.map_mods(glfw.MOD_SHIFT | glfw.MOD_CONTROL) == int(Mod.ModShift) | int(Mod.ModCtrl)
    assert gi.map_mods(glfw.MOD_SUPER | glfw.MOD_CAPS_LOCK | glfw.MOD_NUM_LOCK) == int(Mod.ModSuper)
    assert gi.map_key(glfw.KEY_A) == Key.A
    assert gi.map_key(glfw.KEY_KP_ENTER) == Key.Enter
    assert gi.map_key(glfw.KEY_PAGE_DOWN) == Key.PageDown
    assert gi.map_key(glfw.KEY_7) == Key.Num7
    assert gi.map_key(glfw.KEY_F25) == Key.Unknown
    e = gi.key_event(glfw.KEY_A, 0, glfw.REPEAT, glfw.MOD_CONTROL)
    assert e.type == EventType.KeyPress and e.repeat and e.key == Key.A
    assert e.mods == int(Mod.ModCtrl)
    assert gi.key_event(glfw.KEY_A, 0, glfw.RELEASE, 0).type == EventType.KeyRelease
    assert gi.key_event(glfw.KEY_F25, 0, glfw.PRESS, 0) is None
    assert gi.focus_event(True) is None
    assert gi.focus_event(False).type == EventType.FocusLost
    r = gi.framebuffer_size_event(640, 480)
    assert r.type == EventType.FrameResize and (r.pos.x, r.pos.y) == (640, 480)


def test_import_slikui_does_not_import_glfw():
    import subprocess
    out = subprocess.run([sys.executable, "-c",
                          "import sys, slikui; print('glfw' in sys.modules)"],
                         capture_output=True, text=True, check=True)
    assert out.stdout.strip() == "False"


# -- GlfwInput on a host-owned window ------------------------------------------

def test_chains_host_callbacks_and_delivers(window):
    seen = []
    glfw.set_mouse_button_callback(window, lambda w, b, a, m: seen.append(("button", b)))
    glfw.set_key_callback(window, lambda w, k, s, a, m: seen.append(("key", k)))
    sink = Sink()
    inp = slikui.GlfwInput(window, sink)
    try:
        installed(window, "mouse_button")(window, glfw.MOUSE_BUTTON_RIGHT, glfw.PRESS,
                                          glfw.MOD_SHIFT)
        installed(window, "mouse_button")(window, glfw.MOUSE_BUTTON_MIDDLE, glfw.RELEASE, 0)
        installed(window, "key")(window, glfw.KEY_ESCAPE, 0, glfw.PRESS, 0)
        installed(window, "char")(window, ord("x"))
        installed(window, "cursor_pos")(window, 5.0, 7.0)
        installed(window, "scroll")(window, 1.0, -2.0)
        # Host callbacks ran first, with GLFW's raw arguments.
        assert seen == [("button", glfw.MOUSE_BUTTON_RIGHT),
                        ("button", glfw.MOUSE_BUTTON_MIDDLE),
                        ("key", glfw.KEY_ESCAPE)]
        ev = sink.events
        assert [e.type for e in ev] == [EventType.MousePress, EventType.MouseRelease,
                                        EventType.KeyPress, EventType.TextInput,
                                        EventType.MouseMove, EventType.Scroll]
        assert ev[0].button == int(MouseButton.MouseRight) and ev[0].mods == int(Mod.ModShift)
        assert ev[1].button == int(MouseButton.MouseMiddle)
        assert ev[2].key == Key.Escape
        assert (ev[4].pos.x, ev[4].pos.y) == (5.0, 7.0)
        assert (ev[5].scroll.x, ev[5].scroll.y) == (1.0, -2.0)
    finally:
        inp.close()
        inp.close()  # idempotent
    # close() put the host's callbacks back.
    installed(window, "mouse_button")(window, glfw.MOUSE_BUTTON_LEFT, glfw.PRESS, 0)
    assert seen[-1] == ("button", glfw.MOUSE_BUTTON_LEFT)
    assert len(sink.events) == 6
    glfw.set_mouse_button_callback(window, None)
    glfw.set_key_callback(window, None)


def test_queue_mode_and_cursor_delta(window):
    with slikui.GlfwInput(window) as inp:
        installed(window, "cursor_pos")(window, 10.0, 10.0)
        installed(window, "cursor_pos")(window, 13.0, 14.0)
        installed(window, "window_focus")(window, False)
        installed(window, "window_focus")(window, True)   # no slikui event
        ev = inp.take()
        assert inp.take() == []
        assert [e.type for e in ev] == [EventType.MouseMove, EventType.MouseMove,
                                        EventType.FocusLost]
        assert (ev[1].delta.x, ev[1].delta.y) == (3.0, 4.0)
        sink = Sink()
        installed(window, "framebuffer_size")(window, 200, 100)
        inp.dispatch(sink)
        assert sink.events[0].type == EventType.FrameResize


def test_drives_a_real_context(window, monkeypatch):
    clicks = []
    app = slikui.App(200, 100)
    try:
        app.set_root(slikui.Column, padding=10).add(
            slikui.Button, "Go", on_click=lambda: clicks.append(1))
        app.tick(0.0, 1 / 60)
        b = app.ctx.root.children[0].bounds
        cx, cy = b.x + b.w / 2, b.y + b.h / 2
        # mouse_button_event reads the cursor from GLFW; a hidden window
        # ignores set_cursor_pos (macOS), so pin what GLFW reports.
        monkeypatch.setattr(glfw, "get_cursor_pos", lambda w: (cx, cy))
        with slikui.GlfwInput(window, app):
            installed(window, "cursor_pos")(window, cx, cy)
            # Right click must NOT activate a button (left only)...
            for action in (glfw.PRESS, glfw.RELEASE):
                installed(window, "mouse_button")(window, glfw.MOUSE_BUTTON_RIGHT, action, 0)
            app.tick(0.1, 1 / 60)
            assert clicks == []
            # ...a left click does.
            for action in (glfw.PRESS, glfw.RELEASE):
                installed(window, "mouse_button")(window, glfw.MOUSE_BUTTON_LEFT, action, 0)
            app.tick(0.2, 1 / 60)
            assert clicks == [1]
    finally:
        app.close()


def test_attach_services_clipboard(window):
    app = slikui.App(100, 60)
    try:
        gi.attach_services(app.ctx, window)
        field = app.set_root(slikui.Column).add(slikui.TextField)
        glfw.set_clipboard_string(None, "from-glfw")
        app.tick(0.0, 1 / 60)
        app.ctx.set_focus(field)
        app.handle(slikui.key_press(Key.V, slikui.mods("ctrl")))
        app.handle(slikui.key_press(Key.V, slikui.mods("super")))
        assert "from-glfw" in field.text
    finally:
        app.close()
