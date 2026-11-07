// slikui/text/font.hpp — TrueType font, glyph atlas, measure/wrap/layout.
//
// Vendored stb_truetype + stb_rect_pack are compiled with STB*_STATIC into
// this header: every TU including slikui gets its own private copy, so the
// library stays header-only with no link requirements.
//
// Fonts load from memory or file; glyphs rasterize on demand into a 512x512
// alpha8 atlas page owned by the Font. Context pushes atlas pages to the
// sink (texture ids come from Context::next_texture_id()).
#pragma once

#include "slikui/core/math.hpp"
#include "slikui/core/utf8.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#define STBRP_STATIC
#define STBTT_STATIC
#define STB_RECT_PACK_IMPLEMENTATION
#define STB_TRUETYPE_IMPLEMENTATION
#include "detail/stb_rect_pack.h"
#include "detail/stb_truetype.h"

namespace slikui::text {

constexpr int kAtlasPage = 512;

struct Glyph {
    std::uint32_t tex_id = 0; // atlas page texture
    Rect uv = {};             // atlas coords (px); empty = whitespace glyph
    Size size = {};           // bitmap size
    Vec2 bearing = {};        // pen -> bitmap top-left (y-down screen space)
    float advance = 0;        // horizontal advance
    float size_px = 0;
};

struct PlacedGlyph {
    const Glyph* glyph = nullptr;
    Vec2 pos = Vec2::zero();  // bitmap top-left in layout space
    float advance = 0;
};

struct TextLayout {
    std::vector<PlacedGlyph> glyphs;
    Size size = {};          // block width x height
    float line_height = 0;
};

class Font {
public:
    Font() = default;

    bool load_memory(const std::uint8_t* data, size_t len) {
        data_.assign(data, data + len);
        return init();
    }

    bool load_file(const std::string& path) {
        std::FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) return false;
        std::fseek(f, 0, SEEK_END);
        long n = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        data_.resize(size_t(n));
        size_t rd = std::fread(data_.data(), 1, size_t(n), f);
        std::fclose(f);
        if (rd != size_t(n)) return false;
        return init();
    }

    bool valid() const { return ok_; }

    void set_texture_id(std::uint32_t id) { atlas_id_ = id; }
    std::uint32_t atlas_id() const { return atlas_id_; }

    // --- metrics ---

    float scale(float px) const { return stbtt_ScaleForPixelHeight(&info_, px); }

    float line_height(float px) const {
        int asc = 0, desc = 0, gap = 0;
        stbtt_GetFontVMetrics(&info_, &asc, &desc, &gap);
        return float(asc - desc + gap) * scale(px);
    }

    float kern(std::uint32_t prev, std::uint32_t cp, float px) const {
        return float(stbtt_GetCodepointKernAdvance(&info_, int(prev), int(cp))) * scale(px);
    }

    // --- glyph access (rasterizes on miss) ---

    const Glyph& glyph(std::uint32_t cp, float px) {
        std::uint64_t key = (std::uint64_t(cp) << 20) | std::uint64_t(std::uint32_t(px) & 0xFFFFF);
        auto it = cache_.find(key);
        if (it != cache_.end()) return it->second;

        float sc = scale(px);
        int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&info_, int(cp), &adv, &lsb);

        Glyph g;
        g.size_px = px;
        g.tex_id = atlas_id_;
        g.advance = float(adv) * sc;

        int w = 0, h = 0;
        std::uint8_t* bmp = stbtt_GetCodepointBitmap(&info_, sc, sc, int(cp), &w, &h, nullptr, nullptr);
        if (bmp && w > 0 && h > 0) {
            int ix0 = 0, iy0 = 0, ix1 = 0, iy1 = 0;
            stbtt_GetCodepointBitmapBox(&info_, int(cp), sc, sc, &ix0, &iy0, &ix1, &iy1);
            g.size = {float(w), float(h)};
            g.bearing = {float(ix0), float(iy0)}; // y-down: iy0 < 0 above baseline
            pack_and_copy(g, bmp, w, h);
            stbtt_FreeBitmap(bmp, nullptr);
        }
        return cache_.emplace(key, g).first->second;
    }

    // --- layout: measure, wrap, and place glyphs ---

    TextLayout layout(std::string_view text, float px, float max_width = 0) const {
        TextLayout out;
        out.line_height = line_height(px);
        if (!ok_) return out;

        // Tight metrics: the first line's baseline sits on its ASCENT, not a
        // full leading down — otherwise single-line text rides low in its box
        // and every vertically-centered label (buttons, checkboxes, ...) ends
        // up visibly below center.
        float ascent = 0, descent = 0;
        {
            int asc = 0, desc = 0, gap = 0;
            stbtt_GetFontVMetrics(&info_, &asc, &desc, &gap);
            float sc = scale(px);
            ascent = float(asc) * sc;
            descent = float(-desc) * sc;
        }

        float pen_x = 0;
        float baseline = ascent;
        size_t line_start = 0;
        size_t last_space = size_t(-1);
        std::uint32_t prev = 0; // kerning state (local: layout() is const)

        auto place = [&](std::uint32_t cp) {
            if (prev && cp != ' ') pen_x += const_cast<Font*>(this)->kern(prev, cp, px);
            const Glyph& g = const_cast<Font*>(this)->glyph(cp, px);
            out.glyphs.push_back({&g, {pen_x + g.bearing.x, baseline + g.bearing.y}, g.advance});
            pen_x += g.advance;
            if (cp == ' ') last_space = out.glyphs.size() - 1;
            prev = cp;
        };

        auto newline = [&] {
            baseline += out.line_height;
            pen_x = 0;
            line_start = out.glyphs.size();
            last_space = size_t(-1);
            prev = 0;
        };

        auto wrap = [&] {
            if (last_space == size_t(-1) || last_space < line_start) {
                newline(); // no space: hard wrap
                return;
            }
            // Move glyphs after the last space onto the new line.
            float offset = out.glyphs[last_space].pos.x + out.glyphs[last_space].advance;
            for (size_t k = last_space + 1; k < out.glyphs.size(); ++k) {
                out.glyphs[k].pos.x -= offset;
                // Their baseline moves down too: pos.y was placed with the
                // previous line's baseline and must follow, or the wrapped
                // words paint on top of the first line.
                out.glyphs[k].pos.y += out.line_height;
            }
            line_start = last_space + 1;
            last_space = size_t(-1);
            baseline += out.line_height;
            pen_x = 0;
            for (size_t k = line_start; k < out.glyphs.size(); ++k) pen_x = out.glyphs[k].pos.x + out.glyphs[k].advance;
            prev = 0;
        };

        size_t i = 0;
        while (i < text.size()) {
            std::uint32_t cp = utf8_next(text, i);
            if (cp == '\n') { newline(); continue; }
            if (cp == '\r') continue;

            float next_x = pen_x;
            if (prev) next_x += const_cast<Font*>(this)->kern(prev, cp, px);
            next_x += const_cast<Font*>(this)->glyph_advance(cp, px);

            if (max_width > 0 && next_x > max_width && cp != ' ' && pen_x > 0) {
                wrap();
            }
            place(cp);
        }

        float max_right = 0;
        for (const auto& pg : out.glyphs)
            max_right = std::fmax(max_right, pg.pos.x + pg.glyph->size.w);
        // Tight height: last baseline + real descender + a hair for antialiasing.
        float pad = std::fmax(2.f, out.line_height * .04f);
        out.size = {max_right, baseline + descent + pad};
        return out;
    }

    float measure_width(std::string_view text, float px) const {
        float w = 0;
        std::uint32_t prev = 0;
        size_t i = 0;
        while (i < text.size()) {
            std::uint32_t cp = utf8_next(text, i);
            if (cp == '\n') { prev = 0; continue; }
            if (prev && !is_separator(cp)) w += kern(prev, cp, px);
            w += const_cast<Font*>(this)->glyph_advance(cp, px);
            prev = cp;
        }
        return w;
    }

    // Width of the first `n` codepoints — caret hit-testing in text fields.
    float advance_upto(std::string_view text, size_t n, float px) const {
        float w = 0;
        std::uint32_t prev = 0;
        size_t i = 0, count = 0;
        while (i < text.size() && count < n) {
            std::uint32_t cp = utf8_next(text, i);
            if (prev) w += kern(prev, cp, px);
            w += const_cast<Font*>(this)->glyph_advance(cp, px);
            prev = cp;
            ++count;
        }
        return w;
    }

    // --- atlas ---

    const std::vector<std::uint8_t>& atlas_pixels() const { return atlas_; }
    int atlas_width() const { return kAtlasPage; }
    int atlas_height() const { return kAtlasPage; }
    bool atlas_dirty() const { return atlas_dirty_; }
    void clear_dirty() { atlas_dirty_ = false; }

private:
    static bool is_separator(std::uint32_t) { return false; } // kerning across spaces is fine

    float glyph_advance(std::uint32_t cp, float px) {
        return glyph(cp, px).advance;
    }
    float glyph_advance(std::uint32_t cp, float px) const {
        int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&info_, int(cp), &adv, &lsb);
        return float(adv) * scale(px);
    }

    bool init() {
        if (!stbtt_InitFont(&info_, data_.data(), 0)) {
            ok_ = false;
            return false;
        }
        atlas_.assign(size_t(kAtlasPage) * kAtlasPage, 0);
        stbrp_init_target(&packer_, kAtlasPage, kAtlasPage, nodes_, kAtlasPage + 1);
        ok_ = true;
        atlas_dirty_ = true;
        return true;
    }

    bool pack_and_copy(Glyph& g, const std::uint8_t* bmp, int w, int h) {
        stbrp_rect r{0, w + 1, h + 1, 0, 0, 0};
        if (!stbrp_pack_rects(&packer_, &r, 1) || r.was_packed == 0) return false;
        int x = r.x, y = r.y;
        for (int dy = 0; dy < h; ++dy)
            for (int dx = 0; dx < w; ++dx)
                atlas_[size_t(y + dy) * kAtlasPage + (x + dx)] = bmp[dy * w + dx];
        g.uv = {float(x), float(y), float(w), float(h)};
        atlas_dirty_ = true;
        return true;
    }

    stbtt_fontinfo info_{};
    stbrp_context packer_{};
    stbrp_node nodes_[kAtlasPage + 1]{};
    std::vector<std::uint8_t> data_;
    std::vector<std::uint8_t> atlas_;
    std::map<std::uint64_t, Glyph> cache_;
    std::uint32_t atlas_id_ = 0;
    bool ok_ = false;
    bool atlas_dirty_ = true;
};

} // namespace slikui::text
