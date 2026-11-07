// tools/snapshot.cpp — headless render of the full widget set to a PPM.
//
//   clang++ -std=c++20 -Iinclude tools/snapshot.cpp -o /tmp/snapshot
//   /tmp/snapshot /tmp/snapshot.ppm
//
// No window, no GLFW: pure software rasterizer.
#include <silk/backends/software/software.hpp>
#include <silk/silk.hpp>
#include <silk/text/system_fonts.hpp>

#include <cstdio>
#include <vector>

static void write_ppm(const char* path, const silk::software::Target& t) {
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", t.width(), t.height());
    for (int y = 0; y < t.height(); ++y)
        for (int x = 0; x < t.width(); ++x) {
            silk::Color c = silk::Color::from_abgr(t.at(x, y));
            std::uint8_t rgb[3] = {c.r, c.g, c.b};
            std::fwrite(rgb, 1, 3, f);
        }
    std::fclose(f);
}

int main(int argc, char** argv) {
    float scale = argc > 2 ? (float)atof(argv[2]) : 1.f;
    constexpr int LW = 560, LH = 860;              // logical px
    const int W = int(LW * scale), H = int(LH * scale); // framebuffer px
    silk::software::Target target(W, H);
    silk::software::Sink sink(target);
    silk::Context ctx(sink);
    ctx.set_frame_size({float(LW), float(LH)}); // LOGICAL frame: layout in points
    ctx.theme().scale = scale;
    sink.set_scale(scale);
    ctx.load_font_file(silk::find_system_font());

    auto& root = ctx.set_root<silk::Column>();
    root.set_padding(16).set_gap(10);

    // Header.
    auto& header = root.add<silk::Row>().set_gap(8);
    header.add<silk::Label>("silk snapshot").set_font_size(24);
    header.add<silk::Spacer>();
    header.add<silk::Button>("Ghost")
        .set_variant(silk::Button::Variant::Ghost)
        .on_click([] {});
    root.add<silk::Divider>();

    // Buttons.
    root.add<silk::Label>("Buttons");
    auto& row1 = root.add<silk::Row>().set_gap(8);
    row1.add<silk::Button>("Default").on_click([] {});
    row1.add<silk::Button>("Primary")
        .set_variant(silk::Button::Variant::Primary)
        .on_click([] {});
    row1.add<silk::Button>("Danger")
        .set_variant(silk::Button::Variant::Danger)
        .on_click([] {});

    // Toggles.
    root.add<silk::Checkbox>("checked", true);
    root.add<silk::Checkbox>("unchecked", false);
    root.add<silk::Switch>().set_checked(false);
    auto& on = root.add<silk::Switch>();
    on.set_checked(true);
    root.add<silk::Row>().set_gap(8).add<silk::Label>("switch off / on");

    // Value widgets.
    auto& slider = root.add<silk::Slider>(0.f, 100.f);
    slider.set_value(40.f);
    root.add<silk::ProgressBar>().set_value(0.7f);
    root.add<silk::Spinner>().set_speed(120.f);

    // Text.
    root.add<silk::TextField>().set_placeholder("type here");
    root.add<silk::Tabs>().set_tabs({"First", "Second", "Third"});

    // Panel + list + dropdown.
    auto& panel = root.add<silk::Panel>();
    panel.set_padding(10);
    panel.set_background(silk::Color::from_hex(0x223344));
    panel.set_border(silk::Color::from_hex(0x8899AA));
    panel.draw_border_ = true;
    panel.add<silk::Label>("inside a panel");

    root.add<silk::ListView>().set_items({"alpha", "beta", "gamma"});
    root.add<silk::Dropdown>()
        .set_items({"one", "two", "three"})
        .set_placeholder("choose...");

    // Simulated frames so spinner/anim states advance.
    for (int i = 1; i <= 30; ++i)
        ctx.tick(silk::Time{0.016 * i, 0.016});
    ctx.render();

    write_ppm(argc > 1 ? argv[1] : "snapshot.ppm", target);
    std::printf("rendered\n");
    return 0;
}
