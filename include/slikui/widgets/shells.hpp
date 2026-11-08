// slikui/widgets/shells.hpp — Phase D floating shells & feedback:
// Dialog, MessageBox, FileBrowser (floating layer), Toast, StatusBar,
// LogView, SearchBox, GroupBox, ImageView, CompareView.
//
// Dialogs live on Context's dialog layer (above popups; modal dialogs dim
// the app and trap outside presses). They are borrowed like Menus: build
// once, open/close many times — or hand Context ownership with
// open_dialog(std::unique_ptr<Dialog>).
#pragma once

#include "slikui/context.hpp"
#include "slikui/style/anim.hpp"
#include "slikui/widgets/containers.hpp"
#include "slikui/widgets/controls.hpp"
#include "slikui/widgets/icons.hpp"
#include "slikui/widgets/image.hpp"
#include "slikui/widgets/inputs.hpp"
#include "slikui/widgets/text.hpp"
#include "slikui/widgets/toolbar.hpp"
#include "slikui/widgets/views.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace slikui {

// ---------------------------------------------------------------------------
// Dialog — floating window: draggable title bar with a close button over a
// content column. Opened via Context::open_dialog; closed by the ✕ button,
// close_dialog(), or Escape (topmost first). A modal dialog dims the app and
// swallows presses outside it.
// ---------------------------------------------------------------------------
class Dialog : public Panel {
public:
    Dialog(std::string title, std::string widget_id = {})
        : Panel(std::move(widget_id)), title_(std::move(title)) {
        set_padding(0).set_gap(0);
        auto& bar = Widget::add<Row>();
        bar.set_padding(8).set_gap(8);
        bar.add<Label>(title_);
        bar.add<Spacer>();
        bar.add<IconButton>(Icon::Close)
            .on_click([this] { request_close(); });
        content_ = &Widget::add<Column>();
        content_->set_padding(12).set_gap(8);
    }

    Column& content() { return *content_; }
    const std::string& title() const { return title_; }
    IconButton& close_button() { return *static_cast<IconButton*>(children[0]->children[2].get()); }
    Dialog& set_title(std::string t) {
        title_ = std::move(t);
        title_valid_ = false;
        static_cast<Label&>(*children[0]->children[0]).set_text(title_);
        return *this;
    }
    Dialog& set_modal(bool m) { modal_ = m; return *this; }
    bool modal() const { return modal_; }
    // Pin the dialog somewhere other than the default centered spot.
    Dialog& set_position(Vec2 p) { pos_ = p; pos_set_ = true; return *this; }
    Vec2 position() const { return pos_; }
    Dialog& on_close(std::function<void()> f) { on_close_ = std::move(f); return *this; }

    void request_close() {
        if (ctx) ctx->close_dialog(*this);
        if (on_close_) on_close_();
    }

    void attached(Context& c) override {
        Panel::attached(c);
        radius_ = c.theme().radius;
        draw_border_ = true;
    }

    // Pin to the requested position (set_position / drags) and stay inside
    // the frame — Context lays out at the current bounds, this corrects them.
    void arrange(const Rect& area) override {
        Rect a = area;
        if (pos_set_) {
            a.x = pos_.x;
            a.y = pos_.y;
        }
        Panel::arrange(a);
        if (ctx) {
            Vec2 lim = {std::fmax(ctx->frame_size().x - bounds.w, 0.f),
                        std::fmax(ctx->frame_size().y - bounds.h, 0.f)};
            bounds.x = std::clamp(bounds.x, 0.f, lim.x);
            bounds.y = std::clamp(bounds.y, 0.f, lim.y);
        }
    }

    bool on_event(const Event& e) override {
        switch (e.type) {
        case Event::Type::MousePress:
            if (enabled && children[0]->bounds.contains(e.pos) &&
                !children[0]->children[2]->bounds.contains(e.pos)) {
                ctx->capture_mouse(this);
                dragging_ = true;
                last_ = e.pos;
                return true;
            }
            return false;
        case Event::Type::MouseMove:
            if (dragging_) {
                // Move the live bounds; pos_ follows so arrange stays pinned.
                bounds.x += e.pos.x - last_.x;
                bounds.y += e.pos.y - last_.y;
                pos_ = {bounds.x, bounds.y};
                pos_set_ = true;
                last_ = e.pos;
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

    const char* widget_type() const override { return "Dialog"; }

private:
    std::string title_;
    Column* content_ = nullptr;
    bool modal_ = false, dragging_ = false, pos_set_ = false, title_valid_ = false;
    Vec2 pos_{}, last_{};
    std::function<void()> on_close_;
};

// ---------------------------------------------------------------------------
// MessageBox — a Dialog with a message and an action-button row.
// ---------------------------------------------------------------------------
class MessageBox : public Dialog {
public:
    MessageBox(std::string title, std::string message, std::string widget_id = {})
        : Dialog(std::move(title), std::move(widget_id)) {
        auto& text = content().add<Label>(std::move(message));
        (void)text;
        buttons_ = &content().add<Row>();
        buttons_->set_gap(8);
    }

    // Adds a right-aligned action button; the callback runs before close.
    Button& add_button(std::string label, std::function<void()> action) {
        auto& b = buttons_->add<Button>(std::move(label));
        b.on_click([this, action] {
            if (action) action();
            request_close();
        });
        return b;
    }

    const char* widget_type() const override { return "MessageBox"; }

private:
    Row* buttons_ = nullptr;
};

// ---------------------------------------------------------------------------
// GroupBox — titled panel section.
// ---------------------------------------------------------------------------
class GroupBox : public Panel {
public:
    explicit GroupBox(std::string title, std::string widget_id = {})
        : Panel(std::move(widget_id)), title_(std::move(title)) {
        set_padding(10).set_gap(6);
        title_label_ = &Widget::add<Label>(title_);
        title_label_->set_color(Color::transparent()); // resolved at paint
    }

    GroupBox& set_title(std::string t) {
        title_ = std::move(t);
        title_label_->set_text(title_);
        return *this;
    }
    const std::string& title() const { return title_; }

    void attached(Context& c) override {
        Panel::attached(c);
        radius_ = c.theme().radius;
        draw_border_ = true;
        title_label_->set_color(c.theme().text_dim);
        title_label_->set_font_size(c.theme().small_font_px);
    }

    const char* widget_type() const override { return "GroupBox"; }

private:
    std::string title_;
    Label* title_label_ = nullptr;
};

// ---------------------------------------------------------------------------
// SearchBox — magnifier + field + clear button, one control.
// ---------------------------------------------------------------------------
class SearchBox : public Widget {
public:
    explicit SearchBox(std::string widget_id = {}) : Widget(std::move(widget_id)) {
        Widget::add(std::make_unique<IconButton>(Icon::Search));
        icon_ = static_cast<IconButton*>(children[0].get());
        icon_->set_variant(Button::Variant::Ghost);
        auto field = std::make_unique<TextField>();
        field_ = field.get();
        Widget::add(std::move(field));
        Widget::add(std::make_unique<IconButton>(Icon::Close));
        clear_ = static_cast<IconButton*>(children[2].get());
        clear_->set_variant(Button::Variant::Ghost);
        clear_->visible = false;
        clear_->on_click([this] {
            field_->set_text("");
            if (on_change_) on_change_("");
        });
        field_->on_change([this](std::string_view t) {
            clear_->visible = !t.empty();
            if (on_change_) on_change_(t);
        });
    }

    SearchBox& set_text(std::string t) {
        field_->set_text(std::move(t));
        clear_->visible = !field_->text().empty();
        return *this;
    }
    const std::string& text() const { return field_->text(); }
    SearchBox& set_placeholder(std::string p) {
        field_->set_placeholder(std::move(p));
        return *this;
    }
    SearchBox& on_change(std::function<void(std::string_view)> f) {
        on_change_ = std::move(f);
        return *this;
    }
    SearchBox& on_submit(std::function<void(std::string_view)> f) {
        field_->on_submit(std::move(f));
        return *this;
    }
    TextField& field() { return *field_; }
    IconButton& icon_button() { return *icon_; }
    IconButton& clear_button() { return *clear_; }

    // Not focusable itself — Tab reaches the inner TextField directly.

    Size measure(const Constraints& c) override {
        Size s = children[1]->measure(Constraints::loose({kInfinite, kInfinite}));
        return c.clamp({s.w + 44, std::fmax(s.h, 28.f)});
    }

    void arrange(const Rect& area) override {
        bounds = area;
        children[0]->arrange({area.x, area.y, 22, area.h});
        float right_pad = clear_->visible ? 22.f : 0.f;
        children[1]->arrange({area.x + 22, area.y,
                              std::fmax(area.w - 22 - right_pad, 0.f), area.h});
        if (clear_->visible)
            children[2]->arrange({area.right() - 22, area.y, 22, area.h});
    }

    const char* widget_type() const override { return "SearchBox"; }

private:
    IconButton* icon_ = nullptr;
    TextField* field_ = nullptr;
    IconButton* clear_ = nullptr;
    std::function<void(std::string_view)> on_change_;
};

// ---------------------------------------------------------------------------
// StatusBar — bottom strip: status text, transient messages, app-added
// widgets (ProgressBar/Spinner/labels) on the right.
// ---------------------------------------------------------------------------
class StatusBar : public Row {
public:
    explicit StatusBar(std::string widget_id = {}) : Row(std::move(widget_id)) {
        set_padding(4).set_gap(10);
        text_ = &add<Label>("");
        add<Spacer>();
    }

    StatusBar& set_text(std::string t) {
        base_ = std::move(t);
        transient_until_ = -1;
        text_->set_text(base_);
        return *this;
    }
    const std::string& text() const { return base_; }
    // Transient message shown for `seconds`, then the base text returns.
    StatusBar& show_message(std::string msg, double seconds = 3.0) {
        text_->set_text(std::move(msg));
        transient_until_ = last_now_ + seconds;
        return *this;
    }

    void tick(Time t) override {
        last_now_ = t.now;
        if (transient_until_ >= 0 && t.now >= transient_until_) {
            transient_until_ = -1;
            text_->set_text(base_);
        }
    }

    const char* widget_type() const override { return "StatusBar"; }

private:
    Label* text_ = nullptr;
    std::string base_;
    double transient_until_ = -1;
    double last_now_ = 0;
};

// ---------------------------------------------------------------------------
// LogView — severity-colored, autoscrolling console (shows the tail).
// ---------------------------------------------------------------------------
class LogView : public Widget {
public:
    enum class Level : std::uint8_t { Info, Success, Warning, Error };

    explicit LogView(std::string widget_id = {}) : Widget(std::move(widget_id)) {}

    LogView& log(Level level, std::string text) {
        // One line per entry; embedded newlines become spaces.
        for (auto& ch : text)
            if (ch == '\n' || ch == '\r') ch = ' ';
        lines_.push_back({std::move(text), level});
        if (lines_.size() > kMaxLines) {
            lines_.erase(lines_.begin(),
                         lines_.begin() + long(lines_.size() - kMaxLines));
            lays_valid_ = false; // indices shifted; rebuild
        }
        lays_valid_ = false;
        return *this;
    }
    LogView& clear() {
        lines_.clear();
        lays_valid_ = false;
        return *this;
    }
    size_t line_count() const { return lines_.size(); }

    Size measure(const Constraints& c) override {
        // Layouts are built here (not in paint) so new glyphs reach the font
        // atlas in the same frame's texture upload.
        refresh_layouts();
        return c.clamp({220.f, 100.f});
    }
    bool expands_main() const override { return true; }

    void paint(const PaintEnv& env) const override {
        refresh_layouts();
        const Theme& t = env.theme;
        float inv = ui_scale_inv();
        float line_h = line_height_ * inv;
        env.list.rect(bounds, t.surface, t.radius);
        env.list.hollow_rect(bounds, t.border, t.border_width, t.radius);
        if (line_h <= 0) return;
        size_t shown = size_t(std::fmax(bounds.h / line_h, 1.f));
        size_t first = lines_.size() > shown ? lines_.size() - shown : 0;

        env.list.push_clip(bounds);
        float y = bounds.y;
        for (size_t i = first; i < lines_.size(); ++i) {
            const auto& lay = lays_[i];
            float text_h = lay.size.h * inv;
            for (const auto& pg : lay.glyphs) {
                const text::Glyph& g = *pg.glyph;
                if (g.uv.empty()) continue;
                env.list.glyph({bounds.x + 8 + pg.pos.x * inv,
                                y + (line_h - text_h) * .5f + pg.pos.y * inv,
                                g.size.w * inv, g.size.h * inv},
                               g.tex_id, g.uv, level_color(t, lines_[i].level));
            }
            y += line_h;
        }
        env.list.pop_clip();
    }

    const char* widget_type() const override { return "LogView"; }

private:
    struct Line {
        std::string text;
        Level level;
    };
    static constexpr size_t kMaxLines = 512;

    static Color level_color(const Theme& t, Level l) {
        switch (l) {
        case Level::Success: return t.success;
        case Level::Warning: return t.warning;
        case Level::Error: return t.danger;
        default: return t.text;
        }
    }
    float ui_scale_inv() const { return 1.f / ui_scale(); }

    void refresh_layouts() const {
        if (lays_valid_ || !ctx) return;
        const Theme& t = ctx->theme();
        lays_.resize(lines_.size());
        line_height_ = 0;
        for (size_t i = 0; i < lines_.size(); ++i) {
            lays_[i] = ctx->font().layout(lines_[i].text, t.s(t.small_font_px));
            line_height_ = std::fmax(line_height_, lays_[i].line_height);
        }
        lays_valid_ = true;
    }

    std::vector<Line> lines_;
    mutable std::vector<text::TextLayout> lays_;
    mutable float line_height_ = 0;
    mutable bool lays_valid_ = false;
};

// ---------------------------------------------------------------------------
// FileBrowser — in-UI directory listing + name field on a Dialog shell.
// Directories navigate on click; files land in the name field. Open/Save
// commits through on_pick; Cancel closes. `..` navigates up.
// ---------------------------------------------------------------------------
class FileBrowser : public Dialog {
public:
    enum class Mode : std::uint8_t { Open, Save };

    FileBrowser(Mode mode, std::string title = {}, std::string widget_id = {})
        : Dialog(title.empty() ? (mode == Mode::Open ? "Open" : "Save As")
                               : std::move(title),
                 std::move(widget_id)),
          mode_(mode) {
        path_label_ = &content().add<Label>("");
        list_ = &content().add<ListView>();
        list_->on_select([this](size_t i) { entry_clicked(long(i)); });
        name_ = &content().add<TextField>();
        name_->set_placeholder("file name");
        auto& buttons = content().add<Row>();
        buttons.set_gap(8);
        buttons.add<Spacer>();
        buttons.add<Button>("Cancel").on_click([this] { request_close(); });
        open_button_ = &buttons.add<Button>(mode_ == Mode::Open ? "Open" : "Save");
        open_button_->set_variant(Button::Variant::Primary)
            .on_click([this] { commit(); });
        set_path(".");
    }

    FileBrowser& set_path(std::string dir) {
        path_ = std::filesystem::path(std::move(dir)).lexically_normal();
        refresh();
        return *this;
    }
    std::string path() const { return path_.string(); }
    // The selected file (path + name field); empty when nothing is chosen.
    std::string selection() const {
        if (name_->text().empty()) return {};
        return (path_ / name_->text()).lexically_normal().string();
    }
    FileBrowser& on_pick(std::function<void(std::string)> f) {
        on_pick_ = std::move(f);
        return *this;
    }
    ListView& list() { return *list_; }
    TextField& name_field() { return *name_; }
    Button& open_button() { return *open_button_; }

    void attached(Context& c) override {
        Dialog::attached(c);
        path_label_->set_color(c.theme().text_dim);
    }

    const char* widget_type() const override { return "FileBrowser"; }

private:
    void refresh() {
        entries_.clear();
        is_dir_.clear();
        std::error_code ec;
        std::vector<std::string> dirs;
        if (path_.has_parent_path()) {
            entries_.push_back("..");
            is_dir_.push_back(true);
        }
        std::vector<std::string> files;
        for (const auto& it : std::filesystem::directory_iterator(path_, ec)) {
            bool dir = it.is_directory(ec);
            if (dir) {
                dirs.push_back(it.path().filename().string() + "/");
            } else {
                files.push_back(it.path().filename().string());
            }
        }
        std::sort(dirs.begin(), dirs.end());
        std::sort(files.begin(), files.end());
        for (auto& d : dirs) {
            entries_.push_back(d);
            is_dir_.push_back(true);
        }
        for (auto& f : files) {
            entries_.push_back(f);
            is_dir_.push_back(false);
        }
        list_->set_items(entries_).set_selected(-1);
        path_label_->set_text(path_.string());
    }

    void entry_clicked(long i) {
        if (i < 0 || size_t(i) >= entries_.size()) return;
        if (is_dir_[size_t(i)]) {
            std::string name = entries_[size_t(i)];
            if (name == "..") path_ = path_.parent_path();
            else path_ /= name.substr(0, name.size() - 1); // strip "/"
            refresh();
        } else {
            name_->set_text(entries_[size_t(i)]);
        }
    }

    void commit() {
        std::string sel = selection();
        if (sel.empty()) return;
        if (on_pick_) on_pick_(sel);
        request_close();
    }

    Mode mode_;
    Label* path_label_ = nullptr;
    ListView* list_ = nullptr;
    TextField* name_ = nullptr;
    Button* open_button_ = nullptr;
    std::filesystem::path path_;
    std::vector<std::string> entries_;
    std::vector<bool> is_dir_;
    std::function<void(std::string)> on_pick_;
};

// ---------------------------------------------------------------------------
// Toast — transient corner notice. Pushed owned via Context::push_toast:
// positioned in the bottom-right corner, removed automatically after the
// requested seconds (see Context::tick).
// ---------------------------------------------------------------------------
class Toast : public Widget {
public:
    explicit Toast(std::string text, std::string widget_id = {})
        : Widget(std::move(widget_id)), text_(std::move(text)) {}

    Toast& set_icon(Icon icon) {
        icon_ = icon;
        has_icon_ = true;
        return *this;
    }

    Size measure(const Constraints& c) override {
        refresh();
        float inv = ui_scale_inv();
        float w = 14 + (has_icon_ ? 20.f : 0.f) + lay_.size.w * inv + 14;
        return c.clamp({w, lay_.size.h * inv + 12.f});
    }

    void paint(const PaintEnv& env) const override {
        refresh();
        const Theme& t = env.theme;
        float inv = ui_scale_inv();
        env.list.rect(bounds, t.surface_alt, t.radius);
        env.list.hollow_rect(bounds, t.border, t.border_width, t.radius);
        float x = bounds.x + 14;
        if (has_icon_) {
            float side = bounds.h - 10;
            paint_icon(env.list, icon_, {x, bounds.center().y - side * .5f, side, side},
                       t.accent);
            x += 20;
        }
        paint_text(env, lay_, {x, bounds.center().y - lay_.size.h * inv * .5f},
                   t.text, inv);
    }

    const char* widget_type() const override { return "Toast"; }

    bool stretch_cross() const override { return false; }

private:
    float ui_scale_inv() const { return 1.f / ui_scale(); }
    void refresh() const {
        if (valid_ || !ctx) return;
        lay_ = ctx->font().layout(text_, ctx->theme().s(ctx->theme().base_font_px));
        valid_ = true;
    }

    std::string text_;
    bool has_icon_ = false;
    Icon icon_ = Icon::Check;
    mutable text::TextLayout lay_;
    mutable bool valid_ = false;
};

// ---------------------------------------------------------------------------
// ImageView — pan/zoom preview over an RGBA bitmap (textures, render
// targets). Wheel zooms around the cursor, drag pans (grab cursor),
// checkerboard shows through transparent regions.
// ---------------------------------------------------------------------------
class ImageView : public Widget {
public:
    explicit ImageView(std::string widget_id = {})
        : Widget(std::move(widget_id)) {
        hover_cursor_ = Cursor::Grab;
    }

    ImageView& set_pixels(int w, int h, const std::uint8_t* rgba) {
        if (w <= 0 || h <= 0 || !rgba) return *this;
        w_ = w;
        h_ = h;
        pixels_.assign(rgba, rgba + size_t(w) * h * 4);
        dirty_ = true;
        fit_pending_ = true;
        return *this;
    }
    bool set_file(const std::string& path) {
        int w = 0, h = 0, n = 0;
        std::uint8_t* px = stbi_load(path.c_str(), &w, &h, &n, 4);
        if (!px) return false;
        set_pixels(w, h, px);
        stbi_image_free(px);
        return true;
    }
    int width() const { return w_; }
    int height() const { return h_; }
    float zoom() const { return zoom_; }
    Vec2 pan() const { return pan_; }

    void attached(Context& c) override { tex_id_ = c.next_texture_id(); }

    Size measure(const Constraints& c) override { return c.clamp({200.f, 150.f}); }
    bool expands_main() const override { return true; }

    void paint(const PaintEnv& env) const override {
        const Theme& t = env.theme;
        // checkerboard: 8px squares in two grays
        float cell = 8.f;
        Color a = t.surface_alt, b = t.surface;
        for (float y = bounds.y; y < bounds.bottom(); y += cell)
            for (float x = bounds.x; x < bounds.right(); x += cell) {
                bool odd = (int(std::floor(x / cell)) + int(std::floor(y / cell))) % 2;
                env.list.rect({x, y, std::fmin(cell, bounds.right() - x),
                               std::fmin(cell, bounds.bottom() - y)},
                              odd ? b : a, 0);
            }
        if (w_ <= 0) return;
        if (fit_pending_) {
            zoom_ = std::fmin(bounds.w / float(w_), bounds.h / float(h_));
            pan_ = {0, 0};
            fit_pending_ = false;
        }
        env.list.push_clip(bounds);
        Rect dst{bounds.center().x - float(w_) * zoom_ * .5f + pan_.x,
                 bounds.center().y - float(h_) * zoom_ * .5f + pan_.y,
                 float(w_) * zoom_, float(h_) * zoom_};
        env.list.image(dst, tex_id_, {0, 0, float(w_), float(h_)});
        env.list.pop_clip();
        env.list.hollow_rect(bounds, t.border, t.border_width, 0);
    }

    bool on_event(const Event& e) override {
        switch (e.type) {
        case Event::Type::Scroll:
            if (!bounds.contains(e.pos)) return false;
            zoom_at(e.pos, std::exp(e.scroll.y * .15f)); // wheel up = zoom in
            return true;
        case Event::Type::MousePress:
            if (enabled && bounds.contains(e.pos)) {
                ctx->capture_mouse(this);
                panning_ = true;
                last_ = e.pos;
                return true;
            }
            return false;
        case Event::Type::MouseMove:
            if (panning_) {
                pan_ += {e.pos.x - last_.x, e.pos.y - last_.y};
                last_ = e.pos;
                return true;
            }
            return false;
        case Event::Type::MouseRelease:
            if (panning_) {
                panning_ = false;
                ctx->release_mouse(this);
                return true;
            }
            return false;
        default:
            return false;
        }
    }

    void collect_uploads(std::vector<TexUpload>& out) override {
        if (dirty_ && !pixels_.empty() && tex_id_) {
            out.push_back(TexUpload{tex_id_, w_, h_, TexFormat::Rgba8, pixels_.data()});
            dirty_ = false;
        }
    }

    const char* widget_type() const override { return "ImageView"; }

private:
    void zoom_at(Vec2 anchor, float factor) {
        float old = zoom_;
        zoom_ = std::clamp(zoom_ * factor, .05f, 32.f);
        if (zoom_ == old) return;
        // keep the anchored point fixed: adjust pan around the view center
        Vec2 c = bounds.center();
        Vec2 rel{anchor.x - c.x - pan_.x, anchor.y - c.y - pan_.y};
        pan_.x += rel.x * (1.f - zoom_ / old);
        pan_.y += rel.y * (1.f - zoom_ / old);
    }

    int w_ = 0, h_ = 0;
    std::vector<std::uint8_t> pixels_;
    std::uint32_t tex_id_ = 0;
    bool dirty_ = false, panning_ = false;
    mutable bool fit_pending_ = false;
    mutable float zoom_ = 1;
    mutable Vec2 pan_{};
    Vec2 last_{};
};

// ---------------------------------------------------------------------------
// CompareView — before/after A|B split: two images drawn over the same rect,
// each clipped to its side of a draggable divider (tonemap/filter A-B).
// ---------------------------------------------------------------------------
class CompareView : public Widget {
public:
    explicit CompareView(std::string widget_id = {})
        : Widget(std::move(widget_id)) {
        hover_cursor_ = Cursor::ResizeH;
    }

    // Both images are stretched over the full view; A fills [0, frac],
    // B fills [frac, 1]. Call both setters once per frame at most.
    CompareView& set_pixels_a(int w, int h, const std::uint8_t* rgba) {
        return store(a_, w, h, rgba);
    }
    CompareView& set_pixels_b(int w, int h, const std::uint8_t* rgba) {
        return store(b_, w, h, rgba);
    }
    CompareView& set_fraction(float f) {
        fraction_ = clamp01(f);
        return *this;
    }
    float fraction() const { return fraction_; }

    void attached(Context& c) override {
        tex_a_ = c.next_texture_id();
        tex_b_ = c.next_texture_id();
    }

    Size measure(const Constraints& c) override { return c.clamp({220.f, 140.f}); }

    void paint(const PaintEnv& env) const override {
        const Theme& t = env.theme;
        float split = bounds.x + fraction_ * bounds.w;
        if (a_.w > 0) {
            env.list.push_clip({bounds.x, bounds.y, split - bounds.x, bounds.h});
            env.list.image(bounds, tex_a_, {0, 0, float(a_.w), float(a_.h)});
            env.list.pop_clip();
        }
        if (b_.w > 0) {
            env.list.push_clip({split, bounds.y, bounds.right() - split, bounds.h});
            env.list.image(bounds, tex_b_, {0, 0, float(b_.w), float(b_.h)});
            env.list.pop_clip();
        }
        env.list.line({split, bounds.y}, {split, bounds.bottom()}, Color::white(), 2);
    }

    bool on_event(const Event& e) override {
        switch (e.type) {
        case Event::Type::MousePress:
            if (enabled && bounds.contains(e.pos) &&
                std::fabs(e.pos.x - (bounds.x + fraction_ * bounds.w)) < 8.f) {
                ctx->capture_mouse(this);
                dragging_ = true;
                return true;
            }
            return false;
        case Event::Type::MouseMove:
            if (dragging_) {
                set_fraction((e.pos.x - bounds.x) / std::fmax(bounds.w, 1.f));
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

    void collect_uploads(std::vector<TexUpload>& out) override {
        if (a_.dirty && tex_a_) {
            out.push_back(TexUpload{tex_a_, a_.w, a_.h, TexFormat::Rgba8, a_.px.data()});
            a_.dirty = false;
        }
        if (b_.dirty && tex_b_) {
            out.push_back(TexUpload{tex_b_, b_.w, b_.h, TexFormat::Rgba8, b_.px.data()});
            b_.dirty = false;
        }
    }

    const char* widget_type() const override { return "CompareView"; }

private:
    struct Slot {
        int w = 0, h = 0;
        std::vector<std::uint8_t> px;
        bool dirty = false;
    };
    CompareView& store(Slot& s, int w, int h, const std::uint8_t* rgba) {
        if (w <= 0 || h <= 0 || !rgba) return *this;
        s.w = w;
        s.h = h;
        s.px.assign(rgba, rgba + size_t(w) * h * 4);
        s.dirty = true;
        return *this;
    }

    Slot a_, b_;
    std::uint32_t tex_a_ = 0, tex_b_ = 0;
    float fraction_ = .5f;
    bool dragging_ = false;
};

} // namespace slikui
