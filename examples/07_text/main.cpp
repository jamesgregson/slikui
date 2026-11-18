// examples/07_text/main.cpp - text styles, font families, and custom widgets
// that draw text. The C++ twin of `python -m slikui.examples.text`.
//
//   Label styles  - set_bold / set_italic / set_underline / set_strike, and
//                   set_font("mono") for a font family.
//   Tag           - a pill sized to its text: measure() asks
//                   ctx.text_size, paint() centers it with env.text_in.
//   StatCard      - three env.text_in anchors (top-left, center, bottom-right).
//   FitBox        - ellipsis vs wrapping at a width the slider sets.
//   SelectableText- a kept TextBlock: hit_test / caret_rect /
//                   selection_rects give drag-to-select.
//
// Modes:
//   ./07_text                      interactive (GLFW + OpenGL)
//   ./07_text --smoke [out.ppm]    headless: scripted checks + screenshot
//                                  (python3 tools/ppm2png.py converts it)
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/gl3.h> // GL first: silenced deprecations, no legacy gl.h

#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>
#include <slikui/backends/glfw/glfw_window.hpp>
#include <slikui/backends/opengl/opengl.hpp>
#include <slikui/backends/software/software.hpp>
#include <slikui/slikui.hpp>
#include <slikui/text/system_fonts.hpp>

using namespace slikui;

static const Color kMuted = Color::from_hex(0x8A94A0);

// ---------------------------------------------------------------------------
// Tag: a pill-shaped label sized to its text. measure() and paint() pass the
// same options, so the measured and drawn text agree.
// ---------------------------------------------------------------------------
class Tag : public Widget {
public:
    static constexpr float kPadX = 10, kPadY = 4;

    explicit Tag(std::string text, Color color = Color::transparent())
        : text_(std::move(text)), color_(color) {}

    Size measure(const Constraints& c) override {
        Size s = context()->text_size(text_, opts());
        return c.clamp({s.w + 2 * kPadX, s.h + 2 * kPadY});
    }
    bool stretch_cross() const override { return false; }

    void paint(const PaintEnv& env) const override {
        env.list.rect(bounds, color_.a ? color_ : env.theme.accent, bounds.h / 2);
        TextOpts o = opts();
        o.color = Color(255, 255, 255);
        o.align = TextAlign::Center;
        o.valign = TextVAlign::Middle;
        env.text_in(bounds, text_, o);
    }

private:
    static TextOpts opts() { return {.size = 12, .bold = true}; }

    std::string text_;
    Color color_;
};

// ---------------------------------------------------------------------------
// StatCard: caption top-left, value centered and large, unit bottom-right.
// Three anchors, three env.text_in calls, no layout objects.
// ---------------------------------------------------------------------------
class StatCard : public Widget {
public:
    StatCard(std::string caption, std::string value, std::string unit)
        : caption_(std::move(caption)), value_(std::move(value)), unit_(std::move(unit)) {}

    Size measure(const Constraints& c) override { return c.clamp({140, 84}); }
    bool stretch_cross() const override { return false; }

    void paint(const PaintEnv& env) const override {
        const Theme& th = env.theme;
        env.list.rect(bounds, th.surface_alt, 6);
        Rect in{bounds.x + 10, bounds.y + 8, bounds.w - 20, bounds.h - 16};
        env.text_in(in, caption_, {.size = 12, .color = th.text_dim});
        env.text_in(in, value_, {.size = 28, .bold = true,
                                 .align = TextAlign::Center, .valign = TextVAlign::Middle});
        env.text_in(in, unit_, {.size = 12, .color = th.text_dim, .italic = true,
                                .align = TextAlign::Right, .valign = TextVAlign::Bottom});
    }

private:
    std::string caption_, value_, unit_;
};

// ---------------------------------------------------------------------------
// FitBox: one sentence cut to a line with an ellipsis, and wrapped. measure()
// asks for the wrapped height, so the widget grows as the width shrinks.
// ---------------------------------------------------------------------------
class FitBox : public Widget {
public:
    static constexpr const char* kText =
        "Text that is too wide for its box can be cut short with an "
        "ellipsis, or wrapped onto as many lines as it needs.";

    float width = 300;

    Size measure(const Constraints& c) override {
        float line = context()->text_metrics().line_height;
        return c.clamp({0, line + 12 + wrapped().h});
    }

    void paint(const PaintEnv& env) const override {
        const Theme& th = env.theme;
        Rect line = env.text_in({bounds.x, bounds.y, width, bounds.h}, kText,
                                {.overflow = TextOverflow::Ellipsis});
        Rect para{bounds.x, line.y + line.h + 12, width, para_h_};
        env.list.rect(para, th.surface_alt, 3);
        env.text_in(para, kText, {.wrap = true});
        float edge = bounds.x + width; // the width both texts were given
        env.list.line({edge, bounds.y}, {edge, bounds.y + bounds.h}, th.accent, 1);
    }

private:
    Size wrapped() {
        Size s = context()->text_size(kText, {.wrap = true, .max_width = width});
        para_h_ = s.h; // paint() is const: keep what measure() learned
        return s;
    }

    float para_h_ = 0;
};

// ---------------------------------------------------------------------------
// SelectableText: wrapped, drag-to-select text. The kept TextBlock answers
// which byte is under the pointer, where the caret goes and what to
// highlight. Offsets are UTF-8 byte offsets into the string.
// ---------------------------------------------------------------------------
class SelectableText : public Widget {
public:
    explicit SelectableText(std::string text) : text_(std::move(text)) {}

    SelectableText& on_select(std::function<void(std::string_view)> f) {
        on_select_ = std::move(f);
        return *this;
    }
    std::string_view selected() const {
        size_t a = std::min(anchor_, caret_), b = std::max(anchor_, caret_);
        return std::string_view(text_).substr(a, b - a);
    }
    const TextBlock& block() const { return block_; }

    Size measure(const Constraints& c) override {
        float w = c.max.x < kInfinite ? c.max.x : 0;
        return c.clamp(context()->text_size(text_, {.wrap = true, .max_width = w}));
    }

    void arrange(const Rect& area) override {
        bounds = area;
        // Re-lay the block only when the width or the UI scale changes.
        float scale = context()->theme().scale;
        if (area.w != block_w_ || scale != block_scale_) {
            block_ = context()->text_layout(text_, area.w);
            block_w_ = area.w;
            block_scale_ = scale;
        }
    }

    bool on_event(const Event& e) override {
        using T = Event::Type;
        if (e.type == T::MousePress && e.button == int(MouseButton::MouseLeft)) {
            anchor_ = caret_ = index(e.pos);
            dragging_ = true;
            context()->capture_mouse(this);
            return true;
        }
        if (e.type == T::MouseMove && dragging_) {
            caret_ = index(e.pos);
            return true;
        }
        if (e.type == T::MouseRelease && dragging_) {
            dragging_ = false;
            context()->release_mouse(this);
            if (on_select_) on_select_(selected());
            return true;
        }
        return false;
    }

    void paint(const PaintEnv& env) const override {
        const Theme& th = env.theme;
        for (Rect r : block_.selection_rects(anchor_, caret_))
            env.list.rect({bounds.x + r.x, bounds.y + r.y, r.w, r.h}, th.accent.with_alpha(0.35f));
        env.draw(block_, {bounds.x, bounds.y});
        Rect c = block_.caret_rect(caret_);
        env.list.rect({bounds.x + c.x, bounds.y + c.y, 1.5f, c.h}, th.accent);
    }

private:
    size_t index(Vec2 p) const { return block_.hit_test({p.x - bounds.x, p.y - bounds.y}); }

    std::string text_;
    TextBlock block_;
    float block_w_ = -1, block_scale_ = 0;
    size_t anchor_ = 0, caret_ = 0;
    bool dragging_ = false;
    std::function<void(std::string_view)> on_select_;
};

// ---------------------------------------------------------------------------
// Fonts: give the "mono" role a real monospace font when the system has one.
// Without it, "mono" falls back to the body font; styles a family lacks
// (here italic) are synthesized from the closest face.
// ---------------------------------------------------------------------------
static void register_mono(Context& ctx) {
    for (auto style : {text::FontStyle::Regular, text::FontStyle::Bold})
        if (SystemFont f = find_system_font_face("mono", style))
            ctx.add_font("mono", f.path, style, f.face);
}

// ---------------------------------------------------------------------------
struct UI {
    Label* plain = nullptr;
    Label* bold = nullptr;
    Tag* tag = nullptr;
    Slider* width = nullptr;
    FitBox* fit = nullptr;
    SelectableText* sel = nullptr;
    Label* status = nullptr;
};

static void section(Column& root, const char* title) {
    root.add<Label>(title).set_bold().set_color(kMuted);
}

static void build_ui(Context& ctx, UI& ui) {
    register_mono(ctx);
    auto& root = ctx.set_root<Column>();
    root.set_padding(16).set_gap(8);
    root.add<Label>("Text").set_font_size(26).set_bold();

    section(root, "Label styles");
    auto& styles = root.add<Row>();
    styles.set_gap(12);
    ui.plain = &styles.add<Label>("Regular");
    ui.bold = &styles.add<Label>("Bold").set_bold();
    styles.add<Label>("Italic").set_italic();
    styles.add<Label>("Underline").set_underline();
    styles.add<Label>("Strike").set_strike();
    styles.add<Label>("mono()").set_font("mono");

    section(root, "env.text_in: tags sized by text_size, stat cards");
    auto& tags = root.add<Row>();
    tags.set_gap(6);
    ui.tag = &tags.add<Tag>("release");
    tags.add<Tag>("v0.5", Color::from_hex(0x3FA46A));
    tags.add<Tag>("text", Color::from_hex(0xC2733A));
    auto& cards = root.add<Row>();
    cards.set_gap(10);
    cards.add<StatCard>("Latency", "42", "ms");
    cards.add<StatCard>("Throughput", "1.2k", "req/s");

    section(root, "Ellipsis and wrapping - drag the slider");
    ui.width = &root.add<Slider>(80.f, 460.f);
    ui.width->set_value(300, false);
    ui.fit = &root.add<FitBox>();
    ui.width->on_change([fit = ui.fit](float v) { fit->width = v; });

    section(root, "TextBlock - drag across the text to select");
    ui.sel = &root.add<SelectableText>(
        "A TextBlock keeps its layout, so a widget can ask which character is under "
        "the pointer, where the caret goes and which rectangles to highlight.\n"
        "New lines work too.");
    ui.status = &root.add<Label>("selected: (nothing)");
    ui.status->set_color(kMuted);
    ui.sel->on_select([status = ui.status](std::string_view s) {
        status->set_text("selected: '" + std::string(s) + "'");
    });
}

// ---------------------------------------------------------------------------
// Headless smoke: the same checks as the Python twin's --smoke pass.
// ---------------------------------------------------------------------------
static Event mouse(Event::Type t, Vec2 p) {
    Event e{};
    e.type = t;
    e.pos = p;
    e.button = int(MouseButton::MouseLeft);
    return e;
}
static void drag(Context& ctx, Vec2 from, Vec2 to) {
    ctx.handle(mouse(Event::Type::MousePress, from));
    ctx.handle(mouse(Event::Type::MouseMove, to));
    ctx.handle(mouse(Event::Type::MouseRelease, to));
}

static int run_smoke(const char* ppm_path) {
    constexpr int W = 520, H = 600;
    std::vector<std::uint32_t> pixels(size_t(W) * H);
    software::Target target(pixels.data(), W, H);
    software::Sink sink(target);
    Context ctx(sink);
    ctx.load_font_file(find_system_font());
    ctx.set_frame_size({float(W), float(H)});

    UI ui;
    build_ui(ctx, ui);
    double now = 0;
    auto frame = [&] {
        now += 1.0 / 60;
        ctx.tick(Time{now, 1.f / 60});
        ctx.render();
    };
    frame();

    int failed = 0;
    auto ok = [&](bool cond, const std::string& label) {
        std::printf("%s  %s\n", cond ? "ok  " : "FAIL", label.c_str());
        failed += !cond;
    };
    char buf[128];

    ok(ctx.text_size("Bold", {.bold = true}).w > ctx.text_size("Bold").w,
       "bold text is wider than regular");
    Size s = ctx.text_size("release", {.size = 12, .bold = true});
    std::snprintf(buf, sizeof buf, "Tag sized from text_size (%.1f px wide)", ui.tag->bounds.w);
    ok(std::fabs(ui.tag->bounds.w - (s.w + 2 * Tag::kPadX)) < 0.5f, buf);

    float h0 = ui.fit->bounds.h;
    ui.width->set_value(140);
    frame();
    ok(ui.fit->width == 140, "slider drives the FitBox width");
    std::snprintf(buf, sizeof buf, "narrower box wraps taller (%.0f -> %.0f px)", h0,
                  ui.fit->bounds.h);
    ok(ui.fit->bounds.h > h0, buf);
    ui.width->set_value(300);
    frame();

    Rect sb = ui.sel->bounds;
    float line = ui.sel->block().line_height();
    drag(ctx, {sb.x + 2, sb.y + line / 2}, {sb.x + sb.w, sb.y + sb.h - 2});
    frame();
    ok(ui.sel->selected().find('\n') != std::string_view::npos, "selection spans lines");

    Rect end = ui.sel->block().caret_rect(std::string_view("A TextBlock").size());
    drag(ctx, {sb.x + 1, sb.y + line / 2}, {sb.x + end.x - 1, sb.y + line / 2});
    frame();
    ok(ui.sel->selected() == "A TextBlock",
       "drag selected '" + std::string(ui.sel->selected()) + "'");
    ok(ui.status->text() == "selected: 'A TextBlock'", "on_select reported it");

    std::FILE* out = std::fopen(ppm_path, "wb");
    if (!out) {
        std::fprintf(stderr, "smoke: cannot write %s\n", ppm_path);
        return 1;
    }
    std::fprintf(out, "P6\n%d %d\n255\n", W, H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            Color c = Color::from_abgr(target.at(x, y));
            std::uint8_t rgb[3] = {c.r, c.g, c.b};
            std::fwrite(rgb, 1, 3, out);
        }
    std::fclose(out);
    std::printf("smoke %s: screenshot %s\n", failed ? "FAILED" : "ok", ppm_path);
    return failed ? 1 : 0;
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--smoke")
        return run_smoke(argc > 2 ? argv[2] : "text_smoke.ppm");

    glfw::GlfwWindow win(520, 600, "slikui text");
    win.set_min_size(360, 420);
    if (!win.ok()) return 1;
    opengl::OpenglSink sink;
    if (!sink.init()) return 1;

    Context ctx(sink);
    ctx.load_font_file(find_system_font());
    UI ui;
    build_ui(ctx, ui);

    auto last = std::chrono::steady_clock::now();
    double now = 0;
    while (!win.should_close()) {
        win.dispatch(ctx);
        ctx.set_frame_size(win.point_size()); // LOGICAL frame: layout in points
        ctx.theme().scale = win.ui_scale();
        sink.set_scale(win.ui_scale());

        auto pt = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(pt - last).count();
        last = pt;
        now += dt;
        ctx.tick(Time{now, clamp_dt(dt)});
        ctx.render();
        win.swap();
    }
    return 0;
}
