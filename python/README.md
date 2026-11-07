# slikui — Python bindings for slikui

`pip install slikui` (planned) · import `slikui` · compiled module `slikui._slikui`.

Headless, testable UI: the software rasterizer renders into a `numpy`
framebuffer — no window, no GPU, no event loop required.

```python
import slikui

ui = slikui.App(320, 200)                     # Target + Sink + Context + font
root = ui.set_root(slikui.Column, padding=16, gap=8)
root.add(slikui.Label, "Hello, slikui!", font_size=26)
root.add(slikui.Button, "Save", variant=slikui.ButtonVariant.Primary)

ui.tick(0.0, 1 / 60)                        # layout + animation
frame = ui.render()                         # zero-copy np.ndarray (H, W) uint32
```

Build from source (the C++ side is header-only, nothing to link):

```
pip install -e python/          # editable, into your venv
python -m pytest python/tests   # 34 tests: widgets, events, renderer parity
python python/examples/hello.py # writes hello.ppm
```

Events are first-class: build slikui events in Python and drive the real
dispatch path (see `python/tests/test_events.py`):

```python
ui.handle(slikui.mouse_press((60, 40)))   # click through hit-testing
ui.handle(slikui.text_input("h"))         # into the focused TextField
ui.handle(slikui.scroll(pos, (0, -1)))    # wheel, "ctrl+shift" mods supported
```

Interactive demo — a Python-native GLFW + OpenGL host with the slikui software
rasterizer as the UI layer (`pip install glfw PyOpenGL`):

```
python python/examples/glfw_demo.py
```

Lit cube/sphere/torus over a procedural sky; the controls panel is rasterized
on the CPU into a `uint32` buffer and blended over the GL scene as one
texture. Drag orbits, wheel zooms, the panel edits material/sky/light/camera.

**Your own renderer.** `slikui.RenderSink` is a trampoline over C++
`IRenderSink`: subclass it, implement `upload(tex)` and `render(commands,
fb_size)`, and slikui hands you every frame as a flat command list — point it at
pygame, moderngl, PIL, anything. `slikui.CanvasSink` is a worked example: a
pure-numpy rasterizer that produces **pixel-identical** output to the C++
software backend (same blend/SDF/sampling math, verified by test):

```python
sink = slikui.CanvasSink(320, 240)
ctx = slikui.Context(sink)          # any RenderSink subclass works here
ctx.set_frame_size(320, 240)
ctx.load_font_file(slikui.find_system_font())
# ... build the tree, tick ...
ctx.render()
frame = sink.buffer               # (240, 320) uint32, 0xAABBGGRR
```

See it side by side: `python python/examples/pyrenderer_demo.py` renders one
tree through both renderers and reports the pixel diff (it is 0).

Design and milestones: `docs/python-bindings-plan.md` — M0 (values, software
backend, core widgets), M1 (full widget surface + themes) and M2 (events,
popups, tooltips, simulated-input tests) are done; M3 (wheels/CI/docs) and
M4 (GLFW+GL backend bindings) are tabled.
