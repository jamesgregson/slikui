"""04 — Widgets that do their own drawing.

Every widget here is a pure-Python subclass of ``slikui.Widget``: layout
participation via ``measure``, geometry via ``env.list`` primitives, events
via ``on_event``, animation via ``tick``. Text stays in ordinary ``Label``
widgets beside them — the Python trampoline exposes ``env.list`` and
``env.theme`` to custom widgets, not the font, and composing with Labels is
the intended division of labor.

Run:

    python -m slikui.examples.paint             # writes ./paint.png
    python -m slikui.examples.paint --smoke     # scripted self-check
    python -m slikui.examples.paint --docs-image custom_widget <path>
"""

import math

import numpy as np

import slikui
from slikui.examples import _support

NAME = "paint"
SIZE = (460, 380)

MUTED = 0x8A94A0
BLUE = 0x4C8DFF


# doc: meter
import slikui


class Meter(slikui.Widget):
    """A horizontal bar that fills as ``value`` (0..1) approaches 1."""

    def __init__(self, value=0.0):
        super().__init__()  # required
        self.value = value

    def measure(self, constraints):
        return constraints.clamp(slikui.Size(0, 10))

    def paint(self, env):
        env.list.rect(self.bounds, env.theme.surface_alt, self.bounds.h / 2)
        if self.value > 0:
            fill = slikui.Rect(self.bounds.x, self.bounds.y,
                               self.bounds.w * self.value, self.bounds.h)
            env.list.rect(fill, env.theme.accent, self.bounds.h / 2)

    def on_event(self, event):
        # Click to the left/right of the fill to move it.
        if event.type == slikui.EventType.MousePress and self.bounds.contains(event.pos):
            v = (event.pos.x - self.bounds.x) / self.bounds.w
            self.value = max(0.0, min(v, 1.0))
            return True
        return False


# doc: end

# doc: sparkline
import numpy as np

import slikui


class Sparkline(slikui.Widget):
    """A numpy series as a polyline, with a hover crosshair.

    ``hover_x`` is the pointer's local x (or ``None``); ``paint`` turns it
    into the crosshair line. MouseMove only arrives while the pointer is
    over the widget, so the crosshair is cleared on MouseLeave — the hover
    idiom for any widget.
    """

    def __init__(self, series, capacity=128, color=None):
        super().__init__()
        self.series = np.asarray(series, dtype=np.float32)
        self.capacity = capacity
        self.color = color          # None → theme accent
        self.hover_x = None

    def push(self, v):
        """Append a sample, dropping the oldest past `capacity`."""
        self.series = np.append(self.series, np.float32(v))[-self.capacity:]

    def measure(self, constraints):
        return constraints.clamp(slikui.Size(160, 36))

    def stretch_cross(self):
        return False

    def on_event(self, event):
        if event.type == slikui.EventType.MouseMove:
            self.hover_x = event.pos.x - self.bounds.x
            return True
        if event.type == slikui.EventType.MouseLeave:
            self.hover_x = None
        return False

    def paint(self, env):
        b, th = self.bounds, env.theme
        env.list.rect(b, th.surface_alt, 4)
        if self.series.size >= 2:
            lo, hi = float(self.series.min()), float(self.series.max())
            span = max(hi - lo, 1e-6)
            col = self.color or th.accent
            pts = [slikui.Vec2(b.x + i / (self.series.size - 1) * b.w,
                               b.y + 2 + (1.0 - (float(v) - lo) / span) * (b.h - 4))
                   for i, v in enumerate(self.series)]
            for a, p in zip(pts, pts[1:]):
                env.list.line(a, p, col, 1.5)
        if self.hover_x is not None:  # crosshair at the pointer
            x = b.x + self.hover_x
            env.list.line(slikui.Vec2(x, b.y), slikui.Vec2(x, b.y + b.h),
                          th.text, 1.0)


# doc: end

# doc: gauge
import math

import slikui


class RadialGauge(slikui.Widget):
    """A 270-degree arc gauge built from line segments (no arc primitive —
    draw the circle you need). The gap sits at the bottom; text lives in a
    Label beside the gauge."""

    START, SWEEP = 135.0, 270.0  # degrees, 0° at 3 o'clock, clockwise

    def __init__(self, value=0.5, segments=48):
        super().__init__()
        self.value = min(max(value, 0.0), 1.0)
        self.segments = segments

    def set_value(self, v):
        self.value = min(max(v, 0.0), 1.0)
        return self

    def measure(self, constraints):
        return constraints.clamp(slikui.Size(56, 56))

    def stretch_cross(self):
        return False

    def _pt(self, cx, cy, r, t):
        a = math.radians(self.START + t * self.SWEEP)
        return slikui.Vec2(cx + r * math.cos(a), cy + r * math.sin(a))

    def paint(self, env):
        b, th = self.bounds, env.theme
        cx, cy = b.x + b.w / 2, b.y + b.h / 2
        r = min(b.w, b.h) / 2 - 4
        n = self.segments
        for i in range(n):  # dim track
            env.list.line(self._pt(cx, cy, r, i / n), self._pt(cx, cy, r, (i + 1) / n),
                          th.surface_alt, 4.0)
        m = int(round(n * self.value))
        for i in range(m):  # value arc
            env.list.line(self._pt(cx, cy, r, i / n), self._pt(cx, cy, r, (i + 1) / n),
                          th.accent, 4.0)
        env.list.circle(slikui.Vec2(cx, cy), r * 0.55, th.surface)


# doc: end

# doc: clockface
import math

import slikui


class ClockFace(slikui.Widget):
    """An animated analog clock: rim, twelve ticks, three hands. Time comes
    from ``tick(Time)`` — never ``time.time()`` — so scripted runs are
    deterministic."""

    def __init__(self):
        super().__init__()
        self.now = 0.0

    def measure(self, constraints):
        return constraints.clamp(slikui.Size(56, 56))

    def stretch_cross(self):
        return False

    def tick(self, t):
        self.now = t.now

    def paint(self, env):
        b, th = self.bounds, env.theme
        cx, cy = b.x + b.w / 2, b.y + b.h / 2
        r = min(b.w, b.h) / 2 - 3
        seg = 32
        for i in range(seg):  # rim
            a0, a1 = 2 * math.pi * i / seg, 2 * math.pi * (i + 1) / seg
            env.list.line(slikui.Vec2(cx + r * math.cos(a0), cy + r * math.sin(a0)),
                          slikui.Vec2(cx + r * math.cos(a1), cy + r * math.sin(a1)),
                          th.border, 1.0)
        for i in range(12):  # ticks
            a = 2 * math.pi * i / 12
            env.list.line(slikui.Vec2(cx + 0.85 * r * math.cos(a), cy + 0.85 * r * math.sin(a)),
                          slikui.Vec2(cx + r * math.cos(a), cy + r * math.sin(a)),
                          th.border, 1.0)
        sec = self.now % 60.0
        mins = (self.now / 60.0) % 60.0
        hrs = (self.now / 3600.0) % 12.0

        def hand(frac, length, color, width):
            a = 2 * math.pi * frac - math.pi / 2  # 12 o'clock up, clockwise
            env.list.line(slikui.Vec2(cx, cy),
                          slikui.Vec2(cx + length * math.cos(a), cy + length * math.sin(a)),
                          color, width)

        hand(hrs / 12.0, 0.5 * r, th.text, 3.0)
        hand(mins / 60.0, 0.72 * r, th.text, 2.0)
        hand(sec / 60.0, 0.85 * r, th.accent, 1.0)
        env.list.circle(slikui.Vec2(cx, cy), 2.5, th.text)


# doc: end

# doc: boundsdebug
import slikui


class BoundsDebug(slikui.Widget):
    """Wrapper that delegates layout to its child, then outlines every
    descendant in ``paint_over`` — which runs after the children have
    painted, so the outlines are never covered. The one-widget layout
    debugger."""

    def __init__(self):
        super().__init__()
        self.outlines = False

    def measure(self, constraints):
        if not self.children:
            return constraints.clamp(slikui.Size(0, 0))
        return self.children[0].measure(constraints)

    def arrange(self, area):
        self.bounds = area
        if self.children:
            self.children[0].arrange(area)

    def paint_over(self, env):
        # paint_over runs after the children, so the outlines sit on top.
        if not (self.outlines and self.children):
            return
        stack = [self.children[0]]
        while stack:  # every descendant, depth-first
            w = stack.pop()
            env.list.hollow_rect(w.bounds, env.theme.accent, 1)
            stack.extend(w.children)


# doc: end


def build(app):
    muted = slikui.Color.from_hex(MUTED)
    root = app.set_root(slikui.Column, padding=16, gap=8)

    root.add(slikui.Label, "Custom drawing", font_size=26)
    root.add(slikui.Label, "pure-Python subclasses of slikui.Widget — "
                           "env.list geometry, Labels for text").set_color(muted)

    root.add(slikui.Label, "Meter — click the bar to scrub").set_color(muted)
    meter = root.add(Meter, 0.4)

    row = root.add(slikui.Row, gap=14)
    left = row.add(slikui.Column, gap=4)
    spark = left.add(Sparkline, np.sin(np.arange(64) / 5.0))
    spark.color = slikui.Color.from_hex(BLUE)  # ctor args are positional
    left.add(slikui.Label, "Sparkline — hover").set_color(muted)
    mid = row.add(slikui.Column, gap=4)
    pair = mid.add(slikui.Row, gap=8)
    gauge = pair.add(RadialGauge, 0.7)
    clock = pair.add(ClockFace)
    mid.add(slikui.Label, "Gauge & Clock — tick-animated").set_color(muted)

    root.add(slikui.Label, "BoundsDebug — outlines every descendant") \
        .set_color(muted)
    wrap = root.add(BoundsDebug)
    panel = wrap.add(slikui.Panel, padding=10, gap=6)
    panel.add(slikui.Label, "a Panel hosting a Slider and this Label…")
    panel.add(slikui.Slider, 0, 1, value=0.3)
    outline = root.add(slikui.Checkbox, "show layout bounds", checked=False)
    outline.on_change(lambda v: setattr(wrap, "outlines", v))

    return _support.State(root=root, meter=meter, spark=spark, gauge=gauge,
                          clock=clock, wrap=wrap, outline=outline)


def smoke(app, state, session, log):
    c = _support.Checks(log)

    # Meter: a synthetic click scrubs the fill to the click's x fraction.
    b = state.meter.bounds
    _support.click(app, (b.x + b.w * 0.7, b.y + b.h / 2))
    session.advance(2)
    c.ok(abs(state.meter.value - 0.7) < 0.05,
         f"meter scrubbed to {state.meter.value:.2f}")

    # Sparkline: hover paints a crosshair (visible as a pixel diff).
    sb = state.spark.bounds
    before = _support.crop(app.render(), sb)
    _support.move(app, (sb.x + sb.w * 0.5, sb.y + sb.h * 0.5))
    session.advance(1)
    after = _support.crop(app.render(), sb)
    c.ok(state.spark.hover_x is not None, "sparkline tracks the pointer")
    c.ok(_support.diff(before, after) > 0, "crosshair painted")
    mb = state.meter.bounds  # move onto another widget: MouseLeave clears it
    _support.move(app, (mb.x + 4, mb.y + mb.h / 2))
    c.ok(state.spark.hover_x is None, "crosshair cleared on MouseLeave")

    # Gauge + clock are tick-driven: advance the deterministic clock.
    state.gauge.set_value(0.9)
    t0 = state.clock.now
    session.advance(30)  # half a second of scripted time
    c.ok(state.clock.now > t0, f"clock advanced {state.clock.now - t0:.2f}s")

    # BoundsDebug: the checkbox flips the outlines; the frame changes.
    session.advance(1)
    off = app.render().copy()  # copy: render() returns a live view
    b = state.outline.bounds
    _support.click(app, (b.x + 8, b.y + b.h / 2))
    session.advance(2)
    c.ok(state.wrap.outlines is True, "bounds debug enabled by checkbox")
    c.ok(_support.diff(off, app.render()) > 0, "outlines visible in the frame")
    return {"meter": round(state.meter.value, 2), "hover": state.spark.hover_x,
            "clock": round(state.clock.now, 2)}


def build_meter_docs(app):
    """docs/guide/custom-widgets.md's Meter: the same tree the C++ capture
    tool renders for its twin."""
    root = app.set_root(slikui.Column, padding=16, gap=8)
    root.add(slikui.Label, "Custom widget", font_size=26)
    root.add(slikui.Label, "Meter is a Python class subclassing slikui.Widget") \
        .set_color(slikui.Color.from_hex(0x9AA0B5))
    root.add(Meter, 0.4)
    root.add(slikui.Label, "Click the bar to scrub it")
    return _support.State(meter=root.children[2])


# Doc images: exact trees the docs pages embed, rendered by
# tools/python_examples_capture.py (or --docs-image from the CLI).
# image stem → (width, height, scale, builder callable)
DOCS = {
    "custom_widget": (340, 150, 1.0, build_meter_docs),
}


def interactive():
    from slikui.examples import _window
    return _window.run(NAME, SIZE, build)


def main(argv=None):
    return _support.main_entry(name=NAME, size=SIZE, build=build, smoke=smoke,
                               interactive=interactive,
                               docs=DOCS, argv=argv)


if __name__ == "__main__":
    raise SystemExit(main())
