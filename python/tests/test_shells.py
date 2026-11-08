"""Phase D shells & feedback: dialogs (floating layer, modal trap, drag),
toasts, StatusBar, LogView, SearchBox, GroupBox, FileBrowser, ImageView,
CompareView, cursor shapes. Mirrors tests/test_shells.cpp."""

import tempfile
from pathlib import Path

import numpy as np

import slikui

W, H = 320, 240


def make_ui(w=W, h=H, **kw):
    ui = slikui.App(w, h, **kw)
    return ui, ui.set_root(slikui.Column)


def center(w):
    r = w.bounds
    return (r.x + r.w / 2, r.y + r.h / 2)


def click(ui, pos, button=slikui.MouseButton.MouseLeft):
    ui.handle(slikui.mouse_press(pos, button=button))
    ui.handle(slikui.mouse_release(pos, button=button))


def press(ui, pos, button=slikui.MouseButton.MouseLeft):
    ui.handle(slikui.mouse_press(pos, button=button))


def release(ui, pos, button=slikui.MouseButton.MouseLeft):
    ui.handle(slikui.mouse_release(pos, button=button))


def move(ui, pos):
    ui.handle(slikui.mouse_move(pos))


def type_str(ui, text):
    for ch in text:
        ui.handle(slikui.text_input(ch))


# ---------------------------------------------------------------------------
# Dialog layer
# ---------------------------------------------------------------------------
def test_dialog_open_close_and_layering():
    ui, root = make_ui()
    root.add(slikui.Button, "app")
    ui.tick(0.0, 1 / 60)

    dlg = slikui.Dialog("Tools")
    dlg.content().add(slikui.Label, "Body")
    ref = ui.ctx.open_dialog(dlg)
    ui.tick(1 / 60, 1 / 60)
    assert ui.ctx.dialog_count == 1
    assert ref.bounds.w > 60 and ref.bounds.h > 40
    # Default position is centered.
    cx = ref.bounds.x + ref.bounds.w / 2
    assert abs(cx - W / 2) < 2

    # Escape closes it (dialogs unwind first).
    ui.handle(slikui.key_press(slikui.Key.Escape))
    assert ui.ctx.dialog_count == 0

    # A borrowed dialog can be reopened; the close button closes it too.
    ref2 = ui.ctx.open_dialog(dlg)
    ui.tick(1 / 60, 1 / 60)
    assert ui.ctx.dialog_count == 1 and ref2 is ref or ref2.bounds == ref.bounds
    cb = ref.close_button
    click(ui, center(cb))
    assert ui.ctx.dialog_count == 0


def test_dialog_drag_moves():
    ui, root = make_ui()
    dlg = slikui.Dialog("Tools")
    dlg.content().add(slikui.Label, "Body")
    ref = ui.ctx.open_dialog(dlg)
    ui.tick(1 / 60, 1 / 60)

    x0, y0 = ref.bounds.x, ref.bounds.y
    # Press on the title bar (left side, away from the close button).
    press(ui, (ref.bounds.x + 10, ref.bounds.y + 12))
    move(ui, (ref.bounds.x + 50, ref.bounds.y + 32))
    release(ui, (ref.bounds.x + 50, ref.bounds.y + 32))
    ui.tick(1 / 60, 1 / 60)
    assert ref.bounds.x == x0 + 40
    assert ref.bounds.y == y0 + 20


def test_modal_dialog_traps_presses():
    ui, root = make_ui()
    clicks = []
    btn = root.add(slikui.Button, "app")
    btn.on_click(lambda: clicks.append(1))
    ui.tick(0.0, 1 / 60)

    mb = slikui.MessageBox("Confirm", "Delete item?")
    mb.set_modal(True)
    picked = []
    mb.add_button("Delete", lambda: picked.append(1))
    mb.add_button("Cancel", lambda: picked.append(2))
    ui.ctx.open_dialog(mb)
    ui.tick(1 / 60, 1 / 60)

    # Clicking the app button is trapped by the modal.
    click(ui, center(btn))
    assert clicks == []

    # Pressing a message-box button runs the action and closes.
    del_btn = mb.children[1].children[1].children[0]  # content > buttons row
    click(ui, center(del_btn))
    assert picked == [1]
    assert clicks == []
    assert ui.ctx.dialog_count == 0


# ---------------------------------------------------------------------------
# Toasts
# ---------------------------------------------------------------------------
def test_toast_shows_and_expires():
    ui, root = make_ui()
    root.add(slikui.Label, "app")
    ui.tick(0.5, 1 / 60)

    toast = ui.ctx.push_toast("Screenshot saved", 0.2, icon=slikui.Icon.Camera)
    ui.tick(0.55, 1 / 60)
    assert ui.ctx.popup_count == 1
    b = ui.ctx.popup(0).bounds
    assert b.right() > 300 and b.bottom() > 230  # bottom-right corner
    assert toast is not None

    ui.tick(1.0, 1 / 60)  # 0.5 + 0.2 < 1.0 → expired
    assert ui.ctx.popup_count == 0


# ---------------------------------------------------------------------------
# StatusBar / LogView / SearchBox / GroupBox
# ---------------------------------------------------------------------------
def test_statusbar_transient_message():
    ui, root = make_ui()
    bar = root.add(slikui.StatusBar)
    bar.set_text("Ready")
    bar.add(slikui.ProgressBar, value=0.5)
    ui.tick(0.0, 1 / 60)
    assert bar.bounds.h >= 20

    bar.show_message("Rendering…", 1.0)
    assert bar.text == "Ready"  # getter holds the base text
    ui.tick(2.0, 1 / 60)  # transient window passed → tick restores it
    bar.show_message("Done", 0.1)
    ui.tick(3.0, 1 / 60)
    ui.tick(3.2, 1 / 60)
    assert bar.text == "Ready"


def test_logview_levels_and_tail():
    ui, root = make_ui()
    log = root.add(slikui.LogView)
    for i in range(40):
        level = slikui.LogView.Level.Warning if i % 3 == 0 else slikui.LogView.Level.Info
        log.log(level, f"line {i}")
    ui.tick(0.0, 1 / 60)
    assert log.line_count == 40
    assert log.bounds.h >= 100
    log.clear()
    assert log.line_count == 0


def test_searchbox_typing_and_clear():
    ui, root = make_ui()
    changes = []
    box = root.add(slikui.SearchBox)
    box.set_placeholder("Filter…")
    box.on_change(lambda t: changes.append(str(t)))
    ui.tick(0.0, 1 / 60)

    field = box.field()
    click(ui, center(field))  # focus the field
    type_str(ui, "cube")
    assert box.text == "cube"
    assert len(changes) == 4
    ui.tick(1 / 60, 1 / 60)  # lay out the just-revealed clear button
    clear = box.clear_button
    assert clear.visible
    click(ui, center(clear))
    assert box.text == ""
    assert len(changes) == 5


def test_groupbox_title_and_children():
    ui, root = make_ui()
    box = root.add(slikui.GroupBox, "Light")
    box.add(slikui.Checkbox, "Enabled", True)
    box.add(slikui.Slider, 0.0, 1.0)
    ui.tick(0.0, 1 / 60)
    assert box.title == "Light"
    assert box.bounds.h > 50
    # Title label sits above the added children.
    assert box.children[0].bounds.bottom() <= box.children[1].bounds.y + 1


# ---------------------------------------------------------------------------
# ImageView / CompareView
# ---------------------------------------------------------------------------
def test_imageview_pan_zoom():
    ui, root = make_ui()
    view = root.add(slikui.ImageView)
    view.set_pixels(np.full((32, 32, 4), 200, dtype=np.uint8))
    ui.tick(0.0, 1 / 60)
    ui.render()  # first paint computes the fit zoom
    assert view.zoom > 1  # fit: 32px image in a >=150px viewport

    z0 = view.zoom
    ui.handle(slikui.scroll(center(view), (0, 2)))  # wheel up = zoom in
    assert view.zoom > z0

    px0 = view.pan.x
    c = center(view)
    press(ui, c)
    move(ui, (c[0] + 25, c[1]))
    release(ui, (c[0] + 25, c[1]))
    assert view.pan.x == px0 + 25


def test_compareview_divider_drag():
    ui, root = make_ui()
    view = root.add(slikui.CompareView)
    a = np.zeros((16, 16, 4), dtype=np.uint8)
    b = np.zeros((16, 16, 4), dtype=np.uint8)
    a[..., 0] = 255  # red A
    a[..., 3] = 255
    b[..., 2] = 255  # blue B
    b[..., 3] = 255
    view.set_pixels_a(a)
    view.set_pixels_b(b)
    ui.tick(0.0, 1 / 60)
    assert view.fraction == 0.5

    c = center(view)
    press(ui, c)
    move(ui, (c[0] + 40, c[1]))
    release(ui, (c[0] + 40, c[1]))
    assert view.fraction > 0.6


# ---------------------------------------------------------------------------
# Cursor shapes
# ---------------------------------------------------------------------------
def test_cursor_shape_requests():
    ui, root = make_ui()
    root.set_padding(6)  # dead margin so a corner hit resolves to the root
    field = root.add(slikui.TextField)
    view = root.add(slikui.ImageView)
    view.set_pixels(np.full((8, 8, 4), 128, dtype=np.uint8))
    ui.tick(0.0, 1 / 60)

    cursors = []
    ui.ctx.set_cursor_host(cursors.append)

    move(ui, center(field))
    assert cursors and cursors[-1] == slikui.Cursor.IBeam

    cursors.clear()
    move(ui, center(view))
    assert cursors and cursors[-1] == slikui.Cursor.Grab

    cursors.clear()
    move(ui, (2, 237))  # dead margin → back to arrow
    assert cursors and cursors[-1] == slikui.Cursor.Arrow


# ---------------------------------------------------------------------------
# FileBrowser (real temp directory)
# ---------------------------------------------------------------------------
def test_filebrowser_lists_navigates_picks():
    ui, root = make_ui()
    tmp = Path(tempfile.mkdtemp()) / "slikui_fbtest"
    (tmp / "sub").mkdir(parents=True)
    (tmp / "scene.png").touch()
    (tmp / "mat.png").touch()

    picked = []
    fb = slikui.FileBrowser(slikui.FileBrowser.Mode.Open)
    fb.set_path(str(tmp))
    fb.on_pick(lambda p: picked.append(str(p)))
    ui.ctx.open_dialog(fb)
    ui.tick(1 / 60, 1 / 60)
    assert ui.ctx.dialog_count == 1

    # Directory listing: "..", "sub/", then sorted files.
    listing = fb.list
    click(ui, (listing.bounds.x + 30, listing.bounds.y + 1.5 * 24))
    ui.tick(1 / 60, 1 / 60)  # navigated into "sub" (still open)
    assert ui.ctx.dialog_count == 1

    # Back up via "..", then pick the first file and commit with Open.
    click(ui, (listing.bounds.x + 30, listing.bounds.y + 12))
    ui.tick(1 / 60, 1 / 60)
    click(ui, (listing.bounds.x + 30, listing.bounds.y + 2 * 24 + 12))
    ui.tick(1 / 60, 1 / 60)
    click(ui, center(fb.open_button))
    assert len(picked) == 1
    assert "slikui_fbtest" in picked[0] and picked[0].endswith(".png")
    assert ui.ctx.dialog_count == 0  # commit closes the dialog
