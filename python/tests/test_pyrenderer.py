"""CanvasSink parity tests: the pure-Python numpy renderer must produce
pixel-identical frames to the C++ software.Sink for the same widget trees.
"""

import numpy as np

import silk
from silk._silk import Context, Sink, Target, find_system_font


def _context(sink, w, h, scale, build):
    ctx = Context(sink)
    ctx.set_frame_size(float(w), float(h))
    ctx.load_font_file(find_system_font())
    build(ctx)
    ctx.tick(0.0, 1 / 60)
    ctx.render()
    return ctx


def render_both(w, h, build, scale=1.0):
    target = Target(int(w * scale), int(h * scale))
    cpp = Sink(target)
    cpp.set_scale(float(scale))
    _context(cpp, w, h, scale, build)

    py = silk.CanvasSink(int(w * scale), int(h * scale))
    py.set_scale(float(scale))
    _context(py, w, h, scale, build)
    return target.pixels(), py.buffer


def _rich_tree(ctx):
    root = ctx.set_root_column()
    root.set_padding(10).set_gap(6)
    root.add(silk.Label, "Parity check").set_font_size(18)
    row = root.add(silk.Row, gap=4)
    row.add(silk.Button, "Save", variant=silk.ButtonVariant.Primary)
    row.add(silk.Button, "cancel")
    root.add(silk.Checkbox, "opt-in", checked=True)
    root.add(silk.Slider, 0.42)
    root.add(silk.Panel).add(silk.Label, "inside panel")


def test_parity_rich_tree():
    a, b = render_both(260, 220, _rich_tree)
    assert np.array_equal(a, b)


def test_parity_scroll_and_lines():
    def build(ctx):
        root = ctx.set_root_column()
        root.set_padding(8).set_gap(4)
        root.add(silk.Label, "scroll me")
        scroll = root.add(silk.ScrollArea)
        for i in range(20):
            scroll.add(silk.Label, f"line {i} — overflow the clip")
        root.add(silk.ProgressBar).set_value(0.7)
        root.add(silk.Divider)

    a, b = render_both(240, 180, build)
    assert np.array_equal(a, b)


def test_parity_hidpi_scale2():
    a, b = render_both(200, 150, _rich_tree, scale=2.0)
    assert a.shape == (300, 400) and b.shape == (300, 400)
    assert np.array_equal(a, b)


def test_canvas_sink_receives_upload_and_colors():
    target, buf = render_both(160, 100, lambda ctx: None)
    assert buf.shape == (100, 160)
    # Default background rect (clear=True) alone must have painted.
    theme_bg = 0  # nonzero unless the theme background is pure black
    assert (buf != 0).any() or theme_bg


def test_canvas_sink_deterministic():
    a1, b1 = render_both(160, 100, _rich_tree)
    a2, b2 = render_both(160, 100, _rich_tree)
    assert np.array_equal(b1, b2)
