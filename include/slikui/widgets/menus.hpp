// slikui/widgets/menus.hpp — Menu, MenuItem, MenuBar, shortcut labels.
//
// Menus are BORROWED popups: build them once (a MenuBar owns its menus; a
// standalone Menu is app-owned, e.g. as a widget's context menu) and opening
// pushes the existing object onto Context's popup stack. Nothing is cloned, so
// checkmark state read through callbacks stays live across opens.
//
// Popup-stack rules (see Context): opening a submenu closes its sibling
// submenus; clicking elsewhere in the parent menu closes the open submenu;
// Escape unwinds one level per press; click outside every popup closes all.
#pragma once

#include "slikui/context.hpp"
#include "slikui/style/anim.hpp"
#include "slikui/widgets/containers.hpp"
#include "slikui/widgets/env.hpp"
#include "slikui/widgets/widget.hpp"

#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace slikui {

// Human-readable shortcut hint for menus and tooltips: "Ctrl+Shift+S"
// ("⌘⇧S" on Apple). Complements Context::add_shortcut, which fires actions.
inline std::string shortcut_label(Key key, unsigned mods) {
    std::string out;
#if defined(__APPLE__)
    if (mods & ModSuper) out += "⌘";
    if (mods & ModCtrl) out += "⌃";
    if (mods & ModAlt) out += "⌥";
    if (mods & ModShift) out += "⇧";
#else
    if (mods & ModCtrl) out += "Ctrl+";
    if (mods & ModSuper) out += "Cmd+";
    if (mods & ModAlt) out += "Alt+";
    if (mods & ModShift) out += "Shift+";
#endif
    int k = int(key);
    if (k >= int(Key::A) && k <= int(Key::Z)) {
        out += char('A' + k - int(Key::A));
        return out;
    }
    switch (key) {
    case Key::Enter: out += "Enter"; break;
    case Key::Escape: out += "Esc"; break;
    case Key::Space: out += "Space"; break;
    case Key::Tab: out += "Tab"; break;
    case Key::Backspace: out += "Bksp"; break;
    case Key::Delete: out += "Del"; break;
    case Key::Left: out += "←"; break;
    case Key::Right: out += "→"; break;
    case Key::Up: out += "↑"; break;
    case Key::Down: out += "↓"; break;
    case Key::Home: out += "Home"; break;
    case Key::End: out += "End"; break;
    case Key::PageUp: out += "PgUp"; break;
    case Key::PageDown: out += "PgDn"; break;
    default:
        if (k >= int(Key::F1) && k <= int(Key::F12))
            out += "F" + std::to_string(k - int(Key::F1) + 1);
        break;
    }
    return out;
}

// ---------------------------------------------------------------------------
// MenuItem — one row of a Menu: label, optional checkmark, right-aligned
// shortcut hint, optional submenu (owned; pushed as its own popup on
// hover/click), disabled state.
// ---------------------------------------------------------------------------
class MenuItem : public Widget {
public:
    MenuItem(std::string label, std::function<void()> action, std::string hint = {},
             bool enabled = true, std::string widget_id = {})
        : Widget(std::move(widget_id)), label_(std::move(label)),
          action_(std::move(action)), hint_(std::move(hint)), enabled_(enabled) {}

    MenuItem& set_enabled(bool on) { enabled_ = on; return *this; }
    bool enabled() const { return enabled_; }
    // Checkmark state is read through a callback at paint time, so it stays
    // live however the app mutates its state.
    MenuItem& set_checked_fn(std::function<bool()> f) {
        checked_fn_ = std::move(f);
        return *this;
    }
    bool checked() const { return checked_fn_ && checked_fn_(); }
    // Non-owning: the parent Menu (or the app, for borrowed submenus) keeps
    // the Menu alive.
    void set_submenu(Menu* sub) { submenu_ = sub; }
    Menu* submenu() { return submenu_; }

    Size measure(const Constraints& c) override {
        refresh();
        float inv = 1.f / ui_scale();
        float w = 10 + 18 + lays_.label.size.w * inv;
        if (!hint_.empty()) w += 16 + lays_.hint.size.w * inv;
        if (submenu_) w += 16;
        w += 10;
        float h = std::fmax(lays_.label.line_height * inv, 16.f) + 9.f;
        return c.clamp({w, h});
    }

    void paint(const PaintEnv& env) const override {
        refresh();
        const Theme& t = env.theme;
        float inv = 1.f / ui_scale();
        if (enabled_ && hover_.value > 0)
            env.list.rect(bounds,
                          t.surface_alt.lerp(t.hover(t.surface_alt), hover_.value), 4);
        float cy = bounds.center().y;
        if (checked()) {
            Color mark = enabled_ ? t.accent : t.text_dim;
            env.list.line({bounds.x + 11, cy}, {bounds.x + 15, cy + 4}, mark, 2);
            env.list.line({bounds.x + 15, cy + 4}, {bounds.x + 22, cy - 5}, mark, 2);
        }
        float tx = bounds.x + 32;
        paint_text(env, lays_.label, {tx, cy - lays_.label.size.h * inv * .5f},
                   enabled_ ? t.text : t.text_dim, inv);
        if (!hint_.empty()) {
            float hx = bounds.right() - 10 - lays_.hint.size.w * inv;
            paint_text(env, lays_.hint, {hx, cy - lays_.hint.size.h * inv * .5f},
                       t.text_dim.faded(enabled_ ? 1.f : .4f), inv);
        }
        if (submenu_) {
            float cx = bounds.right() - 14;
            Color mark = enabled_ ? t.text_dim : t.text_dim.faded(.4f);
            env.list.line({cx - 2, cy - 4}, {cx + 2, cy}, mark, 2);
            env.list.line({cx + 2, cy}, {cx - 2, cy + 4}, mark, 2);
        }
    }

    bool on_event(const Event& e) override {
        switch (e.type) {
        case Event::Type::MouseMove: {
            bool over = bounds.contains(e.pos);
            hover_.target = over && enabled_ ? 1.f : 0.f;
            if (over && enabled_ && submenu_ && !submenu_open())
                open_submenu(); // defined after Menu (needs the complete type)
            return false;
        }
        case Event::Type::MousePress:
            if (!bounds.contains(e.pos)) return false;
            if (!enabled_) return true; // swallow: disabled rows don't dismiss
            if (submenu_) {
                if (submenu_open()) close_submenu();
                else open_submenu();
                return true;
            }
            if (action_) action_();
            if (ctx) ctx->close_popups();
            return true;
        default:
            return false;
        }
    }

    void tick(Time t) override { hover_.advance(t.dt); }

    const char* widget_type() const override { return "MenuItem"; }

    bool stretch_cross() const override { return false; }

private:
    friend class Menu;

    // Defined after Menu (Menu must be complete to convert Menu* to Widget&
    // for the Context popup-stack calls).
    void open_submenu();
    void close_submenu();
    bool submenu_open() const;
    Menu* owner_menu();
    void note_opened_from(Menu* m) { opened_from_ = m; }

    void refresh() const {
        if (valid_ || !ctx) return;
        const Theme& t = ctx->theme();
        lays_.label = ctx->font().layout(label_, t.s(t.base_font_px));
        if (!hint_.empty())
            lays_.hint = ctx->font().layout(hint_, t.s(t.small_font_px));
        valid_ = true;
    }

    struct Lays {
        text::TextLayout label, hint;
    };
    std::string label_, hint_;
    std::function<void()> action_;
    std::function<bool()> checked_fn_;
    Menu* submenu_ = nullptr; // non-owning (Menu pool or app-owned)
    Menu* opened_from_ = nullptr; // set while this item's submenu is open
    bool enabled_ = true;
    Anim hover_{0, 0, 15};
    mutable Lays lays_;
    mutable bool valid_ = false;
};

// ---------------------------------------------------------------------------
// Menu — vertical list of MenuItems inside a bordered panel. Build once, open
// many times: open_for() pushes this object onto the popup stack.
// ---------------------------------------------------------------------------
class Menu : public Panel {
public:
    explicit Menu(std::string title = {}, std::string widget_id = {})
        : Panel(std::move(widget_id)), title_(std::move(title)) {
        set_padding(4).set_gap(1);
    }

    const std::string& title() const { return title_; }
    Menu& set_min_width(float w) { min_width_ = w; return *this; }
    // Fired each time the menu is about to open — refresh enabled flags there
    // (checkmarks read live through MenuItem::set_checked_fn callbacks).
    Menu& on_open(std::function<void()> f) { on_open_ = std::move(f); return *this; }

    Menu& add_item(std::string label, std::function<void()> action,
                   std::string hint = {}, bool enabled = true) {
        add<MenuItem>(std::move(label), std::move(action), std::move(hint), enabled);
        return *this;
    }
    Menu& add_checkable(std::string label, std::function<bool()> is_checked,
                        std::function<void()> toggle, std::string hint = {},
                        bool enabled = true) {
        auto& it = add<MenuItem>(std::move(label), std::move(toggle), std::move(hint),
                                 enabled);
        it.set_checked_fn(std::move(is_checked));
        return *this;
    }
    // The submenu is owned by this Menu (C++ callers) and pushed as its own
    // popup when its item is hovered/clicked (right, or left at the edge).
    Menu& add_submenu(std::string label, std::unique_ptr<Menu> sub,
                      std::string hint = {}, bool enabled = true) {
        Menu& ref = *sub;
        owned_submenus_.push_back(std::move(sub));
        attach_submenu(label, ref, std::move(hint), enabled);
        return ref;
    }
    // Borrowed flavor: the caller keeps the submenu alive (Python bindings
    // use this — keep a reference to the Menu on the Python side).
    Menu& add_submenu(std::string label, Menu& sub, std::string hint = {},
                      bool enabled = true) {
        attach_submenu(label, sub, std::move(hint), enabled);
        return sub;
    }
    Menu& add_separator() {
        add<Divider>(true);
        return *this;
    }

    // Open beside `anchor` (a MenuBar title or a parent-menu item) with menu
    // stack discipline: everything above the anchor's popup closes first.
    // The menu takes its Context from the anchor — standalone menus aren't
    // attached to any tree until first opened.
    void open_for(Widget& anchor, PopupDir dir = PopupDir::Below) {
        Context* c = ctx ? ctx : anchor.context();
        if (!c) return;
        if (on_open_) on_open_();
        c->close_popups_above(anchor);
        c->push_popup(*this, anchor, dir);
    }

    Size measure(const Constraints& c) override {
        Size s = Panel::measure(c);
        s.w = std::fmax(s.w, min_width_);
        return c.clamp(s);
    }

    bool on_event(const Event& e) override {
        if (e.type == Event::Type::MouseMove && ctx && opened_item_ &&
            ctx->popup_open(opened_item_->submenu_) &&
            !opened_item_->bounds.contains(e.pos)) {
            // Cursor moved to a different row: close the open submenu.
            ctx->close_popups_above(*this);
            opened_item_->note_opened_from(nullptr);
            opened_item_ = nullptr;
        }
        if (e.type == Event::Type::MousePress && ctx && opened_item_ &&
            ctx->popup_open(opened_item_->submenu_) &&
            bounds.contains(e.pos)) {
            // Click on this menu's dead area (padding, separator): the open
            // submenu closes; items manage their own clicks.
            ctx->close_popups_above(*this);
            opened_item_->note_opened_from(nullptr);
            opened_item_ = nullptr;
        }
        return false;
    }

    const char* widget_type() const override { return "Menu"; }

private:
    friend class MenuItem;

    void note_submenu_open(MenuItem* item) {
        if (opened_item_ && opened_item_ != item && ctx &&
            ctx->popup_open(opened_item_->submenu_))
            ctx->close_popups_above(*this);
        if (opened_item_) opened_item_->note_opened_from(nullptr);
        opened_item_ = item;
        item->note_opened_from(this);
    }

    std::string title_;
    float min_width_ = 140.f;
    std::function<void()> on_open_;
    std::vector<std::unique_ptr<Menu>> owned_submenus_;
    MenuItem* opened_item_ = nullptr; // item whose submenu is currently open

    void attach_submenu(std::string& label, Menu& sub, std::string hint, bool enabled) {
        auto& it = add<MenuItem>(std::move(label), std::function<void()>{},
                                 std::move(hint), enabled);
        it.set_submenu(&sub);
    }
};

inline void MenuItem::open_submenu() {
    if (!ctx || !submenu_) return;
    if (Menu* m = owner_menu()) {
        m->note_submenu_open(this);
        ctx->close_popups_above(*m);
    }
    ctx->push_popup(*submenu_, *this, PopupDir::Right);
}

inline void MenuItem::close_submenu() {
    if (!ctx || !submenu_) return;
    if (Menu* m = owner_menu()) ctx->close_popups_above(*m);
    else ctx->close_topmost_popup();
}

inline bool MenuItem::submenu_open() const {
    return ctx && ctx->popup_open(submenu_);
}

inline Menu* MenuItem::owner_menu() {
    for (Widget* p = parent; p; p = p->parent)
        if (auto* m = dynamic_cast<Menu*>(p)) return m;
    return nullptr;
}

// ---------------------------------------------------------------------------
// MenuBar — top strip of menu titles. Click a title to open its menu below it;
// with a menu already open, moving over another title switches to it.
// ---------------------------------------------------------------------------
class MenuBar : public Row {
public:
    explicit MenuBar(std::string widget_id = {}) : Row(std::move(widget_id)) {
        set_gap(2);
    }

    // Creates the menu (owned by the bar) and its title button; fill the
    // returned Menu with items.
    Menu& add_menu(std::string title) {
        menus_.push_back(std::make_unique<Menu>(std::move(title)));
        Menu& m = *menus_.back();
        add<MenuButton>(this, &m);
        return m;
    }

    Size measure(const Constraints& c) override {
        Size s = Row::measure(c);
        s.h = std::fmax(s.h, 26.f);
        return c.clamp(s);
    }

    void paint(const PaintEnv& env) const override {
        env.list.rect(bounds, env.theme.surface, 0);
        env.list.line({bounds.x, bounds.bottom() - 1},
                      {bounds.right(), bounds.bottom() - 1}, env.theme.border, 1);
    }

    const char* widget_type() const override { return "MenuBar"; }

    // Implementation detail, public only so Widget::add<MenuButton> can
    // instantiate (nested-private classes are inaccessible in the template).
    class MenuButton : public Widget {
    public:
        MenuButton(MenuBar* bar, Menu* menu) : bar_(bar), menu_(menu) {}

        bool open() const { return ctx && ctx->popup_open(menu_); }

        Size measure(const Constraints& c) override {
            refresh();
            float inv = 1.f / ui_scale();
            return c.clamp({lay_.size.w * inv + 20.f,
                            std::fmax(lay_.line_height * inv + 8.f, 24.f)});
        }

        void paint(const PaintEnv& env) const override {
            refresh();
            const Theme& t = env.theme;
            float inv = 1.f / ui_scale();
            if (open())
                env.list.rect(bounds, t.surface_alt, t.radius);
            else if (hover_.value > 0)
                env.list.rect(bounds, t.surface_alt.lerp(t.hover(t.surface_alt),
                                                         hover_.value * .6f),
                              t.radius);
            paint_text(env, lay_, {bounds.x + 10,
                                   bounds.center().y - lay_.size.h * inv * .5f},
                       t.text, inv);
        }

        bool on_event(const Event& e) override {
            switch (e.type) {
            case Event::Type::MouseMove: {
                bool over = bounds.contains(e.pos);
                hover_.target = over ? 1.f : 0.f;
                // Hover-while-open: only when the topmost popup is a Menu, so
                // an unrelated context menu isn't swapped away.
                if (over && ctx && ctx->has_popups() && !open()) {
                    Widget* top = ctx->popup(ctx->popup_count() - 1);
                    if (dynamic_cast<Menu*>(top)) open_menu();
                }
                return false;
            }
            case Event::Type::MousePress:
                if (bounds.contains(e.pos)) {
                    if (open()) ctx->close_popups();
                    else open_menu();
                    return true;
                }
                return false;
            default:
                return false;
            }
        }

        void tick(Time t) override { hover_.advance(t.dt); }

        const char* widget_type() const override { return "MenuButton"; }

        bool stretch_cross() const override { return false; }

    private:
        void open_menu() { menu_->open_for(*this, PopupDir::Below); }
        void refresh() const {
            if (valid_ || !ctx) return;
            lay_ = ctx->font().layout(menu_->title(),
                                      ctx->theme().s(ctx->theme().base_font_px));
            valid_ = true;
        }

        MenuBar* bar_; // (reserved: per-bar open-state coordination)
        Menu* menu_;
        Anim hover_{0, 0, 15};
        mutable text::TextLayout lay_;
        mutable bool valid_ = false;
    };

    std::vector<std::unique_ptr<Menu>> menus_;
};

} // namespace slikui
