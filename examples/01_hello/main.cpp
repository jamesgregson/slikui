// examples/01_hello/main.cpp — minimal windowed app: GLFW + system OpenGL.
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/gl3.h> // GL first: silenced deprecations, no legacy gl.h

#include <chrono>
#include <silk/backends/glfw/glfw_window.hpp>
#include <silk/backends/opengl/opengl.hpp>
#include <silk/silk.hpp>
#include <silk/text/system_fonts.hpp>

int main() {
    silk::glfw::GlfwWindow win(640, 420, "hello, silk");
    win.set_min_size(320, 240);
    if (!win.ok()) return 1;

    silk::opengl::OpenglSink sink;       // system OpenGL, no loader
    if (!sink.init()) return 1;

    silk::Context ctx(sink);
    ctx.load_font_file(silk::find_system_font());

    int clicks = 0;
    auto& root = ctx.set_root<silk::Column>();
    root.set_padding(24).set_gap(12);
    root.add<silk::Label>("Hello, silk!").set_font_size(28);
    root.add<silk::Label>("A header-only, retained-mode C++ UI library.");
    auto& status = root.add<silk::Label>("clicks: 0");
    root.add<silk::Button>("Click me").on_click([&] {
        status.set_text("clicks: " + std::to_string(++clicks));
    });

    auto last = std::chrono::steady_clock::now();
    double now = 0;
    while (!win.should_close()) {
        win.dispatch(ctx);
        ctx.set_frame_size(win.point_size()); // LOGICAL frame: layout in points
        ctx.theme().scale = win.ui_scale();
        sink.set_scale(win.ui_scale());

        auto now_pt = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now_pt - last).count();
        last = now_pt;
        now += dt;

        ctx.tick(silk::Time{now, silk::clamp_dt(dt)});
        ctx.render();
        win.swap();
    }
    return 0;
}
