// examples/02_raw_pixels/main.cpp — headless rendering to raw pixels.
//
// No window, no GPU: the UI is rasterized into a plain uint32_t buffer and
// written out as a PPM image. This is the "raw pixel setting" path — the
// same one an embedded framebuffer or software blitter would use.
#include <silk/backends/software/software.hpp>
#include <silk/silk.hpp>
#include <silk/text/system_fonts.hpp>

#include <cstdio>
#include <vector>

int main() {
    constexpr int W = 480, H = 320;
    std::vector<std::uint32_t> pixels(std::size_t(W) * H);

    // Borrowed storage: silk renders into *your* buffer.
    silk::software::Target target(pixels.data(), W, H);
    silk::software::Sink sink(target);

    silk::Context ctx(sink);
    ctx.set_frame_size({W, H});
    ctx.load_font_file(silk::find_system_font());

    auto& root = ctx.set_root<silk::Column>();
    root.set_padding(20).set_gap(10);
    root.add<silk::Label>("Raw pixel output").set_font_size(20);
    root.add<silk::Label>("480x320, drawn entirely on the CPU.");
    auto& panel = root.add<silk::Panel>();
    panel.set_padding(10);
    panel.set_background(silk::Color::from_hex(0x223344));
    panel.add<silk::Label>("a panel inside the flow");
    auto& bar = root.add<silk::ProgressBar>().set_value(0.7f);

    ctx.tick(silk::Time{0.016, 0.016});
    ctx.render();
    (void)bar;

    std::FILE* out = std::fopen("hello_raw.ppm", "wb");
    if (!out) return 1;
    std::fprintf(out, "P6\n%d %d\n255\n", W, H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            silk::Color c = silk::Color::from_abgr(target.at(x, y));
            std::uint8_t rgb[3] = {c.r, c.g, c.b};
            std::fwrite(rgb, 1, 3, out);
        }
    std::fclose(out);
    std::printf("wrote hello_raw.ppm\n");
    return 0;
}
