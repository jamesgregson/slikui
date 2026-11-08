# slikui — Python bindings for the slikui C++ UI library

Install from PyPI · import `slikui` · compiled module `slikui._slikui`.

```sh
pip install slikui-python
```

pip picks the wheel matching your OS, CPU and Python (3.10–3.14 on Linux
x86_64/aarch64, macOS arm64, Windows x64). No wheel for your platform (e.g.
Intel macOS)? pip builds from the sdist instead — that needs a C++20
compiler. The same files are attached to every
[GitHub Release](https://github.com/jamesgregson/slikui/releases).

Headless, testable UI: the software rasterizer renders into a `numpy`
framebuffer — no window, no GPU, no event loop required.

```python
import slikui

ui = slikui.App(320, 200)                     # Target + Sink + Context + font
root = ui.set_root(slikui.Column, padding=16, gap=8)
root.add(slikui.Label, "Hello, slikui!", font_size=26)
root.add(slikui.Button, "Save", variant=slikui.ButtonVariant.Primary)

ui.tick(0.0, 1 / 60)                          # layout + animation
frame = ui.render()                           # zero-copy np.ndarray (H, W) uint32

ui.handle(slikui.mouse_press((60, 40)))       # real events, real dispatch path
```

## Documentation

The full docs live at **<https://jamesgregson.github.io/slikui/>** —
C++ and Python side by side on every page:

- [Getting started — Python](https://jamesgregson.github.io/slikui/getting-started/python/)
- [Python bindings reference](https://jamesgregson.github.io/slikui/reference/python-bindings/) —
  `App`, builder sugar (`container.add(cls, **props)` → setters), event
  constructors, custom widgets in pure Python, leak-free shutdown
- [Widget index](https://jamesgregson.github.io/slikui/reference/) — every
  widget with rendered examples and C++/Python snippets
- [Your own renderer](https://jamesgregson.github.io/slikui/guide/backends/) —
  subclass `slikui.RenderSink` and point slikui at pygame, moderngl, PIL,
  anything; `slikui.CanvasSink` is a pure-numpy reference sink that is
  pixel-identical to the C++ software backend
- [Custom widgets](https://jamesgregson.github.io/slikui/guide/custom-widgets/) —
  subclass `slikui.Widget`, implement `measure`/`paint`/`on_event`, and the
  C++ core lays out, paints and dispatches to your class

## Build from source

The C++ side is header-only, nothing to link:

```sh
pip install -e .                # editable, into your venv — from the repo root
python -m pytest python/tests   # widgets, events, custom widgets, renderer parity
python python/examples/hello.py # writes hello.png
python python/examples/glfw_demo.py   # interactive GLFW + OpenGL demo
```
