#!/usr/bin/env python3
"""slikui showcase (Python) — the twin of examples/05_showcase/main.cpp.

Every widget, live, in one window: each component prints its state changes
into the on-screen LogView as you use it. Same layout, same pages, same log
strings as the C++ build — the --smoke transcripts should match one-for-one.

    MenuBar ─ Toolbar ─ [ log panel | tabbed pages ] ─ StatusBar

Pages: Controls, Inputs, Data, Containers, Media, Custom — Tabs carry only
labels, so each page is a Panel whose visibility the Tabs callback toggles.

Two custom widgets exercise the measure/paint/on_event/tick contract:
  Fill       — expands on the main axis and delegates to its one child (the
               trick that lets the splitter fill the window).
  LevelMeter — LED bar meter: click/drag to scrub, peak-hold marker,
               green→amber→red gradient, warns above 90%.

Modes:
  python examples/showcase.py             interactive (GLFW window; the CPU-
                                          rasterized UI is blitted as one
                                          texture — deps: glfw, PyOpenGL)
  python examples/showcase.py --smoke [out.png]
                                          headless scripted pass over every
                                          component; prints the log
                                          transcript and a screenshot.
"""

import math
import sys

import numpy as np

import slikui
from slikui._py.pixels import save_image

TAB_NAMES = ["Controls", "Inputs", "Data", "Containers", "Media", "Custom"]

INFO = slikui.LogView.Level.Info
OK = slikui.LogView.Level.Success
WARN = slikui.LogView.Level.Warning
ERR = slikui.LogView.Level.Error


# ---------------------------------------------------------------------------
# Custom widget 1: Fill — expands on the main axis, delegates to its child.
# paint/hit-testing/tick recurse through children, so the wrapped subtree
# stays fully live without extra code.
# ---------------------------------------------------------------------------
class Fill(slikui.Widget):
    def expands_main(self):
        return True

    def measure(self, constraints):
        if not self.children:
            return constraints.clamp(slikui.Size(0, 0))
        return self.children[0].measure(constraints)

    def arrange(self, area):
        self.bounds = area
        if self.children:
            self.children[0].arrange(area)


# ---------------------------------------------------------------------------
# Custom widget 2: LevelMeter — LED bar with click/drag scrubbing, a peak
# marker, and a gradient from success through warning to danger.
# ---------------------------------------------------------------------------
class LevelMeter(slikui.Widget):
    def __init__(self, value=0.4):
        super().__init__()
        self.value = min(max(value, 0.0), 1.0)
        self.peak = self.value
        self.on_change = None
        self.dragging = False

    def set_value(self, v):
        self.value = min(max(v, 0.0), 1.0)
        self.peak = max(self.peak, self.value)

    def measure(self, constraints):
        return constraints.clamp(slikui.Size(240.0, 26.0))

    def stretch_cross(self):
        return False

    def tick(self, t):
        # Peak holds while rising, decays ~0.25/s otherwise.
        if self.value >= self.peak:
            self.peak = self.value
        else:
            self.peak = max(self.value, self.peak - 0.25 * t.dt)

    def paint(self, env):
        th = env.theme
        seg, gap = 16, 2.0
        bw = (self.bounds.w - gap * (seg - 1)) / seg
        for i in range(seg):
            t = i / (seg - 1)
            b = slikui.Rect(self.bounds.x + i * (bw + gap), self.bounds.y, bw,
                            self.bounds.h)
            lit = self.value >= (i + 0.5) / seg
            color = th.success.lerp(th.warning, min(max(t * 2.0, 0.0), 1.0))
            color = color.lerp(th.danger, min(max(t * 2.0 - 1.0, 0.0), 1.0))
            env.list.rect(b, color if lit else th.surface_alt, 2.0)
        if self.peak > 0.01:  # peak-hold marker
            px = self.bounds.x + min(max(self.peak, 0.0), 1.0) * (self.bounds.w - 2.0)
            env.list.rect(slikui.Rect(px, self.bounds.y, 2.0, self.bounds.h), th.text, 0.0)

    def on_event(self, event):
        left = int(slikui.MouseButton.MouseLeft)
        if (event.type == slikui.EventType.MousePress and event.button == left
                and self.bounds.contains(event.pos)):
            self.dragging = True  # no mouse capture in the bindings yet: the
            self._apply(event.pos.x)  # drag ends when the pointer leaves
            return True
        if event.type == slikui.EventType.MouseMove and self.dragging:
            self._apply(event.pos.x)
            return True
        if event.type == slikui.EventType.MouseRelease and self.dragging:
            self.dragging = False
            if self.on_change:
                self.on_change(self.value)
            return True
        return False

    def _apply(self, x):
        self.value = min(max((x - self.bounds.x) / max(self.bounds.w, 1.0), 0.0), 1.0)
        self.peak = max(self.peak, self.value)
        if self.on_change:
            self.on_change(self.value)


# ---------------------------------------------------------------------------
# Demo plumbing: every state change flows through say() — into the LogView
# and into a transcript the smoke mode prints.
# ---------------------------------------------------------------------------
class Demo:
    def __init__(self):
        self.log = None
        self.status = None
        self.count = None
        self.transcript = []
        self.events = 0

    def say(self, msg, level=INFO):
        if self.log:
            self.log.log(level, msg)
        tag = {OK: "ok  ", WARN: "warn", ERR: "err "}.get(level, "info")
        self.transcript.append(f"[{tag}] {msg}")
        self.events += 1
        if self.count:
            self.count.set_text(f"{self.events} events")


class UI:
    def __init__(self):
        self.demo = Demo()
        self.tabs = None
        self.pages = []
        self.btn_ping = self.btn_danger = None
        self.checkbox = None
        self.radios = []
        self.sw = None
        self.slider = self.segmented = self.progress = None
        self.field = self.search = self.dropdown = self.dv = None
        self.vec_vals = np.array([1.0, 2.0], dtype=np.float32)
        self.color = None
        self.list = self.tree = self.table = self.pad = None
        self.prop_opacity = None
        self.acc_geo = self.acc_sh = self.expander = None
        self.vsplit = self.scroll = None
        self.image_view = self.compare = None
        self.meter = None
        self.meter_set_buttons = []
        self.file_dialog = None
        self.playing = True
        self.plot_phase = 0.0
        self.plot_next = 0.0
        self.plot_logged = 0
        self.samples = np.full(24, 0.35, dtype=np.float32)
        self.open_about = None
        self.open_file_dialog = None
        self.save = None


def apply_page(ui, i):
    for k, page in enumerate(ui.pages):
        page.visible = (k == i)


def build_ui(app, ui):
    demo = ui.demo
    ctx = app.ctx

    root = app.set_root(slikui.Column, padding=0, gap=0)

    # ----- menu bar -------------------------------------------------------
    bar = root.add(slikui.MenuBar)
    mfile = bar.add_menu("File")
    mfile.add_item("New", lambda: demo.say("menu File → New"), hint="Ctrl+N")
    mfile.add_item("Open…", lambda: ui.open_file_dialog and ui.open_file_dialog(),
                   hint="Ctrl+O")
    mfile.add_item("Save", lambda: ui.save and ui.save(), hint="Ctrl+S")
    mfile.add_separator()
    mfile.add_item("Exit", lambda: demo.say("menu File → Exit (close the window)"))
    mview = bar.add_menu("View")
    appearance = mview.add_submenu("Appearance", slikui.Menu("Appearance"))
    for i, preset in enumerate(slikui.theme_presets()):
        name = preset.name
        appearance.add_item(name, lambda i=i, name=name: (
            slikui.apply_preset(ctx.theme, i), demo.say(f"theme → '{name}'")))
    mview.add_separator()
    mview.add_checkable("Play samples", lambda: ui.playing, lambda: (
        setattr(ui, "playing", not ui.playing),
        demo.say("menu View → play samples " + ("on" if ui.playing else "off"))))
    mhelp = bar.add_menu("Help")
    mhelp.add_item("About", lambda: ui.open_about and ui.open_about())

    # ----- toolbar --------------------------------------------------------
    tb = root.add(slikui.Toolbar)
    tb.add_button(slikui.Icon.Play, lambda: (
        setattr(ui, "playing", True), demo.say("toolbar → play")),
        tip="Play the live plot + progress")
    tb.add_button(slikui.Icon.Pause, lambda: (
        setattr(ui, "playing", False), demo.say("toolbar → pause")),
        tip="Pause the live plot + progress")
    tb.add_button(slikui.Icon.Stop, lambda: (
        setattr(ui, "playing", False),
        ui.samples.fill(0.35), demo.say("toolbar → stop (plot reset)")),
        tip="Stop and reset the plot")
    tb.add_separator()
    tb.add_button(slikui.Icon.Folder,
                  lambda: ui.open_file_dialog and ui.open_file_dialog(),
                  tip="Open a file (real std::filesystem browser)")
    tb.add_button(slikui.Icon.Save, lambda: ui.save and ui.save(),
                  tip="Save (toast + status)")
    tb.add_button(slikui.Icon.Camera, lambda: (
        ctx.push_toast("Screenshot saved to /tmp", 3.0, icon=slikui.Icon.Camera),
        demo.say("toast → 'Screenshot saved to /tmp'")), tip="Push a toast")
    tb.add_button(slikui.Icon.Eye, lambda: ui.open_about and ui.open_about(),
                  tip="About")
    tb.add_button(slikui.Icon.Trash, lambda: (
        ui.demo.log and ui.demo.log.clear(), demo.say("log cleared")),
        tip="Clear the event log")
    tb.add(slikui.Spacer)
    tb.add(slikui.Label, "everything you touch logs to the panel on the left") \
        .set_color(slikui.Color.from_hex(0x8A94A0)).set_font_size(13)

    # ----- main area: log | tabbed pages ----------------------------------
    split = root.add(slikui.Splitter, False).set_fraction(0.34)

    log_panel = split.add(slikui.Panel)
    log_panel.set_padding(10).set_gap(6)
    log_head = log_panel.add(slikui.Row).set_gap(8)
    log_head.add(slikui.Label, "event log").set_font_size(15)
    log_head.add(slikui.Spacer)
    log_head.add(slikui.Label, "live — every component reports here") \
        .set_color(slikui.Color.from_hex(0x8A94A0)).set_font_size(12)
    demo.log = log_panel.add(slikui.LogView)

    right = split.add(slikui.Column)
    right.set_padding(10).set_gap(10)
    tabs = right.add(slikui.Tabs)
    tabs.set_tabs(TAB_NAMES)
    ui.tabs = tabs
    page_host = right.add(Fill).add(slikui.Column)
    page_host.set_gap(10)

    # --- page 1: Controls -------------------------------------------------
    p1 = page_host.add(slikui.Panel)
    p1.set_padding(12).set_gap(10)
    g_btn = p1.add(slikui.GroupBox, "buttons")
    btn_row = g_btn.add(slikui.Row).set_gap(8)
    ui.btn_ping = btn_row.add(slikui.Button, "Ping").on_click(
        lambda: demo.say("button 'Ping' clicked"))
    btn_row.add(slikui.Button, "Primary").set_variant(slikui.ButtonVariant.Primary) \
        .on_click(lambda: demo.say("button 'Primary' clicked"))
    ui.btn_danger = btn_row.add(slikui.Button, "Cause error").on_click(
        lambda: demo.say("button 'Cause error' clicked — boom", ERR))
    btn_row.add(slikui.Button, "Ghost").set_variant(slikui.ButtonVariant.Ghost) \
        .on_click(lambda: demo.say("button 'Ghost' clicked"))
    g_sel = p1.add(slikui.GroupBox, "toggles")
    sel_row = g_sel.add(slikui.Row).set_gap(16)
    ui.checkbox = sel_row.add(slikui.Checkbox, "autosave", True).on_change(
        lambda v: demo.say("checkbox autosave → " + ("on" if v else "off")))
    for i in range(3):
        r = sel_row.add(slikui.Radio, f"pick {i + 1}", i == 0)
        ui.radios.append(r)

        def make_radio_change(i=i):
            def on_change(on):
                if not on:
                    return
                for k, r2 in enumerate(ui.radios):
                    r2.set_checked(k == i)
                demo.say(f"radio → pick {i + 1}")
            return on_change
        r.on_change(make_radio_change())
    ui.sw = sel_row.add(slikui.Switch).on_change(
        lambda v: demo.say("switch → " + ("on" if v else "off")))
    g_rng = p1.add(slikui.GroupBox, "ranges")
    rng_row = g_rng.add(slikui.Row).set_gap(16)
    ui.slider = rng_row.add(slikui.Slider, 0.0, 1.0) \
        .set_label("gain").set_format("%.2f").set_value(0.35)
    ui.slider.on_change(lambda v: demo.say("slider gain → %.2f" % v))
    ui.progress = rng_row.add(slikui.ProgressBar)
    rng_row2 = g_rng.add(slikui.Row).set_gap(16)
    rng_row2.add(slikui.Spinner).set_speed(2.5)
    ui.segmented = rng_row2.add(slikui.Segmented)
    ui.segmented.set_items(["line", "tree", "grid"]).set_selected(0)
    ui.segmented.on_select(lambda i: demo.say("segmented → "
                                              + ["line", "tree", "grid"][i]))
    ui.pages.append(p1)

    # --- page 2: Inputs ---------------------------------------------------
    p2 = page_host.add(slikui.Panel)
    p2.set_padding(12).set_gap(10)
    g_text = p2.add(slikui.GroupBox, "text")
    ui.field = g_text.add(slikui.TextField).set_placeholder("type here, Enter submits")
    ui.field.on_change(lambda s: demo.say(f"text → '{s}'"))
    ui.field.on_submit(lambda s: demo.say(f"text submit → '{s}'", OK))
    g_text.add(slikui.TextArea).set_placeholder("multiline…").set_rows(3).on_change(
        lambda s: demo.say(f"area → {len(s)} chars"))
    ui.search = g_text.add(slikui.SearchBox).set_placeholder("search…")
    ui.search.on_change(lambda s: demo.say(f"search → '{s}'"))
    ui.search.on_submit(lambda s: demo.say(f"search submit → '{s}'"))
    g_val = p2.add(slikui.GroupBox, "values")
    val_row = g_val.add(slikui.Row).set_gap(14)
    ui.dropdown = val_row.add(slikui.Dropdown)
    ui.dropdown.set_items(["Nearest", "Linear", "Cubic"]).set_placeholder("filter…")
    ui.dropdown.on_select(lambda i: demo.say(
        "dropdown → " + ["Nearest", "Linear", "Cubic"][i]))
    ui.dv = val_row.add(slikui.DragValue, 1.0) \
        .set_range(0.0, 100.0).set_speed(0.6).set_suffix(" fps")
    ui.dv.on_change(lambda v: demo.say("dragvalue → %.1f" % v))
    vec2 = val_row.add_vec2edit(ui.vec_vals)
    vec2.set_range(-10.0, 10.0)
    vec2.on_change(lambda: demo.say(
        "vec2 → (%.1f, %.1f)" % (ui.vec_vals[0], ui.vec_vals[1])))
    g_col = p2.add(slikui.GroupBox, "color")
    col_row = g_col.add(slikui.Row).set_gap(14)
    ui.color = col_row.add(slikui.ColorEdit)
    ui.color.set_color(slikui.Color.from_hex(0x4C8DFF))
    ui.color.on_change(lambda c: demo.say("color → #%02X%02X%02X" % (c.r, c.g, c.b)))
    col_row.add(slikui.Label, "click the swatch for the picker popup") \
        .set_color(slikui.Color.from_hex(0x8A94A0)).set_font_size(12)
    ui.pages.append(p2)

    # --- page 3: Data -----------------------------------------------------
    p3 = page_host.add(slikui.Panel)
    p3.set_padding(12).set_gap(10)
    plot = p3.add(slikui.Plot)
    plot.set_fill(True)
    plot.set_series(ui.samples, slikui.Color.from_hex(0x4C8DFF))
    plot.set_tooltip("live series — press play in the toolbar")
    data_row = p3.add(slikui.Row).set_gap(10)
    ui.tree = data_row.add(slikui.TreeView)
    ui.tree.set_rows([
        slikui.TreeView.Row("slikui/", 0, True),
        slikui.TreeView.Row("src/", 1, True),
        slikui.TreeView.Row("main.cpp", 2, False),
        slikui.TreeView.Row("widgets/", 1, True),
        slikui.TreeView.Row("controls.hpp", 2, False),
        slikui.TreeView.Row("shells.hpp", 2, False),
        slikui.TreeView.Row("docs/", 1, True),
        slikui.TreeView.Row("widgets-roadmap.md", 2, False),
    ])
    ui.tree.set_expanded(0, True).set_expanded(1, True).set_expanded(3, True)
    tree_names = ["slikui/", "src/", "main.cpp", "widgets/", "controls.hpp",
                  "shells.hpp", "docs/", "widgets-roadmap.md"]
    ui.tree.on_select(lambda i: demo.say("tree → " + tree_names[i]))
    ui.table = data_row.add(slikui.Table)
    ui.table.set_columns([
        slikui.Table.ColumnSpec("file", 130.0),
        slikui.Table.ColumnSpec("checks", 70.0, True),
        slikui.Table.ColumnSpec("status", 80.0),
    ])
    ui.table.set_rows([
        ["controls.hpp", "488", "ok"],
        ["shells.hpp", "73", "ok"],
        ["views.hpp", "31", "ok"],
        ["menus.hpp", "18", "warn"],
    ])
    ui.table.on_select(lambda i: demo.say(
        "table → row %d (%s)" % (i, ["controls.hpp", "shells.hpp", "views.hpp",
                                     "menus.hpp"][i])))
    data_row2 = p3.add(slikui.Row).set_gap(10)
    ui.list = data_row2.add(slikui.ListView)
    ui.list.set_items(["alpha", "beta", "gamma", "delta"])
    ui.list.on_select(lambda i: demo.say(
        "list → " + ["alpha", "beta", "gamma", "delta"][i]))
    grid = data_row2.add(slikui.PropertyGrid)
    ui.prop_opacity = grid.add_property("opacity").add(slikui.Slider, 0.0, 1.0)
    ui.prop_opacity.set_value(0.75)
    ui.prop_opacity.on_change(lambda v: demo.say("property opacity → %.2f" % v))
    adv = grid.add_section("advanced", True)
    dv2 = adv.add_property("threads").add(slikui.DragValue, 4.0)
    dv2.set_range(1.0, 64.0).set_int_mode(True)
    dv2.on_change(lambda v: demo.say("property threads → %d" % int(v)))
    pad_row = p3.add(slikui.Row).set_gap(14)
    ui.pad = pad_row.add(slikui.Vec2Pad)
    ui.pad.set_range(slikui.Vec2(0, 0), slikui.Vec2(1, 1))
    ui.pad.set_value(slikui.Vec2(0.3, 0.6))
    ui.pad.on_change(lambda v: demo.say("pad → (%.2f, %.2f)" % (v.x, v.y)))
    pad_row.add(slikui.Label, "Vec2Pad — drag the dot") \
        .set_color(slikui.Color.from_hex(0x8A94A0)).set_font_size(12)
    ui.pages.append(p3)

    # --- page 4: Containers ----------------------------------------------
    p4 = page_host.add(slikui.Panel)
    p4.set_padding(12).set_gap(10)
    c_row = p4.add(slikui.Row).set_gap(14)
    c_left = c_row.add(slikui.Column).set_gap(8)
    accordion = c_left.add(slikui.Accordion).set_exclusive(True)
    sec_geo = accordion.add_section("geometry", True)
    sec_geo.add(slikui.Label, "vertex buffer contents…")
    sec_sh = accordion.add_section("shaders")
    sec_sh.add(slikui.Label, "glsl sources…")
    ui.acc_geo = sec_geo
    ui.acc_sh = sec_sh
    sec_geo.on_toggle(lambda opening: demo.say(
        "accordion → 'geometry' " + ("opened" if opening else "closed")))
    sec_sh.on_toggle(lambda opening: demo.say(
        "accordion → 'shaders' " + ("opened" if opening else "closed")))
    ui.expander = c_left.add(slikui.Expander, "more options")
    ui.expander.add(slikui.Checkbox, "nested checkbox", False)
    ui.expander.on_toggle(lambda opening: demo.say(
        "expander → " + ("opened" if opening else "closed")))
    demo_panel = c_left.add(slikui.Panel)
    demo_panel.set_background(slikui.Color.from_hex(0x223344)).set_radius(8)
    demo_panel.add(slikui.Label, "a Panel inside the flow")
    c_right = c_row.add(slikui.Column).set_gap(8)
    ui.vsplit = c_right.add(slikui.Splitter, True).set_fraction(0.5)
    upper = ui.vsplit.add(slikui.Column).set_gap(2)
    upper.add(slikui.Label, "upper pane").set_color(slikui.Color.from_hex(0x77C7FF))
    upper.add(slikui.Label, "drag the divider").set_color(slikui.Color.from_hex(0x44515F))
    upper.add(slikui.Label, "…it logs here").set_color(slikui.Color.from_hex(0x44515F))
    lower = ui.vsplit.add(slikui.Column).set_gap(2)
    lower.add(slikui.Label, "lower pane").set_color(slikui.Color.from_hex(0xFFD377))
    lower.add(slikui.Label, "both panes stay live").set_color(slikui.Color.from_hex(0x44515F))
    lower.add(slikui.Label, "fraction → log").set_color(slikui.Color.from_hex(0x44515F))
    ui.vsplit.on_change(lambda f: demo.say("splitter → %.2f" % f))
    ui.scroll = c_right.add(slikui.ScrollArea)
    ui.scroll.hint_w = 240.0
    ui.scroll.hint_h = 110.0
    for i in range(10):
        ui.scroll.add(slikui.Label, f"scrollable row {i}")
    ui.pages.append(p4)

    # --- page 5: Media ----------------------------------------------------
    p5 = page_host.add(slikui.Panel)
    p5.set_padding(12).set_gap(10)
    m_row = p5.add(slikui.Row).set_gap(14)
    m_left = m_row.add(slikui.Column).set_gap(6)
    m_left.add(slikui.Label, "ImageView — wheel zooms at the cursor, drag pans") \
        .set_color(slikui.Color.from_hex(0x8A94A0)).set_font_size(12)
    ui.image_view = m_left.add(slikui.ImageView)
    w, h = 96, 72
    xs = np.arange(w, dtype=np.uint8)
    ys = np.arange(h, dtype=np.uint8)
    chk = ((xs[None, :] // 8) + (ys[:, None] // 8)) % 2 == 0
    px = np.empty((h, w, 4), dtype=np.uint8)
    px[..., 0] = np.where(chk, 60 + xs[None, :], 90 + xs[None, :])
    px[..., 1] = np.where(chk, 60 + ys[:, None], 90 + ys[:, None])
    px[..., 2] = 140
    px[..., 3] = 255
    ui.image_view.set_pixels(px)
    m_right = m_row.add(slikui.Column).set_gap(6)
    m_right.add(slikui.Label, "Image — plain, Contain fit") \
        .set_color(slikui.Color.from_hex(0x8A94A0)).set_font_size(12)
    img = m_right.add(slikui.Image, "gradient")
    w2, h2 = 64, 48
    px2 = np.empty((h2, w2, 4), dtype=np.uint8)
    px2[..., 0] = np.tile(np.linspace(0, 255, w2, dtype=np.uint8), (h2, 1))
    px2[..., 1] = np.repeat(np.linspace(0, 255, h2, dtype=np.uint8), w2).reshape(h2, w2)
    px2[..., 2] = 128
    px2[..., 3] = 255
    img.set_pixels(px2).set_size(96, 72).set_fit(slikui.ImageFit.Contain)
    m_right.add(slikui.Label, "CompareView — drag the A|B divider") \
        .set_color(slikui.Color.from_hex(0x8A94A0)).set_font_size(12)
    ui.compare = m_right.add(slikui.CompareView)
    pa = np.empty((h2, w2, 4), dtype=np.uint8)
    pb = np.empty((h2, w2, 4), dtype=np.uint8)
    pa[..., 0] = 40 + xs[:w2][None, :]
    pa[..., 1] = 70
    pa[..., 2] = 160
    pa[..., 3] = 255
    pb[..., 0] = 160
    pb[..., 1] = np.clip(120 - ys[:h2][:, None], 0, 255)
    pb[..., 2] = 60
    pb[..., 3] = 255
    ui.compare.set_pixels_a(pa)
    ui.compare.set_pixels_b(pb)
    ui.compare.set_fraction(0.5)
    ui.pages.append(p5)

    # --- page 6: Custom ---------------------------------------------------
    p6 = page_host.add(slikui.Panel)
    p6.set_padding(12).set_gap(10)
    p6.add(slikui.Label, "LevelMeter — a custom widget (subclass of slikui.Widget)") \
        .set_font_size(15)
    p6.add(slikui.Label, "click or drag to scrub · peak marker decays · "
            ">90% logs a warning").set_color(slikui.Color.from_hex(0x8A94A0)) \
        .set_font_size(12)
    ui.meter = p6.add(LevelMeter, 0.4)
    ui.meter.on_change = lambda v: demo.say(
        "levelmeter → %.2f%s" % (v, " (clip!)" if v > 0.9 else ""),
        WARN if v > 0.9 else INFO)
    m_btns = p6.add(slikui.Row).set_gap(8)
    for v in (0.25, 0.5, 0.75, 1.0):
        val, label = v, "%d%%" % int(v * 100)

        def make_set(val=val, label=label):
            def on_click():
                ui.meter.set_value(val)
                if val > 0.9:
                    demo.say(f"levelmeter set → {label} (clip!)", WARN)
                else:
                    demo.say(f"levelmeter set → {label}")
            return on_click
        ui.meter_set_buttons.append(m_btns.add(slikui.Button, label)
                                    .on_click(make_set()))
    ui.pages.append(p6)

    apply_page(ui, 0)
    tabs.on_change(lambda i: (apply_page(ui, i),
                              demo.say("tabs → " + TAB_NAMES[i])))

    # ----- status bar -----------------------------------------------------
    status = root.add(slikui.StatusBar)
    status.set_text("showcase ready — every component is live")
    demo.status = status
    status.add(slikui.Spinner).set_speed(1.5)
    demo.count = status.add(slikui.Label, "0 events") \
        .set_color(slikui.Color.from_hex(0x8A94A0)).set_font_size(12)

    # ----- shared actions -------------------------------------------------
    def open_about():
        box = slikui.MessageBox(
            "About", "slikui showcase — every widget, live.\n"
                     "Python build; examples/05_showcase is the C++ twin.")
        box.add_button("Nice", lambda: None)
        ctx.open_dialog(box)
        demo.say("dialog 'About' opened")

    def open_file_dialog():
        fb = slikui.FileBrowser(slikui.FileBrowser.Mode.Open)
        ui.file_dialog = fb  # valid until closed (smoke clicks its ✕)
        fb.on_pick(lambda path: demo.say("file → " + path, OK))
        fb.on_close(lambda: demo.say("dialog 'Open' closed"))
        ctx.open_dialog(fb)
        demo.say("dialog 'Open' opened (navigate, pick a file)")

    def save():
        demo.say("menu File → Save (saved showcase.ui)", OK)
        if demo.status:
            demo.status.show_message("saved showcase.ui")
        demo.say("toast → 'Saved showcase.ui'")

    ui.open_about = open_about
    ui.open_file_dialog = open_file_dialog
    ui.save = save

    ctx.add_shortcut(slikui.Key.O, int(slikui.Mod.ModCtrl), lambda: (
        demo.say("shortcut Ctrl+O → open dialog"),
        ui.open_file_dialog and ui.open_file_dialog()))
    ctx.add_shortcut(slikui.Key.S, int(slikui.Mod.ModCtrl), lambda: (
        demo.say("shortcut Ctrl+S → save"), ui.save and ui.save()))


# One live plot/progress update (shared by interactive loop and smoke).
def pump_plot(ui, now, dt):
    if not ui.playing:
        return
    ui.plot_phase += dt
    if now >= ui.plot_next:
        ui.plot_next = now + 0.25
        ui.samples[:-1] = ui.samples[1:]
        ui.samples[-1] = 0.35 + 0.3 * math.sin(ui.plot_phase * 3.0)
        ui.plot_logged += 1
        if ui.plot_logged % 8 == 1:
            ui.demo.say("plot → sample %d" % len(ui.samples))
    ui.progress.set_value(0.5 + 0.5 * math.sin(now))


# ---------------------------------------------------------------------------
# Smoke mode: headless scripted pass over every component. Real synthetic
# mouse/key/scroll events wherever a widget's bounds make that practical;
# direct calls (through the same say() path) for popup-driven widgets.
# Prints the transcript and writes a PNG screenshot.
# ---------------------------------------------------------------------------
def run_smoke(png_path):
    app = slikui.App(960, 640)
    ui = UI()
    build_ui(app, ui)
    ui.demo.say("showcase started (smoke) — scripted pass over every component")

    state = {"now": 0.0}

    def frame(dt=0.016):
        state["now"] += dt
        pump_plot(ui, state["now"], dt)
        app.tick(state["now"], dt)
        app.render()

    frame()  # layout pass — bounds are live after this

    # Synthetic-input helpers (bounds are live per tick).
    def center(w):
        return (w.bounds.x + w.bounds.w / 2.0, w.bounds.y + w.bounds.h / 2.0)

    def click(w):
        p = center(w)
        app.handle(slikui.mouse_press(p))
        app.handle(slikui.mouse_release(p))

    def drag(frm, to):
        app.handle(slikui.mouse_press(frm))
        app.handle(slikui.mouse_move(to))
        app.handle(slikui.mouse_release(to))

    def wheel(p, dy):
        app.handle(slikui.scroll(p, (0.0, dy)))

    def type_text(s):
        for e in slikui.text_input_str(s):
            app.handle(e)

    def press_key(k, mods=0):
        app.handle(slikui.key_press(k, mods))

    # Pages are visibility-toggled Panels, so the script switches tabs before
    # touching each page's widgets (hit-testing skips hidden subtrees).
    def select_tab(i):
        ui.tabs.set_selected(i)
        apply_page(ui, i)
        ui.demo.say("tabs → " + TAB_NAMES[i])
        frame()

    # --- Controls page: real clicks, real slider drag ---------------------
    click(ui.btn_ping)
    click(ui.btn_danger)
    click(ui.checkbox)
    click(ui.radios[1])
    click(ui.sw)
    s = ui.slider.bounds
    y = center(ui.slider)[1]
    drag((s.x + s.w * 0.25, y), (s.x + s.w * 0.70, y))
    click(ui.segmented)
    frame()

    # Dialogs via real plumbing while the Controls page is up.
    press_key(slikui.Key.O, int(slikui.Mod.ModCtrl))
    frame()
    click(ui.file_dialog.close_button)
    frame()
    click(ui.btn_ping)
    frame()
    ui.save()
    frame()

    # --- Inputs page: real typing; direct paths for popup-driven widgets --
    select_tab(1)
    click(ui.field)
    type_text("hi")
    press_key(slikui.Key.Enter)
    click(ui.search.field())
    type_text("q")
    ui.dropdown.set_selected(1)  # set_selected doesn't announce; say directly
    ui.demo.say("dropdown → Linear")
    ui.dv.set_value(42.0)  # set_value fires on_change itself
    ui.vec_vals[:] = (1.0, 2.0)
    ui.demo.say("vec2 → (1.0, 2.0)")
    ui.color.set_color(slikui.Color.from_hex(0x7FD1FF))  # fires on_change
    frame()

    # --- Data page: direct selections, real pad drag, playing plot --------
    select_tab(2)
    ui.list.set_selected(1)
    ui.demo.say("list → beta")
    ui.tree.set_selected(2)
    ui.demo.say("tree → main.cpp")
    ui.table.set_selected(2)
    ui.demo.say("table → row 2 (views.hpp)")
    ui.prop_opacity.set_value(0.5)  # fires on_change
    p = ui.pad.bounds
    drag(center(ui.pad),
         (p.x + p.w * 0.55, p.y + p.h * 0.25))
    for _ in range(8):
        ui.samples[:-1] = ui.samples[1:]
        ui.samples[-1] = 0.35 + 0.3 * math.sin(ui.plot_phase * 3.0)
        ui.plot_phase += 1
    ui.plot_next = state["now"] + 0.25
    ui.demo.say("plot → +8 samples (playing)")
    frame()

    # --- Containers page: real expander/accordion clicks, splitter drag ---
    select_tab(3)
    click(ui.expander)
    click(ui.acc_sh)
    mid = (center(ui.vsplit)[0], ui.vsplit.bounds.y + ui.vsplit.bounds.h * 0.5)
    drag(mid, (mid[0], mid[1] + 24.0))
    wheel(center(ui.scroll), 3.0)
    ui.demo.say("scrollarea → scrolled by 3 ticks")
    frame()

    # --- Media page: real wheel-zoom; compare fraction directly -----------
    select_tab(4)
    wheel(center(ui.image_view), 1.0)
    ui.compare.set_fraction(0.35)
    ui.demo.say("compare → 0.35")
    frame()

    # --- Custom page: real meter scrub at 60%, then the 100% set button ---
    select_tab(5)
    m = ui.meter.bounds
    at = (m.x + m.w * 0.6, center(ui.meter)[1])
    app.handle(slikui.mouse_press(at))
    app.handle(slikui.mouse_release(at))
    click(ui.meter_set_buttons[3])  # 100% → warning level
    frame()

    frame(3.5)  # toast expiry + status transient clear

    save_image(app.render(), png_path)
    for line in ui.demo.transcript:
        print(line)
    print(f"smoke ok: {ui.demo.events} events, {ui.demo.log.line_count} log lines "
          f"live, screenshot {png_path}")
    # Deterministic teardown: close() destroys the C++ tree — including the
    # subtree owned by the Fill custom container — and every callback in it,
    # while the interpreter is alive, so exit is leak-free.
    app.close()
    del ui
    import gc
    gc.collect()


# ---------------------------------------------------------------------------
# Interactive mode: GLFW window; the CPU-rasterized UI is blitted as one
# texture (the UI-only version of glfw_demo.py's blit path).
# ---------------------------------------------------------------------------
BLIT_VS = """
#version 330 core
layout(location=0) in vec2 a_pos;
layout(location=1) in vec2 a_uv;
out vec2 v_uv;
void main(){ v_uv=a_uv; gl_Position=vec4(a_pos,0,1); }
"""

BLIT_FS = """
#version 330 core
in vec2 v_uv;
uniform sampler2D u_tex;
out vec4 frag;
void main(){ frag = texture(u_tex, v_uv); }
"""

KEY_MAP = {
    256: slikui.Key.Escape, 257: slikui.Key.Enter, 258: slikui.Key.Tab,
    259: slikui.Key.Backspace, 261: slikui.Key.Delete, 32: slikui.Key.Space,
    262: slikui.Key.Right, 263: slikui.Key.Left, 264: slikui.Key.Down,
    265: slikui.Key.Up, 268: slikui.Key.Home, 269: slikui.Key.End,
    266: slikui.Key.PageUp, 267: slikui.Key.PageDown,
}
BUTTON_MAP = {0: slikui.MouseButton.MouseLeft, 1: slikui.MouseButton.MouseMiddle,
              2: slikui.MouseButton.MouseRight}


def run_interactive():
    import glfw
    from OpenGL import GL

    def compile_shader(kind, src):
        sh = GL.glCreateShader(kind)
        GL.glShaderSource(sh, src)
        GL.glCompileShader(sh)
        if not GL.glGetShaderiv(sh, GL.GL_COMPILE_STATUS):
            raise RuntimeError(GL.glGetShaderInfoLog(sh).decode())
        return sh

    def build_program(vs_src, fs_src):
        prog = GL.glCreateProgram()
        for kind, src in ((GL.GL_VERTEX_SHADER, vs_src),
                          (GL.GL_FRAGMENT_SHADER, fs_src)):
            sh = compile_shader(kind, src)
            GL.glAttachShader(prog, sh)
            GL.glDeleteShader(sh)
        GL.glLinkProgram(prog)
        if not GL.glGetProgramiv(prog, GL.GL_LINK_STATUS):
            raise RuntimeError(GL.glGetProgramInfoLog(prog).decode())
        return prog

    if not glfw.init():
        raise SystemExit("glfw.init failed")
    glfw.window_hint(glfw.CONTEXT_VERSION_MAJOR, 3)
    glfw.window_hint(glfw.CONTEXT_VERSION_MINOR, 3)
    glfw.window_hint(glfw.OPENGL_PROFILE, glfw.OPENGL_CORE_PROFILE)
    try:  # macOS needs forward compatibility for 3.3 core
        glfw.window_hint(glfw.OPENGL_FORWARD_COMPAT, True)
    except Exception:
        pass
    win = glfw.create_window(960, 640, "slikui showcase (python)", None, None)
    if not win:
        glfw.terminate()
        raise SystemExit("could not create GLFW window")
    glfw.make_context_current(win)
    glfw.set_window_size_limits(win, 720, 480, glfw.DONT_CARE, glfw.DONT_CARE)

    # Blit program + fullscreen quad.
    prog = build_program(BLIT_VS, BLIT_FS)
    u_tex = GL.glGetUniformLocation(prog, "u_tex")
    vao = GL.glGenVertexArrays(1)
    GL.glBindVertexArray(vao)
    quad_pos = np.array([-1, 1, 1, 1, -1, -1, 1, 1, 1, -1, -1, -1], np.float32)
    quad_uv = np.array([0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 0, 1], np.float32)
    for loc, data in ((0, quad_pos), (1, quad_uv)):
        buf = GL.glGenBuffers(1)
        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, buf)
        GL.glBufferData(GL.GL_ARRAY_BUFFER, data.nbytes, data, GL.GL_STATIC_DRAW)
        GL.glEnableVertexAttribArray(loc)
        GL.glVertexAttribPointer(loc, 2, GL.GL_FLOAT, GL.GL_FALSE, 0, None)
    tex = GL.glGenTextures(1)
    GL.glBindTexture(GL.GL_TEXTURE_2D, tex)
    GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_MIN_FILTER, GL.GL_LINEAR)
    GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_MAG_FILTER, GL.GL_LINEAR)
    tex_size = [0, 0]

    app = slikui.App(960, 640)
    ui = UI()
    build_ui(app, ui)
    ui.demo.say("showcase started — click around, watch the log")

    def on_button(_w, button, action, _mods):
        x, y = glfw.get_cursor_pos(win)
        if action == glfw.PRESS:
            if button in BUTTON_MAP:
                app.handle(slikui.mouse_press((x, y), BUTTON_MAP[button]))
        elif action == glfw.RELEASE:
            if button in BUTTON_MAP:
                app.handle(slikui.mouse_release((x, y), BUTTON_MAP[button]))

    def on_cursor(_w, x, y):
        app.handle(slikui.mouse_move((x, y)))

    def on_scroll(_w, _xoff, yoff):
        x, y = glfw.get_cursor_pos(win)
        app.handle(slikui.scroll((x, y), (0.0, yoff)))

    def on_key(_w, key, _sc, action, mods):
        if key in KEY_MAP and action in (glfw.PRESS, glfw.RELEASE):
            e = (slikui.key_press(KEY_MAP[key], mods) if action == glfw.PRESS
                 else slikui.key_release(KEY_MAP[key], mods))
            app.handle(e)

    def on_char(_w, cp):
        app.handle(slikui.text_input(chr(cp)))

    glfw.set_mouse_button_callback(win, on_button)
    glfw.set_cursor_pos_callback(win, on_cursor)
    glfw.set_scroll_callback(win, on_scroll)
    glfw.set_key_callback(win, on_key)
    glfw.set_char_callback(win, on_char)

    last, now = __import__("time").perf_counter(), 0.0
    while not glfw.window_should_close(win):
        glfw.poll_events()
        fw, fh = glfw.get_framebuffer_size(win)
        ww, wh = glfw.get_window_size(win)
        if (fw, fh) != (app.target.width, app.target.height):
            app.target.resize(fw, fh)
        ui_scale = fw / ww if ww > 0 else 1.0
        app.ctx.theme.scale = ui_scale
        app.sink.set_scale(ui_scale)
        app.ctx.set_frame_size(float(ww), float(wh))  # LOGICAL: layout in points

        import time as _time
        dt = min(_time.perf_counter() - last, 0.05)
        last, now = _time.perf_counter(), now + dt
        pump_plot(ui, now, dt)
        app.tick(now, dt)

        GL.glViewport(0, 0, fw, fh)
        GL.glClearColor(0.0, 0.0, 0.0, 1.0)
        GL.glClear(GL.GL_COLOR_BUFFER_BIT)
        pix = app.render()  # (H, W) uint32 ABGR
        hh, wwd = pix.shape
        GL.glEnable(GL.GL_BLEND)
        GL.glBlendFunc(GL.GL_ONE, GL.GL_ONE_MINUS_SRC_ALPHA)  # premultiplied
        GL.glBindTexture(GL.GL_TEXTURE_2D, tex)
        if tex_size[0] != wwd or tex_size[1] != hh:
            GL.glTexImage2D(GL.GL_TEXTURE_2D, 0, GL.GL_RGBA, wwd, hh, 0,
                            GL.GL_RGBA, GL.GL_UNSIGNED_BYTE, None)
            tex_size[:] = [wwd, hh]
        GL.glTexSubImage2D(GL.GL_TEXTURE_2D, 0, 0, 0, wwd, hh, GL.GL_RGBA,
                           GL.GL_UNSIGNED_BYTE, pix.view(np.uint8))
        GL.glUseProgram(prog)
        GL.glUniform1i(u_tex, 0)
        GL.glBindVertexArray(vao)
        GL.glDrawArrays(GL.GL_TRIANGLES, 0, 6)
        GL.glDisable(GL.GL_BLEND)
        glfw.swap_buffers(win)

    glfw.destroy_window(win)
    app.close()  # release UI callbacks while the interpreter is alive
    glfw.terminate()


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--smoke":
        run_smoke(sys.argv[2] if len(sys.argv) > 2 else "showcase_smoke.png")
        return 0
    run_interactive()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
