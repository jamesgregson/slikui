// slikui/backends/sdl/sdl_window.hpp — a ready-made SDL3 window for slikui.
//
// Optional backend: compiles only when <SDL3/SDL.h> is available. Creates a
// resizable, HiDPI-aware window with a GL 3.3 core context and owns an
// SdlInput for it. Already have a window (your engine, your own SDL setup)?
// Use sdl_input.hpp directly. Pairs with OpenglSink:
//
//   slikui::sdl::SdlWindow win(1280, 800, "slikui demo");
//   slikui::opengl::OpenglSink sink;   // system OpenGL (see opengl.hpp)
//   sink.init();
//   slikui::Context ctx(sink);
//   win.attach(ctx);                    // clipboard + cursor shapes
//   while (!win.should_close()) {
//       win.dispatch(ctx);              // pump + translate events
//       sink.set_scale(win.ui_scale());
//       ctx.set_frame_size(win.point_size());
//       ctx.tick({time, dt});
//       ctx.render();
//       win.swap();
//   }
//
// SdlWindow calls SDL_Init(SDL_INIT_VIDEO) if video isn't initialized yet;
// SDL_Quit stays with the app.
#pragma once

#include "slikui/backends/sdl/sdl_input.hpp"

#ifdef SLIKUI_HAS_SDL3

#include <functional>
#include <optional>

namespace slikui::sdl {

class SdlWindow {
public:
    // `extra_flags` are OR'd into SDL_CreateWindow's (e.g. SDL_WINDOW_HIDDEN
    // for offscreen rendering, SDL_WINDOW_FULLSCREEN).
    SdlWindow(int width, int height, const char* title, SDL_WindowFlags extra_flags = 0) {
        if (!(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) && !SDL_Init(SDL_INIT_VIDEO)) {
            failed_ = true;
            return;
        }
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#ifdef __APPLE__
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        window_ = SDL_CreateWindow(title, width, height,
                                   SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
                                       SDL_WINDOW_HIGH_PIXEL_DENSITY | extra_flags);
        if (!window_) {
            failed_ = true;
            return;
        }
        gl_ = SDL_GL_CreateContext(window_);
        if (!gl_) {
            failed_ = true;
            return;
        }
        SDL_GL_MakeCurrent(window_, gl_);
        SDL_GL_SetSwapInterval(1); // vsync where available
        input_.emplace(window_);
    }

    ~SdlWindow() {
        input_.reset(); // stop text input while the window is alive
        if (gl_) SDL_GL_DestroyContext(gl_);
        if (window_) SDL_DestroyWindow(window_);
    }

    SdlWindow(const SdlWindow&) = delete;
    SdlWindow& operator=(const SdlWindow&) = delete;

    bool ok() const { return !failed_ && window_ && gl_; }

    // Set by dispatch() on SDL_EVENT_QUIT or this window's close request.
    bool should_close() const { return close_; }
    void set_should_close(bool v) { close_ = v; }

    // Pump SDL, translate this window's events and hand them to `ctx`.
    // Events slikui has no use for go to on_event (if set) instead.
    void dispatch(Context& ctx) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT ||
                (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                 e.window.windowID == input_->window_id()))
                close_ = true;
            if (!input_->process(e) && on_event) on_event(e);
        }
        input_->dispatch(ctx);
    }

    // Receives every polled event slikui did not use (quit, gamepads, other
    // windows, unmapped keys).
    std::function<void(const SDL_Event&)> on_event;

    SdlInput& input() { return *input_; }

    Vec2 frame_size() const {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window_, &w, &h);
        return {float(w), float(h)};
    }

    Vec2 point_size() const {
        int w = 0, h = 0;
        SDL_GetWindowSize(window_, &w, &h);
        return {float(w), float(h)};
    }

    // Framebuffer px per logical point (HiDPI-aware, always current).
    float ui_scale() const {
        float d = SDL_GetWindowPixelDensity(window_);
        return d > 0 ? d : 1.f;
    }

    void set_min_size(int w, int h) { SDL_SetWindowMinimumSize(window_, w, h); }

    void swap() { SDL_GL_SwapWindow(window_); }

    SDL_Window* handle() { return window_; }
    SDL_GLContext gl_context() { return gl_; }

    // Wire host services (system clipboard, cursor shapes). Call once after
    // creating the Context.
    void attach(Context& ctx) { attach_services(ctx); }

private:
    SDL_Window* window_ = nullptr;
    SDL_GLContext gl_ = nullptr;
    bool failed_ = false;
    bool close_ = false;
    std::optional<SdlInput> input_;
};

} // namespace slikui::sdl

#endif // SLIKUI_HAS_SDL3
