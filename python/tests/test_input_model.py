"""The 0.3 input model, from Python: what a custom widget receives.

Mirrors tests/test_input.cpp for the binding surface — capture, Click /
DoubleClick / clicks, Enter / Leave, focus notifications, KeyRelease,
paint_over / clip_rect / attached overrides, and readable reprs.
"""

import slikui
from slikui import EventType as T


class Probe(slikui.Widget):
    def __init__(self, h=40, focusable=False, capture=False):
        super().__init__()
        self.h, self._focusable, self.capture = h, focusable, capture
        self.log = []

    def measure(self, constraints):
        return constraints.clamp(slikui.Size(0, self.h))

    def focusable(self):
        return self._focusable

    def on_event(self, e):
        self.log.append((e.type, e.button, e.clicks, e.key))
        if self.capture and e.type == T.MousePress:
            self.context.capture_mouse(self)
        if e.type == T.MouseRelease:
            self.context.release_mouse(self)
        return True

    def kinds(self):
        return [k for k, *_ in self.log]


def two_probes(**kw):
    app = slikui.App(200, 120)
    root = app.set_root(slikui.Column, padding=0, gap=10)
    a = root.add(Probe, 40, kw.get("focusable", False), kw.get("capture", False))
    b = root.add(Probe, 40, kw.get("focusable", False))
    app.tick()  # layout happens in tick(), not render()
    return app, a, b  # a: y 0..40, b: y 50..90


def click(app, xy, button=slikui.MouseButton.MouseLeft):
    for e in slikui.mouse_click(xy, button):
        app.handle(e)


def test_capture_keeps_the_drag_and_its_release():
    app, a, b = two_probes(capture=True)
    app.handle(slikui.mouse_press((20, 20)))
    app.handle(slikui.mouse_move((20, 70)))     # over b, but a captured
    app.handle(slikui.mouse_release((20, 70)))
    assert a.kinds().count(T.MouseMove) >= 1
    assert T.MouseRelease in a.kinds()
    assert T.MouseMove not in b.kinds()
    assert T.Click not in a.kinds()              # released elsewhere: no click
    app.close()


def test_click_double_click_and_count():
    app, a, _ = two_probes()
    click(app, (20, 20))
    app.tick(0.1, 0.1)
    click(app, (20, 20))
    clicks = [c for k, _, c, _ in a.log if k == T.Click]
    assert clicks == [1, 2]
    assert a.kinds().count(T.DoubleClick) == 1
    assert a.kinds()[-1] == T.DoubleClick       # after the second Click
    app.close()


def test_every_button_is_delivered():
    app, a, _ = two_probes()
    click(app, (20, 20), slikui.MouseButton.MouseRight)
    app.handle(slikui.mouse_press((20, 20), slikui.MouseButton.MouseMiddle))
    presses = [btn for k, btn, *_ in a.log if k == T.MousePress]
    assert presses == [int(slikui.MouseButton.MouseRight),
                       int(slikui.MouseButton.MouseMiddle)]
    app.close()


def test_enter_leave():
    app, a, b = two_probes()
    app.handle(slikui.mouse_move((20, 20)))
    app.handle(slikui.mouse_move((20, 70)))
    assert a.kinds()[:2] == [T.MouseEnter, T.MouseMove]
    assert a.kinds()[-1] == T.MouseLeave
    assert b.kinds()[0] == T.MouseEnter
    assert app.ctx.hovered is b
    app.close()


def test_focus_notifications_and_request_focus():
    app, a, b = two_probes(focusable=True)
    click(app, (20, 20))
    assert a.focused and T.FocusGained in a.kinds()
    b.request_focus()
    assert b.focused and not a.focused
    assert a.kinds()[-1] == T.FocusLost
    assert b.kinds()[-1] == T.FocusGained
    app.ctx.set_focus(None)
    assert app.ctx.focused is None and b.kinds()[-1] == T.FocusLost
    app.close()


def test_key_release_follows_its_press():
    app, a, _ = two_probes(focusable=True)
    click(app, (20, 20))
    app.handle(slikui.key_press(slikui.Key.Num1))
    app.handle(slikui.key_release(slikui.Key.Num1))
    app.handle(slikui.key_press(slikui.Key.Tab))     # consumed by the Context
    app.handle(slikui.key_release(slikui.Key.Tab))
    keys = [(k, key) for k, _, _, key in a.log if k in (T.KeyPress, T.KeyRelease)]
    assert keys == [(T.KeyPress, slikui.Key.Num1), (T.KeyRelease, slikui.Key.Num1)]
    app.close()


def test_app_focus_lost_reaches_the_dragging_widget():
    app, a, _ = two_probes(capture=True)
    app.handle(slikui.mouse_press((20, 20)))
    app.handle(slikui.focus_lost())
    assert a.kinds()[-1] == T.FocusLost
    app.close()


class Decorated(slikui.Widget):
    def __init__(self):
        super().__init__()
        self.order, self.attached_to = [], None

    def measure(self, constraints):
        return constraints.clamp(slikui.Size(0, 40))

    def paint(self, env):
        self.order.append("paint")

    def paint_over(self, env):
        self.order.append("over")

    def clip_rect(self):
        return self.bounds

    def attached(self, context):
        self.attached_to = context


def test_paint_over_clip_rect_attached_overrides():
    app = slikui.App(200, 120)
    root = app.set_root(slikui.Column, padding=0)
    d = root.add(Decorated)
    app.tick()
    app.render()
    assert d.order == ["paint", "over"]
    assert d.attached_to is not None
    assert d.clip_rect() == d.bounds
    app.close()


def test_reprs_and_arithmetic_enums():
    assert repr(slikui.Rect(1, 2, 3, 4)) == "Rect(1, 2, 3, 4)"
    assert repr(slikui.Vec2(0.5, 2)) == "Vec2(0.5, 2)"
    assert repr(slikui.Size(10, 20)) == "Size(10, 20)"
    assert int(T.Click) == 9 and T.MousePress == 1
    e = slikui.key_press(slikui.Key.A, "ctrl", repeat=True)
    assert repr(e) == "Event(KeyPress key=A repeat mods=2)"
    assert e.repeat and e.ctrl and e.shortcut_mod
    assert slikui.Event.MULTI_CLICK_TIME > 0
