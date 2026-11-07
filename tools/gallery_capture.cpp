// tools/gallery_capture.cpp — runs the full gallery through the real GL sink
// and reads the framebuffer back: a true screenshot without screen-recording
// permissions.
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/gl3.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <chrono>
#include <cstdio>
#include <vector>
#include <silk/backends/glfw/glfw_window.hpp>
#include <silk/backends/opengl/opengl.hpp>
#include <silk/silk.hpp>
#include <silk/text/system_fonts.hpp>

static void dump_ppm(const char* path, int w, int h) {
    std::vector<unsigned char> px(size_t(w) * h * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; --y) // GL origin is bottom-left
        for (int x = 0; x < w; ++x) {
            const unsigned char* p = &px[(size_t(y) * w + x) * 4];
            unsigned char rgb[3] = {p[0], p[1], p[2]};
            fwrite(rgb, 1, 3, f);
        }
    std::fclose(f);
}

int main(int argc, char** argv) {
    silk::glfw::GlfwWindow win(900, 640, "silk gallery");
    win.set_min_size(480, 360);
    if (!win.ok()) return 1;
    silk::opengl::OpenglSink sink;
    if (!sink.init()) return 1;

    silk::Context ctx(sink);
    ctx.load_font_file(silk::find_system_font());

    float slider_value = 0.35f;
    bool checked = true;
    int clicks = 0;
    int radio_pick = 1;

    auto& root = ctx.set_root<silk::Column>();
    root.set_padding(16).set_gap(8);

    auto& header = root.add<silk::Row>().set_gap(8);
    header.add<silk::Label>("silk gallery").set_font_size(22);
    header.add<silk::Spacer>();
    // Theme picker: applies the built-in presets (silk/style/theme.hpp).
    std::vector<std::string> theme_names;
    for (const auto& p : silk::theme_presets()) theme_names.push_back(p.name);
    header.add<silk::Dropdown>()
        .set_items(theme_names)
        .set_placeholder("Theme")
        .set_selected(0)
        .on_select([&](size_t i) { silk::apply_preset(ctx.theme(), i); })
        .set_tooltip("Pick a built-in palette");

    root.add<silk::Divider>();

    auto& controls = root.add<silk::Column>().set_gap(6);
    auto& btn_row = controls.add<silk::Row>().set_gap(8);
    btn_row.add<silk::Button>("Default").on_click([&] { clicks++; });
    btn_row.add<silk::Button>("Primary")
        .set_variant(silk::Button::Variant::Primary)
        .on_click([&] { clicks++; });
    btn_row.add<silk::Button>("Danger")
        .set_variant(silk::Button::Variant::Danger)
        .on_click([&] { clicks++; });

    auto& sel_row = controls.add<silk::Row>().set_gap(16);
    sel_row.add<silk::Checkbox>("a checkbox", checked)
        .on_change([&](bool v) { checked = v; })
        .set_tooltip("Click to toggle");
    for (int i = 0; i < 3; ++i) {
        auto& r = sel_row.add<silk::Radio>("pick " + std::to_string(i + 1), i == radio_pick);
        r.on_change([&sel_row, i](bool on) {
            if (!on) return;
            // Radio exclusivity: the picked one stays on, its group goes off.
            for (size_t k = 1; k < sel_row.children.size(); ++k)
                static_cast<silk::Radio*>(sel_row.children[k].get())
                    ->set_checked(long(k) - 1 == i);
        });
    }
    auto& switch_row = controls.add<silk::Row>().set_gap(16);
    switch_row.add<silk::Switch>();
    auto& sw_on = switch_row.add<silk::Switch>();
    sw_on.set_checked(true);

    auto& slider = controls.add<silk::Slider>(0.f, 1.f);
    slider.set_value(slider_value);
    auto& progress = controls.add<silk::ProgressBar>();
    progress.set_value(0.7f);
    controls.add<silk::Spinner>();

    root.add<silk::Divider>();
    root.add<silk::Label>("Text & views");
    root.add<silk::TextField>().set_placeholder("type here");
    root.add<silk::TextArea>().set_placeholder("multiline...").set_rows(2);
    root.add<silk::Tabs>().set_tabs({"First", "Second", "Third"});
    root.add<silk::ListView>().set_items({"alpha", "beta", "gamma", "delta"});
    root.add<silk::Dropdown>().set_items({"one", "two", "three"}).set_placeholder("choose...");

    // Expander — collapsible section.
    auto& more = root.add<silk::Expander>("more options");
    more.set_open(true);
    more.add<silk::Label>("hidden content lives here");
    more.add<silk::Checkbox>("nested checkbox", checked);

    // Splitter — drag the divider; panes resize live.
    auto& split = root.add<silk::Splitter>();
    split.add<silk::Label>("left pane").set_color(silk::Color::from_hex(0x77C7FF));
    split.add<silk::Label>("right pane").set_color(silk::Color::from_hex(0xFFD377));

    // Image — procedural RGBA gradient uploaded through the sink.
    auto& img_row = root.add<silk::Row>().set_gap(10);
    auto& img = img_row.add<silk::Image>("gradient");
    {
        const int w = 64, h = 48;
        std::vector<std::uint8_t> px(size_t(w) * h * 4);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                auto* p = &px[(size_t(y) * w + x) * 4];
                p[0] = std::uint8_t(x * 255 / (w - 1));
                p[1] = std::uint8_t(y * 255 / (h - 1));
                p[2] = 128;
                p[3] = 255;
            }
        img.set_pixels(w, h, px.data()).set_size(96, 72).set_fit(silk::Image::Fit::Contain);
    }
    img_row.add<silk::Label>("Image widget — Contain fit");

    auto& scroll = root.add<silk::ScrollArea>();
    scroll.hint_w_ = 868;
    scroll.hint_h_ = 60;
    for (int i = 0; i < 6; ++i)
        scroll.add<silk::Label>("scrollable row " + std::to_string(i));

    auto last = std::chrono::steady_clock::now();
    double now = 0;
    int frame = 0;

    while (!glfwWindowShouldClose(win.handle())) {
        win.dispatch(ctx);
        ctx.set_frame_size(win.point_size());
        ctx.theme().scale = win.ui_scale();
        sink.set_scale(win.ui_scale());

        auto pt = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(pt - last).count();
        last = pt;
        now += dt;

        ctx.tick(silk::Time{now, silk::clamp_dt(dt)});
        ctx.render();

        ++frame;
        // Dump BEFORE swap: after swapping, the back buffer's content is
        // undefined (macOS recycles/reallocates surfaces on resize) and reads
        // back structured garbage that looks like a rendering bug.
        if (frame == 30) {
            int w = 0, h = 0;
            glfwGetFramebufferSize(win.handle(), &w, &h);
            dump_ppm(argc > 1 ? argv[1] : "shots/gallery.ppm", w, h);
            std::printf("captured %dx%d px — resizing window now\n", w, h);
            glfwSetWindowSize(win.handle(), 600, 440); // exercise the resize path
        }
        if (frame == 90) {
            int w = 0, h = 0;
            glfwGetFramebufferSize(win.handle(), &w, &h);
            dump_ppm("shots/gallery_small.ppm", w, h);
            std::printf("captured after resize: %dx%d px\n", w, h);
        }
        if (frame == 150) {
            int w = 0, h = 0;
            glfwGetFramebufferSize(win.handle(), &w, &h);
            dump_ppm("shots/gallery_settled.ppm", w, h);
            std::printf("captured settled: %dx%d px\n", w, h);
            return 0;
        }
        win.swap();
    }
    return 0;
}
