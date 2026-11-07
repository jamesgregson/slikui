// silk/render/sink.hpp — the one interface a graphics API must implement.
//
//   1. upload(): create/replace a texture (font atlas, images)
//   2. render(): execute a DrawList for this frame
//
// silk ships two sinks: SoftwareSink (raw uint32_t pixels) and, with an
// OpenGL context, OpenglSink. Anything else — Vulkan, Metal, SDL_Renderer —
// is ~100 lines implementing this contract.
#pragma once

#include "draw_list.hpp"

#include <cstdint>

namespace silk {

enum class TexFormat : std::uint8_t { Alpha8, Rgba8 };

struct TexUpload {
    std::uint32_t id = 0;
    int width = 0, height = 0;
    TexFormat format = TexFormat::Alpha8;
    const std::uint8_t* pixels = nullptr; // row-major, tightly packed
};

class IRenderSink {
public:
    virtual ~IRenderSink() = default;
    virtual void upload(const TexUpload&) = 0;
    virtual void render(const DrawList&, Vec2 framebuffer_size) = 0;
};

} // namespace silk
