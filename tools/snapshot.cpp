// tools/snapshot.cpp — headless render of the full widget set to a PPM.
//
//   clang++ -std=c++20 -Iinclude tools/snapshot.cpp -o /tmp/snapshot
//   /tmp/snapshot /tmp/snapshot.ppm
//
// No window, no GLFW: pure software rasterizer.
#include <slikui/backends/software/software.hpp>
#include <slikui/slikui.hpp>
#include <slikui/text/system_fonts.hpp>

#include <cstdio>
#include <vector>

static void write_ppm(const char* path, const slikui::software::Target& t) {
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", t.width(), t.height());
    for (int y = 0; y < t.height(); ++y)
        for (int x = 0; x < t.width(); ++x) {
            slikui::Color c = slikui::Color::from_abgr(t.at(x, y));
            std::uint8_t rgb[3] = {c.r, c.g, c.b};
            std::fwrite(rgb, 1, 3, f);
        }
    std::fclose(f);
}

int main(int argc, char** argv) {
    float scale = argc > 2 ? (float)atof(argv[2]) : 1.f;
    constexpr int LW = 560, LH = 860;              // logical px
    const int W = int(LW * scale), H = int(LH * scale); // framebuffer px
    slikui::software::Target target(W, H);
    slikui::software::Sink sink(target);
    slikui::Context ctx(sink);
    ctx.set_frame_size({float(LW), float(LH)}); // LOGICAL frame: layout in points
    ctx.theme().scale = scale;
    sink.set_scale(scale);
    ctx.load_font_file(slikui::find_system_font());

    auto& root = ctx.set_root<slikui::Column>();
    root.set_padding(16).set_gap(10);

    // Header.
    auto& header = root.add<slikui::Row>().set_gap(8);
    header.add<slikui::Label>("slikui snapshot").set_font_size(24);
    header.add<slikui::Spacer>();
    header.add<slikui::Button>("Ghost")
        .set_variant(slikui::Button::Variant::Ghost)
        .on_click([] {});
    root.add<slikui::Divider>();

    // Buttons.
    root.add<slikui::Label>("Buttons");
    auto& row1 = root.add<slikui::Row>().set_gap(8);
    row1.add<slikui::Button>("Default").on_click([] {});
    row1.add<slikui::Button>("Primary")
        .set_variant(slikui::Button::Variant::Primary)
        .on_click([] {});
    row1.add<slikui::Button>("Danger")
        .set_variant(slikui::Button::Variant::Danger)
        .on_click([] {});

    // Toggles.
    root.add<slikui::Checkbox>("checked", true);
    root.add<slikui::Checkbox>("unchecked", false);
    root.add<slikui::Switch>().set_checked(false);
    auto& on = root.add<slikui::Switch>();
    on.set_checked(true);
    root.add<slikui::Row>().set_gap(8).add<slikui::Label>("switch off / on");

    // Value widgets.
    auto& slider = root.add<slikui::Slider>(0.f, 100.f);
    slider.set_value(40.f);
    root.add<slikui::ProgressBar>().set_value(0.7f);
    root.add<slikui::Spinner>().set_speed(120.f);

    // Text.
    root.add<slikui::TextField>().set_placeholder("type here");
    root.add<slikui::Tabs>().set_tabs({"First", "Second", "Third"});

    // Panel + list + dropdown.
    auto& panel = root.add<slikui::Panel>();
    panel.set_padding(10);
    panel.set_background(slikui::Color::from_hex(0x223344));
    panel.set_border(slikui::Color::from_hex(0x8899AA));
    panel.draw_border_ = true;
    panel.add<slikui::Label>("inside a panel");

    root.add<slikui::ListView>().set_items({"alpha", "beta", "gamma"});
    root.add<slikui::Dropdown>()
        .set_items({"one", "two", "three"})
        .set_placeholder("choose...");

    // Simulated frames so spinner/anim states advance.
    for (int i = 1; i <= 30; ++i)
        ctx.tick(slikui::Time{0.016 * i, 0.016});
    ctx.render();

    write_ppm(argc > 1 ? argv[1] : "snapshot.ppm", target);
    std::printf("rendered\n");
    return 0;
}
