"""Regression tests for bugs reported from a downstream app (geetar, 0.4.1):
label wrapping at high UI scale, TreeView relabeling and chevrons, Row
cross-centering, software line clipping, lazily-built popup content, and
menu hover clearing.
"""

import numpy as np
import pytest

import slikui
from slikui import Rect, Vec2
from slikui._slikui import Context, Sink, Target


def frame(ui, t=0.0, dt=1 / 60):
    ui.tick(t, dt)
    return ui.render().copy()


def ink_bbox(px, bg, x0, y0, x1, y1):
    """(x0, y0, x1, y1) of non-background pixels in a crop, or None."""
    sub = px[y0:y1, x0:x1]
    ys, xs = np.where(sub != bg)
    if not len(xs):
        return None
    return xs.min(), ys.min(), xs.max() + 1, ys.max() + 1


# --- Label: a single-line label never wraps inside its own measured width ----

@pytest.mark.parametrize("scale", [1.0, 2.0, 2.5, 3.0, 3.125, 3.2, 4.0])
@pytest.mark.parametrize("text,px", [("Introduction", 21), ("Introduction", 15),
                                     ("Curriculum", 17), ("One-string melodies", 30)])
def test_label_does_not_wrap_at_high_scale(scale, text, px):
    ui = slikui.App(600, 120, scale=scale)
    ui.ctx.theme.scale = scale
    row = ui.set_root(slikui.Column).add(slikui.Row)
    label = row.add(slikui.Label, text, font_size=px)
    pixels = frame(ui)
    bg = pixels[-1, -1]
    box = ink_bbox(pixels, bg, 0, 0, pixels.shape[1], pixels.shape[0])
    assert box is not None
    # All ink within the label's frame (one line), in framebuffer px.
    assert box[3] <= int(np.ceil((label.bounds.y + label.bounds.h) * scale)) + 1
    ui.close()


# --- TreeView ----------------------------------------------------------------

R = slikui.TreeView.Row


def test_treeview_set_rows_relabels():
    ui = slikui.App(300, 200)
    tree = ui.set_root(slikui.Column).add(slikui.TreeView)
    tree.set_rows([R("aaaa"), R("bbbb")])
    before = frame(ui)
    tree.set_rows([R("WWWWWWWW"), R("bbbb")])
    after = frame(ui)
    assert not np.array_equal(before, after)

    # Growing the row list after the first layout (used to read past the end
    # of the per-row layout cache) draws every row.
    tree.set_rows([R(f"row {i}") for i in range(6)])
    grown = frame(ui)
    bg = grown[-1, -1]
    for i in range(6):
        y = int(tree.bounds.y + i * 24)
        assert ink_bbox(grown, bg, 0, y, 300, y + 24) is not None, f"row {i} blank"
    ui.close()


def test_treeview_chevron_shows_expanded_state():
    def chevron_box(expanded):
        ui = slikui.App(200, 100)
        tree = ui.set_root(slikui.Column).add(slikui.TreeView)
        tree.set_rows([R("parent", 0, True), R("child", 1)])
        tree.set_expanded(0, expanded)
        px = frame(ui)
        x = int(tree.bounds.x)
        box = ink_bbox(px, px[-1, -1], x, 0, x + 14, 24)  # chevron column, row 0
        ui.close()
        assert box is not None
        return box[2] - box[0], box[3] - box[1]

    w, h = chevron_box(False)
    assert h > w  # ">" is taller than wide
    w, h = chevron_box(True)
    assert w > h  # "v" is wider than tall


# --- Row: CrossMode.Center centers non-stretching children -------------------

class Chip(slikui.Widget):
    def measure(self, c):
        return c.clamp(slikui.Size(10, 10))

    def stretch_cross(self):
        return False


def test_row_center_centers_non_stretching_children():
    ui = slikui.App(300, 100)
    row = ui.set_root(slikui.Column).add(slikui.Row)
    row.set_cross(slikui.CrossMode.Center)
    row.add(slikui.Label, "Tall", font_size=30)
    chip = row.add(Chip)
    switch = row.add(slikui.Switch)
    frame(ui)
    mid = row.bounds.y + row.bounds.h / 2
    for w in (chip, switch):
        assert abs((w.bounds.y + w.bounds.h / 2) - mid) < 0.51
    # Stretch (the default) still pins non-stretching children to the start.
    row.set_cross(slikui.CrossMode.Stretch)
    frame(ui)
    assert chip.bounds.y == row.bounds.y
    ui.close()


# --- Software renderers clip lines -------------------------------------------

RED = slikui.Color(255, 0, 0)


class ClippedLines(slikui.Widget):
    def measure(self, c):
        return c.clamp(slikui.Size(300, 300))

    def paint(self, env):
        env.list.push_clip(Rect(50, 50, 100, 100))
        env.list.line(Vec2(0, 0), Vec2(280, 280), RED, 3)      # crosses the clip
        env.list.line(Vec2(100, 0), Vec2(100, 280), RED, 4)    # crosses the clip
        env.list.line(Vec2(170, 0), Vec2(170, 280), RED, 2)    # entirely outside
        env.list.line(Vec2(0, 200), Vec2(280, 200), RED, 1)    # entirely outside
        env.list.pop_clip()


def _render_lines(sink_kind, scale):
    w = h = 300
    if sink_kind == "cpp":
        target = Target(int(w * scale), int(h * scale))
        sink = Sink(target)
    else:
        sink = slikui.CanvasSink(int(w * scale), int(h * scale))
    sink.set_scale(float(scale))
    ctx = Context(sink)
    ctx.set_frame_size(float(w), float(h))
    ctx.theme.scale = scale
    ctx.set_root_column().add_widget(ClippedLines())
    ctx.tick(0.0, 1 / 60)
    ctx.render()
    px = (target.pixels() if sink_kind == "cpp" else sink.buffer).copy()
    ctx.clear_tree()
    return px


@pytest.mark.parametrize("scale", [1.0, 2.0, 3.125])
def test_software_lines_respect_clip(scale):
    cpp = _render_lines("cpp", scale)
    py = _render_lines("py", scale)
    assert np.array_equal(cpp, py)
    red = ((cpp & 0xFF) > 0xC0) & (((cpp >> 8) & 0xFF) < 0x60)
    ys, xs = np.where(red)
    assert len(xs) > 0
    lo, hi = 50 * scale, 150 * scale
    assert xs.min() >= np.ceil(lo) and xs.max() < np.ceil(hi)
    assert ys.min() >= np.ceil(lo) and ys.max() < np.ceil(hi)


# --- Popups: children created during measure() draw --------------------------

class LazyLabel(slikui.Widget):
    """Builds its Label on first measure (fine in the main tree)."""

    def __init__(self):
        super().__init__()
        self.label = None

    def measure(self, c):
        if self.label is None:
            self.label = self.add(slikui.Label, "HELLO", font_size=20)
        self.label.measure(c)
        return c.clamp(slikui.Size(120, 40))

    def arrange(self, r):
        self.bounds = r
        self.label.arrange(r)


@pytest.mark.parametrize("where", ["root", "popup"])
def test_lazily_built_label_draws_first_frame(where):
    ui = slikui.App(300, 200)
    root = ui.set_root(slikui.Column)
    anchor = root.add(slikui.Button, "anchor")
    if where == "root":
        w = root.add(LazyLabel)
    else:
        w = LazyLabel()
        ui.ctx.push_popup(w, anchor, slikui.PopupDir.Below)
    px = frame(ui)
    r = w.bounds
    crop = px[int(r.y):int(r.y + r.h), int(r.x):int(r.x + r.w)]
    assert len(np.unique(crop)) > 2  # glyph antialiasing, not a flat fill
    ui.close()


# --- Menus: hover clears when the pointer leaves ------------------------------

def _settle(ui, t):
    for i in range(30):
        ui.tick(t + i / 60, 1 / 60)
    return ui.render().copy()


def _crop(px, r):
    return px[int(r.y):int(r.y + r.h), int(r.x):int(r.x + r.w)].copy()


def test_menubar_title_hover_clears_on_leave():
    ui = slikui.App(300, 200)
    root = ui.set_root(slikui.Column)
    bar = root.add(slikui.MenuBar)
    bar.add_menu("File").add_item("Open", lambda: None)
    root.add(slikui.Button, "elsewhere")
    title = bar.children[0]
    rest = _crop(_settle(ui, 0.0), title.bounds)
    c = title.bounds
    ui.handle(slikui.mouse_move((c.x + c.w / 2, c.y + c.h / 2)))
    hovered = _crop(_settle(ui, 1.0), title.bounds)
    assert not np.array_equal(rest, hovered)
    ui.handle(slikui.mouse_move((150, 150)))
    left = _crop(_settle(ui, 2.0), title.bounds)
    assert np.array_equal(rest, left)
    ui.close()


def test_menu_items_hover_one_at_a_time():
    ui = slikui.App(300, 300)
    root = ui.set_root(slikui.Column)
    bar = root.add(slikui.MenuBar)
    menu = bar.add_menu("File")
    for name in ("Open", "Save", "Close"):
        menu.add_item(name, lambda: None)
    title = bar.children[0]
    frame(ui)  # lay out before reading the title's bounds
    c = title.bounds
    for e in slikui.mouse_click((c.x + c.w / 2, c.y + c.h / 2)):
        ui.handle(e)
    _settle(ui, 0.0)
    popup = ui.ctx.popup(0)
    first, second = popup.children[0], popup.children[1]
    rest = _crop(_settle(ui, 0.5), first.bounds)
    b = first.bounds
    ui.handle(slikui.mouse_move((b.x + b.w / 2, b.y + b.h / 2)))
    assert not np.array_equal(rest, _crop(_settle(ui, 1.0), first.bounds))
    b = second.bounds
    ui.handle(slikui.mouse_move((b.x + b.w / 2, b.y + b.h / 2)))
    assert np.array_equal(rest, _crop(_settle(ui, 2.0), first.bounds))
    ui.close()


# --- Toasts are passive: clicks pass through, popups unaffected ---------------

class Probe(slikui.Widget):
    def __init__(self):
        super().__init__()
        self.seen = []

    def on_event(self, e):
        if e.type in (slikui.EventType.MousePress, slikui.EventType.Click):
            self.seen.append(e.type.name)
        return e.type == slikui.EventType.MousePress


def test_open_toast_does_not_swallow_clicks():
    ui = slikui.App(300, 200)
    probe = ui.set_root(Probe)
    frame(ui)
    ui.ctx.push_toast("Welcome", 10.0)
    frame(ui, 0.1)
    c = ui.ctx.toast(0).bounds.center()
    for pos in ((20, 20), (c.x, c.y)):  # elsewhere, then on the toast
        probe.seen.clear()
        for e in slikui.mouse_click(pos):
            ui.handle(e)
        assert probe.seen == ["MousePress", "Click"]
    assert ui.ctx.toast_count == 1 and ui.ctx.popup_count == 0
    ui.close()


def test_escape_closes_menu_not_toast():
    ui = slikui.App(300, 200)
    root = ui.set_root(slikui.Column)
    bar = root.add(slikui.MenuBar)
    bar.add_menu("File").add_item("Open", lambda: None)
    frame(ui)
    t = bar.children[0].bounds
    for e in slikui.mouse_click((t.x + 5, t.y + 5)):
        ui.handle(e)
    ui.ctx.push_toast("Saved", 10.0)
    frame(ui, 0.1)
    assert ui.ctx.popup_count == 1
    ui.handle(slikui.key_press(slikui.Key.Escape))
    assert ui.ctx.popup_count == 0 and ui.ctx.toast_count == 1
    ui.close()


# --- Segmented fits its frame ---------------------------------------------------

def test_segmented_fits_narrow_frame():
    ui = slikui.App(400, 100)
    row = ui.set_root(slikui.Column).add(slikui.Row)
    side = row.add(slikui.Column)
    row.add(slikui.Spacer)
    natural = None
    seg = side.add(slikui.Segmented)
    seg.set_items(["Curriculum", "Library", "Tools"])
    frame(ui)
    natural = seg.bounds.w

    ui2 = slikui.App(int(natural * 0.7) + 20, 100)
    holder = ui2.set_root(slikui.Column, padding=10)
    seg2 = holder.add(slikui.Segmented)
    seg2.set_items(["Curriculum", "Library", "Tools"])
    px = frame(ui2)
    b = seg2.bounds
    assert b.w < natural
    bg = px[-1, -1]
    box = ink_bbox(px, bg, 0, 0, px.shape[1], px.shape[0])
    assert box[0] >= int(b.x) and box[2] <= int(np.ceil(b.x + b.w))
    # The last segment is still reachable and selectable.
    for e in slikui.mouse_click((b.x + b.w - 4, b.y + b.h / 2)):
        ui2.handle(e)
    assert seg2.selected == 2
    ui.close()
    ui2.close()


# --- TreeView / ListView keep long labels inside their frame -------------------

LONG = "a label that is much too long to fit in the narrow frame it was given"


@pytest.mark.parametrize("kind", ["tree", "list"])
def test_long_rows_elide_inside_frame(kind):
    ui = slikui.App(160, 120)
    col = ui.set_root(slikui.Column, padding=10)
    if kind == "tree":
        w = col.add(slikui.TreeView)
        w.set_rows([R(LONG), R("short", 1)])
    else:
        w = col.add(slikui.ListView)
        w.set_items([LONG, "short"])
    px = frame(ui)
    b = w.bounds
    assert b.x + b.w <= 150
    box = ink_bbox(px, px[-1, -1], 0, 0, px.shape[1], px.shape[0])
    assert box[2] <= int(np.ceil(b.x + b.w))
    ui.close()


# --- Runtime UI-scale changes (zoom) -----------------------------------------

def _build_zoomable(root):
    root.add(slikui.Button, "Next lesson: Two-string melodies")
    seg = root.add(slikui.Segmented)
    seg.set_items(["Curriculum", "Library", "Tools"])
    root.add(slikui.Checkbox, "Metronome")
    tree = root.add(slikui.TreeView)
    tree.set_rows([R("Level 0 - Setup", 0, True), R("Meet the guitar", 1)])
    lv = root.add(slikui.ListView)
    lv.set_items(["Reading tab"])
    tabs = root.add(slikui.Tabs)
    tabs.set_tabs(["Curriculum", "Library"])
    return [c for c in root.children]


def test_widget_sizes_follow_runtime_scale_change():
    """Logical sizes must not depend on the scale at first layout."""
    def sizes(start, end):
        ui = slikui.App(600, 400, scale=start)
        ui.ctx.theme.scale = start
        kids = _build_zoomable(ui.set_root(slikui.Column, cross=slikui.CrossMode.Start))
        frame(ui)
        ui.ctx.theme.scale = end
        frame(ui, 0.1)
        out = [(k.widget_type(), round(k.bounds.w), round(k.bounds.h)) for k in kids]
        ui.close()
        return out

    fresh = sizes(3.2, 3.2)
    zoomed = sizes(2.0, 3.2)
    for (name, w0, h0), (_, w1, h1) in zip(fresh, zoomed):
        assert abs(w0 - w1) <= 1 and abs(h0 - h1) <= 1, name


class ThemeWatcher(slikui.Widget):
    def __init__(self):
        super().__init__()
        self.calls = 0

    def theme_changed(self):
        self.calls += 1


def test_custom_widget_theme_changed_hook():
    ui = slikui.App(200, 100)
    w = ui.set_root(slikui.Column).add(ThemeWatcher)
    frame(ui)
    frame(ui, 0.1)
    assert w.calls == 0          # unchanged theme: no call
    ui.ctx.theme.scale = 2.0
    frame(ui, 0.2)
    assert w.calls == 1
    slikui.apply_preset(ui.ctx.theme, 1)  # palette change, same scale
    frame(ui, 0.3)
    frame(ui, 0.4)
    assert w.calls == 2
    ui.close()


# --- Gaps: remove(), expand, scroll API, keys, builder kwargs ------------------

class Box(slikui.Widget):
    def __init__(self, label="box", *, color=None):
        super().__init__()
        self.label = label
        self.color = color

    def measure(self, c):
        return c.clamp(slikui.Size(40, 20))


def test_remove_from_click_handler_and_readd_custom():
    ui = slikui.App(300, 200)
    root = ui.set_root(slikui.Column)
    custom = root.add(Box)
    button = root.add(slikui.Button, "remove both")

    def on_click():
        assert root.remove(custom)
        assert root.remove(button)  # the handler's own widget

    button.on_click(on_click)
    frame(ui)
    b = button.bounds
    for e in slikui.mouse_click((b.x + 5, b.y + 5)):
        ui.handle(e)
    del button  # handle to a removed built-in widget: invalid now
    frame(ui)
    assert len(root.children) == 0
    # A removed custom widget survives on the Python side and can go back in.
    assert custom.label == "box"
    root.add_widget(custom)
    frame(ui)
    assert root.children[0] is custom and custom.bounds.h == 20
    assert not root.remove(Box())  # not a child
    ui.close()


def test_clear_children_scrollarea_forwards_to_content():
    ui = slikui.App(300, 200)
    scroll = ui.set_root(slikui.Column).add(slikui.ScrollArea)
    for i in range(10):
        scroll.add(slikui.Label, f"row {i}")
    frame(ui)
    scroll.clear_children()
    frame(ui)
    assert len(scroll.content.children) == 0
    scroll.add(slikui.Label, "again")
    frame(ui)
    assert len(scroll.content.children) == 1
    ui.close()


def test_expand_kwarg_and_scroll_to_tree_row():
    ui = slikui.App(300, 240)
    root = ui.set_root(slikui.Column)
    root.add(slikui.Label, "header")
    scroll = root.add(slikui.ScrollArea, expand=True)
    tree = scroll.add(slikui.TreeView)
    tree.set_rows([R(f"lesson {i}", 0) for i in range(60)])
    frame(ui)
    assert scroll.expand
    assert scroll.bounds.bottom() == pytest.approx(240, abs=0.5)
    assert scroll.scroll_y == 0 and scroll.max_scroll > 0
    scroll.scroll_to(tree.row_rect(45))
    frame(ui, 0.1)
    r = tree.row_rect(45)
    assert r.y >= scroll.bounds.y and r.bottom() <= scroll.bounds.bottom()
    scroll.scroll_to_top()
    frame(ui, 0.2)
    assert scroll.scroll_y == 0
    ui.close()


def test_punctuation_keys_and_shortcut():
    K = slikui.Key
    for name in ("Equal", "LeftBracket", "RightBracket", "Backslash", "Semicolon", "Grave"):
        assert hasattr(K, name)
    ui = slikui.App(100, 100)
    ui.set_root(slikui.Column)
    hits = []
    ui.ctx.add_shortcut(K.Equal, slikui.Mod.ModCtrl, lambda: hits.append("zoom in"))
    ui.handle(slikui.key_press(K.Equal, "ctrl"))
    assert hits == ["zoom in"]
    assert slikui.shortcut_label(K.RightBracket, slikui.Mod.ModCtrl) == "Ctrl+]"
    ui.close()


def test_builder_routes_constructor_kwargs():
    ui = slikui.App(200, 100)
    root = ui.set_root(slikui.Column)
    red = slikui.Color(255, 0, 0)
    w = root.add(Box, label="hi", color=red, tooltip="tip")
    assert (w.label, w.color, w.tooltip) == ("hi", red, "tip")
    with pytest.raises(AttributeError):
        root.add(Box, no_such_option=1)
    ui.close()


def test_constraints_and_float_color():
    c = slikui.Constraints(Vec2(10, 20), Vec2(100, 200))
    assert (c.min.x, c.max.y) == (10, 200)
    s = c.clamp(slikui.Size(5, 500))
    assert (s.w, s.h) == (10, 200)
    col = slikui.Color.from_float(1.0, 0.5, 0.0)
    assert (col.r, col.g, col.b, col.a) == (255, 128, 0, 255)


# --- Menus: uniform row width, check gutter only when needed --------------------

def _open_menu(build):
    ui = slikui.App(400, 240)
    bar = ui.set_root(slikui.Column).add(slikui.MenuBar)
    build(bar.add_menu("Menu"))
    frame(ui)
    t = bar.children[0].bounds
    for e in slikui.mouse_click((t.x + 5, t.y + 5)):
        ui.handle(e)
    px = frame(ui, 0.1)
    return ui, ui.ctx.popup(0), px


def _label_inset(px, item, bg_color):
    """Logical px from the row's left edge to its first ink column."""
    r = item.bounds
    sub = px[int(r.y):int(r.y + r.h), int(r.x):int(r.x + r.w)]
    cols = np.where((sub != bg_color).any(axis=0))[0]
    return cols.min()


def test_menu_rows_share_width_and_hints_align():
    def build(m):
        m.add_item("Open", lambda: None, hint="Ctrl+O")
        m.add_item("Save as PDF...", lambda: None)
        m.add_item("Quit", lambda: None, hint="Ctrl+Q")
    ui, menu, _ = _open_menu(build)
    widths = {round(c.bounds.w, 3) for c in menu.children}
    assert len(widths) == 1  # one hover-highlight width
    ui.close()


def test_menu_check_gutter_only_when_checkable():
    def plain(m):
        m.add_item("Open", lambda: None)
    def checkable(m):
        m.add_item("Open", lambda: None)
        m.add_checkable("Metronome", lambda: False, lambda: None)
    insets = []
    for build in (plain, checkable):
        ui, menu, px = _open_menu(build)
        item = menu.children[0]
        bg = px[int(item.bounds.y + 1), int(item.bounds.x + 2)]
        insets.append(_label_inset(px, item, bg))
        ui.close()
    assert insets[0] < 16 < insets[1]


# --- ScrollArea spacing: content hugs the viewport, thumb hugs the content ------

def test_scrollarea_tight_insets_and_pane_edge_scrollbar():
    ui = slikui.App(300, 200)
    pane = ui.set_root(slikui.Column)
    pane.set_padding(8, 8, 0, 8)  # per side: no right padding at the pane edge
    scroll = pane.add(slikui.ScrollArea, expand=True)
    tree = scroll.add(slikui.TreeView)
    tree.set_rows([R(f"row {i}") for i in range(40)])
    frame(ui)
    s, t = scroll.bounds, tree.bounds
    assert s.x + s.w == 300                      # scroll area reaches the pane edge
    assert t.x - s.x == pytest.approx(2)         # 2 px left inset (was 8)
    assert (s.x + s.w) - (t.x + t.w) == pytest.approx(18)  # 2 + 6 gap + 8 thumb + 2 (was 28)
    scroll.set_content_insets(10, 10)
    frame(ui, 0.1)
    assert tree.bounds.x - s.x == pytest.approx(10)
    ui.close()
