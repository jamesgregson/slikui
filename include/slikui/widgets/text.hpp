// slikui/widgets/text.hpp - Label.
#pragma once

#include "slikui/context.hpp"
#include "slikui/widgets/env.hpp"
#include "slikui/widgets/widget.hpp"

#include <cmath>
#include <string>

namespace slikui {

class Label : public Widget {
public:
    explicit Label(std::string text = {}, std::string widget_id = {})
        : Widget(std::move(widget_id)), text_(std::move(text)) {}

    Label& set_text(std::string t) {
        text_ = std::move(t);
        valid_ = false;
        return *this;
    }
    const std::string& text() const { return text_; }

    Label& set_font_size(float px) { font_px_ = px; valid_ = false; return *this; }
    Label& set_color(Color c) { color_ = c; return *this; } // alpha 0 = theme default
    Label& set_wrap_width(float w) { wrap_width_ = w; valid_ = false; return *this; }

    // Placement inside the label's bounds, for a label laid out larger than
    // its text (stretched across a Column, given a Row's height, ...).
    // align: each line within the width (wrapped lines too). valign: the
    // block within the height (Baseline behaves like Top); text taller than
    // the label stays top-aligned. Defaults: Left, Top.
    Label& set_align(TextAlign a) { align_ = a; return *this; }
    Label& set_valign(TextVAlign v) { valign_ = v; return *this; }
    TextAlign align() const { return align_; }
    TextVAlign valign() const { return valign_; }

    // Style. Missing bold/italic faces are synthesized from the family's
    // closest face; set_font takes a family registered with
    // Context::add_font, or a role ("body", "heading", "mono").
    Label& set_bold(bool on = true) { bold_ = on; valid_ = false; return *this; }
    Label& set_italic(bool on = true) { italic_ = on; valid_ = false; return *this; }
    Label& set_underline(bool on = true) { underline_ = on; valid_ = false; return *this; }
    Label& set_strike(bool on = true) { strike_ = on; valid_ = false; return *this; }
    Label& set_font(std::string family) { family_ = std::move(family); valid_ = false; return *this; }
    bool bold() const { return bold_; }
    bool italic() const { return italic_; }
    bool underline() const { return underline_; }
    bool strike() const { return strike_; }
    const std::string& font() const { return family_; }

    void attached(Context&) override { valid_ = false; }

    Size measure(const Constraints& c) override {
        float avail = wrap_width_ > 0 ? wrap_width_
                                      : (c.is_bounded_x() ? c.max.x : 0.f);
        refresh(avail);
        float inv = 1.f / sc();
        // Layout size is the INK extent, but wrapping decides on ADVANCE
        // width (TextLine::width), which can be several raster px wider at
        // large raster sizes (high UI scale). Report whichever is wider, or a
        // parent sized exactly to the result makes the label re-wrap in
        // arrange (last word dropped onto a second line, outside its box).
        // +2 keeps the historical slack; the +.05 absorbs the logical-px
        // round trip (w * inv * sc) in arrange.
        float advance = 0;
        for (const auto& ln : cached_.lines) advance = std::fmax(advance, ln.width);
        float w = std::fmax(cached_.size.w + 2.f, advance + .05f);
        return c.clamp({w * inv, cached_.size.h * inv});
    }

    void arrange(const Rect& area) override {
        Widget::arrange(area);
        // Re-layout to the FINAL assigned width. measure() may be called with
        // a much wider constraint than the frame the parent finally assigns;
        // without this, paint would use a layout wrapped for a different
        // width than bounds (overflow or clipped lines).
        refresh(wrap_width_ > 0 ? wrap_width_ : bounds.w);
    }

    void paint(const PaintEnv& env) const override {
        Color col = color_.a > 0 ? color_ : env.theme.text;
        float s = sc(), inv = 1.f / s;
        float slack_y = std::fmax(bounds.h - cached_.size.h * inv, 0.f);
        float y = bounds.y + (valign_ == TextVAlign::Middle   ? slack_y * .5f
                              : valign_ == TextVAlign::Bottom ? slack_y
                                                              : 0.f);
        // Lines align within the bounds' width (raster px), each on its own.
        text::emit_layout(env.list, cached_, {bounds.x, y}, inv, col, bounds.w * s,
                          text_align_factor(align_));
    }

    const char* widget_type() const override { return "Label"; }

    void theme_changed() override { valid_ = false; } // scale/font may have changed

private:
    float sc() const { return ctx ? ctx->theme().scale : 1.f; }
    float effective_px() const { return font_px_ > 0 ? font_px_ : 15.f; }

    void refresh(float avail) {
        float sc_ = sc();
        float raster_px = sc_ * effective_px();
        float wrap = avail * sc_; // raster-px space to match the layout
        if (valid_ && cached_px_ == raster_px && cached_wrap_ == wrap) return;
        if (!ctx) return;
        auto font = ctx->fonts().resolve(family_, bold_, italic_);
        if (!font.font->valid()) return;
        std::uint8_t flags = std::uint8_t(font.synth | (underline_ ? text::Underline : 0) |
                                          (strike_ ? text::Strike : 0));
        cached_ = font.font->layout(text_, raster_px, wrap, flags);
        cached_px_ = raster_px;
        cached_wrap_ = wrap;
        valid_ = true;
    }

    std::string text_;
    float font_px_ = 0;    // 0 = theme default (logical)
    float wrap_width_ = 0; // 0 = wrap to the available width (logical)
    Color color_ = Color::transparent();
    TextAlign align_ = TextAlign::Left;
    TextVAlign valign_ = TextVAlign::Top;
    std::string family_;   // "" = body
    text::TextLayout cached_;
    float cached_px_ = 0;
    float cached_wrap_ = 0;
    bool bold_ = false, italic_ = false, underline_ = false, strike_ = false;
    bool valid_ = false;
};

} // namespace slikui
