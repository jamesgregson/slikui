// silk/style/theme.hpp — palette + metrics shared by all widgets.
//
// Widgets never hardcode colors: they read `Theme` values through
// Context::theme(), optionally overridden per-widget via style setters.
#pragma once

#include "silk/core/color.hpp"
#include "silk/core/math.hpp"

#include <cstddef>
#include <vector>

namespace silk {

struct Theme {
    // Logical px per framebuffer px. Widgets/layout think in logical px;
    // sinks multiply by `scale`. Glyphs rasterize at size*scale for crispness.
    float scale = 1.f;

    float s(float v) const { return v * scale; } // logical -> framebuffer px

    // Palette — dark default.
    Color background   = Color::from_hex(0x101418); // app/window backdrop
    Color surface      = Color::from_hex(0x1B2027); // panels, cards
    Color surface_alt  = Color::from_hex(0x242B34); // elevated, hover fills
    Color border       = Color::from_hex(0x333B45);
    Color text         = Color::from_hex(0xE8EDF2);
    Color text_dim     = Color::from_hex(0x8A94A0);
    Color accent       = Color::from_hex(0x4C8DFF); // primary action
    Color accent_text  = Color::white();
    Color danger       = Color::from_hex(0xE5484D);
    Color success      = Color::from_hex(0x46A758);
    Color warning      = Color::from_hex(0xF5A623);

    // Metrics (in framebuffer-independent logical px).
    float radius        = 6.f;   // default corner rounding
    float border_width  = 1.f;
    float gap           = 8.f;   // default spacing between children
    float base_font_px  = 15.f;
    float small_font_px = 12.f;
    float large_font_px = 20.f;

    // Derived hover/press variants so widgets stay visually consistent.
    Color hover(Color base)  const { return base.lerp(Color::white(), 0.15f); }
    Color press(Color base)  const { return base.lerp(Color::black(), 0.25f); }
    Color dimmed(Color base) const { return base.lerp(background, 0.6f); }

    void use_light() {
        background  = Color::from_hex(0xF3F4F6);
        surface     = Color::from_hex(0xFFFFFF);
        surface_alt = Color::from_hex(0xE9ECF0);
        border      = Color::from_hex(0xD3D9E0);
        text        = Color::from_hex(0x1A2027);
        text_dim    = Color::from_hex(0x6B7683);
        accent      = Color::from_hex(0x2E6BE6);
        accent_text = Color::white();
    }

    // --- Built-in presets ------------------------------------------------
    // Each returns a complete Theme with scale = 1; apps set `scale`
    // themselves (see use_theme below). Everything else is fair game —
    // presets also vary `radius` so each scheme gets its own character.
    static Theme dark() { return Theme{}; } // the default palette

    static Theme light() {
        Theme t;
        t.use_light();
        return t;
    }

    static Theme nord() { // muted arctic blues (nordtheme.com)
        Theme t;
        t.background  = Color::from_hex(0x2E3440);
        t.surface     = Color::from_hex(0x3B4252);
        t.surface_alt = Color::from_hex(0x434C5E);
        t.border      = Color::from_hex(0x4C566A);
        t.text        = Color::from_hex(0xECEFF4);
        t.text_dim    = Color::from_hex(0x9BA6B8);
        t.accent      = Color::from_hex(0x88C0D0); // frost
        t.accent_text = Color::from_hex(0x2E3440); // dark on light frost
        t.danger      = Color::from_hex(0xBF616A);
        t.success     = Color::from_hex(0xA3BE8C);
        t.warning     = Color::from_hex(0xEBCB8B);
        t.radius      = 6.f;
        return t;
    }

    static Theme dracula() { // purple-forward, high contrast
        Theme t;
        t.background  = Color::from_hex(0x282A36);
        t.surface     = Color::from_hex(0x343746);
        t.surface_alt = Color::from_hex(0x44475A); // selection
        t.border      = Color::from_hex(0x52566E);
        t.text        = Color::from_hex(0xF8F8F2);
        t.text_dim    = Color::from_hex(0x6272A4); // comment
        t.accent      = Color::from_hex(0xBD93F9); // purple
        t.accent_text = Color::from_hex(0x1B1C24);
        t.danger      = Color::from_hex(0xFF5555);
        t.success     = Color::from_hex(0x50FA7B);
        t.warning     = Color::from_hex(0xFFB86C); // orange reads as warning
        t.radius      = 8.f;
        return t;
    }

    static Theme gruvbox() { // warm retro contrast
        Theme t;
        t.background  = Color::from_hex(0x282828);
        t.surface     = Color::from_hex(0x3C3836);
        t.surface_alt = Color::from_hex(0x504945);
        t.border      = Color::from_hex(0x665C54);
        t.text        = Color::from_hex(0xEBDBB2);
        t.text_dim    = Color::from_hex(0xA89984);
        t.accent      = Color::from_hex(0xFE8019); // orange
        t.accent_text = Color::from_hex(0x1D2021);
        t.danger      = Color::from_hex(0xFB4934);
        t.success     = Color::from_hex(0xB8BB26);
        t.warning     = Color::from_hex(0xFABD2F);
        t.radius      = 4.f;
        return t;
    }

    static Theme mocha() { // Catppuccin Mocha — pastel on deep indigo
        Theme t;
        t.background  = Color::from_hex(0x1E1E2E);
        t.surface     = Color::from_hex(0x313244);
        t.surface_alt = Color::from_hex(0x45475A);
        t.border      = Color::from_hex(0x585B70);
        t.text        = Color::from_hex(0xCDD6F4);
        t.text_dim    = Color::from_hex(0x9399B2);
        t.accent      = Color::from_hex(0xF5C2E7); // pink (mauve reads too
        t.accent_text = Color::from_hex(0x1E1E2E); // close to Dracula's purple)
        t.danger      = Color::from_hex(0xF38BA8);
        t.success     = Color::from_hex(0xA6E3A1);
        t.warning     = Color::from_hex(0xF9E2AF);
        t.radius      = 8.f;
        return t;
    }

    static Theme solarized() { // classic low-contrast dark teal
        Theme t;
        t.background  = Color::from_hex(0x002B36);
        t.surface     = Color::from_hex(0x073642);
        t.surface_alt = Color::from_hex(0x0D4453);
        t.border      = Color::from_hex(0x586E75);
        t.text        = Color::from_hex(0xEEE8D5); // base2
        t.text_dim    = Color::from_hex(0x93A1A1); // base1
        t.accent      = Color::from_hex(0x268BD2); // blue
        t.accent_text = Color::white();
        t.danger      = Color::from_hex(0xDC322F);
        t.success     = Color::from_hex(0x859900);
        t.warning     = Color::from_hex(0xB58900);
        t.radius      = 3.f;
        return t;
    }

    static Theme phosphor() { // green CRT terminal — square corners
        Theme t;
        t.background  = Color::from_hex(0x050A06);
        t.surface     = Color::from_hex(0x0B140C);
        t.surface_alt = Color::from_hex(0x132416);
        t.border      = Color::from_hex(0x1D3A22);
        t.text        = Color::from_hex(0x52E884);
        t.text_dim    = Color::from_hex(0x2E8B57);
        t.accent      = Color::from_hex(0x52E884); // monochrome glow
        t.accent_text = Color::from_hex(0x05140A);
        t.danger      = Color::from_hex(0xFF6259);
        t.success     = Color::from_hex(0x52E884);
        t.warning     = Color::from_hex(0xE8C552); // amber
        t.radius      = 0.f;
        return t;
    }
};

// Apply a preset while keeping app-owned settings (`scale`). Assigning
// `ctx.theme() = Theme::nord()` would silently reset your pixel density.
inline void use_theme(Theme& into, const Theme& preset) {
    float s = into.scale;
    into = preset;
    into.scale = s;
}

struct ThemePreset {
    const char* name;
    Theme (*make)();
};

// Built-in presets in display order — handy for theme pickers.
inline const std::vector<ThemePreset>& theme_presets() {
    static const std::vector<ThemePreset> list = {
        {"Dark",      &Theme::dark},
        {"Light",     &Theme::light},
        {"Nord",      &Theme::nord},
        {"Dracula",   &Theme::dracula},
        {"Gruvbox",   &Theme::gruvbox},
        {"Mocha",     &Theme::mocha},
        {"Solarized", &Theme::solarized},
        {"Phosphor",  &Theme::phosphor},
    };
    return list;
}

inline void apply_preset(Theme& t, std::size_t index) {
    const auto& ps = theme_presets();
    use_theme(t, ps[index % ps.size()].make());
}

// Easing curves for animation.
namespace ease {
    inline float linear(float t)    { return t; }
    inline float out_cubic(float t) { float u = 1 - t; return 1 - u * u * u; }
    inline float in_out(float t)    { return t < .5f ? 2 * t * t : 1 - powf(2 - 2 * t, 2) / 2; }
    inline float out_quint(float t) { float u = t - 1; return 1 + u * u * u * u * u; }
} // namespace ease

} // namespace silk
