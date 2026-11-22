// slikui/backends/glfw/glfw_input.hpp - GLFW event translation for YOUR window.
//
// Optional backend: compiles only when <GLFW/glfw3.h> is available. Nothing
// here creates a window or touches GL. Three levels, pick one:
//
// 1. Event builders: you own every GLFW callback and forward what you want.
//
//      glfwSetMouseButtonCallback(win, [](GLFWwindow* w, int b, int a, int m) {
//          if (!my_scene_wants_it(w)) app_ctx().handle(slikui::glfw::mouse_button_event(w, b, a, m));
//      });
//
// 2. GlfwInput: installs the callbacks on an existing window, CHAINING to any
//    you installed before it (yours still run, first), and queues events.
//    It does not use the window user pointer, so that stays yours.
//
//      slikui::glfw::GlfwInput input(window);      // after your own callbacks
//      ...
//      glfwPollEvents();
//      input.dispatch(ctx);                          // or input.events()
//
// 3. GlfwWindow (glfw_window.hpp): creates the window and owns a GlfwInput.
//
// attach_services(ctx, window) wires the system clipboard and cursor shapes.
//
// Coordinates: positions are GLFW cursor coordinates (window points, the
// logical space Context lays out in); FrameResize carries framebuffer pixels.
#pragma once

#if defined(__has_include)
#  if __has_include(<GLFW/glfw3.h>)
#    define SLIKUI_HAS_GLFW 1
#  endif
#endif

#ifdef SLIKUI_HAS_GLFW

#include "slikui/context.hpp"

// slikui's wrapper never touches GL itself - keep GLFW from pulling in any
// OpenGL header (examples include their own, e.g. <OpenGL/gl3.h>).
#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif

#include <GLFW/glfw3.h>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace slikui::glfw {

// --- mapping -------------------------------------------------------------------

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
    case GLFW_KEY_EQUAL: return Key::Equal;
    case GLFW_KEY_LEFT_BRACKET: return Key::LeftBracket;
    case GLFW_KEY_RIGHT_BRACKET: return Key::RightBracket;
    case GLFW_KEY_BACKSLASH: return Key::Backslash;
    case GLFW_KEY_SEMICOLON: return Key::Semicolon;
    case GLFW_KEY_GRAVE_ACCENT: return Key::Grave;
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
    case GLFW_KEY_A: return Key::A;
    case GLFW_KEY_B: return Key::B;
    case GLFW_KEY_C: return Key::C;
    case GLFW_KEY_D: return Key::D;
    case GLFW_KEY_E: return Key::E;
    case GLFW_KEY_F: return Key::F;
    case GLFW_KEY_G: return Key::G;
    case GLFW_KEY_H: return Key::H;
    case GLFW_KEY_I: return Key::I;
    case GLFW_KEY_J: return Key::J;
    case GLFW_KEY_K: return Key::K;
    case GLFW_KEY_L: return Key::L;
    case GLFW_KEY_M: return Key::M;
    case GLFW_KEY_N: return Key::N;
    case GLFW_KEY_O: return Key::O;
    case GLFW_KEY_P: return Key::P;
    case GLFW_KEY_Q: return Key::Q;
    case GLFW_KEY_R: return Key::R;
    case GLFW_KEY_S: return Key::S;
    case GLFW_KEY_T: return Key::T;
    case GLFW_KEY_U: return Key::U;
    case GLFW_KEY_V: return Key::V;
    case GLFW_KEY_W: return Key::W;
    case GLFW_KEY_X: return Key::X;
    case GLFW_KEY_Y: return Key::Y;
    case GLFW_KEY_Z: return Key::Z;
    case GLFW_KEY_0: return Key::Num0;
    case GLFW_KEY_1: return Key::Num1;
    case GLFW_KEY_2: return Key::Num2;
    case GLFW_KEY_3: return Key::Num3;
    case GLFW_KEY_4: return Key::Num4;
    case GLFW_KEY_5: return Key::Num5;
    case GLFW_KEY_6: return Key::Num6;
    case GLFW_KEY_7: return Key::Num7;
    case GLFW_KEY_8: return Key::Num8;
    case GLFW_KEY_9: return Key::Num9;
    case GLFW_KEY_KP_0: return Key::Kp0;
    case GLFW_KEY_KP_1: return Key::Kp1;
    case GLFW_KEY_KP_2: return Key::Kp2;
    case GLFW_KEY_KP_3: return Key::Kp3;
    case GLFW_KEY_KP_4: return Key::Kp4;
    case GLFW_KEY_KP_5: return Key::Kp5;
    case GLFW_KEY_KP_6: return Key::Kp6;
    case GLFW_KEY_KP_7: return Key::Kp7;
    case GLFW_KEY_KP_8: return Key::Kp8;
    case GLFW_KEY_KP_9: return Key::Kp9;
    case GLFW_KEY_KP_DECIMAL: return Key::KpDecimal;
    case GLFW_KEY_KP_DIVIDE: return Key::KpDivide;
    case GLFW_KEY_KP_MULTIPLY: return Key::KpMultiply;
    case GLFW_KEY_KP_SUBTRACT: return Key::KpSubtract;
    case GLFW_KEY_KP_ADD: return Key::KpAdd;
    case GLFW_KEY_KP_EQUAL: return Key::KpEqual;
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

// GLFW numbers buttons LEFT=0, RIGHT=1, MIDDLE=2; slikui uses MouseLeft=0,
// MouseMiddle=1, MouseRight=2. Extra buttons (GLFW 3..7) pass through.
inline int map_button(int glfw_button) {
    switch (glfw_button) {
    case GLFW_MOUSE_BUTTON_LEFT: return MouseLeft;
    case GLFW_MOUSE_BUTTON_RIGHT: return MouseRight;
    case GLFW_MOUSE_BUTTON_MIDDLE: return MouseMiddle;
    default: return glfw_button;
    }
}

// Modifier state for events whose GLFW callback carries none (cursor move,
// scroll): read it from the key state so Shift-drag and Ctrl-wheel work.
inline std::uint32_t current_mods(GLFWwindow* w) {
    auto down = [w](int a, int b) {
        return glfwGetKey(w, a) == GLFW_PRESS || glfwGetKey(w, b) == GLFW_PRESS;
    };
    std::uint32_t m = ModNone;
    if (down(GLFW_KEY_LEFT_SHIFT, GLFW_KEY_RIGHT_SHIFT)) m |= ModShift;
    if (down(GLFW_KEY_LEFT_CONTROL, GLFW_KEY_RIGHT_CONTROL)) m |= ModCtrl;
    if (down(GLFW_KEY_LEFT_ALT, GLFW_KEY_RIGHT_ALT)) m |= ModAlt;
    if (down(GLFW_KEY_LEFT_SUPER, GLFW_KEY_RIGHT_SUPER)) m |= ModSuper;
    return m;
}

// --- event builders: one per GLFW callback signature ---------------------------
//
// Each takes the callback's own arguments and returns the slikui Event to
// pass to Context::handle. The window argument (where present) is only
// queried for cursor position / held modifiers.

inline Event mouse_button_event(GLFWwindow* w, int button, int action, int mods) {
    double x = 0, y = 0;
    glfwGetCursorPos(w, &x, &y);
    Event e{};
    e.type = action == GLFW_PRESS ? Event::Type::MousePress : Event::Type::MouseRelease;
    e.pos = {float(x), float(y)};
    e.button = map_button(button);
    e.mods = map_mods(mods);
    return e;
}

// `last` is the previous cursor position (for delta); updated in place.
inline Event cursor_pos_event(GLFWwindow* w, double x, double y, Vec2& last) {
    Event e{};
    e.type = Event::Type::MouseMove;
    e.pos = {float(x), float(y)};
    e.delta = {e.pos.x - last.x, e.pos.y - last.y};
    e.mods = current_mods(w);
    last = e.pos;
    return e;
}

inline Event scroll_event(GLFWwindow* w, double dx, double dy) {
    double x = 0, y = 0;
    glfwGetCursorPos(w, &x, &y); // needed for hit-testing the wheel target
    Event e{};
    e.type = Event::Type::Scroll;
    e.pos = {float(x), float(y)};
    e.scroll = {float(dx), float(dy)};
    e.mods = current_mods(w);
    return e;
}

inline Event key_event(int key, int /*scancode*/, int action, int mods) {
    Event e{};
    // GLFW_REPEAT is a press (auto-repeat), not a release.
    e.type = action == GLFW_RELEASE ? Event::Type::KeyRelease : Event::Type::KeyPress;
    e.repeat = action == GLFW_REPEAT;
    e.key = map_key(key);
    e.mods = map_mods(mods);
    return e;
}

inline Event char_event(unsigned int codepoint) {
    Event e{};
    e.type = Event::Type::TextInput;
    e.codepoint = codepoint;
    return e;
}

// Framebuffer size in PIXELS (glfwSetFramebufferSizeCallback).
inline Event framebuffer_size_event(int width, int height) {
    Event e{};
    e.type = Event::Type::FrameResize;
    e.pos = {float(width), float(height)};
    return e;
}

// Focus out becomes FocusLost (drags, hover, held keys cancel); focus in
// has no slikui event.
inline std::optional<Event> focus_event(int focused) {
    if (focused) return std::nullopt;
    Event e{};
    e.type = Event::Type::FocusLost;
    return e;
}

// --- host services ------------------------------------------------------------------

// System clipboard + cursor shapes for `window`. Call once per Context.
inline void attach_services(Context& ctx, GLFWwindow* window) {
    ctx.set_clipboard_source([] {
        const char* s = glfwGetClipboardString(nullptr);
        return s ? std::string(s) : std::string{};
    });
    ctx.set_clipboard_sink([](std::string text) {
        glfwSetClipboardString(nullptr, text.c_str());
    });
    // Cursor shapes -> GLFW standard cursors (created lazily, shared, never
    // destroyed: glfwTerminate frees them).
    struct Cursors {
        GLFWcursor* map[7] = {};
        GLFWcursor* get(int shape, int slot) {
            if (!map[slot]) map[slot] = glfwCreateStandardCursor(shape);
            return map[slot];
        }
    };
    static Cursors cursors;
    ctx.set_cursor_host([window](slikui::Cursor c) {
        int shape = GLFW_ARROW_CURSOR, slot = 0;
        switch (c) {
        case slikui::Cursor::IBeam: shape = GLFW_IBEAM_CURSOR; slot = 1; break;
        case slikui::Cursor::Hand: shape = GLFW_HAND_CURSOR; slot = 2; break;
        case slikui::Cursor::Grab: shape = GLFW_HAND_CURSOR; slot = 3; break;
        case slikui::Cursor::Crosshair: shape = GLFW_CROSSHAIR_CURSOR; slot = 4; break;
        case slikui::Cursor::ResizeH: shape = GLFW_HRESIZE_CURSOR; slot = 5; break;
        case slikui::Cursor::ResizeV: shape = GLFW_VRESIZE_CURSOR; slot = 6; break;
        default: break;
        }
        glfwSetCursor(window, slot == 0 ? nullptr : cursors.get(shape, slot));
    });
}

// --- GlfwInput: callbacks on an existing window ---------------------------------------

class GlfwInput {
public:
    // Installs slikui's callbacks on `window`, chaining to whatever callbacks
    // were installed before (they run first, unchanged). Construct it AFTER
    // installing your own; callbacks you install later replace slikui's
    // (call the matching builder yourself, or re-create GlfwInput).
    explicit GlfwInput(GLFWwindow* window) : window_(window) {
        registry().push_back(this);
        double x = 0, y = 0;
        glfwGetCursorPos(window_, &x, &y);
        last_mouse_ = {float(x), float(y)};
        prev_.fb = glfwSetFramebufferSizeCallback(window_, &on_fb);
        prev_.focus = glfwSetWindowFocusCallback(window_, &on_focus);
        prev_.button = glfwSetMouseButtonCallback(window_, &on_button);
        prev_.cursor = glfwSetCursorPosCallback(window_, &on_cursor);
        prev_.scroll = glfwSetScrollCallback(window_, &on_scroll);
        prev_.key = glfwSetKeyCallback(window_, &on_key);
        prev_.chr = glfwSetCharCallback(window_, &on_char);
    }

    // Restores the callbacks that were installed before this GlfwInput.
    ~GlfwInput() {
        if (window_) {
            glfwSetFramebufferSizeCallback(window_, prev_.fb);
            glfwSetWindowFocusCallback(window_, prev_.focus);
            glfwSetMouseButtonCallback(window_, prev_.button);
            glfwSetCursorPosCallback(window_, prev_.cursor);
            glfwSetScrollCallback(window_, prev_.scroll);
            glfwSetKeyCallback(window_, prev_.key);
            glfwSetCharCallback(window_, prev_.chr);
        }
        auto& r = registry();
        r.erase(std::remove(r.begin(), r.end(), this), r.end());
    }

    GlfwInput(const GlfwInput&) = delete;
    GlfwInput& operator=(const GlfwInput&) = delete;

    // Hand every queued event to `ctx` (call after glfwPollEvents).
    void dispatch(Context& ctx) {
        // Swap first: a handler may pump events (modal loops) re-entrantly.
        std::vector<Event> batch;
        batch.swap(events_);
        for (const auto& e : batch) ctx.handle(e);
    }

    // Queued events, for hosts that route them themselves (e.g. send mouse
    // events to the scene unless the UI is under the cursor). Clear it after.
    std::vector<Event>& events() { return events_; }

    GLFWwindow* window() const { return window_; }

    // Detach without restoring callbacks - for when the window is already
    // destroyed (GLFW would be called with a dead handle otherwise).
    void release_window() { window_ = nullptr; }

private:
    struct Prev {
        GLFWframebuffersizefun fb = nullptr;
        GLFWwindowfocusfun focus = nullptr;
        GLFWmousebuttonfun button = nullptr;
        GLFWcursorposfun cursor = nullptr;
        GLFWscrollfun scroll = nullptr;
        GLFWkeyfun key = nullptr;
        GLFWcharfun chr = nullptr;
    };

    // window -> GlfwInput, without claiming the window user pointer.
    static std::vector<GlfwInput*>& registry() {
        static std::vector<GlfwInput*> r;
        return r;
    }
    static GlfwInput* of(GLFWwindow* w) {
        for (auto* in : registry())
            if (in->window_ == w) return in;
        return nullptr;
    }

    static void on_fb(GLFWwindow* w, int width, int height) {
        GlfwInput* in = of(w);
        if (!in) return;
        if (in->prev_.fb) in->prev_.fb(w, width, height);
        in->events_.push_back(framebuffer_size_event(width, height));
    }
    static void on_focus(GLFWwindow* w, int focused) {
        GlfwInput* in = of(w);
        if (!in) return;
        if (in->prev_.focus) in->prev_.focus(w, focused);
        if (auto e = focus_event(focused)) in->events_.push_back(*e);
    }
    static void on_button(GLFWwindow* w, int button, int action, int mods) {
        GlfwInput* in = of(w);
        if (!in) return;
        if (in->prev_.button) in->prev_.button(w, button, action, mods);
        in->events_.push_back(mouse_button_event(w, button, action, mods));
    }
    static void on_cursor(GLFWwindow* w, double x, double y) {
        GlfwInput* in = of(w);
        if (!in) return;
        if (in->prev_.cursor) in->prev_.cursor(w, x, y);
        in->events_.push_back(cursor_pos_event(w, x, y, in->last_mouse_));
    }
    static void on_scroll(GLFWwindow* w, double dx, double dy) {
        GlfwInput* in = of(w);
        if (!in) return;
        if (in->prev_.scroll) in->prev_.scroll(w, dx, dy);
        in->events_.push_back(scroll_event(w, dx, dy));
    }
    static void on_key(GLFWwindow* w, int key, int scancode, int action, int mods) {
        GlfwInput* in = of(w);
        if (!in) return;
        if (in->prev_.key) in->prev_.key(w, key, scancode, action, mods);
        in->events_.push_back(key_event(key, scancode, action, mods));
    }
    static void on_char(GLFWwindow* w, unsigned int cp) {
        GlfwInput* in = of(w);
        if (!in) return;
        if (in->prev_.chr) in->prev_.chr(w, cp);
        in->events_.push_back(char_event(cp));
    }

    GLFWwindow* window_;
    Prev prev_;
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
