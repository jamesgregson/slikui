// tests/gl_parity.cpp — OpenglSink vs software::Sink on the same frames.
//
// Renders into an offscreen FBO through a hidden GLFW window and compares
// against the CPU rasterizer. GPU float blending/sampling is not bit-exact
// with the software sink's integer math, so the check is a per-channel
// tolerance of 2/255 per channel. Exits 0 with "SKIP" when no GL context can be created
// (headless CI).
//
//   slikui_gl_parity [--dump DIR]   # DIR: write gl/sw/diff PPMs for a look
#include "slikui/backends/opengl/opengl.hpp"
#include "slikui/backends/software/software.hpp"
#include "slikui/context.hpp"
#include "slikui/text/default_font.hpp"
#include "slikui/widgets/all.hpp"

// The GL context comes from GLFW, or from slikui's SdlWindow when built
// with SLIKUI_PARITY_SDL (ctest slikui_sdl_gl_parity) — same checks, so
// both backends' contexts are covered.
#ifdef SLIKUI_PARITY_SDL
#include "slikui/backends/sdl/sdl_window.hpp"
#else
#include <GLFW/glfw3.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace slikui;

namespace {

constexpr int kEdgeError = 2;          // "matches" threshold, per channel

std::vector<std::uint8_t> checker(int w, int h) {
    std::vector<std::uint8_t> px(std::size_t(w) * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            std::uint8_t* p = &px[(std::size_t(y) * w + x) * 4];
            bool on = ((x / 4) + (y / 4)) % 2;
            p[0] = on ? 230 : 30;
            p[1] = std::uint8_t(x * 255 / std::max(1, w - 1));
            p[2] = std::uint8_t(y * 255 / std::max(1, h - 1));
            p[3] = (x + y) % 3 ? 255 : 128;
        }
    return px;
}

void build(Context& ctx) {
    static const std::vector<std::uint8_t> img = checker(24, 16);
    auto& root = ctx.set_root<Column>();
    root.set_padding(10).set_gap(6);
    root.add<Label>("GL parity — glyphs, shapes, clips");
    auto& row = root.add<Row>();
    row.set_gap(4);
    row.add<Button>("Primary").set_variant(Button::Variant::Primary);
    row.add<Button>("Cancel");
    root.add<Checkbox>("Option", true);
    root.add<Slider>(0.f, 1.f).set_value(0.4f);
    root.add<ProgressBar>().set_value(0.65f);
    auto& scroll = root.add<ScrollArea>();
    for (int i = 0; i < 12; ++i) scroll.add<Label>("scrolled line " + std::to_string(i));
    root.add<Divider>();
    root.add<Image>().set_pixels(24, 16, img.data()).set_size(48, 32);
}

void frame(Context& ctx) {
    ctx.tick(Time{0.5, 0.016});
    ctx.render();
}

struct Result {
    int max_err = 0;
    std::size_t over = 0, total = 0;
};

Result compare(const std::uint8_t* a, const std::uint8_t* b, std::size_t n,
               std::vector<std::uint8_t>* diff) {
    Result r;
    r.total = n;
    if (diff) diff->assign(n * 3, 0);
    for (std::size_t i = 0; i < n; ++i) {
        int e = 0;
        for (int c = 0; c < 4; ++c) e = std::max(e, std::abs(int(a[i * 4 + c]) - int(b[i * 4 + c])));
        r.max_err = std::max(r.max_err, e);
        if (e > kEdgeError) ++r.over;
        if (diff) (*diff)[i * 3] = std::uint8_t(std::min(255, e * 16));
    }
    return r;
}

void write_ppm(const std::string& path, const std::uint8_t* px, int w, int h, int ch) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; ++i) std::fwrite(px + std::size_t(i) * ch, 1, 3, f);
    std::fclose(f);
}

// A scene draws one frame into whichever sink it is handed.
using Scene = void (*)(IRenderSink& sink, Vec2 frame);

// Widgets through a real Context (text, layout, clips, an Image upload).
void widgets_scene(IRenderSink& sink, Vec2 fr) {
    Context ctx(sink);
    ctx.set_frame_size(fr);
    ctx.load_font_memory(text::default_font_data(), text::default_font_size());
    build(ctx);
    frame(ctx);
    frame(ctx);
    ctx.clear_tree();
}

// Shapes only, straight into the sink: fractional SDF geometry, radii,
// borders, angled lines, nested clips, an Rgba8 image. No glyphs.
//
// Kept off two software-sink quirks where GL is the correct one (they
// would dominate the diff): (1) radius-0 fills at fractional positions are
// hard-edged from int(x), with no per-pixel clip test; (2) a rounded rect is
// intersected with the clip BEFORE its SDF, so clipping re-rounds it. Here:
// radius-0 fills and clips sit on whole pixels, clipped rounded rects keep
// their corners inside the clip, and lines stay off the clip's right/bottom
// edge (the software line grid paints one pixel past it).
void shapes_scene(IRenderSink& sink, Vec2 fr) {
    static const std::vector<std::uint8_t> img = checker(16, 12);
    sink.upload(TexUpload{42, 16, 12, TexFormat::Rgba8, img.data()});
    DrawList l;
    l.rect({0, 0, fr.x, fr.y}, Color{18, 20, 24});
    l.rect({10.3f, 8.6f, 60.4f, 30.2f}, Color{70, 130, 230}, 6.f);
    l.rect({80, 8, 40, 30}, Color{230, 90, 60, 160}, 0.f);
    l.hollow_rect({130.25f, 8.75f, 50, 30}, Color{240, 240, 240}, 2.f, 8.f);
    l.hollow_rect({190, 8, 50, 30}, Color{120, 220, 120}, 1.f, 0.f);
    l.circle({30.5f, 70.25f}, 14.3f, Color{250, 200, 60});
    l.circle({70, 70}, 9.f, Color{250, 200, 60, 128});
    for (int i = 0; i < 8; ++i) {
        float a = float(i) * 0.39f;
        l.line({150, 75}, {150 + 45 * std::cos(a), 75 + 45 * std::sin(a)},
               Color{200, 120, 255}, 0.75f + 0.5f * float(i));
    }
    l.line({20, 110}, {20, 110}, Color{255, 255, 255}, 4.f); // dot
    l.push_clip({10, 100, 90, 40});
    l.rect({0, 95, 200, 30}, Color{60, 200, 200, 200}, 0.f);   // square: cut by clip
    l.rect({20.4f, 112.3f, 50, 20}, Color{250, 250, 120, 180}, 9.f); // inside clip
    l.push_clip({40, 0, 200, 300}); // nested: intersects to x >= 40
    l.line({0, 140}, {95, 110}, Color{255, 80, 120}, 3.f);
    l.pop_clip();
    l.pop_clip();
    l.image({120, 110, 64, 48}, 42, {0, 0, 16, 12});
    // Fractional placement exercises the pixel-center rule. (Kept off exact
    // texel boundaries, where nearest sampling of GPU-interpolated UVs vs
    // the CPU's exact ones is a coin toss — e.g. y = 110.5 at 2x.)
    l.image({190.3f, 110.3f, 16, 12}, 42, {0, 0, 16, 12});
    sink.render(l, fr);
}

struct Spec {
    const char* name;
    Scene scene;
    int lw, lh;
    int max_err;     // hard ceiling, any pixel, any channel
    double budget;   // fraction of pixels allowed above kEdgeError
};

bool run(const Spec& spec, float scale, const char* dump) {
    const int w = int(float(spec.lw) * scale), h = int(float(spec.lh) * scale);
    const Vec2 fr{float(spec.lw), float(spec.lh)};

    // CPU reference.
    software::Target target(w, h);
    software::Sink sw(target);
    sw.set_scale(scale);
    spec.scene(sw, fr);
    std::vector<std::uint8_t> ref(std::size_t(w) * h * 4);
    std::memcpy(ref.data(), target.data(), ref.size());

    // GPU, offscreen.
    GLuint fbo = 0, color = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(1, &color);
    glBindTexture(GL_TEXTURE_2D, color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::printf("FAIL framebuffer incomplete\n");
        return false;
    }
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    std::vector<std::uint8_t> got(std::size_t(w) * h * 4);
    {
        opengl::OpenglSink gl;
        if (!gl.init()) {
            std::printf("FAIL OpenglSink::init (shader compile/link)\n");
            return false;
        }
        gl.set_scale(scale);
        spec.scene(gl, fr);
        glFinish();
        std::vector<std::uint8_t> rows(got.size());
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rows.data());
        for (int y = 0; y < h; ++y) // GL rows are bottom-up
            std::memcpy(&got[std::size_t(y) * w * 4], &rows[std::size_t(h - 1 - y) * w * 4],
                        std::size_t(w) * 4);
        std::printf("  %s x%.0f: %zu instances, %zu batches\n", spec.name, double(scale),
                    gl.batcher().instances().size(), gl.batcher().batches().size());
        gl.shutdown();
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteTextures(1, &color);
    glDeleteFramebuffers(1, &fbo);

    std::vector<std::uint8_t> diff;
    Result r = compare(got.data(), ref.data(), std::size_t(w) * h, dump ? &diff : nullptr);
    double frac = double(r.over) / double(r.total);
    bool ok = r.max_err <= spec.max_err && frac <= spec.budget;
    std::printf("%s %s x%.0f: max channel error %d (limit %d), %zu/%zu pixels (%.3f%%, "
                "limit %.3f%%) over %d\n",
                ok ? " ok " : "FAIL", spec.name, double(scale), r.max_err, spec.max_err, r.over,
                r.total, frac * 100, spec.budget * 100, kEdgeError);
    if (dump) {
        std::string base = std::string(dump) + "/gl_parity_" + spec.name + "_x" +
                           std::to_string(int(scale));
        write_ppm(base + "_gl.ppm", got.data(), w, h, 4);
        write_ppm(base + "_sw.ppm", ref.data(), w, h, 4);
        write_ppm(base + "_diff.ppm", diff.data(), w, h, 3);
    }
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    const char* dump = nullptr;
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], "--dump") == 0) dump = argv[i + 1];

#ifdef SLIKUI_PARITY_SDL
    sdl::SdlWindow win(64, 64, "slikui gl parity (SDL)", SDL_WINDOW_HIDDEN);
    if (!win.ok()) {
        std::printf("SKIP no SDL GL 3.3 core context: %s\n", SDL_GetError());
        SDL_Quit();
        return 0;
    }
#else
    if (!glfwInit()) {
        std::printf("SKIP no display / glfwInit failed\n");
        return 0;
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    GLFWwindow* win = glfwCreateWindow(64, 64, "slikui gl parity", nullptr, nullptr);
    if (!win) {
        std::printf("SKIP no GL 3.3 core context\n");
        glfwTerminate();
        return 0;
    }
    glfwMakeContextCurrent(win);
#endif
    std::printf("GL: %s\n", (const char*)glGetString(GL_RENDERER));

    const Spec specs[] = {
        // GL's own math must match the CPU rasterizer to 2/255, text and
        // images included (both rasterize texel quads by pixel center).
        {"shapes", shapes_scene, 260, 170, kEdgeError, 0.0},
        {"widgets", widgets_scene, 260, 360, kEdgeError, 0.0},
    };
    bool ok = true;
    for (const Spec& spec : specs)
        for (float scale : {1.f, 2.f}) ok = run(spec, scale, dump) && ok;

#ifdef SLIKUI_PARITY_SDL
    // SdlWindow cleans up its window + context; SDL_Quit is the app's.
#else
    glfwDestroyWindow(win);
    glfwTerminate();
#endif
    return ok ? 0 : 1;
}
