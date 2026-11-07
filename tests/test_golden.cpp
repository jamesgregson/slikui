// tests/test_golden.cpp — pixel checks through the software backend.
#include "test.hpp"

#include "silk/backends/software/software.hpp"
#include "silk/context.hpp"
#include "silk/text/system_fonts.hpp"
#include "silk/widgets/all.hpp"

#include <cstddef>

namespace {

struct Golden {
    silk::software::Target target{160, 120};
    silk::software::Sink sink{target};
    silk::Context ctx{sink};
    bool has_font = false;

    Golden() {
        ctx.set_frame_size({160, 120});
        has_font = ctx.load_font_file(silk::find_system_font());
    }

    void frame() {
        ctx.tick(silk::Time{0.5, 0.016});
        ctx.render();
    }

    silk::Color pixel(int x, int y) const {
        return silk::Color::from_abgr(target.at(x, y));
    }

    unsigned count_non_background(const silk::Rect& r, silk::Color bg) const {
        unsigned n = 0;
        for (int y = int(r.y); y < r.bottom(); ++y)
            for (int x = int(r.x); x < r.right(); ++x)
                if (pixel(x, y) != bg) ++n;
        return n;
    }
};

} // namespace

// Shared with tests that need text rendering.
static bool g_font_available() {
    static bool ok = !silk::find_system_font().empty();
    return ok;
}

TEST(golden_background_fill) {
    Golden g;
    g.ctx.set_root<silk::Column>();
    g.frame();
    CHECK(g.pixel(0, 0) == g.ctx.theme().background);
    CHECK(g.pixel(159, 119) == g.ctx.theme().background);
}

TEST(golden_primary_button_fill) {
    Golden g;
    auto& root = g.ctx.set_root<silk::Column>();
    auto& btn = root.add<silk::Button>("Go").set_variant(silk::Button::Variant::Primary);
    g.frame();

    silk::Vec2 c = btn.bounds.center();
    // Sample the left padding strip (center can land on a glyph).
    CHECK(g.pixel(int(btn.bounds.x) + 4, int(c.y)) == g.ctx.theme().accent);
}

TEST(golden_button_rounded_corner_transparent) {
    Golden g;
    auto& root = g.ctx.set_root<silk::Column>();
    auto& btn = root.add<silk::Button>("Go").set_variant(silk::Button::Variant::Primary);
    g.frame();

    // Exactly at the frame corner, the SDF coverage is 0 -> background shows.
    silk::Color bg = g.ctx.theme().background;
    CHECK(g.pixel(int(btn.bounds.x), int(btn.bounds.y)) == bg);
    CHECK(g.pixel(int(btn.bounds.right()) - 1, int(btn.bounds.y)) == bg);
}

TEST(golden_label_draws_glyphs) {
    SKIP_IF_UNAVAILABLE(g_font_available(), "no system font");
    Golden g;
    auto& root = g.ctx.set_root<silk::Column>();
    auto& label = root.add<silk::Label>("Hello");
    g.frame();

    unsigned drawn = g.count_non_background(label.bounds, g.ctx.theme().background);
    CHECK(drawn > 10); // glyph ink present
}

TEST(golden_scrollarea_clips_content) {
    SKIP_IF_UNAVAILABLE(g_font_available(), "no system font");
    Golden g;
    auto& root = g.ctx.set_root<silk::Column>();
    auto& scroll = root.add<silk::ScrollArea>();
    scroll.hint_w_ = 100;
    scroll.hint_h_ = 40;
    scroll.add<silk::Label>("clipped row");
    g.frame();

    // Just below the scroll viewport, nothing may be drawn.
    silk::Color bg = g.ctx.theme().background;
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
    auto& root = g.ctx.set_root<silk::Column>();
    auto& bar = root.add<silk::ProgressBar>().set_value(0.5f);
    g.frame();

    silk::Color bg = g.ctx.theme().background;
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
