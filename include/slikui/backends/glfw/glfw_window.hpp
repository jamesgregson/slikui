// slikui/backends/glfw/glfw_window.hpp — GLFW window + event translation.
//
// Optional backend: compiles only when <GLFW/glfw3.h> is available.
// Pairs with OpenglSink:
//
//   slikui::glfw::GlfwWindow win(1280, 800, "slikui demo");
//   slikui::opengl::OpenglSink sink;   // system OpenGL (see opengl.hpp)
//   sink.init();
//   slikui::Context ctx(sink);
//   while (!win.should_close()) {
//       win.dispatch(ctx);              // poll + translate events
//       ctx.set_frame_size(win.frame_size());
//       ctx.tick({time, dt});
//       ctx.render();
//       win.swap();
//   }
#pragma once

#if defined(__has_include)
#  if __has_include(<GLFW/glfw3.h>)
#    define SLIKUI_HAS_GLFW 1
#  endif
#endif

#ifdef SLIKUI_HAS_GLFW

#include "slikui/context.hpp"

// slikui's wrapper never touches GL itself — keep GLFW from pulling in any
// OpenGL header (examples include their own, e.g. <OpenGL/gl3.h>).
#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif

#include <GLFW/glfw3.h>

#include <vector>

namespace slikui::glfw {

inline Key map_key(int glfw_key) {
    switch (glfw_key) {
    case GLFW_KEY_ENTER:
    case GLFW_KEY_KP_ENTER: return Key::Enter;
    case GLFW_KEY_TAB: return Key::Tab;
    case GLFW_KEY_BACKSPACE: return Key::Backspace;
    case GLFW_KEY_DELETE: return Key::Delete;
    case GLFW_KEY_ESCAPE: return Key::Escape;
    case GLFW_KEY_SPACE: return Key::Space;
    case GLFW_KEY_APOSTROPHE: return Key::Apostrophe;
    case GLFW_KEY_COMMA: return Key::Comma;
    case GLFW_KEY_MINUS: return Key::Minus;
    case GLFW_KEY_PERIOD: return Key::Period;
    case GLFW_KEY_SLASH: return Key::Slash;
    case GLFW_KEY_LEFT: return Key::Left;
    case GLFW_KEY_RIGHT: return Key::Right;
    case GLFW_KEY_UP: return Key::Up;
    case GLFW_KEY_DOWN: return Key::Down;
    case GLFW_KEY_HOME: return Key::Home;
    case GLFW_KEY_END: return Key::End;
    case GLFW_KEY_PAGE_UP: return Key::PageUp;
    case GLFW_KEY_PAGE_DOWN: return Key::PageDown;
    case GLFW_KEY_INSERT: return Key::Insert;
    case GLFW_KEY_CAPS_LOCK: return Key::CapsLock;
    case GLFW_KEY_F1: return Key::F1;
    case GLFW_KEY_F2: return Key::F2;
    case GLFW_KEY_F3: return Key::F3;
    case GLFW_KEY_F4: return Key::F4;
    case GLFW_KEY_F5: return Key::F5;
    case GLFW_KEY_F6: return Key::F6;
    case GLFW_KEY_F7: return Key::F7;
    case GLFW_KEY_F8: return Key::F8;
    case GLFW_KEY_F9: return Key::F9;
    case GLFW_KEY_F10: return Key::F10;
    case GLFW_KEY_F11: return Key::F11;
    case GLFW_KEY_F12: return Key::F12;
    default: return Key::Unknown;
    }
}

inline std::uint32_t map_mods(int glfw_mods) {
    std::uint32_t m = ModNone;
    if (glfw_mods & GLFW_MOD_SHIFT) m |= ModShift;
    if (glfw_mods & GLFW_MOD_CONTROL) m |= ModCtrl;
    if (glfw_mods & GLFW_MOD_ALT) m |= ModAlt;
    if (glfw_mods & GLFW_MOD_SUPER) m |= ModSuper;
    return m;
}

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
        glfwSetWindowUserPointer(window_, this);
        glfwSetFramebufferSizeCallback(window_, [](GLFWwindow* w, int width, int height) {
            auto* self = static_cast<GlfwWindow*>(glfwGetWindowUserPointer(w));
            self->frame_ = {float(width), float(height)}; // keep cached size current
            Event e{};
            e.type = Event::Type::FrameResize;
            e.pos = {float(width), float(height)};
            self->push(e);
        });
        glfwSetWindowFocusCallback(window_, [](GLFWwindow* w, int focused) {
            if (!focused) {
                auto* self = static_cast<GlfwWindow*>(glfwGetWindowUserPointer(w));
                Event e{};
                e.type = Event::Type::FocusLost;
                self->push(e);
            }
        });
        glfwSetMouseButtonCallback(window_, [](GLFWwindow* w, int button, int action, int) {
            auto* self = static_cast<GlfwWindow*>(glfwGetWindowUserPointer(w));
            double x = 0, y = 0;
            glfwGetCursorPos(w, &x, &y);
            Event e{};
            e.type = action == GLFW_PRESS ? Event::Type::MousePress : Event::Type::MouseRelease;
            e.pos = {float(x), float(y)};
            e.button = button;
            self->push(e);
        });
        glfwSetCursorPosCallback(window_, [](GLFWwindow* w, double x, double y) {
            auto* self = static_cast<GlfwWindow*>(glfwGetWindowUserPointer(w));
            Event e{};
            e.type = Event::Type::MouseMove;
            e.delta = {float(x) - self->last_mouse_.x, float(y) - self->last_mouse_.y};
            e.pos = {float(x), float(y)};
            self->last_mouse_ = e.pos;
            self->push(e);
        });
        glfwSetScrollCallback(window_, [](GLFWwindow* w, double dx, double dy) {
            auto* self = static_cast<GlfwWindow*>(glfwGetWindowUserPointer(w));
            double x = 0, y = 0;
            glfwGetCursorPos(w, &x, &y); // needed for hit-testing the wheel target
            Event e{};
            e.type = Event::Type::Scroll;
            e.pos = {float(x), float(y)};
            e.scroll = {float(dx), float(dy)};
            self->push(e);
        });
        glfwSetKeyCallback(window_, [](GLFWwindow* w, int key, int, int action, int mods) {
            auto* self = static_cast<GlfwWindow*>(glfwGetWindowUserPointer(w));
            Event e{};
            e.type = action == GLFW_PRESS ? Event::Type::KeyPress : Event::Type::KeyRelease;
            e.key = map_key(key);
            e.mods = map_mods(mods);
            self->push(e);
        });
        glfwSetCharCallback(window_, [](GLFWwindow* w, unsigned int cp) {
            auto* self = static_cast<GlfwWindow*>(glfwGetWindowUserPointer(w));
            Event e{};
            e.type = Event::Type::TextInput;
            e.codepoint = cp;
            self->push(e);
        });

        int fbw = width, fbh = height;
        glfwGetFramebufferSize(window_, &fbw, &fbh);
        frame_ = {float(fbw), float(fbh)};
    }

    ~GlfwWindow() {
        if (window_) glfwDestroyWindow(window_);
        // glfwTerminate is intentionally left to the app (single window typical).
    }

    bool ok() const { return !failed_ && window_; }
    bool should_close() const { return glfwWindowShouldClose(window_); }

    void dispatch(Context& ctx) {
        glfwPollEvents();
        for (const auto& e : events_) ctx.handle(e);
        events_.clear();
    }

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
    // Logical px per framebuffer px for this window (HiDPI-aware, always
    // current — queried from GLFW, never cached).
    float ui_scale() const {
        int fw = 0, fh = 0, ww = 0, wh = 0;
        glfwGetFramebufferSize(window_, &fw, &fh);
        glfwGetWindowSize(window_, &ww, &wh);
        return ww > 0 ? float(fw) / float(ww) : 1.f;
    }

    void swap() { glfwSwapBuffers(window_); }

    GLFWwindow* handle() { return window_; }

private:
    void push(const Event& e) { events_.push_back(e); }

    GLFWwindow* window_ = nullptr;
    bool failed_ = false;
    Vec2 frame_{};
    Vec2 last_mouse_{};
    std::vector<Event> events_;
};

} // namespace slikui::glfw

#else

// GLFW not installed: slikui still builds; only this wrapper is unavailable.
namespace slikui::glfw {
// Intentionally empty.
}

#endif // SLIKUI_HAS_GLFW
