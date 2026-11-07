"""Full-surface gallery rendered headless via the Python bindings.

Writes gallery.ppm next to this file (convert: python3 tools/ppm2png.py
python/examples/gallery.ppm python/examples/gallery.png).
"""

import os

import numpy as np

import slikui
from slikui._py.pixels import to_rgba

ui = slikui.App(320, 720, scale=2)
root = ui.set_root(slikui.Column, padding=14, gap=8)

# header
root.add(slikui.Label, "slikui — widget gallery", font_size=22)
root.add(slikui.Label, "Python bindings · software backend").set_color(
    slikui.Color.from_hex(0x8A94A0)
)

# buttons
row = root.add(slikui.Row, gap=8)
row.add(slikui.Button, "Default")
row.add(slikui.Button, "Primary", variant=slikui.ButtonVariant.Primary)
row.add(slikui.Button, "Danger", variant=slikui.ButtonVariant.Danger)
row.add(slikui.Button, "Ghost", variant=slikui.ButtonVariant.Ghost,
        tooltip="Borderless quiet action")

# toggles
row = root.add(slikui.Row, gap=16)
row.add(slikui.Checkbox, "Sync", checked=True)
row.add(slikui.Radio, "Wi-Fi", checked=True)
row.add(slikui.Radio, "Ethernet")
row.add(slikui.Switch, checked=True)

# sliders / progress / spinner
root.add(slikui.Slider, 0, 100, value=62)
root.add(slikui.ProgressBar, value=0.72)
sp_row = root.add(slikui.Row, gap=8)
sp_row.add(slikui.Label, "Working…").set_color(slikui.Color.from_hex(0x8A94A0))
sp_row.add(slikui.Spacer)
sp_row.add(slikui.Spinner, speed=240)

# inputs
root.add(slikui.TextField, text="jane@example.com", placeholder="you@host")
root.add(slikui.TextArea, rows=2, text="CPU-rendered UI —\nno GPU, no window.")

# views
root.add(slikui.ListView, items=["alpha", "beta", "gamma", "delta"], selected=1)
root.add(slikui.Tabs, tabs=["Home", "Edits", "View"], selected=1)
root.add(slikui.Dropdown, items=["Linear", "Nearest", "Cubic", "Lanczos"],
         placeholder="Filter…")

# accordion (Expander groups)
acc = root.add(slikui.Accordion, gap=4)
shape = acc.add_section("Shape", open=True)
shape.set_gap(4)
shape.add(slikui.Label, "Cube / Sphere / Torus")
material = acc.add_section("Material")
material.add(slikui.Slider, 0, 1, value=0.35)
acc.add_section("Environment")

# panel + image
panel = root.add(slikui.Panel, padding=10, gap=6, radius=10)
panel.add(slikui.Label, "Panel — a bordered container")
prow = panel.add(slikui.Row, gap=12)
prow.add(slikui.Checkbox, "Snap", checked=True)
prow.add(slikui.Checkbox, "Grid")

grad = np.zeros((28, 180, 4), dtype=np.uint8)
ramp = np.linspace(40, 220, 180).astype(np.uint8)[None, :]
grad[..., 0] = ramp  # red ramps left→right
grad[..., 2] = ramp[::-1]
grad[..., 3] = 255
panel.add(slikui.Image, "gradient").set_pixels(grad).set_size(180, 28).set_fit(
    slikui.ImageFit.Stretch
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
