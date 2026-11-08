// slikui/widgets/icons.hpp — procedural icon set painted from DrawList
// primitives. No icon font, no assets: theme-tinted, resolution-independent,
// zero bytes shipped. Designed on a 24x24 grid and scaled to the target box.
#pragma once

#include "slikui/core/math.hpp"
#include "slikui/render/draw_list.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace slikui {

enum class Icon : std::uint8_t {
    Play, Pause, Stop, Record, StepForward, StepBack,
    Grid, Camera, Save, Folder, Refresh,
    Plus, Minus, Close, Check,
    ChevronLeft, ChevronRight, ChevronUp, ChevronDown,
    Eye, Gear, Search, Trash, Copy, Lock, Sun,
};

inline const char* icon_name(Icon icon) {
    switch (icon) {
    case Icon::Play: return "play";
    case Icon::Pause: return "pause";
    case Icon::Stop: return "stop";
    case Icon::Record: return "record";
    case Icon::StepForward: return "step_forward";
    case Icon::StepBack: return "step_back";
    case Icon::Grid: return "grid";
    case Icon::Camera: return "camera";
    case Icon::Save: return "save";
    case Icon::Folder: return "folder";
    case Icon::Refresh: return "refresh";
    case Icon::Plus: return "plus";
    case Icon::Minus: return "minus";
    case Icon::Close: return "close";
    case Icon::Check: return "check";
    case Icon::ChevronLeft: return "chevron_left";
    case Icon::ChevronRight: return "chevron_right";
    case Icon::ChevronUp: return "chevron_up";
    case Icon::ChevronDown: return "chevron_down";
    case Icon::Eye: return "eye";
    case Icon::Gear: return "gear";
    case Icon::Search: return "search";
    case Icon::Trash: return "trash";
    case Icon::Copy: return "copy";
    case Icon::Lock: return "lock";
    case Icon::Sun: return "sun";
    }
    return "unknown";
}

// Paint `icon` centered inside `area`, tinted `color`. `thickness` 0 picks a
// stroke that stays legible at small sizes.
inline void paint_icon(DrawList& list, Icon icon, Rect area, Color color,
                       float thickness = 0.f) {
    float side = std::fmin(area.w, area.h);
    float k = side / 24.f; // design grid -> px
    float t = thickness > 0 ? thickness : std::fmax(1.5f, 2.f * k);
    auto P = [&](float x, float y) { return Vec2{area.x + x * k, area.y + y * k}; };
    auto L = [&](float x1, float y1, float x2, float y2) {
        list.line(P(x1, y1), P(x2, y2), color, t);
    };
    auto F = [&](float x, float y, float w, float h, float rad = 0.f) {
        list.rect({area.x + x * k, area.y + y * k, w * k, h * k}, color, rad * k);
    };
    auto O = [&](float x, float y, float w, float h, float rad) {
        list.hollow_rect({area.x + x * k, area.y + y * k, w * k, h * k}, color, t,
                         rad * k);
    };
    auto C = [&](float cx, float cy, float r) { list.circle(P(cx, cy), r * k, color); };
    auto Ring = [&](float cx, float cy, float r) {
        list.hollow_rect(
            {area.x + (cx - r) * k, area.y + (cy - r) * k, 2 * r * k, 2 * r * k},
            color, t, r * k);
    };

    switch (icon) {
    case Icon::Play:
        L(8, 5, 18, 12); L(18, 12, 8, 19); L(8, 19, 8, 5);
        break;
    case Icon::Pause:
        L(9, 6, 9, 18); L(15, 6, 15, 18);
        break;
    case Icon::Stop:
        O(6.5f, 6.5f, 11, 11, 2);
        break;
    case Icon::Record:
        C(12, 12, 5);
        break;
    case Icon::StepForward:
        L(7, 6, 15, 12); L(15, 12, 7, 18); L(7, 18, 7, 6); L(18, 6, 18, 18);
        break;
    case Icon::StepBack:
        L(17, 6, 9, 12); L(9, 12, 17, 18); L(17, 18, 17, 6); L(6, 6, 6, 18);
        break;
    case Icon::Grid:
        L(9, 4, 9, 20); L(15, 4, 15, 20); L(4, 9, 20, 9); L(4, 15, 20, 15);
        break;
    case Icon::Camera:
        O(4, 8, 16, 11, 2.5f); O(9.5f, 5, 5, 3, 1); Ring(12, 13.5f, 3);
        break;
    case Icon::Save:
        O(5, 5, 14, 14, 2); F(8, 13, 8, 6, 1);
        L(8, 5, 8, 10); L(13, 5, 13, 10); L(8, 10, 13, 10);
        break;
    case Icon::Folder:
        O(4, 8, 16, 11, 2); L(4, 5, 10, 5); L(10, 5, 10, 8);
        break;
    case Icon::Refresh:
        Ring(12, 12, 7); L(16, 3, 20, 6); L(20, 6, 16, 9);
        break;
    case Icon::Plus:
        L(12, 6, 12, 18); L(6, 12, 18, 12);
        break;
    case Icon::Minus:
        L(6, 12, 18, 12);
        break;
    case Icon::Close:
        L(7, 7, 17, 17); L(17, 7, 7, 17);
        break;
    case Icon::Check:
        L(6, 12.5f, 10, 17); L(10, 17, 18, 7);
        break;
    case Icon::ChevronLeft:
        L(15, 6, 9, 12); L(9, 12, 15, 18);
        break;
    case Icon::ChevronRight:
        L(9, 6, 15, 12); L(15, 12, 9, 18);
        break;
    case Icon::ChevronUp:
        L(6, 15, 12, 9); L(12, 9, 18, 15);
        break;
    case Icon::ChevronDown:
        L(6, 9, 12, 15); L(12, 15, 18, 9);
        break;
    case Icon::Eye:
        Ring(12, 12, 6.5f); C(12, 12, 2.5f);
        break;
    case Icon::Gear:
        Ring(12, 12, 5);
        for (int i = 0; i < 8; ++i) {
            float a = 3.14159265f * float(i) / 4.f;
            L(12 + std::cos(a) * 7, 12 + std::sin(a) * 7,
              12 + std::cos(a) * 10, 12 + std::sin(a) * 10);
        }
        break;
    case Icon::Search:
        Ring(10.5f, 10.5f, 6); L(15, 15, 20, 20);
        break;
    case Icon::Trash:
        L(5, 7, 19, 7); O(7, 7, 10, 13, 1.5f); O(10, 4, 4, 3, 1);
        L(10, 10, 10, 17); L(14, 10, 14, 17);
        break;
    case Icon::Copy:
        O(9, 9, 10, 10, 2); L(5, 15, 5, 7); L(5, 7, 15, 7);
        break;
    case Icon::Lock:
        O(6, 11, 12, 9, 2); L(9, 11, 9, 8); L(9, 8, 15, 8); L(15, 8, 15, 11);
        F(11, 14, 2, 3, 1);
        break;
    case Icon::Sun:
        C(12, 12, 4);
        for (int i = 0; i < 8; ++i) {
            float a = 3.14159265f * float(i) / 4.f;
            L(12 + std::cos(a) * 7, 12 + std::sin(a) * 7,
              12 + std::cos(a) * 9.5f, 12 + std::sin(a) * 9.5f);
        }
        break;
    }
}

} // namespace slikui
