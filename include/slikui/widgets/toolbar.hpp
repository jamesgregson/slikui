// slikui/widgets/toolbar.hpp — IconButton + Toolbar.
//
// IconButton is Button's geometry and interaction with a procedural icon
// (widgets/icons.hpp) instead of text — Ghost by default, the way toolbar
// buttons read. Toolbar is a Row of them with separators, a bottom rule and
// an optional icon-over-label mode for chunkier app-toolbars.
#pragma once

#include "slikui/style/anim.hpp"
#include "slikui/widgets/containers.hpp"
#include "slikui/widgets/controls.hpp"
#include "slikui/widgets/icons.hpp"
#include "slikui/widgets/text.hpp"

#include <algorithm>
#include <functional>
#include <string>
#include <utility>

namespace slikui {

// ---------------------------------------------------------------------------
// IconButton — small square button with an icon instead of text.
// ---------------------------------------------------------------------------
class IconButton : public Widget {
public:
    IconButton(Icon icon, std::string widget_id = {})
        : Widget(std::move(widget_id)), icon_(icon) {}

    IconButton& set_icon(Icon i) { icon_ = i; return *this; }
    Icon icon() const { return icon_; }
    IconButton& set_variant(Button::Variant v) { variant_ = v; return *this; }
    Button::Variant variant() const { return variant_; }
    IconButton& set_icon_px(float px) { icon_px_ = px; return *this; }
    IconButton& on_click(std::function<void()> f) { on_click_ = std::move(f); return *this; }

    bool focusable() const override { return true; }

    Size measure(const Constraints& c) override {
        return c.clamp({icon_px_ + pad_h_ * 2,
                        std::fmax(icon_px_ + pad_v_ * 2, 26.f)});
    }

    void paint(const PaintEnv& env) const override {
        const Theme& t = env.theme;
        Color base = base_color(t);
        Color fill = base;
        if (variant_ != Button::Variant::Ghost || hover_.value > 0)
            fill = base.lerp(t.hover(base), hover_.value);
        fill = fill.lerp(t.press(base), pressed_.value);
        if (!enabled) fill = t.surface_alt.faded(.6f);

        env.list.rect(bounds, fill, t.radius);
        if (variant_ != Button::Variant::Ghost)
            env.list.hollow_rect(bounds, t.border, t.border_width, t.radius);

        float side = std::fmin(bounds.w, bounds.h);
        paint_icon(env.list, icon_,
                   {bounds.center().x - side * .5f, bounds.center().y - side * .5f,
                    side, side},
                   enabled ? icon_color(t) : t.text_dim);
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

    const char* widget_type() const override { return "IconButton"; }

    bool stretch_cross() const override { return false; }

private:
    Color base_color(const Theme& t) const {
        switch (variant_) {
        case Button::Variant::Primary: return t.accent;
        case Button::Variant::Danger: return t.danger;
        case Button::Variant::Ghost: return Color::transparent();
        default: return t.surface_alt;
        }
    }
    Color icon_color(const Theme& t) const {
        switch (variant_) {
        case Button::Variant::Primary:
        case Button::Variant::Danger:
            return t.accent_text;
        default:
            return t.text;
        }
    }

    Icon icon_;
    Button::Variant variant_ = Button::Variant::Ghost;
    float icon_px_ = 16, pad_h_ = 7, pad_v_ = 5;
    Anim hover_{0, 0, 15}, pressed_{0, 0, 25};
    bool armed_ = false;
    std::function<void()> on_click_;
};

// ---------------------------------------------------------------------------
// Toolbar — horizontal strip of IconButtons with separators and a bottom
// rule. `add_button(icon, label, ...)` renders the label under the icon.
// ---------------------------------------------------------------------------
class Toolbar : public Row {
public:
    explicit Toolbar(std::string widget_id = {}) : Row(std::move(widget_id)) {
        set_padding(4).set_gap(2).set_cross(CrossMode::Center);
    }

    Toolbar& set_background(Color c) { background_ = c; return *this; }
    Toolbar& set_icon_px(float px) { icon_px_ = px; return *this; }

    IconButton& add_button(Icon icon, std::function<void()> on_click,
                           std::string tip = {}) {
        auto& b = add<IconButton>(icon);
        b.set_icon_px(icon_px_).on_click(std::move(on_click));
        if (!tip.empty()) b.set_tooltip(std::move(tip));
        return b;
    }
    // Icon with a small label underneath (a hugging cell keeps them together;
    // a plain Column would stretch to the toolbar's cross size).
    IconButton& add_button(Icon icon, std::string label,
                           std::function<void()> on_click, std::string tip = {}) {
        auto& cell = add<LabelCell>();
        cell.set_gap(2).set_cross(CrossMode::Center);
        auto& b = cell.add<IconButton>(icon);
        b.set_icon_px(icon_px_).on_click(std::move(on_click));
        if (!tip.empty()) b.set_tooltip(std::move(tip));
        if (!label.empty())
            cell.add<Label>(std::move(label)).set_font_size(12.f); // small caption
        return b;
    }
    void add_separator() { add<Divider>(false); }

    Size measure(const Constraints& c) override {
        Size s = Row::measure(c);
        s.h = std::fmax(s.h, 30.f);
        return c.clamp(s);
    }

    void paint(const PaintEnv& env) const override {
        if (background_.a > 0) env.list.rect(bounds, background_, 0);
        env.list.line({bounds.x, bounds.bottom() - 1},
                      {bounds.right(), bounds.bottom() - 1}, env.theme.border, 1);
    }

    const char* widget_type() const override { return "Toolbar"; }

    class LabelCell : public Column {
    public:
        using Column::Column;
        bool stretch_cross() const override { return false; }
    };

private:
    Color background_ = Color::transparent();
    float icon_px_ = 16;
};

} // namespace slikui
