"""M0 smoke tests: values, software backend, context, first widgets."""

import gc

import numpy as np

import slikui
from slikui._slikui import find_system_font

# Text-rendering checks need an actual font; minimal containers (manylinux
# wheel builds) have none — those tests skip rather than fail there.
_HAS_FONT = bool(find_system_font())


def test_version_and_exports():
    assert slikui.__version__
    for name in ("App", "Column", "Row", "Spacer", "Label", "Button",
                 "Color", "Vec2", "Time", "Target", "Sink", "Context"):
        assert hasattr(slikui, name), name


def test_color():
    c = slikui.Color.from_hex(0xE060B0)
    assert (c.r, c.g, c.b, c.a) == (0xE0, 0x60, 0xB0, 255)
    assert c.to_abgr() == 0xFFB060E0
    assert slikui.Color(1, 2, 3) == slikui.Color(1, 2, 3)
    assert slikui.Color.from_hex(0x112233).with_alpha(0.5).a == 127


def test_hello_renders_text():
    if not _HAS_FONT:
        import pytest

        pytest.skip("no system font (minimal container)")
    ui = slikui.App(240, 120)
    root = ui.set_root(slikui.Column)
    root.set_padding(16).set_gap(8)
    root.add(slikui.Label, "Hello, slikui!").set_font_size(26)
    ui.tick(0.0, 1 / 60)
    frame = ui.render()
    assert isinstance(frame, np.ndarray)
    assert frame.shape == (120, 240)
    assert frame.dtype == np.uint32
    # Uniform background + one label: at least two distinct colors on screen.
    assert len(np.unique(frame)) > 1


def test_kwargs_add_and_chaining():
    ui = slikui.App(240, 120)
    root = ui.set_root(slikui.Column, padding=16, gap=8)
    root.add(slikui.Button, "Save", variant=slikui.ButtonVariant.Primary)
    root.add(slikui.Label, "secondary", font_size=13)
    row = root.add(slikui.Row, gap=4)
    row.add(slikui.Spacer)
    row.add(slikui.Button, "cancel", on_click=lambda: None)
    ui.tick(0.0, 1 / 60)
    frame = ui.render()
    assert len(np.unique(frame)) > 1


def test_borrowed_numpy_target_and_zero_copy():
    buf = np.zeros((60, 80), dtype=np.uint32)
    target = slikui.Target(buf)
    sink = slikui.Sink(target)
    ctx = slikui.Context(sink)
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
    target = slikui.Target(64, 64)
    sink = slikui.Sink(target)
    ctx = slikui.Context(sink)
    ctx.set_frame_size(64.0, 64.0)
    del sink
    gc.collect()
    for _ in range(1000):  # churn the heap so freed memory would be reused
        slikui.Color(1, 2, 3, 4)
    ctx.render()
    assert (target.pixels() != 0).all()  # uniform background still painted


def test_widget_identity_and_lifecycle():
    ui = slikui.App(120, 80)
    root = ui.set_root(slikui.Column)
    label = root.add(slikui.Label, "x")
    # Chained setters return the same Python object (no new wrapper per call).
    assert root.set_gap(4) is root
    assert label.set_text("y") is label
    assert label.type == "Label"
    assert root.find("nope") is None
    root.id = "root"
    assert root.find("root") is root


def test_fontless_context_does_not_crash():
    # Minimal Linux containers (e.g. manylinux wheel builds) have no system
    # fonts: find_system_font() returns "" and the font never loads. The
    # fontless path must degrade to zero-size text, not segfault — the CI
    # wheel tests crashed in App.tick() before the ok_ guards existed.
    from slikui._slikui import Context, Sink, Target

    target = Target(120, 80)
    sink = Sink(target)
    ctx = Context(sink)  # no load_font_file call: invalid font on purpose
    ctx.set_frame_size(120.0, 80.0)
    root = ctx.set_root_column()
    root.add(slikui.Label, "no font loaded")
    root.add(slikui.Button, "still fine")
    ctx.tick(0.0, 1 / 60)
    ctx.render()  # segfaulted here before the fix
    assert (target.pixels() != 0).any()  # background + chrome still painted
