// slikui/widgets/text.hpp - Label.
#pragma once

#include "slikui/context.hpp"
#include "slikui/widgets/env.hpp"
#include "slikui/widgets/widget.hpp"

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
        // +2 raster px: layout size is the ink extent, but wrapping decides
        // on ADVANCE width, which can be a hair wider. Without the slack a
        // parent sized exactly to the ink width makes the label re-wrap in
        // arrange (text wider than its box).
        return c.clamp({(cached_.size.w + 2.f) * inv, cached_.size.h * inv});
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
        paint_text(env, cached_, {bounds.x, bounds.y}, col, 1.f / sc());
    }

    const char* widget_type() const override { return "Label"; }

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
    std::string family_;   // "" = body
    text::TextLayout cached_;
    float cached_px_ = 0;
    float cached_wrap_ = 0;
    bool bold_ = false, italic_ = false, underline_ = false, strike_ = false;
    bool valid_ = false;
};

} // namespace slikui
