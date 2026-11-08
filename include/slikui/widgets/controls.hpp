// slikui/widgets/controls.hpp — Button, Checkbox, Switch, Slider, ProgressBar,
// Spinner. All controls follow the same pattern: fluent setters, theme-driven
// colors, Anim-driven hover/press transitions, focusable keyboard support.
#pragma once

#include "slikui/context.hpp"
#include "slikui/style/anim.hpp"
#include "slikui/widgets/env.hpp"
#include "slikui/widgets/widget.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace slikui {

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
        slikui::paint_text(env, cached_text_, top_left, col, inv);
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
            slikui::paint_text(env, cached_text_, top_left, env.theme.text, inv);
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
            slikui::paint_text(env, cached_text_, top_left, env.theme.text, inv);
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
        if (step_ > 0) v = min_ + std::round((v - min_) / step_) * step_;
        v = std::clamp(v, min_, max_);
        if (v == value_) return *this;
        value_ = v;
        fraction_.target = fraction();
        label_valid_ = false;
        if (fire_callback && on_change_) on_change_(value_);
        return *this;
    }
    float value() const { return value_; }
    Slider& on_change(std::function<void(float)> f) { on_change_ = std::move(f); return *this; }

    // Built-in label + live value row above the track (the demo helper this
    // replaces was: Row[label, value-Label] + Slider underneath).
    Slider& set_label(std::string label) { label_ = std::move(label); label_valid_ = false; return *this; }
    // printf format for the value text; showing a value implies the label row.
    Slider& set_format(std::string fmt) { fmt_ = std::move(fmt); fmt_set_ = true; label_valid_ = false; return *this; }
    // Position maps through log(); requires min > 0 (silently ignored else).
    Slider& set_log_scale(bool on) {
        log_ = on && min_ > 0.f && max_ > min_;
        set_value(value_, false);
        return *this;
    }
    // Quantize values to multiples of step (step 1 = integer mode). 0 = off.
    Slider& set_step(float step) { step_ = step; set_value(value_, false); return *this; }

    bool focusable() const override { return true; }

    Size measure(const Constraints& c) override {
        return c.clamp({160.f, shows_value() ? 37.f : 22.f});
    }

    void paint(const PaintEnv& env) const override {
        const Theme& t = env.theme;
        refresh_value_text();
        float inv = 1.f / ui_scale();
        float label_h = shows_value() ? 15.f : 0.f;
        float cy = bounds.y + label_h + (bounds.h - label_h) * .5f;
        float x0 = bounds.x + 8, x1 = bounds.right() - 8;
        env.list.line({x0, cy}, {x1, cy}, t.surface_alt, 4);
        float fx = x0 + fraction_.value * (x1 - x0);
        if (fx > x0) env.list.line({x0, cy}, {fx, cy}, t.accent, 4);
        env.list.circle({fx, cy}, 7, Color::white());
        env.list.circle({fx, cy}, 6, t.accent);
        if (shows_value()) {
            float ty = bounds.y + 1;
            if (!label_.empty()) paint_text(env, label_lay_, {bounds.x, ty}, t.text_dim, inv);
            float vw = value_lay_.size.w * inv;
            paint_text(env, value_lay_, {bounds.right() - vw, ty}, t.text_dim, inv);
        }
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
            case Key::Down: set_value(value_ - key_step()); return true;
            case Key::Right:
            case Key::Up:   set_value(value_ + key_step()); return true;
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
    bool shows_value() const { return !label_.empty() || fmt_set_; }
    float key_step() const { return step_ > 0 ? step_ : (max_ - min_) / 20.f; }

    float fraction() const {
        if (max_ <= min_) return 0.f;
        if (log_) return clamp01(std::log(value_ / min_) / std::log(max_ / min_));
        return clamp01((value_ - min_) / (max_ - min_));
    }
    void value_from_pos(float x) {
        float x0 = bounds.x + 8, x1 = bounds.right() - 8;
        float frac = x1 > x0 ? clamp01((x - x0) / (x1 - x0)) : 0.f;
        float v = log_ ? min_ * std::pow(max_ / min_, frac) : min_ + frac * (max_ - min_);
        set_value(v);
    }

    void refresh_value_text() const {
        if (label_valid_ || !ctx) return;
        const Theme& t = ctx->theme();
        if (!label_.empty()) label_lay_ = ctx->font().layout(label_, t.s(t.base_font_px));
        char buf[64];
        std::snprintf(buf, sizeof buf, fmt_.c_str(), double(value_));
        value_lay_ = ctx->font().layout(buf, t.s(t.base_font_px));
        label_valid_ = true;
    }

    float min_, max_, value_;
    std::string label_;
    std::string fmt_ = "%.3g";
    bool fmt_set_ = false, log_ = false;
    float step_ = 0;
    bool dragging_ = false;
    Anim fraction_{0, 0, 20};
    std::function<void(float)> on_change_;
    mutable text::TextLayout label_lay_, value_lay_;
    mutable bool label_valid_ = false;
};

// ---------------------------------------------------------------------------
// Segmented — N-option single-select row (a Dropdown for 2-5 options)
// ---------------------------------------------------------------------------
class Segmented : public Widget {
public:
    explicit Segmented(std::string widget_id = {}) : Widget(std::move(widget_id)) {}

    Segmented& set_items(std::vector<std::string> items) {
        items_ = std::move(items);
        selected_ = -1;
        hovered_ = -1;
        labels_valid_ = false;
        return *this;
    }
    Segmented& set_selected(long index, bool fire_callback = true) {
        if (index == selected_) return *this;
        selected_ = index;
        if (fire_callback && on_select_ && selected_ >= 0) on_select_(size_t(selected_));
        return *this;
    }
    long selected() const { return selected_; }
    Segmented& on_select(std::function<void(size_t)> f) { on_select_ = std::move(f); return *this; }

    bool stretch_cross() const override { return false; }

    Size measure(const Constraints& c) override {
        refresh_labels();
        float inv = 1.f / ui_scale();
        float w = 0;
        for (auto& l : lays_) w += l.size.w * inv + 24.f;
        return c.clamp({std::fmax(w, 60.f), 26.f});
    }

    void paint(const PaintEnv& env) const override {
        refresh_labels();
        const Theme& t = env.theme;
        float inv = 1.f / ui_scale();
        env.list.rect(bounds, t.surface_alt, t.radius);
        float x = bounds.x;
        for (size_t i = 0; i < items_.size(); ++i) {
            float w = lays_[i].size.w * inv + 24.f;
            Rect seg{x, bounds.y, w, bounds.h};
            float ty = seg.center().y - lays_[i].size.h * inv * .5f;
            if (long(i) == selected_) {
                env.list.rect(seg, t.accent, t.radius);
                paint_text(env, lays_[i], {x + 12, ty}, t.accent_text, inv);
            } else {
                Color txt = long(i) == hovered_ && enabled ? t.text : t.text_dim;
                paint_text(env, lays_[i], {x + 12, ty}, txt, inv);
            }
            x += w;
        }
    }

    bool on_event(const Event& e) override {
        switch (e.type) {
        case Event::Type::MouseMove:
            hovered_ = index_at(e.pos.x);
            return false;
        case Event::Type::MousePress:
            if (enabled && bounds.contains(e.pos)) {
                long idx = index_at(e.pos.x);
                if (idx >= 0) set_selected(idx);
                return true;
            }
            return false;
        default:
            return false;
        }
    }

    const char* widget_type() const override { return "Segmented"; }

private:
    long index_at(float x) const {
        float inv = 1.f / ui_scale();
        float acc = bounds.x;
        for (size_t i = 0; i < items_.size(); ++i) {
            acc += lays_[i].size.w * inv + 24.f;
            if (x < acc) return long(i);
        }
        return -1;
    }
    void refresh_labels() const {
        if (labels_valid_ || !ctx) return;
        const Theme& t = ctx->theme();
        lays_.clear();
        for (auto& it : items_) lays_.push_back(ctx->font().layout(it, t.s(t.base_font_px)));
        labels_valid_ = true;
    }

    std::vector<std::string> items_;
    long selected_ = -1, hovered_ = -1;
    std::function<void(size_t)> on_select_;
    mutable std::vector<text::TextLayout> lays_;
    mutable bool labels_valid_ = false;
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

} // namespace slikui
