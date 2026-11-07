// silk/widgets/image.hpp — Image: an RGBA bitmap drawn from app-provided
// pixels. The upload flows through the normal sink texture contract
// (TexFormat::Rgba8, IRenderSink::upload) — both bundled sinks already
// implement it. Decoding helpers wrap the vendored stb_image (PNG/JPEG/BMP/
// GIF/PNM/...); a private static copy is compiled into every TU so the
// library stays header-only with no link requirements.
#pragma once

#include "silk/context.hpp"
#include "silk/widgets/env.hpp"
#include "silk/widgets/widget.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#define STBIDEF static // one private copy of stb_image per TU
#define STB_IMAGE_IMPLEMENTATION
#include "silk/text/detail/stb_image.h"

namespace silk {

class Image : public Widget {
public:
    enum class Fit : std::uint8_t { Stretch, Contain, Cover };

    Image() = default;
    explicit Image(std::string widget_id) : Widget(std::move(widget_id)) {}

    // Raw RGBA8 pixels, row-major, tightly packed (4 bytes per pixel).
    Image& set_pixels(int w, int h, const std::uint8_t* rgba) {
        if (w <= 0 || h <= 0 || !rgba) return *this;
        w_ = w;
        h_ = h;
        pixels_.assign(rgba, rgba + size_t(w) * h * 4);
        dirty_ = true;
        return *this;
    }

    // Decode from an encoded image file (PNG, JPEG, BMP, ...).
    bool set_file(const std::string& path) {
        int w = 0, h = 0, n = 0;
        std::uint8_t* px = stbi_load(path.c_str(), &w, &h, &n, 4);
        if (!px) return false;
        set_pixels(w, h, px);
        stbi_image_free(px);
        return true;
    }

    // Decode from an encoded image in memory.
    bool set_memory(const std::uint8_t* data, size_t len) {
        int w = 0, h = 0, n = 0;
        std::uint8_t* px = stbi_load_from_memory(data, int(len), &w, &h, &n, 4);
        if (!px) return false;
        set_pixels(w, h, px);
        stbi_image_free(px);
        return true;
    }

    Image& set_fit(Fit f) { fit_ = f; return *this; }
    Fit fit() const { return fit_; }

    // Override the logical size; default is the natural pixel size at 1x.
    Image& set_size(float w, float h) { size_ = {w, h}; return *this; }

    int width() const { return w_; }
    int height() const { return h_; }

    Size measure(const Constraints& c) override {
        return c.clamp(size_.w > 0 ? size_ : Size{float(w_), float(h_)});
    }

    void attached(Context& c) override { tex_id_ = c.next_texture_id(); }

    void paint(const PaintEnv& env) const override {
        if (w_ <= 0 || h_ <= 0) return;
        Rect dst = bounds;
        if (fit_ != Fit::Stretch && bounds.w > 0 && bounds.h > 0) {
            float s = fit_ == Fit::Contain
                          ? std::fmin(bounds.w / float(w_), bounds.h / float(h_))
                          : std::fmax(bounds.w / float(w_), bounds.h / float(h_));
            float dw = float(w_) * s, dh = float(h_) * s;
            dst = {bounds.center().x - dw * .5f, bounds.center().y - dh * .5f, dw, dh};
        }
        env.list.image(dst, tex_id_, {0, 0, float(w_), float(h_)});
    }

    void collect_uploads(std::vector<TexUpload>& out) override {
        if (dirty_ && !pixels_.empty() && tex_id_) {
            out.push_back(TexUpload{tex_id_, w_, h_, TexFormat::Rgba8, pixels_.data()});
            dirty_ = false;
        }
    }

    const char* widget_type() const override { return "Image"; }

    bool stretch_cross() const override { return false; }

private:
    int w_ = 0, h_ = 0;
    std::vector<std::uint8_t> pixels_;
    Size size_{};
    Fit fit_ = Fit::Stretch;
    std::uint32_t tex_id_ = 0;
    bool dirty_ = false;
};

} // namespace silk
