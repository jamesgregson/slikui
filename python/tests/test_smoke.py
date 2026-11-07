"""M0 smoke tests: values, software backend, context, first widgets."""

import gc

import numpy as np

import silk


def test_version_and_exports():
    assert silk.__version__
    for name in ("App", "Column", "Row", "Spacer", "Label", "Button",
                 "Color", "Vec2", "Time", "Target", "Sink", "Context"):
        assert hasattr(silk, name), name


def test_color():
    c = silk.Color.from_hex(0xE060B0)
    assert (c.r, c.g, c.b, c.a) == (0xE0, 0x60, 0xB0, 255)
    assert c.to_abgr() == 0xFFB060E0
    assert silk.Color(1, 2, 3) == silk.Color(1, 2, 3)
    assert silk.Color.from_hex(0x112233).with_alpha(0.5).a == 127


def test_hello_renders_text():
    ui = silk.App(240, 120)
    root = ui.set_root(silk.Column)
    root.set_padding(16).set_gap(8)
    root.add(silk.Label, "Hello, silk!").set_font_size(26)
    ui.tick(0.0, 1 / 60)
    frame = ui.render()
    assert isinstance(frame, np.ndarray)
    assert frame.shape == (120, 240)
    assert frame.dtype == np.uint32
    # Uniform background + one label: at least two distinct colors on screen.
    assert len(np.unique(frame)) > 1


def test_kwargs_add_and_chaining():
    ui = silk.App(240, 120)
    root = ui.set_root(silk.Column, padding=16, gap=8)
    root.add(silk.Button, "Save", variant=silk.ButtonVariant.Primary)
    root.add(silk.Label, "secondary", font_size=13)
    row = root.add(silk.Row, gap=4)
    row.add(silk.Spacer)
    row.add(silk.Button, "cancel", on_click=lambda: None)
    ui.tick(0.0, 1 / 60)
    frame = ui.render()
    assert len(np.unique(frame)) > 1


def test_borrowed_numpy_target_and_zero_copy():
    buf = np.zeros((60, 80), dtype=np.uint32)
    target = silk.Target(buf)
    sink = silk.Sink(target)
    ctx = silk.Context(sink)
    ctx.set_frame_size(80.0, 60.0)
    ctx.set_root_row()
    ctx.tick(0.0, 0.0)
    ctx.render()
    assert buf.any()  # the background was painted into OUR buffer

    view = target.pixels()
    view[0, 0] = 0xFF00FF00
    assert buf[0, 0] == 0xFF00FF00  # same memory, no copy


def test_context_keeps_sink_alive():
    # If keep_alive on Context(sink) is wrong, deleting the sink frees the
    # C++ Sink and render() below runs on freed memory.
    target = silk.Target(64, 64)
    sink = silk.Sink(target)
    ctx = silk.Context(sink)
    ctx.set_frame_size(64.0, 64.0)
    del sink
    gc.collect()
    for _ in range(1000):  # churn the heap so freed memory would be reused
        silk.Color(1, 2, 3, 4)
    ctx.render()
    assert (target.pixels() != 0).all()  # uniform background still painted


def test_widget_identity_and_lifecycle():
    ui = silk.App(120, 80)
    root = ui.set_root(silk.Column)
    label = root.add(silk.Label, "x")
    # Chained setters return the same Python object (no new wrapper per call).
    assert root.set_gap(4) is root
    assert label.set_text("y") is label
    assert label.type == "Label"
    assert root.find("nope") is None
    root.id = "root"
    assert root.find("root") is root
