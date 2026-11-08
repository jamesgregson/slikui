"""Full-surface gallery rendered headless via the Python bindings.

Writes gallery.png next to this file.
"""

import os

import numpy as np

import slikui
from slikui._py.pixels import save_image


def main():
    ui = slikui.App(320, 1340, scale=2)
    root = ui.set_root(slikui.Column, padding=14, gap=8)

    # header + menu bar + toolbar (Phase B)
    root.add(slikui.Label, "slikui — widget gallery", font_size=22)
    root.add(slikui.Label, "Python bindings · software backend").set_color(
        slikui.Color.from_hex(0x8A94A0)
    )
    bar = root.add(slikui.MenuBar)
    file = bar.add_menu("File")
    file.add_item("Open…", lambda: None, hint="Ctrl+O")
    file.add_item("Save", lambda: None, hint="Ctrl+S")
    edit = bar.add_menu("Edit")
    edit.add_item("Undo", lambda: None, hint="Ctrl+Z")
    tb = root.add(slikui.Toolbar)
    tb.add_button(slikui.Icon.Play, lambda: None, tip="Run")
    tb.add_button(slikui.Icon.Pause, lambda: None)
    tb.add_separator()
    tb.add_button(slikui.Icon.Grid, lambda: None, tip="Toggle grid")
    tb.add_button(slikui.Icon.Camera, lambda: None, tip="Screenshot")

    # buttons
    row = root.add(slikui.Row, gap=8)
    row.add(slikui.Button, "Default")
    row.add(slikui.Button, "Primary", variant=slikui.ButtonVariant.Primary)
    row.add(slikui.Button, "Danger", variant=slikui.ButtonVariant.Danger)
    row.add(slikui.Button, "Ghost", variant=slikui.ButtonVariant.Ghost,
            tooltip="Borderless quiet action")

    # toggles
    row = root.add(slikui.Row, gap=16)
    row.add(slikui.Checkbox, "Sync", checked=True)
    row.add(slikui.Radio, "Wi-Fi", checked=True)
    row.add(slikui.Radio, "Ethernet")
    row.add(slikui.Switch, checked=True)

    # sliders / progress / spinner
    root.add(slikui.Slider, 0, 100, value=62)
    root.add(slikui.ProgressBar, value=0.72)
    sp_row = root.add(slikui.Row, gap=8)
    sp_row.add(slikui.Label, "Working…").set_color(slikui.Color.from_hex(0x8A94A0))
    sp_row.add(slikui.Spacer)
    sp_row.add(slikui.Spinner, speed=240)

    # inputs
    root.add(slikui.TextField, text="jane@example.com", placeholder="you@host")
    root.add(slikui.TextArea, rows=2, text="CPU-rendered UI —\nno GPU, no window.")

    # editors: DragValue scrub fields, a Vec3Edit bound to a numpy buffer,
    # ColorEdit chips (click → popup), Segmented, and a labeled Slider.
    material_buf = np.array([0.75, 0.42, 0.31], dtype=np.float32)
    root.add(slikui.Segmented, items=["Cube", "Sphere", "Torus"])
    root.add(slikui.Vec3Edit, material_buf, labels="RGB")
    root.add(slikui.DragValue, 1.5, prefix="exp ", suffix=" EV")
    colors = root.add(slikui.Row, gap=8)
    colors.add(slikui.ColorEdit, slikui.Color.from_hex(0xBF5C38))
    colors.add(slikui.ColorEdit, slikui.Color.from_hex(0x3F6FBF))
    colors.add(slikui.ColorEdit, slikui.Color.from_hex(0x46A758))
    root.add(slikui.Slider, 0, 1, value=0.35, label="Roughness", format="%.2f")

    # views + data display (Phase C)
    data_row = root.add(slikui.Row, gap=10)
    pc = data_row.add(slikui.Column, gap=8)
    plot = pc.add(slikui.Plot)
    xs = np.arange(64, dtype=np.float32)
    plot.set_series((np.sin(xs / 7.0) * np.exp(-xs / 40.0)).astype(np.float32),
                    slikui.Color.from_hex(0x4C8DFF))
    tree = data_row.add(slikui.TreeView)
    tree.set_rows([
        slikui.TreeView.Row("Scene", 0, True),
        slikui.TreeView.Row("Dragon", 1),
        slikui.TreeView.Row("Camera", 1),
    ])
    root.add(slikui.ListView, items=["alpha", "beta", "gamma", "delta"], selected=1)
    root.add(slikui.Tabs, tabs=["Home", "Edits", "View"], selected=1)
    root.add(slikui.Dropdown, items=["Linear", "Nearest", "Cubic", "Lanczos"],
             placeholder="Filter…")

    # shells (Phase D): dialog layer, toast, group box, search, log view
    shells_row = root.add(slikui.Row, gap=10)
    sleft = shells_row.add(slikui.Column, gap=8)
    gb = sleft.add(slikui.GroupBox, "Light")
    gb.add(slikui.Checkbox, "Enabled", checked=True)
    gb.add(slikui.Slider, 0, 1, value=0.35)
    sleft.add(slikui.SearchBox, placeholder="Search assets…")
    shells_log = shells_row.add(slikui.LogView)
    shells_log.log(slikui.LogView.Level.Info, "loaded scene.dragon (24 ms)")
    shells_log.log(slikui.LogView.Level.Warning, "missing texture: bark_d")
    shells_log.log(slikui.LogView.Level.Error, "shader compile failed")
    mb = slikui.MessageBox("Export", "Render 128 frames at 4K?")
    mb.add_button("Export", lambda: None)
    mb.add_button("Cancel", lambda: None)
    ui.ctx.open_dialog(mb)
    ui.ctx.push_toast("Screenshot saved", 60.0)

    # accordion (Expander groups)
    acc = root.add(slikui.Accordion, gap=4)
    shape = acc.add_section("Shape", open=True)
    shape.set_gap(4)
    shape.add(slikui.Label, "Cube / Sphere / Torus")
    material = acc.add_section("Material")
    material.add(slikui.Slider, 0, 1, value=0.35)
    acc.add_section("Environment")

    # panel + image
    panel = root.add(slikui.Panel, padding=10, gap=6, radius=10)
    panel.add(slikui.Label, "Panel — a bordered container")
    prow = panel.add(slikui.Row, gap=12)
    prow.add(slikui.Checkbox, "Snap", checked=True)
    prow.add(slikui.Checkbox, "Grid")

    grad = np.zeros((28, 180, 4), dtype=np.uint8)
    ramp = np.linspace(40, 220, 180).astype(np.uint8)[None, :]
    grad[..., 0] = ramp  # red ramps left→right
    grad[..., 2] = ramp[::-1]
    grad[..., 3] = 255
    panel.add(slikui.Image, "gradient").set_pixels(grad).set_size(180, 28).set_fit(
        slikui.ImageFit.Stretch
    )

    ui.tick(0.0, 1 / 60)
    ui.render()  # frame 1: glyph atlas uploads what measure() cached
    for i in range(1, 4):
        ui.tick(i / 60, 1 / 60)
    frame = ui.render()  # frame 4: atlas settled — every glyph present

    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "gallery.png")
    save_image(frame, out)
    print(f"wrote {out} ({frame.shape[1]}x{frame.shape[0]})")


if __name__ == "__main__":
    main()
