// slikui/widgets/text.hpp — Label.
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

    void attached(Context& c) override { font_ = &c.font(); }

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
        float inv = 1.f / sc();
        for (const auto& pg : cached_.glyphs) {
            const text::Glyph& g = *pg.glyph;
            if (g.uv.empty()) continue;
            env.list.glyph({bounds.x + pg.pos.x * inv, bounds.y + pg.pos.y * inv,
                            g.size.w * inv, g.size.h * inv},
                           g.tex_id, g.uv, col);
        }
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
        if (!font_ || !font_->valid()) return;
        cached_ = font_->layout(text_, raster_px, wrap);
        cached_px_ = raster_px;
        cached_wrap_ = wrap;
        valid_ = true;
    }

    std::string text_;
    float font_px_ = 0;    // 0 = theme default (logical)
    float wrap_width_ = 0; // 0 = wrap to the available width (logical)
    Color color_ = Color::transparent();
    text::TextLayout cached_;
    float cached_px_ = 0;
    float cached_wrap_ = 0;
    text::Font* font_ = nullptr;
    bool valid_ = false;
};

} // namespace slikui
