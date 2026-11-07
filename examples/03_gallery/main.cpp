// examples/03_gallery/main.cpp — the full widget showcase.
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/gl3.h> // GL first: silenced deprecations, no legacy gl.h

#include <chrono>
#include <vector>
#include <silk/backends/glfw/glfw_window.hpp>
#include <silk/backends/opengl/opengl.hpp>
#include <silk/silk.hpp>
#include <silk/text/system_fonts.hpp>

int main() {
    silk::glfw::GlfwWindow win(900, 640, "silk widget gallery");
    win.set_min_size(480, 360);
    if (!win.ok()) return 1;
    silk::opengl::OpenglSink sink;
    if (!sink.init()) return 1;

    silk::Context ctx(sink);
    ctx.load_font_file(silk::find_system_font());

    // ----- state -----
    float slider_value = 0.35f;
    bool checked = true, switched = false;
    int radio_pick = 1;
    std::string typed = "";

    auto& root = ctx.set_root<silk::Column>();
    root.set_padding(16).set_gap(8);

    // Header with theme toggle.
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

    // Controls section.
    auto& controls = root.add<silk::Column>().set_gap(6);
    auto& btn_row = controls.add<silk::Row>().set_gap(8);
    btn_row.add<silk::Button>("Default").on_click([] {});
    btn_row.add<silk::Button>("Primary")
        .set_variant(silk::Button::Variant::Primary)
        .on_click([] {});
    btn_row.add<silk::Button>("Danger")
        .set_variant(silk::Button::Variant::Danger)
        .on_click([] {});

    auto& sel_row = controls.add<silk::Row>().set_gap(16);
    sel_row.add<silk::Checkbox>("a checkbox", checked)
        .on_change([&](bool v) { checked = v; })
        .set_tooltip("Click to toggle");
    for (int i = 0; i < 3; ++i) {
        auto& r = sel_row.add<silk::Radio>("pick " + std::to_string(i + 1), i == radio_pick);
        r.on_change([&sel_row, i](bool on) {
            if (!on) return;
            for (size_t k = 1; k < sel_row.children.size(); ++k)
                static_cast<silk::Radio*>(sel_row.children[k].get())
                    ->set_checked(long(k) - 1 == i);
        });
    }
    auto& switch_row = controls.add<silk::Row>().set_gap(16);
    switch_row.add<silk::Switch>().on_change([&](bool v) { switched = v; });
    auto& sw_on = switch_row.add<silk::Switch>();
    sw_on.set_checked(true);

    auto& slider = controls.add<silk::Slider>(0.f, 1.f);
    slider.set_value(slider_value);
    slider.on_change([&](float v) { slider_value = v; });
    auto& progress = controls.add<silk::ProgressBar>();
    controls.add<silk::Spinner>();

    // Text & views section.
    root.add<silk::Divider>();
    root.add<silk::Label>("Text & views");
    auto& field = root.add<silk::TextField>().set_placeholder("type here");
    field.on_change([&](std::string_view s) { typed = std::string(s); });
    root.add<silk::TextArea>().set_placeholder("multiline...").set_rows(3);
    root.add<silk::Tabs>().set_tabs({"first", "second", "third"});
    auto& list = root.add<silk::ListView>();
    list.set_items({"alpha", "beta", "gamma", "delta"});
    root.add<silk::Dropdown>()
        .set_items({"one", "two", "three"})
        .set_placeholder("choose...");

    // Expander — collapsible section.
    auto& more = root.add<silk::Expander>("more options");
    more.add<silk::Label>("hidden content lives here");
    more.add<silk::Checkbox>("nested checkbox", checked);

    // Splitter — drag the divider.
    auto& split = root.add<silk::Splitter>();
    split.add<silk::Label>("left pane").set_color(silk::Color::from_hex(0x77C7FF));
    split.add<silk::Label>("right pane").set_color(silk::Color::from_hex(0xFFD377));

    // Image — procedural gradient (swap for Image::set_file with real assets).
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
    scroll.hint_h_ = 120;
    for (int i = 0; i < 12; ++i)
        scroll.add<silk::Label>("scrollable row " + std::to_string(i));

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

        progress.set_value(slider_value);

        ctx.tick(silk::Time{now, silk::clamp_dt(dt)});
        ctx.render();
        win.swap();
    }
    return 0;
}
