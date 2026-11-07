"""Full-surface gallery rendered headless via the Python bindings.

Writes gallery.ppm next to this file (convert: python3 tools/ppm2png.py
python/examples/gallery.ppm python/examples/gallery.png).
"""

import os

import numpy as np

import silk
from silk._py.pixels import to_rgba

ui = silk.App(320, 720, scale=2)
root = ui.set_root(silk.Column, padding=14, gap=8)

# header
root.add(silk.Label, "silk — widget gallery", font_size=22)
root.add(silk.Label, "Python bindings · software backend").set_color(
    silk.Color.from_hex(0x8A94A0)
)

# buttons
row = root.add(silk.Row, gap=8)
row.add(silk.Button, "Default")
row.add(silk.Button, "Primary", variant=silk.ButtonVariant.Primary)
row.add(silk.Button, "Danger", variant=silk.ButtonVariant.Danger)
row.add(silk.Button, "Ghost", variant=silk.ButtonVariant.Ghost,
        tooltip="Borderless quiet action")

# toggles
row = root.add(silk.Row, gap=16)
row.add(silk.Checkbox, "Sync", checked=True)
row.add(silk.Radio, "Wi-Fi", checked=True)
row.add(silk.Radio, "Ethernet")
row.add(silk.Switch, checked=True)

# sliders / progress / spinner
root.add(silk.Slider, 0, 100, value=62)
root.add(silk.ProgressBar, value=0.72)
sp_row = root.add(silk.Row, gap=8)
sp_row.add(silk.Label, "Working…").set_color(silk.Color.from_hex(0x8A94A0))
sp_row.add(silk.Spacer)
sp_row.add(silk.Spinner, speed=240)

# inputs
root.add(silk.TextField, text="jane@example.com", placeholder="you@host")
root.add(silk.TextArea, rows=2, text="CPU-rendered UI —\nno GPU, no window.")

# views
root.add(silk.ListView, items=["alpha", "beta", "gamma", "delta"], selected=1)
root.add(silk.Tabs, tabs=["Home", "Edits", "View"], selected=1)
root.add(silk.Dropdown, items=["Linear", "Nearest", "Cubic", "Lanczos"],
         placeholder="Filter…")

# accordion (Expander groups)
acc = root.add(silk.Accordion, gap=4)
shape = acc.add_section("Shape", open=True)
shape.set_gap(4)
shape.add(silk.Label, "Cube / Sphere / Torus")
material = acc.add_section("Material")
material.add(silk.Slider, 0, 1, value=0.35)
acc.add_section("Environment")

# panel + image
panel = root.add(silk.Panel, padding=10, gap=6, radius=10)
panel.add(silk.Label, "Panel — a bordered container")
prow = panel.add(silk.Row, gap=12)
prow.add(silk.Checkbox, "Snap", checked=True)
prow.add(silk.Checkbox, "Grid")

grad = np.zeros((28, 180, 4), dtype=np.uint8)
ramp = np.linspace(40, 220, 180).astype(np.uint8)[None, :]
grad[..., 0] = ramp  # red ramps left→right
grad[..., 2] = ramp[::-1]
grad[..., 3] = 255
panel.add(silk.Image, "gradient").set_pixels(grad).set_size(180, 28).set_fit(
    silk.ImageFit.Stretch
)

ui.tick(0.0, 1 / 60)
ui.render()  # frame 1: glyph atlas uploads what measure() cached
for i in range(1, 4):
    ui.tick(i / 60, 1 / 60)
frame = ui.render()  # frame 4: atlas settled — every glyph present

rgba = to_rgba(frame)
h, w = rgba.shape[:2]
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "gallery.ppm")
with open(out, "wb") as f:
    f.write(f"P6\n{w} {h}\n255\n".encode())
    f.write(rgba[..., :3].tobytes())
print(f"wrote {out} ({w}x{h})")
