// slikui/widgets/env.hpp — the environment handed to paint().
#pragma once

#include "slikui/style/theme.hpp"
#include "slikui/text/font.hpp"
#include "slikui/render/draw_list.hpp"

namespace slikui {

struct PaintEnv {
    DrawList& list;
    const Theme& theme;
    const text::Font& font;
};

// Draws a laid-out text block with `top_left` as the block's top-left corner.
// `inv` converts the raster-space layout back to logical px (1 / theme scale).
inline void paint_text(const PaintEnv& env, const text::TextLayout& lay, Vec2 top_left,
                       Color col, float inv) {
    for (const auto& pg : lay.glyphs) {
        const text::Glyph& g = *pg.glyph;
        if (g.uv.empty()) continue;
        env.list.glyph({top_left.x + pg.pos.x * inv, top_left.y + pg.pos.y * inv,
                        g.size.w * inv, g.size.h * inv},
                       g.tex_id, g.uv, col);
    }
}

} // namespace slikui
