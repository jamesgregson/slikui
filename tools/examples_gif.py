#!/usr/bin/env python3
"""tools/examples_gif.py — animated docs images from the shipped examples.

Motion only where motion explains: Snake (the game loop in tick) and the
ClockFace (tick-driven animation). Both run on the examples' deterministic
clock, so the GIFs are reproducible; frames are cropped to the widget.

    python tools/examples_gif.py   # writes docs/images/examples-snake.gif,
                                   #        docs/images/examples-clock.gif
"""

from __future__ import annotations

import pathlib

from PIL import Image

import slikui
from slikui.examples import _support, keyboard, paint
from slikui._py.pixels import to_rgba

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "docs" / "images"
SCALE = 2.0


def crop(frame, r, pad=6):
    s = SCALE
    y0, x0 = int((r.y - pad) * s), int((r.x - pad) * s)
    y1, x1 = int((r.bottom() + pad) * s), int((r.right() + pad) * s)
    return Image.fromarray(to_rgba(frame[max(y0, 0):y1, max(x0, 0):x1]), "RGBA")


def save_gif(frames, path, ms):
    rgb = [f.convert("RGB") for f in frames]
    rgb[0].save(path, save_all=True, append_images=rgb[1:], duration=ms, loop=0,
                optimize=True)
    print(f"wrote {path.relative_to(ROOT)} ({len(frames)} frames)")


def autopilot(s):
    """Greedy steering toward the food, never reversing or hitting itself."""
    hx, hy = s.body[0]
    options = []
    for d in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        if d == (-s.dir[0], -s.dir[1]):
            continue
        n = (hx + d[0], hy + d[1])
        if not (0 <= n[0] < s.COLS and 0 <= n[1] < s.ROWS) or n in s.body[:-1]:
            continue
        options.append((abs(n[0] - s.food[0]) + abs(n[1] - s.food[1]), d))
    if options:
        s.next_dir = min(options)[1]


def snake_gif():
    app = slikui.App(*keyboard.SIZE, scale=SCALE)
    try:
        st = keyboard.build(app)
        st.tabs.set_selected(1)
        st.show(1)
        session = _support.Session(app)
        session.advance(2)
        s = st.snake
        s.request_focus()
        frames = []
        for i in range(160):          # 160 * 5 frames = 13 s at 60 fps
            for _ in range(5):
                autopilot(s)
                session.advance(1)
            if i % 2 == 0:
                frames.append(crop(app.render(), s.bounds))
            if s.dead:
                break
        save_gif(frames, OUT / "examples-snake.gif", 1000 // 6)
    finally:
        app.close()


def clock_gif():
    app = slikui.App(*paint.SIZE, scale=SCALE)
    try:
        st = paint.build(app)
        session = _support.Session(app)
        frames = []
        for i in range(30):           # 15 s of clock time, 2 frames per second
            session.advance(30)
            st.gauge.set_value(0.5 + 0.45 * ((i % 15) / 15.0))
            r = st.gauge.bounds
            both = slikui.Rect(r.x, r.y, st.clock.bounds.right() - r.x, r.h)
            frames.append(crop(app.render(), both))
        save_gif(frames, OUT / "examples-clock.gif", 250)
    finally:
        app.close()


if __name__ == "__main__":
    snake_gif()
    clock_gif()
