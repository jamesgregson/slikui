// tests/sdl_input.cpp — SDL3 event translation through SDL's real queue.
//
// Uses the dummy video driver (no display, no GL needed). Events go in with
// SDL_PushEvent and come back out of SDL_PollEvent, as in a real app.
// Prints SKIP and passes only if SDL video cannot initialize at all.
#include "slikui/backends/sdl/sdl_input.hpp"
#include "slikui/backends/software/software.hpp"
#include "slikui/widgets/all.hpp"

#include <cstdio>
#include <vector>

using namespace slikui;
using T = Event::Type;

namespace {

int failures = 0;
#define EXPECT(cond)                                                          \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
            ++failures;                                                       \
        }                                                                     \
    } while (0)

SDL_Event button(SDL_WindowID w, Uint8 b, bool down, float x, float y) {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.windowID = w;
    e.button.button = b;
    e.button.down = down;
    e.button.clicks = 1;
    e.button.x = x;
    e.button.y = y;
    return e;
}

SDL_Event motion(SDL_WindowID w, float x, float y, float dx, float dy) {
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.windowID = w;
    e.motion.x = x;
    e.motion.y = y;
    e.motion.xrel = dx;
    e.motion.yrel = dy;
    return e;
}

SDL_Event key(SDL_WindowID w, SDL_Keycode k, bool down, SDL_Keymod mod = SDL_KMOD_NONE,
              bool repeat = false) {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.windowID = w;
    e.key.key = k;
    e.key.mod = mod;
    e.key.down = down;
    e.key.repeat = repeat;
    return e;
}

// Push, then drain the queue through SdlInput (what an app loop does).
void pump(sdl::SdlInput& in, std::initializer_list<SDL_Event> evs) {
    for (SDL_Event e : evs) SDL_PushEvent(&e);
    SDL_Event e;
    while (SDL_PollEvent(&e)) in.process(e);
}

void mapping() {
    EXPECT(sdl::map_button(SDL_BUTTON_LEFT) == MouseLeft);
    EXPECT(sdl::map_button(SDL_BUTTON_MIDDLE) == MouseMiddle);
    EXPECT(sdl::map_button(SDL_BUTTON_RIGHT) == MouseRight);
    EXPECT(sdl::map_button(SDL_BUTTON_X1) == 3 && sdl::map_button(SDL_BUTTON_X2) == 4);
    EXPECT(sdl::map_key(SDLK_A) == Key::A && sdl::map_key(SDLK_Z) == Key::Z);
    EXPECT(sdl::map_key(SDLK_0) == Key::Num0 && sdl::map_key(SDLK_9) == Key::Num9);
    EXPECT(sdl::map_key(SDLK_KP_ENTER) == Key::Enter && sdl::map_key(SDLK_RETURN) == Key::Enter);
    EXPECT(sdl::map_key(SDLK_PAGEDOWN) == Key::PageDown && sdl::map_key(SDLK_F12) == Key::F12);
    EXPECT(sdl::map_key(SDLK_F13) == Key::Unknown);
    EXPECT(sdl::map_mods(SDL_KMOD_LSHIFT | SDL_KMOD_RGUI) == (ModShift | ModSuper));
    EXPECT(sdl::map_mods(SDL_KMOD_CAPS | SDL_KMOD_NUM) == ModNone);

    std::vector<Event> out;
    SDL_Event w{};
    w.type = SDL_EVENT_MOUSE_WHEEL;
    w.wheel.x = 1.5f;
    w.wheel.y = -2.f;
    w.wheel.mouse_x = 10;
    w.wheel.mouse_y = 20;
    w.wheel.direction = SDL_MOUSEWHEEL_FLIPPED;
    EXPECT(sdl::translate(w, out) && out.size() == 1);
    EXPECT(out[0].scroll.x == -1.5f && out[0].scroll.y == 2.f && out[0].pos.y == 20);

    out.clear();
    SDL_Event t{};
    t.type = SDL_EVENT_TEXT_INPUT;
    t.text.text = "a\xC3\xA9\xE2\x98\xBA"; // a é ☺
    EXPECT(sdl::translate(t, out) && out.size() == 3);
    if (out.size() == 3)
        EXPECT(out[0].codepoint == 'a' && out[1].codepoint == 0xE9 && out[2].codepoint == 0x263A);

    out.clear();
    EXPECT(!sdl::translate(key(0, SDLK_F13, true), out) && out.empty());
    SDL_Event q{};
    q.type = SDL_EVENT_QUIT;
    EXPECT(!sdl::translate(q, out));
}

} // namespace

int main() {
    mapping(); // pure: runs even without a video driver

    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy"); // no GL, no display
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("SKIP SDL video init failed: %s (%d mapping failures)\n", SDL_GetError(),
                    failures);
        return failures ? 1 : 0;
    }
    SDL_Window* a = SDL_CreateWindow("a", 300, 200, SDL_WINDOW_HIDDEN);
    SDL_Window* b = SDL_CreateWindow("b", 100, 100, SDL_WINDOW_HIDDEN);
    EXPECT(a && b);
    if (!a || !b) {
        SDL_Quit();
        std::printf("FAIL no offscreen windows\n");
        return 1;
    }
    SDL_WindowID ida = SDL_GetWindowID(a), idb = SDL_GetWindowID(b);
    // Window creation queues SDL's own events (incl. PIXEL_SIZE_CHANGED,
    // which translates to FrameResize); start from an empty queue.
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);

    {
        sdl::SdlInput in(a);
        EXPECT(SDL_TextInputActive(a));
        pump(in, {motion(ida, 10, 20, 3, 4),
                  button(ida, SDL_BUTTON_RIGHT, true, 10, 20),
                  button(ida, SDL_BUTTON_MIDDLE, false, 10, 20),
                  key(ida, SDLK_Z, true, SDL_KMOD_LCTRL, true),
                  motion(idb, 1, 1, 0, 0),            // other window: ignored
                  key(idb, SDLK_A, true)});           // other window: ignored
        auto& ev = in.events();
        EXPECT(ev.size() == 4);
        if (ev.size() != 4)
            for (auto& x : ev)
                std::printf("  got type %d button %d key %u\n", int(x.type), x.button,
                            unsigned(x.key));
        if (ev.size() == 4) {
            EXPECT(ev[0].type == T::MouseMove && ev[0].delta.x == 3 && ev[0].delta.y == 4);
            EXPECT(ev[1].type == T::MousePress && ev[1].button == MouseRight);
            EXPECT(ev[2].type == T::MouseRelease && ev[2].button == MouseMiddle);
            EXPECT(ev[3].type == T::KeyPress && ev[3].key == Key::Z && ev[3].repeat &&
                   ev[3].mods == ModCtrl);
        }
        ev.clear();

        // A real Context: right-click opens a context menu, left-click
        // fires a button, both through SDL's queue.
        software::Target target(300, 200);
        software::Sink sink(target);
        Context ctx(sink);
        ctx.set_frame_size({300, 200});
        auto& root = ctx.set_root<Column>();
        root.set_padding(10);
        int clicks = 0;
        auto& go = root.add<Button>("Go");
        go.on_click([&] { ++clicks; });
        Menu menu;
        menu.add_item("Copy", [] {});
        go.set_context_menu(&menu);
        ctx.tick(Time{0, 0.016});
        Vec2 c = go.bounds.center();

        pump(in, {motion(ida, c.x, c.y, 0, 0), button(ida, SDL_BUTTON_RIGHT, true, c.x, c.y),
                  button(ida, SDL_BUTTON_RIGHT, false, c.x, c.y)});
        in.dispatch(ctx);
        EXPECT(ctx.popup_count() == 1 && clicks == 0);
        ctx.close_popups();
        ctx.tick(Time{0.1, 0.016});

        pump(in, {button(ida, SDL_BUTTON_LEFT, true, c.x, c.y),
                  button(ida, SDL_BUTTON_LEFT, false, c.x, c.y)});
        in.dispatch(ctx);
        EXPECT(clicks == 1);
        EXPECT(ctx.wants_mouse(c));
        ctx.clear_tree();
    }
    EXPECT(!SDL_TextInputActive(a)); // SdlInput stopped what it started

    SDL_DestroyWindow(b);
    SDL_DestroyWindow(a);
    SDL_Quit();
    std::printf("%s sdl input: %d failures\n", failures ? "FAIL" : " ok ", failures);
    return failures ? 1 : 0;
}
