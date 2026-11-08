"""Shared harness for the shipped examples: synthetic input, a deterministic
clock, and the CLI contract every example's ``main()`` delegates to.

This module is plumbing, not a widget library — the examples stay standalone
(widgets a file teaches live in that file).
"""

from __future__ import annotations

import argparse
from types import SimpleNamespace

import numpy as np

import slikui
from slikui._py.pixels import save_image

State = SimpleNamespace  # build() returns whatever the example needs later


# ---------------------------------------------------------------------------
# Synthetic input — thin wrappers over the real event constructors so smoke
# scripts read like actions, not event-field assignments.
# ---------------------------------------------------------------------------
def _button(button):
    return slikui.MouseButton.MouseLeft if button is None else button


def move(app, xy, delta=(0.0, 0.0)):
    app.handle(slikui.mouse_move(xy, delta))


def press(app, xy, button=None, mods_=""):
    app.handle(slikui.mouse_press(xy, _button(button), slikui.mods(mods_)))


def release(app, xy, button=None, mods_=""):
    app.handle(slikui.mouse_release(xy, _button(button), slikui.mods(mods_)))


def click(app, xy, button=None, mods_=""):
    press(app, xy, button, mods_)
    release(app, xy, button, mods_)


def double_click(app, session, xy, gap_frames=10, button=None):
    """Two clicks ``gap_frames`` apart — widgets detect this by counting in
    ``tick()``, since slikui events carry no timestamps."""
    click(app, xy, button)
    session.advance(gap_frames)
    click(app, xy, button)


def drag(app, a, b, steps=8, button=None, mods_=""):
    """Press at ``a``, interpolated moves, release at ``b``."""
    press(app, a, button, mods_)
    for i in range(1, steps + 1):
        t = i / steps
        move(app, (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t))
    release(app, b, button, mods_)


def wheel(app, xy, dy, dx=0.0):
    app.handle(slikui.scroll(xy, (dx, dy)))


def tap(app, key, mods_=""):
    app.handle(slikui.key_press(key, slikui.mods(mods_)))
    app.handle(slikui.key_release(key, slikui.mods(mods_)))


def type_text(app, text):
    for e in slikui.text_input_str(text):
        app.handle(e)


# ---------------------------------------------------------------------------
# Deterministic clock — smoke passes never read wall time, so scripted PNGs
# and asserts are reproducible.
# ---------------------------------------------------------------------------
class Session:
    def __init__(self, app, fps=60.0):
        self.app = app
        self.t = 0.0
        self.dt = 1.0 / fps
        self.frame = 0

    def advance(self, frames=1):
        for _ in range(frames):
            self.frame += 1
            self.t += self.dt
            self.app.tick(self.t, self.dt)
        return self


class Transcript(list):
    """Smoke log; printed at the end of a --smoke pass."""

    def say(self, msg):
        self.append(str(msg))


class Checks:
    """Self-checking smoke asserts; every check prints a transcript line."""

    def __init__(self, log):
        self.log = log
        self.count = 0

    def ok(self, cond, label):
        self.count += 1
        if not cond:
            raise AssertionError(f"smoke check failed: {label}")
        self.log.say(f"ok  {label}")


# ---------------------------------------------------------------------------
# Tree + frame helpers
# ---------------------------------------------------------------------------
def walk(root):
    yield root
    for child in root.children:
        yield from walk(child)


def find(root, widget_id):
    for w in walk(root):
        if w.id == widget_id:
            return w
    return None


def crop(frame, bounds, scale=1.0):
    """A copy of the framebuffer region of a widget's bounds (logical → px).

    The copy matters: `App.render()` returns a zero-copy view of the live
    target, so an un-copied "before" frame changes underneath you when the
    next render repaints the same memory — diff checks would always read 0.
    """
    y, x = int(bounds.y * scale), int(bounds.x * scale)
    h, w = int(bounds.h * scale), int(bounds.w * scale)
    return frame[y:y + h, x:x + w].copy()


def diff(a, b):
    """Count differing pixels between two frames (or crops)."""
    return int(np.count_nonzero(np.asarray(a) != np.asarray(b)))


# ---------------------------------------------------------------------------
# Driver — the fixed CLI contract: default | --png | --smoke | --docs-image
# | --window. Examples implement build()/smoke() and delegate main() here.
# ---------------------------------------------------------------------------
def _render(build, size, scale, path):
    """Build + settle + render one frame into ``path``; returns the frame."""
    app = slikui.App(*size, scale=scale)
    try:
        build(app)
        session = Session(app)
        session.advance(2)
        frame = app.render()
        save_image(frame, path)
        return frame
    finally:
        app.close()


def run_smoke(name, size, build, smoke, png=None, scale=1.0):
    """Programmatic smoke pass — used by ``--smoke`` and the test suite.

    Runs ``smoke(app, state, session, log)`` (which asserts via
    ``Checks``), renders the settled frame, optionally saves it, prints the
    transcript and returns the example's summary dict.
    """
    if smoke is None:
        raise SystemExit(f"{name}: no smoke pass implemented")
    app = slikui.App(*size, scale=scale)
    try:
        state = build(app)
        session = Session(app)
        session.advance(2)
        log = Transcript()
        summary = dict(smoke(app, state, session, log) or {})
        session.advance(2)
        frame = app.render()
        if png:
            save_image(frame, png)
    finally:
        app.close()
    print(f"[{name}] smoke — {len(log)} transcript lines")
    for line in log:
        print(f"  {line}")
    if png:
        print(f"  wrote {png} ({frame.shape[1]}x{frame.shape[0]})")
    return summary


def main_entry(*, name, size, build, smoke=None, interactive=None, docs=None,
               scale=1.0, argv=None):
    """The shared CLI. Modes (mutually exclusive, first match wins):

    (default)        render one frame to ``--out`` or ``./NAME.png``
    --png PATH       same, explicit target (docs capture tool)
    --smoke [PNG]    scripted interaction pass, self-checking
    --docs-image NAME PATH
                     render one of the module's DOCS images (exact trees the
                     docs pages embed); ``tools/python_examples_capture.py``
                     loops these
    --window         live window via the module's ``interactive()`` (optional)
    """
    docs = docs or {}
    p = argparse.ArgumentParser(
        prog=f"python -m slikui.examples.{name}",
        description=f"slikui shipped example: {name}")
    p.add_argument("--out", metavar="PNG",
                   help="default mode: screenshot path (default ./NAME.png)")
    p.add_argument("--png", metavar="PATH",
                   help="render-only: write the settled frame to PATH")
    p.add_argument("--smoke", nargs="?", const="", metavar="PNG",
                   help="scripted interaction pass; optional output path "
                        "(default ./NAME_smoke.png)")
    p.add_argument("--docs-image", nargs=2, metavar=("NAME", "PNG"),
                   help="render a named DOCS image")
    p.add_argument("--window", action="store_true",
                   help="open a live window (needs 'pip install slikui-python[demo]')")
    args = p.parse_args(argv)

    if args.window:
        if interactive is None:
            print(f"{name}: no window mode (headless by design)")
            return 2
        return interactive() or 0
    if args.docs_image:
        image, path = args.docs_image
        if image not in docs:
            p.error(f"no docs image {image!r}; known: {', '.join(docs) or 'none'}")
        w, h, s, builder = docs[image]
        _render(builder, (w, h), s, path)
        print(f"{name}: wrote {path}")
        return 0
    if args.png is not None:
        _render(build, size, scale, args.png)
        print(f"{name}: wrote {args.png}")
        return 0
    if args.smoke is not None:
        run_smoke(name, size, build, smoke, args.smoke or f"{name}_smoke.png",
                  scale=scale)
        return 0
    default_path = args.out or f"{name}.png"
    _render(build, size, scale, default_path)
    print(f"{name}: wrote {default_path} "
          "(--window for live, --smoke for the scripted pass)")
    return 0
