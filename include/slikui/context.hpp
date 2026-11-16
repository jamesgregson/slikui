// slikui/context.hpp - Context: root widget tree + theme + font + sink wiring.
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
#include "slikui/text/text.hpp"
#include "slikui/widgets/env.hpp"
#include "slikui/widgets/widget.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace slikui {

class Menu; // widgets/menus.hpp - borrowed popups, opened by right-click
class Dialog; // widgets/shells.hpp - floating shell layer (see open_dialog)

// Where an anchored popup opens relative to its anchor widget. When the
// preferred side would leave the frame, the popup flips to the opposite side
// and is then clamped fully inside the frame.
enum class PopupDir : std::uint8_t { Below, Above, Right, Left };

// Builds the tooltip popup for `w` at `pos`. Defined after Context (needs the
// complete type); declared here so Context::show_tooltip can call it.
void show_tooltip_for(Context& ctx, Widget& w, Vec2 pos);

class Context {
public:
    explicit Context(IRenderSink& sink) : sink_(&sink) {
        fonts_.body().set_texture_id(next_texture_id());
        fonts_.set_texture_source([this] { return next_texture_id(); });
    }
    // Expire the liveness token first: borrowed widgets (Menu, Dialog) that
    // outlive this Context must not call back into it from their destructor.
    ~Context() {
        alive_.reset();
        clear_tree();
    }
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    // --- configuration ---

    Theme& theme() { return theme_; }
    const Theme& theme() const { return theme_; }

    // The body font's regular face (what load_font_file loads).
    text::Font& font() { return fonts_.body(); }

    bool load_font_file(const std::string& path) { return fonts_.body().load_file(path); }
    bool load_font_memory(const std::uint8_t* data, size_t len) {
        return fonts_.body().load_memory(data, len);
    }

    // --- font families ---
    //
    // Fonts are registered by family name, one face per style. Three names
    // are roles every text call understands: "body" (the default - the font
    // load_font_file/load_font_memory load), "heading" and "mono". Asking
    // for an unregistered family falls back to "body"; asking for a style
    // the family lacks synthesizes it from the closest face.
    //
    //   ctx.add_font("mono", "/path/JetBrainsMono-Regular.ttf");
    //   ctx.add_font("body", "/path/Inter-Bold.ttf", text::FontStyle::Bold);
    //
    // `face` selects a face inside a .ttc collection; -1 picks the face whose
    // style bits (head.macStyle) match `style`. False when the file can't be
    // loaded; the family then keeps using its other faces.
    bool add_font(std::string_view family, const std::string& path,
                  text::FontStyle style = text::FontStyle::Regular, int face = 0) {
        return fonts_.add_font(family, path, style, face);
    }
    bool add_font_memory(std::string_view family, const std::uint8_t* data, size_t len,
                         text::FontStyle style = text::FontStyle::Regular, int face = 0) {
        return fonts_.add_font_memory(family, data, len, style, face);
    }
    bool has_font(std::string_view family) const { return fonts_.has_font(family); }

    text::FontLibrary& fonts() { return fonts_; }

    // --- text (logical px; see slikui/text/text.hpp) ---

    // Size of `s` drawn with `o`: widest line advance x block height. Wraps
    // when o.wrap and o.max_width > 0. Use it from measure().
    Size text_size(std::string_view s, const TextOpts& o = {}) {
        auto rq = fonts_.request(o, theme_);
        if (!rq.font->valid()) return {};
        float avail = o.max_width > 0 ? theme_.s(o.max_width) : 0.f;
        const text::TextLayout& lay =
            fonts_.layout(s, rq, o.wrap ? avail : 0.f,
                          o.overflow == TextOverflow::Ellipsis && !o.wrap ? avail : 0.f);
        float inv = 1.f / theme_.scale;
        return {text::layout_advance(lay) * inv, lay.size.h * inv};
    }

    TextMetrics text_metrics(const TextOpts& o = {}) {
        auto rq = fonts_.request(o, theme_);
        float inv = 1.f / theme_.scale;
        return {rq.font->ascent(rq.px) * inv, rq.font->descent(rq.px) * inv,
                rq.font->line_height(rq.px) * inv};
    }

    // A block to keep: draw with env.draw(block, pos), hit-test, caret and
    // selection queries. Wraps to `wrap_width` when > 0 (lines align inside
    // it per o.align); o.wrap / o.max_width are ignored here, except that
    // TextOverflow::Ellipsis truncates to o.max_width.
    TextBlock text_layout(std::string_view s, float wrap_width = 0, const TextOpts& o = {}) {
        auto rq = fonts_.request(o, theme_);
        float inv = 1.f / theme_.scale;
        Color col = o.color.a > 0 ? o.color : theme_.text;
        if (!rq.font->valid()) return TextBlock::make({}, std::string(s), inv, 0, o.align, col);
        float wrap = wrap_width > 0 ? theme_.s(wrap_width) : 0.f;
        float ell = o.overflow == TextOverflow::Ellipsis && wrap <= 0 && o.max_width > 0
                        ? theme_.s(o.max_width) : 0.f;
        text::TextLayout lay = text::layout_fitted(*rq.font, s, rq.px, wrap, ell, rq.flags);
        return TextBlock::make(std::move(lay), std::string(s), inv, wrap, o.align, col);
    }

    void set_frame_size(Vec2 size) { frame_ = size; }
    Vec2 frame_size() const { return frame_; }

    // --- cursor shapes (host-provided) ---
    //
    // The host turns Cursor values into real system cursors (GLFW does this
    // via attach); without a callback, widget cursor requests are no-ops.
    Context& set_cursor_host(std::function<void(Cursor)> f) {
        cursor_host_ = std::move(f);
        return *this;
    }

    // --- clipboard (host-provided) ---
    //
    // The window system owns the real clipboard; slikui reaches it through two
    // host callbacks (the GLFW wrapper installs glfwGetClipboardString /
    // glfwSetClipboardString via GlfwWindow::attach). Headless hosts (tests)
    // may inject fakes; with no callbacks set, clipboard operations are
    // silent no-ops.
    Context& set_clipboard_source(std::function<std::string()> f) {
        clipboard_source_ = std::move(f);
        return *this;
    }
    Context& set_clipboard_sink(std::function<void(std::string)> f) {
        clipboard_sink_ = std::move(f);
        return *this;
    }
    std::string clipboard_text() const {
        return clipboard_source_ ? clipboard_source_() : std::string{};
    }
    void set_clipboard_text(std::string text) {
        if (clipboard_sink_) clipboard_sink_(std::move(text));
    }

    // --- tree ---

    template <class T, class... Args>
    T& set_root(Args&&... args) {
        root_.reset(new T(std::forward<Args>(args)...));
        return *static_cast<T*>(root_.get());
    }
    void set_root(std::unique_ptr<Widget> w) { root_.reset(w.release()); }
    Widget* root() { return root_.get(); }

    Widget* find(const std::string& id) { return root_ ? root_->find(id) : nullptr; }

    // Destroys the widget tree, popups and deferred closures, releasing the
    // Python callbacks they hold. The Python bindings route App.close() /
    // App.__del__ through this: a callback capturing a widget wrapper forms a
    // cycle closed by C++ ownership, which the GC cannot traverse - destroying
    // the tree while the interpreter is alive is what makes shutdown
    // leak-free (see python/tests/test_shutdown_leaks.py). Not safe to call
    // from inside a widget callback: destruction is immediate, not deferred.
    void clear_tree() {
        // Teardown is silent: no FocusLost/MouseLeave callbacks into widgets
        // (or Python) that are about to be destroyed.
        if (focused_) focused_->focused = false; // flag while still alive
        focused_ = nullptr;
        focus_notes_.clear();
        mouse_capture_ = nullptr;
        hover_ = nullptr;
        reset_press_state();
        tooltip_open_ = false;
        hover_since_ = -1;
        inside_press_ = false;
        popups_.clear();  // popup widgets may hold callbacks
        dialogs_.clear(); // dialog entries (owned dialogs move to closing_)
        toasts_.clear();
        root_.reset();    // the tree itself
        closing_.clear(); // widgets deferred by close_popups()
        shortcuts_.clear(); // may hold Python callables
    }

    // --- input ---

    // NOTE: popup destruction is deferred to the end of handle() - callbacks
    // may close the popup they're executing in.
    void handle(const Event& e) {
        {
            Busy busy(*this); // focus notifications flush when this closes
            handle_event(e);
        }
        closing_.clear();
    }

    Widget* focused() { return focused_; }
    Widget* hovered() { return hover_; }

    // Moves keyboard focus. The widget losing focus receives FocusLost and
    // the one gaining it FocusGained (no bubbling). Notifications are queued
    // and delivered once the Context is between operations, so a handler
    // may itself change focus or close popups safely.
    void set_focus(Widget* w) {
        if (focused_ == w) return;
        if (focused_) {
            focused_->focused = false;
            focus_notes_.push_back({focused_, Event::Type::FocusLost});
        }
        focused_ = w;
        if (focused_) {
            focused_->focused = true;
            focus_notes_.push_back({focused_, Event::Type::FocusGained});
        }
        if (busy_ == 0) flush_focus_notes();
    }

    // --- frame ---

    void tick(Time t) {
        Busy busy(*this);
        now_ = t.now;
        if (!root_) return;
        attach_tree(*root_);
        root_->measure(Constraints::tight({frame_.x, frame_.y}));
        root_->arrange({0, 0, frame_.x, frame_.y});
        root_->tick_tree(t);
        for (auto& p : popups_) {
            layout_popup(p);
            p.widget->tick_tree(t); // menus animate hover, toasts fade
        }
        for (auto& d : dialogs_) {
            layout_dialog(d);
            d.widget->tick_tree(t);
        }
        // Expired toasts leave the popup stack.
        for (size_t i = toasts_.size(); i-- > 0;) {
            if (t.now >= toasts_[i].expires_at) {
                close_popup_widget(toasts_[i].widget);
                toasts_.erase(toasts_.begin() + long(i));
            }
        }
        update_tooltip(t);
    }

    void render(bool clear = true) {
        if (!sink_) return;
        DrawList list;
        const PaintEnv env{list, theme_, fonts_.body(), &fonts_};
        if (clear) list.rect({0, 0, frame_.x, frame_.y}, theme_.background);
        if (root_) root_->paint_tree(env);
        for (auto& p : popups_) p.widget->paint_tree(env);
        if (Widget* modal = topmost_modal()) // dim everything under dialogs
            list.rect({0, 0, frame_.x, frame_.y}, Color::black().faded(.45f));
        for (auto& d : dialogs_) d.widget->paint_tree(env);
        // After painting: widgets that lay out text in paint() (Slider's
        // value) rasterize new glyphs into the atlas there, and the sink
        // must see them this frame, not the next.
        upload_dirty_textures();
        sink_->render(list, frame_);
        fonts_.end_frame(); // drop env.text() layouts nobody drew for a while
    }

    // --- popups ---
    //
    // Popups float above the tree and are dismissed by clicking outside,
    // Escape (topmost only), or FocusLost. They form a stack - a submenu is
    // pushed above its parent menu - and clicks are resolved against the
    // topmost popup containing the cursor: everything above it closes, the
    // popup itself stays (unless the click lands outside every popup, which
    // closes the whole stack).
    //
    // Two ownership flavors: owned popups (tooltips, Dropdown) are destroyed
    // by Context on close; borrowed popups (Menu) stay owned by the caller and
    // must outlive their time on the stack.
    Widget& push_popup(std::unique_ptr<Widget> w, Vec2 pos) {
        Widget::WidgetPtr owned(w.release());
        Widget& ref = *owned;
        popups_.push_back(Popup{std::move(owned), &ref, nullptr, pos, PopupDir::Below});
        attach_tree(ref);
        layout_popup(popups_.back());
        return ref;
    }
    Widget& push_popup(Widget& w, Vec2 pos) {
        popups_.push_back(Popup{nullptr, &w, nullptr, pos, PopupDir::Below});
        attach_tree(w);
        layout_popup(popups_.back());
        return w;
    }
    // Anchored variants: open beside `anchor` toward `dir` (flipping to the
    // opposite side and clamping into the frame when needed).
    Widget& push_popup(std::unique_ptr<Widget> w, Widget& anchor, PopupDir dir) {
        Widget::WidgetPtr owned(w.release());
        Widget& ref = *owned;
        popups_.push_back(Popup{std::move(owned), &ref, &anchor, {}, dir});
        attach_tree(ref);
        layout_popup(popups_.back());
        return ref;
    }
    Widget& push_popup(Widget& w, Widget& anchor, PopupDir dir) {
        popups_.push_back(Popup{nullptr, &w, &anchor, {}, dir});
        attach_tree(w);
        layout_popup(popups_.back());
        return w;
    }
    // Open `w` (typically a Menu) as a floating popup at a screen position.
    // The right-click handler calls this for widgets with a context menu;
    // apps may call it directly for custom triggers.
    template <class W>
    Widget& open_context_menu(W& w, Vec2 pos) {
        return push_popup(w, pos);
    }

    void close_popups() {
        Busy busy(*this);
        for (auto& p : popups_) {
            Widget* root = p.widget;
            if (focused_ && inside(focused_, root)) set_focus(nullptr);
            if (p.owned) closing_.push_back(std::move(p.owned));
        }
        popups_.clear();
    }
    bool has_popups() const { return !popups_.empty(); }
    size_t popup_count() const { return popups_.size(); }
    Widget* popup(size_t i) { return popups_[i].widget; }
    // Index of the popup whose widget is exactly `w`, or -1 (menus use this to
    // detect "is my submenu already open" and toggle it).
    long popup_index(const Widget* w) const {
        for (size_t i = 0; i < popups_.size(); ++i)
            if (popups_[i].widget == w) return long(i);
        return -1;
    }
    bool popup_open(const Widget* w) const { return popup_index(w) >= 0; }
    // Close every popup above the one containing `anchor` (menus: opening a
    // submenu closes its siblings; an anchor outside all popups closes all).
    // Also used as "close the topmost popup" via close_topmost_popup (Escape).
    void close_popups_above(Widget& anchor) {
        long k = -1;
        for (long i = long(popups_.size()) - 1; i >= 0; --i)
            if (inside(&anchor, popups_[size_t(i)].widget)) {
                k = i;
                break;
            }
        close_popups_above_index(k);
    }
    void close_topmost_popup() {
        if (popups_.empty()) return;
        close_popups_above_index(long(popups_.size()) - 2);
    }

    // --- dialogs ---
    //
    // Floating shell windows ABOVE the popup layer (Dialog, MessageBox,
    // FileBrowser - widgets/dialogs.hpp). Borrowed entries are app-owned and
    // must outlive their stay; owned entries are destroyed on close. A modal
    // dialog dims the app and traps presses outside it.
    template <class D>
    Widget& open_dialog(std::unique_ptr<D> d) {
        D& ref = *d; // D is complete at the call site
        Widget::WidgetPtr owned(d.release());
        dialogs_.push_back(DialogEntry{std::move(owned), &ref, ref.modal()});
        attach_tree(ref);
        layout_dialog(dialogs_.back());
        return ref;
    }
    // Borrowed flavor: the app keeps the dialog alive (like Menu).
    template <class D>
    Widget& open_dialog(D& d) {
        dialogs_.push_back(DialogEntry{nullptr, &d, d.modal()});
        attach_tree(d);
        layout_dialog(dialogs_.back());
        return d;
    }
    void close_dialog(Widget& d) {
        Busy busy(*this);
        for (size_t i = dialogs_.size(); i-- > 0;) {
            if (dialogs_[i].widget == &d) {
                if (focused_ && inside(focused_, dialogs_[i].widget))
                    set_focus(nullptr);
                if (dialogs_[i].owned)
                    closing_.push_back(std::move(dialogs_[i].owned));
                dialogs_.erase(dialogs_.begin() + long(i));
            }
        }
    }
    size_t dialog_count() const { return dialogs_.size(); }
    Widget* dialog(size_t i) { return dialogs_[i].widget; }

    // --- toasts ---
    //
    // Transient corner notices on the popup layer: pushed owned, positioned
    // in the bottom-right corner, removed automatically after `seconds`
    // (measured from the last tick's time).
    void push_toast(std::unique_ptr<Widget> w, double seconds) {
        Widget* raw = w.get();
        push_popup(std::move(w), {frame_.x - 12, frame_.y - 12});
        toasts_.push_back(ToastEntry{raw, now_ + seconds});
    }

    // --- shortcuts ---
    //
    // Global accelerators, checked on KeyPress before widget dispatch. A
    // shortcut requires at least one modifier so plain typing never fires one;
    // matching is exact (Ctrl+S != Ctrl+Shift+S).
    Context& add_shortcut(Key key, unsigned mods, std::function<void()> action) {
        shortcuts_.push_back(Shortcut{key, mods, std::move(action)});
        return *this;
    }
    void clear_shortcuts() { shortcuts_.clear(); }

    // --- textures ---

    std::uint32_t next_texture_id() { return next_tex_id_++; }

    // A texture id will never be drawn again (its owning widget died):
    // forwards to IRenderSink::release so the sink can free it.
    void release_texture(std::uint32_t id) {
        if (sink_ && id) sink_->release(id);
    }

    // The sink lost every texture it held (GPU context lost or recreated,
    // sink reset): re-upload the font atlas and every live widget texture
    // on the next render().
    void invalidate_textures() {
        fonts_.for_each_font([](text::Font& f) { f.mark_dirty(); });
        if (root_) root_->invalidate_tree();
        for (auto& p : popups_) p.widget->invalidate_tree();
        for (auto& d : dialogs_) d.widget->invalidate_tree();
    }

    // --- host routing ---
    //
    // For hosts that share input between the UI and their own scene (a 3D
    // viewport behind a panel): send a pointer event to the UI when
    // wants_mouse(pos), else to the scene; keys when wants_keyboard().

    // True when the UI should get a pointer event at `pos` (logical px): a
    // drag or press is in progress in the UI (so its release must come back
    // here), a modal dialog is open, or a popup, dialog or any widget that
    // is not mouse_transparent() lies under `pos`. Pure layout (Column, Row,
    // Spacer) lets the pointer through to the scene.
    bool wants_mouse(Vec2 pos) {
        if (mouse_capture_ || topmost_modal()) return true;
        for (Widget* t : press_target_)
            if (t) return true;
        for (auto& d : dialogs_)
            if (hit_test_in(*d.widget, pos)) return true;
        for (auto& p : popups_)
            if (hit_test_in(*p.widget, pos)) return true;
        if (!root_) return false;
        for (Widget* w = hit_test_in(*root_, pos); w; w = w->parent)
            if (!w->mouse_transparent()) return true;
        return false;
    }

    // True when the UI should get key events: a widget has keyboard focus
    // (text field, slider, ...) or a modal dialog is open.
    bool wants_keyboard() const { return focused_ != nullptr || topmost_modal() != nullptr; }

    // --- mouse capture ---

    // While captured, all mouse move/press/release events go to `w`, even
    // outside its bounds (dragging a slider, selecting text, ...).
    void capture_mouse(Widget* w) { mouse_capture_ = w; }
    void release_mouse(Widget* w) {
        if (mouse_capture_ == w) mouse_capture_ = nullptr;
    }

private:
    struct Shortcut {
        Key key;
        unsigned mods;
        std::function<void()> action;
    };

    struct DialogEntry {
        Widget::WidgetPtr owned; // null for borrowed dialogs
        Widget* widget = nullptr;
        bool modal = false;
        bool placed = false; // false until the first layout centers it
    };
    struct ToastEntry {
        Widget* widget = nullptr;
        double expires_at = 0;
    };

    struct Popup {
        Widget::WidgetPtr owned;  // null for borrowed popups
        Widget* widget = nullptr;
        Widget* anchor = nullptr; // position beside this widget (null = at pos)
        Vec2 pos;
        PopupDir dir = PopupDir::Below;
    };

    static bool inside(Widget* w, Widget* root) {
        for (Widget* chain = w; chain; chain = chain->parent)
            if (chain == root) return true;
        return false;
    }

    // True when `pos` is inside `modal`'s subtree bounds (dialogs are roots).
    bool inside_hit_any(Widget& modal, Vec2 pos) { return hit_test_in(modal, pos); }

    // Resolve the hover cursor from the hit chain and notify the host on
    // change (no-ops without a host callback).
    void update_cursor(Widget* hit) {
        Cursor cur = Cursor::Arrow;
        for (Widget* chain = hit; chain; chain = chain->parent)
            if (chain->cursor() != Cursor::Arrow) {
                cur = chain->cursor();
                break;
            }
        if (cur != cursor_current_) {
            cursor_current_ = cur;
            if (cursor_host_) cursor_host_(cur);
        }
    }

    void handle_event(const Event& e);
    void handle_press(const Event& e);
    void handle_release(const Event& e);

    // Marks "inside an operation": focus notifications queue until the
    // outermost Busy closes, then flush. Keeps widget callbacks from running
    // while a Context loop (popups, dialogs, tick) is mid-iteration.
    struct Busy {
        Context& c;
        explicit Busy(Context& ctx) : c(ctx) { ++c.busy_; }
        ~Busy() {
            if (--c.busy_ == 0) c.flush_focus_notes();
        }
        Busy(const Busy&) = delete;
        Busy& operator=(const Busy&) = delete;
    };

    void flush_focus_notes() {
        ++busy_; // focus changes made by handlers queue onto this same pass
        for (size_t i = 0; i < focus_notes_.size(); ++i) {
            FocusNote n = focus_notes_[i];
            if (!n.widget) continue; // forgotten (destroyed) meanwhile
            Event e{};
            e.type = n.type;
            n.widget->on_event(e);
        }
        focus_notes_.clear();
        --busy_;
    }

    void reset_press_state() {
        for (int b = 0; b < kButtons; ++b) {
            press_target_[b] = nullptr;
            swallow_release_[b] = false;
        }
        inside_press_ = false;
        std::fill(key_down_.begin(), key_down_.end(), false);
    }

    // A widget is being destroyed: drop every raw pointer to it. Touches only
    // pointer fields (never containers being cleared) - it can run from
    // inside popups_.clear(), closing_.clear() or a dialog erase.
    void forget(const Widget* w) noexcept {
        if (focused_ == w) focused_ = nullptr;
        if (mouse_capture_ == w) mouse_capture_ = nullptr;
        if (hover_ == w) hover_ = nullptr;
        for (auto& t : press_target_)
            if (t == w) t = nullptr;
        for (auto& n : focus_notes_)
            if (n.widget == w) n.widget = nullptr;
    }
    friend void context_forget(Context* ctx, const Widget* w) noexcept;

    static size_t key_index(Key k) { return size_t(k) & 0xFF; }

    // Topmost widget under `pos`, ignoring mouse capture.
    Widget* hit_test_uncaptured(Vec2 pos) {
        Widget* saved = mouse_capture_;
        mouse_capture_ = nullptr;
        Widget* w = hit_test(pos);
        mouse_capture_ = saved;
        return w;
    }

    void layout_popup(Popup& p) {
        Size s = p.widget->measure(Constraints::loose(frame_));
        s.w = std::fmin(s.w, std::fmax(frame_.x, 40.f));
        s.h = std::fmin(s.h, std::fmax(frame_.y, 40.f));
        Vec2 pos = p.pos;
        if (p.anchor && p.anchor->ctx == this) {
            const Rect& a = p.anchor->bounds;
            switch (p.dir) {
            case PopupDir::Below: pos = {a.x, a.bottom() + 2}; break;
            case PopupDir::Above: pos = {a.x, a.y - s.h - 2}; break;
            case PopupDir::Right: pos = {a.right() + 2, a.y}; break;
            case PopupDir::Left:  pos = {a.x - s.w - 2, a.y}; break;
            }
            // Flip to the opposite side when the preferred one overflows.
            switch (p.dir) {
            case PopupDir::Below:
                if (pos.y + s.h > frame_.y && a.y - s.h - 2 >= 0)
                    pos.y = a.y - s.h - 2;
                break;
            case PopupDir::Above:
                if (pos.y < 0 && a.bottom() + 2 + s.h <= frame_.y)
                    pos.y = a.bottom() + 2;
                break;
            case PopupDir::Right:
                if (pos.x + s.w > frame_.x && a.x - s.w - 2 >= 0)
                    pos.x = a.x - s.w - 2;
                break;
            case PopupDir::Left:
                if (pos.x < 0 && a.right() + 2 + s.w <= frame_.x)
                    pos.x = a.right() + 2;
                break;
            }
        }
        // Clamp fully inside the frame (a free popup near an edge slides in
        // instead of being clipped; a popup taller than the frame keeps its
        // clamped size and sits at the top-left corner).
        pos.x = std::clamp(pos.x, 0.f, std::fmax(frame_.x - s.w, 0.f));
        pos.y = std::clamp(pos.y, 0.f, std::fmax(frame_.y - s.h, 0.f));
        p.widget->arrange({pos.x, pos.y, s.w, s.h});
    }

    // Topmost popup whose bounds contain `pos`, or -1.
    long topmost_popup_containing(Vec2 pos) const {
        for (long i = long(popups_.size()) - 1; i >= 0; --i)
            if (popups_[size_t(i)].widget->bounds.contains(pos)) return i;
        return -1;
    }
    void close_popups_above_index(long k) {
        Busy busy(*this);
        for (long i = long(popups_.size()) - 1; i > k; --i) {
            Popup& p = popups_[size_t(i)];
            if (focused_ && inside(focused_, p.widget)) set_focus(nullptr);
            if (p.owned) closing_.push_back(std::move(p.owned));
        }
        if (k + 1 < long(popups_.size())) popups_.resize(size_t(k + 1));
    }

    // Dispatch an event up the chain from `w` to the root, until consumed.
    void dispatch(Widget& w, const Event& e) {
        for (Widget* chain = &w; chain; chain = chain->parent)
            if (chain->on_event(e)) return;
    }

    void attach_tree(Widget& w) {
        // expired(): a new Context reusing a destroyed one's address must
        // still re-attach (and hand out its own liveness token).
        if (w.ctx != this || w.ctx_alive_.expired()) {
            w.ctx = this;
            w.ctx_alive_ = alive_;
            w.attached(*this);
        }
        for (auto& c : w.children) attach_tree(*c);
        // NOTE: popups are attached individually in push_popup - never here,
        // or a popup re-attaching its own subtree recurses forever.
    }

    void upload_dirty_textures() {
        fonts_.for_each_font([this](text::Font& f) {
            if (!f.valid()) return;
            for (auto& page : f.atlas().pages()) {
                if (!page->dirty) continue;
                sink_->upload(TexUpload{page->tex_id, text::kAtlasPage, text::kAtlasPage,
                                        TexFormat::Alpha8, page->pixels.data()});
                page->dirty = false;
            }
        });
        // Widgets that own textures (Image, ...) report pending uploads.
        uploads_.clear();
        if (root_) root_->collect_tree(uploads_);
        for (auto& p : popups_) p.widget->collect_tree(uploads_);
        for (auto& d : dialogs_) d.widget->collect_tree(uploads_);
        for (const auto& up : uploads_) sink_->upload(up);
    }

    // --- tooltips ---

    // Hover change: MouseLeave to every widget on the old hover chain that is
    // not on the new one (deepest first), then MouseEnter down the new chain
    // (outermost first). Moving from a container onto its own child is not a
    // leave. Neither event bubbles.
    void set_hover(Widget* w, Vec2 pos) {
        hover_pos_ = pos;
        if (w == hover_) return;
        Widget* old = hover_;
        hover_ = w;
        hover_since_ = -1;
        if (tooltip_open_) {
            close_popups();
            tooltip_open_ = false;
        }
        Event ev{};
        ev.pos = pos;
        ev.type = Event::Type::MouseLeave;
        for (Widget* c = old; c && !inside(w, c); c = c->parent) {
            c->on_event(ev);
            if (hover_ != w) return; // a handler moved hover (re-entrant move)
        }
        std::vector<Widget*> enter;
        for (Widget* c = w; c && !inside(old, c); c = c->parent) enter.push_back(c);
        ev.type = Event::Type::MouseEnter;
        for (auto it = enter.rbegin(); it != enter.rend(); ++it) {
            (*it)->on_event(ev);
            if (hover_ != w) return;
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
        for (auto it = dialogs_.rbegin(); it != dialogs_.rend(); ++it)
            if (Widget* w = hit_test_in(*it->widget, pos)) return w;
        for (auto it = popups_.rbegin(); it != popups_.rend(); ++it) {
            // Popups float above the root tree; the topmost one wins so a
            // submenu overlapping its parent menu gets the click.
            if (Widget* w = hit_test_in(*it->widget, pos)) return w;
        }
        return root_ ? hit_test_in(*root_, pos) : nullptr;
    }

    void layout_dialog(DialogEntry& d) {
        Size s = d.widget->measure(Constraints::loose(frame_));
        s.w = std::fmin(s.w, std::fmax(frame_.x - 16.f, 60.f));
        s.h = std::fmin(s.h, std::fmax(frame_.y - 16.f, 40.f));
        // First layout centers; afterwards the widget's bounds carry the
        // position (dragging moves bounds; Dialog::arrange re-pins + clamps).
        Vec2 pos = d.placed
                       ? Vec2{d.widget->bounds.x, d.widget->bounds.y}
                       : Vec2{(frame_.x - s.w) * .5f, (frame_.y - s.h) * .5f};
        d.widget->arrange({pos.x, pos.y, s.w, s.h});
        d.placed = true;
    }

    Widget* topmost_modal() const {
        for (auto it = dialogs_.rbegin(); it != dialogs_.rend(); ++it)
            if (it->modal) return it->widget;
        return nullptr;
    }
    // Remove one popup (used by toast expiry); moves owned storage to closing_.
    void close_popup_widget(Widget* w) {
        Busy busy(*this);
        for (size_t i = 0; i < popups_.size(); ++i) {
            if (popups_[i].widget == w) {
                if (focused_ && inside(focused_, w)) set_focus(nullptr);
                if (popups_[i].owned) closing_.push_back(std::move(popups_[i].owned));
                popups_.erase(popups_.begin() + long(i));
                return;
            }
        }
    }

    Widget* hit_test_in(Widget& w, Vec2 pos) {
        if (!w.visible) return nullptr;
        // Children are only pickable where they are painted: a clipping
        // widget (ScrollArea) hides content scrolled out of view, so that
        // content must not steal clicks from widgets outside the clip.
        std::optional<Rect> clip = w.clip_rect();
        if (!clip || clip->contains(pos)) {
            for (auto it = w.children.rbegin(); it != w.children.rend(); ++it) {
                std::optional<Rect> child_clip = w.child_clip_rect(**it);
                if (child_clip && !child_clip->contains(pos)) continue;
                if (Widget* hit_child = hit_test_in(**it, pos)) return hit_child;
            }
        }
        return w.bounds.contains(pos) ? &w : nullptr;
    }

    Widget* nearest_focusable(Widget* w) {
        while (w && !w->focusable()) w = w->parent;
        return w;
    }

    void cycle_focus(bool backwards) {
        std::vector<Widget*> focusables;
        collect_focusables(root_.get(), focusables);
        for (auto& p : popups_) collect_focusables(p.widget, focusables);
        if (focusables.empty()) return;
        const size_t n = focusables.size();
        size_t cur = n; // n = nothing focused (or focus outside the cycle)
        for (size_t i = 0; i < n; ++i)
            if (focusables[i] == focused_) cur = i;
        size_t next;
        if (backwards) next = cur == n ? n - 1 : (cur + n - 1) % n;
        else next = cur == n ? 0 : (cur + 1) % n;
        set_focus(focusables[next]);
        reveal(focusables[next]);
    }

    // Scroll every scrolling ancestor so `w` is in view (inner first; each
    // ancestor sees the frame shifted by the scrolls already applied).
    void reveal(Widget* w) {
        Rect r = w->bounds;
        for (Widget* p = w->parent; p; p = p->parent) r.y -= p->scroll_into_view(r);
    }

    void collect_focusables(Widget* w, std::vector<Widget*>& out) {
        if (!w || !w->visible) return;
        if (w->focusable() && w->enabled) out.push_back(w);
        for (auto& c : w->children) collect_focusables(c.get(), out);
    }

    IRenderSink* sink_;
    Theme theme_;
    text::FontLibrary fonts_;
    Widget::WidgetPtr root_;
    Widget* focused_ = nullptr;
    Vec2 frame_{};
    std::uint32_t next_tex_id_ = 1;
    std::vector<Popup> popups_;
    std::vector<Widget::WidgetPtr> closing_; // deferred destruction
    bool inside_press_ = false; // mouse button held from a press inside the tree
    Widget* mouse_capture_ = nullptr;

    // Liveness token handed to attached widgets (see Widget::ctx_alive_).
    std::shared_ptr<const bool> alive_ = std::make_shared<const bool>(true);

    // Focus notifications waiting for a safe point (see set_focus / Busy).
    struct FocusNote {
        Widget* widget;
        Event::Type type;
    };
    std::vector<FocusNote> focus_notes_;
    int busy_ = 0;

    // Click synthesis, per mouse button (left/middle/right).
    static constexpr int kButtons = 3;
    Widget* press_target_[kButtons] = {};   // widget the press dispatched to
    bool swallow_release_[kButtons] = {};   // press opened a context menu
    int click_count_ = 0;                   // consecutive presses (Event::clicks)
    int last_press_button_ = -1;
    double last_press_time_ = -1e9;
    Vec2 last_press_pos_{};

    // Keys whose KeyPress reached a widget: only those get a KeyRelease.
    std::vector<bool> key_down_ = std::vector<bool>(256, false);

    // clipboard host callbacks (may be empty)
    std::function<std::string()> clipboard_source_;
    std::function<void(std::string)> clipboard_sink_;

    // global accelerators (see add_shortcut)
    std::vector<Shortcut> shortcuts_;

    // floating shell layer above popups + toast expiry tracking
    std::vector<DialogEntry> dialogs_;
    std::vector<ToastEntry> toasts_;

    // cursor shape host callback + last-requested shape
    std::function<void(Cursor)> cursor_host_;
    Cursor cursor_current_ = Cursor::Arrow;

    // last tick's time (toast expiry reference)
    double now_ = 0;

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

// Called from ~Widget (Context is complete here).
inline void context_forget(Context* ctx, const Widget* w) noexcept {
    if (ctx) ctx->forget(w);
}

// noexcept: runs from widget destructors. A sink's release() must not throw
// (the Python trampoline reports Python exceptions as unraisable).
inline void context_release_texture(Context* ctx, std::uint32_t id) noexcept {
    if (ctx) ctx->release_texture(id);
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
    case T::FocusLost: {
        // The app lost focus: releases may never arrive. Tell the captured
        // widget (if it is not also the focused one - set_focus covers that)
        // so drags can cancel, then drop all transient input state.
        if (mouse_capture_ && mouse_capture_ != focused_)
            focus_notes_.push_back({mouse_capture_, T::FocusLost});
        mouse_capture_ = nullptr;
        reset_press_state();
        set_focus(nullptr);
        close_popups();
        return;
    }
    case T::MouseMove: {
        Widget* hit = hit_test(e.pos);
        set_hover(hit, e.pos); // Leave/Enter before the move itself
        if (hit) dispatch(*hit, e);
        update_cursor(hit);
        return;
    }
    case T::MousePress:
        handle_press(e);
        return;
    case T::MouseRelease:
        handle_release(e);
        return;
    case T::Scroll:
        if (Widget* hit = hit_test(e.pos)) dispatch(*hit, e);
        return;
    case T::TextInput:
        if (focused_) dispatch(*focused_, e); // text goes to the focused field
        return;
    case T::KeyPress: {
        if (e.key == Key::Escape) {
            if (!dialogs_.empty()) {
                close_dialog(*dialogs_.back().widget); // dialogs unwind first
                return;
            }
            if (has_popups()) {
                close_topmost_popup(); // submenus unwind one level per Esc
                return;
            }
            set_focus(nullptr);
            return;
        }
        if (e.key == Key::Tab) {
            cycle_focus(e.shift());
            return;
        }
        if (e.mods != 0 && !shortcuts_.empty()) {
            for (auto& s : shortcuts_) {
                if (s.key == e.key && s.mods == e.mods && s.action) {
                    s.action();
                    return;
                }
            }
        }
        // Focused widget first; with nothing focused, the widget under the
        // pointer (key events carry no position of their own).
        Widget* target = focused_ ? focused_ : hover_;
        if (target) {
            key_down_[key_index(e.key)] = true;
            dispatch(*target, e);
        }
        return;
    }
    case T::KeyRelease: {
        // Only for presses a widget received: no orphan releases for Tab,
        // Escape or shortcut chords.
        size_t k = key_index(e.key);
        if (!key_down_[k]) return;
        key_down_[k] = false;
        Widget* target = focused_ ? focused_ : hover_;
        if (target) dispatch(*target, e);
        return;
    }
    // Synthesized by the Context itself; hosts never send these.
    case T::Click:
    case T::DoubleClick:
    case T::MouseEnter:
    case T::MouseLeave:
    case T::FocusGained:
        return;
    }
}

inline void Context::handle_press(const Event& in) {
    Event e = in;
    const int b = e.button;
    const bool tracked = b >= 0 && b < kButtons;

    // Consecutive-click counting (Event::clicks), on the tick clock.
    Vec2 d{e.pos.x - last_press_pos_.x, e.pos.y - last_press_pos_.y};
    bool multi = b == last_press_button_ &&
                 now_ - last_press_time_ < Event::kMultiClickTime &&
                 d.length() <= Event::kMultiClickSlop;
    click_count_ = multi ? click_count_ + 1 : 1;
    last_press_button_ = b;
    last_press_time_ = now_;
    last_press_pos_ = e.pos;
    e.clicks = click_count_;
    if (tracked) {
        press_target_[b] = nullptr;
        swallow_release_[b] = false;
    }

    if (tooltip_open_) {
        close_popups();
        tooltip_open_ = false;
        hover_since_ = -1;
    }

    // Right-click opens the nearest ancestor's context menu, if any; the
    // matching release is swallowed.
    if (b == MouseRight) {
        if (Widget* hit = hit_test(e.pos)) {
            for (Widget* chain = hit; chain; chain = chain->parent) {
                if (chain->context_menu_) {
                    close_popups();
                    open_context_menu(*chain->context_menu_, e.pos);
                    swallow_release_[b] = true;
                    return;
                }
            }
        }
    }

    // A modal dialog traps presses outside it.
    if (Widget* modal = topmost_modal()) {
        if (!inside_hit_any(*modal, e.pos)) return;
    }
    Widget* hit = hit_test(e.pos);
    if (!hit) {
        if (b == MouseLeft) set_focus(nullptr);
        return;
    }
    // A press outside every popup closes the whole stack; a press inside one
    // dispatches into it untouched - menu items manage their own sub-stack.
    if (has_popups() && topmost_popup_containing(e.pos) < 0) {
        close_popups();
        return;
    }
    inside_press_ = true;
    if (tracked) press_target_[b] = hit;
    dispatch(*hit, e);
    if (b == MouseLeft)
        if (Widget* f = nearest_focusable(hit)) set_focus(f);
}

inline void Context::handle_release(const Event& in) {
    Event e = in;
    const int b = e.button;
    const bool tracked = b >= 0 && b < kButtons;
    if (tracked && swallow_release_[b]) {
        swallow_release_[b] = false;
        return;
    }
    e.clicks = b == last_press_button_ ? click_count_ : 1;
    inside_press_ = false;

    // Where the pointer really is (capture redirects the release itself).
    // Decide "landed on the pressed widget" before dispatching: the release
    // handler may destroy widgets (forget() then nulls press_target_).
    Widget* under = hit_test_uncaptured(e.pos);
    bool landed = tracked && press_target_[b] && under && inside(under, press_target_[b]);
    if (Widget* hit = hit_test(e.pos)) dispatch(*hit, e);

    // Click: the release landed inside the widget that received the press.
    Widget* target = tracked ? press_target_[b] : nullptr; // null if destroyed
    if (landed && target) {
        Event click = e;
        click.type = Event::Type::Click;
        dispatch(*target, click);
        // Still alive after the Click handler? (it may have closed a dialog)
        if (e.clicks == 2 && press_target_[b] == target) {
            Event dbl = click;
            dbl.type = Event::Type::DoubleClick;
            dispatch(*target, dbl);
        }
    }
    if (tracked) press_target_[b] = nullptr;
}

} // namespace slikui
