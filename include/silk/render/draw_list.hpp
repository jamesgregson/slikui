// silk/render/draw_list.hpp — retained, API-agnostic display list.
//
// Widgets paint into a DrawList; backends (software rasterizer, OpenGL,
// anything) consume it. This is the entire rendering contract between the
// UI and a graphics API: flat commands, one texture id space, clip stack.
#pragma once

#include "silk/core/color.hpp"
#include "silk/core/math.hpp"

#include <vector>
#include <cstdint>

namespace silk {

struct DrawCmd {
    enum class Type : std::uint8_t {
        Rect,       // filled rect (radius > 0: rounded); hollow: border only
        Line,       // (rect.x, rect.y) -> (rect.x + rect.w, rect.y + rect.h)
        Glyph,      // alpha glyph quad, colored by `color`
        Image,      // textured quad
        ClipPush,   // rect = clip
        ClipPop,
    };

    Type type;
    Rect rect;                     // geometry (see per-type comments)
    float radius = 0;              // corner radius (Rect)
    float thickness = 1;           // line/border width
    bool hollow = false;           // Rect: outline instead of fill
    Color color = Color::white();
    std::uint32_t tex_id = 0;      // Glyph/Image: texture to sample
    Rect uv = {};                  // Glyph/Image: source rect in texture
};

class DrawList {
public:
    void reserve(size_t n) { cmds_.reserve(n); }
    size_t size() const { return cmds_.size(); }
    void clear() { cmds_.clear(); }
    const std::vector<DrawCmd>& commands() const { return cmds_; }

    void rect(Rect r, Color c, float radius = 0) {
        if (r.empty()) return;
        push({DrawCmd::Type::Rect, r, radius, 1, false, c, 0, {}});
    }

    void hollow_rect(Rect r, Color c, float width, float radius = 0) {
        if (r.empty()) return;
        push({DrawCmd::Type::Rect, r, radius, width, true, c, 0, {}});
    }

    void line(Vec2 a, Vec2 b, Color c, float thickness = 1) {
        push({DrawCmd::Type::Line, {a.x, a.y, b.x - a.x, b.y - a.y},
              0, thickness, false, c, 0, {}});
    }

    void circle(Vec2 center, float radius, Color c) {
        if (radius <= 0) return;
        push({DrawCmd::Type::Rect, {center.x - radius, center.y - radius, radius * 2, radius * 2},
              radius, 1, false, c, 0, {}});
    }

    void glyph(Rect dst, std::uint32_t tex_id, Rect uv, Color c) {
        if (dst.empty()) return;
        push({DrawCmd::Type::Glyph, dst, 0, 1, false, c, tex_id, uv});
    }

    void image(Rect dst, std::uint32_t tex_id, Rect uv) {
        if (dst.empty()) return;
        push({DrawCmd::Type::Image, dst, 0, 1, false, Color::white(), tex_id, uv});
    }

    void push_clip(Rect r) { push({DrawCmd::Type::ClipPush, r}); }
    void pop_clip() { push({DrawCmd::Type::ClipPop, {}}); }

    // Appends `other`, offsetting geometry by `offset` (keeps one list per frame).
    void append(const DrawList& other, Vec2 offset) {
        for (const auto& c : other.commands()) {
            DrawCmd copy = c;
            copy.rect.x += offset.x;
            copy.rect.y += offset.y;
            cmds_.push_back(copy);
        }
    }

    // True while the list is balanced between push_clip/pop_clip.
    bool clip_balanced() const {
        int depth = 0;
        for (const auto& c : cmds_) {
            if (c.type == DrawCmd::Type::ClipPush) ++depth;
            if (c.type == DrawCmd::Type::ClipPop) --depth;
            if (depth < 0) return false;
        }
        return depth == 0;
    }

private:
    void push(const DrawCmd& c) { cmds_.push_back(c); }
    std::vector<DrawCmd> cmds_;
};

} // namespace silk
