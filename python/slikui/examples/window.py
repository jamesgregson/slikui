"""08 — A live app in a window: the GLFW host plus a small tour.

``python -m slikui.examples.window --window`` opens a real window (needs
``pip install "slikui-python[demo]"`` for glfw + PyOpenGL); the host lives
in ``slikui/examples/_window.py`` — GLFW callbacks become slikui events,
the numpy frame is blitted each frame, HiDPI, cursors, clipboard and
window focus are wired. Any other toolkit works the same way: translate its
input into ``slikui.mouse_*`` / ``key_*`` / ``text_input`` events for
``app.handle``, call ``app.tick`` then ``app.render`` each frame, and show
the returned (H, W) uint32 array however you like.

The tour: type into the field (echoed below it), drag the slider, click
the button for a toast; the status bar follows the pointer — a custom root
widget sees every MouseMove that bubbles up unconsumed. Ctrl+Q quits.

Run:

    python -m slikui.examples.window --window   # live
    python -m slikui.examples.window --smoke    # the same tour, scripted
"""

import slikui
from slikui.examples import _support

NAME = "window"
SIZE = (420, 300)


# doc: pointerroot
import slikui


class PointerRoot(slikui.Widget):
    """Root container that reports the pointer position: events bubble from
    the widget under the pointer up to the root unless a widget consumes
    them, so the root sees moves over anything that doesn't."""

    def __init__(self):
        super().__init__()
        self.on_pointer = None

    def measure(self, constraints):
        return self.children[0].measure(constraints)

    def arrange(self, area):
        self.bounds = area
        self.children[0].arrange(area)

    def on_event(self, e):
        if e.type == slikui.EventType.MouseMove and self.on_pointer:
            self.on_pointer(e.pos.x, e.pos.y)
        return False


# doc: end


def build(app):
    muted = slikui.Color.from_hex(0x9AA0B5)
    root = app.set_root(PointerRoot)
    col = root.add(slikui.Column, padding=16, gap=10)
    col.add(slikui.Label, "A live slikui app", font_size=24)

    field = col.add(slikui.TextField, placeholder="type something")
    echo = col.add(slikui.Label, "you typed: —")
    field.on_change(lambda text: echo.set_text(f"you typed: {text or '—'}"))

    slider = col.add(slikui.Slider, 0, 100, value=25)
    slider.set_label("volume").set_format("%.0f")

    toasts = []

    def toast():
        toasts.append(app.ctx.push_toast("Hello from a toast", 2.0))

    button = col.add(slikui.Button, "Show a toast",
                     variant=slikui.ButtonVariant.Primary, on_click=toast)
    col.add(slikui.Label, "Ctrl+Q quits").set_color(muted)
    col.add(slikui.Spacer)
    status = col.add(slikui.StatusBar)
    status.set_text("move the pointer")
    root.on_pointer = lambda x, y: status.set_text(f"pointer {x:.0f}, {y:.0f}")

    # The window host polls app.quit (see _window.run).
    app.ctx.add_shortcut(slikui.Key.Q, slikui.mods("ctrl"),
                         lambda: setattr(app, "quit", True))
    return _support.State(field=field, echo=echo, slider=slider, button=button,
                          status=status, toasts=toasts)


def smoke(app, state, session, log):
    c = _support.Checks(log)
    fb = state.field.bounds
    _support.click(app, (fb.x + 20, fb.y + fb.h / 2))
    _support.type_text(app, "hi")
    c.ok(state.echo.text == "you typed: hi", "typing echoes through on_change")

    sb = state.slider.bounds
    _support.drag(app, (sb.x + 10, sb.bottom() - 6), (sb.right() - 10, sb.bottom() - 6))
    c.ok(state.slider.value > 90, f"slider dragged to {state.slider.value:.0f}")

    _support.move(app, (300, 20))
    c.ok(state.status.text == "pointer 300, 20", "root sees bubbled moves")

    bb = state.button.bounds
    _support.click(app, (bb.x + 10, bb.y + bb.h / 2))
    session.advance(2)
    c.ok(len(state.toasts) == 1 and app.ctx.popup_count == 1, "button -> toast")

    _support.tap(app, slikui.Key.Q, "ctrl")
    c.ok(getattr(app, "quit", False), "Ctrl+Q requests quit")
    return {"echo": state.echo.text, "slider": round(state.slider.value)}


def interactive():
    from slikui.examples import _window
    return _window.run(NAME, SIZE, build)


def main(argv=None):
    return _support.main_entry(name=NAME, size=SIZE, build=build, smoke=smoke,
                               interactive=interactive, argv=argv)


if __name__ == "__main__":
    raise SystemExit(main())
