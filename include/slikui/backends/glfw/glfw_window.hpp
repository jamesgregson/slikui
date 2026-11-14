// slikui/backends/glfw/glfw_window.hpp - a ready-made GLFW window for slikui.
//
// Optional backend: compiles only when <GLFW/glfw3.h> is available. Creates
// a GL 3.3 core window and owns a GlfwInput for it. Already have a window
// (your engine, your own GLFW setup)? Use glfw_input.hpp directly: event
// builders, GlfwInput on your GLFWwindow*, attach_services. Pairs with
// OpenglSink:
//
//   slikui::glfw::GlfwWindow win(1280, 800, "slikui demo");
//   slikui::opengl::OpenglSink sink;   // system OpenGL (see opengl.hpp)
//   sink.init();
//   slikui::Context ctx(sink);
//   win.attach(ctx);                    // clipboard + cursor shapes
//   while (!win.should_close()) {
//       win.dispatch(ctx);              // poll + translate events
//       ctx.set_frame_size(win.point_size());
//       ctx.tick({time, dt});
//       ctx.render();
//       win.swap();
//   }
#pragma once

#include "slikui/backends/glfw/glfw_input.hpp"

#ifdef SLIKUI_HAS_GLFW

#include <optional>

namespace slikui::glfw {

class GlfwWindow {
public:
    GlfwWindow(int width, int height, const char* title) {
        glfwSetErrorCallback([](int, const char* msg) { /* surface via breakage */ (void)msg; });
        if (!glfwInit()) {
            failed_ = true;
            return;
        }
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
        window_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
        if (!window_) {
            failed_ = true;
            return;
        }
        // Explicit: don't rely on glfwCreateWindow's implicit context-current.
        glfwMakeContextCurrent(window_);
        input_.emplace(window_);
    }

    ~GlfwWindow() {
        input_.reset(); // restore callbacks while the window is still alive
        if (window_) glfwDestroyWindow(window_);
        // glfwTerminate is intentionally left to the app (single window typical).
    }

    GlfwWindow(const GlfwWindow&) = delete;
    GlfwWindow& operator=(const GlfwWindow&) = delete;

    bool ok() const { return !failed_ && window_; }
    bool should_close() const { return glfwWindowShouldClose(window_); }

    // Poll GLFW, then hand every translated event to `ctx`.
    void dispatch(Context& ctx) {
        glfwPollEvents();
        if (input_) input_->dispatch(ctx);
    }

    // The event translator, for hosts that route events themselves:
    //   glfwPollEvents(); for (auto& e : win.input().events()) ...; clear().
    GlfwInput& input() { return *input_; }

    Vec2 frame_size() const {
        int w = 0, h = 0;
        glfwGetFramebufferSize(window_, &w, &h);
        return {float(w), float(h)};
    }

    // Window size limits: keeps absurd layouts away on tiny windows.
    void set_min_size(int w, int h) {
        glfwSetWindowSizeLimits(window_, w, h, GLFW_DONT_CARE, GLFW_DONT_CARE);
    }

    Vec2 point_size() const {
        int w = 0, h = 0;
        glfwGetWindowSize(window_, &w, &h);
        return {float(w), float(h)};
    }
    // Framebuffer px per logical point for this window (HiDPI-aware, always
    // current - queried from GLFW, never cached).
    float ui_scale() const {
        int fw = 0, fh = 0, ww = 0, wh = 0;
        glfwGetFramebufferSize(window_, &fw, &fh);
        glfwGetWindowSize(window_, &ww, &wh);
        return ww > 0 ? float(fw) / float(ww) : 1.f;
    }

    void swap() { glfwSwapBuffers(window_); }

    GLFWwindow* handle() { return window_; }

    // Wire host services that live on the window (system clipboard, cursor
    // shapes). Call once after creating the Context.
    void attach(Context& ctx) { attach_services(ctx, window_); }

private:
    GLFWwindow* window_ = nullptr;
    bool failed_ = false;
    std::optional<GlfwInput> input_;
};

} // namespace slikui::glfw

#endif // SLIKUI_HAS_GLFW
