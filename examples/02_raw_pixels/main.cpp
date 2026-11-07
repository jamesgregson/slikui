// examples/02_raw_pixels/main.cpp — headless rendering to raw pixels.
//
// No window, no GPU: the UI is rasterized into a plain uint32_t buffer and
// written out as a PPM image. This is the "raw pixel setting" path — the
// same one an embedded framebuffer or software blitter would use.
#include <slikui/backends/software/software.hpp>
#include <slikui/slikui.hpp>
#include <slikui/text/system_fonts.hpp>

#include <cstdio>
#include <vector>

int main() {
    constexpr int W = 480, H = 320;
    std::vector<std::uint32_t> pixels(std::size_t(W) * H);

    // Borrowed storage: slikui renders into *your* buffer.
    slikui::software::Target target(pixels.data(), W, H);
    slikui::software::Sink sink(target);

    slikui::Context ctx(sink);
    ctx.set_frame_size({W, H});
    ctx.load_font_file(slikui::find_system_font());

    auto& root = ctx.set_root<slikui::Column>();
    root.set_padding(20).set_gap(10);
    root.add<slikui::Label>("Raw pixel output").set_font_size(20);
    root.add<slikui::Label>("480x320, drawn entirely on the CPU.");
    auto& panel = root.add<slikui::Panel>();
    panel.set_padding(10);
    panel.set_background(slikui::Color::from_hex(0x223344));
    panel.add<slikui::Label>("a panel inside the flow");
    auto& bar = root.add<slikui::ProgressBar>().set_value(0.7f);

    ctx.tick(slikui::Time{0.016, 0.016});
    ctx.render();
    (void)bar;

    std::FILE* out = std::fopen("hello_raw.ppm", "wb");
    if (!out) return 1;
    std::fprintf(out, "P6\n%d %d\n255\n", W, H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            slikui::Color c = slikui::Color::from_abgr(target.at(x, y));
            std::uint8_t rgb[3] = {c.r, c.g, c.b};
            std::fwrite(rgb, 1, 3, out);
        }
    std::fclose(out);
    std::printf("wrote hello_raw.ppm\n");
    return 0;
}
