// slikui/core/color.hpp — sRGB color with alpha and simple blending.
#pragma once

#include <cstdint>
#include <algorithm>
#include <array>
#include <cmath>

namespace slikui {

struct Color {
    std::uint8_t r = 0, g = 0, b = 0, a = 255;

    Color() = default;
    Color(std::uint8_t pr, std::uint8_t pg, std::uint8_t pb, std::uint8_t pa = 255)
        : r(pr), g(pg), b(pb), a(pa) {}

    static Color from_hex(std::uint32_t rgb, std::uint8_t alpha = 255) {
        return Color(std::uint8_t((rgb >> 16) & 0xFF), std::uint8_t((rgb >> 8) & 0xFF),
                     std::uint8_t(rgb & 0xFF), alpha);
    }
    static Color from_hex(std::uint32_t rgba, int /*disambiguation*/) {
        return Color(std::uint8_t((rgba >> 24) & 0xFF), std::uint8_t((rgba >> 16) & 0xFF),
                     std::uint8_t((rgba >> 8) & 0xFF), std::uint8_t(rgba & 0xFF));
    }

    // Uniform RGBA hex, e.g. rgba(0x3344FF80).
    static Color rgba(std::uint32_t v) {
        return from_hex(v >> 8, std::uint8_t(v & 0xFF));
    }

    bool operator==(const Color& o) const = default;

    Color with_alpha(float alpha) const {
        return Color(r, g, b, std::uint8_t(std::clamp(alpha, 0.f, 1.f) * 255.f));
    }

    Color faded(float f) const { return with_alpha(float(a) / 255.f * f); } // scale existing alpha

    Color lerp(Color o, float t) const {
        t = std::clamp(t, 0.f, 1.f);
        auto mix = [t](std::uint8_t x, std::uint8_t y) {
            return std::uint8_t(float(x) + (float(y) - float(x)) * t + 0.5f);
        };
        return Color(mix(r, o.r), mix(g, o.g), mix(b, o.b), mix(a, o.a));
    }

    // Returns packed 0xAABBGGRR (little-endian u32 pixel order).
    std::uint32_t to_abgr() const {
        return (std::uint32_t(a) << 24) | (std::uint32_t(b) << 16) |
               (std::uint32_t(g) << 8) | std::uint32_t(r);
    }

    static Color from_abgr(std::uint32_t v) {
        return Color(std::uint8_t(v & 0xFF), std::uint8_t((v >> 8) & 0xFF),
                     std::uint8_t((v >> 16) & 0xFF), std::uint8_t((v >> 24) & 0xFF));
    }

    // --- HSV (for color editors) -------------------------------------------------
    // h in [0,1), s/v in [0,1]. Achromatic colors return h = 0, so the
    // round-trip through from_hsv is exact for grays and the pure hues.
    std::array<float, 3> to_hsv() const {
        const float rf = r / 255.f, gf = g / 255.f, bf = b / 255.f;
        const float mx = std::fmax(rf, std::fmax(gf, bf));
        const float mn = std::fmin(rf, std::fmin(gf, bf));
        const float d = mx - mn;
        float h = 0.f;
        if (d > 0.f) {
            if (mx == rf)      h = std::fmod((gf - bf) / d, 6.f);
            else if (mx == gf) h = (bf - rf) / d + 2.f;
            else               h = (rf - gf) / d + 4.f;
            h /= 6.f;
            if (h < 0.f) h += 1.f;
        }
        return {h, mx > 0.f ? d / mx : 0.f, mx};
    }

    // h wraps (negative / >1 accepted); s/v clamp to [0,1].
    static Color from_hsv(float h, float s, float v, std::uint8_t alpha = 255) {
        h -= std::floor(h);
        s = std::clamp(s, 0.f, 1.f);
        v = std::clamp(v, 0.f, 1.f);
        float hp = std::fmin(h * 6.f, 5.999999f);
        int hi = int(hp);
        float f = hp - float(hi);
        float p = v * (1.f - s);
        float q = v * (1.f - s * f);
        float t = v * (1.f - s * (1.f - f));
        float rf, gf, bf;
        switch (hi) {
        case 0:  rf = v; gf = t; bf = p; break;
        case 1:  rf = q; gf = v; bf = p; break;
        case 2:  rf = p; gf = v; bf = t; break;
        case 3:  rf = p; gf = q; bf = v; break;
        case 4:  rf = t; gf = p; bf = v; break;
        default: rf = v; gf = p; bf = q; break;
        }
        auto ch = [](float x) {
            return std::uint8_t(std::clamp(x, 0.f, 1.f) * 255.f + 0.5f);
        };
        return Color(ch(rf), ch(gf), ch(bf), alpha);
    }

    static Color black()      { return Color(0, 0, 0); }
    static Color white()      { return Color(255, 255, 255); }
    static Color transparent(){ return Color(0, 0, 0, 0); }
};

// Straight-alpha blend of src over dst, 8-bit channels.
inline Color blend(Color dst, Color src) {
    if (src.a == 255) return src;
    if (src.a == 0)   return dst;
    std::uint32_t ia = 255 - src.a;
    auto mix = [&](std::uint8_t d, std::uint8_t s) {
        return std::uint8_t((std::uint32_t(s) * src.a + std::uint32_t(d) * ia + 127) / 255);
    };
    return Color(mix(dst.r, src.r), mix(dst.g, src.g), mix(dst.b, src.b), std::uint8_t(src.a + std::uint32_t(dst.a) * ia / 255));
}

} // namespace slikui
