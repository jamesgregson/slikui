// slikui/backends/sdl/sdl_input.hpp - SDL3 event translation for YOUR window.
//
// Optional backend: compiles only when <SDL3/SDL.h> is available. Nothing
// here creates a window or touches GL. SDL hands events to the app through
// its queue (SDL_PollEvent) rather than callbacks, so the levels are:
//
// 1. translate(): one SDL_Event -> 0..n slikui events. You own the loop and
//    forward what the UI should see.
//
//      SDL_Event ev;
//      std::vector<slikui::Event> out;
//      while (SDL_PollEvent(&ev)) {
//          out.clear();
//          slikui::sdl::translate(ev, out);
//          bool pointer = ev.type >= SDL_EVENT_MOUSE_MOTION && ev.type <= SDL_EVENT_MOUSE_WHEEL;
//          if (out.empty() || (pointer && !ctx.wants_mouse(out[0].pos))) my_scene(ev);
//          else for (auto& e : out) ctx.handle(e);
//      }
//
// 2. SdlInput: bound to one window - filters by window id, enables SDL text
//    input on it, and queues events.
//
//      slikui::sdl::SdlInput input(window);
//      while (SDL_PollEvent(&ev)) { input.process(ev); my_app(ev); }
//      input.dispatch(ctx);                          // or input.events()
//
// 3. SdlWindow (sdl_window.hpp): creates the window + GL context and owns an
//    SdlInput.
//
// attach_services(ctx, window) wires the system clipboard and cursor shapes.
//
// Coordinates: SDL3 reports mouse positions in window coordinates (points),
// the logical space Context lays out in; FrameResize carries framebuffer
// pixels. Keys map by KEYCODE (layout-aware: Ctrl+Z follows the user's
// keyboard layout), not by physical position.
#pragma once

#if defined(__has_include)
#  if __has_include(<SDL3/SDL.h>)
#    define SLIKUI_HAS_SDL3 1
#  endif
#endif

#ifdef SLIKUI_HAS_SDL3

#include "slikui/context.hpp"
#include "slikui/core/utf8.hpp"

#include <SDL3/SDL.h>

#include <string>
#include <string_view>
#include <vector>

namespace slikui::sdl {

// --- mapping -------------------------------------------------------------------

inline Key map_key(SDL_Keycode k) {
    switch (k) {
    case SDLK_RETURN:
    case SDLK_KP_ENTER: return Key::Enter;
    case SDLK_TAB: return Key::Tab;
    case SDLK_BACKSPACE: return Key::Backspace;
    case SDLK_DELETE: return Key::Delete;
    case SDLK_ESCAPE: return Key::Escape;
    case SDLK_SPACE: return Key::Space;
    case SDLK_APOSTROPHE: return Key::Apostrophe;
    case SDLK_COMMA: return Key::Comma;
    case SDLK_MINUS: return Key::Minus;
    case SDLK_PERIOD: return Key::Period;
    case SDLK_SLASH: return Key::Slash;
    case SDLK_EQUALS: return Key::Equal;
    case SDLK_LEFTBRACKET: return Key::LeftBracket;
    case SDLK_RIGHTBRACKET: return Key::RightBracket;
    case SDLK_BACKSLASH: return Key::Backslash;
    case SDLK_SEMICOLON: return Key::Semicolon;
    case SDLK_GRAVE: return Key::Grave;
    case SDLK_LEFT: return Key::Left;
    case SDLK_RIGHT: return Key::Right;
    case SDLK_UP: return Key::Up;
    case SDLK_DOWN: return Key::Down;
    case SDLK_HOME: return Key::Home;
    case SDLK_END: return Key::End;
    case SDLK_PAGEUP: return Key::PageUp;
    case SDLK_PAGEDOWN: return Key::PageDown;
    case SDLK_INSERT: return Key::Insert;
    case SDLK_CAPSLOCK: return Key::CapsLock;
    case SDLK_F1: return Key::F1;
    case SDLK_F2: return Key::F2;
    case SDLK_F3: return Key::F3;
    case SDLK_F4: return Key::F4;
    case SDLK_F5: return Key::F5;
    case SDLK_F6: return Key::F6;
    case SDLK_F7: return Key::F7;
    case SDLK_F8: return Key::F8;
    case SDLK_F9: return Key::F9;
    case SDLK_F10: return Key::F10;
    case SDLK_F11: return Key::F11;
    case SDLK_F12: return Key::F12;
    default: break;
    }
    // SDL3 keycodes for letters/digits are their (lowercase) characters.
    if (k >= SDLK_A && k <= SDLK_Z) return Key(int(Key::A) + int(k - SDLK_A));
    if (k >= SDLK_0 && k <= SDLK_9) return Key(int(Key::Num0) + int(k - SDLK_0));
    return Key::Unknown;
}

inline std::uint32_t map_mods(SDL_Keymod m) {
    std::uint32_t out = ModNone;
    if (m & SDL_KMOD_SHIFT) out |= ModShift;
    if (m & SDL_KMOD_CTRL) out |= ModCtrl;
    if (m & SDL_KMOD_ALT) out |= ModAlt;
    if (m & SDL_KMOD_GUI) out |= ModSuper; // Cmd / Windows key
    return out;
}

// SDL numbers buttons from 1: LEFT=1, MIDDLE=2, RIGHT=3 (X1=4, X2=5).
// slikui: MouseLeft=0, MouseMiddle=1, MouseRight=2; extra buttons -> 3, 4.
inline int map_button(Uint8 sdl_button) {
    switch (sdl_button) {
    case SDL_BUTTON_LEFT: return MouseLeft;
    case SDL_BUTTON_MIDDLE: return MouseMiddle;
    case SDL_BUTTON_RIGHT: return MouseRight;
    default: return int(sdl_button) - 1;
    }
}

// --- translation ------------------------------------------------------------------

// The SDL window an event belongs to (0 for global events such as QUIT).
inline SDL_WindowID event_window(const SDL_Event& e) {
    switch (e.type) {
    case SDL_EVENT_MOUSE_MOTION: return e.motion.windowID;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: return e.button.windowID;
    case SDL_EVENT_MOUSE_WHEEL: return e.wheel.windowID;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: return e.key.windowID;
    case SDL_EVENT_TEXT_INPUT: return e.text.windowID;
    default:
        if (e.type >= SDL_EVENT_WINDOW_FIRST && e.type <= SDL_EVENT_WINDOW_LAST)
            return e.window.windowID;
        return 0;
    }
}

// Appends the slikui events for `e` to `out` (TEXT_INPUT can yield several:
// one per codepoint). Returns false when slikui has no use for `e` - the
// caller's to handle (quit, gamepads, unmapped keys, ...).
inline bool translate(const SDL_Event& e, std::vector<Event>& out) {
    Event s{};
    switch (e.type) {
    case SDL_EVENT_MOUSE_MOTION:
        s.type = Event::Type::MouseMove;
        s.pos = {e.motion.x, e.motion.y};
        s.delta = {e.motion.xrel, e.motion.yrel};
        s.mods = map_mods(SDL_GetModState());
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        s.type = e.button.down ? Event::Type::MousePress : Event::Type::MouseRelease;
        s.pos = {e.button.x, e.button.y};
        s.button = map_button(e.button.button);
        s.mods = map_mods(SDL_GetModState());
        break;
    case SDL_EVENT_MOUSE_WHEEL: {
        float flip = e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.f : 1.f;
        s.type = Event::Type::Scroll;
        s.pos = {e.wheel.mouse_x, e.wheel.mouse_y};
        s.scroll = {e.wheel.x * flip, e.wheel.y * flip};
        s.mods = map_mods(SDL_GetModState());
        break;
    }
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        s.key = map_key(e.key.key);
        if (s.key == Key::Unknown) return false;
        s.type = e.key.down ? Event::Type::KeyPress : Event::Type::KeyRelease;
        s.repeat = e.key.down && e.key.repeat;
        s.mods = map_mods(e.key.mod);
        break;
    case SDL_EVENT_TEXT_INPUT: {
        if (!e.text.text) return false;
        std::string_view text(e.text.text);
        bool any = false;
        for (size_t i = 0; i < text.size();) {
            Event t{};
            t.type = Event::Type::TextInput;
            t.codepoint = utf8_next(text, i);
            out.push_back(t);
            any = true;
        }
        return any;
    }
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        s.type = Event::Type::FrameResize;
        s.pos = {float(e.window.data1), float(e.window.data2)};
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        s.type = Event::Type::FocusLost;
        break;
    default:
        return false;
    }
    out.push_back(s);
    return true;
}

// --- host services ------------------------------------------------------------------

// System clipboard + cursor shapes. SDL's clipboard and cursor are global,
// so no window is needed. Call once per Context.
inline void attach_services(Context& ctx) {
    ctx.set_clipboard_source([] {
        char* s = SDL_GetClipboardText(); // never null; "" when empty
        std::string out = s ? std::string(s) : std::string{};
        SDL_free(s);
        return out;
    });
    ctx.set_clipboard_sink([](std::string text) { SDL_SetClipboardText(text.c_str()); });
    // Cursor shapes -> SDL system cursors (created lazily, shared, never
    // destroyed: SDL_Quit frees them).
    struct Cursors {
        SDL_Cursor* map[7] = {};
        SDL_Cursor* get(SDL_SystemCursor shape, int slot) {
            if (!map[slot]) map[slot] = SDL_CreateSystemCursor(shape);
            return map[slot];
        }
    };
    static Cursors cursors;
    ctx.set_cursor_host([](slikui::Cursor c) {
        SDL_SystemCursor shape = SDL_SYSTEM_CURSOR_DEFAULT;
        int slot = 0;
        switch (c) {
        case slikui::Cursor::IBeam: shape = SDL_SYSTEM_CURSOR_TEXT; slot = 1; break;
        case slikui::Cursor::Hand: shape = SDL_SYSTEM_CURSOR_POINTER; slot = 2; break;
        case slikui::Cursor::Grab: shape = SDL_SYSTEM_CURSOR_MOVE; slot = 3; break;
        case slikui::Cursor::Crosshair: shape = SDL_SYSTEM_CURSOR_CROSSHAIR; slot = 4; break;
        case slikui::Cursor::ResizeH: shape = SDL_SYSTEM_CURSOR_EW_RESIZE; slot = 5; break;
        case slikui::Cursor::ResizeV: shape = SDL_SYSTEM_CURSOR_NS_RESIZE; slot = 6; break;
        default: break;
        }
        SDL_SetCursor(slot == 0 ? SDL_GetDefaultCursor() : cursors.get(shape, slot));
    });
}

// --- SdlInput: one window's events ------------------------------------------------------

class SdlInput {
public:
    // Starts SDL text input on `window` (SDL3 sends TEXT_INPUT only while it
    // is active) and stops it again on destruction.
    explicit SdlInput(SDL_Window* window)
        : window_(window), id_(SDL_GetWindowID(window)) {
        SDL_StartTextInput(window_);
    }
    ~SdlInput() {
        if (window_) SDL_StopTextInput(window_);
    }

    SdlInput(const SdlInput&) = delete;
    SdlInput& operator=(const SdlInput&) = delete;

    // Translate one polled event if it belongs to this window. Returns
    // whether slikui used it (false: another window's, or not UI input).
    bool process(const SDL_Event& e) {
        SDL_WindowID w = event_window(e);
        if (w != 0 && w != id_) return false;
        return translate(e, events_);
    }

    // Hand every queued event to `ctx`.
    void dispatch(Context& ctx) {
        std::vector<Event> batch;
        batch.swap(events_); // a handler may pump events re-entrantly
        for (const auto& e : batch) ctx.handle(e);
    }

    // Queued events, for hosts that route them themselves. Clear it after.
    std::vector<Event>& events() { return events_; }

    SDL_Window* window() const { return window_; }
    SDL_WindowID window_id() const { return id_; }

    // Detach without touching SDL - for when the window is already gone.
    void release_window() { window_ = nullptr; }

private:
    SDL_Window* window_;
    SDL_WindowID id_;
    std::vector<Event> events_;
};

} // namespace slikui::sdl

#else

// SDL3 not installed: slikui still builds; only this wrapper is unavailable.
namespace slikui::sdl {
// Intentionally empty.
}

#endif // SLIKUI_HAS_SDL3
