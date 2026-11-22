// slikui/text/text.hpp - the text API: TextOpts, FontLibrary, TextBlock.
//
// Everything here is in LOGICAL px: theme scale is applied internally, so
// callers never convert between raster and logical space.
//
//   env.text({x, y}, "Hello");                                // theme defaults
//   env.text_in(bounds, "42%", {.size = 20, .bold = true,
//                               .align = TextAlign::Center,
//                               .valign = TextVAlign::Middle});
//   Size s = ctx.text_size("42%", {.size = 20, .bold = true}); // measure()
//
// TextOpts fields default to the theme; designated initializers must follow
// declaration order (font, size, color, bold, italic, underline, strike,
// align, valign, wrap, max_width, overflow).
#pragma once

#include "slikui/core/color.hpp"
#include "slikui/core/math.hpp"
#include "slikui/render/draw_list.hpp"
#include "slikui/style/theme.hpp"
#include "slikui/text/font.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace slikui {

enum class TextAlign : std::uint8_t { Left, Center, Right };

// Vertical placement. Point form (env.text): what the point's y is.
// Rect form (env.text_in): Top / Middle / Bottom of the rect; Baseline puts
// the first baseline on the rect's top edge.
enum class TextVAlign : std::uint8_t { Top, Middle, Baseline, Bottom };

// Text wider than the available width (rect width, or TextOpts::max_width).
// Ellipsis applies to single-line, non-wrapping text.
enum class TextOverflow : std::uint8_t { Visible, Clip, Ellipsis };

struct TextOpts {
    std::string_view font = {};           // family or role: "" / "body", "heading", "mono", ...
    float size = 0;                       // logical px; 0 = theme base_font_px
    Color color = Color::transparent();   // alpha 0 = theme text color
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strike = false;
    TextAlign align = TextAlign::Left;
    TextVAlign valign = TextVAlign::Top;
    bool wrap = false;                    // wrap to the available width
    float max_width = 0;                  // point form: available width (0 = none)
    TextOverflow overflow = TextOverflow::Visible;
};

// Vertical metrics for one line of a style, in logical px.
struct TextMetrics {
    float ascent = 0;      // baseline below the line top
    float descent = 0;     // below the baseline (positive)
    float line_height = 0; // baseline-to-baseline distance
};

inline float text_align_factor(TextAlign a) {
    return a == TextAlign::Center ? .5f : a == TextAlign::Right ? 1.f : 0.f;
}

namespace text {

// Widest line of a layout, raster px. The last line counts its trailing
// spaces (so "Status: " chains into the next run); earlier lines don't (a
// wrapped line's trailing space would overhang the wrap width).
inline float layout_advance(const TextLayout& lay) {
    float w = 0;
    for (size_t l = 0; l < lay.lines.size(); ++l) {
        const TextLine& ln = lay.lines[l];
        if (l + 1 < lay.lines.size() || ln.last == ln.first) {
            w = std::fmax(w, ln.width);
            continue;
        }
        const PlacedGlyph& g = lay.glyphs[ln.last - 1];
        w = std::fmax(w, g.pen + g.advance);
    }
    return w;
}

// Emits a layout's glyphs and decorations at `top_left` (logical). Lines are
// aligned inside a box `box` raster px wide by factor `f` (0 left, .5 center,
// 1 right); f = 0 emits every glyph exactly where the layout placed it.
inline void emit_layout(DrawList& list, const TextLayout& lay, Vec2 top_left, float inv,
                        Color col, float box = 0, float f = 0) {
    auto offset = [&](std::uint32_t line) {
        if (f == 0 || line >= lay.lines.size()) return 0.f;
        return (box - lay.lines[line].width) * f;
    };
    for (const auto& pg : lay.glyphs) {
        const Glyph& g = *pg.glyph;
        if (g.uv.empty()) continue;
        float ox = offset(pg.line);
        list.glyph({top_left.x + (pg.pos.x + ox) * inv, top_left.y + pg.pos.y * inv,
                    g.size.w * inv, g.size.h * inv},
                   g.tex_id, g.uv, col);
    }
    for (const auto& d : lay.decorations) {
        float ox = offset(d.line);
        list.rect({top_left.x + (d.rect.x + ox) * inv, top_left.y + d.rect.y * inv,
                   d.rect.w * inv, d.rect.h * inv},
                  col);
    }
}

// Lays out `s` with an ellipsis when it is wider than `ellipsis_w` (raster
// px; 0 = never). Single-line text only; wrapped text is returned as is.
inline TextLayout layout_fitted(const Font& font, std::string_view s, float px, float wrap,
                                float ellipsis_w, std::uint8_t flags) {
    TextLayout lay = font.layout(s, px, wrap, flags);
    if (ellipsis_w <= 0 || wrap > 0 || lay.lines.size() != 1 || lay.lines[0].width <= ellipsis_w)
        return lay;
    std::string_view ell = font.has_glyph(0x2026) ? std::string_view("\xE2\x80\xA6") : "...";
    float ew = font.measure_width(ell, px, flags);
    size_t cut = 0;
    while (cut < lay.glyphs.size() &&
           lay.glyphs[cut].pen + lay.glyphs[cut].advance + ew <= ellipsis_w)
        ++cut;
    while (cut > 0 && s[lay.glyphs[cut - 1].byte] == ' ') --cut;
    size_t cut_byte = cut < lay.glyphs.size() ? lay.glyphs[cut].byte : s.size();
    std::string t(s.substr(0, cut_byte));
    t += ell;
    return font.layout(t, px, 0, flags);
}

} // namespace text

class TextBlock;

namespace text {

// A Context's fonts: families of faces by name, plus the immediate-mode
// layout cache behind env.text() / Context::text_size().
class FontLibrary {
public:
    FontLibrary() { families_.push_back(Family{"body", {&body_, nullptr, nullptr, nullptr}}); }
    FontLibrary(const FontLibrary&) = delete;
    FontLibrary& operator=(const FontLibrary&) = delete;

    Font& body() { return body_; }
    const Font& body() const { return body_; }

    // Texture ids for atlas pages of every face (Context::next_texture_id).
    void set_texture_source(std::function<std::uint32_t()> f) {
        id_source_ = std::move(f);
        for_each_font([&](Font& font) { font.set_texture_source(id_source_); });
    }

    // See Context::add_font.
    bool add_font(std::string_view family, const std::string& path,
                  FontStyle style = FontStyle::Regular, int face = 0) {
        if (face < 0) {
            Font probe;
            if (!probe.load_file(path)) return false;
            face = Font::find_face(probe.data().data(), probe.data().size(), style);
        }
        return slot(family, style)->load_file(path, face);
    }
    bool add_font_memory(std::string_view family, const std::uint8_t* data, size_t len,
                         FontStyle style = FontStyle::Regular, int face = 0) {
        if (face < 0) face = Font::find_face(data, len, style);
        return slot(family, style)->load_memory(data, len, face);
    }

    // True when `family` has a loaded face (any style).
    bool has_font(std::string_view family) const {
        const Family* fam = find(family);
        if (!fam) return false;
        for (const Font* f : fam->faces)
            if (f && f->valid()) return true;
        return false;
    }

    struct Resolved {
        Font* font = nullptr;
        std::uint8_t synth = 0; // SynthBold / SynthItalic for what the face lacks
    };

    // The closest loaded face of `family` (unknown families fall back to
    // "body"), plus synthesis flags for the style bits that face lacks.
    // Never null: the body font, possibly invalid, is the last resort.
    Resolved resolve(std::string_view family, bool bold = false, bool italic = false) {
        const Family* fam = find(family);
        if (!fam || !has_font(family)) fam = find("body");
        const bool tries[4][2] = {{bold, italic}, {bold, false}, {false, italic}, {false, false}};
        for (const auto& t : tries) {
            Font* f = fam->faces[int(make_style(t[0], t[1]))];
            if (f && f->valid()) {
                std::uint8_t synth = std::uint8_t((bold && !t[0] ? SynthBold : 0) |
                                                  (italic && !t[1] ? SynthItalic : 0));
                return {f, synth};
            }
        }
        return {&body_, std::uint8_t((bold ? SynthBold : 0) | (italic ? SynthItalic : 0))};
    }

    template <class F>
    void for_each_font(F&& fn) {
        fn(body_);
        for (auto& f : extra_) fn(*f);
    }

    // --- immediate-mode layout ---

    // A TextOpts resolved against a theme: face, layout flags, raster px.
    struct Request {
        Font* font = nullptr;
        std::uint8_t flags = 0;
        float px = 0;
    };
    Request request(const TextOpts& o, const Theme& theme) {
        Resolved r = resolve(o.font, o.bold, o.italic);
        return {r.font, flags_of(o, r.synth), theme.s(o.size > 0 ? o.size : theme.base_font_px)};
    }

    static std::uint8_t flags_of(const TextOpts& o, std::uint8_t synth) {
        return std::uint8_t(synth | (o.underline ? Underline : 0) | (o.strike ? Strike : 0));
    }

    // Cached layout of `s` (raster space). The reference stays valid until
    // the next end_frame().
    const TextLayout& layout(std::string_view s, const Request& rq, float wrap = 0,
                             float ellipsis_w = 0) {
        std::string key;
        key.reserve(sizeof(void*) + 1 + 3 * sizeof(float) + s.size());
        auto put = [&](const void* p, size_t n) { key.append(static_cast<const char*>(p), n); };
        put(&rq.font, sizeof rq.font);
        put(&rq.flags, 1);
        put(&rq.px, sizeof(float));
        put(&wrap, sizeof(float));
        put(&ellipsis_w, sizeof(float));
        key.append(s);
        auto it = cache_.find(key);
        if (it == cache_.end()) {
            if (cache_.size() >= evict_at_) {
                evict(frame_); // runaway text: drop all but this frame
                // Whatever survived was used this frame (e.g. a big list
                // measured in one tick). Let the cache grow instead of
                // rescanning it on every later miss: amortized O(1).
                evict_at_ = std::max(kMaxCache, cache_.size() * 2);
            }
            it = cache_.emplace(std::move(key),
                                Entry{layout_fitted(*rq.font, s, rq.px, wrap, ellipsis_w, rq.flags), 0})
                     .first;
        }
        it->second.used = frame_;
        return it->second.lay;
    }

    // Called once per rendered frame: layouts unused for a while are dropped.
    void end_frame() {
        ++frame_;
        if (frame_ > kKeepFrames) evict(frame_ - kKeepFrames);
        evict_at_ = std::max(kMaxCache, cache_.size() * 2);
    }
    size_t cache_size() const { return cache_.size(); }

private:
    static constexpr size_t kMaxCache = 4096;
    static constexpr std::uint64_t kKeepFrames = 120;

    struct Family {
        std::string name;
        Font* faces[4] = {}; // indexed by FontStyle
    };
    struct Entry {
        TextLayout lay;
        std::uint64_t used = 0;
    };

    const Family* find(std::string_view name) const {
        if (name.empty()) name = "body";
        for (const auto& f : families_)
            if (f.name == name) return &f;
        return nullptr;
    }

    // The Font `family`/`style` loads into, created on first use. The body
    // family's Regular face is body_ itself.
    Font* slot(std::string_view family, FontStyle style) {
        if (family.empty()) family = "body";
        Family* fam = nullptr;
        for (auto& f : families_)
            if (f.name == family) fam = &f;
        if (!fam) {
            families_.push_back(Family{std::string(family), {}});
            fam = &families_.back();
        }
        Font*& s = fam->faces[int(style)];
        if (!s) {
            extra_.push_back(std::make_unique<Font>());
            s = extra_.back().get();
            if (id_source_) s->set_texture_source(id_source_);
        }
        return s;
    }

    void evict(std::uint64_t older_than) {
        for (auto it = cache_.begin(); it != cache_.end();)
            it = it->second.used < older_than ? cache_.erase(it) : std::next(it);
    }

    Font body_;
    std::vector<std::unique_ptr<Font>> extra_;
    std::vector<Family> families_;
    std::function<std::uint32_t()> id_source_;
    std::unordered_map<std::string, Entry> cache_;
    size_t evict_at_ = kMaxCache; // cache size that triggers a mid-frame evict
    std::uint64_t frame_ = 0;
};

} // namespace text

// A laid-out block of text kept by the caller: draw it many times, and
// query it for interactive widgets (hit-testing, carets, selections).
// Built by Context::text_layout(); drawn with PaintEnv::draw(). All
// coordinates are logical px relative to the block's top-left. Rebuild it
// when the theme scale changes (it is rasterized for one scale).
class TextBlock {
public:
    TextBlock() = default;

    // Natural size: widest line advance x block height.
    Size size() const { return {text::layout_advance(lay_) * inv_, lay_.size.h * inv_}; }
    size_t line_count() const { return lay_.lines.size(); }
    float line_height() const { return lay_.line_height * inv_; }
    const std::string& text() const { return text_; }
    bool empty() const { return lay_.glyphs.empty(); }

    // Caret byte offset nearest to `p` (block-local).
    size_t hit_test(Vec2 p) const {
        if (lay_.lines.empty()) return 0;
        size_t l = line_at(p.y);
        const text::TextLine& ln = lay_.lines[l];
        float x = p.x / inv_ - offset(l);
        for (std::uint32_t k = ln.first; k < ln.last; ++k) {
            const text::PlacedGlyph& g = lay_.glyphs[k];
            if (x < g.pen + g.advance * .5f) return g.byte;
        }
        return line_end(l);
    }

    // Caret rectangle (1 logical px wide) before the codepoint at `byte`.
    Rect caret_rect(size_t byte) const {
        if (lay_.lines.empty()) return {0, 0, 1, lay_.line_height * inv_};
        size_t l = line_of(byte);
        const text::TextLine& ln = lay_.lines[l];
        float x = offset(l) + caret_x(l, byte);
        return {x * inv_, (ln.baseline - lay_.ascent) * inv_, 1.f, lay_.line_height * inv_};
    }

    // Highlight rectangles for the byte range [a, b), one per line touched.
    std::vector<Rect> selection_rects(size_t a, size_t b) const {
        std::vector<Rect> out;
        if (a > b) std::swap(a, b);
        if (a == b) return out;
        for (size_t l = 0; l < lay_.lines.size(); ++l) {
            const text::TextLine& ln = lay_.lines[l];
            size_t lo = std::max<size_t>(a, ln.byte_begin), hi = std::min<size_t>(b, ln.byte_end);
            bool last_line = l + 1 == lay_.lines.size();
            if (lo >= hi) continue;
            float x0 = caret_x(l, lo);
            float x1 = caret_x(l, hi);
            if (hi >= ln.byte_end && !last_line) x1 += lay_.line_height * .25f; // the line break
            float ox = offset(l);
            out.push_back({(ox + x0) * inv_, (ln.baseline - lay_.ascent) * inv_,
                           std::fmax(x1 - x0, 1.f) * inv_, lay_.line_height * inv_});
        }
        return out;
    }

    // Raster-space layout (glyph pointers, lines) for custom drawing.
    const text::TextLayout& layout() const { return lay_; }
    float inv_scale() const { return inv_; }
    TextAlign align() const { return align_; }
    Color color() const { return color_; }

    // Internal: Context::text_layout builds blocks through this.
    static TextBlock make(text::TextLayout lay, std::string text, float inv, float box,
                          TextAlign align, Color color) {
        TextBlock b;
        b.lay_ = std::move(lay);
        b.text_ = std::move(text);
        b.inv_ = inv;
        b.box_ = box > 0 ? box : text::layout_advance(b.lay_);
        b.align_ = align;
        b.color_ = color;
        return b;
    }

    // Emits the block at `top_left` (logical). Alpha-0 `color` = block color.
    void emit(DrawList& list, Vec2 top_left, Color color = Color::transparent()) const {
        text::emit_layout(list, lay_, top_left, inv_, color.a > 0 ? color : color_, box_,
                          text_align_factor(align_));
    }

private:
    float offset(size_t l) const {
        float f = text_align_factor(align_);
        return f == 0 ? 0.f : (box_ - lay_.lines[l].width) * f;
    }
    size_t line_at(float y) const {
        float lh = lay_.line_height > 0 ? lay_.line_height : 1.f;
        long l = long(std::floor(y / inv_ / lh));
        return size_t(std::clamp<long>(l, 0, long(lay_.lines.size()) - 1));
    }
    size_t line_of(size_t byte) const {
        for (size_t l = 0; l < lay_.lines.size(); ++l)
            if (byte < lay_.lines[l].byte_end) return l;
        return lay_.lines.size() - 1;
    }
    // Caret position at the end of line l: before its '\n', if it has one.
    size_t line_end(size_t l) const {
        size_t e = lay_.lines[l].byte_end;
        if (e > lay_.lines[l].byte_begin && e <= text_.size() && text_[e - 1] == '\n') --e;
        return e;
    }
    float caret_x(size_t l, size_t byte) const {
        const text::TextLine& ln = lay_.lines[l];
        for (std::uint32_t k = ln.first; k < ln.last; ++k)
            if (lay_.glyphs[k].byte >= byte) return lay_.glyphs[k].pen;
        if (ln.last > ln.first) {
            const text::PlacedGlyph& g = lay_.glyphs[ln.last - 1];
            return g.pen + g.advance;
        }
        return 0.f;
    }

    text::TextLayout lay_;
    std::string text_;
    float inv_ = 1;
    float box_ = 0; // raster width lines align within
    TextAlign align_ = TextAlign::Left;
    Color color_ = Color::transparent();
};

namespace text {

// Shared by PaintEnv::text / text_in. `lib` may be null (a PaintEnv built by
// hand): `fallback` is then used, uncached, with synthesized styles.
inline Rect draw_text(DrawList& list, const Theme& theme, FontLibrary* lib, const Font& fallback,
                      bool in_rect, Rect box, std::string_view s, const TextOpts& o) {
    FontLibrary::Request rq;
    if (lib) {
        rq = lib->request(o, theme);
    } else {
        rq.font = const_cast<Font*>(&fallback);
        rq.flags = FontLibrary::flags_of(
            o, std::uint8_t((o.bold ? SynthBold : 0) | (o.italic ? SynthItalic : 0)));
        rq.px = theme.s(o.size > 0 ? o.size : theme.base_font_px);
    }
    if (!rq.font || !rq.font->valid()) return {box.x, box.y, 0, 0};

    float avail = in_rect ? box.w : o.max_width;
    float wrap = (o.wrap && avail > 0) ? theme.s(avail) : 0.f;
    float ell = (o.overflow == TextOverflow::Ellipsis && !o.wrap && avail > 0) ? theme.s(avail) : 0.f;
    TextLayout local;
    const TextLayout& lay = lib ? lib->layout(s, rq, wrap, ell)
                                : (local = layout_fitted(*rq.font, s, rq.px, wrap, ell, rq.flags));

    float inv = 1.f / theme.scale;
    float f = text_align_factor(o.align);
    float box_r = layout_advance(lay);
    float bw = box_r * inv, h = lay.size.h * inv, asc = lay.ascent * inv;
    float ax = in_rect ? box.x + box.w * f : box.x;
    float left = ax - bw * f;
    float ref = box.y;
    if (in_rect) {
        if (o.valign == TextVAlign::Middle) ref = box.y + box.h * .5f;
        else if (o.valign == TextVAlign::Bottom) ref = box.bottom();
    }
    float top = ref;
    switch (o.valign) {
        case TextVAlign::Top: break;
        case TextVAlign::Middle: top = ref - h * .5f; break;
        case TextVAlign::Baseline: top = ref - asc; break;
        case TextVAlign::Bottom: top = ref - h; break;
    }

    bool clip = o.overflow == TextOverflow::Clip && avail > 0;
    if (clip) {
        Rect c = in_rect ? box : Rect{ax - avail * f, top, avail, h};
        list.push_clip(c);
    }
    emit_layout(list, lay, {left, top}, inv, o.color.a > 0 ? o.color : theme.text, box_r, f);
    if (clip) list.pop_clip();
    return {left, top, bw, h};
}

} // namespace text

} // namespace slikui
