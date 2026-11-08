// slikui/core/input.hpp — platform-agnostic input events.
//
// The window/backend layer translates its native events into slikui::Event and
// feeds them to Context::handle(). Widgets see exactly one event vocabulary
// regardless of the underlying API (GLFW, SDL, custom, headless tests).
#pragma once

#include "math.hpp"

#include <cstdint>

namespace slikui {

enum class Key : std::uint32_t {
    Unknown = 0,
    Enter, Tab, Backspace, Delete, Escape,
    Space, Apostrophe, Comma, Minus, Period, Slash,
    Left, Right, Up, Down,
    Home, End, PageUp, PageDown,
    Insert, CapsLock,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
};

enum Mod : std::uint32_t {
    ModNone   = 0,
    ModShift  = 1 << 0,
    ModCtrl   = 1 << 1,
    ModAlt    = 1 << 2,
    ModSuper  = 1 << 3,
};

enum MouseButton : int {
    MouseLeft = 0, MouseMiddle = 1, MouseRight = 2,
};

// One flat event struct: cheap to copy, trivially exhaustive.
struct Event {
    enum class Type : std::uint8_t {
        MouseMove,      // pos, delta
        MousePress,     // pos, button
        MouseRelease,   // pos, button
        Scroll,         // scroll
        KeyPress,       // key, mods
        KeyRelease,     // key, mods
        TextInput,      // codepoint (Unicode)
        FocusLost,      // app focus lost: cancel drags/hover
        FrameResize,    // framebuffer_size updated
    };

    Type type;
    Vec2 pos = Vec2::zero();          // mouse position (framebuffer px)
    Vec2 delta = Vec2::zero();        // mouse delta this frame
    Vec2 scroll = Vec2::zero();       // scroll ticks
    int button = -1;                  // MouseButton for mouse press/release
    Key key = Key::Unknown;           // key for key events
    std::uint32_t codepoint = 0;      // Unicode scalar for TextInput
    std::uint32_t mods = ModNone;

    bool shift() const { return mods & ModShift; }
    bool ctrl()  const { return mods & ModCtrl; }
    bool alt()   const { return mods & ModAlt; }
    bool super() const { return mods & ModSuper; }
    // Copy/paste chords use Ctrl everywhere and Cmd on macOS.
    bool shortcut_mod() const { return mods & (ModCtrl | ModSuper); }
};

// Mouse cursor shapes a widget can request while hovered. The host renders
// them (GLFW standard cursors via GlfwWindow::attach; headless apps can
// ignore the callback).
enum class Cursor : std::uint8_t {
    Arrow,     // default
    IBeam,     // text fields
    Hand,      // clickable
    Grab,      // panning / dragging (maps to hand where unavailable)
    Crosshair, // precise picking
    ResizeH,   // horizontal resize (↔)
    ResizeV,   // vertical resize (↕)
};

} // namespace slikui
