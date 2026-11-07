// tests/test_theme.cpp — built-in theme presets and use_theme/apply_preset.
#include "test.hpp"

#include <silk/style/theme.hpp>

using silk::Color;
using silk::Theme;

TEST(theme_presets_have_usable_palettes) {
    for (const auto& p : silk::theme_presets()) {
        Theme t = p.make();
        CHECK(t.background != t.text);        // text must contrast the backdrop
        CHECK(t.background != t.surface);     // cards must lift off the backdrop
        CHECK(t.background != t.accent);
        CHECK(t.background.a == 255 && t.text.a == 255); // fully opaque
        CHECK(t.text_dim != t.text);
    }
}

TEST(theme_presets_are_distinct) {
    const auto& ps = silk::theme_presets();
    for (size_t i = 0; i < ps.size(); ++i)
        for (size_t j = i + 1; j < ps.size(); ++j)
            CHECK(ps[i].make().background != ps[j].make().background);
}

TEST(use_theme_preserves_scale) {
    Theme t;
    t.scale = 2.f;
    silk::use_theme(t, Theme::dracula());
    CHECK(t.scale == 2.f); // app-owned pixel density survives
    CHECK(t.accent == Color::from_hex(0xBD93F9));
}

TEST(apply_preset_indexes_and_wraps) {
    Theme t;
    silk::apply_preset(t, 2); // Nord
    CHECK(t.background == Color::from_hex(0x2E3440));
    silk::apply_preset(t, silk::theme_presets().size() + 1); // wraps to Light
    CHECK(t.background == Color::from_hex(0xF3F4F6));
}

TEST(theme_helpers_derive_consistently) {
    Theme t = Theme::dark();
    Color base = t.surface;
    Color h = t.hover(base), pr = t.press(base);
    CHECK(h.r >= base.r && h.g >= base.g && h.b >= base.b); // hover lightens
    CHECK(pr.r <= base.r && pr.g <= base.g && pr.b <= base.b); // press darkens
    Color d = t.dimmed(t.accent); // lerps toward background
    CHECK(d != t.accent);
}
