"""09 - Text: styles, font families, and drawing text from custom widgets.

Built-in ``Label`` takes bold / italic / underline / strike and a font
family. Custom widgets draw text with ``env.text`` (anchored at a point) and
``env.text_in`` (aligned in a rect); both use the theme font, size and color
unless told otherwise, and return the rect the text covers.
``self.context.text_size`` measures the same text in ``measure()``. When a
widget needs to hit-test its text (carets, selection), it keeps a
``TextBlock`` from ``context.text_layout``.

Run:

    python -m slikui.examples.text              # writes ./text.png
    python -m slikui.examples.text --smoke      # scripted self-check
    python -m slikui.examples.text --window     # live window
"""

import slikui
from slikui.examples import _support

NAME = "text"
SIZE = (520, 600)

MUTED = 0x8A94A0


# doc: tag
import slikui


class Tag(slikui.Widget):
    """A pill-shaped label sized to its text: ``measure`` asks the context
    how big the text is, ``paint`` centers it in the pill with
    ``env.text_in``. The same options go to both calls, so they agree."""

    PAD_X, PAD_Y = 10, 4

    def __init__(self, text, color=None):
        super().__init__()
        self.text = text
        self.color = color  # None -> theme accent

    def opts(self):
        return dict(size=12, bold=True)

    def measure(self, constraints):
        s = self.context.text_size(self.text, **self.opts())
        return constraints.clamp(slikui.Size(s.w + 2 * self.PAD_X,
                                             s.h + 2 * self.PAD_Y))

    def stretch_cross(self):
        return False

    def paint(self, env):
        b = self.bounds
        env.list.rect(b, self.color or env.theme.accent, b.h / 2)
        env.text_in(b, self.text, align="center", valign="middle",
                    color=slikui.Color(255, 255, 255), **self.opts())


# doc: end


class StatCard(slikui.Widget):
    """A dashboard tile: caption top-left, value centered and large, unit
    in the bottom-right corner. Three anchors, three ``env.text_in`` calls,
    no layout objects."""

    def __init__(self, caption, value, unit=""):
        super().__init__()
        self.caption, self.value, self.unit = caption, value, unit

    def measure(self, constraints):
        return constraints.clamp(slikui.Size(140, 84))

    def stretch_cross(self):
        return False

    def paint(self, env):
        b, th = self.bounds, env.theme
        env.list.rect(b, th.surface_alt, 6)
        inner = slikui.Rect(b.x + 10, b.y + 8, b.w - 20, b.h - 16)
        env.text_in(inner, self.caption, size=12, color=th.text_dim)
        env.text_in(inner, self.value, size=28, bold=True,
                    align="center", valign="middle")
        env.text_in(inner, self.unit, size=12, italic=True, color=th.text_dim,
                    align="right", valign="bottom")


class FitBox(slikui.Widget):
    """The same sentence two ways at an adjustable width: cut to one line
    with an ellipsis, and wrapped. ``measure`` asks for the wrapped height
    at that width, so the widget grows as the width shrinks."""

    TEXT = ("Text that is too wide for its box can be cut short with an "
            "ellipsis, or wrapped onto as many lines as it needs.")

    def __init__(self, width=300):
        super().__init__()
        self.width = width

    def wrapped(self):
        return self.context.text_size(self.TEXT, wrap=True, max_width=self.width)

    def measure(self, constraints):
        line = self.context.text_metrics().line_height
        return constraints.clamp(slikui.Size(0, line + 12 + self.wrapped().h))

    def paint(self, env):
        b, th = self.bounds, env.theme
        line = env.text_in(slikui.Rect(b.x, b.y, self.width, b.h), self.TEXT,
                           overflow="ellipsis")
        para = slikui.Rect(b.x, line.y + line.h + 12, self.width, self.wrapped().h)
        env.list.rect(para, th.surface_alt, 3)
        env.text_in(para, self.TEXT, wrap=True)
        edge = b.x + self.width  # the width both texts were given
        env.list.line(slikui.Vec2(edge, b.y), slikui.Vec2(edge, b.y + b.h),
                      th.accent, 1)


# doc: selectabletext
import slikui


class SelectableText(slikui.Widget):
    """Wrapped, drag-to-select text: a kept ``TextBlock`` answers which
    character is under the pointer (``hit_test``), where the caret goes
    (``caret_rect``) and what to highlight (``selection_rects``). Indices
    are ordinary ``str`` indices."""

    def __init__(self, text, on_select=None):
        super().__init__()
        self.text = text
        self.on_select = on_select
        self.block = None
        self.anchor = self.caret = 0
        self.dragging = False

    @property
    def selected(self):
        a, b = sorted((self.anchor, self.caret))
        return self.text[a:b]

    def measure(self, constraints):
        w = constraints.max.x if constraints.max.x < 1e6 else 0
        return constraints.clamp(self.context.text_size(self.text, wrap=True,
                                                        max_width=w))

    def arrange(self, area):
        self.bounds = area
        # Re-lay the block only when the width changes.
        if self.block is None or self.block_w != area.w:
            self.block = self.context.text_layout(self.text, area.w)
            self.block_w = area.w

    def _index(self, pos):
        return self.block.hit_test(slikui.Vec2(pos.x - self.bounds.x,
                                               pos.y - self.bounds.y))

    def on_event(self, e):
        T = slikui.EventType
        if e.type == T.MousePress and e.button == slikui.MouseButton.MouseLeft:
            self.anchor = self.caret = self._index(e.pos)
            self.dragging = True
            self.context.capture_mouse(self)
            return True
        if e.type == T.MouseMove and self.dragging:
            self.caret = self._index(e.pos)
            return True
        if e.type == T.MouseRelease and self.dragging:
            self.dragging = False
            self.context.release_mouse(self)
            if self.on_select:
                self.on_select(self.selected)
            return True
        return False

    def paint(self, env):
        if self.block is None:
            return
        b, th = self.bounds, env.theme
        for r in self.block.selection_rects(self.anchor, self.caret):
            env.list.rect(slikui.Rect(b.x + r.x, b.y + r.y, r.w, r.h),
                          th.accent.with_alpha(0.35))
        env.draw(self.block, slikui.Vec2(b.x, b.y))
        c = self.block.caret_rect(self.caret)
        env.list.rect(slikui.Rect(b.x + c.x, b.y + c.y, 1.5, c.h), th.accent)


# doc: end


def register_mono(ctx):
    """Give the "mono" role a real monospace font when the system has one.
    Without it, "mono" falls back to the body font; styles a family lacks
    (here italic) are synthesized from the closest face."""
    for style in (slikui.FontStyle.Regular, slikui.FontStyle.Bold):
        found = slikui.find_system_font_face("mono", style)
        if found:
            ctx.add_font("mono", found.path, style, found.face)


def section(root, title):
    root.add(slikui.Label, title, bold=True).set_color(slikui.Color.from_hex(MUTED))


def build(app):
    register_mono(app.ctx)
    muted = slikui.Color.from_hex(MUTED)
    root = app.set_root(slikui.Column, padding=16, gap=8)
    root.add(slikui.Label, "Text", font_size=26, bold=True)

    section(root, "Label styles")
    row = root.add(slikui.Row, gap=12)
    plain = row.add(slikui.Label, "Regular")
    bold = row.add(slikui.Label, "Bold", bold=True)
    row.add(slikui.Label, "Italic", italic=True)
    row.add(slikui.Label, "Underline", underline=True)
    row.add(slikui.Label, "Strike", strike=True)
    row.add(slikui.Label, "mono()").set_font("mono")

    section(root, "env.text_in: tags sized by text_size, stat cards")
    tags = root.add(slikui.Row, gap=6)
    tag = tags.add(Tag, "release")
    tags.add(Tag, "v0.5").color = slikui.Color.from_hex(0x3FA46A)
    tags.add(Tag, "text").color = slikui.Color.from_hex(0xC2733A)
    cards = root.add(slikui.Row, gap=10)
    cards.add(StatCard, "Latency", "42", "ms")
    cards.add(StatCard, "Throughput", "1.2k", "req/s")

    section(root, "Ellipsis and wrapping - drag the slider")
    width = root.add(slikui.Slider, 80, 460, value=300)
    fit = root.add(FitBox, 300)
    width.on_change(lambda v: setattr(fit, "width", v))

    section(root, "TextBlock - drag across the text to select")
    sel = root.add(SelectableText,
                   "A TextBlock keeps its layout, so a widget can ask which "
                   "character is under the pointer, where the caret goes and "
                   "which rectangles to highlight.\nNew lines work too.")
    status = root.add(slikui.Label, "selected: (nothing)")
    status.set_color(muted)
    sel.on_select = lambda s: status.set_text(f"selected: {s!r}")

    return _support.State(root=root, plain=plain, bold=bold, tag=tag, fit=fit,
                          width=width, sel=sel, status=status)


def smoke(app, state, session, log):
    c = _support.Checks(log)
    session.advance(1)
    ctx = app.ctx

    loose = slikui.Constraints.loose()
    c.ok(ctx.text_size("Bold", bold=True).w > ctx.text_size("Bold").w,
         "bold text is wider than regular")
    s = ctx.text_size("release", size=12, bold=True)
    tb = state.tag.bounds
    c.ok(abs(tb.w - (s.w + 2 * Tag.PAD_X)) < 0.5,
         f"Tag sized from text_size ({tb.w:.1f} px wide)")

    # Narrowing the FitBox wraps onto more lines: the widget grows.
    h0 = state.fit.bounds.h
    state.width.set_value(140)
    session.advance(2)
    c.ok(state.fit.width == 140, "slider drives the FitBox width")
    c.ok(state.fit.bounds.h > h0,
         f"narrower box wraps taller ({h0:.0f} -> {state.fit.bounds.h:.0f} px)")
    state.width.set_value(300)
    session.advance(2)

    sb = state.sel.bounds
    line = state.sel.block.line_height
    before = _support.crop(app.render(), sb)

    # A drag from the first line to the last spans the line break.
    _support.drag(app, (sb.x + 2, sb.y + line / 2), (sb.x + sb.w, sb.y + sb.h - 2))
    session.advance(1)
    c.ok("\n" in state.sel.selected, "selection spans lines")
    c.ok(_support.diff(before, _support.crop(app.render(), sb)) > 0,
         "selection highlight painted")

    # Drag across the start of the first line: the selection is its prefix.
    end = state.sel.block.caret_rect(len("A TextBlock"))
    _support.drag(app, (sb.x + 1, sb.y + line / 2),
                  (sb.x + end.x - 1, sb.y + line / 2))
    session.advance(1)
    c.ok(state.sel.selected == "A TextBlock",
         f"drag selected {state.sel.selected!r}")
    c.ok(state.status.text == "selected: 'A TextBlock'", "on_select reported it")
    return {"selected": len(state.sel.selected), "fit_h": round(state.fit.bounds.h)}


def interactive():
    from slikui.examples import _window
    return _window.run(NAME, SIZE, build)


def main(argv=None):
    return _support.main_entry(name=NAME, size=SIZE, build=build, smoke=smoke,
                               interactive=interactive, argv=argv)


if __name__ == "__main__":
    raise SystemExit(main())
