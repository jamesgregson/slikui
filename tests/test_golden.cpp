// tests/test_golden.cpp — pixel checks through the software backend.
#include "test.hpp"

#include "slikui/backends/software/software.hpp"
#include "slikui/context.hpp"
#include "slikui/text/default_font.hpp"
#include "slikui/widgets/all.hpp"

#include <cstddef>

namespace {

struct Golden {
    slikui::software::Target target{160, 120};
    slikui::software::Sink sink{target};
    slikui::Context ctx{sink};
    bool has_font = false;

    Golden() {
        ctx.set_frame_size({160, 120});
        // Bundled font: identical text on every platform, even fontless
        // containers (manylinux), so text checks never skip.
        has_font = ctx.load_font_memory(slikui::text::default_font_data(),
                                        slikui::text::default_font_size());
    }

    void frame() {
        ctx.tick(slikui::Time{0.5, 0.016});
        ctx.render();
    }

    slikui::Color pixel(int x, int y) const {
        return slikui::Color::from_abgr(target.at(x, y));
    }

    unsigned count_non_background(const slikui::Rect& r, slikui::Color bg) const {
        unsigned n = 0;
        for (int y = int(r.y); y < r.bottom(); ++y)
            for (int x = int(r.x); x < r.right(); ++x)
                if (pixel(x, y) != bg) ++n;
        return n;
    }
};

} // namespace


TEST(golden_background_fill) {
    Golden g;
    g.ctx.set_root<slikui::Column>();
    g.frame();
    CHECK(g.pixel(0, 0) == g.ctx.theme().background);
    CHECK(g.pixel(159, 119) == g.ctx.theme().background);
}

TEST(golden_primary_button_fill) {
    Golden g;
    auto& root = g.ctx.set_root<slikui::Column>();
    auto& btn = root.add<slikui::Button>("Go").set_variant(slikui::Button::Variant::Primary);
    g.frame();

    slikui::Vec2 c = btn.bounds.center();
    // Sample the left padding strip (center can land on a glyph).
    CHECK(g.pixel(int(btn.bounds.x) + 4, int(c.y)) == g.ctx.theme().accent);
}

TEST(golden_button_rounded_corner_transparent) {
    Golden g;
    auto& root = g.ctx.set_root<slikui::Column>();
    auto& btn = root.add<slikui::Button>("Go").set_variant(slikui::Button::Variant::Primary);
    g.frame();

    // Exactly at the frame corner, the SDF coverage is 0 -> background shows.
    slikui::Color bg = g.ctx.theme().background;
    CHECK(g.pixel(int(btn.bounds.x), int(btn.bounds.y)) == bg);
    CHECK(g.pixel(int(btn.bounds.right()) - 1, int(btn.bounds.y)) == bg);
}

TEST(golden_label_draws_glyphs) {
    Golden g;
    auto& root = g.ctx.set_root<slikui::Column>();
    auto& label = root.add<slikui::Label>("Hello");
    g.frame();

    unsigned drawn = g.count_non_background(label.bounds, g.ctx.theme().background);
    CHECK(drawn > 10); // glyph ink present
}

TEST(golden_scrollarea_clips_content) {
    Golden g;
    auto& root = g.ctx.set_root<slikui::Column>();
    auto& scroll = root.add<slikui::ScrollArea>();
    scroll.hint_w_ = 100;
    scroll.hint_h_ = 40;
    scroll.add<slikui::Label>("clipped row");
    g.frame();

    // Just below the scroll viewport, nothing may be drawn.
    slikui::Color bg = g.ctx.theme().background;
    int y_below = int(scroll.bounds.bottom()) + 2;
    if (y_below < 120) {
        bool clear = true;
        for (int x = int(scroll.bounds.x); x < scroll.bounds.right() && x < 160; ++x)
            if (g.pixel(x, y_below) != bg) clear = false;
        CHECK(clear);
    }
}

TEST(golden_progress_bar_fill) {
    Golden g;
    auto& root = g.ctx.set_root<slikui::Column>();
    auto& bar = root.add<slikui::ProgressBar>().set_value(0.5f);
    g.frame();

    slikui::Color bg = g.ctx.theme().background;
    // Left half is accent-filled...
    bool left_inked = g.count_non_background(
                          {bar.bounds.x, bar.bounds.y, bar.bounds.w * .5f, bar.bounds.h},
                          bg) > 0;
    CHECK(left_inked);
    // ...and the bar's own track color sits on the right half.
    bool right_inked = g.count_non_background(
                           {bar.bounds.x + bar.bounds.w * .6f, bar.bounds.y,
                            bar.bounds.w * .35f, bar.bounds.h},
                           bg) > 0;
    CHECK(right_inked);
}

TEST(golden_glyph_fully_below_clip_paints_nothing) {
    // A glyph starting at a fractional y just past the clip's bottom edge
    // intersects to an empty rect. draw_glyph used to loop from int(clip.y)
    // anyway and paint one row outside the clip (seen as a ScrollArea's
    // next line bleeding 1px below the viewport with DejaVu metrics).
    slikui::software::Target target(16, 16);
    slikui::software::Sink sink(target);
    std::uint8_t ink[4 * 4];
    for (auto& a : ink) a = 255;
    sink.upload({7, 4, 4, slikui::TexFormat::Alpha8, ink});

    slikui::Color bg{0, 0, 0, 255};
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) target.at(x, y) = bg.to_abgr();

    slikui::DrawList list;
    list.push_clip({0, 0, 16, 9});                                   // rows 0..8
    list.glyph({2, 9.961f, 4, 4}, 7, {0, 0, 4, 4}, {255, 255, 255, 255}); // starts below
    list.image({8, 9.5f, 4, 4}, 7, {0, 0, 4, 4});
    list.pop_clip();
    sink.render(list, {16, 16});

    bool clean = true;
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x)
            if (slikui::Color::from_abgr(target.at(x, y)) != bg) clean = false;
    CHECK(clean);
}

TEST(golden_glyph_never_samples_its_atlas_neighbor) {
    // Atlas row: an empty 2x2 glyph at x 0..1, the 1px gutter at x 2, and a
    // solid neighbor at x 3..4 (font.hpp packs glyphs exactly like this).
    // Drawn at a fractional position the empty glyph must paint nothing.
    // The sampler used to put texel centers at integers, so the last column
    // reached x 3 and the neighbor bled in as a faint bar (seen at 1x).
    slikui::software::Target target(16, 16);
    slikui::software::Sink sink(target);
    std::uint8_t atlas[5 * 3] = {};
    for (int y = 0; y < 2; ++y) atlas[y * 5 + 3] = atlas[y * 5 + 4] = 255;
    sink.upload({7, 5, 3, slikui::TexFormat::Alpha8, atlas});

    slikui::Color bg{0, 0, 0, 255};
    for (float off : {0.f, 0.3f, 0.5f, 0.7f}) {
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) target.at(x, y) = bg.to_abgr();
        slikui::DrawList list;
        list.glyph({4 + off, 4 + off, 2, 2}, 7, {0, 0, 2, 2}, {255, 255, 255, 255});
        sink.render(list, {16, 16});
        bool clean = true;
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x)
                if (slikui::Color::from_abgr(target.at(x, y)) != bg) clean = false;
        CHECK(clean);
    }
}
