// slikui/widgets/icons.hpp - procedural icon set painted from DrawList
// primitives. No icon font, no assets: theme-tinted, resolution-independent,
// zero bytes shipped. Designed on a 24x24 grid and scaled to the target box.
#pragma once

#include "slikui/core/math.hpp"
#include "slikui/render/draw_list.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>

namespace slikui {

// The icon list: X(EnumName, "snake_name"). Enum values follow this order, so
// new icons go at the end (existing values stay stable). The Python enum and
// icon_name() are generated from it; paint_icon() needs a case per entry.
#define SLIKUI_ICONS(X)                                                       \
    /* media */                                                                \
    X(Play, "play") X(Pause, "pause") X(Stop, "stop") X(Record, "record")      \
    X(StepForward, "step_forward") X(StepBack, "step_back")                    \
    /* tools, files */                                                         \
    X(Grid, "grid") X(Camera, "camera") X(Save, "save") X(Folder, "folder")    \
    X(Refresh, "refresh")                                                      \
    /* actions, navigation */                                                  \
    X(Plus, "plus") X(Minus, "minus") X(Close, "close") X(Check, "check")      \
    X(ChevronLeft, "chevron_left") X(ChevronRight, "chevron_right")            \
    X(ChevronUp, "chevron_up") X(ChevronDown, "chevron_down")                  \
    /* misc */                                                                 \
    X(Eye, "eye") X(Gear, "gear") X(Search, "search") X(Trash, "trash")        \
    X(Copy, "copy") X(Lock, "lock") X(Sun, "sun")                              \
    /* navigation */                                                           \
    X(ArrowLeft, "arrow_left") X(ArrowRight, "arrow_right")                    \
    X(ArrowUp, "arrow_up") X(ArrowDown, "arrow_down") X(Home, "home")          \
    X(Menu, "menu") X(MoreHorizontal, "more_horizontal")                       \
    X(MoreVertical, "more_vertical") X(ExternalLink, "external_link")          \
    X(Maximize, "maximize") X(Minimize, "minimize")                            \
    /* editing */                                                              \
    X(Edit, "edit") X(Cut, "cut") X(Paste, "paste") X(Undo, "undo")            \
    X(Redo, "redo") X(Link, "link") X(Filter, "filter") X(Sort, "sort")        \
    X(DragHandle, "drag_handle")                                               \
    /* files */                                                                \
    X(File, "file") X(FolderOpen, "folder_open") X(Download, "download")       \
    X(Upload, "upload") X(Share, "share") X(Print, "print")                    \
    /* view */                                                                 \
    X(ZoomIn, "zoom_in") X(ZoomOut, "zoom_out") X(List, "list")                \
    X(EyeOff, "eye_off") X(Image, "image") X(Sidebar, "sidebar")               \
    X(Layers, "layers")                                                        \
    /* status */                                                               \
    X(Info, "info") X(Warning, "warning") X(Error, "error") X(Help, "help")    \
    X(Bell, "bell")                                                            \
    /* marking */                                                              \
    X(Star, "star") X(Heart, "heart") X(Pin, "pin") X(Bookmark, "bookmark")    \
    X(Flag, "flag") X(Tag, "tag")                                              \
    /* media */                                                                \
    X(FastForward, "fast_forward") X(Rewind, "rewind") X(Repeat, "repeat")     \
    X(Shuffle, "shuffle") X(Volume, "volume") X(Mute, "mute") X(Mic, "mic")    \
    X(Music, "music")                                                          \
    /* misc */                                                                 \
    X(Unlock, "unlock") X(Moon, "moon") X(User, "user") X(Mail, "mail")        \
    X(Calendar, "calendar") X(Clock, "clock") X(Globe, "globe")                \
    X(Power, "power") X(Code, "code") X(Terminal, "terminal")                 \
    /* 3D view manipulation */                                                 \
    X(Pan, "pan") X(RotateX, "rotate_x") X(RotateY, "rotate_y")                \
    X(RotateZ, "rotate_z") X(RotateFree, "rotate_free")                       \
    /* 3D editing */                                                           \
    X(Vertex, "vertex") X(Edge, "edge") X(Face, "face") X(Cube, "cube")        \
    X(Sphere, "sphere") X(Cylinder, "cylinder") X(Cone, "cone")                \
    X(Plane, "plane") X(Mesh, "mesh") X(Wireframe, "wireframe")                \
    X(Solid, "solid") X(Texture, "texture") X(Material, "material")            \
    X(UV, "uv") X(Normal, "normal") X(Modifier, "modifier") X(Light, "light")  \
    X(Bone, "bone") X(Keyframe, "keyframe") X(Curve, "curve")                  \
    X(Extrude, "extrude") X(Scale, "scale") X(Axes, "axes") X(Snap, "snap")    \
    X(Pivot, "pivot") X(Hierarchy, "hierarchy") X(Torus, "torus")             \
    /* painting */                                                             \
    X(Brush, "brush") X(Eraser, "eraser") X(Fill, "fill")                      \
    X(Eyedropper, "eyedropper") X(Airbrush, "airbrush") X(Select, "select")    \
    X(Lasso, "lasso") X(MagicWand, "magic_wand") X(Crop, "crop")               \
    X(Gradient, "gradient") X(Palette, "palette") X(Text, "text")              \
    X(Stamp, "stamp") X(Blur, "blur")                                          \
    /* vector drawing */                                                       \
    X(Pointer, "pointer") X(DirectSelect, "direct_select") X(Pen, "pen")       \
    X(Rectangle, "rectangle") X(Ellipse, "ellipse") X(Polygon, "polygon")      \
    X(Line, "line") X(Union, "union") X(Subtract, "subtract")                  \
    X(Intersect, "intersect") X(AlignLeft, "align_left")                       \
    X(AlignCenter, "align_center") X(AlignRight, "align_right")                \
    X(AlignTop, "align_top") X(AlignMiddle, "align_middle")                    \
    X(AlignBottom, "align_bottom") X(Group, "group") X(Ungroup, "ungroup")     \
    X(BringForward, "bring_forward") X(SendBackward, "send_backward")          \
    X(FlipH, "flip_h") X(FlipV, "flip_v")

enum class Icon : std::uint8_t {
#define SLIKUI_ICON_ENUM_(name, str) name,
    SLIKUI_ICONS(SLIKUI_ICON_ENUM_)
#undef SLIKUI_ICON_ENUM_
};

// Number of icons; every value in [0, icon_count) is a valid Icon.
inline constexpr int icon_count = 0
#define SLIKUI_ICON_COUNT_(name, str) +1
    SLIKUI_ICONS(SLIKUI_ICON_COUNT_)
#undef SLIKUI_ICON_COUNT_
    ;

inline const char* icon_name(Icon icon) {
    switch (icon) {
#define SLIKUI_ICON_NAME_(name, str) case Icon::name: return str;
        SLIKUI_ICONS(SLIKUI_ICON_NAME_)
#undef SLIKUI_ICON_NAME_
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
    // Open polyline through grid points (repeat the first point to close).
    auto PL = [&](std::initializer_list<Vec2> pts) {
        const Vec2* prev = nullptr;
        for (const Vec2& p : pts) {
            if (prev) L(prev->x, prev->y, p.x, p.y);
            prev = &p;
        }
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
    // Elliptical arc from a0 to a1 degrees (0 = +x, 90 = down, increasing
    // clockwise on screen), as short segments.
    auto A = [&](float cx, float cy, float rx, float ry, float a0, float a1) {
        int n = std::max(3, int(std::ceil(std::fabs(a1 - a0) / 12.f)));
        constexpr float deg = 3.14159265f / 180.f;
        float px = cx + std::cos(a0 * deg) * rx, py = cy + std::sin(a0 * deg) * ry;
        for (int i = 1; i <= n; ++i) {
            float a = (a0 + (a1 - a0) * float(i) / float(n)) * deg;
            float x = cx + std::cos(a) * rx, y = cy + std::sin(a) * ry;
            L(px, py, x, y);
            px = x;
            py = y;
        }
    };
    auto Arc = [&](float cx, float cy, float r, float a0, float a1) { A(cx, cy, r, r, a0, a1); };
    // Heavier stroke, for the highlighted part of an icon (Edge).
    auto LT = [&](float x1, float y1, float x2, float y2) {
        list.line(P(x1, y1), P(x2, y2), color, t * 2.f);
    };
    // Dashed line (marquees, guides). Round caps lengthen each dash by the
    // stroke width, so the drawn dashes are short and the spacing wide.
    auto DL = [&](float x1, float y1, float x2, float y2) {
        float dx = x2 - x1, dy = y2 - y1, len = std::sqrt(dx * dx + dy * dy);
        if (len <= 0) return;
        for (float d = 0; d < len; d += 4.f) {
            float e = std::fmin(d + .6f, len);
            L(x1 + dx * d / len, y1 + dy * d / len, x1 + dx * e / len, y1 + dy * e / len);
        }
    };
    auto DRect = [&](float x0, float y0, float x1, float y1) {
        DL(x0, y0, x1, y0); DL(x1, y0, x1, y1); DL(x1, y1, x0, y1); DL(x0, y1, x0, y0);
    };
    // Cube in oblique projection: front face (4,9)-(15,20), back face offset
    // by (+5,-5). Hidden edges only with `hidden`.
    auto CubeOutline = [&](bool hidden) {
        PL({{4, 9}, {15, 9}, {15, 20}, {4, 20}, {4, 9}});
        PL({{4, 9}, {9, 4}, {20, 4}, {20, 15}, {15, 20}});
        L(15, 9, 20, 4);
        if (hidden) { L(9, 4, 9, 15); L(9, 15, 4, 20); L(9, 15, 20, 15); }
    };
    // Elliptical arc with an arrowhead at a1, pointing along the direction
    // of travel.
    auto ArcArrow = [&](float cx, float cy, float rx, float ry, float a0, float a1) {
        A(cx, cy, rx, ry, a0, a1);
        constexpr float deg = 3.14159265f / 180.f;
        float a = a1 * deg, dir = a1 >= a0 ? 1.f : -1.f;
        float tx = cx + std::cos(a) * rx, ty = cy + std::sin(a) * ry;
        float dx = -std::sin(a) * rx * dir, dy = std::cos(a) * ry * dir;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len <= 0) return;
        dx /= len;
        dy /= len;
        constexpr float head = 3.5f, c = 0.7071f; // 45-degree barbs
        L(tx, ty, tx - head * (dx * c - dy * c), ty - head * (dy * c + dx * c));
        L(tx, ty, tx - head * (dx * c + dy * c), ty - head * (dy * c - dx * c));
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
    case Icon::Folder: // outline with the tab on top
        PL({{3, 5}, {9, 5}, {11, 8}, {21, 8}, {21, 19}, {3, 19}, {3, 5}});
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
    case Icon::Trash: // lid with a handle over a tapered bin
        L(3.5f, 6, 20.5f, 6); PL({{9, 6}, {9, 3.5f}, {15, 3.5f}, {15, 6}});
        PL({{5.5f, 6}, {7, 20.5f}, {17, 20.5f}, {18.5f, 6}});
        L(10, 10, 10, 16.5f); L(14, 10, 14, 16.5f);
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

    // --- navigation ---
    case Icon::ArrowLeft:
        L(19, 12, 5, 12); L(11, 6, 5, 12); L(5, 12, 11, 18);
        break;
    case Icon::ArrowRight:
        L(5, 12, 19, 12); L(13, 6, 19, 12); L(19, 12, 13, 18);
        break;
    case Icon::ArrowUp:
        L(12, 19, 12, 5); L(6, 11, 12, 5); L(12, 5, 18, 11);
        break;
    case Icon::ArrowDown:
        L(12, 5, 12, 19); L(6, 13, 12, 19); L(12, 19, 18, 13);
        break;
    case Icon::Home:
        PL({{3.5f, 11.5f}, {12, 4}, {20.5f, 11.5f}});
        PL({{6, 10}, {6, 20}, {18, 20}, {18, 10}});
        PL({{10, 20}, {10, 15}, {14, 15}, {14, 20}});
        break;
    case Icon::Menu:
        L(5, 7, 19, 7); L(5, 12, 19, 12); L(5, 17, 19, 17);
        break;
    case Icon::MoreHorizontal:
        C(6, 12, 1.8f); C(12, 12, 1.8f); C(18, 12, 1.8f);
        break;
    case Icon::MoreVertical:
        C(12, 6, 1.8f); C(12, 12, 1.8f); C(12, 18, 1.8f);
        break;
    case Icon::ExternalLink:
        PL({{11, 5}, {5, 5}, {5, 19}, {19, 19}, {19, 13}});
        L(11, 13, 19, 5); L(13.5f, 5, 19, 5); L(19, 5, 19, 10.5f);
        break;
    case Icon::Maximize:
        PL({{4, 9}, {4, 4}, {9, 4}});    PL({{15, 4}, {20, 4}, {20, 9}});
        PL({{20, 15}, {20, 20}, {15, 20}}); PL({{9, 20}, {4, 20}, {4, 15}});
        break;
    case Icon::Minimize:
        PL({{9, 4}, {9, 9}, {4, 9}});    PL({{15, 4}, {15, 9}, {20, 9}});
        PL({{20, 15}, {15, 15}, {15, 20}}); PL({{9, 20}, {9, 15}, {4, 15}});
        break;

    // --- editing ---
    case Icon::Edit: // pencil
        PL({{5, 19}, {5.5f, 15.5f}, {15.5f, 5.5f}, {18.5f, 8.5f}, {8.5f, 18.5f}, {5, 19}});
        L(13, 8, 16, 11);
        break;
    case Icon::Cut: // scissors
        Ring(7, 17, 2.8f); Ring(17, 17, 2.8f);
        L(9, 15, 17, 4); L(15, 15, 7, 4);
        break;
    case Icon::Paste: // clipboard
        O(5.5f, 5, 13, 16, 2); F(9, 3, 6, 4, 1.2f);
        L(9, 12, 15, 12); L(9, 16, 15, 16);
        break;
    case Icon::Undo:
        Arc(13.5f, 13.5f, 5.5f, -90, 90);
        L(13.5f, 8, 5, 8); L(13.5f, 19, 9, 19);
        L(5, 8, 8.5f, 4.5f); L(5, 8, 8.5f, 11.5f);
        break;
    case Icon::Redo:
        Arc(10.5f, 13.5f, 5.5f, 90, 270);
        L(10.5f, 8, 19, 8); L(10.5f, 19, 15, 19);
        L(19, 8, 15.5f, 4.5f); L(19, 8, 15.5f, 11.5f);
        break;
    case Icon::Link:
        O(2, 8.5f, 9.5f, 7, 3.5f); O(12.5f, 8.5f, 9.5f, 7, 3.5f); L(8, 12, 16, 12);
        break;
    case Icon::Filter: // funnel
        PL({{4, 5}, {20, 5}, {14, 12}, {14, 19}, {10, 17}, {10, 12}, {4, 5}});
        break;
    case Icon::Sort: // up + down arrows
        L(8, 5, 8, 19); L(4, 9, 8, 5); L(8, 5, 12, 9);
        L(16, 5, 16, 19); L(12, 15, 16, 19); L(16, 19, 20, 15);
        break;
    case Icon::DragHandle: // 2x3 grip dots
        for (int r = 0; r < 3; ++r) {
            C(9, 6 + 6 * float(r), 1.6f);
            C(15, 6 + 6 * float(r), 1.6f);
        }
        break;

    // --- files ---
    case Icon::File: // page with a folded corner
        PL({{6, 3.5f}, {14, 3.5f}, {18.5f, 8}, {18.5f, 20.5f}, {6, 20.5f}, {6, 3.5f}});
        PL({{14, 3.5f}, {14, 8}, {18.5f, 8}});
        break;
    case Icon::FolderOpen:
        PL({{4, 19}, {4, 5}, {9, 5}, {11, 8}, {17.5f, 8}, {17.5f, 11}});
        PL({{4, 19}, {7, 11}, {21, 11}, {18, 19}, {4, 19}});
        break;
    case Icon::Download:
        L(12, 4, 12, 15); L(7, 10, 12, 15); L(12, 15, 17, 10);
        PL({{5, 15}, {5, 20}, {19, 20}, {19, 15}});
        break;
    case Icon::Upload:
        L(12, 15, 12, 4); L(7, 9, 12, 4); L(12, 4, 17, 9);
        PL({{5, 15}, {5, 20}, {19, 20}, {19, 15}});
        break;
    case Icon::Share: // three connected nodes
        L(7, 12, 17, 6); L(7, 12, 17, 18);
        C(17, 6, 2.6f); C(7, 12, 2.6f); C(17, 18, 2.6f);
        break;
    case Icon::Print:
        PL({{8, 9}, {8, 4}, {16, 4}, {16, 9}});
        PL({{8, 17}, {4, 17}, {4, 9}, {20, 9}, {20, 17}, {16, 17}});
        PL({{8, 14}, {16, 14}, {16, 20}, {8, 20}, {8, 14}});
        break;

    // --- view ---
    case Icon::ZoomIn:
        Ring(10.5f, 10.5f, 6); L(15, 15, 20, 20);
        L(10.5f, 8, 10.5f, 13); L(8, 10.5f, 13, 10.5f);
        break;
    case Icon::ZoomOut:
        Ring(10.5f, 10.5f, 6); L(15, 15, 20, 20);
        L(8, 10.5f, 13, 10.5f);
        break;
    case Icon::List: // bulleted lines
        for (int r = 0; r < 3; ++r) {
            float y = 7 + 5 * float(r);
            C(5.5f, y, 1.4f);
            L(9, y, 19, y);
        }
        break;
    case Icon::EyeOff:
        Ring(12, 12, 6.5f); C(12, 12, 2.5f); L(4, 4, 20, 20);
        break;
    case Icon::Image: // landscape: sun + mountains in a frame
        O(4, 5, 16, 14, 2); C(9, 9.5f, 1.8f);
        PL({{5, 17.5f}, {10, 12.5f}, {13, 15.5f}, {15, 13.5f}, {19, 17.5f}});
        break;
    case Icon::Sidebar:
        O(4, 5, 16, 14, 2); L(9.5f, 5, 9.5f, 19);
        break;
    case Icon::Layers: // three stacked sheets
        PL({{12, 4}, {20, 8.5f}, {12, 13}, {4, 8.5f}, {12, 4}});
        PL({{4, 12.5f}, {12, 17}, {20, 12.5f}});
        PL({{4, 16.5f}, {12, 21}, {20, 16.5f}});
        break;

    // --- status ---
    case Icon::Info:
        Ring(12, 12, 9); L(12, 11, 12, 16.5f); C(12, 7.8f, 1.3f);
        break;
    case Icon::Warning: // triangle with !
        PL({{12, 4}, {21, 19.5f}, {3, 19.5f}, {12, 4}});
        L(12, 9.5f, 12, 14); C(12, 16.8f, 1.2f);
        break;
    case Icon::Error: // circle with x
        Ring(12, 12, 9); L(9, 9, 15, 15); L(15, 9, 9, 15);
        break;
    case Icon::Help: // circle with ?
        Ring(12, 12, 9);
        Arc(12, 9.8f, 2.8f, 180, 400);
        L(14.15f, 11.6f, 12, 13); L(12, 13, 12, 14);
        C(12, 16.8f, 1.2f);
        break;
    case Icon::Bell:
        Arc(12, 11, 5.5f, 180, 360);
        L(6.5f, 11, 6.5f, 15); L(17.5f, 11, 17.5f, 15);
        L(6.5f, 15, 4.5f, 17); L(17.5f, 15, 19.5f, 17); L(4.5f, 17, 19.5f, 17);
        L(12, 3.5f, 12, 5.5f); C(12, 19.5f, 1.7f);
        break;

    // --- marking ---
    case Icon::Star: { // five-point outline
        constexpr float deg = 3.14159265f / 180.f;
        float px = 0, py = 0;
        for (int i = 0; i <= 10; ++i) {
            float r = (i % 2) ? 3.9f : 9.f;
            float a = (-90.f + 36.f * float(i)) * deg;
            float x = 12 + std::cos(a) * r, y = 12.8f + std::sin(a) * r;
            if (i) L(px, py, x, y);
            px = x;
            py = y;
        }
        break;
    }
    case Icon::Heart:
        Arc(8.3f, 9.5f, 3.8f, 135, 360); Arc(15.7f, 9.5f, 3.8f, 180, 405);
        L(5.6f, 12.2f, 12, 19); L(18.4f, 12.2f, 12, 19);
        break;
    case Icon::Pin: // pushpin
        L(8.5f, 4, 15.5f, 4);
        PL({{10, 4}, {10, 10}, {6.5f, 14}, {17.5f, 14}, {14, 10}, {14, 4}});
        L(12, 14, 12, 21);
        break;
    case Icon::Bookmark:
        PL({{6, 3.5f}, {18, 3.5f}, {18, 20.5f}, {12, 15.5f}, {6, 20.5f}, {6, 3.5f}});
        break;
    case Icon::Flag:
        L(5, 3.5f, 5, 21);
        PL({{5, 4.5f}, {19, 4.5f}, {15.5f, 9}, {19, 13.5f}, {5, 13.5f}});
        break;
    case Icon::Tag:
        PL({{4, 4}, {12, 4}, {20.5f, 12.5f}, {12.5f, 20.5f}, {4, 12}, {4, 4}});
        C(8.3f, 8.3f, 1.5f);
        break;

    // --- media ---
    case Icon::FastForward:
        PL({{4, 6}, {11.5f, 12}, {4, 18}, {4, 6}});
        PL({{12.5f, 6}, {20, 12}, {12.5f, 18}, {12.5f, 6}});
        break;
    case Icon::Rewind:
        PL({{20, 6}, {12.5f, 12}, {20, 18}, {20, 6}});
        PL({{11.5f, 6}, {4, 12}, {11.5f, 18}, {11.5f, 6}});
        break;
    case Icon::Repeat:
        PL({{5, 11}, {5, 7}, {18, 7}}); L(15, 4, 18, 7); L(18, 7, 15, 10);
        PL({{19, 13}, {19, 17}, {6, 17}}); L(9, 14, 6, 17); L(6, 17, 9, 20);
        break;
    case Icon::Shuffle:
        PL({{4, 17}, {8, 17}, {16, 7}, {20, 7}}); L(17, 4, 20, 7); L(20, 7, 17, 10);
        PL({{4, 7}, {8, 7}, {16, 17}, {20, 17}}); L(17, 14, 20, 17); L(20, 17, 17, 20);
        break;
    case Icon::Volume:
        PL({{4, 9.5f}, {8, 9.5f}, {12.5f, 5}, {12.5f, 19}, {8, 14.5f}, {4, 14.5f}, {4, 9.5f}});
        Arc(13, 12, 3.8f, -50, 50); Arc(13, 12, 7.5f, -50, 50);
        break;
    case Icon::Mute:
        PL({{4, 9.5f}, {8, 9.5f}, {12.5f, 5}, {12.5f, 19}, {8, 14.5f}, {4, 14.5f}, {4, 9.5f}});
        L(15.5f, 9.5f, 20.5f, 14.5f); L(20.5f, 9.5f, 15.5f, 14.5f);
        break;
    case Icon::Mic:
        O(9, 3.5f, 6, 11, 3);
        Arc(12, 11, 6, 0, 180);
        L(12, 17, 12, 20.5f); L(8.5f, 20.5f, 15.5f, 20.5f);
        break;
    case Icon::Music: // two beamed eighth notes
        C(7.5f, 17.5f, 2.6f); C(16.5f, 15.5f, 2.6f);
        L(9.9f, 17.5f, 9.9f, 6); L(18.9f, 15.5f, 18.9f, 4);
        L(9.9f, 6, 18.9f, 4); L(9.9f, 8.5f, 18.9f, 6.5f);
        break;

    // --- misc ---
    case Icon::Unlock:
        O(6, 11, 12, 9, 2); PL({{9, 11}, {9, 6}, {15, 6}, {15, 8}});
        F(11, 14, 2, 3, 1);
        break;
    case Icon::Moon: // crescent: outer arc + the bite's inner arc
        Arc(12, 12, 8.5f, 6.9f, 269.2f);
        Arc(17, 7.5f, 6.5f, 58.1f, 218.0f);
        break;
    case Icon::User:
        Ring(12, 8, 4); Arc(12, 21, 7.5f, 180, 360);
        break;
    case Icon::Mail:
        O(3.5f, 6, 17, 12, 2); PL({{4, 7}, {12, 13}, {20, 7}});
        break;
    case Icon::Calendar:
        O(4, 5, 16, 15, 2); L(4, 10, 20, 10); L(8, 3, 8, 7); L(16, 3, 16, 7);
        break;
    case Icon::Clock:
        Ring(12, 12, 9); L(12, 7, 12, 12); L(12, 12, 15.5f, 14);
        break;
    case Icon::Globe:
        Ring(12, 12, 9); L(3, 12, 21, 12);
        A(12, 12, 4, 9, -90, 270);
        break;
    case Icon::Power:
        Arc(12, 13, 7.5f, -55, 235); L(12, 3.5f, 12, 12);
        break;
    case Icon::Code: // < / >
        PL({{8, 7}, {3, 12}, {8, 17}}); PL({{16, 7}, {21, 12}, {16, 17}});
        L(14, 5, 10, 19);
        break;
    case Icon::Terminal:
        O(3, 4.5f, 18, 15, 2); PL({{7, 9}, {10, 12}, {7, 15}}); L(12, 15, 17, 15);
        break;

    // --- 3D view manipulation ---
    case Icon::Pan: // four-way arrows
        L(12, 3, 12, 21); L(3, 12, 21, 12);
        L(9, 6, 12, 3); L(12, 3, 15, 6);     L(9, 18, 12, 21); L(12, 21, 15, 18);
        L(6, 9, 3, 12); L(3, 12, 6, 15);     L(18, 9, 21, 12); L(21, 12, 18, 15);
        break;
    case Icon::RotateX: // arrowed ring around a horizontal axis
        L(2.5f, 12, 21.5f, 12);
        ArcArrow(12, 12, 4.5f, 8.5f, -50, 235);
        break;
    case Icon::RotateY: // arrowed ring around a vertical axis
        L(12, 2.5f, 12, 21.5f);
        ArcArrow(12, 12, 8.5f, 4.5f, 40, 325);
        break;
    case Icon::RotateZ: // arrowed circle around the axis facing the viewer
        C(12, 12, 1.8f);
        ArcArrow(12, 12, 8, 8, -50, 235);
        break;
    case Icon::RotateFree: // sphere with an orbit arrow: rotate about any axis
        Ring(12, 12, 7);
        ArcArrow(12, 12, 10, 4.5f, 190, 0); // front of the orbit, heading up
        break;

    // --- 3D editing ---
    case Icon::Vertex: // cube with one vertex picked out
        CubeOutline(false); C(15, 9, 3);
        break;
    case Icon::Edge: // cube with one edge picked out
        CubeOutline(false); LT(15, 9, 20, 4);
        break;
    case Icon::Face: // cube with the front face filled
        CubeOutline(false); F(4, 9, 11, 11);
        break;
    case Icon::Cube:
        CubeOutline(false);
        break;
    case Icon::Sphere: // outline + front half of the equator
        Ring(12, 12, 8.5f); A(12, 12, 8.5f, 3, 0, 180);
        break;
    case Icon::Cylinder:
        A(12, 6, 7, 2.8f, 0, 360); A(12, 18, 7, 2.8f, 0, 180);
        L(5, 6, 5, 18); L(19, 6, 19, 18);
        break;
    case Icon::Cone:
        L(12, 3.5f, 5, 18); L(12, 3.5f, 19, 18); A(12, 18, 7, 2.8f, 0, 180);
        break;
    case Icon::Plane: // a flat quad in perspective
        PL({{3, 17}, {15, 17}, {21, 8}, {9, 8}, {3, 17}});
        break;
    case Icon::Mesh: // triangulated patch with its vertices
        O(3, 3, 18, 18, 0); L(4, 12, 20, 12); L(12, 4, 12, 20);
        L(4, 12, 12, 4); L(12, 12, 20, 4); L(4, 20, 12, 12); L(12, 20, 20, 12);
        break;
    case Icon::Wireframe: // cube with its hidden edges
        CubeOutline(true);
        break;
    case Icon::Solid: // solid shading: a filled ball
        C(12, 12, 8.5f);
        break;
    case Icon::Texture: // checkerboard
        O(4, 4, 16, 16, 1.5f); F(4, 4, 8, 8); F(12, 12, 8, 8);
        break;
    case Icon::Material: // glossy ball: outline + specular highlight
        Ring(12, 12, 8.5f); Arc(12, 12, 5, 195, 255);
        break;
    case Icon::UV: // the letters U and V
        L(4, 6, 4, 14); Arc(7.5f, 14, 3.5f, 180, 0); L(11, 14, 11, 6);
        PL({{13, 6}, {16.5f, 17.5f}, {20, 6}});
        break;
    case Icon::Normal: // surface with a perpendicular arrow
        L(3, 19, 21, 19);
        L(12, 19, 12, 5); L(8.5f, 8.5f, 12, 5); L(12, 5, 15.5f, 8.5f);
        break;
    case Icon::Modifier: // wrench
        Arc(16, 8, 4.5f, 30, 260);
        LT(12.8f, 11.2f, 5.5f, 18.5f);
        break;
    case Icon::Light: // light bulb
        Arc(12, 10, 6, 135, 405);
        L(7.8f, 14.2f, 9.5f, 17); L(16.2f, 14.2f, 14.5f, 17);
        L(9.5f, 17, 14.5f, 17); L(10, 20, 14, 20);
        break;
    case Icon::Bone: // octahedral armature bone
        PL({{5, 19}, {8, 12}, {19, 5}, {12, 16}, {5, 19}}); L(8, 12, 12, 16);
        break;
    case Icon::Keyframe: // diamond
        PL({{12, 4}, {20, 12}, {12, 20}, {4, 12}, {12, 4}}); C(12, 12, 2);
        break;
    case Icon::Curve: // curve with a tangent handle at its node
        Arc(12, 21, 9, 195, 345);
        L(4.5f, 12, 19.5f, 12); C(4.5f, 12, 1.9f); C(19.5f, 12, 1.9f);
        F(10.25f, 10.25f, 3.5f, 3.5f, .5f);
        break;
    case Icon::Extrude: // face with an arrow pulled out of it
        PL({{3, 19}, {15, 19}, {21, 14}, {9, 14}, {3, 19}});
        L(12, 16.5f, 12, 4); L(8.5f, 7.5f, 12, 4); L(12, 4, 15.5f, 7.5f);
        break;
    case Icon::Scale: // small box growing toward a larger outline
        O(4, 12, 8, 8, 1); L(11, 13, 19, 5); PL({{13.5f, 5}, {19, 5}, {19, 10.5f}});
        PL({{4, 9}, {4, 4}, {9, 4}}); PL({{20, 15}, {20, 20}, {15, 20}});
        break;
    case Icon::Axes: // X right, Y up, Z toward the viewer
        L(7, 17, 20, 17); L(17, 14, 20, 17); L(20, 17, 17, 20);
        L(7, 17, 7, 4); L(4, 7, 7, 4); L(7, 4, 10, 7);
        L(7, 17, 3, 21);
        break;
    case Icon::Snap: // horseshoe magnet
        Arc(12, 12, 6, 0, 180); L(6, 12, 6, 6); L(18, 12, 18, 6);
        F(4, 3.5f, 4, 3.5f, .5f); F(16, 3.5f, 4, 3.5f, .5f);
        break;
    case Icon::Pivot: // crosshair on the pivot point
        Ring(12, 12, 5); C(12, 12, 1.4f);
        L(12, 3, 12, 7); L(12, 17, 12, 21); L(3, 12, 7, 12); L(17, 12, 21, 12);
        break;
    case Icon::Hierarchy: // parent node with two children
        O(4, 3.5f, 7, 5, 1); L(7.5f, 8.5f, 7.5f, 18.5f);
        L(7.5f, 12, 13, 12); L(7.5f, 18.5f, 13, 18.5f);
        O(13, 9.5f, 7, 5, 1); O(13, 16, 7, 5, 1);
        break;
    case Icon::Torus: // ring outline with the hole's two edges
        A(12, 12, 10, 6.5f, 0, 360);
        A(12, 9.5f, 6, 3.5f, 20, 160);         // the hole's lower rim
        A(12, 12.37f, 3.44f, 1.7f, 180, 360);  // its upper rim, ends on the lower
        break;

    // --- painting ---
    case Icon::Brush: // handle, ferrule, loaded tip
        L(20, 4, 13, 11); LT(13, 11, 11, 13);
        C(8.3f, 15.7f, 3); PL({{5.5f, 17}, {4, 20.5f}, {7, 18.5f}});
        break;
    case Icon::Eraser:
        PL({{20, 20}, {7, 20}, {3, 16}, {13.5f, 5.5f}, {20, 12}, {12, 20}});
        L(8.5f, 10.5f, 15, 17);
        break;
    case Icon::Fill: // tipped paint bucket with a drip
        PL({{3, 11}, {10, 4}, {17, 11}, {10, 18}, {3, 11}}); L(3, 11, 17, 11);
        L(19.5f, 12, 19.5f, 15.5f); C(19.5f, 17.5f, 2);
        break;
    case Icon::Eyedropper:
        C(17.5f, 6.5f, 3); L(12.5f, 7.5f, 16.5f, 11.5f);
        LT(14, 10, 7.5f, 16.5f); L(7.5f, 16.5f, 4.5f, 19.5f);
        break;
    case Icon::Airbrush: // spray can with a cloud of dots
        O(4, 9, 9, 12, 1.5f); F(6.5f, 5.5f, 4, 3.5f, .8f);
        C(16, 5, 1.1f); C(19.5f, 4, 1.1f); C(17.5f, 8.5f, 1.1f);
        C(21, 8, 1.1f); C(16, 12, 1.1f); C(20, 12.5f, 1.1f);
        break;
    case Icon::Select: // dashed marquee
        DRect(4, 5, 20, 19);
        break;
    case Icon::Lasso:
        A(13.5f, 9.5f, 7.5f, 5, 0, 360);
        PL({{8.5f, 13.3f}, {7, 17}, {9, 19.5f}, {6, 21}});
        break;
    case Icon::MagicWand: // stick + sparkle
        L(4, 20, 14, 10);
        L(18, 3, 18, 9); L(15, 6, 21, 6);
        C(13, 4.5f, 1.1f); C(21, 11, 1.1f);
        break;
    case Icon::Crop:
        PL({{7, 3}, {7, 17}, {21, 17}}); PL({{3, 7}, {17, 7}, {17, 21}});
        break;
    case Icon::Gradient: // frame filled with a fading ramp
        for (int i = 0; i < 4; ++i) // fading columns
            list.rect({area.x + (4 + 4 * float(i)) * k, area.y + 4 * k, 4 * k, 16 * k},
                      color.faded(1.f - .3f * float(i)));
        O(4, 4, 16, 16, 0);
        break;
    case Icon::Palette: // round palette with a thumb notch and paint dots
        Arc(12, 12, 9, 50, 340);
        PL({{20.5f, 8.9f}, {17, 11}, {16, 14}, {17.8f, 18.9f}});
        C(7.5f, 11, 1.6f); C(10.5f, 7, 1.6f); C(15, 6.8f, 1.6f); C(8.5f, 15.5f, 1.6f);
        break;
    case Icon::Text: // serif T
        L(5, 5, 19, 5); L(5, 5, 5, 7.5f); L(19, 5, 19, 7.5f);
        L(12, 5, 12, 19); L(9, 19, 15, 19);
        break;
    case Icon::Stamp: // rubber stamp
        Ring(12, 6, 2.8f); L(12, 8.8f, 12, 12.5f);
        O(4, 12.5f, 16, 4.5f, 1.5f); L(5, 20, 19, 20);
        break;
    case Icon::Blur: // water drop
        Arc(12, 14.5f, 6, -33.1f, 213.1f);
        L(12, 3.5f, 6.97f, 11.23f); L(12, 3.5f, 17.03f, 11.23f);
        break;

    // --- vector drawing ---
    case Icon::Pointer: // selection arrow
        PL({{6, 3}, {6, 19}, {10, 15}, {13, 21}, {15.5f, 20}, {12.5f, 14}, {18, 14}, {6, 3}});
        break;
    case Icon::DirectSelect: // arrow + the anchor it grabs
        PL({{4, 2.5f}, {4, 16}, {7.5f, 12.8f}, {10, 18}, {12, 17}, {9.5f, 12}, {14, 12}, {4, 2.5f}});
        F(15.5f, 15.5f, 5, 5, .5f);
        break;
    case Icon::Pen: // fountain-pen nib
        PL({{12, 21}, {6, 12}, {9, 6}, {15, 6}, {18, 12}, {12, 21}});
        L(12, 21, 12, 14.5f); C(12, 12.5f, 1.6f); L(8.5f, 3.5f, 15.5f, 3.5f);
        break;
    case Icon::Rectangle:
        O(4, 6, 16, 12, 1);
        break;
    case Icon::Ellipse:
        A(12, 12, 9, 6.5f, 0, 360);
        break;
    case Icon::Polygon: { // hexagon
        constexpr float deg = 3.14159265f / 180.f;
        float px = 0, py = 0;
        for (int i = 0; i <= 6; ++i) {
            float a = (-90.f + 60.f * float(i)) * deg;
            float x = 12 + std::cos(a) * 9, y = 12 + std::sin(a) * 9;
            if (i) L(px, py, x, y);
            px = x;
            py = y;
        }
        break;
    }
    case Icon::Line: // segment with its end points
        L(5.5f, 18.5f, 18.5f, 5.5f); C(5, 19, 2); C(19, 5, 2);
        break;
    case Icon::Union: // outline of two merged squares
        PL({{4, 4}, {14, 4}, {14, 10}, {20, 10}, {20, 20}, {10, 20}, {10, 14}, {4, 14}, {4, 4}});
        break;
    case Icon::Subtract: // front square cut out of the back one
        PL({{4, 4}, {14, 4}, {14, 10}, {10, 10}, {10, 14}, {4, 14}, {4, 4}});
        DRect(10, 10, 20, 20);
        break;
    case Icon::Intersect: // only the overlap is kept
        DRect(4, 4, 14, 14); DRect(10, 10, 20, 20); F(10, 10, 4, 4);
        break;
    case Icon::AlignLeft:
        L(4, 3, 4, 21); F(6.5f, 6, 13, 4, 1); F(6.5f, 14, 8, 4, 1);
        break;
    case Icon::AlignCenter:
        L(12, 3, 12, 21); F(5, 6, 14, 4, 1); F(8, 14, 8, 4, 1);
        break;
    case Icon::AlignRight:
        L(20, 3, 20, 21); F(4.5f, 6, 13, 4, 1); F(9.5f, 14, 8, 4, 1);
        break;
    case Icon::AlignTop:
        L(3, 4, 21, 4); F(6, 6.5f, 4, 13, 1); F(14, 6.5f, 4, 8, 1);
        break;
    case Icon::AlignMiddle:
        L(3, 12, 21, 12); F(6, 5, 4, 14, 1); F(14, 8, 4, 8, 1);
        break;
    case Icon::AlignBottom:
        L(3, 20, 21, 20); F(6, 4.5f, 4, 13, 1); F(14, 9.5f, 4, 8, 1);
        break;
    case Icon::Group: // two shapes inside one selection frame
        DRect(3, 3, 21, 21); O(6, 6, 7, 7, 1); Ring(15, 15, 3.5f);
        break;
    case Icon::Ungroup: // the same shapes, each on its own
        DRect(2.5f, 2.5f, 12.5f, 12.5f); O(5, 5, 5, 5, 1);
        DRect(11.5f, 11.5f, 21.5f, 21.5f); Ring(16.5f, 16.5f, 2.8f);
        break;
    case Icon::BringForward: // the filled square is in front
        O(4, 4, 11, 11, 1.5f); F(9, 9, 11, 11, 1.5f);
        break;
    case Icon::SendBackward: // the filled square is behind
        F(9, 9, 11, 11, 1.5f); PL({{9, 15}, {4, 15}, {4, 4}, {15, 4}, {15, 9}});
        break;
    case Icon::FlipH: // mirrored triangles across a dashed axis
        DL(12, 3, 12, 21);
        PL({{9.5f, 6}, {9.5f, 18}, {3, 18}, {9.5f, 6}});
        PL({{14.5f, 6}, {14.5f, 18}, {21, 18}, {14.5f, 6}});
        break;
    case Icon::FlipV:
        DL(3, 12, 21, 12);
        PL({{6, 9.5f}, {18, 9.5f}, {18, 3}, {6, 9.5f}});
        PL({{6, 14.5f}, {18, 14.5f}, {18, 21}, {6, 14.5f}});
        break;
    }
}

} // namespace slikui
