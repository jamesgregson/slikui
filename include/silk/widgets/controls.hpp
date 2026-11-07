// silk/widgets/controls.hpp — Button, Checkbox, Switch, Slider, ProgressBar,
// Spinner. All controls follow the same pattern: fluent setters, theme-driven
// colors, Anim-driven hover/press transitions, focusable keyboard support.
#pragma once

#include "silk/context.hpp"
#include "silk/style/anim.hpp"
#include "silk/widgets/env.hpp"
#include "silk/widgets/widget.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>

namespace silk {

// ---------------------------------------------------------------------------
// Button
// ---------------------------------------------------------------------------
class Button : public Widget {
public:
    enum class Variant : std::uint8_t { Default, Primary, Danger, Ghost };

    explicit Button(std::string text = {}, std::string widget_id = {})
        : Widget(std::move(widget_id)), text_(std::move(text)) {}

    Button& set_text(std::string t) { text_ = std::move(t); label_valid_ = false; return *this; }
    const std::string& text() const { return text_; }
    Button& set_variant(Variant v) { variant_ = v; return *this; }
    Button& set_padding(float h, float v) { pad_h_ = h; pad_v_ = v; return *this; }
    Button& on_click(std::function<void()> f) { on_click_ = std::move(f); return *this; }

    bool focusable() const override { return true; }

    Size measure(const Constraints& c) override {
        refresh_label();
        float inv = 1.f / ui_scale();
        Size s{cached_text_.size.w * inv + pad_h_ * 2, cached_text_.size.h * inv + pad_v_ * 2};
        s.h = std::fmax(s.h, 30.f);
        return c.clamp(s);
    }

    void paint(const PaintEnv& env) const override {
        refresh_label();
        const Theme& t = env.theme;
        Color base = base_color(t);
        Color fill = base;
        if (variant_ != Variant::Ghost || hover_.value > 0) fill = base.lerp(t.hover(base), hover_.value);
        fill = fill.lerp(t.press(base), pressed_.value);
        if (!enabled) fill = t.surface_alt.faded(.6f);

        env.list.rect(bounds, fill, t.radius);
        if (draw_border_)
            env.list.hollow_rect(bounds, t.border, t.border_width, t.radius);

        Color text_col = variant_ == Variant::Primary || variant_ == Variant::Danger
                             ? t.accent_text
                             : t.text;
        if (!enabled) text_col = t.text_dim;
        float inv = 1.f / ui_scale();
        Vec2 top_left{bounds.center().x - cached_text_.size.w * inv * .5f,
                      bounds.center().y - cached_text_.size.h * inv * .5f};
        paint_text(env, top_left, text_col, inv);
    }

    bool on_event(const Event& e) override {
        switch (e.type) {
        case Event::Type::MouseMove:
            hover_.target = bounds.contains(e.pos) ? 1.f : 0.f;
            return false;
        case Event::Type::MousePress:
            if (enabled && bounds.contains(e.pos)) {
                armed_ = true;
                pressed_.target = 1;
                return true;
            }
            return false;
        case Event::Type::MouseRelease:
            if (armed_) {
                armed_ = false;
                pressed_.target = 0;
                if (enabled && bounds.contains(e.pos) && on_click_) on_click_();
                return true;
            }
            return false;
        case Event::Type::KeyPress:
            if (focused && (e.key == Key::Enter || e.key == Key::Space)) {
                if (enabled && on_click_) on_click_();
                return true;
            }
            return false;
        default:
            return false;
        }
    }

    void tick(Time t) override {
        hover_.advance(t.dt);
        pressed_.advance(t.dt);
    }

    const char* widget_type() const override { return "Button"; }

    bool stretch_cross() const override { return false; }
    bool draw_border_ = true;

private:
    Color base_color(const Theme& t) const {
        switch (variant_) {
        case Variant::Primary: return t.accent;
        case Variant::Danger:  return t.danger;
        case Variant::Ghost:   return Color::transparent();
        default:               return t.surface_alt;
        }
    }

    void refresh_label() const {
        if (label_valid_ || !ctx) return;
        const Theme& t = ctx->theme();
        cached_text_ = ctx->font().layout(text_, t.s(t.base_font_px));
        label_valid_ = true;
    }

    void paint_text(const PaintEnv& env, Vec2 top_left, Color col, float inv) const {
        silk::paint_text(env, cached_text_, top_left, col, inv);
    }

    std::string text_;
    Variant variant_ = Variant::Default;
    float pad_h_ = 14, pad_v_ = 7;
    Anim hover_{0, 0, 15}, pressed_{0, 0, 25};
    bool armed_ = false;
    mutable bool label_valid_ = false;
    mutable text::TextLayout cached_text_;
    std::function<void()> on_click_;
};

// ---------------------------------------------------------------------------
// Radio — single-select circle marker. Exclusivity is the app's job: from
// on_change, set_checked(true) on the picked radio and set_checked(false) on
// the rest of its group (or drive `checked` from app state each frame).
// ---------------------------------------------------------------------------
class Radio : public Widget {
public:
    explicit Radio(std::string label = {}, bool checked = false, std::string widget_id = {})
        : Widget(std::move(widget_id)), label_(std::move(label)), checked_(checked) {}

    Radio& on_change(std::function<void(bool)> f) { on_change_ = std::move(f); return *this; }
    Radio& set_checked(bool c, bool fire_callback = false) {
        checked_ = c;
        if (fire_callback && on_change_) on_change_(checked_);
        return *this;
    }
    bool checked() const { return checked_; }

    bool focusable() const override { return true; }

    Size measure(const Constraints& c) override {
        refresh_label();
        const Theme& t = ctx->theme();
        float inv = 1.f / ui_scale();
        float box = 18.f;
        float h = std::fmax(box, cached_text_.size.h * inv);
        return c.clamp({box + (label_.empty() ? 0 : 8.f + cached_text_.size.w * inv), h});
    }

    void paint(const PaintEnv& env) const override {
        refresh_label();
        const Theme& t = env.theme;
        float inv = 1.f / ui_scale();
        Vec2 c{bounds.x + 9, bounds.center().y};
        env.list.circle(c, 9, checked_ ? t.accent : t.surface);
        env.list.hollow_rect({c.x - 9, c.y - 9, 18, 18},
                             checked_ ? t.accent : t.border, t.border_width, 9);
        if (checked_) env.list.circle(c, 4, t.accent_text);
        if (!label_.empty()) {
            Vec2 top_left{c.x + 9 + 8.f,
                          bounds.center().y - cached_text_.size.h * inv * .5f};
            silk::paint_text(env, cached_text_, top_left, env.theme.text, inv);
        }
    }

    bool on_event(const Event& e) override {
        // A radio selects itself; it never unselects on click.
        if (e.type == Event::Type::MousePress && enabled && bounds.contains(e.pos)) {
            select();
            return true;
        }
        if (e.type == Event::Type::KeyPress && focused && e.key == Key::Space) {
            select();
            return true;
        }
        return false;
    }

    const char* widget_type() const override { return "Radio"; }

    bool stretch_cross() const override { return false; }

private:
    void select() {
        if (checked_ || !enabled) return;
        checked_ = true;
        if (on_change_) on_change_(true);
    }
    void refresh_label() const {
        if (label_valid_ || !ctx) return;
        const Theme& t = ctx->theme();
        cached_text_ = ctx->font().layout(label_, t.s(t.base_font_px));
        label_valid_ = true;
    }

    std::string label_;
    bool checked_;
    mutable bool label_valid_ = false;
    mutable text::TextLayout cached_text_;
    std::function<void(bool)> on_change_;
};

// ---------------------------------------------------------------------------
// Checkbox
// ---------------------------------------------------------------------------
class Checkbox : public Widget {
public:
    explicit Checkbox(std::string label = {}, bool checked = false, std::string widget_id = {})
        : Widget(std::move(widget_id)), label_(std::move(label)), checked_(checked) {}

    Checkbox& on_change(std::function<void(bool)> f) { on_change_ = std::move(f); return *this; }
    Checkbox& set_checked(bool c, bool fire_callback = false) {
        checked_ = c;
        if (fire_callback && on_change_) on_change_(checked_);
        return *this;
    }
    bool checked() const { return checked_; }

    bool focusable() const override { return true; }

    Size measure(const Constraints& c) override {
        refresh_label();
        const Theme& t = ctx->theme();
        float inv = 1.f / ui_scale();
        float box = 18.f;
        float h = std::fmax(box, cached_text_.size.h * inv);
        return c.clamp({box + (label_.empty() ? 0 : 8.f + cached_text_.size.w * inv), h});
    }

    void paint(const PaintEnv& env) const override {
        refresh_label();
        const Theme& t = env.theme;
        float inv = 1.f / ui_scale();
        Rect box{bounds.x, bounds.center().y - 9, 18, 18};
        env.list.rect(box, checked_ ? t.accent : t.surface, 4);
        env.list.hollow_rect(box, checked_ ? t.accent : t.border, t.border_width, 4);

        if (checked_) {
            Color mark = t.accent_text;
            env.list.line({box.x + box.w * .25f, box.y + box.h * .5f},
                          {box.x + box.w * .42f, box.y + box.h * .78f}, mark, 2);
            env.list.line({box.x + box.w * .42f, box.y + box.h * .78f},
                          {box.x + box.w * .78f, box.y + box.h * .22f}, mark, 2);
        }
        if (!label_.empty()) {
            // Logical gap: bounds are in points; the sink scales once. Do NOT
            // route this through Theme::s() — that is raster px, and it would
            // double-scale the gap (16pt at 2x instead of 8pt).
            Vec2 top_left{box.right() + 8.f,
                          bounds.center().y - cached_text_.size.h * inv * .5f};
            silk::paint_text(env, cached_text_, top_left, env.theme.text, inv);
        }
    }

    bool on_event(const Event& e) override {
        if (e.type == Event::Type::MousePress && enabled && bounds.contains(e.pos)) {
            toggle();
            return true;
        }
        if (e.type == Event::Type::KeyPress && focused && e.key == Key::Space) {
            toggle();
            return true;
        }
        return false;
    }

    const char* widget_type() const override { return "Checkbox"; }

    bool stretch_cross() const override { return false; }

private:
    void toggle() {
        checked_ = !checked_;
        if (on_change_) on_change_(checked_);
    }
    void refresh_label() const {
        if (label_valid_ || !ctx) return;
        const Theme& t = ctx->theme();
        cached_text_ = ctx->font().layout(label_, t.s(t.base_font_px));
        label_valid_ = true;
    }

    std::string label_;
    bool checked_;
    mutable bool label_valid_ = false;
    mutable text::TextLayout cached_text_;
    std::function<void(bool)> on_change_;
};

// ---------------------------------------------------------------------------
// Switch
// ---------------------------------------------------------------------------
class Switch : public Widget {
public:
    explicit Switch(std::string widget_id = {}) : Widget(std::move(widget_id)) {}

    Switch& on_change(std::function<void(bool)> f) { on_change_ = std::move(f); return *this; }
    Switch& set_checked(bool c, bool fire_callback = false) {
        checked_ = c;
        knob_.target = c ? 1.f : 0.f;
        if (fire_callback && on_change_) on_change_(checked_);
        return *this;
    }
    bool checked() const { return checked_; }

    bool focusable() const override { return true; }

    Size measure(const Constraints& c) override { return c.clamp({44.f, 24.f}); }

    void paint(const PaintEnv& env) const override {
        const Theme& t = env.theme;
        float k = knob_.value;
        env.list.rect(bounds, t.surface_alt.lerp(t.accent, k), bounds.h * .5f);
        float d = bounds.h - 8;
        env.list.circle({bounds.x + 4 + k * (bounds.w - 8 - d), bounds.center().y}, d * .5f,
                        Color::white());
    }

    bool on_event(const Event& e) override {
        if (e.type == Event::Type::MousePress && enabled && bounds.contains(e.pos)) {
            set_checked(!checked_, true);
            return true;
        }
        if (e.type == Event::Type::KeyPress && focused && e.key == Key::Space) {
            set_checked(!checked_, true);
            return true;
        }
        return false;
    }

    void tick(Time t) override { knob_.advance(t.dt); }

    const char* widget_type() const override { return "Switch"; }

    bool stretch_cross() const override { return false; }

private:
    bool checked_ = false;
    Anim knob_{0, 0, 18};
    std::function<void(bool)> on_change_;
};

// ---------------------------------------------------------------------------
// Slider
// ---------------------------------------------------------------------------
class Slider : public Widget {
public:
    explicit Slider(float min = 0, float max = 1, std::string widget_id = {})
        : Widget(std::move(widget_id)), min_(min), max_(max), value_(min) {}

    Slider& set_range(float min, float max) {
        min_ = min;
        max_ = max;
        set_value(value_, false);
        return *this;
    }
    Slider& set_value(float v, bool fire_callback = true) {
        v = std::clamp(v, min_, max_);
        if (v == value_) return *this;
        value_ = v;
        fraction_.target = fraction();
        if (fire_callback && on_change_) on_change_(value_);
        return *this;
    }
    float value() const { return value_; }
    Slider& on_change(std::function<void(float)> f) { on_change_ = std::move(f); return *this; }

    bool focusable() const override { return true; }

    Size measure(const Constraints& c) override { return c.clamp({160.f, 22.f}); }

    void paint(const PaintEnv& env) const override {
        const Theme& t = env.theme;
        float cy = bounds.center().y;
        float x0 = bounds.x + 8, x1 = bounds.right() - 8;
        env.list.line({x0, cy}, {x1, cy}, t.surface_alt, 4);
        float fx = x0 + fraction_.value * (x1 - x0);
        if (fx > x0) env.list.line({x0, cy}, {fx, cy}, t.accent, 4);
        env.list.circle({fx, cy}, 7, Color::white());
        env.list.circle({fx, cy}, 6, t.accent);
    }

    bool on_event(const Event& e) override {
        switch (e.type) {
        case Event::Type::MousePress:
            if (enabled && bounds.contains(e.pos)) {
                ctx->capture_mouse(this);
                dragging_ = true;
                value_from_pos(e.pos.x);
                return true;
            }
            return false;
        case Event::Type::MouseMove:
            if (dragging_) {
                value_from_pos(e.pos.x);
                return true;
            }
            return false;
        case Event::Type::MouseRelease:
            if (dragging_) {
                dragging_ = false;
                ctx->release_mouse(this);
                return true;
            }
            return false;
        case Event::Type::KeyPress:
            if (!focused) return false;
            switch (e.key) {
            case Key::Left:
            case Key::Down: set_value(value_ - (max_ - min_) / 20.f); return true;
            case Key::Right:
            case Key::Up:   set_value(value_ + (max_ - min_) / 20.f); return true;
            case Key::Home: set_value(min_); return true;
            case Key::End:  set_value(max_); return true;
            default: return false;
            }
        default:
            return false;
        }
    }

    void tick(Time t) override {
        fraction_.target = fraction();
        fraction_.advance(t.dt);
    }

    const char* widget_type() const override { return "Slider"; }

private:
    float fraction() const {
        return max_ > min_ ? clamp01((value_ - min_) / (max_ - min_)) : 0.f;
    }
    void value_from_pos(float x) {
        float x0 = bounds.x + 8, x1 = bounds.right() - 8;
        float frac = x1 > x0 ? clamp01((x - x0) / (x1 - x0)) : 0.f;
        set_value(min_ + frac * (max_ - min_));
    }

    float min_, max_, value_;
    bool dragging_ = false;
    Anim fraction_{0, 0, 20};
    std::function<void(float)> on_change_;
};

// ---------------------------------------------------------------------------
// ProgressBar
// ---------------------------------------------------------------------------
class ProgressBar : public Widget {
public:
    explicit ProgressBar(std::string widget_id = {}) : Widget(std::move(widget_id)) {}

    ProgressBar& set_value(float v) { value_ = clamp01(v); return *this; }
    float value() const { return value_; }

    Size measure(const Constraints& c) override { return c.clamp({120.f, 8.f}); }

    void paint(const PaintEnv& env) const override {
        const Theme& t = env.theme;
        env.list.rect(bounds, t.surface_alt, bounds.h * .5f);
        float w = value_ * bounds.w;
        if (w > 0) env.list.rect({bounds.x, bounds.y, w, bounds.h}, t.accent, bounds.h * .5f);
    }

    const char* widget_type() const override { return "ProgressBar"; }

private:
    float value_ = 0;
};

// ---------------------------------------------------------------------------
// Spinner (indeterminate)
// ---------------------------------------------------------------------------
class Spinner : public Widget {
public:
    explicit Spinner(std::string widget_id = {}) : Widget(std::move(widget_id)) {}

    Spinner& set_speed(float deg_per_sec) { speed_ = deg_per_sec; return *this; }

    Size measure(const Constraints& c) override { return c.clamp({20.f, 20.f}); }

    void tick(Time t) override { angle_ += float(t.dt) * speed_; }

    void paint(const PaintEnv& env) const override {
        Vec2 c = bounds.center();
        float r = std::fmin(bounds.w, bounds.h) * .5f;
        for (int i = 0; i < 8; ++i) {
            float a = (angle_ + i * 45.f) * 3.14159265f / 180.f;
            Vec2 p{c.x + std::cos(a) * r, c.y + std::sin(a) * r};
            float fade = 1.f - float(i) / 8.f;
            env.list.circle(p, 1.6f, env.theme.text.faded(.25f + .75f * fade));
        }
    }

    const char* widget_type() const override { return "Spinner"; }

    bool stretch_cross() const override { return false; }

private:
    float angle_ = 0;
    float speed_ = 360.f;
};

} // namespace silk
