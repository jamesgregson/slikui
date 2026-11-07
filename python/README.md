# silk-ui — Python bindings for silk

`pip install silk-ui` (planned) · import `silk` · compiled module `silk._silk`.

Headless, testable UI: the software rasterizer renders into a `numpy`
framebuffer — no window, no GPU, no event loop required.

```python
import silk

ui = silk.App(320, 200)                     # Target + Sink + Context + font
root = ui.set_root(silk.Column, padding=16, gap=8)
root.add(silk.Label, "Hello, silk!", font_size=26)
root.add(silk.Button, "Save", variant=silk.ButtonVariant.Primary)

ui.tick(0.0, 1 / 60)                        # layout + animation
frame = ui.render()                         # zero-copy np.ndarray (H, W) uint32
```

Build from source (the C++ side is header-only, nothing to link):

```
pip install -e python/          # editable, into your venv
python -m pytest python/tests   # 34 tests: widgets, events, renderer parity
python python/examples/hello.py # writes hello.ppm
```

Events are first-class: build silk events in Python and drive the real
dispatch path (see `python/tests/test_events.py`):

```python
ui.handle(silk.mouse_press((60, 40)))   # click through hit-testing
ui.handle(silk.text_input("h"))         # into the focused TextField
ui.handle(silk.scroll(pos, (0, -1)))    # wheel, "ctrl+shift" mods supported
```

Interactive demo — a Python-native GLFW + OpenGL host with the silk software
rasterizer as the UI layer (`pip install glfw PyOpenGL`):

```
python python/examples/glfw_demo.py
```

Lit cube/sphere/torus over a procedural sky; the controls panel is rasterized
on the CPU into a `uint32` buffer and blended over the GL scene as one
texture. Drag orbits, wheel zooms, the panel edits material/sky/light/camera.

**Your own renderer.** `silk.RenderSink` is a trampoline over C++
`IRenderSink`: subclass it, implement `upload(tex)` and `render(commands,
fb_size)`, and silk hands you every frame as a flat command list — point it at
pygame, moderngl, PIL, anything. `silk.CanvasSink` is a worked example: a
pure-numpy rasterizer that produces **pixel-identical** output to the C++
software backend (same blend/SDF/sampling math, verified by test):

```python
sink = silk.CanvasSink(320, 240)
ctx = silk.Context(sink)          # any RenderSink subclass works here
ctx.set_frame_size(320, 240)
ctx.load_font_file(silk.find_system_font())
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
