// slikui/render/batcher.hpp - GpuBatcher: DrawList -> GPU-ready arrays.
//
// API-agnostic (no GL/Vulkan/Metal headers). Turns a frame's DrawList into
//
//   instances  one GpuInstance per drawable command (20 floats), meant to be
//              drawn as an instanced 4-vertex triangle strip;
//   batches    runs of instances sharing one scissor rect and one texture.
//
// so a GPU sink is: upload `instances` once, then per batch set the scissor,
// bind the texture, draw `count` instances from `first`. OpenglSink uses it,
// and the Python bindings expose it zero-copy (slikui.GpuBatcher), so a
// Python sink costs one buffer upload + one draw call per batch rather than
// per command.
//
// The reference GLSL 330 shaders (kGpuVertexShader / kGpuFragmentShader)
// reproduce the software rasterizer's coverage math: SDF rounded rects and
// borders, capsule lines, bilinear Alpha8 glyphs, Rgba8 images.
//
// Coordinates: instances stay in LOGICAL px (the shader multiplies by
// u_scale); scissor rects are FRAMEBUFFER px with a bottom-left origin
// (ready for glScissor), using the same pixel-inclusion rule as the
// software sink (pixel X is inside when ceil(x0) <= X < ceil(x1)).
#pragma once

#include "slikui/render/draw_list.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace slikui {

// Shape kinds (GpuInstance::shape[0]).
enum class GpuKind : int { Rect = 0, Glyph = 1, Image = 2, Line = 3 };

// One drawable command. Attribute order = vertex shader locations 0..4.
struct GpuInstance {
    float rect[4];  // x, y, w, h - quad bounds, logical px (Line: capsule bbox)
    float color[4]; // r, g, b, a in 0..1, straight alpha
    float uv[4];    // u0, v0, u1, v1 in TEXELS (shader divides by textureSize)
    float shape[4]; // kind, radius, border width (0 = fill), line half-thickness
    float line[4];  // Line endpoints a.x, a.y, b.x, b.y (logical px)
};
static_assert(sizeof(GpuInstance) == 20 * sizeof(float));

// A run of instances drawn with one scissor + one texture.
struct GpuBatch {
    std::int32_t first;   // first instance
    std::int32_t count;   // instance count (> 0)
    std::int32_t tex_id;  // slikui texture id to bind; 0 = none needed
    std::int32_t scissor[4]; // x, y, w, h in framebuffer px, bottom-left origin
};
static_assert(sizeof(GpuBatch) == 7 * sizeof(std::int32_t));

class GpuBatcher {
public:
    // Rebuilds instances()/batches() for one frame. `frame` is the logical
    // frame size handed to IRenderSink::render; `scale` maps logical px to
    // framebuffer px. The framebuffer is int(frame * scale) pixels.
    void build(const DrawList& list, Vec2 frame, float scale) {
        instances_.clear();
        batches_.clear();
        scale_ = scale > 0 ? scale : 1.f;
        fb_w_ = int(frame.x * scale_);
        fb_h_ = int(frame.y * scale_);
        clips_.assign(1, Rect{0, 0, frame.x, frame.y});
        open_batch();

        for (const auto& c : list.commands()) {
            switch (c.type) {
            case DrawCmd::Type::ClipPush:
                clips_.push_back(c.rect.intersect(clips_.back()));
                open_batch();
                break;
            case DrawCmd::Type::ClipPop:
                if (clips_.size() > 1) clips_.pop_back();
                open_batch();
                break;
            case DrawCmd::Type::Rect: add_rect(c); break;
            case DrawCmd::Type::Line: add_line(c); break;
            case DrawCmd::Type::Glyph:
            case DrawCmd::Type::Image: add_textured(c); break;
            }
        }
        close_batch();
    }

    const std::vector<GpuInstance>& instances() const { return instances_; }
    const std::vector<GpuBatch>& batches() const { return batches_; }
    int framebuffer_width() const { return fb_w_; }
    int framebuffer_height() const { return fb_h_; }
    float scale() const { return scale_; }

private:
    // --- batching ---------------------------------------------------------

    // Scissor for the current clip: framebuffer px, bottom-left origin.
    void scissor_for(const Rect& r, std::int32_t out[4]) const {
        auto px = [&](float v, int hi) {
            return std::clamp(int(std::ceil(v * scale_)), 0, hi);
        };
        int x0 = px(r.x, fb_w_), x1 = px(r.x + r.w, fb_w_);
        int y0 = px(r.y, fb_h_), y1 = px(r.y + r.h, fb_h_);
        out[0] = x0;
        out[1] = fb_h_ - y1;
        out[2] = std::max(0, x1 - x0);
        out[3] = std::max(0, y1 - y0);
    }

    // Start a batch for the current clip. An empty open batch is reused.
    void open_batch() {
        if (!batches_.empty() && batches_.back().count == 0) {
            scissor_for(clips_.back(), batches_.back().scissor);
            batches_.back().tex_id = 0;
            batches_.back().first = std::int32_t(instances_.size());
            return;
        }
        GpuBatch b{std::int32_t(instances_.size()), 0, 0, {0, 0, 0, 0}};
        scissor_for(clips_.back(), b.scissor);
        batches_.push_back(b);
    }

    void close_batch() {
        if (!batches_.empty() && batches_.back().count == 0) batches_.pop_back();
    }

    bool clip_empty() const {
        const GpuBatch& b = batches_.back();
        return b.scissor[2] <= 0 || b.scissor[3] <= 0;
    }

    // Conservative visibility: bounds (+1 framebuffer px of AA fringe)
    // against the current clip.
    bool visible(const Rect& bounds) const {
        if (clip_empty()) return false;
        float m = 1.f / scale_;
        const Rect& c = clips_.back();
        return bounds.x - m < c.x + c.w && bounds.x + bounds.w + m > c.x &&
               bounds.y - m < c.y + c.h && bounds.y + bounds.h + m > c.y;
    }

    GpuInstance& push(const Rect& r, Color col, GpuKind kind) {
        GpuInstance in{};
        in.rect[0] = r.x;
        in.rect[1] = r.y;
        in.rect[2] = r.w;
        in.rect[3] = r.h;
        in.color[0] = col.r / 255.f;
        in.color[1] = col.g / 255.f;
        in.color[2] = col.b / 255.f;
        in.color[3] = col.a / 255.f;
        in.shape[0] = float(int(kind));
        instances_.push_back(in);
        batches_.back().count++;
        return instances_.back();
    }

    // --- commands -----------------------------------------------------------

    void add_rect(const DrawCmd& c) {
        if (c.rect.w <= 0 || c.rect.h <= 0 || c.color.a == 0) return;
        if (!visible(c.rect)) return;
        GpuInstance& in = push(c.rect, c.color, GpuKind::Rect);
        in.shape[1] = std::max(c.radius, 0.f);
        in.shape[2] = c.hollow ? std::max(c.thickness, 0.f) : 0.f;
        // A zero-width border draws nothing (software: inner == outer).
        if (c.hollow && in.shape[2] <= 0) {
            instances_.pop_back();
            batches_.back().count--;
        }
    }

    void add_line(const DrawCmd& c) {
        if (c.color.a == 0 || c.thickness <= 0) return;
        float ax = c.rect.x, ay = c.rect.y;
        float bx = ax + c.rect.w, by = ay + c.rect.h;
        float half = c.thickness * .5f;
        // Capsule bounds; the vertex shader adds the AA fringe.
        Rect bb{std::min(ax, bx) - half, std::min(ay, by) - half,
                std::fabs(bx - ax) + 2 * half, std::fabs(by - ay) + 2 * half};
        if (!visible(bb)) return;
        GpuInstance& in = push(bb, c.color, GpuKind::Line);
        in.shape[3] = half;
        in.line[0] = ax;
        in.line[1] = ay;
        in.line[2] = bx;
        in.line[3] = by;
    }

    void add_textured(const DrawCmd& c) {
        if (c.tex_id == 0 || c.rect.w <= 0 || c.rect.h <= 0) return;
        if (c.type == DrawCmd::Type::Glyph && (c.uv.w <= 0 || c.uv.h <= 0)) return;
        if (c.type == DrawCmd::Type::Glyph && c.color.a == 0) return;
        if (!visible(c.rect)) return;
        GpuBatch* b = &batches_.back();
        if (b->tex_id != 0 && b->tex_id != std::int32_t(c.tex_id)) {
            open_batch();
            b = &batches_.back();
        }
        b->tex_id = std::int32_t(c.tex_id);
        GpuKind kind = c.type == DrawCmd::Type::Glyph ? GpuKind::Glyph : GpuKind::Image;
        // Images ignore cmd.color (always white in DrawList::image).
        Color col = kind == GpuKind::Image ? Color::white() : c.color;
        GpuInstance& in = push(c.rect, col, kind);
        in.uv[0] = c.uv.x;
        in.uv[1] = c.uv.y;
        in.uv[2] = c.uv.x + c.uv.w;
        in.uv[3] = c.uv.y + c.uv.h;
    }

    std::vector<GpuInstance> instances_;
    std::vector<GpuBatch> batches_;
    std::vector<Rect> clips_;
    float scale_ = 1.f;
    int fb_w_ = 0, fb_h_ = 0;
};

// ---------------------------------------------------------------------------
// Reference shaders (GLSL 330 core). Per-instance attributes at locations
// 0..4 = GpuInstance fields, divisor 1; draw GL_TRIANGLE_STRIP, 4 vertices,
// N instances, no vertex buffer for the corners (gl_VertexID).
// Uniforms: u_res (framebuffer px), u_scale (logical -> fb px), u_tex (unit).
// Blend: glBlendFuncSeparate(SRC_ALPHA, ONE_MINUS_SRC_ALPHA, ONE,
// ONE_MINUS_SRC_ALPHA) - the software sink's straight-alpha "over".
// ---------------------------------------------------------------------------

inline constexpr const char* kGpuVertexShader = R"(#version 330 core
layout(location=0) in vec4 a_rect;
layout(location=1) in vec4 a_color;
layout(location=2) in vec4 a_uv;
layout(location=3) in vec4 a_shape;
layout(location=4) in vec4 a_line;
uniform vec2 u_res;
uniform float u_scale;
out vec4 v_color;
out vec2 v_uv;
flat out vec4 v_rect;
flat out vec4 v_shape;
flat out vec4 v_line;
void main() {
    vec2 corner = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));
    vec4 r = a_rect * u_scale;              // framebuffer px
    int kind = int(a_shape.x + 0.5);
    // SDF shapes need a 1px fringe for antialiasing; textured quads map UVs
    // exactly onto their rect.
    float pad = (kind == 0 || kind == 3) ? 1.0 : 0.0;
    vec2 p = r.xy - vec2(pad) + corner * (r.zw + vec2(2.0 * pad));
    gl_Position = vec4(p.x / u_res.x * 2.0 - 1.0, 1.0 - p.y / u_res.y * 2.0, 0.0, 1.0);
    // UV extrapolates with the padding so it stays exact inside the rect.
    vec2 t = (p - r.xy) / max(r.zw, vec2(1e-6));
    v_uv = mix(a_uv.xy, a_uv.zw, t);
    v_color = a_color;
    v_rect = r;
    v_shape = vec4(a_shape.x, a_shape.yzw * u_scale);
    v_line = a_line * u_scale;
}
)";

inline constexpr const char* kGpuFragmentShader = R"(#version 330 core
in vec4 v_color;
in vec2 v_uv;
flat in vec4 v_rect;
flat in vec4 v_shape;
flat in vec4 v_line;
uniform vec2 u_res;
uniform sampler2D u_tex;
out vec4 frag;
float sd_box(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - (b - vec2(r));
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}
void main() {
    // Pixel center in top-left-origin framebuffer px (matches software).
    vec2 px = vec2(gl_FragCoord.x, u_res.y - gl_FragCoord.y);
    int kind = int(v_shape.x + 0.5);
    vec4 col = v_color;
    float cov = 1.0;
    if (kind == 0) {
        vec2 half_ = v_rect.zw * 0.5;
        vec2 p = px - (v_rect.xy + half_);
        float radius = v_shape.y;
        float border = v_shape.z;
        cov = clamp(0.5 - sd_box(p, half_, radius), 0.0, 1.0);
        if (border > 0.0) {
            vec2 inner = max(half_ - vec2(border), vec2(0.0));
            cov *= clamp(0.5 + sd_box(p, inner, max(radius - border, 0.0)), 0.0, 1.0);
        }
    } else if (kind == 3) {
        vec2 a = v_line.xy, b = v_line.zw;
        vec2 pa = px - a, ba = b - a;
        float len2 = dot(ba, ba);
        float h = len2 > 0.0 ? clamp(dot(pa, ba) / len2, 0.0, 1.0) : 0.0;
        cov = clamp(v_shape.w + 0.5 - length(pa - ba * h), 0.0, 1.0);
    } else {
        vec4 s = texture(u_tex, v_uv / vec2(textureSize(u_tex, 0)));
        if (kind == 1) cov = s.r;           // Alpha8 atlas (stored as R8)
        else col = s;                       // Rgba8 image, straight alpha
    }
    float a = col.a * cov;
    if (a <= 0.0) discard;
    frag = vec4(col.rgb, a);
}
)";

} // namespace slikui
