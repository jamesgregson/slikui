"""06 — Keyboard: focus, shortcuts, and a game driven by keys.

* Focus tour — Tab / Shift+Tab cycle the focusable widgets (each draws its
  own focus ring from FocusGained/FocusLost), arrows act on the focused one,
  Escape clears focus, Ctrl+R is a global shortcut (``ctx.add_shortcut``)
  that resets everything.
* Snake — a whole game in one widget: arrows/WASD steer, Space pauses,
  R restarts; the game loop runs in ``tick`` and pauses itself on
  FocusLost. Click to focus; the wheel changes speed.

Run:

    python -m slikui.examples.keyboard            # writes ./keyboard.png
    python -m slikui.examples.keyboard --smoke    # scripted self-check
    python -m slikui.examples.keyboard --window   # live
"""

import random

import slikui
from slikui.examples import _support

NAME = "keyboard"
SIZE = (460, 380)

# doc: chip
import slikui

T, K = slikui.EventType, slikui.Key


class Chip(slikui.Widget):
    """A focusable counter: arrows change it while focused. It reports
    focus changes through ``on_focus(chip, gained)``."""

    def __init__(self, name):
        super().__init__()
        self.name, self.count = name, 0
        self.on_focus = None

    def measure(self, constraints):
        return constraints.clamp(slikui.Size(64, 44))

    def stretch_cross(self):
        return False

    def focusable(self):
        return True

    def on_event(self, e):
        if e.type in (T.FocusGained, T.FocusLost):
            if self.on_focus:
                self.on_focus(self, e.type == T.FocusGained)
            return False
        if e.type == T.KeyPress and e.key in (K.Up, K.Right):
            self.count += 1
            return True
        if e.type == T.KeyPress and e.key in (K.Down, K.Left):
            self.count -= 1
            return True
        return e.type == T.MousePress  # a click focuses (Context does that)

    def paint(self, env):
        b, th = self.bounds, env.theme
        env.list.rect(b, th.surface_alt, 6)
        fill = min(abs(self.count), 10) / 10.0
        if fill:
            env.list.rect(slikui.Rect(b.x, b.bottom() - 4, b.w * fill, 4),
                          th.accent if self.count > 0 else th.danger, 2)
        if self.focused:
            env.list.hollow_rect(b.inflate(2, 2), th.accent, 2, 8)


# doc: end

# doc: snake
import random

import slikui

T, K = slikui.EventType, slikui.Key


class Snake(slikui.Widget):
    """Grid snake. Steering is buffered per step so a quick Up+Left between
    two steps can't reverse the snake into itself."""

    COLS, ROWS, CELL = 24, 14, 14
    DIRS = {K.Up: (0, -1), K.W: (0, -1), K.Down: (0, 1), K.S: (0, 1),
            K.Left: (-1, 0), K.A: (-1, 0), K.Right: (1, 0), K.D: (1, 0)}

    def __init__(self, seed=7):
        super().__init__()
        self.rng = random.Random(seed)  # deterministic food for scripted runs
        self.rate = 6.0                 # steps per second
        self.on_score = None
        self.restart()

    def restart(self):
        y = self.ROWS // 2
        self.body = [(5, y), (4, y), (3, y)]   # head first
        self.dir = self.next_dir = (1, 0)
        self.score, self.paused, self.dead, self._acc = 0, False, False, 0.0
        self.place_food()
        if self.on_score:
            self.on_score(self.score)

    def place_food(self):
        free = [(x, y) for x in range(self.COLS) for y in range(self.ROWS)
                if (x, y) not in self.body]
        self.food = self.rng.choice(free)

    def measure(self, constraints):
        return constraints.clamp(slikui.Size(self.COLS * self.CELL, self.ROWS * self.CELL))

    def stretch_cross(self):
        return False

    def focusable(self):
        return True

    def on_event(self, e):
        if e.type == T.FocusLost:
            self.paused = True  # never run unattended
        elif e.type == T.KeyPress and e.key in self.DIRS:
            d = self.DIRS[e.key]
            if d != (-self.dir[0], -self.dir[1]):  # no reversing
                self.next_dir = d
            return True
        elif e.type == T.KeyPress and e.key == K.Space:
            self.paused = not self.paused
            return True
        elif e.type == T.KeyPress and e.key == K.R:
            self.restart()
            return True
        elif e.type == T.Scroll:
            self.rate = min(max(self.rate + e.scroll.y, 2.0), 20.0)
            return True
        return e.type == T.MousePress

    def step(self):
        self.dir = self.next_dir
        hx, hy = self.body[0]
        head = (hx + self.dir[0], hy + self.dir[1])
        if (not (0 <= head[0] < self.COLS and 0 <= head[1] < self.ROWS)
                or head in self.body[:-1]):
            self.dead = True
            return
        self.body.insert(0, head)
        if head == self.food:
            self.score += 1
            if self.on_score:
                self.on_score(self.score)
            self.place_food()
        else:
            self.body.pop()

    def tick(self, t):
        if self.paused or self.dead:
            return
        self._acc += t.dt
        while self._acc >= 1.0 / self.rate and not self.dead:
            self._acc -= 1.0 / self.rate
            self.step()

    def paint(self, env):
        b, th, c = self.bounds, env.theme, self.CELL
        env.list.rect(b, th.surface_alt, 4)

        def cell(x, y, color, inset=1.0, radius=3.0):
            env.list.rect(slikui.Rect(b.x + x * c + inset, b.y + y * c + inset,
                                      c - 2 * inset, c - 2 * inset), color, radius)

        env.list.circle(slikui.Vec2(b.x + (self.food[0] + 0.5) * c,
                                    b.y + (self.food[1] + 0.5) * c), c * 0.35, th.warning)
        for i, (x, y) in enumerate(self.body):
            cell(x, y, th.danger if self.dead else (th.accent if i == 0 else th.text_dim))
        edge = th.accent if self.focused else th.border
        env.list.hollow_rect(b, edge, 2 if self.focused else 1, 4)


# doc: end


def build(app):
    muted = slikui.Color.from_hex(0x9AA0B5)
    root = app.set_root(slikui.Column, padding=16, gap=10)
    root.add(slikui.Label, "Keyboard", font_size=26)
    tabs = root.add(slikui.Tabs)
    tabs.set_tabs(["Focus tour", "Snake"])

    # --- page 1: focus tour
    tour = root.add(slikui.Column, gap=8)
    tour.add(slikui.Label, "Tab / Shift+Tab move focus · arrows change the "
                           "focused chip · Esc clears").set_color(muted)
    row = tour.add(slikui.Row, gap=10)
    field = row.add(slikui.TextField, placeholder="type here")
    chips = []
    for n in "ABC":  # custom widgets draw no text: a Label names each chip
        col = row.add(slikui.Column, gap=2)
        chips.append(col.add(Chip, n))
        col.add(slikui.Label, f"chip {n}").set_color(muted)
    status = tour.add(slikui.Label, "nothing focused")

    def on_focus(chip, gained):
        status.set_text(f"chip {chip.name} focused" if gained else "nothing focused")

    for chip in chips:
        chip.on_focus = on_focus

    def reset():
        for chip in chips:
            chip.count = 0
        field.set_text("")
        status.set_text("reset by Ctrl+R")

    ctrl = slikui.mods("ctrl")
    app.ctx.add_shortcut(slikui.Key.R, ctrl, reset)
    tour.add(slikui.Label, "Ctrl+R resets everything (a global shortcut)") \
        .set_color(muted)

    # --- page 2: snake
    page = root.add(slikui.Column, gap=6)
    score = page.add(slikui.Label, "score 0")
    snake = page.add(Snake)
    snake.on_score = lambda s: score.set_text(f"score {s}")
    page.add(slikui.Label, "arrows/WASD steer · Space pause · R restart · "
                           "wheel = speed").set_color(muted)

    pages = [tour, page]

    def show(i):
        for j, p in enumerate(pages):
            p.visible = j == i

    tabs.on_change(show)
    show(0)
    return _support.State(tabs=tabs, show=show, field=field, chips=chips,
                          status=status, snake=snake, score=score)


def smoke(app, state, session, log):
    c = _support.Checks(log)
    a, b, cc = state.chips

    # Tab order: field, A, B, C (tree order); Shift+Tab walks back.
    _support.tap(app, K.Tab)
    c.ok(state.field.focused, "Tab focuses the text field first")
    _support.tap(app, K.Tab)
    c.ok(a.focused and state.status.text == "chip A focused",
         "Tab → chip A (FocusGained updates the status)")
    _support.tap(app, K.Up)
    _support.tap(app, K.Up)
    c.ok(a.count == 2, "arrows act on the focused chip")
    _support.tap(app, K.Tab)
    _support.tap(app, K.Tab, "shift")
    c.ok(a.focused, "Shift+Tab walks back")
    _support.tap(app, K.Escape)
    c.ok(app.ctx.focused is None and state.status.text == "nothing focused",
         "Escape clears focus (FocusLost)")
    _support.tap(app, K.R, "ctrl")
    c.ok(a.count == 0 and state.status.text == "reset by Ctrl+R", "Ctrl+R shortcut")

    # Snake on its tab.
    state.tabs.set_selected(1)
    state.show(1)
    session.advance(2)
    s = state.snake
    s.request_focus()
    head0 = s.body[0]
    session.advance(20)  # 1/3 s at 6 steps/s → 2 steps right
    c.ok(s.body[0][0] > head0[0], "the snake moves right")
    _support.tap(app, K.Left)
    c.ok(s.next_dir == (1, 0), "no reversing into itself")
    _support.tap(app, K.Down)
    s.food = (s.body[0][0], s.body[0][1] + 1)  # food straight ahead
    session.advance(12)
    c.ok(s.score == 1 and state.score.text == "score 1", "eats the food, scores")
    app.handle(slikui.focus_lost())
    c.ok(s.paused, "pauses on FocusLost")
    s.request_focus()
    _support.tap(app, K.R)
    c.ok(s.score == 0 and not s.paused and len(s.body) == 3, "R restarts")
    return {"score": s.score, "steps_rate": s.rate}


def interactive():
    from slikui.examples import _window
    return _window.run(NAME, SIZE, build)


def main(argv=None):
    return _support.main_entry(name=NAME, size=SIZE, build=build, smoke=smoke,
                               interactive=interactive, argv=argv)


if __name__ == "__main__":
    raise SystemExit(main())
