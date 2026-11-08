// slikui/widgets/editors.hpp — numeric and color editors for demo panels.
//
//   DragValue   click-drag to scrub a number; click to type it (Enter
//               commits, Esc or focus loss cancels); Up/Down/Left/Right step.
//   VecNEdit    N DragValues bound to a borrowed float[N] (zero-copy — the
//               widget mirrors the array every tick).
//   ColorEdit   color chip that opens a popup: SV square, hue bar, hex field,
//               RGBA fields.
//
// All follow the house pattern: fluent setters, theme tokens, Anim hovers,
// focusable keyboard support.
#pragma once

#include "slikui/context.hpp"
#include "slikui/style/anim.hpp"
#include "slikui/widgets/containers.hpp"
#include "slikui/widgets/env.hpp"
#include "slikui/widgets/inputs.hpp"
#include "slikui/widgets/widget.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>

namespace slikui {

// ---------------------------------------------------------------------------
// DragValue
// ---------------------------------------------------------------------------
class DragValue : public Widget {
public:
    explicit DragValue(float value = 0, std::string widget_id = {})
        : Widget(std::move(widget_id)), value_(value) {}

    DragValue& set_value(float v, bool fire_callback = true) {
        if (has_range_) v = std::clamp(v, min_, max_);
        if (step_ > 0) v = min_ + std::round((v - min_) / step_) * step_;
        if (has_range_) v = std::clamp(v, min_, max_);
        if (int_mode_) v = std::round(v);
        if (v == value_) return *this;
        value_ = v;
        label_valid_ = false;
        if (fire_callback && on_change_) on_change_(value_);
        return *this;
    }
    float value() const { return value_; }

    DragValue& set_range(float min, float max) {
        min_ = min;
        max_ = max;
        has_range_ = max > min;
        set_value(value_, false);
        return *this;
    }
    // Units per pixel of horizontal drag. Negative = auto (1/200 of range,
    // or 0.02 when unbounded).
    DragValue& set_speed(float units_per_px) { speed_ = units_per_px; return *this; }
    // Keyboard step (Up/Down/Left/Right). 0 = auto (range/20, or 1).
    DragValue& set_step(float step) { step_ = step; return *this; }
    // Whole-number display + rounding. step 1 + int_mode = integer field.
    DragValue& set_int_mode(bool on) { int_mode_ = on; set_value(value_, false); return *this; }
    DragValue& set_prefix(std::string p) { prefix_ = std::move(p); label_valid_ = false; return *this; }
    DragValue& set_suffix(std::string s) { suffix_ = std::move(s); label_valid_ = false; return *this; }
    DragValue& set_format(std::function<std::string(float)> f) {
        format_ = std::move(f);
        label_valid_ = false;
        return *this;
    }
    DragValue& on_change(std::function<void(float)> f) { on_change_ = std::move(f); return *this; }

    bool focusable() const override { return true; }
    bool stretch_cross() const override { return false; }

    Size measure(const Constraints& c) override {
        refresh_label();
        float inv = 1.f / ui_scale();
        return c.clamp({std::fmax(cached_w_ * inv + 16.f, 64.f), 24.f});
    }

    void paint(const PaintEnv& env) const override {
        refresh_label();
        const Theme& t = env.theme;
        env.list.rect(bounds, editing_ ? t.background : t.surface_alt, t.radius);
        env.list.hollow_rect(bounds, focused ? t.accent : t.border, t.border_width, t.radius);
        if (hover_.value > 0 && !editing_)
            env.list.hollow_rect(bounds, t.accent.faded(.4f * hover_.value), t.border_width, t.radius);

        float inv = 1.f / ui_scale();
        float text_y = bounds.center().y - cached_lay_.size.h * inv * .5f;
        paint_text(env, cached_lay_, {bounds.x + 8, text_y}, t.text, inv);
        if (editing_ && caret_visible_) {
            float px = t.s(t.base_font_px);
            float cx = bounds.x + 8 + ctx->font().advance_upto(buffer_, buffer_.size(), px) * inv;
            env.list.line({cx, text_y + 2}, {cx, text_y + cached_lay_.size.h * inv - 2}, t.text, 1);
        }
    }

    bool on_event(const Event& e) override {
        switch (e.type) {
        case Event::Type::MousePress:
            if (e.button == MouseLeft && enabled && bounds.contains(e.pos)) {
                ctx->capture_mouse(this);
                dragging_ = true;
                dragged_ = false;
                press_x_ = e.pos.x;
                press_value_ = value_;
                return true;
            }
            return false;
        case Event::Type::MouseMove:
            if (dragging_) {
                if (!dragged_ && std::fabs(e.pos.x - press_x_) > 3) dragged_ = true;
                if (dragged_) set_value(press_value_ + (e.pos.x - press_x_) * scrub_speed());
                return true;
            }
            hover_.target = bounds.contains(e.pos) ? 1.f : 0.f;
            return false;
        case Event::Type::MouseRelease:
            if (e.button == MouseLeft && dragging_) {
                dragging_ = false;
                ctx->release_mouse(this);
                if (!dragged_) start_editing(); // a click, not a scrub
                return true;
            }
            return false;
        case Event::Type::TextInput: {
            if (!editing_ || e.codepoint == 0 || e.codepoint < 32) return false;
            char c = char(e.codepoint);
            if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+') {
                buffer_ += c;
                label_valid_ = false;
                return true;
            }
            return false;
        }
        case Event::Type::KeyPress: {
            if (editing_) {
                if (e.key == Key::V && e.shortcut_mod()) { // paste into the buffer
                    for (char c : ctx->clipboard_text())
                        if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+')
                            buffer_ += c;
                    label_valid_ = false;
                    return true;
                }
                switch (e.key) {
                case Key::Enter:   commit_editing(); return true;
                case Key::Escape:  cancel_editing(); return true;
                case Key::Backspace:
                    if (!buffer_.empty()) {
                        buffer_.pop_back();
                        label_valid_ = false;
                    }
                    return true;
                default: return false;
                }
            }
            if (!focused) return false;
            switch (e.key) {
            case Key::Up:
            case Key::Right: set_value(value_ + step()); return true;
            case Key::Down:
            case Key::Left:  set_value(value_ - step()); return true;
            case Key::Enter: start_editing(); return true;
            default: return false;
            }
        }
        default:
            return false;
        }
    }

    void tick(Time t) override {
        hover_.advance(t.dt);
        // Focus was taken away while typing (Esc routes through Context, and
        // so does any click outside): abandon the edit without committing.
        if (editing_ && !focused) cancel_editing();
        if (editing_) {
            blink_ += t.dt;
            if (blink_ > 0.53) blink_ = 0;
            caret_visible_ = blink_ < 0.35;
        }
    }

    const char* widget_type() const override { return "DragValue"; }

private:
    float scrub_speed() const {
        if (speed_ >= 0) return speed_;
        return has_range_ ? (max_ - min_) / 200.f : 0.02f;
    }
    float step() const {
        if (step_ > 0) return step_;
        if (int_mode_) return 1.f; // integer fields step whole numbers
        return has_range_ ? (max_ - min_) / 20.f : 1.f;
    }

    void start_editing() {
        editing_ = true;
        blink_ = 0;
        caret_visible_ = true;
        buffer_.clear(); // fresh entry: typing replaces, not appends
        label_valid_ = false;
    }
    void commit_editing() {
        editing_ = false;
        const char* s = buffer_.c_str();
        char* end = nullptr;
        double v = std::strtod(s, &end);
        if (end != s) set_value(float(v));
        label_valid_ = false;
    }
    void cancel_editing() {
        editing_ = false;
        label_valid_ = false;
    }

    void refresh_label() const {
        if (label_valid_ || !ctx) return;
        const Theme& t = ctx->theme();
        char buf[64];
        if (format_)                 text_ = format_(value_);
        else if (int_mode_) { std::snprintf(buf, sizeof buf, "%d", int(std::lround(value_))); text_ = buf; }
        else                 { std::snprintf(buf, sizeof buf, "%.3g", double(value_)); text_ = buf; }
        cached_lay_ = ctx->font().layout(prefix_ + text_ + suffix_, t.s(t.base_font_px));
        cached_w_ = cached_lay_.size.w;
        label_valid_ = true;
    }

    float value_;
    float min_ = 0, max_ = 0;
    bool has_range_ = false;
    float speed_ = -1, step_ = 0;
    bool int_mode_ = false;
    std::string prefix_, suffix_;
    std::function<std::string(float)> format_;
    std::function<void(float)> on_change_;

    bool dragging_ = false, dragged_ = false, editing_ = false;
    float press_x_ = 0, press_value_ = 0;
    std::string buffer_;
    double blink_ = 0;
    bool caret_visible_ = true;
    Anim hover_{0, 0, 20};

    // paint/measure are const; caches go through the Button-style mutable set.
    mutable std::string text_;
    mutable text::TextLayout cached_lay_;
    mutable float cached_w_ = 0;
    mutable bool label_valid_ = false;
};

// ---------------------------------------------------------------------------
// VecNEdit — DragValue row bound to a borrowed float[N]
// ---------------------------------------------------------------------------
// `values` is borrowed, not copied: the widget writes edits straight into it
// and re-reads it every tick, so external code can move the values (camera
// params, material uniforms) and the fields follow. The array must outlive
// the widget.
template <size_t N>
class VecNEdit : public Widget {
public:
    explicit VecNEdit(float* values, std::string labels = "XYZ", std::string widget_id = {})
        : Widget(std::move(widget_id)), values_(values) {
        Row& row = add<Row>();
        row.set_gap(kGap);
        for (size_t i = 0; i < N; ++i) {
            DragValue& dv = row.add<DragValue>(values[i]);
            if (i < labels.size()) dv.set_prefix(labels.substr(i, 1));
            dv.on_change([this, i](float v) {
                values_[i] = v;
                if (on_change_) on_change_();
            });
        }
    }

    VecNEdit& set_labels(std::string labels) {
        Row& row = static_cast<Row&>(*children[0]);
        for (size_t i = 0; i < N; ++i)
            static_cast<DragValue&>(*row.children[i])
                .set_prefix(i < labels.size() ? labels.substr(i, 1) : std::string());
        return *this;
    }
    VecNEdit& set_range(float min, float max) {
        for (auto& c : children[0]->children) static_cast<DragValue&>(*c).set_range(min, max);
        return *this;
    }
    VecNEdit& set_axis_range(size_t axis, float min, float max) {
        if (axis < N) static_cast<DragValue&>(*children[0]->children[axis]).set_range(min, max);
        return *this;
    }
    VecNEdit& set_int_mode(bool on) {
        for (auto& c : children[0]->children) static_cast<DragValue&>(*c).set_int_mode(on);
        return *this;
    }
    VecNEdit& set_step(float step) {
        for (auto& c : children[0]->children) static_cast<DragValue&>(*c).set_step(step);
        return *this;
    }
    VecNEdit& on_change(std::function<void()> f) { on_change_ = std::move(f); return *this; }

    Size measure(const Constraints& c) override {
        float w = float(N - 1) * kGap;
        for (auto& ch : children[0]->children)
            w += ch->measure(Constraints::loose({kInfinite, kInfinite})).w;
        return c.clamp({w, 24.f});
    }

    void arrange(const Rect& area) override {
        bounds = area;
        Row& row = static_cast<Row&>(*children[0]);
        row.arrange({area.x, area.y, area.w, area.h});
    }

    void tick(Time t) override {
        // Mirror the borrowed array: external writes show up automatically.
        Row& row = static_cast<Row&>(*children[0]);
        for (size_t i = 0; i < N; ++i)
            static_cast<DragValue&>(*row.children[i]).set_value(values_[i], false);
        Widget::tick(t);
    }

    const char* widget_type() const override { return N == 2 ? "Vec2Edit" : N == 3 ? "Vec3Edit" : "Vec4Edit"; }
    bool stretch_cross() const override { return false; }

    static constexpr float kGap = 4;

private:
    float* values_;
    std::function<void()> on_change_;
};

using Vec2Edit = VecNEdit<2>;
using Vec3Edit = VecNEdit<3>;
using Vec4Edit = VecNEdit<4>;

// ---------------------------------------------------------------------------
// ColorEdit — chip + popup editor
// ---------------------------------------------------------------------------
class ColorEdit;

// Popup body for ColorEdit. The SV square and hue bar are self-painted (the
// software rasterizer has no gradient primitive: the square is a grid of
// quads, the bar a row of them). The hex field and RGBA fields are real
// children so focus/typing come for free. Changes flow through apply() into
// the owning ColorEdit, which fires its on_change.
class ColorPopup : public Widget {
public:
    ColorPopup(ColorEdit* owner, Color initial);

    Size measure(const Constraints& c) override { return c.clamp({kW, kH}); }

    void arrange(const Rect& area) override;

    void paint(const PaintEnv& env) const override;

    bool on_event(const Event& e) override;

    const char* widget_type() const override { return "ColorPopup"; }

    static constexpr float kW = 176;
    static constexpr float kH = 198;
    static constexpr float kPad = 8;

private:
    static constexpr float kSvW = kW - 2 * kPad;  // 160
    static constexpr float kSvH = 108;
    static constexpr float kHueH = 10;

    Rect sv_rect() const { return {bounds.x + kPad, bounds.y + kPad, kSvW, kSvH}; }
    Rect hue_rect() const { return {bounds.x + kPad, bounds.y + kPad + kSvH + 6, kSvW, kHueH}; }
    Rect row_rect() const { return {bounds.x + kPad, bounds.y + kPad + kSvH + 6 + kHueH + 6, kSvW, 24}; }
    Rect hex_rect() const {
        return {bounds.x + kPad, bounds.y + kPad + kSvH + 6 + kHueH + 6 + 24 + 4, kSvW, 24};
    }

    void apply(Color c);
    void refresh_fields();

    ColorEdit* owner_;
    float h_ = 0, s_ = 0, v_ = 1, alpha_ = 1;
    bool guard_ = false; // suppresses feedback while pushing values into fields
    bool dragging_sv_ = false, dragging_hue_ = false;
};

class ColorEdit : public Widget {
public:
    explicit ColorEdit(Color color = Color::white(), std::string widget_id = {})
        : Widget(std::move(widget_id)), color_(color) {}

    ColorEdit& set_color(Color c, bool fire_callback = true) {
        color_ = c;
        if (fire_callback && on_change_) on_change_(color_);
        return *this;
    }
    const Color& color() const { return color_; }
    ColorEdit& on_change(std::function<void(Color)> f) { on_change_ = std::move(f); return *this; }

    bool stretch_cross() const override { return false; }

    Size measure(const Constraints& c) override { return c.clamp({26.f, 26.f}); }

    void paint(const PaintEnv& env) const override {
        const Theme& t = env.theme;
        env.list.rect(bounds, color_, 4);
        env.list.hollow_rect(bounds, hover_.value > 0 ? t.text_dim : t.border, 1, 4);
    }

    bool on_event(const Event& e) override {
        switch (e.type) {
        case Event::Type::MousePress:
            if (e.button == MouseLeft && enabled && bounds.contains(e.pos)) {
                auto popup = std::make_unique<ColorPopup>(this, color_);
                ctx->push_popup(std::move(popup), {bounds.x, bounds.bottom() + 2});
                return true;
            }
            return false;
        case Event::Type::MouseMove:
            hover_.target = bounds.contains(e.pos) ? 1.f : 0.f;
            return false;
        default:
            return false;
        }
    }

    void tick(Time t) override { hover_.advance(t.dt); }

    const char* widget_type() const override { return "ColorEdit"; }

    // Called by the popup (single direction: popup -> chip). External
    // set_color while the popup is open only updates the chip.
    void apply_color(Color c) {
        color_ = c;
        if (on_change_) on_change_(color_);
    }

private:
    Color color_;
    std::function<void(Color)> on_change_;
    Anim hover_{0, 0, 20};
};

inline ColorPopup::ColorPopup(ColorEdit* owner, Color initial) : owner_(owner) {
    auto hsv = initial.to_hsv();
    h_ = hsv[0];
    s_ = hsv[1];
    v_ = hsv[2];
    alpha_ = float(initial.a) / 255.f;

    auto& row = add<Row>();
    row.set_gap(4);
    const char* names = "RGBA";
    for (int i = 0; i < 4; ++i) {
        DragValue& dv = row.add<DragValue>(i < 3 ? (&initial.r)[i] : initial.a);
        dv.set_prefix(std::string(1, names[i])).set_range(0, 255).set_step(1).set_int_mode(true);
        dv.on_change([this, i](float v) {
            if (guard_) return;
            Color c = owner_->color();
            (&c.r)[i] = std::uint8_t(std::clamp(v, 0.f, 255.f));
            apply(c);
        });
    }
    // (arranged manually below; paint_tree still recurses into children)

    auto& hex = add<TextField>();
    hex.on_change([this](std::string_view text) {
        if (guard_) return;
        std::string s(text);
        if (!s.empty() && s.front() == '#') s.erase(0, 1);
        if (s.size() > 2 && (s[0] == '0') && (s[1] == 'x' || s[1] == 'X')) s.erase(0, 2);
        if (s.size() != 6) return;
        for (char ch : s)
            if (!std::isxdigit(unsigned(ch))) return;
        apply(Color(std::uint8_t(std::strtoul(s.substr(0, 2).c_str(), nullptr, 16)),
                    std::uint8_t(std::strtoul(s.substr(2, 2).c_str(), nullptr, 16)),
                    std::uint8_t(std::strtoul(s.substr(4, 2).c_str(), nullptr, 16)),
                    owner_->color().a));
    });
    refresh_fields();
}

inline void ColorPopup::arrange(const Rect& area) {
    bounds = area;
    // The Row/TextField are arranged by hand: their order in the popup does
    // not follow container layout, and Widget::paint_tree only needs bounds.
    Row& row = static_cast<Row&>(*children[0]);
    row.bounds = row_rect();
    float fw = (kSvW - 3 * 4) / 4;
    float x = row.bounds.x;
    for (auto& c : row.children) {
        c->arrange({x, row.bounds.y, fw, row.bounds.h});
        x += fw + 4;
    }
    static_cast<TextField&>(*children[1]).arrange(hex_rect());
}

inline void ColorPopup::paint(const PaintEnv& env) const {
    const Theme& t = env.theme;
    env.list.rect(bounds, t.surface, t.radius);
    env.list.hollow_rect(bounds, t.border, t.border_width, t.radius);

    // SV square: saturation across, value down. Grid of quads — cheap in the
    // software rasterizer and exact enough to look like a gradient.
    Rect sv = sv_rect();
    constexpr int kCols = 32, kRows = 24;
    float cw = sv.w / kCols, chh = sv.h / kRows;
    for (int gy = 0; gy < kRows; ++gy)
        for (int gx = 0; gx < kCols; ++gx)
            env.list.rect({sv.x + gx * cw, sv.y + gy * chh, cw + .5f, chh + .5f},
                          Color::from_hsv(h_, float(gx) / (kCols - 1), 1.f - float(gy) / (kRows - 1)));
    // SV cursor: little ring at the current s/v.
    Vec2 cur{sv.x + s_ * sv.w, sv.y + (1.f - v_) * sv.h};
    env.list.circle(cur, 5, Color::white());
    env.list.circle(cur, 4, Color::from_hsv(h_, s_, v_));

    // Hue bar: 32 segments across the wheel.
    Rect hue = hue_rect();
    float hw = hue.w / 32;
    for (int i = 0; i < 32; ++i)
        env.list.rect({hue.x + i * hw, hue.y, hw + .5f, hue.h}, Color::from_hsv(float(i) / 32.f, 1, 1));
    float hx = hue.x + h_ * hue.w;
    env.list.line({hx, hue.y - 1}, {hx, hue.y + hue.h + 1}, Color::white(), 2);

    // Fields are painted by paint_tree (Row + TextField children).
}

inline bool ColorPopup::on_event(const Event& e) {
    Rect sv = sv_rect(), hue = hue_rect();
    switch (e.type) {
    case Event::Type::MousePress:
        if (e.button == MouseLeft && (sv.contains(e.pos) || hue.contains(e.pos))) {
            ctx->capture_mouse(this);
            dragging_sv_ = sv.contains(e.pos);
            dragging_hue_ = hue.contains(e.pos);
        } else {
            break;
        }
        [[fallthrough]];
    case Event::Type::MouseMove: {
        if (dragging_sv_ && sv.w > 0 && sv.h > 0) {
            s_ = clamp01((e.pos.x - sv.x) / sv.w);
            v_ = 1.f - clamp01((e.pos.y - sv.y) / sv.h);
            apply(Color::from_hsv(h_, s_, v_, owner_->color().a));
            return true;
        }
        if (dragging_hue_ && hue.w > 0) {
            h_ = clamp01((e.pos.x - hue.x) / hue.w);
            apply(Color::from_hsv(h_, s_, v_, owner_->color().a));
            return true;
        }
        return false;
    }
    case Event::Type::MouseRelease:
        if (e.button == MouseLeft && (dragging_sv_ || dragging_hue_)) {
            dragging_sv_ = dragging_hue_ = false;
            ctx->release_mouse(this);
            return true;
        }
        return false;
    default:
        return false;
    }
    return false; // press that landed outside SV/hue (breaks out of the switch)
}

inline void ColorPopup::apply(Color c) {
    if (guard_) return;
    guard_ = true;
    owner_->apply_color(c);
    auto hsv = c.to_hsv();
    h_ = hsv[0];
    s_ = hsv[1];
    v_ = hsv[2];
    alpha_ = float(c.a) / 255.f;
    refresh_fields();
    guard_ = false;
}

inline void ColorPopup::refresh_fields() {
    Color c = Color::from_hsv(h_, s_, v_, std::uint8_t(alpha_ * 255.f + .5f));
    Row& row = static_cast<Row&>(*children[0]);
    for (int i = 0; i < 4; ++i)
        static_cast<DragValue&>(*row.children[i]).set_value((&c.r)[i], false);
    char buf[8];
    std::snprintf(buf, sizeof buf, "%02X%02X%02X", c.r, c.g, c.b);
    static_cast<TextField&>(*children[1]).set_text(buf);
}

} // namespace slikui
