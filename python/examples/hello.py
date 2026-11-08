"""Headless render of the M0 demo UI; writes hello.png next to this file."""

import os

import slikui
from slikui._py.pixels import save_image

ui = slikui.App(340, 150)
root = ui.set_root(slikui.Column, padding=16, gap=8)
root.add(slikui.Label, "Hello, slikui!", font_size=26)
root.add(slikui.Label, "Python bindings — rendered headless to numpy").set_color(
    slikui.Color.from_hex(0x9AA0B5)
)
row = root.add(slikui.Row, gap=10)
row.add(slikui.Button, "Default")
row.add(slikui.Button, "Primary", variant=slikui.ButtonVariant.Primary)

ui.tick(0.0, 1 / 60)
frame = ui.render()

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "hello.png")
save_image(frame, out)
print(f"wrote {out} ({frame.shape[1]}x{frame.shape[0]})")
