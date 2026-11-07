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
