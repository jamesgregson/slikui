// silk/widgets/containers.hpp — Column, Row, Panel, Spacer, Divider.
#pragma once

#include "silk/context.hpp"
#include "silk/style/theme.hpp"
#include "silk/widgets/env.hpp"
#include "silk/widgets/widget.hpp"

namespace silk {

// Shared flexbox-style behaviour: stack children along one axis with a gap
// and padding; children may stretch on the cross axis.
class Container : public Widget {
public:
    using Widget::Widget;

    Container& set_padding(float all) { padding_ = Padding::all(all); return *this; }
    Container& set_padding(Padding p) { padding_ = p; return *this; }
    Container& set_gap(float g) { gap_ = g; return *this; }
    Container& set_cross(CrossMode m) { cross_ = m; return *this; }

protected:
    Padding padding_ = Padding::all(0);
    float gap_ = 0;
    CrossMode cross_ = CrossMode::Stretch;

    size_t visible_children() const {
        size_t n = 0;
        for (auto& c : children)
            if (c->visible) ++n;
        return n;
    }

    static Rect apply_cross(Rect r, const Rect& inner, CrossMode mode,
                            bool cross_vertical, bool stretch) {
        if (!stretch) mode = CrossMode::Start;
        switch (mode) {
        case CrossMode::Stretch:
            if (cross_vertical) { r.y = inner.y; r.h = inner.h; }
            else                { r.x = inner.x; r.w = inner.w; }
            break;
        case CrossMode::Center:
            if (cross_vertical) r.y = inner.y + (inner.h - r.h) * .5f;
            else                r.x = inner.x + (inner.w - r.w) * .5f;
            break;
        case CrossMode::Start:
            break;
        }
        return r;
    }
};

// NOTE: containers do NOT paint their children in paint() — Widget::paint_tree
// already recurses. Painting them here too would emit every command twice
// (doubling alpha coverage on text and antialiased edges).

// Stacks children vertically.
class Column : public Container {
public:
    using Container::Container;

    Size measure(const Constraints& c) override {
        Vec2 inner_max{std::fmax(c.max.x - padding_.horizontal(), 0.f),
                       std::fmax(c.max.y - padding_.vertical(), 0.f)};
        float w = 0, h = 0;
        size_t n = 0;
        for (auto& ch : children) {
            if (!ch->visible) continue;
            Size s = ch->measure(Constraints::loose(inner_max));
            w = std::fmax(w, s.w);
            h += s.h;
            ++n;
        }
        h += gap_ * float(n > 0 ? n - 1 : 0);
        return c.clamp({w + padding_.horizontal(), h + padding_.vertical()});
    }

    void arrange(const Rect& area) override {
        bounds = area;
        Rect inner = padding_.apply(area);
        // Measure children first; expanding children (Spacer) split leftovers.
        float used = gap_ * float(visible_children() > 0 ? visible_children() - 1 : 0);
        size_t expanders = 0;
        for (auto& ch : children) {
            if (!ch->visible) continue;
            used += ch->measure(Constraints::loose({inner.w, inner.h})).h;
            if (ch->expands_main()) ++expanders;
        }
        float extra = (expanders > 0 && inner.h > used) ? (inner.h - used) / float(expanders) : 0;
        float y = inner.y;
        for (auto& ch : children) {
            if (!ch->visible) continue;
            Size s = ch->measure(Constraints::loose({inner.w, inner.h}));
            Rect child = apply_cross({inner.x, y, s.w, s.h}, inner, cross_,
                                     /*cross_vertical=*/false, ch->stretch_cross());
            child.y = y;
            if (ch->expands_main()) child.h += extra;
            ch->arrange(child);
            y += child.h + gap_;
        }
    }

    const char* widget_type() const override { return "Column"; }
};

// Stacks children horizontally.
class Row : public Container {
public:
    using Container::Container;

    Size measure(const Constraints& c) override {
        Vec2 inner_max{std::fmax(c.max.x - padding_.horizontal(), 0.f),
                       std::fmax(c.max.y - padding_.vertical(), 0.f)};
        float w = 0, h = 0;
        size_t n = 0;
        for (auto& ch : children) {
            if (!ch->visible) continue;
            Size s = ch->measure(Constraints::loose(inner_max));
            w += s.w;
            h = std::fmax(h, s.h);
            ++n;
        }
        w += gap_ * float(n > 0 ? n - 1 : 0);
        return c.clamp({w + padding_.horizontal(), h + padding_.vertical()});
    }

    void arrange(const Rect& area) override {
        bounds = area;
        Rect inner = padding_.apply(area);
        float used = gap_ * float(visible_children() > 0 ? visible_children() - 1 : 0);
        size_t expanders = 0;
        for (auto& ch : children) {
            if (!ch->visible) continue;
            used += ch->measure(Constraints::loose({inner.w, inner.h})).w;
            if (ch->expands_main()) ++expanders;
        }
        float extra = (expanders > 0 && inner.w > used) ? (inner.w - used) / float(expanders) : 0;
        float x = inner.x;
        for (auto& ch : children) {
            if (!ch->visible) continue;
            Size s = ch->measure(Constraints::loose({inner.w, inner.h}));
            Rect child = apply_cross({x, inner.y, s.w, s.h}, inner, cross_,
                                     /*cross_vertical=*/true, ch->stretch_cross());
            child.x = x;
            if (ch->expands_main()) child.w += extra;
            ch->arrange(child);
            x += child.w + gap_;
        }
    }

    const char* widget_type() const override { return "Row"; }
};

// Decorated container: background, border, padding, stacking like a Column.
class Panel : public Container {
public:
    using Container::Container;

    Panel& set_background(Color c) { background_ = c; return *this; }
    Panel& set_border(Color c) { border_ = c; return *this; }
    Panel& set_radius(float r) { radius_ = r; return *this; }

    Size measure(const Constraints& c) override {
        Vec2 inner_max{std::fmax(c.max.x - padding_.horizontal(), 0.f),
                       std::fmax(c.max.y - padding_.vertical(), 0.f)};
        float w = 0, h = 0;
        size_t n = 0;
        for (auto& ch : children) {
            if (!ch->visible) continue;
            Size s = ch->measure(Constraints::loose(inner_max));
            w = std::fmax(w, s.w);
            h += s.h;
            ++n;
        }
        h += gap_ * float(n > 0 ? n - 1 : 0);
        return c.clamp({w + padding_.horizontal(), h + padding_.vertical()});
    }

    void arrange(const Rect& area) override {
        bounds = area;
        Rect inner = padding_.apply(area);
        float y = inner.y;
        for (auto& ch : children) {
            if (!ch->visible) continue;
            Size s = ch->measure(Constraints::loose({inner.w, inner.h}));
            Rect child = apply_cross({inner.x, y, s.w, s.h}, inner, cross_,
                                     /*cross_vertical=*/false, ch->stretch_cross());
            child.y = y;
            ch->arrange(child);
            y += s.h + gap_;
        }
    }

    void paint(const PaintEnv& env) const override {
        const Theme& t = env.theme;
        env.list.rect(bounds, background_.a > 0 ? background_ : t.surface, radius_);
        if (draw_border_)
            env.list.hollow_rect(bounds, border_.a > 0 ? border_ : t.border, t.border_width, radius_);
        // children painted by Widget::paint_tree
    }

    const char* widget_type() const override { return "Panel"; }

    Color background_ = Color::transparent();
    Color border_ = Color::transparent();
    float radius_ = 0;
    bool draw_border_ = false;
};

// Blank space that expands along the main axis of its parent.
class Spacer : public Widget {
public:
    using Widget::Widget;
    Size measure(const Constraints&) override { return {0, 0}; }
    bool expands_main() const override { return true; }
    const char* widget_type() const override { return "Spacer"; }
};

// Thin separator line.
class Divider : public Widget {
public:
    explicit Divider(bool is_horizontal = true, std::string widget_id = {})
        : Widget(std::move(widget_id)), horizontal(is_horizontal) {}

    Size measure(const Constraints& c) override {
        return c.clamp(horizontal ? Size{0, 1} : Size{1, 0});
    }

    void paint(const PaintEnv& env) const override {
        if (horizontal)
            env.list.line({bounds.x, bounds.center().y}, {bounds.right(), bounds.center().y},
                          env.theme.border, 1);
        else
            env.list.line({bounds.center().x, bounds.y}, {bounds.center().x, bounds.bottom()},
                          env.theme.border, 1);
    }

    const char* widget_type() const override { return "Divider"; }

    bool horizontal = true;
};

// ---------------------------------------------------------------------------
// Expander — collapsible section: a chevron + title header above a Column of
// content. Closed: header only; children get zero-size frames so they stop
// painting and intercepting clicks.
// ---------------------------------------------------------------------------
class Expander : public Container {
public:
    explicit Expander(std::string title = {}, std::string widget_id = {})
        : Container(std::move(widget_id)), title_(std::move(title)) {}

    Expander& set_open(bool o) { open_ = o; return *this; }
    bool open() const { return open_; }
    Expander& set_title(std::string t) { title_ = std::move(t); label_valid_ = false; return *this; }
    Expander& on_toggle(std::function<void(bool)> f) { on_toggle_ = std::move(f); return *this; }

    bool focusable() const override { return true; }

    Size measure(const Constraints& c) override {
        refresh_title();
        float inv = 1.f / ui_scale();
        float header_h = header_height();
        float h = header_h;
        float w = 24.f + cached_title_.size.w * inv;
        if (open_) {
            for (auto& ch : children) {
                if (!ch->visible) continue;
                Size s = ch->measure(Constraints::loose(
                    {std::fmax(c.max.x, 0.f), std::fmax(c.max.y - h, 0.f)}));
                w = std::fmax(w, s.w);
                h += s.h + gap_;
            }
        }
        return c.clamp({w, h});
    }

    void arrange(const Rect& area) override {
        bounds = area;
        header_rect_ = {area.x, area.y, area.w, header_height()};
        float y = header_rect_.bottom();
        for (auto& ch : children) {
            // The expander owns child visibility: closed hides the subtree
            // and zeroes its frame (paint and hit-testing honor visibility).
            ch->visible = open_;
            if (!open_) {
                ch->arrange({area.x, y, 0, 0});
                continue;
            }
            Size s = ch->measure(
                Constraints::loose({area.w, std::fmax(area.bottom() - y, 0.f)}));
            float cw = ch->stretch_cross() ? area.w : s.w;
            ch->arrange({area.x, y, cw, s.h});
            y += s.h + gap_;
        }
    }

    void paint(const PaintEnv& env) const override {
        refresh_title();
        const Theme& t = env.theme;
        float inv = 1.f / ui_scale();
        // Chevron, pointing right when closed, down when open.
        float cx = bounds.x + 9, cy = header_rect_.center().y;
        if (open_) {
            env.list.line({cx - 4, cy - 2}, {cx, cy + 2}, t.text_dim, 2);
            env.list.line({cx, cy + 2}, {cx + 4, cy - 2}, t.text_dim, 2);
        } else {
            env.list.line({cx - 2, cy - 4}, {cx + 2, cy}, t.text_dim, 2);
            env.list.line({cx + 2, cy}, {cx - 2, cy + 4}, t.text_dim, 2);
        }
        Vec2 top_left{bounds.x + 22,
                      header_rect_.center().y - cached_title_.size.h * inv * .5f};
        silk::paint_text(env, cached_title_, top_left, t.text, inv);
        // open children painted by Widget::paint_tree
    }

    bool on_event(const Event& e) override {
        if (e.type == Event::Type::MousePress && enabled && header_rect_.contains(e.pos)) {
            set_open_state(!open_);
            return true;
        }
        if (e.type == Event::Type::KeyPress && focused && e.key == Key::Space) {
            set_open_state(!open_);
            return true;
        }
        return false;
    }

    const char* widget_type() const override { return "Expander"; }

private:
    float header_height() const {
        float inv = 1.f / ui_scale();
        return std::fmax(24.f, cached_title_.size.h * inv + 10.f);
    }
    void set_open_state(bool o) {
        if (open_ == o) return;
        open_ = o;
        if (on_toggle_) on_toggle_(open_);
    }
    void refresh_title() const {
        if (label_valid_ || !ctx) return;
        const Theme& t = ctx->theme();
        cached_title_ = ctx->font().layout(title_, t.s(t.base_font_px));
        label_valid_ = true;
    }

    std::string title_;
    bool open_ = false;
    Rect header_rect_{};
    mutable bool label_valid_ = false;
    mutable text::TextLayout cached_title_;
    std::function<void(bool)> on_toggle_;
};

// ---------------------------------------------------------------------------
// Accordion — a stack of Expander sections. Exclusive mode (the default)
// closes the other sections when one opens, so at most one is expanded at a
// time; set_exclusive(false) turns it into a plain stack of collapsible
// groups. Sections are added with add_section() and filled via the returned
// Expander (a Container: rows, sliders, swatches...).
// ---------------------------------------------------------------------------
class Accordion : public Column {
public:
    using Column::Column;

    Accordion& set_exclusive(bool e) { exclusive_ = e; return *this; }
    bool exclusive() const { return exclusive_; }

    Expander& add_section(std::string title, bool open = false) {
        auto& s = add<Expander>(std::move(title)).set_open(open);
        s.on_toggle([this, &s](bool opening) {
            if (!opening || !exclusive_) return;
            for (auto& c : children)
                if (c.get() != static_cast<Widget*>(&s))
                    static_cast<Expander*>(c.get())->set_open(false);
        });
        return s;
    }

    const char* widget_type() const override { return "Accordion"; }

private:
    bool exclusive_ = true;
};

// ---------------------------------------------------------------------------
// Splitter — two panes with a draggable divider. add() the two panes in
// order; extra children are ignored. vertical=false → left|right,
// vertical=true → top|bottom. The fraction is the first pane's share of the
// space minus the grab area.
// ---------------------------------------------------------------------------
class Splitter : public Widget {
public:
    explicit Splitter(bool vertical = false, std::string widget_id = {})
        : Widget(std::move(widget_id)), vertical_(vertical) {}

    Splitter& set_fraction(float f) { fraction_ = clamp01(f); return *this; }
    float fraction() const { return fraction_; }
    Splitter& on_change(std::function<void(float)> f) { on_change_ = std::move(f); return *this; }

    Size measure(const Constraints& c) override {
        Size a{0, 0}, b{0, 0};
        if (children.size() > 0) a = children[0]->measure(Constraints::loose(c.max));
        if (children.size() > 1) b = children[1]->measure(Constraints::loose(c.max));
        float w, h;
        if (vertical_) {
            w = std::fmax(a.w, b.w);
            h = a.h + b.h + kGrab;
        } else {
            w = a.w + b.w + kGrab;
            h = std::fmax(a.h, b.h);
        }
        return c.clamp({w, h});
    }

    void arrange(const Rect& area) override {
        bounds = area;
        float total = vertical_ ? area.h : area.w;
        float pool = std::fmax(total - kGrab, 0.f);
        float lo = std::fmin(24.f, pool);
        float a_size = clamp01(fraction_) * pool;
        a_size = std::clamp(a_size, lo, std::fmax(lo, pool - std::fmin(24.f, pool)));
        for (size_t i = 2; i < children.size(); ++i) { // extras: hidden, out of the way
            children[i]->visible = false;
            children[i]->arrange({area.x, area.y, 0, 0});
        }
        if (children.empty()) { divider_ = {}; return; }
        if (children.size() == 1) {
            children[0]->arrange(area);
            divider_ = {};
            return;
        }
        if (vertical_) {
            children[0]->arrange({area.x, area.y, area.w, a_size});
            divider_ = {area.x, area.y + a_size, area.w, kGrab};
            children[1]->arrange({area.x, divider_.bottom(), area.w,
                                  std::fmax(0.f, area.h - a_size - kGrab)});
        } else {
            children[0]->arrange({area.x, area.y, a_size, area.h});
            divider_ = {area.x + a_size, area.y, kGrab, area.h};
            children[1]->arrange({divider_.right(), area.y,
                                  std::fmax(0.f, area.w - a_size - kGrab), area.h});
        }
    }

    void paint(const PaintEnv& env) const override {
        const Theme& t = env.theme;
        if (divider_.w > 0 && divider_.h > 0) {
            if (hovered_ || dragging_)
                env.list.rect(divider_, t.surface_alt, 3);
            if (vertical_)
                env.list.line({divider_.x, divider_.center().y},
                              {divider_.right(), divider_.center().y}, t.border, 1);
            else
                env.list.line({divider_.center().x, divider_.y},
                              {divider_.center().x, divider_.bottom()}, t.border, 1);
        }
        // panes painted by Widget::paint_tree
    }

    bool on_event(const Event& e) override {
        switch (e.type) {
        case Event::Type::MouseMove:
            hovered_ = divider_.contains(e.pos);
            if (dragging_) {
                update_fraction(e.pos);
                return true;
            }
            return false;
        case Event::Type::MousePress:
            if (enabled && divider_.contains(e.pos)) {
                dragging_ = true;
                ctx->capture_mouse(this);
                update_fraction(e.pos);
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
        default:
            return false;
        }
    }

    const char* widget_type() const override { return "Splitter"; }

private:
    static constexpr float kGrab = 6.f; // divider hit area, logical px

    void update_fraction(Vec2 p) {
        float rel = vertical_ ? (p.y - bounds.y) : (p.x - bounds.x);
        float pool = std::fmax((vertical_ ? bounds.h : bounds.w) - kGrab, 1.f);
        float f = clamp01((rel - kGrab * .5f) / pool);
        if (f != fraction_) {
            fraction_ = f;
            if (on_change_) on_change_(f);
        }
    }

    bool vertical_;
    float fraction_ = 0.5f;
    Rect divider_{};
    bool hovered_ = false, dragging_ = false;
    std::function<void(float)> on_change_;
};

} // namespace silk
