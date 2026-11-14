// tests/glfw_input.cpp — GLFW event translation on a host-owned window.
//
// GLFW cannot inject synthetic OS events, so this fetches the callbacks
// GlfwInput installed (glfwSet* returns the current one) and calls them
// exactly as GLFW would. Hidden window; prints SKIP and passes without a
// display.
#include "slikui/backends/glfw/glfw_input.hpp"

#include <cstdio>
#include <vector>

using namespace slikui;

namespace {

int failures = 0;
#define EXPECT(cond)                                                          \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
            ++failures;                                                       \
        }                                                                     \
    } while (0)

struct HostLog {
    std::vector<int> buttons;   // raw GLFW buttons the host callback saw
    int keys = 0, chars = 0, scrolls = 0, moves = 0;
} host;

void host_button(GLFWwindow*, int b, int, int) { host.buttons.push_back(b); }
void host_key(GLFWwindow*, int, int, int, int) { ++host.keys; }
void host_char(GLFWwindow*, unsigned) { ++host.chars; }
void host_scroll(GLFWwindow*, double, double) { ++host.scrolls; }
void host_cursor(GLFWwindow*, double, double) { ++host.moves; }

template <class Fn, class Setter>
Fn current(GLFWwindow* w, Setter set) {
    Fn cb = set(w, nullptr);
    set(w, cb);
    return cb;
}

} // namespace

int main() {
    // Pure mapping: no window needed.
    EXPECT(glfw::map_button(GLFW_MOUSE_BUTTON_LEFT) == MouseLeft);
    EXPECT(glfw::map_button(GLFW_MOUSE_BUTTON_RIGHT) == MouseRight);
    EXPECT(glfw::map_button(GLFW_MOUSE_BUTTON_MIDDLE) == MouseMiddle);
    EXPECT(glfw::map_button(GLFW_MOUSE_BUTTON_4) == GLFW_MOUSE_BUTTON_4);
    EXPECT(glfw::map_mods(GLFW_MOD_SHIFT | GLFW_MOD_SUPER) == (ModShift | ModSuper));
    {
        Event k = glfw::key_event(GLFW_KEY_A, 0, GLFW_REPEAT, GLFW_MOD_CONTROL);
        EXPECT(k.type == Event::Type::KeyPress && k.repeat && k.key == Key::A &&
               k.mods == ModCtrl);
        EXPECT(glfw::key_event(GLFW_KEY_A, 0, GLFW_RELEASE, 0).type == Event::Type::KeyRelease);
        EXPECT(glfw::char_event(0x263A).codepoint == 0x263A);
        Event r = glfw::framebuffer_size_event(640, 480);
        EXPECT(r.type == Event::Type::FrameResize && r.pos.x == 640 && r.pos.y == 480);
        EXPECT(!glfw::focus_event(1).has_value());
        EXPECT(glfw::focus_event(0)->type == Event::Type::FocusLost);
    }

    if (!glfwInit()) {
        std::printf("SKIP no display / glfwInit failed (%d mapping failures)\n", failures);
        return failures ? 1 : 0;
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* w = glfwCreateWindow(64, 64, "slikui glfw input", nullptr, nullptr);
    if (!w) {
        glfwTerminate();
        std::printf("SKIP no window (%d mapping failures)\n", failures);
        return failures ? 1 : 0;
    }

    // The host installs its own callbacks first; GlfwInput must chain them.
    glfwSetMouseButtonCallback(w, host_button);
    glfwSetKeyCallback(w, host_key);
    glfwSetCharCallback(w, host_char);
    glfwSetScrollCallback(w, host_scroll);
    glfwSetCursorPosCallback(w, host_cursor);
    void* user = &host;
    glfwSetWindowUserPointer(w, user);

    {
        glfw::GlfwInput input(w);
        EXPECT(glfwGetWindowUserPointer(w) == user); // untouched

        auto button = current<GLFWmousebuttonfun>(w, glfwSetMouseButtonCallback);
        auto key = current<GLFWkeyfun>(w, glfwSetKeyCallback);
        auto chr = current<GLFWcharfun>(w, glfwSetCharCallback);
        auto scroll = current<GLFWscrollfun>(w, glfwSetScrollCallback);
        auto cursor = current<GLFWcursorposfun>(w, glfwSetCursorPosCallback);
        EXPECT(button != host_button && key != host_key);

        cursor(w, 10.0, 20.0);
        cursor(w, 13.0, 24.0);
        button(w, GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, GLFW_MOD_SHIFT);
        button(w, GLFW_MOUSE_BUTTON_MIDDLE, GLFW_RELEASE, 0);
        button(w, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        key(w, GLFW_KEY_ESCAPE, 0, GLFW_PRESS, 0);
        chr(w, 'x');
        scroll(w, 0.0, -2.0);

        // Host callbacks ran, with GLFW's own (unmapped) arguments.
        EXPECT((host.buttons == std::vector<int>{GLFW_MOUSE_BUTTON_RIGHT, GLFW_MOUSE_BUTTON_MIDDLE,
                                                 GLFW_MOUSE_BUTTON_LEFT}));
        EXPECT(host.keys == 1 && host.chars == 1 && host.scrolls == 1 && host.moves == 2);

        auto& ev = input.events();
        EXPECT(ev.size() == 8);
        if (ev.size() == 8) {
            EXPECT(ev[1].type == Event::Type::MouseMove && ev[1].delta.x == 3 && ev[1].delta.y == 4);
            EXPECT(ev[2].type == Event::Type::MousePress && ev[2].button == MouseRight &&
                   ev[2].mods == ModShift);
            EXPECT(ev[3].type == Event::Type::MouseRelease && ev[3].button == MouseMiddle);
            EXPECT(ev[4].button == MouseLeft);
            EXPECT(ev[5].key == Key::Escape);
            EXPECT(ev[6].codepoint == 'x');
            EXPECT(ev[7].type == Event::Type::Scroll && ev[7].scroll.y == -2);
        }
    }

    // Destroying GlfwInput restored the host's callbacks.
    EXPECT(current<GLFWmousebuttonfun>(w, glfwSetMouseButtonCallback) == host_button);
    EXPECT(current<GLFWkeyfun>(w, glfwSetKeyCallback) == host_key);
    EXPECT(current<GLFWcursorposfun>(w, glfwSetCursorPosCallback) == host_cursor);

    glfwDestroyWindow(w);
    glfwTerminate();
    std::printf("%s glfw input: %d failures\n", failures ? "FAIL" : " ok ", failures);
    return failures ? 1 : 0;
}
