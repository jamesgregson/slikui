"""Headless render of the M0 demo UI; writes hello.ppm next to this file.

Convert with: python3 tools/ppm2png.py python/examples/hello.ppm \
python/examples/hello.png
"""

import os

import silk
from silk._py.pixels import to_rgba

ui = silk.App(340, 150)
root = ui.set_root(silk.Column, padding=16, gap=8)
root.add(silk.Label, "Hello, silk!", font_size=26)
root.add(silk.Label, "Python bindings — rendered headless to numpy").set_color(
    silk.Color.from_hex(0x9AA0B5)
)
row = root.add(silk.Row, gap=10)
row.add(silk.Button, "Default")
row.add(silk.Button, "Primary", variant=silk.ButtonVariant.Primary)

ui.tick(0.0, 1 / 60)
frame = ui.render()

rgba = to_rgba(frame)
h, w = rgba.shape[:2]
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "hello.ppm")
with open(out, "wb") as f:
    f.write(f"P6\n{w} {h}\n255\n".encode())
    f.write(rgba[..., :3].tobytes())
print(f"wrote {out} ({w}x{h})")
