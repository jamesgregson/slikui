"""03 — Event anatomy: what a custom widget receives, in what order, and how
consumption, capture and focus work.

The EventProbe is a custom widget whose whole job is narrating its input:
every event routed to it becomes one line in the adjacent LogView. The demo
exercises the whole vocabulary — enter/leave, press/release/click,
double-click, a captured drag that leaves the widget, scroll, right-click
context menu, focus gained/lost, focus-routed keys (press *and* release)
and text, Tab traversal and the app losing focus.

What a widget receives (slikui 0.3):

* MouseMove, MousePress, MouseRelease, Scroll — the widget under the pointer
  (or the mouse-capture widget), bubbling to ancestors until one returns
  True. Every button arrives; check ``event.button``.
* Click — press + release on the same widget (any button), after the
  release; ``event.clicks`` counts consecutive clicks. DoubleClick follows
  the Click whose ``clicks == 2``.
* MouseEnter / MouseLeave — this widget only, no bubbling. Moving onto a
  child is not a leave.
* KeyPress / KeyRelease — the focused widget (else the hovered one).
  Tab, Escape and shortcut chords are consumed by the Context first;
  KeyRelease only follows a KeyPress the tree received.
* TextInput — the focused widget only (``focusable()`` must return True).
* FocusGained / FocusLost — this widget only. FocusLost also arrives when
  the app loses focus while the widget is focused or capturing the mouse:
  cancel drags there.
* Layout runs in ``tick()``: bounds are zero until the first tick.

Run:

    python -m slikui.examples.events            # writes ./events.png
    python -m slikui.examples.events --smoke    # scripted self-check
"""

import slikui
from slikui.examples import _support

NAME = "events"
SIZE = (560, 356)


# doc: eventprobe
import slikui

INFO = slikui.LogView.Level.Info
WARN = slikui.LogView.Level.Warning
OK = slikui.LogView.Level.Success

T = slikui.EventType
LEFT = slikui.MouseButton.MouseLeft


def mods_str(mask):
    """0x3 → 'ctrl+shift' — the inverse of slikui.mods('ctrl+shift')."""
    names = []
    for bit, name in ((slikui.Mod.ModShift, "shift"), (slikui.Mod.ModCtrl, "ctrl"),
                      (slikui.Mod.ModAlt, "alt"), (slikui.Mod.ModSuper, "super")):
        if mask & int(bit):
            names.append(name)
    return "+".join(names) if names else "none"


def describe(e):
    """One human line per event — the shape of the whole vocabulary."""
    t = e.type
    if t == T.MouseMove:
        return (f"move     pos ({e.pos.x:.0f}, {e.pos.y:.0f})"
                f"   delta ({e.delta.x:+.1f}, {e.delta.y:+.1f})")
    if t in (T.MousePress, T.MouseRelease, T.Click, T.DoubleClick):
        return (f"{t.name.lower():<8} button {e.button}   clicks {e.clicks}"
                f"   mods {mods_str(e.mods)}")
    if t == T.Scroll:
        return f"scroll   ({e.scroll.x:+.1f}, {e.scroll.y:+.1f})"
    if t in (T.KeyPress, T.KeyRelease):
        rep = "  (repeat)" if e.repeat else ""
        return f"{'keydown' if t == T.KeyPress else 'keyup':<8} {e.key.name}   mods {mods_str(e.mods)}{rep}"
    if t == T.TextInput:
        return f"text     {chr(e.codepoint)!r} (focus-routed)"
    if t in (T.MouseEnter, T.MouseLeave):
        return f"{'enter' if t == T.MouseEnter else 'leave':<8} pos ({e.pos.x:.0f}, {e.pos.y:.0f})"
    return t.name  # FocusGained / FocusLost


class EventProbe(slikui.Widget):
    """Narrates every event it receives into a LogView via ``sink``.

    Also the reference drag idiom: capture the mouse on press, so moves and
    the release keep arriving after the pointer leaves the widget; release
    the capture on MouseRelease; cancel on FocusLost (the app lost focus
    mid-drag — the release may never come).
    """

    def __init__(self):
        super().__init__()
        self.sink = None            # callable(level, text) — usually a LogView
        self.seen = []              # (EventType, line) — full record, for tests
        self.last_pos = None        # last pointer position seen, for painting
        self.hovered = False
        self.dragging = False
        self.log_moves = True

    def measure(self, constraints):
        return constraints.clamp(slikui.Size(220, 120))

    def stretch_cross(self):
        return False

    def focusable(self):
        return True

    def on_event(self, e):
        if e.type != T.MouseMove or self.log_moves:
            self._say(e.type, INFO, describe(e))
        if e.type in (T.MouseMove, T.MousePress, T.MouseRelease, T.Scroll):
            self.last_pos = (e.pos.x, e.pos.y)

        if e.type == T.MouseEnter:
            self.hovered = True
        elif e.type == T.MouseLeave:
            self.hovered = False
        elif e.type == T.MousePress and e.button == LEFT:
            self.dragging = True
            self.context.capture_mouse(self)  # moves + release keep coming
        elif e.type == T.MouseRelease and e.button == LEFT and self.dragging:
            self.dragging = False
            self.context.release_mouse(self)
        elif e.type == T.FocusLost and self.dragging:
            self.dragging = False             # app lost focus mid-drag
            self.context.release_mouse(self)
            self._say("note", WARN, "drag cancelled by FocusLost")
        elif e.type == T.DoubleClick:
            self._say("note", OK, "double-click (core DoubleClick event)")

        # Consume pointer input; keys/text only log (bubbling is harmless).
        return e.type in (T.MouseMove, T.MousePress, T.MouseRelease, T.Click,
                          T.DoubleClick, T.Scroll)

    def _say(self, kind, level, text):
        self.seen.append((kind, text))
        if self.sink:
            self.sink(level, text)

    def paint(self, env):
        b, th = self.bounds, env.theme
        env.list.rect(b, th.surface_alt, 6)
        edge = th.accent if (self.focused or self.hovered) else th.border
        env.list.hollow_rect(b, edge, 2 if self.focused else 1, 6)
        if self.dragging:
            env.list.hollow_rect(b.inflate(2, 2), th.accent, 2, 8)
        if self.last_pos is not None and self.hovered:
            env.list.circle(slikui.Vec2(*self.last_pos),
                            6.0 if self.dragging else 3.0, th.accent)


# doc: end


def build(app):
    muted = slikui.Color.from_hex(0x9AA0B5)
    root = app.set_root(slikui.Column, padding=16, gap=10)

    root.add(slikui.Label, "Event anatomy", font_size=26)
    root.add(slikui.Label, "every event the probe receives logs a line") \
        .set_color(muted)

    row = root.add(slikui.Row, gap=12)
    left = row.add(slikui.Column, gap=6)
    panel = left.add(slikui.Panel, padding=10, gap=8)

    # Right side first (tree order) so the probe can narrate into it; added
    # to the row after the left column, so it sits on the right.
    logview = row.add(slikui.LogView)
    logview.log(INFO, "events land here")

    probe = panel.add(EventProbe)
    probe.sink = lambda lvl, msg: logview.log(lvl, msg)
    panel.add(slikui.Label, "click · drag out · scroll · right-click") \
        .set_color(muted)

    field = left.add(slikui.TextField, placeholder="click, then type")
    left.add(slikui.Label, "TextField — text is focus-routed").set_color(muted)

    # Right-click target: a borrowed Menu — State keeps the object alive.
    menu = slikui.Menu("probe")
    menu.add_item("Clear the log", lambda: logview.clear(), hint="Esc")
    menu.add_separator()
    menu.add_checkable("Log moves too", lambda: probe.log_moves,
                       lambda: setattr(probe, "log_moves", not probe.log_moves))
    probe.set_context_menu(menu)

    return _support.State(root=root, probe=probe, field=field,
                          logview=logview, menu=menu)


def smoke(app, state, session, log):
    c = _support.Checks(log)
    probe, field = state.probe, state.field
    session.advance(2)
    b = probe.bounds
    inside = (b.x + 12, b.y + 12)
    center = (b.x + b.w / 2, b.y + b.h / 2)
    outside = (b.x + b.w + 60, b.y + b.h + 60)

    def kinds_since(n):
        return [k for k, _ in probe.seen[n:]]

    # Layout runs in tick(): a fresh tree has zero bounds until then.
    fresh = slikui.App(120, 60)
    try:
        w = fresh.set_root(slikui.Column).add(EventProbe)
        c.ok(w.bounds.w == 0, "bounds are zero before the first tick")
        fresh.tick()
        c.ok(w.bounds.w > 0, "tick() runs layout")
    finally:
        fresh.close()

    # hover: Enter arrives before the first move
    n = len(probe.seen)
    _support.move(app, inside)
    c.ok(kinds_since(n) == [T.MouseEnter, T.MouseMove], "enter, then move")

    # click = press, release, click (+ focus, since the probe is focusable)
    session.advance(30)
    n = len(probe.seen)
    _support.click(app, center)
    k = kinds_since(n)
    c.ok([x for x in k if x != T.FocusGained] ==
         [T.MousePress, T.MouseRelease, T.Click], "press, release, click")
    c.ok(T.FocusGained in k and probe.focused, "a click focuses the probe")

    # captured drag: moves and the release arrive outside the bounds
    session.advance(30)
    n = len(probe.seen)
    _support.press(app, inside)
    _support.move(app, outside)
    c.ok(kinds_since(n)[-1] == T.MouseMove, "a captured drag gets moves outside")
    _support.release(app, outside)
    k = kinds_since(n)
    c.ok(T.MouseRelease in k and not probe.dragging,
         "the release outside arrives - the drag ends cleanly")
    c.ok(T.Click not in k, "no Click: the release landed elsewhere")
    _support.move(app, outside)
    c.ok(kinds_since(n)[-1] == T.MouseLeave, "leave once the pointer is really gone")

    # scroll over the probe
    session.advance(30)
    _support.wheel(app, center, -1.0)
    c.ok(probe.seen[-1][0] == T.Scroll, "scroll arrives with signed ticks")

    # middle click is delivered like any button
    session.advance(30)
    n = len(probe.seen)
    _support.click(app, center, button=slikui.MouseButton.MouseMiddle)
    c.ok(kinds_since(n)[-3:] == [T.MousePress, T.MouseRelease, T.Click],
         "middle press/release/click delivered")

    # right-click opens the context menu: press and release are swallowed
    session.advance(30)
    n = len(probe.seen)
    _support.click(app, center, button=slikui.MouseButton.MouseRight)
    session.advance(2)
    c.ok(app.ctx.popup_count == 1, "right-click opened the context menu")
    c.ok(T.MousePress not in kinds_since(n), "the menu's press is not delivered")
    app.handle(slikui.key_press(slikui.Key.Escape))
    session.advance(2)
    c.ok(app.ctx.popup_count == 0, "escape closed the context menu")

    # keys are focus-routed — press and release
    probe.request_focus()
    n = len(probe.seen)
    _support.tap(app, slikui.Key.Z)
    c.ok(kinds_since(n) == [T.KeyPress, T.KeyRelease], "keydown, keyup")

    # text is focus-routed: focusing the field takes focus from the probe
    session.advance(30)
    n = len(probe.seen)
    tb = field.bounds
    _support.click(app, (tb.x + 30, tb.y + tb.h / 2))
    _support.type_text(app, "hi")
    c.ok(field.text == "hi", "text routed to the focused TextField")
    c.ok(T.FocusLost in kinds_since(n), "the probe got FocusLost")
    c.ok(T.TextInput not in kinds_since(n), "and no text")

    # double-click: the core counts clicks on the tick clock
    session.advance(30)
    n = len(probe.seen)
    _support.double_click(app, session, center)
    k = kinds_since(n)
    c.ok(k.count(T.DoubleClick) == 1, "one DoubleClick")
    c.ok(k.index(T.DoubleClick) > max(i for i, x in enumerate(k) if x == T.Click),
         "DoubleClick follows the second Click")

    # the app losing focus mid-drag cancels it
    session.advance(30)
    _support.press(app, inside)
    app.handle(slikui.focus_lost())
    c.ok(not probe.dragging, "app focus lost cancels the drag")
    return {"events": len(probe.seen),
            "kinds": sorted({k.name for k, _ in probe.seen if k != "note"})}


def interactive():
    from slikui.examples import _window
    return _window.run(NAME, SIZE, build)


def main(argv=None):
    return _support.main_entry(name=NAME, size=SIZE, build=build, smoke=smoke,
                               interactive=interactive,
                               argv=argv)


if __name__ == "__main__":
    raise SystemExit(main())
