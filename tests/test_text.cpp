// tests/test_text.cpp - fonts, styles and the custom-widget text API:
// multi-page atlas, glyph cache keys, synthesized bold/italic, underline /
// strikethrough, font families, env.text / text_in / TextBlock, UTF-8.
#include "test.hpp"

#include "slikui/backends/software/software.hpp"
#include "slikui/context.hpp"
#include "slikui/text/default_font.hpp"
#include "slikui/text/font.hpp"
#include "slikui/text/system_fonts.hpp"
#include "slikui/widgets/all.hpp"

#include <cmath>
#include <set>
#include <string>
#include <vector>

using namespace slikui;

namespace {

struct UploadLog : IRenderSink {
    std::vector<TexUpload> uploads;
    DrawList last;
    void upload(const TexUpload& up) override { uploads.push_back(up); }
    void render(const DrawList& l, Vec2) override { last = l; }
};

bool load_default(text::Font& f) {
    return f.load_memory(text::default_font_data(), text::default_font_size());
}

struct TextFixture {
    software::Target target{320, 240};
    software::Sink sink{target};
    Context ctx{sink};
    bool has_font = false;
    TextFixture() {
        ctx.set_frame_size({320, 240});
        has_font = ctx.load_font_memory(text::default_font_data(), text::default_font_size());
    }
    void frame() {
        ctx.tick(Time{0.5, 0.016});
        ctx.render();
    }
};

size_t count(const DrawList& l, DrawCmd::Type t) {
    size_t n = 0;
    for (const auto& c : l.commands()) n += c.type == t;
    return n;
}

size_t inked(const text::TextLayout& lay) {
    size_t n = 0;
    for (const auto& g : lay.glyphs) n += !g.glyph->uv.empty();
    return n;
}

// A widget that draws text from paint() through the env API.
struct TextProbe : Widget {
    std::function<void(const PaintEnv&, Rect)> fn;
    Size measure(const Constraints& c) override { return c.clamp({300, 200}); }
    void paint(const PaintEnv& env) const override {
        if (fn) fn(env, bounds);
    }
    const char* widget_type() const override { return "TextProbe"; }
};

} // namespace

// --- UTF-8 ---

TEST(utf8_rejects_malformed_sequences) {
    auto decode = [](std::string_view s) {
        std::vector<std::uint32_t> out;
        size_t i = 0;
        while (i < s.size()) out.push_back(utf8_next(s, i));
        return out;
    };
    CHECK(decode("a\xC3\xA9") == (std::vector<std::uint32_t>{'a', 0xE9}));
    CHECK(decode("\xF0\x9F\x98\x80") == (std::vector<std::uint32_t>{0x1F600}));
    CHECK(decode("\xC0\xAF")[0] == 0xFFFD);          // overlong '/'
    CHECK(decode("\xE0\x80\xAF")[0] == 0xFFFD);      // overlong, 3 bytes
    CHECK(decode("\xED\xA0\x80")[0] == 0xFFFD);      // UTF-16 surrogate
    CHECK(decode("\xF4\x90\x80\x80")[0] == 0xFFFD);  // above U+10FFFF
    CHECK(decode("\xE2\x82") == (std::vector<std::uint32_t>{0xFFFD})); // truncated
    // A bad continuation byte is not swallowed: it decodes on its own.
    CHECK(decode("\xC3" "A") == (std::vector<std::uint32_t>{0xFFFD, 'A'}));

    CHECK(utf8_encode(0xE9) == "\xC3\xA9");
    CHECK(utf8_encode(0x20AC) == "\xE2\x82\xAC");
    CHECK(utf8_encode(0x1F600) == "\xF0\x9F\x98\x80");
    CHECK(utf8_encode(0xD800) == "\xEF\xBF\xBD"); // surrogate -> U+FFFD
}

// --- atlas / glyph cache ---

TEST(atlas_grows_pages_instead_of_dropping_glyphs) {
    text::Font font;
    REQUIRE(load_default(font));
    // Many large glyphs overflow one 512x512 page.
    std::vector<const text::Glyph*> gs;
    for (float px = 40; px <= 120; px += 8)
        for (std::uint32_t cp = 'A'; cp <= 'Z'; ++cp) gs.push_back(&font.glyph(cp, px));
    CHECK(font.atlas().pages().size() > 1);
    std::set<std::uint32_t> ids;
    for (const auto& p : font.atlas().pages()) ids.insert(p->tex_id);
    for (const auto* g : gs) CHECK(!g->uv.empty()); // every inked glyph got a cell
}

TEST(context_uploads_every_atlas_page_with_distinct_ids) {
    UploadLog sink;
    Context ctx(sink);
    ctx.set_frame_size({200, 200});
    REQUIRE(ctx.load_font_memory(text::default_font_data(), text::default_font_size()));
    for (float px = 40; px <= 120; px += 8)
        for (std::uint32_t cp = 'A'; cp <= 'Z'; ++cp) ctx.font().glyph(cp, px);
    size_t pages = ctx.font().atlas().pages().size();
    REQUIRE(pages > 1);
    ctx.render();
    std::set<std::uint32_t> ids;
    for (const auto& up : sink.uploads) {
        CHECK(up.format == TexFormat::Alpha8);
        ids.insert(up.id);
    }
    CHECK(ids.size() == pages);
    CHECK(ctx.font().atlas().pages()[0]->tex_id == ctx.font().atlas_id());

    // Clean pages are not re-sent; invalidate_textures re-sends them all.
    sink.uploads.clear();
    ctx.render();
    CHECK(sink.uploads.empty());
    ctx.invalidate_textures();
    ctx.render();
    CHECK(sink.uploads.size() == pages);
}

TEST(glyph_cache_keys_on_quarter_pixel_size) {
    text::Font font;
    REQUIRE(load_default(font));
    // These used to share one cache slot (key = int(px)).
    const text::Glyph& a = font.glyph('H', 15.2f);
    const text::Glyph& b = font.glyph('H', 15.6f);
    CHECK(&a != &b);
    CHECK(a.size_px == 15.25f);
    CHECK(b.size_px == 15.5f);
    CHECK(&font.glyph('H', 15.5f) == &b); // same quarter-px bucket
}

TEST(font_face_queries_on_a_plain_ttf) {
    const std::uint8_t* d = text::default_font_data();
    size_t n = text::default_font_size();
    CHECK(text::Font::face_count(d, n) == 1);
    CHECK(text::Font::face_style(d, n, 0) == text::FontStyle::Regular);
    CHECK(text::Font::find_face(d, n, text::FontStyle::Bold) == 0); // no match: face 0
    text::Font f;
    CHECK(!f.load_memory(d, n, 1)); // no second face in a plain TTF
    CHECK(f.load_memory(d, n, 0));
}

// --- synthesized styles + decorations ---

TEST(synth_bold_and_italic_change_glyph_rasters) {
    text::Font font;
    REQUIRE(load_default(font));
    const text::Glyph& reg = font.glyph('l', 20);
    const text::Glyph& bold = font.glyph('l', 20, text::SynthBold);
    const text::Glyph& ital = font.glyph('l', 20, text::SynthItalic);
    CHECK(&reg != &bold && &reg != &ital);
    CHECK(bold.size.w > reg.size.w);    // smeared wider
    CHECK(bold.advance > reg.advance);  // and advances further
    CHECK(ital.size.w > reg.size.w);    // a slanted stem is wider
    CHECK(ital.advance == reg.advance); // italic keeps pen positions
    CHECK(ital.size.h == reg.size.h);

    auto r = font.layout("Hello", 20);
    auto b = font.layout("Hello", 20, 0, text::SynthBold);
    CHECK(b.size.w > r.size.w);
}

TEST(underline_and_strike_add_one_bar_per_line) {
    text::Font font;
    REQUIRE(load_default(font));
    auto plain = font.layout("Hello", 18);
    CHECK(plain.decorations.empty());

    auto u = font.layout("Hello", 18, 0, text::Underline);
    REQUIRE(u.decorations.size() == 1);
    float base = u.lines[0].baseline;
    CHECK(u.decorations[0].rect.y >= base - 1.f);           // at or below the baseline
    CHECK_NEAR(u.decorations[0].rect.w, u.lines[0].width, 0.01f);
    CHECK(u.decorations[0].rect.h >= 1.f);

    auto s = font.layout("Hello", 18, 0, text::Strike);
    REQUIRE(s.decorations.size() == 1);
    CHECK(s.decorations[0].rect.bottom() < base);           // above the baseline

    // Wrapped text: one bar per line, trailing spaces excluded.
    auto w = font.layout("Hello world again", 18, 60, text::Underline | text::Strike);
    CHECK(w.lines.size() >= 2);
    CHECK(w.decorations.size() == 2 * w.lines.size());
    for (const auto& d : w.decorations) CHECK(d.rect.w <= 60.f + 0.5f);
}

TEST(layout_records_lines_and_byte_offsets) {
    text::Font font;
    REQUIRE(load_default(font));
    auto lay = font.layout("ab\ncd", 16);
    REQUIRE(lay.lines.size() == 2);
    CHECK(lay.lines[0].byte_begin == 0 && lay.lines[0].byte_end == 3);
    CHECK(lay.lines[1].byte_begin == 3 && lay.lines[1].byte_end == 5);
    CHECK(lay.lines[0].last - lay.lines[0].first == 2);
    CHECK(lay.glyphs[2].byte == 3 && lay.glyphs[2].line == 1);
    CHECK_NEAR(lay.lines[1].baseline - lay.lines[0].baseline, lay.line_height, 0.01f);

    // Word wrap: the second line starts at the wrapped word's byte.
    auto w = font.layout("hello world", 16, 50);
    REQUIRE(w.lines.size() == 2);
    CHECK(w.lines[1].byte_begin == 6);
    CHECK(w.glyphs[6].pen == 0.f); // 'w' starts its line
    // Multi-byte text keeps byte (not codepoint) offsets.
    auto m = font.layout("\xC3\xA9x", 16);
    CHECK(m.glyphs[1].byte == 2);

    // Empty lines are kept, with empty glyph ranges.
    auto e = font.layout("a\n\nb", 16);
    REQUIRE(e.lines.size() == 3);
    CHECK(e.lines[1].first == e.lines[1].last);
    CHECK(e.lines[1].width == 0.f);
}

// --- font families ---

TEST(font_families_resolve_and_synthesize_missing_styles) {
    TextFixture f;
    REQUIRE(f.has_font);
    auto r = f.ctx.fonts().resolve("", false, false);
    CHECK(r.font == &f.ctx.font() && r.synth == 0);

    // Body has no bold face yet: synthesized from regular.
    r = f.ctx.fonts().resolve("body", true, true);
    CHECK(r.font == &f.ctx.font());
    CHECK(r.synth == (text::SynthBold | text::SynthItalic));

    // Unknown families fall back to body.
    CHECK(!f.ctx.has_font("mono"));
    CHECK(f.ctx.fonts().resolve("mono").font == &f.ctx.font());

    // Register a "bold" face (same bytes, but a distinct Font object).
    REQUIRE(f.ctx.add_font_memory("body", text::default_font_data(), text::default_font_size(),
                                  text::FontStyle::Bold));
    r = f.ctx.fonts().resolve("body", true, false);
    CHECK(r.font != &f.ctx.font() && r.synth == 0);
    r = f.ctx.fonts().resolve("body", true, true); // bold italic -> bold + synth italic
    CHECK(r.synth == text::SynthItalic);

    REQUIRE(f.ctx.add_font_memory("mono", text::default_font_data(), text::default_font_size()));
    CHECK(f.ctx.has_font("mono"));
    CHECK(f.ctx.fonts().resolve("mono").font != &f.ctx.font());

    // A failed load leaves the family using its other faces.
    CHECK(!f.ctx.add_font("mono", "/not/a/font.ttf", text::FontStyle::Italic));
    CHECK(f.ctx.fonts().resolve("mono", false, true).synth == text::SynthItalic);
}

TEST(added_font_atlas_pages_reach_the_sink) {
    UploadLog sink;
    Context ctx(sink);
    ctx.set_frame_size({200, 100});
    REQUIRE(ctx.load_font_memory(text::default_font_data(), text::default_font_size()));
    REQUIRE(ctx.add_font_memory("mono", text::default_font_data(), text::default_font_size()));
    auto& probe = ctx.set_root<TextProbe>();
    probe.fn = [](const PaintEnv& env, Rect b) {
        env.text({b.x, b.y}, "body");
        env.text({b.x, b.y + 30}, "mono", {.font = "mono"});
    };
    ctx.tick(Time{0, 0.016});
    ctx.render();
    std::uint32_t mono_id = ctx.fonts().resolve("mono").font->atlas_id();
    CHECK(mono_id != ctx.font().atlas_id());
    bool uploaded = false;
    for (const auto& up : sink.uploads) uploaded |= up.id == mono_id;
    CHECK(uploaded);
    bool drawn = false;
    for (const auto& c : sink.last.commands())
        drawn |= c.type == DrawCmd::Type::Glyph && c.tex_id == mono_id;
    CHECK(drawn);
}

TEST(system_font_lookup_by_role_and_style) {
    auto sans = find_system_font_face("sans");
    SKIP_IF_UNAVAILABLE(sans, "no system sans font");
    TextFixture f;
    CHECK(f.ctx.add_font("body", sans.path, text::FontStyle::Regular, sans.face));
    if (auto bold = find_system_font_face("sans", text::FontStyle::Bold)) {
        CHECK(f.ctx.add_font("body", bold.path, text::FontStyle::Bold, bold.face));
        CHECK(f.ctx.fonts().resolve("body", true).synth == 0);
    }
    if (auto mono = find_system_font_face("mono", text::FontStyle::Bold)) {
        CHECK(f.ctx.add_font("mono", mono.path, text::FontStyle::Bold, mono.face));
        auto r = f.ctx.fonts().resolve("mono", true);
        CHECK(r.font->valid() && r.synth == 0);
    }
}

// --- Label styles ---

TEST(label_style_setters_draw_decorations) {
    TextFixture f;
    REQUIRE(f.has_font);
    auto& root = f.ctx.set_root<Column>();
    auto& plain = root.add<Label>("Hello");
    auto& styled = root.add<Label>("Hello");
    styled.set_bold().set_underline().set_strike();
    f.frame();
    CHECK(styled.bold() && styled.underline() && styled.strike() && !styled.italic());
    auto loose = Constraints::loose({1000, 1000});
    CHECK(styled.measure(loose).w > plain.measure(loose).w); // synthesized bold is wider

    DrawList list;
    PaintEnv env{list, f.ctx.theme(), f.ctx.font(), &f.ctx.fonts()};
    styled.paint(env);
    CHECK(count(list, DrawCmd::Type::Rect) == 2); // underline + strike bars
    CHECK(count(list, DrawCmd::Type::Glyph) == 5);

    styled.set_underline(false).set_strike(false);
    f.frame();
    list.clear();
    styled.paint(env);
    CHECK(count(list, DrawCmd::Type::Rect) == 0);
}

// Bounding box of the glyph quads a widget paints.
Rect glyph_bbox(const Widget& w, TextFixture& f) {
    DrawList list;
    PaintEnv env{list, f.ctx.theme(), f.ctx.font(), &f.ctx.fonts()};
    w.paint(env);
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (const auto& c : list.commands()) {
        if (c.type != DrawCmd::Type::Glyph) continue;
        x0 = std::fmin(x0, c.rect.x);
        y0 = std::fmin(y0, c.rect.y);
        x1 = std::fmax(x1, c.rect.right());
        y1 = std::fmax(y1, c.rect.bottom());
    }
    return {x0, y0, x1 - x0, y1 - y0};
}

TEST(label_align_and_valign_place_text_in_its_bounds) {
    TextFixture f;
    REQUIRE(f.has_font);
    auto& root = f.ctx.set_root<Column>();
    auto& label = root.add<Label>("Hello");
    f.frame();
    const Rect box{20, 10, 280, 100}; // much bigger than the text
    label.arrange(box);

    Rect tl = glyph_bbox(label, f); // default: left, top
    CHECK(label.align() == TextAlign::Left && label.valign() == TextVAlign::Top);
    CHECK(tl.x - box.x < 4.f);
    CHECK(tl.y - box.y < 8.f);

    label.set_align(TextAlign::Center).set_valign(TextVAlign::Middle);
    Rect mid = glyph_bbox(label, f);
    CHECK(std::fabs((mid.x - box.x) - (box.right() - mid.right())) < 3.f);
    CHECK(std::fabs((mid.y - box.y) - (box.bottom() - mid.bottom())) < 6.f);
    CHECK(std::fabs(mid.w - tl.w) < 0.01f); // moved, not re-laid out

    label.set_align(TextAlign::Right).set_valign(TextVAlign::Bottom);
    Rect br = glyph_bbox(label, f);
    CHECK(box.right() - br.right() < 4.f);
    CHECK(box.bottom() - br.bottom() < 8.f);
    CHECK(br.y > mid.y && mid.y > tl.y);

    // Taller than its bounds: stays top-aligned instead of riding up.
    label.arrange({20, 10, 280, 4});
    CHECK(glyph_bbox(label, f).y >= tl.y - 0.01f);
}

TEST(label_align_centers_each_wrapped_line) {
    TextFixture f;
    REQUIRE(f.has_font);
    auto& root = f.ctx.set_root<Column>();
    auto& label = root.add<Label>("short\na much longer second line");
    label.set_align(TextAlign::Center);
    f.frame();
    label.arrange({0, 0, 300, 80});
    DrawList list;
    PaintEnv env{list, f.ctx.theme(), f.ctx.font(), &f.ctx.fonts()};
    label.paint(env);
    // Per line: centers match the box center.
    float lo[2] = {1e9f, 1e9f}, hi[2] = {-1e9f, -1e9f}, first_y = -1;
    for (const auto& c : list.commands()) {
        if (c.type != DrawCmd::Type::Glyph) continue;
        if (first_y < 0) first_y = c.rect.y;
        int line = c.rect.y > first_y + 10 ? 1 : 0;
        lo[line] = std::fmin(lo[line], c.rect.x);
        hi[line] = std::fmax(hi[line], c.rect.right());
    }
    for (int l = 0; l < 2; ++l) CHECK(std::fabs((lo[l] + hi[l]) * .5f - 150.f) < 3.f);
    CHECK(hi[1] - lo[1] > hi[0] - lo[0] + 50.f);
}

// --- env.text / text_in ---

TEST(env_text_draws_with_theme_defaults_and_returns_its_rect) {
    TextFixture f;
    REQUIRE(f.has_font);
    Rect got{};
    auto& probe = f.ctx.set_root<TextProbe>();
    probe.fn = [&](const PaintEnv& env, Rect) { got = env.text({10, 20}, "Hello"); };
    f.frame();
    CHECK(got.x == 10 && got.y == 20);
    Size s = f.ctx.text_size("Hello");
    CHECK_NEAR(got.w, s.w, 0.01f);
    CHECK_NEAR(got.h, s.h, 0.01f);
    // Same metrics as the body font at the theme's base size.
    float px = f.ctx.theme().s(f.ctx.theme().base_font_px);
    CHECK_NEAR(s.w, f.ctx.font().measure_width("Hello", px), 0.01f);
    TextMetrics m = f.ctx.text_metrics();
    CHECK_NEAR(m.line_height, f.ctx.font().line_height(px), 0.01f);
    CHECK(m.ascent > 0 && m.descent > 0);
}

TEST(env_text_alignment_and_anchors) {
    TextFixture f;
    REQUIRE(f.has_font);
    Rect centered{}, right{}, base{}, mid{}, in_rect{};
    TextMetrics m{};
    auto& probe = f.ctx.set_root<TextProbe>();
    probe.fn = [&](const PaintEnv& env, Rect) {
        centered = env.text({100, 50}, "Hi", {.align = TextAlign::Center});
        right = env.text({100, 50}, "Hi", {.align = TextAlign::Right});
        base = env.text({100, 50}, "Hi", {.valign = TextVAlign::Baseline});
        mid = env.text({100, 50}, "Hi", {.valign = TextVAlign::Middle});
        in_rect = env.text_in({0, 0, 200, 100}, "Hi",
                              {.align = TextAlign::Center, .valign = TextVAlign::Middle});
    };
    f.frame();
    m = f.ctx.text_metrics();
    CHECK_NEAR(centered.x + centered.w * .5f, 100.f, 0.01f);
    CHECK_NEAR(right.right(), 100.f, 0.01f);
    CHECK_NEAR(base.y + m.ascent, 50.f, 0.01f);
    CHECK_NEAR(mid.y + mid.h * .5f, 50.f, 0.01f);
    CHECK_NEAR(in_rect.x + in_rect.w * .5f, 100.f, 0.01f);
    CHECK_NEAR(in_rect.y + in_rect.h * .5f, 50.f, 0.01f);
}

TEST(env_text_styles_and_overflow) {
    TextFixture f;
    REQUIRE(f.has_font);
    DrawList* list = nullptr;
    Rect ell{}, wrapped{};
    size_t glyphs_before = 0, glyphs_ell = 0, rects = 0, clips = 0;
    auto& probe = f.ctx.set_root<TextProbe>();
    probe.fn = [&](const PaintEnv& env, Rect) {
        list = &env.list;
        size_t g0 = count(env.list, DrawCmd::Type::Glyph);
        env.text({0, 0}, "underlined", {.underline = true});
        rects = count(env.list, DrawCmd::Type::Rect);
        glyphs_before = count(env.list, DrawCmd::Type::Glyph) - g0;

        size_t g1 = count(env.list, DrawCmd::Type::Glyph);
        ell = env.text_in({0, 40, 60, 20}, "a long line of text", {.overflow = TextOverflow::Ellipsis});
        glyphs_ell = count(env.list, DrawCmd::Type::Glyph) - g1;

        env.text_in({0, 80, 40, 20}, "clipped text", {.overflow = TextOverflow::Clip});
        clips = count(env.list, DrawCmd::Type::ClipPush);

        wrapped = env.text({0, 120}, "wrap me onto lines", {.wrap = true, .max_width = 60});
    };
    f.frame();
    CHECK(glyphs_before == 10);
    CHECK(rects >= 2); // background + underline bar
    CHECK(ell.w <= 60.f + 0.01f);
    CHECK(glyphs_ell > 0 && glyphs_ell < 16); // truncated + an ellipsis glyph
    CHECK(clips >= 1);
    CHECK(wrapped.w <= 60.f + 0.01f);
    CHECK(wrapped.h > f.ctx.text_metrics().line_height * 1.5f);
    Size ws = f.ctx.text_size("wrap me onto lines", {.wrap = true, .max_width = 60});
    CHECK_NEAR(ws.h, wrapped.h, 0.01f);
    (void)list;
}

TEST(env_text_cache_reuses_layouts_and_evicts_stale_ones) {
    TextFixture f;
    REQUIRE(f.has_font);
    bool draw = true;
    auto& probe = f.ctx.set_root<TextProbe>();
    probe.fn = [&](const PaintEnv& env, Rect) {
        if (draw) env.text({0, 0}, "cached");
    };
    f.frame();
    size_t n = f.ctx.fonts().cache_size();
    CHECK(n >= 1);
    for (int i = 0; i < 10; ++i) f.frame();
    CHECK(f.ctx.fonts().cache_size() == n); // same text, same entry
    draw = false;
    for (int i = 0; i < 130; ++i) f.frame();
    CHECK(f.ctx.fonts().cache_size() < n); // unused -> evicted
}

TEST(env_text_scales_with_the_theme) {
    TextFixture f;
    REQUIRE(f.has_font);
    Size s1 = f.ctx.text_size("Scale");
    f.ctx.theme().scale = 2.f;
    Size s2 = f.ctx.text_size("Scale");
    CHECK_NEAR(s1.w, s2.w, 0.75f); // logical size is (nearly) scale-independent
    float px2 = f.ctx.theme().s(f.ctx.theme().base_font_px);
    CHECK_NEAR(s2.w * 2.f, f.ctx.font().measure_width("Scale", px2), 0.01f);
}

TEST(hand_built_paint_env_text_falls_back_to_its_font) {
    TextFixture f;
    REQUIRE(f.has_font);
    DrawList list;
    PaintEnv env{list, f.ctx.theme(), f.ctx.font()}; // no FontLibrary
    Rect r = env.text({5, 5}, "Hi", {.bold = true});
    CHECK(r.w > 0 && r.h > 0);
    CHECK(count(list, DrawCmd::Type::Glyph) == 2);
}

TEST(fontless_text_api_draws_nothing_and_does_not_crash) {
    software::Target target(100, 100);
    software::Sink sink(target);
    Context ctx(sink);
    ctx.set_frame_size({100, 100});
    CHECK(ctx.text_size("hi").w == 0);
    TextBlock b = ctx.text_layout("hi", 50);
    CHECK(b.empty());
    CHECK(b.hit_test({10, 10}) == 0);
    auto& probe = ctx.set_root<TextProbe>();
    probe.fn = [](const PaintEnv& env, Rect) {
        env.text({0, 0}, "hi", {.bold = true, .underline = true});
        env.text_in({0, 0, 50, 20}, "hi", {.overflow = TextOverflow::Ellipsis});
    };
    ctx.tick(Time{0, 0.016});
    ctx.render();
}

// --- TextBlock ---

TEST(text_block_hit_test_caret_and_selection) {
    TextFixture f;
    REQUIRE(f.has_font);
    TextBlock b = f.ctx.text_layout("hello world\nnext", 0);
    REQUIRE(b.line_count() == 2);
    float lh = b.line_height();
    Size s = b.size();
    CHECK(s.w > 0 && s.h > lh);

    CHECK(b.hit_test({-5, 1}) == 0);           // left of the text
    CHECK(b.hit_test({1000, 1}) == 11);        // end of line 1: before '\n'
    CHECK(b.hit_test({1000, lh * 1.5f}) == 16);// end of the text
    CHECK(b.hit_test({0, lh * 1.5f}) == 12);   // start of line 2

    Rect c0 = b.caret_rect(0), c6 = b.caret_rect(6), c12 = b.caret_rect(12);
    CHECK(c0.x == 0 && c6.x > c0.x);
    CHECK_NEAR(c12.y, lh, 0.01f);
    CHECK(b.hit_test({c6.x + 0.1f, 1}) == 6);  // round-trip

    auto sel = b.selection_rects(6, 14);       // "world\nne"
    REQUIRE(sel.size() == 2);
    CHECK_NEAR(sel[0].x, c6.x, 0.01f);
    CHECK_NEAR(sel[1].x, 0.f, 0.01f);
    CHECK(b.selection_rects(3, 3).empty());

    // Centered blocks shift each line inside the wrap width.
    TextBlock cb = f.ctx.text_layout("ab", 200, {.align = TextAlign::Center});
    Rect cc = cb.caret_rect(0);
    CHECK_NEAR(cc.x, (200.f - cb.size().w) * .5f, 0.01f);
    CHECK(cb.hit_test({cc.x - 1, 1}) == 0);

    // Drawn through env.draw at an offset.
    DrawList list;
    PaintEnv env{list, f.ctx.theme(), f.ctx.font(), &f.ctx.fonts()};
    env.draw(b, {50, 60});
    CHECK(count(list, DrawCmd::Type::Glyph) == 14);
    CHECK(list.commands()[0].rect.x >= 50.f);
}
