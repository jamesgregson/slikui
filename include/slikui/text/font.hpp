// slikui/text/font.hpp - TrueType font face, glyph atlas, measure/wrap/layout.
//
// Vendored stb_truetype + stb_rect_pack are compiled with STB*_STATIC into
// this header: every TU including slikui gets its own private copy, so the
// library stays header-only with no link requirements.
//
// A Font is one face (one TTF/OTF, or one face of a TTC collection). Glyphs
// rasterize on demand into the font's GlyphAtlas: alpha8 pages of 512x512
// that grow one page at a time as they fill. Context pushes dirty pages to
// the sink (texture ids come from Context::next_texture_id()).
//
// Missing bold/italic faces are synthesized per glyph (SynthBold smears the
// coverage horizontally, SynthItalic shears the outline before rasterizing);
// the synthesis flags are part of the glyph cache key.
#pragma once

#include "slikui/core/math.hpp"
#include "slikui/core/utf8.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
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
constexpr int kMaxAtlasPages = 32; // 8 MB of alpha8: a hard stop, not a budget

// Which face of a family a Font is (Context font registration).
enum class FontStyle : std::uint8_t { Regular = 0, Bold = 1, Italic = 2, BoldItalic = 3 };

inline constexpr bool is_bold(FontStyle s) { return (int(s) & 1) != 0; }
inline constexpr bool is_italic(FontStyle s) { return (int(s) & 2) != 0; }
inline constexpr FontStyle make_style(bool bold, bool italic) {
    return FontStyle((bold ? 1 : 0) | (italic ? 2 : 0));
}

// Layout flags: synthesis (applied to glyph rasters) + decorations.
enum StyleFlag : std::uint8_t {
    SynthBold = 1,   // embolden glyphs (no real bold face)
    SynthItalic = 2, // slant glyphs (no real italic face)
    Underline = 4,
    Strike = 8,
};
constexpr std::uint8_t kSynthMask = SynthBold | SynthItalic;

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
    float pen = 0;            // pen x (glyph origin) in layout space
    std::uint32_t byte = 0;   // offset of this codepoint in the source text
    std::uint32_t line = 0;   // index into TextLayout::lines
};

// One laid-out line. Glyphs [first, last) belong to it; it covers source
// bytes [byte_begin, byte_end) - including a terminating '\n', if any.
struct TextLine {
    float baseline = 0;       // y of the baseline in layout space
    float width = 0;          // advance width, trailing spaces excluded
    std::uint32_t first = 0, last = 0;
    std::uint32_t byte_begin = 0, byte_end = 0;
};

// An underline / strikethrough bar, in layout space, for one line.
struct TextDecoration {
    Rect rect = {};
    std::uint32_t line = 0;
};

struct TextLayout {
    std::vector<PlacedGlyph> glyphs;
    std::vector<TextLine> lines;
    std::vector<TextDecoration> decorations;
    Size size = {};          // block width x height
    float line_height = 0;
    float ascent = 0;        // first baseline below the block top
    float descent = 0;       // positive, below each baseline
};

// Growable set of alpha8 atlas pages. Page texture ids come from an id
// source (Context::next_texture_id); page 0 can be given a fixed id.
class GlyphAtlas {
public:
    struct Page {
        std::uint32_t tex_id = 0;
        std::vector<std::uint8_t> pixels;
        stbrp_context packer{};
        std::vector<stbrp_node> nodes;
        bool dirty = true;
    };

    void set_id_source(std::function<std::uint32_t()> f) { id_source_ = std::move(f); }

    // Fixed id for page 0 (created on demand, so this also works first).
    void set_first_id(std::uint32_t id) {
        first_id_ = id;
        if (!pages_.empty()) pages_[0]->tex_id = id;
    }
    std::uint32_t first_id() const { return pages_.empty() ? first_id_ : pages_[0]->tex_id; }

    // Drops every page (a font reload): page 0 keeps its id.
    void reset() {
        if (!pages_.empty()) first_id_ = pages_[0]->tex_id;
        pages_.clear();
        add_page();
    }

    // Packs a w x h coverage bitmap; fills g.uv / g.tex_id. False when every
    // page (up to kMaxAtlasPages) is full - the glyph then draws as blank.
    bool add(const std::uint8_t* bmp, int w, int h, Glyph& g) {
        if (pages_.empty()) add_page();
        if (pack_into(*pages_.back(), bmp, w, h, g)) return true;
        if (int(pages_.size()) >= kMaxAtlasPages || w + 1 > kAtlasPage || h + 1 > kAtlasPage)
            return false;
        add_page();
        return pack_into(*pages_.back(), bmp, w, h, g);
    }

    const std::vector<std::unique_ptr<Page>>& pages() const { return pages_; }
    std::vector<std::unique_ptr<Page>>& pages() { return pages_; }

    bool dirty() const {
        for (const auto& p : pages_)
            if (p->dirty) return true;
        return false;
    }
    void mark_dirty() {
        for (auto& p : pages_) p->dirty = true;
    }
    void clear_dirty() {
        for (auto& p : pages_) p->dirty = false;
    }

private:
    void add_page() {
        auto p = std::make_unique<Page>();
        if (pages_.empty()) p->tex_id = first_id_ ? first_id_ : (id_source_ ? id_source_() : 0);
        else p->tex_id = id_source_ ? id_source_() : 0;
        if (pages_.empty()) first_id_ = p->tex_id;
        p->pixels.assign(size_t(kAtlasPage) * kAtlasPage, 0);
        p->nodes.resize(kAtlasPage + 1);
        stbrp_init_target(&p->packer, kAtlasPage, kAtlasPage, p->nodes.data(), kAtlasPage + 1);
        p->dirty = true;
        pages_.push_back(std::move(p));
    }

    static bool pack_into(Page& p, const std::uint8_t* bmp, int w, int h, Glyph& g) {
        stbrp_rect r{0, w + 1, h + 1, 0, 0, 0};
        if (!stbrp_pack_rects(&p.packer, &r, 1) || r.was_packed == 0) return false;
        int x = r.x, y = r.y;
        for (int dy = 0; dy < h; ++dy)
            for (int dx = 0; dx < w; ++dx)
                p.pixels[size_t(y + dy) * kAtlasPage + (x + dx)] = bmp[dy * w + dx];
        g.uv = {float(x), float(y), float(w), float(h)};
        g.tex_id = p.tex_id;
        p.dirty = true;
        return true;
    }

    std::vector<std::unique_ptr<Page>> pages_;
    std::function<std::uint32_t()> id_source_;
    std::uint32_t first_id_ = 0;
};

// Faux-italic slant (x += y * kSlant in font units, sheared about the
// baseline so pen positions are unchanged): about 11 degrees.
constexpr float kSynthSlant = 0.2f;

// Faux-bold smear width in raster px for a given size.
inline float synth_bold_px(float px) { return std::fmax(1.f, px / 16.f); }

// Font sizes quantize to 1/4 px: one glyph raster per quarter pixel.
inline float quantize_px(float px) { return std::round(px * 4.f) / 4.f; }

class Font {
public:
    Font() = default;
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    // `face` picks a face inside a TrueType collection (.ttc); 0 otherwise.
    bool load_memory(const std::uint8_t* data, size_t len, int face = 0) {
        data_.assign(data, data + len);
        return init(face);
    }

    bool load_file(const std::string& path, int face = 0) {
        std::FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) return false;
        std::fseek(f, 0, SEEK_END);
        long n = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (n <= 0) {
            std::fclose(f);
            return false;
        }
        data_.resize(size_t(n));
        size_t rd = std::fread(data_.data(), 1, size_t(n), f);
        std::fclose(f);
        if (rd != size_t(n)) return false;
        return init(face);
    }

    bool valid() const { return ok_; }

    // --- atlas texture ids (Context wires these) ---

    void set_texture_id(std::uint32_t id) { atlas_.set_first_id(id); }
    void set_texture_source(std::function<std::uint32_t()> f) { atlas_.set_id_source(std::move(f)); }
    std::uint32_t atlas_id() const { return atlas_.first_id(); }

    // --- metrics ---

    // All entry points tolerate an invalid font (load failed / never loaded):
    // metrics come back zero and glyphs are whitespace, so a fontless UI
    // degrades to text-less layout instead of crashing - e.g. minimal Linux
    // containers have no system fonts (find_system_font() returns "").
    float scale(float px) const {
        return ok_ ? stbtt_ScaleForPixelHeight(&info_, quantize_px(px)) : 0.f;
    }

    float line_height(float px) const {
        if (!ok_) return 0;
        return float(asc_ - desc_ + gap_) * scale(px);
    }
    float ascent(float px) const { return ok_ ? float(asc_) * scale(px) : 0.f; }
    float descent(float px) const { return ok_ ? float(-desc_) * scale(px) : 0.f; }

    float kern(std::uint32_t prev, std::uint32_t cp, float px) const {
        if (!ok_) return 0;
        return float(stbtt_GetGlyphKernAdvance(&info_, index(prev), index(cp))) * scale(px);
    }

    // True if the font has an outline for `cp` (false for an invalid font).
    // Missing codepoints otherwise render as the font's .notdef box.
    bool has_glyph(std::uint32_t cp) const { return ok_ && index(cp) != 0; }

    // Underline / strikethrough placement for `px`: offsets are measured
    // DOWN from the baseline (strike_offset is negative: above it).
    struct Decor {
        float underline_offset = 0, underline_thickness = 0;
        float strike_offset = 0, strike_thickness = 0;
    };
    Decor decor(float px) const {
        Decor d;
        if (!ok_) return d;
        float sc = scale(px);
        float fallback_thick = std::fmax(1.f, quantize_px(px) / 14.f);
        d.underline_thickness = upos_thick_ > 0 ? float(upos_thick_) * sc : fallback_thick;
        d.underline_offset = upos_ != 0 ? float(-upos_) * sc : float(-desc_) * sc * .35f;
        d.strike_thickness = strike_thick_ > 0 ? float(strike_thick_) * sc : d.underline_thickness;
        d.strike_offset = strike_pos_ > 0 ? -float(strike_pos_) * sc : -float(asc_) * sc * .3f;
        d.underline_thickness = std::fmax(1.f, d.underline_thickness);
        d.strike_thickness = std::fmax(1.f, d.strike_thickness);
        return d;
    }

    // --- glyph access (rasterizes on miss) ---

    // `synth` is SynthBold | SynthItalic (other bits are ignored).
    const Glyph& glyph(std::uint32_t cp, float px, std::uint8_t synth = 0) {
        if (!ok_) {
            static const Glyph empty;
            return empty;
        }
        return glyph_index(index(cp), px, synth);
    }

    // --- layout: measure, wrap, and place glyphs ---

    // `flags` is any StyleFlag combination: synthesis + decorations.
    TextLayout layout(std::string_view text, float px, float max_width = 0,
                      std::uint8_t flags = 0) const {
        TextLayout out;
        if (!ok_) return out; // before any metrics call - see the class comment
        Font& self = const_cast<Font&>(*this); // glyph cache is a mutable detail
        std::uint8_t synth = flags & kSynthMask;
        out.line_height = line_height(px);

        // Tight metrics: the first line's baseline sits on its ASCENT, not a
        // full leading down - otherwise single-line text rides low in its box
        // and every vertically-centered label (buttons, checkboxes, ...) ends
        // up visibly below center.
        float ascent = 0, descent = 0;
        {
            float sc = scale(px);
            ascent = float(asc_) * sc;
            descent = float(-desc_) * sc;
        }
        out.ascent = ascent;
        out.descent = descent;

        float pen_x = 0;
        float baseline = ascent;
        size_t line_start = 0;
        size_t last_space = size_t(-1);
        int prev = 0; // kerning state: previous glyph index (0 = none)

        out.lines.push_back({baseline, 0, 0, 0, 0, 0});

        auto place = [&](std::uint32_t cp, int gi, std::uint32_t byte) {
            if (prev && cp != ' ') pen_x += self.kern_index(prev, gi, px);
            const Glyph& g = self.glyph_index(gi, px, synth);
            out.glyphs.push_back({&g, {pen_x + g.bearing.x, baseline + g.bearing.y}, g.advance,
                                  pen_x, byte, std::uint32_t(out.lines.size() - 1)});
            pen_x += g.advance;
            if (cp == ' ') last_space = out.glyphs.size() - 1;
            prev = gi;
        };

        auto newline = [&](std::uint32_t next_byte) {
            baseline += out.line_height;
            pen_x = 0;
            line_start = out.glyphs.size();
            last_space = size_t(-1);
            prev = 0;
            out.lines.push_back({baseline, 0, 0, 0, next_byte, 0});
        };

        auto wrap = [&](std::uint32_t byte) {
            if (last_space == size_t(-1) || last_space < line_start) {
                newline(byte); // no space: hard wrap
                return;
            }
            // Move glyphs after the last space onto the new line.
            float offset = out.glyphs[last_space].pos.x + out.glyphs[last_space].advance;
            float pen_offset = out.glyphs[last_space].pen + out.glyphs[last_space].advance;
            std::uint32_t next_line = std::uint32_t(out.lines.size());
            for (size_t k = last_space + 1; k < out.glyphs.size(); ++k) {
                out.glyphs[k].pos.x -= offset;
                out.glyphs[k].pen -= pen_offset;
                // Their baseline moves down too: pos.y was placed with the
                // previous line's baseline and must follow, or the wrapped
                // words paint on top of the first line.
                out.glyphs[k].pos.y += out.line_height;
                out.glyphs[k].line = next_line;
            }
            std::uint32_t begin = last_space + 1 < out.glyphs.size()
                                      ? out.glyphs[last_space + 1].byte
                                      : byte;
            line_start = last_space + 1;
            last_space = size_t(-1);
            baseline += out.line_height;
            pen_x = 0;
            for (size_t k = line_start; k < out.glyphs.size(); ++k) pen_x = out.glyphs[k].pos.x + out.glyphs[k].advance;
            prev = 0;
            out.lines.push_back({baseline, 0, 0, 0, begin, 0});
        };

        size_t i = 0;
        while (i < text.size()) {
            std::uint32_t byte = std::uint32_t(i);
            std::uint32_t cp = utf8_next(text, i);
            if (cp == '\n') { newline(std::uint32_t(i)); continue; }
            if (cp == '\r') continue;
            int gi = index(cp);

            float next_x = pen_x;
            if (prev) next_x += self.kern_index(prev, gi, px);
            next_x += self.glyph_index(gi, px, synth).advance;

            if (max_width > 0 && next_x > max_width && cp != ' ' && pen_x > 0) {
                wrap(byte);
            }
            place(cp, gi, byte);
        }

        finish_lines(out, text);

        float max_right = 0;
        for (const auto& pg : out.glyphs)
            max_right = std::fmax(max_right, pg.pos.x + pg.glyph->size.w);
        // Tight height: last baseline + real descender + a hair for antialiasing.
        float pad = std::fmax(2.f, out.line_height * .04f);
        out.size = {max_right, baseline + descent + pad};

        if (flags & (Underline | Strike)) add_decorations(out, px, flags);
        return out;
    }

    float measure_width(std::string_view text, float px, std::uint8_t synth = 0) const {
        if (!ok_) return 0;
        Font& self = const_cast<Font&>(*this);
        float w = 0;
        int prev = 0;
        size_t i = 0;
        while (i < text.size()) {
            std::uint32_t cp = utf8_next(text, i);
            if (cp == '\n') { prev = 0; continue; }
            int gi = index(cp);
            if (prev) w += self.kern_index(prev, gi, px);
            w += self.glyph_index(gi, px, synth & kSynthMask).advance;
            prev = gi;
        }
        return w;
    }

    // Width of the first `n` codepoints - caret hit-testing in text fields.
    float advance_upto(std::string_view text, size_t n, float px, std::uint8_t synth = 0) const {
        if (!ok_) return 0;
        Font& self = const_cast<Font&>(*this);
        float w = 0;
        int prev = 0;
        size_t i = 0, count = 0;
        while (i < text.size() && count < n) {
            std::uint32_t cp = utf8_next(text, i);
            int gi = index(cp);
            if (prev) w += self.kern_index(prev, gi, px);
            w += self.glyph_index(gi, px, synth & kSynthMask).advance;
            prev = gi;
            ++count;
        }
        return w;
    }

    // --- atlas ---

    GlyphAtlas& atlas() { return atlas_; }
    const GlyphAtlas& atlas() const { return atlas_; }

    // Page-0 accessors (single-page compatibility).
    const std::vector<std::uint8_t>& atlas_pixels() const {
        static const std::vector<std::uint8_t> none;
        return atlas_.pages().empty() ? none : atlas_.pages()[0]->pixels;
    }
    int atlas_width() const { return kAtlasPage; }
    int atlas_height() const { return kAtlasPage; }
    bool atlas_dirty() const { return atlas_.dirty(); }
    void clear_dirty() { atlas_.clear_dirty(); }
    void mark_dirty() { atlas_.mark_dirty(); }

    // Style bits of face `face` in a font file, from head.macStyle
    // (bit 0 bold, bit 1 italic); Regular when the face is unreadable.
    static FontStyle face_style(const std::uint8_t* data, size_t len, int face) {
        if (!data || len < 12) return FontStyle::Regular;
        int off = stbtt_GetFontOffsetForIndex(data, face);
        if (off < 0) return FontStyle::Regular;
        stbtt_uint32 head = stbtt__find_table(const_cast<std::uint8_t*>(data), stbtt_uint32(off), "head");
        if (!head || head + 46 > len) return FontStyle::Regular;
        int mac_style = (data[head + 44] << 8) | data[head + 45];
        return make_style(mac_style & 1, mac_style & 2);
    }

    // Number of faces in a font file (1 for a plain TTF/OTF, 0 if invalid).
    static int face_count(const std::uint8_t* data, size_t len) {
        if (!data || len < 12) return 0;
        int n = stbtt_GetNumberOfFonts(data);
        return n < 0 ? 0 : n;
    }

    // The TTC face whose head.macStyle matches `style` best; 0 if none.
    static int find_face(const std::uint8_t* data, size_t len, FontStyle style) {
        int n = face_count(data, len);
        for (int f = 0; f < n; ++f)
            if (face_style(data, len, f) == style) return f;
        return 0;
    }

    const std::vector<std::uint8_t>& data() const { return data_; }

private:
    int index(std::uint32_t cp) const { return stbtt_FindGlyphIndex(&info_, int(cp)); }

    float kern_index(int a, int b, float px) const {
        return float(stbtt_GetGlyphKernAdvance(&info_, a, b)) * scale(px);
    }

    const Glyph& glyph_index(int gi, float px, std::uint8_t synth) {
        synth &= kSynthMask;
        float q = quantize_px(px);
        std::uint64_t key = (std::uint64_t(std::uint32_t(gi)) << 32) |
                            (std::uint64_t(std::uint32_t(q * 4.f) & 0xFFFFFF) << 8) | synth;
        auto it = cache_.find(key);
        if (it != cache_.end()) return it->second;

        float sc = stbtt_ScaleForPixelHeight(&info_, q);
        int adv = 0, lsb = 0;
        stbtt_GetGlyphHMetrics(&info_, gi, &adv, &lsb);

        Glyph g;
        g.size_px = q;
        g.tex_id = atlas_.first_id();
        g.advance = float(adv) * sc;

        int w = 0, h = 0, x0 = 0, y0 = 0;
        std::vector<std::uint8_t> bmp;
        if (synth & SynthItalic) {
            raster_slanted(gi, sc, bmp, w, h, x0, y0);
        } else if (std::uint8_t* b = stbtt_GetGlyphBitmap(&info_, sc, sc, gi, &w, &h, &x0, &y0)) {
            if (w > 0 && h > 0) bmp.assign(b, b + size_t(w) * h);
            stbtt_FreeBitmap(b, nullptr);
        }
        if (synth & SynthBold) {
            float e = synth_bold_px(q);
            g.advance += e;
            if (!bmp.empty()) embolden(bmp, w, h, e);
        }
        if (!bmp.empty() && w > 0 && h > 0) {
            g.size = {float(w), float(h)};
            g.bearing = {float(x0), float(y0)}; // y-down: y0 < 0 above baseline
            atlas_.add(bmp.data(), w, h, g);
        }
        return cache_.emplace(key, g).first->second;
    }

    // Outline sheared about the baseline, rasterized with stb's scanline
    // rasterizer (the same call stbtt_GetGlyphBitmap makes).
    void raster_slanted(int gi, float sc, std::vector<std::uint8_t>& out, int& w, int& h,
                        int& x0, int& y0) const {
        stbtt_vertex* v = nullptr;
        int n = stbtt_GetGlyphShape(&info_, gi, &v);
        w = h = x0 = y0 = 0;
        if (n <= 0 || !v) {
            if (v) STBTT_free(v, info_.userdata);
            return;
        }
        auto shear = [](stbtt_vertex_type x, stbtt_vertex_type y) {
            float s = float(x) + float(y) * kSynthSlant;
            s = std::fmax(-32768.f, std::fmin(32767.f, std::round(s)));
            return stbtt_vertex_type(s);
        };
        float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
        auto grow = [&](float x, float y) {
            minx = std::fmin(minx, x); maxx = std::fmax(maxx, x);
            miny = std::fmin(miny, y); maxy = std::fmax(maxy, y);
        };
        for (int k = 0; k < n; ++k) {
            v[k].x = shear(v[k].x, v[k].y);
            v[k].cx = shear(v[k].cx, v[k].cy);
            v[k].cx1 = shear(v[k].cx1, v[k].cy1);
            grow(v[k].x, v[k].y);
            if (v[k].type == STBTT_vcurve || v[k].type == STBTT_vcubic) grow(v[k].cx, v[k].cy);
            if (v[k].type == STBTT_vcubic) grow(v[k].cx1, v[k].cy1);
        }
        int ix0 = int(std::floor(minx * sc)), iy0 = int(std::floor(-maxy * sc));
        int ix1 = int(std::ceil(maxx * sc)), iy1 = int(std::ceil(-miny * sc));
        w = ix1 - ix0;
        h = iy1 - iy0;
        x0 = ix0;
        y0 = iy0;
        if (w > 0 && h > 0) {
            out.assign(size_t(w) * h, 0);
            stbtt__bitmap gbm{w, h, w, out.data()};
            stbtt_Rasterize(&gbm, 0.35f, v, n, sc, sc, 0, 0, ix0, iy0, 1, info_.userdata);
        } else {
            w = h = 0;
        }
        STBTT_free(v, info_.userdata);
    }

    // Horizontal max-filter `e` px wide (fractional tail blended): the
    // classic double-strike faux bold. Grows the bitmap by ceil(e) columns.
    static void embolden(std::vector<std::uint8_t>& bmp, int& w, int h, float e) {
        int whole = int(std::floor(e));
        float frac = e - float(whole);
        int grow = int(std::ceil(e));
        int nw = w + grow;
        std::vector<std::uint8_t> out(size_t(nw) * h, 0);
        for (int y = 0; y < h; ++y) {
            const std::uint8_t* src = &bmp[size_t(y) * w];
            std::uint8_t* dst = &out[size_t(y) * nw];
            for (int x = 0; x < nw; ++x) {
                int best = 0;
                for (int k = 0; k <= whole; ++k) {
                    int sx = x - k;
                    if (sx >= 0 && sx < w) best = std::max<int>(best, src[sx]);
                }
                int tail = x - whole - 1;
                if (frac > 0 && tail >= 0 && tail < w)
                    best = std::max(best, int(float(src[tail]) * frac + .5f));
                dst[x] = std::uint8_t(best);
            }
        }
        bmp.swap(out);
        w = nw;
    }

    // Per-line glyph ranges, byte ranges and advance widths.
    // Glyph line indices never decrease in glyph order, so one forward pass
    // assigns contiguous [first, last) ranges; an empty line gets an empty
    // range positioned at the next line's first glyph.
    static void finish_lines(TextLayout& out, std::string_view text) {
        auto& lines = out.lines;
        const auto n = std::uint32_t(out.glyphs.size());
        std::uint32_t k = 0;
        for (size_t l = 0; l < lines.size(); ++l) {
            TextLine& ln = lines[l];
            ln.first = k;
            while (k < n && out.glyphs[k].line == l) ++k;
            ln.last = k;
            ln.byte_end = l + 1 < lines.size() ? lines[l + 1].byte_begin : std::uint32_t(text.size());
            std::uint32_t e = ln.last; // trailing spaces don't count toward the width
            while (e > ln.first && text[out.glyphs[e - 1].byte] == ' ') --e;
            ln.width = e > ln.first ? out.glyphs[e - 1].pen + out.glyphs[e - 1].advance : 0.f;
        }
    }

    void add_decorations(TextLayout& out, float px, std::uint8_t flags) const {
        Decor d = decor(px);
        for (size_t l = 0; l < out.lines.size(); ++l) {
            const TextLine& ln = out.lines[l];
            if (ln.width <= 0) continue;
            auto bar = [&](float offset, float thick) {
                float t = std::fmax(1.f, std::round(thick));
                float y = std::round(ln.baseline + offset - t * .5f);
                out.decorations.push_back({{0, y, ln.width, t}, std::uint32_t(l)});
                out.size.h = std::fmax(out.size.h, y + t);
            };
            if (flags & Underline) bar(d.underline_offset, d.underline_thickness);
            if (flags & Strike) bar(d.strike_offset, d.strike_thickness);
        }
    }

    static int be16(const std::uint8_t* p) { return std::int16_t((p[0] << 8) | p[1]); }

    bool init(int face) {
        int off = stbtt_GetFontOffsetForIndex(data_.data(), face);
        if (off < 0 || !stbtt_InitFont(&info_, data_.data(), off)) {
            ok_ = false;
            return false;
        }
        stbtt_GetFontVMetrics(&info_, &asc_, &desc_, &gap_);
        upos_ = upos_thick_ = strike_pos_ = strike_thick_ = 0;
        size_t len = data_.size();
        if (stbtt_uint32 post = stbtt__find_table(data_.data(), stbtt_uint32(off), "post");
            post && post + 12 <= len) {
            upos_ = be16(&data_[post + 8]);
            upos_thick_ = be16(&data_[post + 10]);
        }
        if (stbtt_uint32 os2 = stbtt__find_table(data_.data(), stbtt_uint32(off), "OS/2");
            os2 && os2 + 30 <= len) {
            strike_thick_ = be16(&data_[os2 + 26]);
            strike_pos_ = be16(&data_[os2 + 28]);
        }
        atlas_.reset();
        ok_ = true;
        return true;
    }

    stbtt_fontinfo info_{};
    std::vector<std::uint8_t> data_;
    GlyphAtlas atlas_;
    std::map<std::uint64_t, Glyph> cache_;
    int asc_ = 0, desc_ = 0, gap_ = 0;
    int upos_ = 0, upos_thick_ = 0, strike_pos_ = 0, strike_thick_ = 0;
    bool ok_ = false;
};

} // namespace slikui::text
