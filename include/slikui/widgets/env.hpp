// slikui/widgets/env.hpp - the environment handed to paint().
#pragma once

#include "slikui/style/theme.hpp"
#include "slikui/text/font.hpp"
#include "slikui/text/text.hpp"
#include "slikui/render/draw_list.hpp"

#include <string_view>

namespace slikui {

struct PaintEnv {
    DrawList& list;
    const Theme& theme;
    const text::Font& font;            // body font, regular face
    text::FontLibrary* fonts = nullptr; // set by Context::render (null: hand-built env)
    // The part of the frame this widget's drawing can show through: the
    // window, narrowed by every clip pushed on the way down the tree (a
    // ScrollArea's viewport, ...). Anything outside it is clipped away, so
    // paint() can skip it - e.g. a long list drawing only its on-screen
    // rows. Logical px, same space as bounds. Unbounded in a hand-built env.
    Rect visible_rect{-1e9f, -1e9f, 2e9f, 2e9f};

    // A copy whose visible_rect is narrowed to `clip`.
    PaintEnv clipped(Rect clip) const {
        PaintEnv e = *this;
        e.visible_rect = visible_rect.intersect(clip);
        return e;
    }

    // Draws `s` anchored at `pos` (logical px) and returns the rect it
    // occupies. With default options `pos` is the top-left corner; align /
    // valign make it the center, right edge, baseline, ... instead.
    //   env.text({x, y}, "Hello");
    //   env.text({cx, cy}, "42%", {.size = 20, .bold = true,
    //            .align = TextAlign::Center, .valign = TextVAlign::Middle});
    Rect text(Vec2 pos, std::string_view s, const TextOpts& o = {}) const {
        return text::draw_text(list, theme, fonts, font, false, {pos.x, pos.y, 0, 0}, s, o);
    }

    // Draws `s` inside `box`, aligned per o.align / o.valign; wraps to the
    // box width with o.wrap, and o.overflow handles text wider than the box.
    //   env.text_in(bounds, "OK", {.align = TextAlign::Center, .valign = TextVAlign::Middle});
    Rect text_in(Rect box, std::string_view s, const TextOpts& o = {}) const {
        return text::draw_text(list, theme, fonts, font, true, box, s, o);
    }

    // Draws a kept TextBlock (Context::text_layout) with its top-left at
    // `top_left`; an alpha-0 color uses the block's own color.
    void draw(const TextBlock& block, Vec2 top_left, Color color = Color::transparent()) const {
        block.emit(list, top_left, color);
    }
};

// Draws a laid-out text block with `top_left` as the block's top-left corner.
// `inv` converts the raster-space layout back to logical px (1 / theme scale).
// Underline / strikethrough bars in the layout draw in the same color.
inline void paint_text(const PaintEnv& env, const text::TextLayout& lay, Vec2 top_left,
                       Color col, float inv) {
    text::emit_layout(env.list, lay, top_left, inv, col);
}

} // namespace slikui
