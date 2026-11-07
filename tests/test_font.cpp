// tests/test_font.cpp — an invalid font must degrade to zero metrics, never
// crash. Before the ok_ guards, every Font entry point dereferenced the
// (null) stbtt font data: this test segfaulted instead of failing a CHECK.
// Reproduces the manylinux-wheel CI failure — containers ship no fonts.
#include "test.hpp"

#include "slikui/backends/software/software.hpp"
#include "slikui/context.hpp"
#include "slikui/text/default_font.hpp"
#include "slikui/text/font.hpp"
#include "slikui/text/system_fonts.hpp"
#include "slikui/widgets/all.hpp"

using namespace slikui;

TEST(fontless_font_degrades_to_zero_metrics) {
    text::Font font; // never loaded
    CHECK(!font.valid());
    CHECK(!font.load_file("/definitely/not/a/real/font.ttf"));
    CHECK(!font.valid());

    // Metrics are zero; layout is empty; glyphs are whitespace.
    CHECK(font.scale(16.f) == 0.f);
    CHECK(font.line_height(16.f) == 0.f);
    CHECK(font.kern('a', 'b', 16.f) == 0.f);
    CHECK(font.measure_width("hello", 16.f) == 0.f);
    CHECK(font.advance_upto("hello", 3, 16.f) == 0.f);

    text::TextLayout lay = font.layout("hello world", 16.f);
    CHECK(lay.glyphs.empty());
    CHECK(lay.size.w == 0.f && lay.size.h == 0.f);
    CHECK(lay.line_height == 0.f);

    const text::Glyph& g = font.glyph('x', 16.f);
    CHECK(g.uv.w == 0.f && g.uv.h == 0.f);
}

TEST(fontless_context_ticks_and_renders) {
    // The full pipeline with a font that never loaded: widgets measure to
    // zero-size text and everything else lays out normally.
    software::Target target(120, 80);
    software::Sink sink(target);
    Context ctx(sink);
    CHECK(!ctx.font().valid());

    auto& root = ctx.set_root<Column>();
    root.set_padding(8).set_gap(4);
    root.add<Label>("no font here");
    root.add<Button>("still fine");

    ctx.set_frame_size({120, 80});
    ctx.tick(Time{0, 1.f / 60});
    ctx.render();
    CHECK(target.data() != nullptr); // rendered without crashing
}

TEST(fontless_list_widgets_render_with_items) {
    // ListView/Tabs cache one TextLayout per item and paint() indexes that
    // cache. Fontless, the cache used to stay empty -> out-of-bounds read in
    // paint(). This is the wheel-CI crash in test_dropdown_popup_opens_
    // selects_closes: the Dropdown popup is a ListView, painted while open.
    software::Target target(240, 200);
    software::Sink sink(target);
    Context ctx(sink);
    CHECK(!ctx.font().valid());

    auto& root = ctx.set_root<Column>();
    root.set_padding(8).set_gap(4);
    auto& dd = root.add<Dropdown>().set_items({"Cube", "Sphere", "Torus"});
    root.add<ListView>().set_items({"one", "two", "three"}).set_selected(1);
    root.add<Tabs>().set_tabs({"A", "B", "C"});

    ctx.set_frame_size({240, 200});
    ctx.tick(Time{0, 1.f / 60});
    ctx.render();

    Event press{};
    press.type = Event::Type::MousePress;
    press.button = MouseLeft;
    press.pos = dd.bounds.center();
    ctx.handle(press);
    CHECK(ctx.has_popups());

    // Lay out and paint the open popup.
    ctx.tick(Time{1.f / 60, 1.f / 60});
    ctx.render();

    Event move{};
    move.type = Event::Type::MouseMove;
    move.pos = {20, 60};
    ctx.handle(move);
    ctx.render();
    CHECK(target.data() != nullptr); // reached here without crashing
}

TEST(bundled_default_font_loads_and_covers_ui_text) {
    CHECK(text::default_font_size() > 10000);
    text::Font font;
    CHECK(font.load_memory(text::default_font_data(), text::default_font_size()));
    CHECK(font.valid());
    CHECK(font.line_height(16.f) > 10.f);

    text::TextLayout lay = font.layout("Hello", 16.f);
    CHECK(lay.glyphs.size() == 5);
    CHECK(lay.size.w > 20.f);

    // Subset coverage: Latin-1 and General Punctuation have real ink;
    // a codepoint outside the subset falls back to .notdef, not a crash.
    CHECK(!font.glyph(0x00E9, 16.f).uv.empty()); // é
    CHECK(!font.glyph(0x2014, 16.f).uv.empty()); // em dash
    CHECK(!font.glyph(0x2713, 16.f).uv.empty()); // check mark
    font.glyph(0x4E2D, 16.f);                    // CJK: not in the subset

    // GPOS kerning survived subsetting: "AV" is tighter than A + V.
    CHECK(font.kern('A', 'V', 16.f) < 0.f);
}
