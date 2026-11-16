"""Text API from Python: env.text / text_in / draw, Context.text_size /
text_metrics / text_layout, font families, Label styles."""

import pytest

import slikui


def default_app(w=320, h=200):
    """An App on the bundled font, so metrics are identical everywhere."""
    ui = slikui.App(w, h)
    assert ui.ctx.load_default_font()
    return ui


class Probe(slikui.Widget):
    """Calls `fn(env, bounds)` from paint() and keeps what it returns."""

    def __init__(self, fn):
        super().__init__()
        self.fn = fn
        self.result = None

    def measure(self, constraints):
        return constraints.clamp(slikui.Size(300, 180))

    def paint(self, env):
        self.result = self.fn(env, self.bounds)


def run(ui, fn):
    probe = Probe(fn)
    ui.ctx.set_root_widget(probe)
    ui.tick(0.0, 1 / 60)
    ui.render()
    return probe.result


def test_env_text_defaults_and_rect():
    ui = default_app()
    r = run(ui, lambda env, b: env.text(slikui.Vec2(10, 20), "Hello"))
    assert (r.x, r.y) == (10, 20)
    s = ui.ctx.text_size("Hello")
    assert r.w == pytest.approx(s.w) and r.h == pytest.approx(s.h)
    m = ui.ctx.text_metrics()
    assert m.ascent > 0 and m.descent > 0 and m.line_height > 0


def test_env_text_keywords_and_string_enums():
    ui = default_app()

    def paint(env, b):
        return [
            env.text(slikui.Vec2(100, 50), "Hi", align="center"),
            env.text(slikui.Vec2(100, 50), "Hi", align=slikui.TextAlign.Right),
            env.text_in(slikui.Rect(0, 0, 200, 100), "Hi", align="center", valign="middle"),
            env.text(slikui.Vec2(0, 0), "styled", size=20, bold=True, italic=True,
                     underline=True, strike=True, color=slikui.Color(255, 0, 0)),
        ]

    centered, right, boxed, styled = run(ui, paint)
    assert centered.x + centered.w / 2 == pytest.approx(100)
    assert right.x + right.w == pytest.approx(100)
    assert boxed.x + boxed.w / 2 == pytest.approx(100)
    assert boxed.y + boxed.h / 2 == pytest.approx(50)
    assert styled.h > ui.ctx.text_size("styled").h  # 20px > theme 15px


def test_bad_enum_string_raises():
    ui = default_app()
    with pytest.raises(ValueError, match="align must be one of"):
        ui.ctx.text_size("x", align="middle")
    with pytest.raises(TypeError):
        ui.ctx.text_size("x", 12)  # options are keyword-only


def test_text_size_wrap_and_ellipsis():
    ui = default_app()
    one = ui.ctx.text_size("wrap me onto several lines")
    wrapped = ui.ctx.text_size("wrap me onto several lines", wrap=True, max_width=60)
    assert wrapped.w <= 60 + 1e-3
    assert wrapped.h > one.h * 2
    cut = ui.ctx.text_size("a long line of text", max_width=60, overflow="ellipsis")
    assert cut.w <= 60 + 1e-3


def test_text_draws_glyph_commands():
    ui = default_app()
    seen = {}

    def paint(env, b):
        before = len(env.list)
        env.text(slikui.Vec2(0, 0), "abc", underline=True)
        cmds = [env.list[i].type for i in range(before, len(env.list))]
        seen["glyphs"] = cmds.count(slikui.DrawCmdType.Glyph)
        seen["rects"] = cmds.count(slikui.DrawCmdType.Rect)

    run(ui, paint)
    assert seen == {"glyphs": 3, "rects": 1}


def test_text_block_queries_use_str_indices():
    ui = default_app()
    # U+00E9 is two UTF-8 bytes: indices must still be str indices.
    block = ui.ctx.text_layout("h\u00e9llo world\nnext")
    assert block.line_count == 2
    assert block.text == "h\u00e9llo world\nnext"
    lh = block.line_height
    assert block.hit_test(slikui.Vec2(-5, 1)) == 0
    assert block.hit_test(slikui.Vec2(1000, 1)) == 11   # before the '\n'
    assert block.hit_test(slikui.Vec2(0, lh * 1.5)) == 12
    c2 = block.caret_rect(2)
    assert block.hit_test(slikui.Vec2(c2.x + 0.1, 1)) == 2
    sel = block.selection_rects(6, 14)
    assert len(sel) == 2

    def paint(env, b):
        before = len(env.list)
        env.draw(block, slikui.Vec2(5, 5))
        return len(env.list) - before

    assert run(ui, paint) == 14  # inked glyphs (spaces and '\n' draw nothing)


def test_add_font_memory_registers_a_family():
    path = slikui.find_system_font()
    if not path:
        pytest.skip("no system font")
    ui = default_app()
    with open(path, "rb") as fh:
        data = fh.read()
    assert ui.ctx.add_font_memory("heading", data)
    assert ui.ctx.has_font("heading")
    # A different face than body: same text, different advance.
    assert ui.ctx.text_size("Heading", font="heading").w != ui.ctx.text_size("Heading").w


def test_label_style_setters():
    ui = default_app()
    root = ui.set_root(slikui.Column, padding=8)
    plain = root.add(slikui.Label, "Hello")
    styled = root.add(slikui.Label, "Hello", bold=True, underline=True)
    styled.set_italic().set_font("mono")
    ui.tick(0.0, 1 / 60)
    ui.render()
    assert styled.bold and styled.underline and styled.italic and not styled.strike
    assert styled.font == "mono"
    loose = slikui.Constraints.loose()
    assert styled.measure(loose).w > plain.measure(loose).w


def test_add_font_and_system_lookup():
    ui = default_app()
    assert not ui.ctx.has_font("mono")
    assert not ui.ctx.add_font("mono", "/definitely/not/a/font.ttf")
    f = slikui.find_system_font_face("mono", slikui.FontStyle.Bold)
    if not f:
        pytest.skip("no system monospace font")
    assert ui.ctx.add_font("mono", f.path, slikui.FontStyle.Bold, f.face)
    assert ui.ctx.has_font("mono")
    assert ui.ctx.text_size("mono", font="mono", bold=True).w > 0
