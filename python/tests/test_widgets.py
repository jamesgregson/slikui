"""M1 tests: full widget surface, callbacks, themes, images."""

import numpy as np
import pytest

import slikui


def make_ui(w=300, h=400, **kw):
    ui = slikui.App(w, h, **kw)
    return ui, ui.set_root(slikui.Column, padding=10, gap=6)


def test_controls_render_and_kwargs():
    ui, root = make_ui()
    row = root.add(slikui.Row, gap=12)
    row.add(slikui.Checkbox, "Sync", checked=True)
    row.add(slikui.Radio, "Wi-Fi", checked=True)
    row.add(slikui.Radio, "Ethernet")
    row.add(slikui.Switch, checked=True)
    root.add(slikui.Slider, 0, 100, value=62)
    root.add(slikui.ProgressBar, value=0.72)
    root.add(slikui.Spinner, speed=180)
    ui.tick(0.0, 1 / 60)
    assert len(np.unique(ui.render())) > 1


def test_callbacks_fire_without_events():
    ui, root = make_ui()
    fired = []
    cb = root.add(slikui.Checkbox, "c", on_change=lambda v: fired.append(v))
    cb.set_checked(True, True)
    assert fired == [True]

    slider = root.add(slikui.Slider, 0, 1, on_change=lambda v: fired.append(("s", round(v, 2))))
    slider.set_value(0.5)  # set_value defaults to firing the callback
    assert ("s", 0.5) in fired


def test_inputs():
    ui, root = make_ui()
    got = []
    root.add(slikui.TextField, text="jane@example.com", placeholder="you@host",
             on_change=lambda t: got.append(t))
    root.add(slikui.TextArea, rows=2, text="line one\nline two")
    ui.tick(0.0, 1 / 60)
    assert len(np.unique(ui.render())) > 1
    assert got == []  # programmatic set_text must not fire on_change


def test_views_and_scrollarea_routing():
    ui, root = make_ui(260, 320)
    scroll = root.add(slikui.ScrollArea)
    scroll.content.set_gap(4)
    scroll.add(slikui.Label, "in the content column")  # routed via ScrollArea::add
    scroll.add(slikui.Button, "also content")
    root.add(slikui.ListView, items=["alpha", "beta", "gamma"], selected=1)
    root.add(slikui.Tabs, tabs=["Home", "Edits", "View"], selected=1)
    root.add(slikui.Dropdown, items=["Linear", "Nearest", "Cubic"], placeholder="Filter…")
    ui.tick(0.0, 1 / 60)
    assert len(np.unique(ui.render())) > 1
    assert scroll.content.find("nope") is None


def test_accordion_sections():
    ui, root = make_ui(240, 300)
    acc = root.add(slikui.Accordion, gap=4, exclusive=False)
    shape = acc.add_section("Shape", open=True)
    assert shape.type == "Expander" and shape.open
    shape.set_gap(4)
    shape.add(slikui.Label, "Cube / Sphere / Torus")
    material = acc.add_section("Material")
    material.add(slikui.Slider, 0, 1, value=0.35)
    acc.add_section("Environment")
    assert acc.exclusive is False
    ui.tick(0.0, 1 / 60)
    assert len(np.unique(ui.render())) > 1


def test_panel_divider_image():
    ui, root = make_ui(220, 220)
    panel = root.add(slikui.Panel, padding=10, gap=6, radius=10)
    panel.add(slikui.Label, "Panel")
    panel.add(slikui.Divider)
    panel.add(slikui.Label, "below the rule")
    grad = np.zeros((16, 64, 4), dtype=np.uint8)
    grad[..., 0] = np.arange(64) * 4  # red ramp
    grad[..., 3] = 255
    img = root.add(slikui.Image, "gradient").set_pixels(grad).set_size(128, 32)
    assert (img.width, img.height) == (64, 16)
    ui.tick(0.0, 1 / 60)
    frame = ui.render()
    assert len(np.unique(frame)) > 3  # gradient adds many colors


def test_theme_presets():
    assert len(slikui.theme_presets()) == 8
    names = [p.name for p in slikui.theme_presets()]
    assert names[0] == "Dark" and "Nord" in names

    dark_ui, _ = make_ui(60, 40)
    nord_ui, _ = make_ui(60, 40, theme=slikui.Theme.nord())
    dark_ui.tick(0.0, 0.0), nord_ui.tick(0.0, 0.0)
    bg_dark = dark_ui.render()[0, 0]
    bg_nord = nord_ui.render()[0, 0]
    assert bg_dark != bg_nord

    t = slikui.Theme()
    slikui.apply_preset(t, 99)  # wraps around, still a valid preset
    assert t.background != slikui.Theme.dark().background


def test_atlas_settles_by_second_frame():
    # Glyphs cached during paint (TextField metrics) miss the first frame's
    # atlas upload; the second render must be identical to the third.
    ui, root = make_ui()
    root.add(slikui.TextField, text="jane@example.com")
    root.add(slikui.TextArea, rows=2, text="no GPU, no window — U, j, @, x")
    frames = []
    for i in range(3):
        ui.tick(i / 60, 1 / 60)
        frames.append(ui.render().copy())
    assert not np.array_equal(frames[0], frames[1])  # atlas regrew
    assert np.array_equal(frames[1], frames[2])      # then stable


def test_unknown_kwarg_raises():
    ui, root = make_ui()
    with pytest.raises(AttributeError):
        root.add(slikui.Label, "x", bogus_property=1)


def test_hidscale_framebuffer():
    ui = slikui.App(80, 50, scale=2.0)
    ui.set_root(slikui.Column)
    ui.tick(0.0, 0.0)
    frame = ui.render()
    assert frame.shape == (100, 160)  # logical 80x50 at 2x
