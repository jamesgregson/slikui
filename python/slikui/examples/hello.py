"""01 — Hello, slikui: the smallest complete example.

One App, a two-widget column, one headless frame written to a numpy
buffer. This is the flow the getting-started page teaches.

Run:

    python -m slikui.examples.hello             # writes ./hello.png
    python -m slikui.examples.hello --png any/path.png
    python -m slikui.examples.hello --smoke     # scripted self-check
"""

from types import SimpleNamespace

import numpy as np

import slikui
from slikui.examples import _support

NAME = "hello"
SIZE = (340, 150)


# doc: hello-build
from types import SimpleNamespace

import slikui


def build(app):
    hits = []
    root = app.set_root(slikui.Column, padding=16, gap=8)
    root.add(slikui.Label, "Hello, slikui!", font_size=26)
    root.add(slikui.Label, "Python bindings — rendered headless to numpy") \
        .set_color(slikui.Color.from_hex(0x9AA0B5))
    row = root.add(slikui.Row, gap=10)
    row.add(slikui.Button, "Default")
    row.add(slikui.Button, "Primary", variant=slikui.ButtonVariant.Primary,
            on_click=lambda: hits.append("primary"))
    return SimpleNamespace(root=root, hits=hits)  # whatever smoke() needs


# doc: end

# doc: hello-frame
def frame(app):
    app.tick(0.0, 1 / 60)   # layout + animation step
    return app.render()     # zero-copy (H, W) uint32, ABGR packing


# doc: end

def smoke(app, state, session, log):
    c = _support.Checks(log)
    session.advance(2)
    f = frame(app)
    log.say(f"frame {f.shape[1]}x{f.shape[0]}, dtype {f.dtype}")
    c.ok(f.dtype == np.uint32 and f.shape == (SIZE[1], SIZE[0]),
         "uint32 framebuffer, (H, W)")
    colors = len(np.unique(f))
    c.ok(colors > 1, f"{colors} distinct colors — something was painted")

    # A synthetic click routes the real dispatch path into the button.
    b = state.root.children[2].children[1].bounds
    _support.click(app, (b.x + b.w / 2, b.y + b.h / 2))
    c.ok(state.hits == ["primary"], "synthetic click hit the Primary button")
    return {"frame": f.shape, "colors": colors, "clicks": state.hits}


def interactive():
    from slikui.examples import _window
    return _window.run(NAME, SIZE, build)


def main(argv=None):
    return _support.main_entry(name=NAME, size=SIZE, build=build, smoke=smoke,
                               interactive=interactive,
                               argv=argv)


if __name__ == "__main__":
    raise SystemExit(main())
