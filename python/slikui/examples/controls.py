"""05 — Extended controls: custom widgets with full mouse + keyboard handling.

* Knob — drag vertically (Shift = fine), wheel steps, double-click resets,
  arrows/Home/End when focused (Ctrl = fine).
* XYPad — click to jump, drag to move (Shift locks the axis you started
  on), arrows nudge when focused; the teaching twin of the built-in Vec2Pad.
* Joystick — drag the stick, it springs back on release (animated in
  tick); WASD/arrows push while held (KeyPress/KeyRelease), Space freezes.
  It steers the ball in the box beside it.
* LimitSlider — the built-in Slider wrapped, with limit marks drawn on its
  track via ``paint_over`` (which runs after the children paint).

Every drag uses the same idiom: ``context.capture_mouse(self)`` on press,
``release_mouse`` on release, and cancel on FocusLost.

Run:

    python -m slikui.examples.controls            # writes ./controls.png
    python -m slikui.examples.controls --smoke    # scripted self-check
    python -m slikui.examples.controls --window   # live
"""

import math

import slikui
from slikui.examples import _support

NAME = "controls"
SIZE = (540, 360)


# doc: knob
import math

import slikui

T, K = slikui.EventType, slikui.Key
LEFT = slikui.MouseButton.MouseLeft


class Knob(slikui.Widget):
    """Rotary dial over [lo, hi]. ``on_change(value)`` fires on every edit."""

    START, SWEEP, DRAG_PX = 135.0, 270.0, 150.0  # degrees; px per full range

    def __init__(self, lo=0.0, hi=1.0, value=0.5, step=0.05):
        super().__init__()
        self.lo, self.hi, self.step = lo, hi, step
        self.value = self.default = min(max(value, lo), hi)
        self.on_change = None
        self._drag = None  # (start y, start value) while dragging
        self.set_cursor(slikui.Cursor.ResizeV)

    def set(self, v):
        v = min(max(v, self.lo), self.hi)
        if v != self.value:
            self.value = v
            if self.on_change:
                self.on_change(v)

    def measure(self, constraints):
        return constraints.clamp(slikui.Size(64, 64))

    def stretch_cross(self):
        return False

    def focusable(self):
        return True

    def on_event(self, e):
        span = self.hi - self.lo
        if e.type == T.MousePress and e.button == LEFT:
            self._drag = (e.pos.y, self.value)
            self.context.capture_mouse(self)
            return True
        if e.type == T.MouseMove and self._drag:
            y0, v0 = self._drag
            fine = 0.1 if e.shift else 1.0
            self.set(v0 + (y0 - e.pos.y) / self.DRAG_PX * span * fine)
            return True
        if e.type in (T.MouseRelease, T.FocusLost) and self._drag:
            self._drag = None
            self.context.release_mouse(self)
            return True
        if e.type == T.DoubleClick:
            self.set(self.default)
            return True
        if e.type == T.Scroll:
            self.set(self.value + e.scroll.y * self.step * span)
            return True
        if e.type == T.KeyPress and self.focused:
            step = self.step * span * (0.1 if e.ctrl else 1.0)
            moves = {K.Left: -step, K.Down: -step, K.Right: step, K.Up: step}
            if e.key in moves:
                self.set(self.value + moves[e.key])
            elif e.key == K.Home:
                self.set(self.lo)
            elif e.key == K.End:
                self.set(self.hi)
            else:
                return False
            return True
        return False

    def _pt(self, cx, cy, r, t):
        a = math.radians(self.START + t * self.SWEEP)
        return slikui.Vec2(cx + r * math.cos(a), cy + r * math.sin(a))

    def paint(self, env):
        b, th = self.bounds, env.theme
        cx, cy = b.x + b.w / 2, b.y + b.h / 2
        r = min(b.w, b.h) / 2 - 5
        t = (self.value - self.lo) / ((self.hi - self.lo) or 1.0)
        n = 36
        for i in range(n):  # track, then the value arc over it
            col = th.accent if (i + 1) / n <= t + 1e-6 else th.surface_alt
            env.list.line(self._pt(cx, cy, r, i / n), self._pt(cx, cy, r, (i + 1) / n),
                          col, 4.0)
        env.list.circle(slikui.Vec2(cx, cy), r * 0.62, th.surface)
        env.list.line(slikui.Vec2(cx, cy), self._pt(cx, cy, r * 0.55, t), th.text, 2.5)
        if self.focused:  # a round focus ring: hollow_rect with full radius
            env.list.hollow_rect(b.inflate(-1, -1), th.accent, 1.5, b.w / 2)


# doc: end

# doc: xypad
import slikui

T, K = slikui.EventType, slikui.Key
LEFT = slikui.MouseButton.MouseLeft


class XYPad(slikui.Widget):
    """2D value in [0, 1]^2 (y up). ``on_change(x, y)``."""

    def __init__(self, x=0.5, y=0.5):
        super().__init__()
        self.x, self.y = x, y
        self.on_change = None
        self._dragging = False
        self._lock = None  # "x"/"y" while Shift-dragging; None = free
        self._start = None
        self.set_cursor(slikui.Cursor.Crosshair)

    def set(self, x, y):
        self.x, self.y = min(max(x, 0.0), 1.0), min(max(y, 0.0), 1.0)
        if self.on_change:
            self.on_change(self.x, self.y)

    def measure(self, constraints):
        return constraints.clamp(slikui.Size(130, 130))

    def stretch_cross(self):
        return False

    def focusable(self):
        return True

    def _from_pos(self, p):
        b = self.bounds
        return (p.x - b.x) / b.w, 1.0 - (p.y - b.y) / b.h

    def on_event(self, e):
        if e.type == T.MousePress and e.button == LEFT:
            self._dragging, self._lock, self._start = True, None, (e.pos.x, e.pos.y)
            self.set(*self._from_pos(e.pos))  # click jumps
            self.context.capture_mouse(self)
            return True
        if e.type == T.MouseMove and self._dragging:
            x, y = self._from_pos(e.pos)
            if e.shift and self._lock is None:  # lock the dominant axis
                dx, dy = abs(e.pos.x - self._start[0]), abs(e.pos.y - self._start[1])
                self._lock = "x" if dx >= dy else "y"
            if self._lock == "x":
                y = self.y
            elif self._lock == "y":
                x = self.x
            self.set(x, y)
            return True
        if e.type in (T.MouseRelease, T.FocusLost) and self._dragging:
            self._dragging = False
            self.context.release_mouse(self)
            return True
        if e.type == T.KeyPress and self.focused:
            d = {K.Left: (-0.01, 0), K.Right: (0.01, 0),
                 K.Up: (0, 0.01), K.Down: (0, -0.01)}.get(e.key)
            if d:
                self.set(self.x + d[0], self.y + d[1])
                return True
        return False

    def paint(self, env):
        b, th = self.bounds, env.theme
        env.list.rect(b, th.surface_alt, 6)
        for i in range(1, 4):  # grid
            gx, gy = b.x + b.w * i / 4, b.y + b.h * i / 4
            env.list.line(slikui.Vec2(gx, b.y), slikui.Vec2(gx, b.bottom()), th.border, 1)
            env.list.line(slikui.Vec2(b.x, gy), slikui.Vec2(b.right(), gy), th.border, 1)
        px, py = b.x + self.x * b.w, b.y + (1.0 - self.y) * b.h
        env.list.line(slikui.Vec2(px, b.y), slikui.Vec2(px, b.bottom()), th.accent, 1)
        env.list.line(slikui.Vec2(b.x, py), slikui.Vec2(b.right(), py), th.accent, 1)
        env.list.circle(slikui.Vec2(px, py), 5, th.accent)
        env.list.hollow_rect(b, th.accent if self.focused else th.border, 1, 6)


# doc: end

# doc: joystick
import math

import slikui

T, K = slikui.EventType, slikui.Key
LEFT = slikui.MouseButton.MouseLeft


class Joystick(slikui.Widget):
    """Stick vector in the unit disc (y up), springing back to centre."""

    SPRING = 12.0  # 1/s — exponential return rate
    KEYS = {K.W: (0, 1), K.Up: (0, 1), K.S: (0, -1), K.Down: (0, -1),
            K.A: (-1, 0), K.Left: (-1, 0), K.D: (1, 0), K.Right: (1, 0)}

    def __init__(self):
        super().__init__()
        self.vx = self.vy = 0.0
        self.frozen = False
        self._dragging = False
        self._held = set()  # keys currently down — needs KeyRelease

    def measure(self, constraints):
        return constraints.clamp(slikui.Size(110, 110))

    def stretch_cross(self):
        return False

    def focusable(self):
        return True

    def _aim(self, p):
        b = self.bounds
        r = b.w / 2 - 14
        x, y = (p.x - b.center().x) / r, -(p.y - b.center().y) / r
        m = math.hypot(x, y)
        self.vx, self.vy = (x / m, y / m) if m > 1 else (x, y)

    def on_event(self, e):
        if e.type == T.MousePress and e.button == LEFT:
            self._dragging = True
            self._aim(e.pos)
            self.context.capture_mouse(self)
            return True
        if e.type == T.MouseMove and self._dragging:
            self._aim(e.pos)
            return True
        if e.type in (T.MouseRelease, T.FocusLost) and self._dragging:
            self._dragging = False
            self.context.release_mouse(self)
        if e.type == T.FocusLost:
            self._held.clear()
        if e.type == T.KeyPress and e.key in self.KEYS:
            self._held.add(e.key)
            return True
        if e.type == T.KeyRelease and e.key in self.KEYS:
            self._held.discard(e.key)
            return True
        if e.type == T.KeyPress and e.key == K.Space:
            self.frozen = not self.frozen
            return True
        return False

    def tick(self, t):
        if self._dragging or self.frozen:
            return
        if self._held:  # keys push the stick to the rim
            x = sum(self.KEYS[k][0] for k in self._held)
            y = sum(self.KEYS[k][1] for k in self._held)
            m = math.hypot(x, y) or 1.0
            self.vx, self.vy = x / m, y / m
            return
        k = math.exp(-self.SPRING * t.dt)  # spring back
        self.vx, self.vy = self.vx * k, self.vy * k

    def paint(self, env):
        b, th = self.bounds, env.theme
        env.list.rect(b, th.surface_alt, b.w / 2)
        env.list.hollow_rect(b, th.accent if self.focused else th.border, 1, b.w / 2)
        r = b.w / 2 - 14
        c = b.center()
        knob = slikui.Vec2(c.x + self.vx * r, c.y - self.vy * r)
        env.list.line(c, knob, th.border, 3)
        env.list.circle(knob, 12, th.warning if self.frozen else th.accent)


class BallBox(slikui.Widget):
    """A ball steered by a Joystick: velocity follows the stick."""

    def __init__(self, stick):
        super().__init__()
        self.stick = stick
        self.x, self.y, self.vx, self.vy = 0.5, 0.5, 0.0, 0.0

    def measure(self, constraints):
        return constraints.clamp(slikui.Size(140, 110))

    def tick(self, t):
        self.vx += self.stick.vx * 2.0 * t.dt
        self.vy -= self.stick.vy * 2.0 * t.dt
        self.vx *= 0.98
        self.vy *= 0.98
        self.x += self.vx * t.dt
        self.y += self.vy * t.dt
        for p, v in (("x", "vx"), ("y", "vy")):  # bounce off the walls
            if not 0.0 <= getattr(self, p) <= 1.0:
                setattr(self, p, min(max(getattr(self, p), 0.0), 1.0))
                setattr(self, v, -getattr(self, v))

    def paint(self, env):
        b, th = self.bounds, env.theme
        env.list.rect(b, th.surface_alt, 6)
        env.list.circle(slikui.Vec2(b.x + 8 + self.x * (b.w - 16),
                                    b.y + 8 + self.y * (b.h - 16)), 8, th.accent)


# doc: end

# doc: limitslider
import slikui


class LimitSlider(slikui.Widget):
    """The built-in Slider with limit marks on its track.

    Python can't subclass bound widgets, so this wraps one: layout is
    delegated to the child, and the marks are drawn in ``paint_over`` —
    after the Slider has painted, so they sit on top of its track. The
    track geometry mirrors the Slider's paint (8 px inset, centred; no
    label row).
    """

    INSET = 8.0

    def __init__(self, lo, hi, value, limits):
        super().__init__()
        self.lo, self.hi, self.limits = lo, hi, limits
        self.slider = self.add(slikui.Slider, lo, hi, value=value)

    def measure(self, constraints):
        return self.slider.measure(constraints)

    def arrange(self, area):
        self.bounds = area
        self.slider.arrange(area)

    def paint_over(self, env):
        b, th = self.slider.bounds, env.theme
        x0, x1 = b.x + self.INSET, b.right() - self.INSET
        cy = b.y + b.h / 2
        for v in self.limits:
            x = x0 + (v - self.lo) / (self.hi - self.lo) * (x1 - x0)
            env.list.line(slikui.Vec2(x, cy - 7), slikui.Vec2(x, cy + 7), th.warning, 2)


# doc: end


def build(app):
    muted = slikui.Color.from_hex(0x9AA0B5)
    root = app.set_root(slikui.Column, padding=16, gap=10)
    root.add(slikui.Label, "Extended controls", font_size=26)
    root.add(slikui.Label, "custom widgets with drag, wheel, double-click and keys") \
        .set_color(muted)

    row = root.add(slikui.Row, gap=18)
    col = row.add(slikui.Column, gap=4)
    knob = col.add(Knob, 0.0, 100.0, 40.0, 0.02)
    knob_label = col.add(slikui.Label, "gain 40")
    knob.on_change = lambda v: knob_label.set_text(f"gain {v:.0f}")

    col = row.add(slikui.Column, gap=4)
    pad = col.add(XYPad, 0.5, 0.5)
    pad_label = col.add(slikui.Label, "x 0.50  y 0.50")
    pad.on_change = lambda x, y: pad_label.set_text(f"x {x:.2f}  y {y:.2f}")

    col = row.add(slikui.Column, gap=4)
    stick = col.add(Joystick)
    col.add(slikui.Label, "WASD · Space").set_color(muted)
    ball = row.add(BallBox, stick)

    root.add(slikui.Label, "LimitSlider — paint_over marks on a built-in Slider") \
        .set_color(muted)
    limits = root.add(LimitSlider, -90.0, 90.0, 10.0, (-35.0, 60.0))
    return _support.State(knob=knob, knob_label=knob_label, pad=pad,
                          pad_label=pad_label, stick=stick, ball=ball,
                          limits=limits)


def smoke(app, state, session, log):
    c = _support.Checks(log)
    knob, pad, stick = state.knob, state.pad, state.stick
    kb = knob.bounds
    kc = (kb.x + kb.w / 2, kb.y + kb.h / 2)

    # Knob: drag up raises the value; the release outside still ends it.
    v0 = knob.value
    _support.drag(app, kc, (kc[0], kc[1] - 30))
    c.ok(knob.value > v0, f"knob dragged up to {knob.value:.0f}")
    c.ok(state.knob_label.text == f"gain {knob.value:.0f}", "readout follows")
    _support.drag(app, kc, (kc[0] + 300, kc[1] + 200))  # release far away
    v1 = knob.value
    _support.move(app, kc)                                # re-hover, no button
    c.ok(knob.value == v1, "no phantom drag after an outside release")
    _support.tap(app, K.Right)  # focused by the click
    c.ok(knob.focused and knob.value > v1, "Right steps the focused knob")
    _support.wheel(app, kc, 1.0)
    c.ok(knob.value > v1, "wheel steps")
    session.advance(30)
    _support.double_click(app, session, kc)
    c.ok(knob.value == knob.default, "double-click resets to default")

    # XYPad: click jumps, Shift-drag locks the dominant axis.
    pb = pad.bounds
    session.advance(30)
    _support.click(app, (pb.x + pb.w * 0.25, pb.y + pb.h * 0.25))
    c.ok(abs(pad.x - 0.25) < 0.02 and abs(pad.y - 0.75) < 0.02, "click jumps")
    _support.press(app, (pb.x + 20, pb.y + 20))
    y_pressed = pad.y
    for dx, dy in ((20, 2), (50, 5), (70, 9)):  # mostly-horizontal, Shift held
        e = slikui.mouse_move((pb.x + 20 + dx, pb.y + 20 + dy))
        e.mods = slikui.mods("shift")
        app.handle(e)
    _support.release(app, (pb.x + 90, pb.y + 29))
    c.ok(abs(pad.y - y_pressed) < 1e-6 and pad.x > 0.5, "shift-drag locks to x")
    c.ok(state.pad_label.text.startswith(f"x {pad.x:.2f}"), "readout follows")

    # Joystick: hold D (KeyPress … KeyRelease), the ball moves; springs back.
    stick.request_focus()
    app.handle(slikui.key_press(K.D))
    session.advance(20)
    c.ok(stick.vx > 0.9, "holding D pushes the stick right")
    app.handle(slikui.key_release(K.D))
    session.advance(40)
    c.ok(abs(stick.vx) < 0.05, "released: the stick springs back")
    c.ok(state.ball.x != 0.5, "the ball moved")

    # LimitSlider: the marks are real pixels on the Slider's track.
    lb = state.limits.bounds
    on = _support.crop(app.render(), lb)
    state.limits.limits = ()
    off = _support.crop(app.render(), lb)
    c.ok(_support.diff(on, off) > 0, "paint_over marks drawn on the track")
    state.limits.limits = (-35.0, 60.0)
    return {"knob": round(knob.value, 1), "pad": (round(pad.x, 2), round(pad.y, 2)),
            "ball": round(state.ball.x, 2)}


def interactive():
    from slikui.examples import _window
    return _window.run(NAME, SIZE, build)


def main(argv=None):
    return _support.main_entry(name=NAME, size=SIZE, build=build, smoke=smoke,
                               interactive=interactive, argv=argv)


if __name__ == "__main__":
    raise SystemExit(main())
