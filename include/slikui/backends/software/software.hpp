// slikui/backends/software/software.hpp — rasterizer to raw uint32_t pixels.
//
// SoftwareSink implements the IRenderSink contract with no dependencies
// whatsoever: point it at a framebuffer (yours or owned) and draw.
// This is also what all tests and golden-image checks use, so the rendering
// path is exercised headlessly.
//
// Pixel format: 0xAABBGGRR in native byte order (matches Color::to_abgr).
#pragma once

#include "slikui/render/sink.hpp"
#include "slikui/render/draw_list.hpp"

#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace slikui::software {

// A drawing surface: either owns its memory or borrows yours (e.g. an
// engine's swap chain, an X11 shm segment, an NES framebuffer).
class Target {
public:
    Target(int width, int height) { resize(width, height); }
    Target(std::uint32_t* borrowed, int width, int height)
        : pixels_(borrowed), width_(width), height_(height), owned_(false) {}

    ~Target() {
        if (owned_) delete[] pixels_;
    }
    Target(const Target&) = delete;
    Target& operator=(const Target&) = delete;

    void resize(int width, int height) {
        if (!owned_) return;
        delete[] pixels_;
        width_ = width; height_ = height;
        pixels_ = new std::uint32_t[std::size_t(width) * std::size_t(height)]{};
        owned_ = true;
    }

    void clear(Color c) {
        const std::uint32_t p = c.to_abgr();
        for (int y = 0; y < height_; ++y)
            for (int x = 0; x < width_; ++x)
                at(x, y) = p;
    }

    int width() const { return width_; }
    int height() const { return height_; }
    std::uint32_t* data() { return pixels_; }
    const std::uint32_t* data() const { return pixels_; }

    std::uint32_t& at(int x, int y) {
        return pixels_[std::size_t(y) * std::size_t(width_) + std::size_t(x)];
    }
    std::uint32_t at(int x, int y) const {
        return pixels_[std::size_t(y) * std::size_t(width_) + std::size_t(x)];
    }

private:
    std::uint32_t* pixels_ = nullptr;
    int width_ = 0, height_ = 0;
    bool owned_ = true;
};

namespace detail {

inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// Approximate signed distance to a rounded box, `b` = half-extents,
// `r` = corner radius. Negative inside.
inline float sd_rounded_box(Vec2 p, Vec2 b, float r) {
    Vec2 q{std::fabs(p.x) - (b.x - r), std::fabs(p.y) - (b.y - r)};
    float ax = q.x > 0 ? q.x : 0, ay = q.y > 0 ? q.y : 0;
    float outside = std::sqrt(ax * ax + ay * ay);
    return outside + std::fmin(std::fmax(q.x, q.y), 0.f) - r;
}

inline Color sample_alpha(const std::uint8_t* tex, int tw, int th, float u, float v) {
    // Bilinear alpha sample, clamped to texture bounds.
    float fx = std::fmax(0.f, std::fmin(u, float(tw - 1)));
    float fy = std::fmax(0.f, std::fmin(v, float(th - 1)));
    int x0 = int(fx), y0 = int(fy);
    int x1 = std::fmin(x0 + 1, tw - 1), y1 = std::fmin(y0 + 1, th - 1);
    float tx = fx - x0, ty = fy - y0;
    auto px = [&](int x, int y) { return tex[std::size_t(y) * tw + x]; };
    float a00 = px(x0, y0), a10 = px(x1, y0), a01 = px(x0, y1), a11 = px(x1, y1);
    float a = (a00 * (1 - tx) + a10 * tx) * (1 - ty) + (a01 * (1 - tx) + a11 * tx) * ty;
    return Color(255, 255, 255, std::uint8_t(a + 0.5f));
}

inline Color sample_rgba(const std::uint8_t* tex, int tw, int, float u, float v) {
    int x = std::clamp(int(u), 0, tw - 1);
    int y = std::clamp(int(v), 0, tw - 1);
    const std::uint8_t* p = tex + (std::size_t(y) * tw + x) * 4;
    return Color(p[0], p[1], p[2], p[3]);
}

} // namespace detail

// IRenderSink over a CPU framebuffer.
class Sink : public IRenderSink {
public:
    explicit Sink(Target& target) : target_(&target) {}

    void set_target(Target& t) { target_ = &t; }
    Target& target() { return *target_; }

    // Logical px -> framebuffer px (HiDPI). DrawList coordinates are logical.
    void set_scale(float s) { scale_ = s; }
    float scale() const { return scale_; }

    void upload(const TexUpload& up) override {
        auto& t = textures_[up.id];
        t.w = up.width; t.h = up.height; t.fmt = up.format;
        t.data.assign(up.pixels, up.pixels + std::size_t(up.width) * std::size_t(up.height) *
                                     (up.format == TexFormat::Rgba8 ? 4 : 1));
    }

    void render(const DrawList& list, Vec2 framebuffer_size) override {
        (void)framebuffer_size;
        clips_.assign(1, Rect{0, 0, float(target_->width()), float(target_->height())});
        for (const auto& cmd : list.commands()) execute(cmd);
    }

    // Draw one pre-flattened command at an absolute position (internal).
    void execute(const DrawCmd& cmd) {
        DrawCmd c = cmd;
        if (scale_ != 1.f) { // logical -> framebuffer px
            c.rect = {cmd.rect.x * scale_, cmd.rect.y * scale_,
                      cmd.rect.w * scale_, cmd.rect.h * scale_};
            c.radius *= scale_;
            c.thickness *= scale_;
        }
        switch (c.type) {
        case DrawCmd::Type::ClipPush:
            clips_.push_back(c.rect.intersect(clips_.back()));
            break;
        case DrawCmd::Type::ClipPop:
            if (clips_.size() > 1) clips_.pop_back();
            break;
        case DrawCmd::Type::Rect: c.hollow ? stroke_rect(c) : fill_rect(c.rect, c.color, c.radius); break;
        case DrawCmd::Type::Line: stroke_line(c); break;
        case DrawCmd::Type::Glyph: draw_glyph(c); break;
        case DrawCmd::Type::Image: draw_image(c); break;
        }
    }

    // --- primitives (public so tests can exercise them directly) ---

    void fill_rect(Rect r, Color c, float radius = 0) {
        Rect clip = clips_.back();
        r = r.intersect(clip);
        if (r.empty()) return;
        if (radius <= 0) {
            for (int y = int(r.y); y < int(std::ceil(r.bottom())); ++y)
                for (int x = int(r.x); x < int(std::ceil(r.right())); ++x)
                    blend_at(x, y, c);
            return;
        }
        // Per-pixel coverage via SDF (antialiased edges).
        for (int y = int(r.y) - 1; y <= int(std::ceil(r.bottom())); ++y) {
            for (int x = int(r.x) - 1; x <= int(std::ceil(r.right())); ++x) {
                if (!clip.contains({float(x), float(y)})) continue;
                Vec2 p{x + .5f - r.x - r.w * .5f, y + .5f - r.y - r.h * .5f};
                float d = detail::sd_rounded_box(p, {r.w * .5f, r.h * .5f}, radius);
                float cov = detail::clamp01(.5f - d);
                if (cov <= 0) continue;
                blend_at(x, y, c.faded(cov));
            }
        }
    }

    void stroke_rect(const DrawCmd& cmd) {
        Rect r = cmd.rect;
        Rect clip = clips_.back();
        float w = cmd.thickness;
        float inner_r = std::fmax(cmd.radius - w, 0.f);
        for (int y = int(r.y) - 1; y <= int(std::ceil(r.bottom())); ++y) {
            for (int x = int(r.x) - 1; x <= int(std::ceil(r.right())); ++x) {
                if (!clip.contains({float(x), float(y)})) continue;
                Vec2 p{x + .5f - r.x - r.w * .5f, y + .5f - r.y - r.h * .5f};
                Vec2 b{r.w * .5f, r.h * .5f};
                float d_out = detail::sd_rounded_box(p, b, cmd.radius);
                Vec2 bi{std::fmax(b.x - w, 0.f), std::fmax(b.y - w, 0.f)};
                float d_in = detail::sd_rounded_box(p, bi, inner_r);
                float cov = detail::clamp01(.5f - d_out) * detail::clamp01(.5f + d_in);
                if (cov <= 0) continue;
                blend_at(x, y, cmd.color.faded(cov));
            }
        }
    }

    void stroke_line(const DrawCmd& cmd) {
        Vec2 a{cmd.rect.x, cmd.rect.y};
        Vec2 b{cmd.rect.x + cmd.rect.w, cmd.rect.y + cmd.rect.h};
        float half = cmd.thickness * .5f;
        Rect bounds = Rect{
            std::fmin(a.x, b.x) - half - 1, std::fmin(a.y, b.y) - half - 1,
            std::fabs(b.x - a.x) + 2 * half + 2, std::fabs(b.y - a.y) + 2 * half + 2}
                        .intersect(clips_.back());
        Vec2 d{b.x - a.x, b.y - a.y};
        float len2 = dot(d, d);
        for (int y = int(bounds.y); y <= int(std::ceil(bounds.bottom())); ++y) {
            for (int x = int(bounds.x); x <= int(std::ceil(bounds.right())); ++x) {
                Vec2 p{x + .5f, y + .5f};
                float t = len2 > 0 ? dot(p - a, d) / len2 : 0;
                t = detail::clamp01(t);
                Vec2 proj{a.x + d.x * t, a.y + d.y * t};
                float dist = (p - proj).length();
                float cov = detail::clamp01(half + .5f - dist);
                if (cov > 0) blend_at(x, y, cmd.color.faded(cov));
            }
        }
    }

    void draw_glyph(const DrawCmd& cmd) {
        auto* tex = find(cmd.tex_id);
        if (!tex || cmd.uv.empty()) return;
        Rect clip = clips_.back().intersect(cmd.rect);
        // Fully clipped: the loop below would still visit row int(clip.y)
        // when clip.y is fractional, painting a sliver outside the clip.
        if (clip.empty()) return;
        for (int y = int(clip.y); y < int(std::ceil(clip.bottom())); ++y) {
            for (int x = int(clip.x); x < int(std::ceil(clip.right())); ++x) {
                float fx = float(x) + .5f, fy = float(y) + .5f;
                float u = cmd.uv.x + (fx - cmd.rect.x) / cmd.rect.w * cmd.uv.w;
                float v = cmd.uv.y + (fy - cmd.rect.y) / cmd.rect.h * cmd.uv.h;
                Color a = tex->fmt == TexFormat::Alpha8
                    ? detail::sample_alpha(tex->data.data(), tex->w, tex->h, u, v)
                    : detail::sample_rgba(tex->data.data(), tex->w, tex->h, u, v);
                Color c = cmd.color.faded(float(a.a) / 255.f);
                if (c.a > 0) blend_at(x, y, c);
            }
        }
    }

    void draw_image(const DrawCmd& cmd) {
        auto* tex = find(cmd.tex_id);
        if (!tex) return;
        Rect clip = clips_.back().intersect(cmd.rect);
        if (clip.empty()) return; // see draw_glyph
        for (int y = int(clip.y); y < int(std::ceil(clip.bottom())); ++y) {
            for (int x = int(clip.x); x < int(std::ceil(clip.right())); ++x) {
                float u = cmd.uv.x + (float(x) + .5f - cmd.rect.x) / cmd.rect.w * cmd.uv.w;
                float v = cmd.uv.y + (float(y) + .5f - cmd.rect.y) / cmd.rect.h * cmd.uv.h;
                Color s = tex->fmt == TexFormat::Rgba8
                    ? detail::sample_rgba(tex->data.data(), tex->w, tex->h, u, v)
                    : detail::sample_alpha(tex->data.data(), tex->w, tex->h, u, v);
                blend_at(x, y, s);
            }
        }
    }

    void blend_at(int x, int y, Color c) {
        if (x < 0 || y < 0 || x >= target_->width() || y >= target_->height()) return;
        auto& dst = target_->at(x, y);
        Color mixed = blend(Color::from_abgr(dst), c);
        dst = mixed.to_abgr();
    }

private:
    struct SoftTex {
        int w = 0, h = 0;
        TexFormat fmt = TexFormat::Alpha8;
        std::vector<std::uint8_t> data;
    };

    SoftTex* find(std::uint32_t id) {
        auto it = textures_.find(id);
        return it == textures_.end() ? nullptr : &it->second;
    }

    Target* target_;
    float scale_ = 1.f;
    std::unordered_map<std::uint32_t, SoftTex> textures_;
    std::vector<Rect> clips_;
};

} // namespace slikui::software
