"""Phase B menus & commands: popup anchoring + stack, shortcut table,
Menu/MenuItem, MenuBar, right-click context menus, IconButton/Toolbar/icons.
Mirrors tests/test_menus.cpp."""

import sys

import pytest

import slikui

CTRL = int(slikui.Mod.ModCtrl)
SHIFT = int(slikui.Mod.ModShift)
# The platform shortcut modifier: Cmd on macOS, Ctrl elsewhere.
MOD_SHORTCUT = int(slikui.Mod.ModSuper) if sys.platform == "darwin" else CTRL


def make_ui(w=320, h=240, **kw):
    ui = slikui.App(w, h, **kw)
    return ui, ui.set_root(slikui.Column)


def center(w):
    r = w.bounds
    return (r.x + r.w / 2, r.y + r.h / 2)


def click(ui, pos, button=slikui.MouseButton.MouseLeft):
    ui.handle(slikui.mouse_press(pos, button=button))
    ui.handle(slikui.mouse_release(pos, button=button))


def move(ui, pos):
    ui.handle(slikui.mouse_move(pos))


def popup_at(ui):
    return ui.ctx.popup(ui.ctx.popup_count - 1)


def test_popup_anchor_flip_and_clamp():
    ui, root = make_ui()
    btn = root.add(slikui.Button, "Anchor")
    ui.tick(0.0, 1 / 60)

    # A tall popup anchored Below flips above and stays inside the frame.
    tall = slikui.Panel()
    for i in range(5):
        tall.add(slikui.Label, f"line {i}")
    pop = ui.ctx.push_popup(tall, btn, slikui.PopupDir.Below)
    ui.tick(1 / 60, 1 / 60)
    assert pop.bounds.y + pop.bounds.h <= 240
    assert pop.bounds.bottom() <= btn.bounds.y + 0.5 or pop.bounds.y >= btn.bounds.bottom()

    # Right of a widget near the right edge flips/clamps inside.
    far = slikui.Panel()
    far.add(slikui.Label, "wide popup content here")
    pop2 = ui.ctx.push_popup(far, btn, slikui.PopupDir.Right)
    ui.tick(1 / 60, 1 / 60)
    assert pop2.bounds.x + pop2.bounds.w <= 320
    ui.ctx.close_popups()


def test_popup_stack_and_escape():
    ui, root = make_ui()
    btn = root.add(slikui.Button, "open")
    ui.tick(0.0, 1 / 60)

    sub = slikui.Menu("Export")
    sub.add_item("PNG", lambda: None)
    menu = slikui.Menu("File")
    menu.add_item("New", lambda: None)
    menu.add_submenu("Export As", sub)
    menu.open_for(btn, slikui.PopupDir.Below)
    ui.tick(1 / 60, 1 / 60)

    # Hovering the submenu item opens the child popup above the parent.
    export_item = ui.ctx.popup(0).children[1]
    move(ui, center(export_item))
    ui.tick(1 / 60, 1 / 60)
    assert ui.ctx.popup_count == 2

    # Click on the parent menu's dead area: submenu closes, parent stays.
    parent = ui.ctx.popup(0)
    click(ui, (parent.bounds.x + 2, parent.bounds.y + 2))
    assert ui.ctx.popup_count == 1
    assert ui.ctx.popup(0) is parent

    # Escape unwinds one level at a time.
    ui.handle(slikui.key_press(slikui.Key.Escape))
    assert ui.ctx.popup_count == 0

    # Owned popups: a click outside every popup closes the whole stack.
    ui.ctx.push_popup(slikui.Panel(), slikui.Vec2(10, 10))
    ui.ctx.push_popup(slikui.Panel(), slikui.Vec2(60, 10))
    ui.tick(1 / 60, 1 / 60)
    assert ui.ctx.popup_count == 2
    click(ui, (300, 200))
    assert ui.ctx.popup_count == 0


def test_shortcut_table():
    ui, root = make_ui()
    field = root.add(slikui.TextField)
    ui.tick(0.0, 1 / 60)

    fired = []
    ui.ctx.add_shortcut(slikui.Key.S, CTRL, lambda: fired.append("s"))
    ui.ctx.add_shortcut(slikui.Key.R, CTRL, lambda: fired.append("r"))

    ui.handle(slikui.key_press(slikui.Key.Tab))  # focus the field
    ui.handle(slikui.text_input("h"))
    assert field.text == "h"
    assert fired == []

    ui.handle(slikui.key_press(slikui.Key.S, mods_=CTRL))
    assert fired == ["s"]
    ui.handle(slikui.key_press(slikui.Key.S, mods_=CTRL | SHIFT))
    assert fired == ["s"]  # exact mods only
    ui.handle(slikui.key_press(slikui.Key.R, mods_=CTRL))
    assert fired == ["s", "r"]
    ui.handle(slikui.key_press(slikui.Key.S))  # bare key never fires
    assert fired == ["s", "r"]

    ui.ctx.clear_shortcuts()
    ui.handle(slikui.key_press(slikui.Key.S, mods_=CTRL))
    assert fired == ["s", "r"]


def test_menu_items_and_selection():
    ui, root = make_ui()
    btn = root.add(slikui.Button, "Menu")
    ui.tick(0.0, 1 / 60)

    menu = slikui.Menu("File")
    picked = []
    state = {"on": False}
    menu.add_item("Open", lambda: picked.append(1), hint="Ctrl+O")
    menu.add_item("Disabled thing", lambda: picked.append(99), enabled=False)
    menu.add_separator()
    menu.add_checkable("Grid", lambda: state["on"],
                       lambda: state.update(on=not state["on"]))
    menu.add_item("Quit", lambda: picked.append(2), hint="Ctrl+Q")

    menu.open_for(btn, slikui.PopupDir.Below)
    ui.tick(1 / 60, 1 / 60)
    assert ui.ctx.popup_count == 1
    pop = ui.ctx.popup(0)
    assert pop.bounds.w >= 140  # min width

    click(ui, center(pop.children[0]))  # "Open" fires and closes
    assert picked == [1]
    assert ui.ctx.popup_count == 0

    menu.open_for(btn, slikui.PopupDir.Below)
    ui.tick(1 / 60, 1 / 60)
    pop = ui.ctx.popup(0)
    click(ui, center(pop.children[1]))  # disabled: swallowed
    assert picked == [1]
    assert ui.ctx.popup_count == 1

    click(ui, center(pop.children[3]))  # checkable toggles and closes
    assert state["on"]
    assert ui.ctx.popup_count == 0

    # on_open hook runs before each open.
    opens = []
    menu.on_open(lambda: opens.append(1))
    menu.open_for(btn, slikui.PopupDir.Below)
    ui.tick(1 / 60, 1 / 60)
    assert opens == [1]
    ui.ctx.close_popups()


def test_menu_submenu():
    ui, root = make_ui()
    btn = root.add(slikui.Button, "Menu")
    ui.tick(0.0, 1 / 60)

    sub = slikui.Menu("Export")
    got = []
    sub.add_item("PNG", lambda: got.append("png"))
    sub.add_item("JPEG", lambda: got.append("jpg"))
    menu = slikui.Menu("File")
    menu.add_item("New", lambda: None)
    menu.add_submenu("Export As", sub)
    menu.add_item("Close", lambda: None)

    menu.open_for(btn, slikui.PopupDir.Below)
    ui.tick(1 / 60, 1 / 60)

    export_item = ui.ctx.popup(0).children[1]
    move(ui, center(export_item))
    ui.tick(1 / 60, 1 / 60)
    assert ui.ctx.popup_count == 2
    child = ui.ctx.popup(1)
    assert child.bounds.x >= export_item.bounds.right() - 1  # opened right

    # Moving to a different item closes the submenu.
    new_item = ui.ctx.popup(0).children[0]
    move(ui, center(new_item))
    ui.tick(1 / 60, 1 / 60)
    assert ui.ctx.popup_count == 1

    # Reopen, click a submenu entry: fires and closes everything.
    move(ui, center(export_item))
    ui.tick(1 / 60, 1 / 60)
    assert ui.ctx.popup_count == 2
    click(ui, center(ui.ctx.popup(1).children[0]))
    assert got == ["png"]
    assert ui.ctx.popup_count == 0

    # Escape unwinds one level per press.
    menu.open_for(btn, slikui.PopupDir.Below)
    ui.tick(1 / 60, 1 / 60)
    move(ui, center(export_item))
    ui.tick(1 / 60, 1 / 60)
    assert ui.ctx.popup_count == 2
    ui.handle(slikui.key_press(slikui.Key.Escape))
    assert ui.ctx.popup_count == 1
    ui.handle(slikui.key_press(slikui.Key.Escape))
    assert ui.ctx.popup_count == 0


def test_menubar_open_switch_select():
    ui, root = make_ui()
    bar = root.add(slikui.MenuBar)
    picked = []
    file = bar.add_menu("File")
    file.add_item("Open", lambda: picked.append("open"), hint="Ctrl+O")
    file.add_item("Save", lambda: picked.append("save"),
                  hint=slikui.shortcut_label(slikui.Key.S, CTRL))
    edit = bar.add_menu("Edit")
    edit.add_item("Undo", lambda: picked.append("undo"), hint="Ctrl+Z")
    ui.tick(0.0, 1 / 60)
    assert bar.bounds.h >= 26

    file_btn, edit_btn = bar.children[0], bar.children[1]
    click(ui, center(file_btn))
    ui.tick(1 / 60, 1 / 60)
    assert ui.ctx.popup_count == 1
    menu = ui.ctx.popup(0)
    assert menu.bounds.y >= file_btn.bounds.bottom()  # opened below

    # Hover-while-open switches menus.
    move(ui, center(edit_btn))
    ui.tick(1 / 60, 1 / 60)
    assert ui.ctx.popup_count == 1
    assert ui.ctx.popup(0) is not menu

    click(ui, center(ui.ctx.popup(0).children[0]))  # Undo
    assert picked == ["undo"]
    assert ui.ctx.popup_count == 0

    # Clicking the open title again closes it.
    click(ui, center(file_btn))
    ui.tick(1 / 60, 1 / 60)
    assert ui.ctx.popup_count == 1
    click(ui, center(file_btn))
    ui.tick(1 / 60, 1 / 60)
    assert ui.ctx.popup_count == 0


def test_context_menu_right_click():
    ui, root = make_ui()
    menu = slikui.Menu()
    picked = []
    menu.add_item("Duplicate", lambda: picked.append(1))
    menu.add_item("Delete", lambda: picked.append(2))

    panel = root.add(slikui.Panel, padding=20)
    panel.add(slikui.Label, "right-click me")
    panel.set_context_menu(menu)
    ui.tick(0.0, 1 / 60)

    at = center(panel)
    click(ui, at, button=slikui.MouseButton.MouseRight)
    ui.tick(1 / 60, 1 / 60)
    assert ui.ctx.popup_count == 1
    r = ui.ctx.popup(0).bounds
    assert r.x <= at[0] < r.x + r.w and r.y <= at[1] < r.y + r.h

    # A child inherits the ancestor's menu.
    lab = panel.children[0]
    click(ui, center(lab), button=slikui.MouseButton.MouseRight)
    ui.tick(1 / 60, 1 / 60)
    assert ui.ctx.popup_count == 1
    ui.ctx.close_popups()

    # Right-click outside the panel: nothing opens.
    click(ui, (10, 200), button=slikui.MouseButton.MouseRight)
    ui.tick(1 / 60, 1 / 60)
    assert ui.ctx.popup_count == 0

    # Selecting an entry closes the menu.
    click(ui, at, button=slikui.MouseButton.MouseRight)
    ui.tick(1 / 60, 1 / 60)
    click(ui, center(ui.ctx.popup(0).children[0]))
    assert picked == [1]
    assert ui.ctx.popup_count == 0


def test_icon_button_and_toolbar():
    ui, root = make_ui(320, 240)
    clicks = []
    bar = root.add(slikui.Toolbar)
    play = bar.add_button(slikui.Icon.Play, lambda: clicks.append("play"), tip="Run")
    bar.add_separator()
    bar.add_button(slikui.Icon.Grid, lambda: clicks.append("grid"))
    bar.add_labeled_button(slikui.Icon.Save, "Save", lambda: clicks.append("save"))
    ui.tick(0.0, 1 / 60)
    assert bar.bounds.h >= 30

    assert play.variant == slikui.ButtonVariant.Ghost
    dividers = sum(1 for c in bar.children if isinstance(c, slikui.Divider))
    assert dividers == 1
    assert bar.children[3].bounds.h > play.bounds.h  # labeled cell is taller

    click(ui, center(play))
    assert clicks == ["play"]

    # Icons actually paint: the button interior is not flat.
    frame = ui.render()
    r = play.bounds
    cx, cy = int(r.x + r.w / 2), int(r.y + r.h / 2)
    patch = {frame[cy + dy, cx + dx] for dy in range(-8, 9) for dx in range(-8, 9)}
    assert len(patch) > 2  # strokes vs fill vs background


def test_shortcut_label_format():
    if sys.platform == "darwin":
        assert slikui.shortcut_label(slikui.Key.S, MOD_SHORTCUT) == "⌘S"
        assert slikui.shortcut_label(slikui.Key.S, MOD_SHORTCUT | SHIFT) == "⌘⇧S"
    else:
        assert slikui.shortcut_label(slikui.Key.S, MOD_SHORTCUT) == "Ctrl+S"
        assert slikui.shortcut_label(slikui.Key.S, MOD_SHORTCUT | SHIFT) == "Ctrl+Shift+S"
    assert slikui.shortcut_label(slikui.Key.F5, 0) == "F5"
