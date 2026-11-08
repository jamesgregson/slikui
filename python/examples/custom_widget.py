"""Custom widgets in pure Python — subclass slikui.Widget, implement the
measure/paint/on_event contract, and slikui lays out, paints and dispatches
events to them. Writes custom_widget.png next to this file.
"""

import os

import slikui
from slikui._py.pixels import save_image


class Meter(slikui.Widget):
    """A horizontal bar that fills as `value` (0..1) approaches 1."""

    def __init__(self, value=0.0):
        super().__init__()
        self.value = value
        self.h = 10.0

    def measure(self, constraints):
        return constraints.clamp(slikui.Size(0, self.h))

    def paint(self, env):
        t = env.theme
        env.list.rect(self.bounds, t.surface_alt, self.h / 2)
        if self.value > 0:
            fill = slikui.Rect(self.bounds.x, self.bounds.y,
                               self.bounds.w * self.value, self.bounds.h)
            env.list.rect(fill, t.accent, self.h / 2)

    def on_event(self, event):
        # Click to the left/right of the fill to move it.
        if event.type == slikui.EventType.MousePress and self.bounds.contains(event.pos):
            self.value = (event.pos.x - self.bounds.x) / self.bounds.w
            return True
        return False


ui = slikui.App(340, 150)
root = ui.set_root(slikui.Column, padding=16, gap=8)
root.add(slikui.Label, "Custom widget", font_size=26)
root.add(slikui.Label, "Meter is a Python class subclassing slikui.Widget") \
    .set_color(slikui.Color.from_hex(0x9AA0B5))
meter = root.add(Meter, 0.4)
root.add(slikui.Label, "Click the bar in the interactive demo to scrub it")

ui.tick(0.0, 1 / 60)
frame = ui.render()

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "custom_widget.png")
save_image(frame, out)
print(f"wrote {out} ({frame.shape[1]}x{frame.shape[0]}), meter value {meter.value:.2f}")
