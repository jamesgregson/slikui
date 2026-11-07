"""M2 tests: simulated input — popups, tooltips, typing, scrolling."""

import numpy as np

import slikui


def make_ui(w=220, h=160, **kw):
    ui = slikui.App(w, h, **kw)
    return ui, ui.set_root(slikui.Column, padding=10, gap=8)


def center(w):
    r = w.bounds
    return (r.x + r.w / 2, r.y + r.h / 2)


def click(ui, pos):
    ui.handle(slikui.mouse_press(pos))
    ui.handle(slikui.mouse_release(pos))


def test_event_factories():
    e = slikui.mouse_press((12, 34), mods_="ctrl+shift")
    assert e.type == slikui.EventType.MousePress
    assert (e.pos.x, e.pos.y) == (12, 34)
    assert e.button == int(slikui.MouseButton.MouseLeft)
    assert e.ctrl and e.shift and not e.alt
    assert slikui.mods("alt|cmd") == int(slikui.Mod.ModAlt | slikui.Mod.ModSuper)
    assert [x.codepoint for x in slikui.text_input_str("hi!")] == [ord("h"), ord("i"), ord("!")]


def test_dropdown_popup_opens_selects_closes():
    ui, root = make_ui(220, 160)
    dd = root.add(slikui.Dropdown, items=["Cube", "Sphere", "Torus"], placeholder="Pick")
    picked = []
    dd.on_select(lambda i: picked.append(i))
    ui.tick(0.0, 1 / 60)
    ui.render()
    base = ui.render().copy()
    assert ui.ctx.popup_count == 0

    # click opens the popup (needs a tick to lay out)
    click(ui, center(dd))
    assert ui.ctx.popup_count == 1
    ui.tick(1 / 60, 1 / 60)
    assert not np.array_equal(base, ui.render())  # popup visibly drawn

    # click item 0: popup Panel bounds + 4px padding; rows split the rest
    r = ui.ctx.popup(0).bounds
    row_h = (r.h - 8) / 3
    click(ui, (r.x + r.w / 2, r.y + 4 + row_h * 0.5))
    assert picked == [0]
    assert dd.selected == 0
    ui.tick(2 / 60, 1 / 60)
    assert ui.ctx.popup_count == 0


def test_popup_dismissal():
    ui, root = make_ui(220, 160)
    dd = root.add(slikui.Dropdown, items=["A", "B", "C"])
    ui.tick(0.0, 1 / 60)
    click(ui, center(dd))
    assert ui.ctx.has_popups

    ui.handle(slikui.mouse_press((4, 4)))  # outside click dismisses
    assert ui.ctx.popup_count == 0

    click(ui, center(dd))
    assert ui.ctx.has_popups
    ui.handle(slikui.key_press(slikui.Key.Escape))  # Escape dismisses
    assert ui.ctx.popup_count == 0


def test_accordion_click_toggles():
    ui, root = make_ui(200, 300)
    acc = root.add(slikui.Accordion, gap=4)
    a = acc.add_section("Alpha", open=True)
    b = acc.add_section("Beta")
    a.add(slikui.Label, "content a")
    ui.tick(0.0, 1 / 60)
    assert a.open and not b.open

    beta = acc.children[1].bounds  # exclusive: opening Beta closes Alpha
    pos = (beta.x + 30, beta.y + beta.h / 2)
    click(ui, pos)
    ui.tick(1 / 60, 1 / 60)
    assert b.open and not a.open

    beta = acc.children[1].bounds  # layout shifted once Alpha collapsed
    pos = (beta.x + 30, beta.y + beta.h / 2)
    click(ui, pos)
    ui.tick(2 / 60, 1 / 60)
    assert not b.open


def test_tooltip_after_hover():
    ui, root = make_ui(220, 120)
    btn = root.add(slikui.Button, "Save", tooltip="Saves your work")
    ui.tick(0.0, 1 / 60)
    ui.handle(slikui.mouse_move(center(btn)))
    ui.tick(0.1, 0.1)  # hover timer starts
    ui.tick(0.8, 0.7)  # past the 0.6 s delay
    assert ui.ctx.popup_count == 1
    before = ui.render().copy()

    ui.handle(slikui.mouse_move((4, 4)))  # moving away dismisses
    ui.tick(1.0, 0.2)
    assert ui.ctx.popup_count == 0
    assert not np.array_equal(before, ui.render())


def test_textfield_typing_submit_backspace():
    ui, root = make_ui(240, 120)
    tf = root.add(slikui.TextField)
    submitted = []
    tf.on_submit(lambda t: submitted.append(t))
    ui.tick(0.0, 1 / 60)

    click(ui, center(tf))  # focus + place caret
    for ch in "hi!":
        ui.handle(slikui.text_input(ch))
    assert tf.text == "hi!"
    ui.handle(slikui.key_press(slikui.Key.Backspace))
    assert tf.text == "hi"
    ui.handle(slikui.key_press(slikui.Key.Enter))
    assert submitted == ["hi"]


def test_scrollarea_wheel():
    ui, root = make_ui(240, 220)
    sc = root.add(slikui.ScrollArea)
    for i in range(12):
        sc.add(slikui.Label, f"row {i}")
    ui.tick(0.0, 1 / 60)
    base = ui.render().copy()

    ui.handle(slikui.scroll(center(sc), (0, -3)))  # wheel down
    ui.tick(1 / 60, 1 / 60)
    assert not np.array_equal(base, ui.render())  # content moved


def test_frame_resize_event():
    ui, root = make_ui(120, 80)
    ui.handle(slikui.frame_resize(90, 60))
    ui.tick(0.0, 1 / 60)
    assert ui.ctx.frame_size.x == 90 and ui.ctx.frame_size.y == 60
