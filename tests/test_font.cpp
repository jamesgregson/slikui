// tests/test_font.cpp — an invalid font must degrade to zero metrics, never
// crash. Before the ok_ guards, every Font entry point dereferenced the
// (null) stbtt font data: this test segfaulted instead of failing a CHECK.
// Reproduces the manylinux-wheel CI failure — containers ship no fonts.
#include "test.hpp"

#include "slikui/backends/software/software.hpp"
#include "slikui/context.hpp"
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
