// slikui/widgets/views.hpp — ScrollArea, ListView, Tabs, Dropdown.
#pragma once

#include "slikui/context.hpp"
#include "slikui/widgets/containers.hpp"
#include "slikui/widgets/controls.hpp"
#include "slikui/widgets/env.hpp"
#include "slikui/widgets/widget.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace slikui {

// ---------------------------------------------------------------------------
// ScrollArea — vertical scroll region with a clipped content column.
// ---------------------------------------------------------------------------
class ScrollArea : public Widget {
public:
    explicit ScrollArea(std::string widget_id = {}) : Widget(std::move(widget_id)) {
        auto col = std::make_unique<Column>();
        col->set_gap(4);
        content_ = col.get();
        Widget::add(std::unique_ptr<Widget>(std::move(col))); // real child so events propagate
    }

    template <class T, class... Args>
    T& add(Args&&... args) {
        return content_->add<T>(std::forward<Args>(args)...);
    }
    Column& content() { return *content_; }

    Size measure(const Constraints& c) override { return c.clamp({hint_w_, hint_h_}); }

    void arrange(const Rect& area) override {
        bounds = area;
        Size content_size = content_->measure(Constraints::loose({area.w, kInfinite}));
        content_height_ = content_size.h;
        max_scroll_ = std::fmax(0.f, content_height_ - area.h);
        scroll_y_ = std::clamp(scroll_y_, 0.f, max_scroll_);
        // Inset the content so anything drawn slightly outside a widget's
        // bounds (selection rings, shadows) isn't clipped by the scroll
        // region, and keep a clear gap before the scrollbar gutter.
        float right = content_inset_r_;
        if (max_scroll_ > 0) right += bar_w_ + 12.f;
        content_->arrange({area.x + content_inset_l_, area.y - scroll_y_,
                           std::fmax(0.f, area.w - content_inset_l_ - right),
                           content_height_});
    }

    std::optional<Rect> clip_rect() const override { return bounds; }

    Rect bar_rect() const {
        float bar_h = std::fmax(24.f, bounds.h * bounds.h / content_height_);
        float track = bounds.h - bar_h;
        float y = bounds.y + (max_scroll_ > 0 ? scroll_y_ / max_scroll_ * track : 0.f);
        return {bounds.right() - bar_w_ - 2.f, y, bar_w_, bar_h};
    }

    void paint(const PaintEnv& env) const override {
        // scrollbar thumb in its reserved gutter (content is clipped around it)
        if (max_scroll_ > 0 && bounds.h > 0) {
            Rect bar = bar_rect();
            Color c = env.theme.text_dim.faded(drag_bar_ ? .75f : .4f);
            env.list.rect(bar, c, bar.w * .5f);
        }
    }

    bool on_event(const Event& e) override {
        using T = Event::Type;
        if (drag_bar_) {
            if (e.type == T::MouseMove) {
                float bar_h = bar_rect().h;
                float track = bounds.h - bar_h;
                if (track > 1.f)
                    scroll_y_ = std::clamp(
                        (e.pos.y - drag_grab_ - bounds.y) / track * max_scroll_, 0.f,
                        max_scroll_);
                return true;
            }
            if (e.type == T::MouseRelease && e.button == MouseLeft) {
                drag_bar_ = false;
                ctx->release_mouse(this);
                return true;
            }
            return false;
        }
        switch (e.type) {
        case T::MousePress: {
            if (e.button != MouseLeft || max_scroll_ <= 0 || !bounds.contains(e.pos))
                return false;
            Rect bar = bar_rect();
            if (bar.contains(e.pos)) { // grab the thumb
                drag_bar_ = true;
                drag_grab_ = e.pos.y - bar.y;
                ctx->capture_mouse(this);
                return true;
            }
            if (e.pos.x >= bounds.right() - bar_w_ - 6.f) { // click on the track: jump+drag
                drag_bar_ = true;
                drag_grab_ = bar.h * .5f;
                ctx->capture_mouse(this);
                Event move = e; // reuse press position as the first drag move
                move.type = T::MouseMove;
                return on_event(move);
            }
            return false;
        }
        case T::Scroll:
            if (!bounds.contains(e.pos)) return false;
            scroll_y_ = std::clamp(scroll_y_ - e.scroll.y * 40.f, 0.f, max_scroll_);
            return true;
        default:
            return false;
        }
    }

    const char* widget_type() const override { return "ScrollArea"; }

    float hint_w_ = 200, hint_h_ = 150;

private:
    Column* content_ = nullptr;
    float scroll_y_ = 0, max_scroll_ = 0, content_height_ = 0;
    float bar_w_ = 8;           // thumb width; sits 2px from the right edge
    float content_inset_l_ = 8; // breathing room inside the clip rect
    float content_inset_r_ = 8;
    bool drag_bar_ = false;
    float drag_grab_ = 0;  // cursor offset inside the thumb while dragging
};

// ---------------------------------------------------------------------------
// ListView — vertical list of string rows with selection.
// ---------------------------------------------------------------------------
class ListView : public Widget {
public:
    explicit ListView(std::string widget_id = {}) : Widget(std::move(widget_id)) {}

    ListView& set_items(std::vector<std::string> items) {
        items_ = std::move(items);
        valid_ = false;
        return *this;
    }
    ListView& set_selected(long index) {
        selected_ = index;
        return *this;
    }
    long selected() const { return selected_; }
    ListView& on_select(std::function<void(size_t)> f) { on_select_ = std::move(f); return *this; }

    Size measure(const Constraints& c) override {
        refresh_metrics();
        float inv = 1.f / ui_scale();
        return c.clamp({natural_w_ * inv, float(items_.size()) * row_height()});
    }

    void paint(const PaintEnv& env) const override {
        refresh_metrics();
        const Theme& t = env.theme;
        float sc = ui_scale(), inv = 1.f / sc;
        float y = bounds.y;
        for (size_t i = 0; i < items_.size(); ++i) {
            Rect row{bounds.x, y, bounds.w, row_height()};
            if (long(i) == selected_) env.list.rect(row, t.accent.faded(.25f), 4);
            else if (i == hovered_ && enabled)
                env.list.rect(row, t.surface_alt, 4);

            const text::TextLayout& lay = layouts_[i];
            float text_h = lay.size.h * inv;
            for (const auto& pg : lay.glyphs) {
                const text::Glyph& g = *pg.glyph;
                if (g.uv.empty()) continue;
                env.list.glyph({row.x + 8 + pg.pos.x * inv,
                                row.y + (row_height() - text_h) * .5f + pg.pos.y * inv,
                                g.size.w * inv, g.size.h * inv},
                               g.tex_id, g.uv, t.text);
            }
            y += row_height();
        }
    }

    bool on_event(const Event& e) override {
        if (e.type == Event::Type::MouseMove) {
            hovered_ = row_at(e.pos.y);
            return false;
        }
        if (e.type == Event::Type::MousePress && e.button == MouseLeft && enabled && bounds.contains(e.pos)) {
            long idx = row_at(e.pos.y);
            if (idx >= 0) {
                selected_ = idx;
                if (on_select_) on_select_(size_t(idx));
            }
            return true;
        }
        return false;
    }

    const char* widget_type() const override { return "ListView"; }

private:
    float sc() const { return ctx ? ctx->theme().scale : 1.f; }
    // Row height in logical px.
    float row_height() const { return (cached_line_height_ + ctx->theme().s(10.f)) / sc(); }
    long row_at(float y) const {
        if (cached_line_height_ <= 0 || y < bounds.y || y >= bounds.bottom()) return -1;
        long idx = long((y - bounds.y) / row_height());
        return idx < long(items_.size()) ? idx : -1;
    }
    void refresh_metrics() const {
        if (valid_ || !ctx) return;
        if (!ctx->font().valid()) {
            // Fontless: paint() indexes layouts_[i] per item, so keep one
            // (empty) layout per row. valid_ stays false so a font loaded
            // later still rebuilds real layouts.
            if (layouts_.size() != items_.size()) layouts_.assign(items_.size(), {});
            return;
        }
        const Theme& t = ctx->theme();
        float px = t.s(t.base_font_px);
        cached_line_height_ = ctx->font().line_height(px);
        layouts_.clear();
        natural_w_ = 0;
        for (const auto& it : items_) {
            text::TextLayout lay = ctx->font().layout(it, px);
            natural_w_ = std::fmax(natural_w_, lay.size.w + t.s(16.f));
            layouts_.push_back(std::move(lay));
        }
        valid_ = true;
    }

    std::vector<std::string> items_;
    mutable std::vector<text::TextLayout> layouts_;
    long selected_ = -1;
    size_t hovered_ = size_t(-1);
    mutable float cached_line_height_ = 0, natural_w_ = 100;
    mutable bool valid_ = false;
    std::function<void(size_t)> on_select_;
};

// ---------------------------------------------------------------------------
// Tabs — header row with active underline.
// ---------------------------------------------------------------------------
class Tabs : public Widget {
public:
    explicit Tabs(std::string widget_id = {}) : Widget(std::move(widget_id)) {}

    Tabs& set_tabs(std::vector<std::string> labels) {
        labels_ = std::move(labels);
        valid_ = false;
        return *this;
    }
    Tabs& set_selected(size_t index) { selected_ = index; return *this; }
    size_t selected() const { return selected_; }
    Tabs& on_change(std::function<void(size_t)> f) { on_change_ = std::move(f); return *this; }

    Size measure(const Constraints& c) override {
        refresh_metrics();
        float inv = 1.f / ui_scale();
        return c.clamp({natural_w_ * inv, (cached_line_height_ + ctx->theme().s(16.f)) * inv});
    }

    void paint(const PaintEnv& env) const override {
        refresh_metrics();
        const Theme& t = env.theme;
        float sc = ui_scale(), inv = 1.f / sc;
        float x = bounds.x;
        env.list.line({bounds.x, bounds.bottom()}, {bounds.right(), bounds.bottom()}, t.border, 1);
        for (size_t i = 0; i < labels_.size(); ++i) {
            const auto& lay = layouts_[i];
            Color col = i == selected_ ? t.text : t.text_dim;
            bool active = i == selected_;
            for (const auto& pg : lay.glyphs) {
                const text::Glyph& g = *pg.glyph;
                if (g.uv.empty()) continue;
                env.list.glyph({x + pg.pos.x * inv, bounds.y + 8 + pg.pos.y * inv,
                                g.size.w * inv, g.size.h * inv},
                               g.tex_id, g.uv, col);
            }
            if (active)
                env.list.rect({x, bounds.bottom() - 2, lay.size.w * inv, 2}, t.accent, 1);
            if (i == hovered_ && !active)
                env.list.rect({x, bounds.bottom() - 2, lay.size.w * inv, 2}, t.border, 1);
            x += lay.size.w * inv + 20;
        }
    }

    bool on_event(const Event& e) override {
        if (e.type == Event::Type::MouseMove) {
            hovered_ = tab_at(e.pos.x);
            return false;
        }
        if (e.type == Event::Type::MousePress && e.button == MouseLeft && enabled && bounds.contains(e.pos)) {
            size_t idx = tab_at(e.pos.x);
            if (idx != size_t(-1) && idx != selected_) {
                selected_ = idx;
                if (on_change_) on_change_(idx);
            }
            return true;
        }
        return false;
    }

    const char* widget_type() const override { return "Tabs"; }

private:
    float sc() const { return ctx ? ctx->theme().scale : 1.f; }
    size_t tab_at(float x) const {
        float cx = bounds.x;
        for (size_t i = 0; i < layouts_.size(); ++i) {
            float w = layouts_[i].size.w / sc() + 20;
            if (x >= cx && x < cx + w) return i;
            cx += w;
        }
        return size_t(-1);
    }
    void refresh_metrics() const {
        if (valid_ || !ctx) return;
        if (!ctx->font().valid()) {
            // Fontless: see ListView::refresh_metrics — one empty layout per
            // tab keeps paint()'s layouts_[i] in bounds.
            if (layouts_.size() != labels_.size()) layouts_.assign(labels_.size(), {});
            return;
        }
        const Theme& t = ctx->theme();
        float px = t.s(t.base_font_px);
        cached_line_height_ = ctx->font().line_height(px);
        layouts_.clear();
        natural_w_ = 0;
        for (const auto& l : labels_) {
            text::TextLayout lay = ctx->font().layout(l, px);
            natural_w_ += lay.size.w + t.s(20.f);
            layouts_.push_back(std::move(lay));
        }
        valid_ = true;
    }

    std::vector<std::string> labels_;
    mutable std::vector<text::TextLayout> layouts_;
    size_t selected_ = 0, hovered_ = size_t(-1);
    mutable float cached_line_height_ = 0, natural_w_ = 0;
    mutable bool valid_ = false;
    std::function<void(size_t)> on_change_;
};

// ---------------------------------------------------------------------------
// Dropdown — button that opens a ListView popup.
// ---------------------------------------------------------------------------
class Dropdown : public Widget {
public:
    explicit Dropdown(std::string widget_id = {}) : Widget(std::move(widget_id)) {}

    Dropdown& set_items(std::vector<std::string> items) {
        items_ = std::move(items);
        label_valid_ = false;
        return *this;
    }
    Dropdown& set_placeholder(std::string p) { placeholder_ = std::move(p); return *this; }
    Dropdown& set_selected(long index) {
        selected_ = index;
        label_valid_ = false;
        return *this;
    }
    long selected() const { return selected_; }
    Dropdown& on_select(std::function<void(size_t)> f) { on_select_ = std::move(f); return *this; }

    Size measure(const Constraints& c) override {
        refresh_label();
        const Theme& t = ctx->theme();
        float inv = 1.f / ui_scale();
        return c.clamp({std::fmax(label_w_ * inv + 34.f, 120.f),
                        std::fmax(cached_line_height_ * inv + 12.f, 30.f)});
    }

    void paint(const PaintEnv& env) const override {
        refresh_label();
        const Theme& t = env.theme;
        float inv = 1.f / ui_scale();
        env.list.rect(bounds, t.surface_alt, t.radius);
        env.list.hollow_rect(bounds, t.border, t.border_width, t.radius);

        float text_y = bounds.center().y - label_.size.h * inv * .5f;
        for (const auto& pg : label_.glyphs) {
            const text::Glyph& g = *pg.glyph;
            if (g.uv.empty()) continue;
            env.list.glyph({bounds.x + 10 + pg.pos.x * inv, text_y + pg.pos.y * inv,
                            g.size.w * inv, g.size.h * inv},
                           g.tex_id, g.uv, t.text);
        }
        // caret triangle
        float cx = bounds.right() - 14, cy = bounds.center().y;
        env.list.line({cx - 4, cy - 2}, {cx, cy + 2}, t.text_dim, 2);
        env.list.line({cx, cy + 2}, {cx + 4, cy - 2}, t.text_dim, 2);
    }

    bool on_event(const Event& e) override {
        if (e.type == Event::Type::MousePress && e.button == MouseLeft && enabled && bounds.contains(e.pos)) {
            open_popup();
            return true;
        }
        return false;
    }

    const char* widget_type() const override { return "Dropdown"; }

private:
    void refresh_label() const {
        if (label_valid_ || !ctx) return;
        const Theme& t = ctx->theme();
        std::string text = selected_ >= 0 && size_t(selected_) < items_.size()
                               ? items_[size_t(selected_)]
                               : placeholder_;
        label_ = ctx->font().layout(text, t.s(t.base_font_px));
        label_w_ = label_.size.w;
        cached_line_height_ = label_.line_height;
        label_valid_ = true;
    }

    void open_popup() {
        auto panel = std::make_unique<Panel>();
        panel->set_padding(4);
        panel->set_radius(ctx->theme().radius);
        panel->background_ = ctx->theme().surface;
        panel->border_ = ctx->theme().border;
        panel->draw_border_ = true;
        auto& list = panel->add<ListView>();
        list.set_items(items_).set_selected(selected_);
        list.on_select([this, id = std::string{}](size_t idx) {
            set_selected(long(idx));
            if (on_select_) on_select_(idx);
            ctx->close_popups();
        });
        // Longest item decides popup width.
        float inv = 1.f / ui_scale();
        float w = 0;
        for (const auto& it : items_)
            w = std::fmax(w, ctx->font().measure_width(it, ctx->theme().s(ctx->theme().base_font_px)) * inv + 40.f);
        ctx->push_popup(std::move(panel), {bounds.x, bounds.bottom() + 2});
        (void)w; // width derives from ListView natural size (see Context::push_popup)
    }

    std::vector<std::string> items_;
    std::string placeholder_;
    long selected_ = -1;
    mutable bool label_valid_ = false;
    mutable text::TextLayout label_;
    mutable float label_w_ = 0, cached_line_height_ = 0;
    std::function<void(size_t)> on_select_;
};

} // namespace slikui
