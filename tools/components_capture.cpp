// tools/components_capture.cpp — renders one image per widget family for the
// README component reference.
//
// Uses the software backend only: no window, no GPU, fully deterministic.
// Each section is a real slikui tree (the same code shown in the README),
// simulated for ~1 s of frames so hover/press animations settle, then dumped
// to a PPM (converted to PNG by tools/ppm2png.py).
//
//   clang++ -std=c++20 -Iinclude tools/components_capture.cpp -o /tmp/ccap
//   /tmp/ccap docs/images
#include <slikui/backends/software/software.hpp>
#include <slikui/slikui.hpp>
#include <slikui/text/system_fonts.hpp>

#include <slikui/widgets/all.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

using namespace slikui;

struct Buffer {
    int w = 0, h = 0;
    std::vector<std::uint32_t> px;
};

// slikui packs colors R | G<<8 | B<<16; PPM wants R,G,B bytes in file order.
static void dump_buffer(const char* path, const Buffer& b) {
    std::FILE* f = std::fopen(path, "wb");
    if (!f) { std::fprintf(stderr, "cannot write %s\n", path); return; }
    std::fprintf(f, "P6\n%d %d\n255\n", b.w, b.h);
    for (std::uint32_t c : b.px) {
        std::uint8_t rgb[3] = {
            std::uint8_t((c >> 0) & 0xFF),
            std::uint8_t((c >> 8) & 0xFF),
            std::uint8_t((c >> 16) & 0xFF),
        };
        std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
    std::printf("wrote %s (%dx%d)\n", path, b.w, b.h);
}

static std::uint32_t pack_rgb(std::uint32_t hex) { // 0xRRGGBB -> packed
    std::uint8_t r = std::uint8_t(hex >> 16), g = std::uint8_t(hex >> 8),
                 b = std::uint8_t(hex);
    return 0xFF000000u | (std::uint32_t(b) << 16) | (std::uint32_t(g) << 8) |
           std::uint32_t(r);
}

// Paste tiles into one buffer, in `dir` order, with a `gap` gutter.
static Buffer join(const std::vector<Buffer>& tiles, int gap, bool vertical,
                   std::uint32_t gap_hex = 0x0B0E12) {
    int w = 0, h = 0;
    for (auto& t : tiles) {
        if (vertical) { w = std::max(w, t.w); h += t.h; }
        else          { w += t.w; h = std::max(h, t.h); }
    }
    if (!tiles.empty()) {
        if (vertical) h += gap * int(tiles.size() - 1);
        else          w += gap * int(tiles.size() - 1);
    }
    Buffer out{w, h, std::vector<std::uint32_t>(std::size_t(w) * h,
                                                pack_rgb(gap_hex))};
    int x = 0, y = 0;
    for (auto& t : tiles) {
        for (int ty = 0; ty < t.h; ++ty)
            std::memcpy(&out.px[std::size_t(y + ty) * w + x], &t.px[std::size_t(ty) * t.w],
                        sizeof(std::uint32_t) * std::size_t(t.w));
        if (vertical) y += t.h + gap; else x += t.w + gap;
    }
    return out;
}

struct Opt {
    Vec2 hover{};            // logical px; sends MouseMove there every frame
    bool click_at_hover = false; // presses + releases at hover pos (frame 10)
    int frames = 70;
};

// Render one section and return its pixels (at 2x density).
static Buffer render_section(int w, int h,
                             const std::function<void(Context&, Column&)>& build,
                             const Opt& opt = {}, bool light = false,
                             const Theme* theme_override = nullptr,
                             float pad = 24.f, float gap = 12.f) {
    constexpr float S = 2.f; // render at 2x for crisp docs images
    software::Target target(int(w * S), int(h * S));
    software::Sink sink(target);
    sink.set_scale(S); // logical px -> 2x framebuffer (parity with GL sink)
    Context ctx(sink);
    ctx.set_frame_size({float(w), float(h)});
    ctx.load_font_file(find_system_font());
    if (theme_override) ctx.theme() = *theme_override;
    ctx.theme().scale = S;
    if (light) ctx.theme().use_light();

    auto& root = ctx.set_root<Column>();
    root.set_padding(pad).set_gap(gap);
    build(ctx, root);

    double now = 0.0;
    for (int f = 0; f < opt.frames; ++f) {
        if (opt.hover.x != 0.f || opt.hover.y != 0.f) {
            // Events arrive in LOGICAL px here (the window layer converts
            // framebuffer px before ctx.handle; FrameResize is the exception).
            Event mv{}; mv.type = Event::Type::MouseMove;
            mv.pos = opt.hover;
            ctx.handle(mv);
            if (opt.click_at_hover && f == 10) {
                Event pr{}; pr.type = Event::Type::MousePress;
                pr.pos = mv.pos; pr.button = 0;
                ctx.handle(pr);
                Event rl{}; rl.type = Event::Type::MouseRelease;
                rl.pos = mv.pos; rl.button = 0;
                ctx.handle(rl);
            }
        }
        now += 1.0 / 60.0;
        ctx.tick(Time{now, clamp_dt(1.0 / 60.0)});
        ctx.render();
    }
    return {target.width(), target.height(),
            {target.data(), target.data() + target.width() * target.height()}};
}

static void capture(const char* dir, const char* name, int w, int h,
                    const std::function<void(Context&, Column&)>& build,
                    const Opt& opt = {}, bool light = false) {
    std::string path = std::string(dir) + "/" + name + ".ppm";
    dump_buffer(path.c_str(), render_section(w, h, build, opt, light));
}

// A 64x48 RGBA gradient used by the Image examples (procedural, no file I/O).
static std::vector<std::uint8_t> gradient_rgba() {
    std::vector<std::uint8_t> px(64 * 48 * 4);
    for (int y = 0; y < 48; ++y)
        for (int x = 0; x < 64; ++x) {
            auto* p = &px[(std::size_t(y) * 64 + x) * 4];
            p[0] = std::uint8_t(40 + x * 3);          // r sweeps with x
            p[1] = std::uint8_t(70 + 3 * y);          // g sweeps with y
            p[2] = std::uint8_t(200 - x);             // b falls with x
            p[3] = 255;
        }
    return px;
}

// ---------------------------------------------------------------------------
// Custom widget: Meter — the C++ twin of the Python Meter in
// python/examples/custom_widget.py. Measures 10 px tall, stretches across the
// column (stretch_cross defaults to true), paints a rounded track plus an
// accent fill, and scrubs on click.
// ---------------------------------------------------------------------------
class Meter : public Widget {
public:
    explicit Meter(float value = 0.f) : value_(value) {}

    Size measure(const Constraints& c) override { return c.clamp({0.f, 10.f}); }

    void paint(const PaintEnv& env) const override {
        env.list.rect(bounds, env.theme.surface_alt, bounds.h / 2);
        if (value_ > 0.f)
            env.list.rect({bounds.x, bounds.y, bounds.w * value_, bounds.h},
                          env.theme.accent, bounds.h / 2);
    }

    bool on_event(const Event& e) override {
        // Click to the left/right of the fill to move it.
        if (e.type == Event::Type::MousePress && bounds.contains(e.pos)) {
            value_ = std::clamp((e.pos.x - bounds.x) / bounds.w, 0.f, 1.f);
            return true;
        }
        return false;
    }

private:
    float value_ = 0.f;
};

int main(int argc, char** argv) {
    const char* dir = argc > 1 ? argv[1] : "docs/images";

    // ---- buttons -------------------------------------------------------
    capture(dir, "buttons", 560, 112, [](Context&, Column& root) {
        auto& row = root.add<Row>();
        row.set_gap(10);
        row.add<Button>("Default");
        row.add<Button>("Primary").set_variant(Button::Variant::Primary);
        row.add<Button>("Danger").set_variant(Button::Variant::Danger);
        row.add<Button>("Ghost").set_variant(Button::Variant::Ghost);
    });

    // ---- toggles: checkbox / radio / switch ----------------------------
    capture(dir, "toggles", 640, 96, [](Context&, Column& root) {
        auto& row = root.add<Row>();
        row.set_gap(18);
        row.add<Checkbox>("Sync files", true);
        row.add<Checkbox>("Compress", false);
        row.add<Radio>("Wi-Fi", true);
        row.add<Radio>("Ethernet", false);
        row.add<Switch>().set_checked(true);
        row.add<Switch>().set_checked(false);
    });

    // ---- sliders, progress, spinner ------------------------------------
    // NOTE: keep in sync with the `editors` capture — `Slider`'s built-in
    // label/value row is shown there.
    capture(dir, "sliders", 560, 168, [](Context& ctx, Column& root) {
        auto& cap = root.add<Row>();
        cap.set_gap(8);
        cap.add<Label>("Volume");
        cap.add<Spacer>();
        cap.add<Label>("62%").set_color(ctx.theme().text_dim);
        root.add<Slider>(0.f, 100.f).set_value(62.f);
        auto& pb = root.add<ProgressBar>();
        pb.set_value(0.72f);
        auto& busy = root.add<Row>();
        busy.set_gap(10);
        busy.add<Spinner>();
        busy.add<Label>("Compiling shaders…").set_color(ctx.theme().text_dim);
    });

    // ---- labels ---------------------------------------------------------
    capture(dir, "labels", 560, 150, [](Context& ctx, Column& root) {
        root.add<Label>("Title — 26 px").set_font_size(26);
        root.add<Label>("Body label at the default 15 px size.");
        root.add<Label>("Dim secondary text.")
            .set_color(ctx.theme().text_dim);
        root.add<Label>("Accent-colored label.")
            .set_color(ctx.theme().accent);
    });

    // ---- text input ------------------------------------------------------
    capture(dir, "inputs", 560, 196, [](Context&, Column& root) {
        root.add<TextField>().set_text("jane@example.com").set_placeholder("you@host");
        root.add<TextField>().set_placeholder("Search…");
        root.add<TextArea>().set_rows(3)
            .set_text("slikui renders this entirely on the CPU —\nno GPU, no window, no dependencies.");
    });

    // ---- data display: plot + pad + tree + table + property grid ---------
    capture(dir, "data", 560, 400, [](Context& ctx, Column& root) {
        auto& cols = root.add<Row>();
        cols.set_gap(14);
        auto& left = cols.add<Column>();
        left.set_gap(10);
        auto& plot = left.add<Plot>();
        plot.set_fill(true);
        std::vector<Vec2> pts;
        for (int i = 0; i < 48; ++i) {
            float x = float(i) / 47.f * 6.2831f;
            pts.push_back({x, std::sin(x) * std::exp(-x / 6.f)});
        }
        plot.add_series(pts, ctx.theme().accent);
        left.add<Vec2Pad>().set_value({0.35f, 0.4f});

        auto& right = cols.add<Column>();
        right.set_gap(10);
        right.add<TreeView>().set_rows({
            {"Scene", 0, true},
            {"Dragon", 1, false},
            {"Suzanne", 1, false},
            {"Lights", 0, true},
            {"Sun", 1, false},
            {"Rim", 1, false},
        });
        auto& table = right.add<Table>();
        table.set_columns({{"Mesh", 100, false}, {"Tris", 60, true},
                           {"Pass", 44, true}});
        table.set_rows({{"Dragon", "870k", "4"}, {"Suzanne", "50k", "2"},
                        {"Bunny", "69k", "2"}});
        table.set_selected(1);

        auto& grid = root.add<PropertyGrid>();
        grid.add_property("Roughness").add<Slider>(0.f, 1.f).set_value(0.35f, false);
        grid.add_property("Samples").add<DragValue>(64.f);
        grid.add_section("Advanced");
    });

    // ---- menus: menu bar with the File menu open (click at frame 10) -----
    capture(dir, "menus", 560, 300, [](Context&, Column& root) {
        auto& bar = root.add<MenuBar>();
        auto& file = bar.add_menu("File");
        file.add_item("Open…", [] {}, "Ctrl+O");
        file.add_item("Save", [] {}, "Ctrl+S");
        file.add_separator();
        file.add_item("Quit", [] {}, "Ctrl+Q");
        auto& edit = bar.add_menu("Edit");
        edit.add_item("Undo", [] {}, "Ctrl+Z");
        edit.add_item("Redo", [] {}, "Ctrl+Shift+Z", false);
    }, {.hover = {44.f, 40.f}, .click_at_hover = true, .frames = 80});

    // ---- toolbar: icon buttons, separator, labeled button ----------------
    capture(dir, "toolbar", 560, 120, [](Context&, Column& root) {
        auto& bar = root.add<Toolbar>();
        bar.add_button(Icon::Play, [] {}, "Run");
        bar.add_button(Icon::Pause, [] {});
        bar.add_button(Icon::StepForward, [] {});
        bar.add_separator();
        bar.add_button(Icon::Grid, [] {}, "Toggle grid");
        bar.add_button(Icon::Camera, [] {}, "Screenshot");
        bar.add_button(Icon::Save, "Save", [] {});
        root.add<Label>("ghost fill fades in on hover");
    });

    // ---- shells: dialog + toast, group box, search, log, status, views --
    capture(dir, "shells", 560, 470, [](Context& ctx, Column& root) {
        auto grad = gradient_rgba();
        std::vector<std::uint8_t> grad2 = grad;
        for (std::size_t i = 0; i < grad2.size(); i += 4)
            std::swap(grad2[i], grad2[i + 2]); // swap R/B for the B image

        auto& top = root.add<Row>();
        top.set_gap(10);
        auto& left = top.add<Column>();
        left.set_gap(8);
        auto& gb = left.add<GroupBox>("Light");
        gb.add<Checkbox>("Enabled", true);
        gb.add<Slider>(0.f, 1.f);
        left.add<SearchBox>().set_placeholder("Search assets…");
        auto& log = top.add<LogView>();
        log.log(LogView::Level::Info, "loaded scene.dragon (24 ms)");
        log.log(LogView::Level::Warning, "missing texture: bark_d");
        log.log(LogView::Level::Error, "shader compile failed");
        log.log(LogView::Level::Success, "thumbnail bake complete");

        auto mb = std::make_unique<MessageBox>("Export", "Render 128 frames at 4K?");
        mb->add_button("Export", nullptr);
        mb->add_button("Cancel", nullptr);
        mb->set_position({300, 26});
        ctx.open_dialog(std::move(mb));
        auto toast = std::make_unique<Toast>("Screenshot saved");
        toast->set_icon(Icon::Camera);
        ctx.push_toast(std::move(toast), 60.0);

        auto& views = root.add<Row>();
        views.set_gap(10);
        auto& iv = views.add<ImageView>();
        iv.set_pixels(64, 48, grad.data());
        auto& cv = views.add<CompareView>();
        cv.set_pixels_a(64, 48, grad.data());
        cv.set_pixels_b(64, 48, grad2.data());
        cv.set_fraction(.62f);
        root.add<StatusBar>().set_text("60 fps · 1,440 objects");
    });

    // ---- editors: segmented, vec/drag values, labeled slider, colors ----
    // The color row sits last so the ColorEdit popup has room to open;
    // hover+click the middle chip to show it (like the Dropdown above).
    capture(dir, "editors", 560, 400, [](Context& ctx, Column& root) {
        root.add<Segmented>().set_items({"Cube", "Sphere", "Torus"})
            .set_selected(1);
        static float light_dir[3] = {0.60f, 0.40f, 0.70f};
        root.add<Vec3Edit>(light_dir, "XYZ");
        root.add<DragValue>(1.5f).set_prefix("exp ").set_suffix(" EV");
        root.add<Slider>(0.f, 1.f).set_label("Roughness")
            .set_format("%.2f").set_value(0.35f);
        auto& colors = root.add<Row>();
        colors.set_gap(8);
        colors.add<ColorEdit>(Color::from_hex(0xBF5C38));
        colors.add<ColorEdit>(Color::from_hex(0x3F6FBF));
        colors.add<ColorEdit>(Color::from_hex(0x46A758));
        colors.add<Label>("#BF5C38 #3F6FBF #46A758")
            .set_color(ctx.theme().text_dim);
    }, {.hover = {71.f, 196.f}, .click_at_hover = true, .frames = 80});

    // ---- views: list, tabs, dropdown (popup open) ------------------------
    capture(dir, "views", 560, 316,
            [](Context&, Column& root) {
        auto& row = root.add<Row>();
        row.set_gap(24);
        auto& list = row.add<ListView>();
        list.set_items({"alpha", "beta", "gamma", "delta"}).set_selected(1);
        auto& col = row.add<Column>();
        col.set_gap(10);
        col.add<Tabs>().set_tabs({"Home", "Edits", "View"}).set_selected(1);
        col.add<Dropdown>().set_items({"Linear", "Nearest", "Cubic", "Lanczos"})
            .set_placeholder("Filter…").set_selected(0);
    }, {.hover = {200.f, 84.f}, .click_at_hover = true, .frames = 80});

    // ---- layout: panel / row / spacer / divider ---------------------------
    capture(dir, "layout", 560, 208, [](Context&, Column& root) {
        auto& panel = root.add<Panel>();
        panel.set_padding(14).set_gap(10);
        panel.set_radius(10);
        panel.add<Label>("Panel — a bordered container");
        auto& top = panel.add<Row>();
        top.add<Label>("left");
        top.add<Spacer>(); // takes all remaining space
        top.add<Label>("right");
        panel.add<Divider>();
        auto& bot = panel.add<Row>();
        bot.set_gap(16);
        bot.add<Checkbox>("Snap", true);
        bot.add<Checkbox>("Grid", false);
    });

    // ---- containers: expander / splitter ----------------------------------
    capture(dir, "containers", 560, 240, [](Context& ctx, Column& root) {
        auto& exp = root.add<Expander>("More options");
        exp.set_open(true);
        auto& inner = exp.add<Column>();
        inner.set_gap(8);
        inner.add<Checkbox>("Compress", true);
        inner.add<Checkbox>("Overwrite", false);
        auto& split = root.add<Splitter>();
        split.set_fraction(0.45f);
        auto& l = split.add<Panel>();
        l.set_padding(12);
        l.set_radius(8);
        l.add<Label>("left pane").set_color(ctx.theme().accent);
        auto& r = split.add<Panel>();
        r.set_padding(12);
        r.set_radius(8);
        r.add<Label>("right pane").set_color(ctx.theme().warning);
    });

    // ---- image --------------------------------------------------------------
    static const std::vector<std::uint8_t> grad = gradient_rgba();
    capture(dir, "image", 560, 226, [](Context& ctx, Column& root) {
        auto& row = root.add<Row>();
        row.set_gap(16);
        const char* names[3] = {"Stretch", "Contain", "Cover"};
        Image::Fit fits[3] = {Image::Fit::Stretch, Image::Fit::Contain,
                              Image::Fit::Cover};
        for (int i = 0; i < 3; ++i) {
            auto& cell = row.add<Column>();
            cell.set_gap(6).set_cross(CrossMode::Center);
            cell.add<Image>("gradient").set_pixels(64, 48, grad.data())
                .set_size(150, 110).set_fit(fits[i]);
            cell.add<Label>(names[i]).set_color(ctx.theme().text_dim);
        }
    });

    // ---- tooltip -------------------------------------------------------------
    capture(dir, "tooltip", 560, 112, [](Context&, Column& root) {
        auto& row = root.add<Row>();
        row.set_gap(10);
        row.add<Button>("Save");
        row.add<Button>("Delete all")
            .set_tooltip("Removes every item — this cannot be undone");
        // cursor rests on "Delete all": 24 + Save(~76) + gap(10) + half(~62)
    }, {.hover = {168.f, 40.f}, .frames = 80});

    // ---- hello: the getting-started tree --------------------------------
    capture(dir, "hello", 320, 200, [](Context&, Column& root) {
        root.set_padding(16).set_gap(8);
        root.add<Label>("Hello, slikui!").set_font_size(26);
        root.add<Button>("Save").set_variant(Button::Variant::Primary);
    }, {.frames = 30});

    // ---- accordion --------------------------------------------------------
    capture(dir, "accordion", 480, 250, [](Context&, Column& root) {
        auto& acc = root.add<Accordion>();
        acc.set_gap(6);
        auto& shape = acc.add_section("Shape", /*open=*/true);
        shape.set_gap(8);
        shape.add<Dropdown>().set_items({"Cube", "Sphere", "Torus"})
            .set_selected(0);
        auto& material = acc.add_section("Material");
        material.set_gap(8);
        material.add<Slider>(0.f, 1.f);           // roughness
        material.add<Slider>(0.f, 1.f);           // metallic
    });

    // ---- scroll area -------------------------------------------------------
    capture(dir, "scroll", 480, 240, [](Context&, Column& root) {
        auto& scroll = root.add<ScrollArea>();
        for (int i = 1; i <= 12; ++i)
            scroll.add<Label>("Item " + std::to_string(i) +
                              " — laid out, painted and clipped on the CPU");
    });

    // ---- file browser (a Dialog on the shell layer, over real directories) -
    capture(dir, "browser", 520, 380, [](Context& ctx, Column& root) {
        root.add<Label>("Pick a scene file…");
        auto fb = std::make_unique<FileBrowser>(FileBrowser::Mode::Open);
        fb->set_path("examples");
        ctx.open_dialog(std::move(fb));
    });

    // ---- context menu: set_context_menu + opened as a borrowed popup -------
    capture(dir, "contextmenu", 520, 300, [](Context& ctx, Column& root) {
        auto& bar = root.add<MenuBar>();
        auto& view = bar.add_menu("View");
        view.add_item("Zoom In", [] {}, "Ctrl+=");
        view.add_item("Zoom Out", [] {}, "Ctrl+-");
        view.add_separator();
        view.add_checkable("Grid", [] { return true; }, [] {});
        auto& viewport = root.add<Panel>();
        viewport.set_padding(12);
        viewport.add<Label>("Right-click the viewport for the View menu");
        viewport.set_context_menu(&view);
        ctx.open_context_menu(view, {140.f, 150.f});
    });

    // ---- shells, one widget per capture -----------------------------------
    capture(dir, "dialog", 520, 320, [](Context& ctx, Column& root) {
        root.add<Label>("Scene hierarchy");
        auto& panel = root.add<Panel>();
        panel.set_padding(12).set_gap(8);
        panel.add<Checkbox>("Show grid", true);
        panel.add<Slider>(0.f, 1.f);
        auto mb = std::make_unique<MessageBox>("Delete", "Remove 'dragon.obj'?");
        mb->set_modal(true);
        mb->add_button("Delete", nullptr);
        mb->add_button("Cancel", nullptr);
        ctx.open_dialog(std::move(mb));
    });

    capture(dir, "toast", 480, 220, [](Context& ctx, Column& root) {
        root.add<Label>("Rendering finished.");
        auto toast = std::make_unique<Toast>("Screenshot saved");
        toast->set_icon(Icon::Camera);
        ctx.push_toast(std::move(toast), 60.0);
    });

    capture(dir, "groupbox", 480, 200, [](Context&, Column& root) {
        auto& gb = root.add<GroupBox>("Light");
        gb.add<Checkbox>("Enabled", true);
        gb.add<Slider>(0.f, 1.f);
    });

    capture(dir, "searchbox", 480, 140, [](Context&, Column& root) {
        root.add<SearchBox>().set_placeholder("Search assets…");
    });

    capture(dir, "logview", 560, 260, [](Context&, Column& root) {
        auto& log = root.add<LogView>();
        log.log(LogView::Level::Info, "loaded scene.dragon (24 ms)");
        log.log(LogView::Level::Warning, "missing texture: bark_d");
        log.log(LogView::Level::Error, "shader compile failed");
        log.log(LogView::Level::Success, "thumbnail bake complete");
    });

    capture(dir, "statusbar", 480, 180, [](Context&, Column& root) {
        root.add<Label>("Workspace");
        root.add<StatusBar>().set_text("60 fps · 1,440 objects");
    });

    capture(dir, "imageviews", 560, 220, [](Context&, Column& root) {
        auto grad = gradient_rgba();
        std::vector<std::uint8_t> grad2 = grad;
        for (std::size_t i = 0; i < grad2.size(); i += 4)
            std::swap(grad2[i], grad2[i + 2]);
        auto& views = root.add<Row>();
        views.set_gap(10);
        views.add<ImageView>().set_pixels(64, 48, grad.data());
        auto& cv = views.add<CompareView>();
        cv.set_pixels_a(64, 48, grad.data());
        cv.set_pixels_b(64, 48, grad2.data());
        cv.set_fraction(.62f);
    });

    // ---- custom widget: the C++ twin of python/examples/custom_widget.py ---
    capture(dir, "custom_cpp", 340, 150, [](Context& ctx, Column& root) {
        root.set_padding(16).set_gap(8);
        root.add<Label>("Custom widget").set_font_size(26);
        root.add<Label>("Meter is a C++ class subclassing slikui::Widget")
            .set_color(ctx.theme().text_dim);
        root.add<Meter>(0.4f);
        root.add<Label>("Click the bar to scrub it")
            .set_color(ctx.theme().text_dim);
    });

    // ---- custom theme: the preset card with a two-line override ------------
    {
        Theme t = theme_presets().front().make();
        t.accent = Color::from_hex(0xE060B0);
        t.radius = 4.f;
        auto card = render_section(
            310, 150,
            [](Context&, Column& root) {
                root.add<Label>("Your brand").set_font_size(16);
                auto& r1 = root.add<Row>();
                r1.set_gap(8);
                r1.add<Button>("Primary").set_variant(Button::Variant::Primary);
                r1.add<Button>("Ghost").set_variant(Button::Variant::Ghost);
                auto& r2 = root.add<Row>();
                r2.set_gap(12);
                r2.add<Checkbox>("Snap", true);
                r2.add<Radio>("Wi-Fi", true);
                r2.add<Switch>().set_checked(true);
            },
            {}, false, &t, 14.f, 10.f);
        dump_buffer((std::string(dir) + "/themecustom.ppm").c_str(), card);
    }

    // ---- theme presets: one card per built-in palette -------------------
    {
        std::vector<Buffer> cards;
        for (const auto& p : theme_presets()) {
            Theme preset = p.make();
            cards.push_back(render_section(
                310, 150,
                [&p](Context&, Column& root) {
                    root.add<Label>(p.name).set_font_size(16);
                    auto& r1 = root.add<Row>();
                    r1.set_gap(8);
                    r1.add<Button>("Primary").set_variant(Button::Variant::Primary);
                    r1.add<Button>("Ghost").set_variant(Button::Variant::Ghost);
                    auto& r2 = root.add<Row>();
                    r2.set_gap(12);
                    r2.add<Checkbox>("Snap", true);
                    r2.add<Radio>("Wi-Fi", true);
                    r2.add<Switch>().set_checked(true);
                },
                {}, false, &preset, 14.f, 10.f));
        }
        std::vector<Buffer> rows;
        for (std::size_t i = 0; i < cards.size(); i += 4)
            rows.push_back(join({cards.begin() + long(i),
                                 cards.begin() + long(std::min(i + 4, cards.size()))},
                                20, false));
        dump_buffer((std::string(dir) + "/themes.ppm").c_str(),
                    join(rows, 20, true));
    }

    return 0;
}
