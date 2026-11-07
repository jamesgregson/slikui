// slikui/context.hpp — Context: root widget tree + theme + font + sink wiring.
//
// Typical frame:
//   while (running) {
//     while (event = window.poll()) ctx.handle(event);
//     ctx.tick({now, dt});   // attach -> layout -> widget animation ticks
//     ctx.render();          // paint -> sink
//   }
#pragma once

#include "slikui/layout/layout.hpp"
#include "slikui/render/sink.hpp"
#include "slikui/style/theme.hpp"
#include "slikui/text/font.hpp"
#include "slikui/widgets/env.hpp"
#include "slikui/widgets/widget.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace slikui {

// Builds the tooltip popup for `w` at `pos`. Defined after Context (needs the
// complete type); declared here so Context::show_tooltip can call it.
void show_tooltip_for(Context& ctx, Widget& w, Vec2 pos);

class Context {
public:
    explicit Context(IRenderSink& sink) : sink_(&sink) {
        font_.set_texture_id(next_texture_id());
    }

    // --- configuration ---

    Theme& theme() { return theme_; }
    const Theme& theme() const { return theme_; }
    text::Font& font() { return font_; }

    bool load_font_file(const std::string& path) { return font_.load_file(path); }
    bool load_font_memory(const std::uint8_t* data, size_t len) {
        return font_.load_memory(data, len);
    }

    void set_frame_size(Vec2 size) { frame_ = size; }
    Vec2 frame_size() const { return frame_; }

    // --- tree ---

    template <class T, class... Args>
    T& set_root(Args&&... args) {
        root_ = std::make_unique<T>(std::forward<Args>(args)...);
        return *static_cast<T*>(root_.get());
    }
    void set_root(std::unique_ptr<Widget> w) { root_ = std::move(w); }
    Widget* root() { return root_.get(); }

    Widget* find(const std::string& id) { return root_ ? root_->find(id) : nullptr; }

    // --- input ---

    // NOTE: popup destruction is deferred to the end of handle() — callbacks
    // may close the popup they're executing in.
    void handle(const Event& e) {
        handle_event(e);
        closing_.clear();
    }

    Widget* focused() { return focused_; }
    void set_focus(Widget* w) {
        if (focused_ == w) return;
        if (focused_) focused_->focused = false;
        focused_ = w;
        if (focused_) focused_->focused = true;
    }

    // --- frame ---

    void tick(Time t) {
        if (!root_) return;
        attach_tree(*root_);
        root_->measure(Constraints::tight({frame_.x, frame_.y}));
        root_->arrange({0, 0, frame_.x, frame_.y});
        root_->tick_tree(t);
        for (auto& p : popups_) layout_popup(p);
        update_tooltip(t);
    }

    void render(bool clear = true) {
        if (!sink_) return;
        upload_dirty_textures();
        DrawList list;
        const PaintEnv env{list, theme_, font_};
        if (clear) list.rect({0, 0, frame_.x, frame_.y}, theme_.background);
        if (root_) root_->paint_tree(env);
        for (auto& p : popups_) p.widget->paint_tree(env);
        sink_->render(list, frame_);
    }

    // --- popups ---

    // Overlay rendered above everything; auto-dismissed when clicking outside.
    Widget& push_popup(std::unique_ptr<Widget> w, Vec2 pos) {
        Widget& ref = *w;
        popups_.push_back({std::move(w), pos});
        attach_tree(ref);
        layout_popup(popups_.back());
        return ref;
    }
    void close_popups() {
        for (auto& p : popups_) {
            Widget* root = p.widget.get();
            if (focused_ && inside(focused_, root)) set_focus(nullptr);
            closing_.push_back(std::move(p.widget));
        }
        popups_.clear();
    }
    bool has_popups() const { return !popups_.empty(); }
    size_t popup_count() const { return popups_.size(); }
    Widget* popup(size_t i) { return popups_[i].widget.get(); }

    // --- textures ---

    std::uint32_t next_texture_id() { return next_tex_id_++; }

    // --- mouse capture ---

    // While captured, all mouse move/press/release events go to `w`, even
    // outside its bounds (dragging a slider, selecting text, ...).
    void capture_mouse(Widget* w) { mouse_capture_ = w; }
    void release_mouse(Widget* w) {
        if (mouse_capture_ == w) mouse_capture_ = nullptr;
    }

private:
    struct Popup {
        std::unique_ptr<Widget> widget;
        Vec2 pos;
    };

    static bool inside(Widget* w, Widget* root) {
        for (Widget* chain = w; chain; chain = chain->parent)
            if (chain == root) return true;
        return false;
    }

    void handle_event(const Event& e);

    void layout_popup(Popup& p) {
        Size s = p.widget->measure(Constraints::loose({frame_.x, frame_.y}));
        // Clamp inside the frame.
        s.w = std::fmin(s.w, std::fmax(frame_.x - p.pos.x, 40.f));
        s.h = std::fmin(s.h, std::fmax(frame_.y - p.pos.y, 40.f));
        p.widget->arrange({p.pos.x, p.pos.y, s.w, s.h});
    }

    // Dispatch an event up the chain from `w` to the root, until consumed.
    void dispatch(Widget& w, const Event& e) {
        for (Widget* chain = &w; chain; chain = chain->parent)
            if (chain->on_event(e)) return;
    }

    void attach_tree(Widget& w) {
        if (w.ctx != this) {
            w.ctx = this;
            w.attached(*this);
        }
        for (auto& c : w.children) attach_tree(*c);
        // NOTE: popups are attached individually in push_popup — never here,
        // or a popup re-attaching its own subtree recurses forever.
    }

    void upload_dirty_textures() {
        if (font_.valid() && font_.atlas_dirty()) {
            TexUpload up{font_.atlas_id(), font_.atlas_width(), font_.atlas_height(),
                         TexFormat::Alpha8, font_.atlas_pixels().data()};
            sink_->upload(up);
            font_.clear_dirty();
        }
        // Widgets that own textures (Image, ...) report pending uploads.
        uploads_.clear();
        if (root_) root_->collect_tree(uploads_);
        for (auto& p : popups_) p.widget->collect_tree(uploads_);
        for (const auto& up : uploads_) sink_->upload(up);
    }

    // --- tooltips ---

    void set_hover(Widget* w, Vec2 pos) {
        hover_pos_ = pos;
        if (w == hover_) return;
        hover_ = w;
        hover_since_ = -1;
        if (tooltip_open_) {
            close_popups();
            tooltip_open_ = false;
        }
    }

    void update_tooltip(Time t) {
        if (tooltip_open_) {
            // Closed implicitly when the hover target changed (set_hover);
            // also close if the widget lost its tooltip text.
            if ((!hover_ || hover_->tooltip_.empty()) && popups_.empty())
                tooltip_open_ = false;
            return;
        }
        if (!hover_ || hover_->tooltip_.empty() || !popups_.empty() || inside_press_) {
            hover_since_ = -1;
            return;
        }
        if (hover_since_ < 0) {
            hover_since_ = t.now;
            return;
        }
        if (t.now - hover_since_ >= 0.6) show_tooltip(*hover_);
    }

    void show_tooltip(Widget& w) {
        show_tooltip_for(*this, w, {hover_pos_.x + 12, hover_pos_.y + 18});
        tooltip_open_ = true;
    }

    Widget* hit_test(Vec2 pos) {
        if (mouse_capture_) return mouse_capture_;
        for (auto& p : popups_) { // popups float above the root tree
            if (Widget* w = hit_test_in(*p.widget, pos)) return w;
        }
        return root_ ? hit_test_in(*root_, pos) : nullptr;
    }

    Widget* hit_test_in(Widget& w, Vec2 pos) {
        if (!w.visible) return nullptr;
        for (auto it = w.children.rbegin(); it != w.children.rend(); ++it)
            if (Widget* hit_child = hit_test_in(**it, pos)) return hit_child;
        return w.bounds.contains(pos) ? &w : nullptr;
    }

    Widget* nearest_focusable(Widget* w) {
        while (w && !w->focusable()) w = w->parent;
        return w;
    }

    void cycle_focus(bool backwards) {
        std::vector<Widget*> focusables;
        collect_focusables(root_.get(), focusables);
        for (auto& p : popups_) collect_focusables(p.widget.get(), focusables);
        if (focusables.empty()) return;
        size_t idx = 0;
        for (size_t i = 0; i < focusables.size(); ++i)
            if (focusables[i] == focused_) idx = i + 1;
        if (backwards) idx = (idx == 0 ? focusables.size() : idx) - 1;
        set_focus(focusables[idx % focusables.size()]);
    }

    void collect_focusables(Widget* w, std::vector<Widget*>& out) {
        if (!w || !w->visible) return;
        if (w->focusable() && w->enabled) out.push_back(w);
        for (auto& c : w->children) collect_focusables(c.get(), out);
    }

    IRenderSink* sink_;
    Theme theme_;
    text::Font font_;
    std::unique_ptr<Widget> root_;
    Widget* focused_ = nullptr;
    Vec2 frame_{};
    std::uint32_t next_tex_id_ = 1;
    std::vector<Popup> popups_;
    std::vector<std::unique_ptr<Widget>> closing_; // deferred destruction
    bool inside_press_ = false; // mouse button held from a press inside the tree
    Widget* mouse_capture_ = nullptr;

    // tooltip state
    Widget* hover_ = nullptr;
    Vec2 hover_pos_{};
    double hover_since_ = -1;
    bool tooltip_open_ = false;
    std::vector<TexUpload> uploads_;
};

// Free accessor used by Widget::ui_scale() (Context is complete here).
inline float theme_scale_of(const Context* ctx) {
    return ctx ? ctx->theme().scale : 1.f;
}

// Minimal text widget for tooltip popups. Lives in this header (not
// widgets/text.hpp) to keep Context free of include cycles.
class TooltipLabel : public Widget {
public:
    explicit TooltipLabel(std::string text) : text_(std::move(text)) {}

    Size measure(const Constraints& c) override {
        if (!valid_ && ctx && ctx->font().valid()) {
            lay_ = ctx->font().layout(text_, ctx->theme().s(ctx->theme().small_font_px));
            valid_ = true;
        }
        float inv = 1.f / ui_scale();
        return c.clamp({lay_.size.w * inv, lay_.size.h * inv});
    }

    void paint(const PaintEnv& env) const override {
        paint_text(env, lay_, {bounds.x, bounds.y}, env.theme.text, 1.f / env.theme.scale);
    }

    const char* widget_type() const override { return "TooltipLabel"; }
    bool stretch_cross() const override { return false; }

private:
    std::string text_;
    mutable text::TextLayout lay_;
    mutable bool valid_ = false;
};

// Rounded box + tooltip text, anchored just below-right of the cursor
// (Context::layout_popup clamps it into the frame). Self-contained: uses only
// primitives defined in this header, so containers.hpp can include context.hpp.
class TooltipBox : public Widget {
public:
    explicit TooltipBox(std::string text) {
        auto lab = std::make_unique<TooltipLabel>(std::move(text));
        Widget::add(std::move(lab));
    }

    Size measure(const Constraints& c) override {
        Size s = children[0]->measure(Constraints::loose({kInfinite, kInfinite}));
        return c.clamp({s.w + 12, s.h + 10});
    }

    void arrange(const Rect& area) override {
        bounds = area;
        children[0]->arrange({area.x + 6, area.y + 5,
                              std::fmax(area.w - 12, 0.f), std::fmax(area.h - 10, 0.f)});
    }

    void paint(const PaintEnv& env) const override {
        env.list.rect(bounds, env.theme.surface, 4);
        env.list.hollow_rect(bounds, env.theme.border, env.theme.border_width, 4);
        // label painted by Widget::paint_tree
    }

    const char* widget_type() const override { return "TooltipBox"; }
};

inline void show_tooltip_for(Context& ctx, Widget& w, Vec2 pos) {
    ctx.push_popup(std::make_unique<TooltipBox>(w.tooltip()), pos);
}

// --- input routing ---------------------------------------------------------

inline void Context::handle_event(const Event& e) {
    using T = Event::Type;

    switch (e.type) {
    case T::FrameResize:
        // e.pos is the framebuffer size in px; the layout frame is logical.
        frame_ = {e.pos.x / theme_.scale, e.pos.y / theme_.scale};
        return;
    case T::FocusLost:
        set_focus(nullptr);
        close_popups();
        return;
    case T::MouseMove: {
        Widget* hit = hit_test(e.pos);
        if (hit) dispatch(*hit, e);
        set_hover(hit, e.pos);
        return;
    }
    case T::MousePress:
        if (tooltip_open_) {
            close_popups();
            tooltip_open_ = false;
            hover_since_ = -1;
        }
        if (e.button == MouseLeft) {
            if (Widget* hit = hit_test(e.pos)) {
                // Click outside an open popup dismisses it and stops here.
                bool hit_popup = false;
                for (auto& p : popups_)
                    if (p.widget->bounds.contains(e.pos)) hit_popup = true;
                if (has_popups() && !hit_popup) {
                    close_popups();
                    return;
                }
                inside_press_ = true;
                dispatch(*hit, e);
                if (Widget* f = nearest_focusable(hit)) set_focus(f);
                return;
            }
            set_focus(nullptr);
        }
        return;
    case T::MouseRelease:
        inside_press_ = false;
        if (Widget* hit = hit_test(e.pos)) dispatch(*hit, e);
        return;
    case T::Scroll:
        if (Widget* hit = hit_test(e.pos)) dispatch(*hit, e);
        return;
    case T::TextInput:
        if (focused_) dispatch(*focused_, e); // text goes to the focused field
        return;
    case T::KeyPress: {
        if (e.key == Key::Escape) {
            if (has_popups()) {
                close_popups();
                return;
            }
            set_focus(nullptr);
            return;
        }
        if (e.key == Key::Tab) {
            cycle_focus(e.shift());
            return;
        }
        if (focused_) {
            dispatch(*focused_, e);
            return;
        }
        if (Widget* hit = hit_test(e.pos)) dispatch(*hit, e);
        return;
    }
    case T::KeyRelease:
        return;
    }
}

} // namespace slikui
