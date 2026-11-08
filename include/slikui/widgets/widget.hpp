// slikui/widgets/widget.hpp — Widget base class and tree protocol.
//
// Frames are ABSOLUTE framebuffer coordinates, assigned during arrange().
// That makes hit-testing, painting and clipping trivial for every widget.
//
//   measure(Constraints) -> intrinsic Size
//   arrange(Rect abs)    -> assign own frame, then arrange children
//   paint(DrawList&)     -> append draw commands
//   on_event(Event)      -> return true if consumed (children see it first)
//   tick(Time)           -> advance animation state
#pragma once

#include "slikui/core/input.hpp"
#include "slikui/core/math.hpp"
#include "slikui/core/time.hpp"
#include "slikui/layout/layout.hpp"
#include "slikui/render/draw_list.hpp"
#include "slikui/render/sink.hpp"
#include "slikui/widgets/env.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace slikui {

struct Theme;   // style/theme.hpp
struct PaintEnv; // widgets/env.hpp — draw list + theme + font for paint()
class Context;   // context.hpp — back-reference set during arrange
class Widget;

// Defined in context.hpp: logical px per framebuffer px for a widget's context.
float theme_scale_of(const Context* ctx);
// Defined in context.hpp: drop every Context reference (focus, hover, mouse
// capture, press targets) to a widget that is being destroyed.
void context_forget(Context* ctx, const Widget* w) noexcept;

class Widget {
public:
    Widget() = default;
    explicit Widget(std::string id) : id(std::move(id)) {}
    // A destroyed widget must not stay focused/hovered/captured: the Context
    // would later dispatch into freed memory. ctx_alive_ guards the reverse
    // case — a borrowed widget (Menu, Dialog) outliving its Context.
    virtual ~Widget() {
        if (ctx && !ctx_alive_.expired()) context_forget(ctx, this);
    }

    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;

    // --- tree ---

    template <class T, class... Args>
    T& add(Args&&... args) {
        WidgetPtr child(new T(std::forward<Args>(args)...));
        T& ref = *static_cast<T*>(child.get());
        ref.parent = this;
        children.push_back(std::move(child));
        return ref;
    }

    void add(std::unique_ptr<Widget> child) {
        if (!child) return;
        child->parent = this;
        children.push_back(WidgetPtr(child.release()));
    }

    Widget* find(const std::string& widget_id) {
        if (id == widget_id) return this;
        for (auto& c : children)
            if (Widget* w = c->find(widget_id)) return w;
        return nullptr;
    }

    // --- protocol ---

    virtual Size measure(const Constraints&) { return {0, 0}; }

    virtual void arrange(const Rect& area) { bounds = area; }

    virtual void paint(const PaintEnv&) const {}

    // Paints after (on top of) the subtree, outside this widget's own
    // clip_rect: decorations over children — marks on a wrapped Slider's
    // track, selection outlines, focus rings around content.
    virtual void paint_over(const PaintEnv&) const {}

    // Paints this widget and its visible subtree: paint, children (inside
    // clip_rect), then paint_over.
    void paint_tree(const PaintEnv& env) const {
        if (!visible) return;
        paint(env);
        std::optional<Rect> clip = clip_rect();
        if (clip) env.list.push_clip(*clip);
        for (const auto& c : children) c->paint_tree(env);
        if (clip) env.list.pop_clip();
        paint_over(env);
    }

    // Children get the event first (topmost child first). Return true to
    // consume so ancestors/siblings don't act on it.
    virtual bool on_event(const Event&) { return false; }

    virtual void tick(Time) {}

    virtual bool focusable() const { return false; }

    // Children marked expanding share leftover main-axis space (Spacer).
    virtual bool expands_main() const { return false; }

    // Whether containers may stretch this widget along their cross axis.
    // Fixed-size controls (buttons, switches, ...) opt out and hug.
    virtual bool stretch_cross() const { return true; }

protected:
    // Logical px per framebuffer px (from the context theme).
    float ui_scale() const { return theme_scale_of(ctx); }

    // Optional clip region applied to the subtree while painting children.
    virtual std::optional<Rect> clip_rect() const { return {}; }

    // Called by Context once per widget when it enters a context.
    virtual void attached(Context&) {}

public:
    virtual const char* widget_type() const { return "Widget"; }

    // --- destruction protocol ---
    //
    // The tree destroys widgets through unique_ptr<Widget, Deleter>, which
    // calls release(). The default destroys a C++-owned widget. Bindings
    // override release() for widgets whose storage is owned by the Python
    // runtime (Python-subclass instances live inside a PyObject allocation,
    // where `delete` is undefined behavior) — see
    // docs/python-custom-widgets.md.
    virtual void release() noexcept { delete this; }

    struct Deleter {
        void operator()(Widget* w) const noexcept { w->release(); }
    };
    using WidgetPtr = std::unique_ptr<Widget, Deleter>;

    // --- tooltips ---

    // Text shown in a popup after hovering this widget (Context manages the
    // popup: 0.6 s hover delay, dismissed by moving away or pressing).
    // NOTE: returns Widget& — in fluent chains, call base-class setters last
    // (pre-C++23 has no deducing-this, so Widget& can't know the leaf type).
    Widget& set_tooltip(std::string t) { tooltip_ = std::move(t); return *this; }
    const std::string& tooltip() const { return tooltip_; }

    // --- context menu ---
    //
    // Widget shown when the user right-clicks this widget or anything inside
    // it (the nearest ancestor with one wins). Pass a Menu (widgets/menus.hpp;
    // any popup-suitable widget works). Borrowed, never owned: keep it alive in
    // your shell/app code for as long as this widget lives. Context::handle
    // opens it as a floating popup at the cursor.
    Widget& set_context_menu(Widget* w) { context_menu_ = w; return *this; }
    Widget* context_menu() const { return context_menu_; }

    // --- hover cursor ---
    //
    // Cursor shape requested while the mouse is over this widget (the nearest
    // ancestor with a non-Arrow cursor wins). The host renders it; nothing
    // happens without a host callback.
    Widget& set_cursor(Cursor c) { hover_cursor_ = c; return *this; }
    Cursor cursor() const { return hover_cursor_; }

    // --- texture ownership (Image and friends) ---

    // Widgets that own GPU-side textures (fonts, images) report pending
    // uploads here; Context collects the whole tree every frame.
    virtual void collect_uploads(std::vector<TexUpload>&) {}
    void collect_tree(std::vector<TexUpload>& out) {
        collect_uploads(out);
        for (auto& c : children) c->collect_tree(out);
    }

    // --- state ---

    std::string id;             // optional, for Context::find()
    Rect bounds;                // absolute frame (assigned by arrange)
    Widget* parent = nullptr;
    std::vector<WidgetPtr> children;
    bool visible = true;
    bool enabled = true;
    bool focused = false;       // maintained by Context

protected:
    Context* ctx = nullptr;     // set by Context::arrange_tree (theme/font access)
    std::weak_ptr<const bool> ctx_alive_; // expires when ctx is destroyed
    std::string tooltip_;       // hover-delay tooltip text
    Widget* context_menu_ = nullptr;  // right-click target (borrowed, see above)
    Cursor hover_cursor_ = Cursor::Arrow; // requested while hovered

    friend class Context;

public:

    // The Context this widget is attached to (null while detached). Menus and
    // other popup widgets read it off their anchor when first opened.
    Context* context() const { return ctx; }

    // Invoke an event on the subtree, deepest-first, until consumed.
    bool dispatch(const Event& e) {
        if (!visible) return false;
        for (auto it = children.rbegin(); it != children.rend(); ++it)
            if ((*it)->dispatch(e)) return true;
        return on_event(e);
    }

    void tick_tree(Time t) {
        tick(t);
        for (auto& c : children) c->tick_tree(t);
    }
};

// Common hit-test helper.
inline bool hit(const Widget& w, Vec2 p) {
    return w.visible && w.bounds.contains(p);
}

} // namespace slikui
