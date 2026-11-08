// slikui/widgets/inputs.hpp — TextField (single-line, caret, placeholder).
#pragma once

#include "slikui/context.hpp"
#include "slikui/widgets/env.hpp"
#include "slikui/widgets/widget.hpp"

#include <functional>
#include <string>
#include <utility>

namespace slikui {

class TextField : public Widget {
public:
    explicit TextField(std::string widget_id = {})
        : Widget(std::move(widget_id)) {
        hover_cursor_ = Cursor::IBeam; // text fields ask for the I-beam
    }

    TextField& set_text(std::string t) {
        text_ = std::move(t);
        caret_ = text_.size();
        clear_sel();
        return *this;
    }
    const std::string& text() const { return text_; }
    TextField& set_placeholder(std::string p) { placeholder_ = std::move(p); return *this; }
    TextField& set_password(bool p) { password_ = p; return *this; }
    TextField& on_change(std::function<void(std::string_view)> f) {
        on_change_ = std::move(f);
        return *this;
    }
    TextField& on_submit(std::function<void(std::string_view)> f) {
        on_submit_ = std::move(f);
        return *this;
    }

    bool focusable() const override { return true; }

    Size measure(const Constraints& c) override {
        refresh_metrics();
        const Theme& t = ctx->theme();
        float inv = 1.f / ui_scale();
        float h = cached_line_height_ > 0 ? cached_line_height_ * inv + 10.f : 28.f;
        return c.clamp({160.f, std::fmax(h, 26.f)});
    }

    void paint(const PaintEnv& env) const override {
        const Theme& t = env.theme;
        float sc = ui_scale(), inv = 1.f / sc;
        env.list.rect(bounds, focused ? t.background : t.surface_alt, t.radius);
        env.list.hollow_rect(bounds, focused ? t.accent : t.border, t.border_width, t.radius);

        const_cast<TextField*>(this)->refresh_metrics();
        std::string shown = display_text();
        float px = t.s(t.base_font_px); // raster size
        float pad_x = 8.f;
        // Center by the tight text block, not the leading — the block now
        // starts at the ascent, so this is typographically centered.

        if (!shown.empty()) {
            text::TextLayout lay = ctx->font().layout(shown, px);
            float text_y = bounds.center().y - lay.size.h * inv * .5f;
            if (focused && has_sel()) {
                auto [lo, hi] = sel_range();
                float sx = bounds.x + pad_x + ctx->font().advance_upto(text_, lo, px) * inv;
                float ex = bounds.x + pad_x + ctx->font().advance_upto(text_, hi, px) * inv;
                env.list.rect({sx, text_y, ex - sx, lay.size.h * inv}, t.accent.faded(.35f), 2);
            }
            paint_lay(env, lay, bounds.x + pad_x, text_y, t.text, inv);
            if (focused && caret_visible_) {
                float cx = bounds.x + pad_x +
                           ctx->font().advance_upto(text_, caret_, px) * inv;
                env.list.line({cx, text_y + 2}, {cx, text_y + lay.size.h * inv - 2},
                              t.text, 1);
            }
        } else if (!placeholder_.empty()) {
            text::TextLayout lay = ctx->font().layout(placeholder_, px);
            float text_y = bounds.center().y - lay.size.h * inv * .5f;
            paint_lay(env, lay, bounds.x + pad_x, text_y, t.text_dim.faded(.6f), inv);
        }
    }

    bool on_event(const Event& e) override {
        switch (e.type) {
        case Event::Type::MousePress:
            if (bounds.contains(e.pos)) {
                caret_ = caret_at(e.pos.x);
                sel_anchor_ = long(caret_);
                selecting_ = true;
                ctx->capture_mouse(this);
                return true;
            }
            return false;
        case Event::Type::MouseMove:
            if (selecting_) {
                caret_ = caret_at(e.pos.x);
                return true;
            }
            return false;
        case Event::Type::MouseRelease:
            if (selecting_) {
                selecting_ = false;
                ctx->release_mouse(this);
                if (!has_sel()) clear_sel(); // a click with no drag: no selection
                return true;
            }
            return false;

        case Event::Type::TextInput: {
            if (!focused || e.codepoint == 0) return false;
            if (e.codepoint < 32) return false;
            erase_selection();
            std::string insert = utf8_encode(e.codepoint);
            text_.insert(caret_, insert);
            caret_ += insert.size();
            clear_sel(); // typing collapses the selection
            if (on_change_) on_change_(text_);
            return true;
        }

        case Event::Type::KeyPress: {
            if (!focused) return false;
            // Ctrl/Cmd chords: select-all, copy, cut, paste (host clipboard).
            if (e.shortcut_mod()) {
                switch (e.key) {
                case Key::A: caret_ = text_.size(); sel_anchor_ = 0; return true;
                case Key::C:
                    if (has_sel()) {
                        auto [lo, hi] = sel_range();
                        ctx->set_clipboard_text(text_.substr(lo, hi - lo));
                    }
                    return true;
                case Key::X:
                    if (has_sel()) {
                        auto [lo, hi] = sel_range();
                        ctx->set_clipboard_text(text_.substr(lo, hi - lo));
                        text_.erase(lo, hi - lo);
                        caret_ = lo;
                        clear_sel();
                        if (on_change_) on_change_(text_);
                    }
                    return true;
                case Key::V: {
                    std::string clip = ctx->clipboard_text();
                    if (clip.empty()) return true;
                    erase_selection();
                    text_.insert(caret_, clip);
                    caret_ += clip.size();
                    clear_sel();
                    if (on_change_) on_change_(text_);
                    return true;
                }
                default: return false;
                }
            }
            switch (e.key) {
            case Key::Backspace:
                if (has_sel()) {
                    erase_selection();
                    if (on_change_) on_change_(text_);
                } else if (caret_ > 0) {
                    caret_ = prev_cp_boundary(caret_);
                    text_.erase(caret_, next_cp_boundary(caret_) - caret_);
                    clear_sel();
                    if (on_change_) on_change_(text_);
                }
                return true;
            case Key::Delete:
                if (has_sel()) {
                    erase_selection();
                    if (on_change_) on_change_(text_);
                } else if (caret_ < text_.size()) {
                    text_.erase(caret_, next_cp_boundary(caret_) - caret_);
                    clear_sel();
                    if (on_change_) on_change_(text_);
                }
                return true;
            case Key::Left:
                if (e.shift()) {
                    if (sel_anchor_ < 0) sel_anchor_ = long(caret_);
                    caret_ = prev_cp_boundary(caret_);
                } else {
                    clear_sel();
                    caret_ = prev_cp_boundary(caret_);
                }
                return true;
            case Key::Right:
                if (e.shift()) {
                    if (sel_anchor_ < 0) sel_anchor_ = long(caret_);
                    caret_ = next_cp_boundary(caret_);
                } else {
                    clear_sel();
                    caret_ = next_cp_boundary(caret_);
                }
                return true;
            case Key::Home:
                if (e.shift()) {
                    if (sel_anchor_ < 0) sel_anchor_ = long(caret_);
                } else clear_sel();
                caret_ = 0;
                return true;
            case Key::End:
                if (e.shift()) {
                    if (sel_anchor_ < 0) sel_anchor_ = long(caret_);
                } else clear_sel();
                caret_ = text_.size();
                return true;
            case Key::Enter:
                if (on_submit_) on_submit_(text_);
                return true;
            case Key::Space:
                // Space arrives as TextInput in most backends; also accept here.
                erase_selection();
                text_.insert(caret_, " ");
                ++caret_;
                clear_sel();
                if (on_change_) on_change_(text_);
                return true;
            default:
                return false;
            }
        }
        default:
            return false;
        }
    }

    void tick(Time t) override {
        blink_ += t.dt;
        if (blink_ > 0.53) blink_ = 0;
        caret_visible_ = !focused || blink_ < 0.35;
    }

    const char* widget_type() const override { return "TextField"; }

private:
    void paint_lay(const PaintEnv& env, const text::TextLayout& lay, float x, float y,
                   Color col, float inv) const {
        for (const auto& pg : lay.glyphs) {
            const text::Glyph& g = *pg.glyph;
            if (g.uv.empty()) continue;
            env.list.glyph({x + pg.pos.x * inv, y + pg.pos.y * inv,
                            g.size.w * inv, g.size.h * inv},
                           g.tex_id, g.uv, col);
        }
    }

    std::string display_text() const {
        return password_ ? std::string(text_.size(), '*') : text_;
    }

    // --- selection (byte offsets into text_) ---
    bool has_sel() const { return sel_anchor_ >= 0 && size_t(sel_anchor_) != caret_; }
    std::pair<size_t, size_t> sel_range() const {
        size_t a = size_t(sel_anchor_), b = caret_;
        return a < b ? std::pair{a, b} : std::pair{b, a};
    }
    void clear_sel() { sel_anchor_ = -1; }
    void erase_selection() {
        if (!has_sel()) return;
        auto [lo, hi] = sel_range();
        text_.erase(lo, hi - lo);
        caret_ = lo;
        clear_sel();
    }
    // Place the caret at a click position (approximate: walk codepoints).
    size_t caret_at(float x) const {
        float inv = 1.f / ui_scale();
        float px = ctx->theme().s(ctx->theme().base_font_px);
        float rel = x - bounds.x - 8;
        size_t n = 0;
        while (n < text_.size() &&
               ctx->font().advance_upto(text_, n + 1, px) * inv < rel)
            ++n;
        return n;
    }

    size_t next_cp_boundary(size_t i) const {
        if (i >= text_.size()) return text_.size();
        size_t n = 1;
        while (i + n < text_.size() && (text_[i + n] & 0xC0) == 0x80) ++n;
        return i + n;
    }
    size_t prev_cp_boundary(size_t i) const {
        if (i == 0) return 0;
        size_t n = i - 1;
        while (n > 0 && (text_[n] & 0xC0) == 0x80) --n;
        return n;
    }

    static std::string utf8_encode(std::uint32_t cp) {
        std::string out;
        if (cp < 0x80) {
            out += char(cp);
        } else if (cp < 0x800) {
            out += char(0xC0 | (cp >> 6));
            out += char(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += char(0xE0 | (cp >> 12));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        } else {
            out += char(0xF0 | (cp >> 18));
            out += char(0x80 | ((cp >> 12) & 0x3F));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        }
        return out;
    }

    void refresh_metrics() {
        if (!ctx || !ctx->font().valid()) return;
        cached_line_height_ =
            ctx->font().line_height(ctx->theme().s(ctx->theme().base_font_px));
    }

    std::string text_, placeholder_;
    size_t caret_ = 0;
    long sel_anchor_ = -1; // byte offset where the selection was anchored
    bool selecting_ = false;
    bool password_ = false;
    bool caret_visible_ = true;
    double blink_ = 0;
    float cached_line_height_ = 0;
    std::function<void(std::string_view)> on_change_, on_submit_;
};

// ---------------------------------------------------------------------------
// TextArea — multiline editor. Newline insertion, per-line caret navigation,
// click placement, vertical auto-scroll of the caret into view. No soft wrap
// in v0.1: long lines clip at the field edge.
// ---------------------------------------------------------------------------
class TextArea : public Widget {
public:
    explicit TextArea(std::string widget_id = {}) : Widget(std::move(widget_id)) {}

    TextArea& set_text(std::string t) {
        text_ = std::move(t);
        caret_ = text_.size();
        scroll_y_ = 0;
        return *this;
    }
    const std::string& text() const { return text_; }
    TextArea& set_placeholder(std::string p) { placeholder_ = std::move(p); return *this; }
    TextArea& set_rows(float rows) { rows_ = std::fmax(1.f, rows); return *this; }
    TextArea& on_change(std::function<void(std::string_view)> f) {
        on_change_ = std::move(f);
        return *this;
    }

    bool focusable() const override { return true; }

    Size measure(const Constraints& c) override {
        refresh_metrics();
        float inv = 1.f / ui_scale();
        float h = cached_line_height_ * inv * rows_ + 12.f;
        return c.clamp({160.f, h});
    }

    void paint(const PaintEnv& env) const override {
        const Theme& t = env.theme;
        float sc = ui_scale(), inv = 1.f / sc;
        env.list.rect(bounds, focused ? t.background : t.surface_alt, t.radius);
        env.list.hollow_rect(bounds, focused ? t.accent : t.border, t.border_width, t.radius);

        const_cast<TextArea*>(this)->refresh_metrics();
        float px = t.s(t.base_font_px);
        float pad = 8.f;
        float lh_l = cached_line_height_ * inv; // logical line height
        float text_top = bounds.y + pad - scroll_y_;

        std::vector<Line> lines = split_lines();
        env.list.push_clip(bounds);

        if (text_.empty() && !placeholder_.empty()) {
            text::TextLayout lay = ctx->font().layout(placeholder_, px);
            slikui::paint_text(env, lay, {bounds.x + pad, bounds.y + pad},
                             t.text_dim.faded(.6f), inv);
        }
        for (size_t i = 0; i < lines.size(); ++i) {
            if (lines[i].len == 0) continue; // empty line (caret row only)
            float y = text_top + float(i) * lh_l;
            if (y + lh_l < bounds.y || y > bounds.bottom()) continue; // offscreen
            text::TextLayout lay = ctx->font().layout(
                std::string_view{text_.data() + lines[i].start, lines[i].len}, px);
            slikui::paint_text(env, lay, {bounds.x + pad, y}, t.text, inv);
        }

        if (focused && caret_visible_) {
            auto [line, col] = caret_line_col(lines);
            float cx = bounds.x + pad;
            if (col > 0 && line < lines.size() && lines[line].len > 0)
                cx += ctx->font()
                          .advance_upto(
                              std::string_view{text_.data() + lines[line].start,
                                               lines[line].len},
                              col, px) *
                      inv;
            float cy = text_top + float(line) * lh_l;
            env.list.line({cx, cy + 1}, {cx, cy + lh_l - 1}, t.text, 1);
        }
        env.list.pop_clip();
    }

    bool on_event(const Event& e) override {
        switch (e.type) {
        case Event::Type::MousePress: {
            if (!bounds.contains(e.pos)) return false;
            float inv = 1.f / ui_scale();
            float lh_l = line_height_logical() * inv;
            float pad = 8.f;
            std::vector<Line> lines = split_lines();
            size_t line = size_t(std::fmax(
                0.f, std::floor((e.pos.y - bounds.y - pad + scroll_y_) / lh_l)));
            line = std::fmin(line, lines.size() - 1);
            float x = e.pos.x - bounds.x - pad;
            float px = ctx->theme().s(ctx->theme().base_font_px);
            size_t col = 0;
            if (lines[line].len > 0) {
                std::string_view lv{text_.data() + lines[line].start, lines[line].len};
                while (col < lines[line].len &&
                       ctx->font().advance_upto(lv, col + 1, px) * inv < x)
                    ++col;
            }
            caret_ = lines[line].start + col;
            ensure_caret_visible();
            return true;
        }
        case Event::Type::TextInput: {
            if (!focused || e.codepoint == 0 || e.codepoint < 32) return false;
            insert(utf8_encode(e.codepoint));
            return true;
        }
        case Event::Type::KeyPress: {
            if (!focused) return false;
            switch (e.key) {
            case Key::Backspace:
                if (caret_ > 0) {
                    caret_ = prev_cp_boundary(caret_);
                    text_.erase(caret_, next_cp_boundary(caret_) - caret_);
                    changed();
                }
                return true;
            case Key::Delete:
                if (caret_ < text_.size()) {
                    text_.erase(caret_, next_cp_boundary(caret_) - caret_);
                    changed();
                }
                return true;
            case Key::Left:
                if (caret_ > 0) {
                    caret_ = prev_cp_boundary(caret_);
                    ensure_caret_visible();
                }
                return true;
            case Key::Right:
                if (caret_ < text_.size()) {
                    caret_ = next_cp_boundary(caret_);
                    ensure_caret_visible();
                }
                return true;
            case Key::Up:
                move_vertically(-1);
                return true;
            case Key::Down:
                move_vertically(1);
                return true;
            case Key::Home: {
                auto [line, col] = caret_line_col(split_lines());
                caret_ = split_lines()[line].start;
                ensure_caret_visible();
                return true;
            }
            case Key::End: {
                auto lines = split_lines();
                auto [line, col] = caret_line_col(lines);
                caret_ = lines[line].start + lines[line].len;
                ensure_caret_visible();
                return true;
            }
            case Key::Enter:
                insert("\n");
                return true;
            case Key::Space:
                insert(" ");
                return true;
            case Key::Tab:
                return false; // Tab keeps its focus-cycling meaning
            default:
                return false;
            }
        }
        default:
            return false;
        }
    }

    void tick(Time t) override {
        blink_ += t.dt;
        if (blink_ > 0.53) blink_ = 0;
        caret_visible_ = !focused || blink_ < 0.35;
    }

    const char* widget_type() const override { return "TextArea"; }

private:
    struct Line {
        size_t start = 0, len = 0; // byte range, '\n' excluded
    };

    std::vector<Line> split_lines() const {
        std::vector<Line> out;
        size_t start = 0;
        for (size_t i = 0; i <= text_.size(); ++i) {
            if (i == text_.size() || text_[i] == '\n') {
                out.push_back({start, i - start});
                start = i + 1;
            }
        }
        return out;
    }

    // Caret position as (line index, codepoint column).
    std::pair<size_t, size_t> caret_line_col(const std::vector<Line>& lines) const {
        size_t line = 0;
        for (size_t i = 0; i < text_.size() && i < caret_; ++i)
            if (text_[i] == '\n') ++line;
        line = std::fmin(line, lines.size() - 1);
        size_t col = caret_ - lines[line].start;
        return {line, std::fmin(col, lines[line].len)};
    }

    void insert(std::string s) {
        text_.insert(caret_, s);
        caret_ += s.size();
        changed();
    }

    void changed() {
        ensure_caret_visible();
        if (on_change_) on_change_(text_);
    }

    void move_vertically(int dir) {
        std::vector<Line> lines = split_lines();
        auto [line, col] = caret_line_col(lines);
        long target = long(line) + dir;
        if (target < 0) {
            caret_ = 0;
            ensure_caret_visible();
            return;
        }
        if (target >= long(lines.size())) {
            caret_ = text_.size();
            ensure_caret_visible();
            return;
        }
        float px = ctx->theme().s(ctx->theme().base_font_px);
        float inv = 1.f / ui_scale();
        // Keep the horizontal position when moving between lines.
        float x = col > 0 && lines[line].len > 0
                      ? ctx->font().advance_upto(
                            std::string_view{text_.data() + lines[line].start,
                                             lines[line].len},
                            col, px) *
                            inv
                      : 0.f;
        const Line& tl = lines[size_t(target)];
        size_t col2 = 0;
        if (tl.len > 0) {
            std::string_view lv{text_.data() + tl.start, tl.len};
            while (col2 < tl.len &&
                   ctx->font().advance_upto(lv, col2 + 1, px) * inv < x)
                ++col2;
        }
        caret_ = tl.start + col2;
        ensure_caret_visible();
    }

    void ensure_caret_visible() {
        std::vector<Line> lines = split_lines();
        auto [line, col] = caret_line_col(lines);
        float lh = line_height_logical();
        float pad = 8.f;
        float view_h = std::fmax(bounds.h - pad * 2, lh);
        float top = float(line) * lh;
        if (top < scroll_y_) scroll_y_ = top;
        if (top + lh > scroll_y_ + view_h) scroll_y_ = top + lh - view_h;
        float max_scroll = std::fmax(0.f, float(lines.size()) * lh - view_h);
        scroll_y_ = std::clamp(scroll_y_, 0.f, max_scroll);
    }

    float line_height_logical() const {
        const_cast<TextArea*>(this)->refresh_metrics();
        return cached_line_height_ / ui_scale();
    }

    void refresh_metrics() const {
        if (!ctx || !ctx->font().valid()) return;
        cached_line_height_ =
            ctx->font().line_height(ctx->theme().s(ctx->theme().base_font_px));
    }

    size_t next_cp_boundary(size_t i) const {
        if (i >= text_.size()) return text_.size();
        size_t n = 1;
        while (i + n < text_.size() && (text_[i + n] & 0xC0) == 0x80) ++n;
        return i + n;
    }
    size_t prev_cp_boundary(size_t i) const {
        if (i == 0) return 0;
        size_t n = i - 1;
        while (n > 0 && (text_[n] & 0xC0) == 0x80) --n;
        return n;
    }

    static std::string utf8_encode(std::uint32_t cp) {
        std::string out;
        if (cp < 0x80) {
            out += char(cp);
        } else if (cp < 0x800) {
            out += char(0xC0 | (cp >> 6));
            out += char(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += char(0xE0 | (cp >> 12));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        } else {
            out += char(0xF0 | (cp >> 18));
            out += char(0x80 | ((cp >> 12) & 0x3F));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        }
        return out;
    }

    std::string text_, placeholder_;
    size_t caret_ = 0;
    float rows_ = 4;
    float scroll_y_ = 0;
    bool caret_visible_ = true;
    double blink_ = 0;
    mutable float cached_line_height_ = 0;
    std::function<void(std::string_view)> on_change_;
};

} // namespace slikui
