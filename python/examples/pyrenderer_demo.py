"""Python renderer demo — the same widget tree through two renderers.

Left: silk's C++ software rasterizer (software.Sink).
Right: CanvasSink — a pure-Python numpy renderer implementing the same
IRenderSink contract (silk.RenderSink trampoline).

The two halves should be pixel-identical; the script reports any diff and
writes `shots/pyrenderer_demo.ppm` (convert with tools/ppm2png.py).

Run:  python examples/pyrenderer_demo.py
"""

import os
import sys

import numpy as np

import silk
from silk._py.pixels import to_rgba
from silk._silk import Context, Sink, Target, find_system_font

W, H = 480, 320


def build(ctx):
    root = ctx.set_root_column()
    root.set_padding(14).set_gap(8)
    root.add(silk.Label, "One tree, two renderers").set_font_size(22)
    row = root.add(silk.Row, gap=6)
    row.add(silk.Button, "Primary", variant=silk.ButtonVariant.Primary)
    row.add(silk.Button, "Secondary")
    row.add(silk.Button, "Ghost", variant=silk.ButtonVariant.Ghost)
    root.add(silk.Checkbox, "checkbox", checked=True)
    root.add(silk.Switch, checked=True)
    root.add(silk.Slider).set_value(0.35)
    root.add(silk.ProgressBar).set_value(0.62)
    root.add(silk.Divider)
    panel = root.add(silk.Panel)
    panel.set_padding(8)
    panel.add(silk.Label, "panel with rounded corners")
    scroll = root.add(silk.ScrollArea)
    for i in range(12):
        scroll.add(silk.Label, f"clipped row {i}")


def render(w, h):
    target = Target(w, H)
    cpp = Sink(target)
    ctx_a = Context(cpp)

    py = silk.CanvasSink(w, H)
    ctx_b = Context(py)

    for ctx in (ctx_a, ctx_b):
        ctx.set_frame_size(float(w), float(H))
        ctx.load_font_file(find_system_font())
        build(ctx)
        ctx.tick(0.0, 1 / 60)
        ctx.render()
    return target.pixels(), py.buffer


def write_ppm(path, frame):
    rgba = to_rgba(frame)[..., :3]  # H, W, 3
    with open(path, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (rgba.shape[1], rgba.shape[0]))
        f.write(rgba.tobytes())


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "shots/pyrenderer_demo.ppm"
    a, b = render(W, H)
    diff = int(np.count_nonzero(a != b))
    print(f"C++ software.Sink vs CanvasSink: {diff} differing pixels of {a.size}")
    if diff:
        ys, xs = np.nonzero(a != b)
        print(f"  first diff at row {ys[0]}, col {xs[0]}")

    # Side-by-side composite with a 4px divider column.
    divider = np.full((H, 4), 0xFF808080, np.uint32)
    combo = np.concatenate([a, divider, b], axis=1)
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    write_ppm(out, combo)
    print(f"wrote {out} ({W * 2 + 4}x{H})")


if __name__ == "__main__":
    main()
