"""03 — Event anatomy: what slikui events look like, who gets them, and how
consumption works.

The EventProbe is a custom widget whose whole job is narrating its input:
every event routed to it becomes one line in the adjacent LogView. The demo
exercises the whole vocabulary — hover, click, drag, scroll, right-click
context menu, focus-routed keys and text, Tab traversal, and a manual
double-click detector (slikui events carry no timestamps; the probe times
press pairs against the deterministic tick clock).

Run:

    python -m slikui.examples.events            # writes ./events.png
    python -m slikui.examples.events --smoke    # scripted self-check
"""

import slikui
from . import _support

NAME = "events"
SIZE = (560, 356)

INFO = slikui.LogView.Level.Info
WARN = slikui.LogView.Level.Warning
OK = slikui.LogView.Level.Success

LEFT = int(slikui.MouseButton.MouseLeft)
DOUBLE_CLICK_S = 0.35


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
    if e.type == slikui.EventType.MouseMove:
        return (f"move    pos ({e.pos.x:.0f}, {e.pos.y:.0f})"
                f"   delta ({e.delta.x:+.1f}, {e.delta.y:+.1f})")
    if e.type == slikui.EventType.MousePress:
        return f"press   button {e.button}   mods {mods_str(e.mods)}"
    if e.type == slikui.EventType.MouseRelease:
        return f"release button {e.button}"
    if e.type == slikui.EventType.Scroll:
        return f"scroll  ({e.scroll.x:+.1f}, {e.scroll.y:+.1f})"
    if e.type == slikui.EventType.KeyPress:
        return f"keydown {e.key}   mods {mods_str(e.mods)}"
    if e.type == slikui.EventType.KeyRelease:
        return f"keyup   {e.key}"
    if e.type == slikui.EventType.TextInput:
        return f"text    {chr(e.codepoint)!r} (focus-routed)"
    if e.type == slikui.EventType.FocusLost:
        return "focus lost"
    return str(e.type)


def type_name(e):
    return str(e.type).rsplit(".", 1)[-1]


# doc: eventprobe
class EventProbe(slikui.Widget):
    """Narrates every event it receives into a LogView via ``sink``.

    Demonstrates the three rules of slikui input:

    * return ``True`` to consume — ancestors/siblings stop seeing it;
    * key and text events are focus-routed — the probe only gets them while
      it holds focus (it is focusable; the TextField beside it shows the
      contrast). Note the current core drops KeyRelease entirely
      (`Context::handle` never dispatches it) — apps that need key-up must
      plan around it;
    * there is no mouse capture in the bindings yet — moves and releases
      outside the widget's bounds are never delivered, so a drag "ends"
      when the pointer leaves and a lost release must be handled
      defensively (a fresh press while still ``dragging`` logs the warning).
    """

    def __init__(self):
        super().__init__()
        self.sink = None            # callable(level, text) — usually a LogView
        self.seen = []              # (kind, line) — full record, for tests
        self.now = 0.0              # tick clock, for press-pair timing
        self.last_pos = None        # last mouse position seen, for painting
        self.press_count = 0
        self.double_clicks = 0
        self.dragging = False
        self.log_moves = True
        self.move_positions = []   # every MouseMove actually delivered
        self._last_press = -1.0

    def measure(self, constraints):
        return constraints.clamp(slikui.Size(220, 120))

    def stretch_cross(self):
        return False

    def focusable(self):
        return True

    def tick(self, t):
        self.now = t.now

    def on_event(self, e):
        self._note(e)
        return self._consume(e)

    def _note(self, e):
        kind = type_name(e)
        if kind == "MouseMove":
            self.move_positions.append((e.pos.x, e.pos.y))
            if self.log_moves:
                self._say(kind, INFO, describe(e))
        else:
            self._say(kind, INFO, describe(e))
        if e.type in (slikui.EventType.MouseMove, slikui.EventType.MousePress,
                      slikui.EventType.MouseRelease, slikui.EventType.Scroll):
            self.last_pos = (e.pos.x, e.pos.y)

    def _consume(self, e):
        if e.type == slikui.EventType.MousePress and e.button == LEFT:
            inside = self.bounds.contains(e.pos)
            if not inside:
                return False
            if self.dragging:  # a lost release: the pointer left, pressed
                self._say("note", WARN, "press while still dragging — the "
                            "previous release never arrived (no mouse capture "
                            "yet)")
            self.dragging = True
            self.press_count += 1
            if 0.0 <= self.now - self._last_press < DOUBLE_CLICK_S:
                self.double_clicks += 1
                self._say("note", OK, "double-click (two presses < "
                                      f"{DOUBLE_CLICK_S:.2f} s apart)")
            self._last_press = self.now
            return True
        if e.type == slikui.EventType.MouseMove:
            inside = self.bounds.contains(e.pos)
            return bool(self.dragging) or inside
        if e.type == slikui.EventType.MouseRelease:
            was = self.dragging
            self.dragging = False
            return was
        if e.type == slikui.EventType.Scroll and self.bounds.contains(e.pos):
            return True
        return False  # keys/text: routed by focus, log only

    def _say(self, kind, level, text):
        self.seen.append((kind, text))
        if self.sink:
            self.sink(level, text)

    def paint(self, env):
        b, th = self.bounds, env.theme
        env.list.rect(b, th.surface_alt, 6)
        env.list.hollow_rect(b, th.border, 1, 6)
        if self.dragging:
            env.list.hollow_rect(b.inflate(2, 2), th.accent, 2, 8)
        if self.last_pos is not None:
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

    # right side first (tree order), created now so the probe can narrate
    # into it; added to the row after the left column → it sits on the right
    logview = row.add(slikui.LogView)
    logview.log(INFO, "events land here")

    probe = panel.add(EventProbe)
    probe.sink = lambda lvl, msg: logview.log(lvl, msg)
    panel.add(slikui.Label, "click · drag · scroll · right-click") \
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
    center = (b.x + b.w / 2, b.y + b.h / 2)
    kinds = lambda: [k for k, _ in probe.seen]

    # hover
    _support.move(app, (b.x + 12, b.y + 12))
    c.ok(kinds()[-1] == "MouseMove", "hover arrives as MouseMove")

    # click = press + release pair, consumed
    session.advance(24)
    _support.click(app, center)
    c.ok(kinds()[-2:] == ["MousePress", "MouseRelease"],
         "a click is MousePress then MouseRelease")

    # drag: moves arrive only while inside — no capture in the bindings
    session.advance(24)
    _support.press(app, (b.x + 12, b.y + 12))
    _support.move(app, (b.x + b.w - 20, b.y + b.h - 20))   # still inside
    _support.move(app, (b.x + b.w + 60, b.y + b.h + 60))   # outside
    _support.release(app, (b.x + b.w + 60, b.y + b.h + 60))
    c.ok(all(b.x <= x <= b.x + b.w and b.y <= y <= b.y + b.h
             for x, y in probe.move_positions),
         "no MouseMove outside the probe (no capture)")
    c.ok(probe.dragging is True,
         "drag state survives: the outside release never arrived")

    # scroll over the probe
    session.advance(24)
    _support.wheel(app, center, -1.0)
    c.ok(kinds()[-1] == "Scroll", "scroll arrives with signed ticks")

    # right-click opens the context menu; Escape closes it
    session.advance(24)
    _support.click(app, center, button=slikui.MouseButton.MouseRight)
    session.advance(2)
    c.ok(app.ctx.popup_count == 1, "right-click opened the context menu")
    app.handle(slikui.key_press(slikui.Key.Escape))
    session.advance(2)
    c.ok(app.ctx.popup_count == 0, "escape closed the context menu")

    # text is focus-routed: click the field, type — the probe sees nothing
    session.advance(24)
    tb = field.bounds
    _support.click(app, (tb.x + 30, tb.y + tb.h / 2))
    _support.type_text(app, "hi")
    c.ok(field.text == "hi", "text routed to the focused TextField")
    c.ok("TextInput" not in kinds(), "focus-routed: the probe got no text")

    # click the probe to focus it, then keys route to the probe — and the
    # stale drag from the lost release warns (the no-capture lesson)
    session.advance(24)
    _support.click(app, (b.x + 12, b.y + 12))
    warns = [t for k, t in probe.seen if k == "note"]
    c.ok(any("release never arrived" in t for t in warns),
         "stale drag detected and logged defensively")
    _support.tap(app, slikui.Key.Z)
    c.ok(kinds()[-1] == "KeyPress", "keys are focus-routed to the focused probe")
    c.ok("KeyRelease" not in kinds(),
         "key releases are not dispatched (current core contract)")

    # double-click: two presses < 0.35 s apart on the scripted clock
    session.advance(24)
    _support.click(app, center)
    session.advance(10)  # ~0.17 s
    _support.click(app, center)
    c.ok(probe.double_clicks >= 1, "double-click detected via the tick clock")
    return {"events": len(probe.seen), "presses": probe.press_count,
            "double_clicks": probe.double_clicks}


def interactive():
    from . import _window
    return _window.run(NAME, SIZE, build)


def main(argv=None):
    return _support.main_entry(name=NAME, size=SIZE, build=build, smoke=smoke,
                               interactive=interactive,
                               argv=argv)


if __name__ == "__main__":
    raise SystemExit(main())
