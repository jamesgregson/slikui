// examples/06_sdl_opengl/main.cpp - SDL3 + system OpenGL, UI over a "scene".
//
// SdlWindow creates the window and GL context; the event loop is the app's.
// Each translated event goes to the UI when the UI wants it
// (Context::wants_mouse / wants_keyboard) and to the scene otherwise: here
// the scene is just the clear color - click to pick a hue, scroll to change
// brightness - under a docked slikui panel.
#include <slikui/backends/opengl/opengl.hpp> // system GL headers first
#include <slikui/backends/sdl/sdl_window.hpp>
#include <slikui/slikui.hpp>
#include <slikui/text/default_font.hpp>
#include <slikui/text/system_fonts.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

namespace {

bool is_pointer(const slikui::Event& e) {
    using T = slikui::Event::Type;
    return e.type == T::MouseMove || e.type == T::MousePress ||
           e.type == T::MouseRelease || e.type == T::Scroll;
}

bool is_key(const slikui::Event& e) {
    using T = slikui::Event::Type;
    return e.type == T::KeyPress || e.type == T::KeyRelease || e.type == T::TextInput;
}

} // namespace

int main() {
    slikui::sdl::SdlWindow win(900, 600, "slikui + SDL3");
    if (!win.ok()) return 1;
    win.set_min_size(480, 320);

    slikui::opengl::OpenglSink sink;
    if (!sink.init()) return 1;
    slikui::Context ctx(sink);
    win.attach(ctx); // clipboard + cursor shapes
    if (!ctx.load_font_file(slikui::find_system_font()))
        ctx.load_font_memory(slikui::text::default_font_data(), slikui::text::default_font_size());

    // Scene state, changed only by events the UI does not want.
    float hue = 0.6f, brightness = 0.25f;
    int scene_clicks = 0;

    auto& root = ctx.set_root<slikui::Row>(); // pure layout: pointer falls through
    root.set_padding(16);
    root.add<slikui::Spacer>();
    auto& panel = root.add<slikui::Panel>();
    panel.set_background(slikui::Color(20, 22, 26, 220)).set_radius(10);
    panel.set_padding(14).set_gap(8);
    panel.add<slikui::Label>("SDL3 backend").set_font_size(20);
    panel.add<slikui::Label>("Click or scroll outside this panel:");
    panel.add<slikui::Label>("the scene gets it, the UI doesn't.");
    auto& info = panel.add<slikui::Label>("scene clicks: 0");
    auto& slider = panel.add<slikui::Slider>(0.f, 1.f);
    slider.set_value(brightness, false);
    slider.on_change([&](float v) { brightness = v; });
    panel.add<slikui::TextField>().set_placeholder("type here (keys go to the UI)");
    panel.add<slikui::Button>("Reset").on_click([&] {
        hue = 0.6f;
        brightness = 0.25f;
        slider.set_value(brightness, false);
    });

    auto last = std::chrono::steady_clock::now();
    double now = 0;
    while (!win.should_close()) {
        // 1. Pump SDL ourselves; SdlInput translates this window's events.
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT || ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
                win.set_should_close(true);
            win.input().process(ev);
        }
        // 2. Route: UI first where it wants the event, the scene otherwise.
        for (const slikui::Event& e : win.input().events()) {
            bool to_ui = is_pointer(e) ? ctx.wants_mouse(e.pos)
                         : is_key(e)   ? ctx.wants_keyboard()
                                       : true; // resize, focus loss: always
            if (to_ui) {
                ctx.handle(e);
                continue;
            }
            using T = slikui::Event::Type;
            if (e.type == T::MousePress && e.button == slikui::MouseLeft) {
                hue = std::fmod(e.pos.x / std::max(win.point_size().x, 1.f), 1.f);
                info.set_text("scene clicks: " + std::to_string(++scene_clicks));
            } else if (e.type == T::Scroll) {
                brightness = std::clamp(brightness + e.scroll.y * 0.03f, 0.f, 1.f);
                slider.set_value(brightness, false);
            }
        }
        win.input().events().clear();

        float scale = win.ui_scale();
        sink.set_scale(scale);
        ctx.theme().scale = scale;
        ctx.set_frame_size(win.point_size());

        auto t = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(t - last).count();
        last = t;
        now += dt;
        ctx.tick(slikui::Time{now, slikui::clamp_dt(dt)});

        // 3. Scene, then the UI over it (render(false): no background fill).
        slikui::Vec2 px = win.frame_size();
        glViewport(0, 0, int(px.x), int(px.y));
        glDisable(GL_SCISSOR_TEST);
        auto channel = [&](float off) {
            return brightness * (0.5f + 0.5f * std::cos(6.2831853f * (hue + off)));
        };
        glClearColor(channel(0.f), channel(0.33f), channel(0.67f), 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ctx.render(false);
        win.swap();
    }
    ctx.clear_tree();
    sink.shutdown();
    return 0;
}
