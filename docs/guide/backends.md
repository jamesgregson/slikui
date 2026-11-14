# Render backends

Everything slikui needs from a graphics API is one interface:

```cpp
class IRenderSink {
public:
    virtual ~IRenderSink() = default;
    virtual void upload(const TexUpload&) = 0;
    virtual void render(const DrawList&, Vec2 framebuffer_size) = 0;
    virtual void release(std::uint32_t id) {} // optional: texture is dead
};
```

Two backends ship out of the box and are the reference implementations:

- **`slikui::software::Sink`** — antialiased CPU rasterizer over a raw
  `uint32_t` framebuffer (owned or borrowed). Works headless, on embedded
  targets, or blitted into any engine. `sink.set_scale()` renders logical
  coordinates at any pixel density.
- **`slikui::opengl::OpenglSink`** — GL 3.3 core renderer using your **system
  OpenGL headers** (no vendored loader). Optional
  `slikui::glfw::GlfwWindow` wraps window creation and event translation.
  Both self-gate on `__has_include`, so they compile to nothing when the
  headers are absent.

Anything else — Vulkan, Metal, SDL_Renderer, a raylib batch — implements the
same two methods. A minimal sink is ~100–400 lines; the software sink is the
smallest complete example, start there when porting. Every screenshot on
this site is real `IRenderSink` output from the software backend — the two
methods are the entire contract.

## Frame flow

```
ctx.render()
  ├─ upload() for dirty atlases (font atlas, images)
  ├─ build DrawList (root tree, then popups, then dialogs/toasts)
  └─ sink.render(list, framebuffer_size)
```

`render` should clear nothing by default — the list contains a background
rect (`Context::render(clear=true)` paints `Theme::background` first).
Presenting (swap buffers, blit) stays with the window layer, not the sink.

## Texture uploads

```cpp
struct TexUpload {
    std::uint32_t id;          // from Context::next_texture_id()
    int width, height;
    TexFormat format;          // Alpha8 (glyph atlas) or Rgba8 (images)
    const std::uint8_t* pixels;// row-major, tightly packed
};
```

- `Alpha8`: one coverage byte per pixel. Store it as `R8` (GL) or expand to
  RGBA on upload; shaders multiply color by the red/alpha channel.
- `Rgba8`: straight (non-premultiplied) alpha.
- `upload` may be called repeatedly for the same id (atlas regrows) — treat
  it as create-or-replace. Texture ids are never reused for different
  content.
- `release(id)` is called when the widget owning a texture (`Image`,
  `ImageView`, `CompareView`) is destroyed; free it then. It can arrive
  outside `render()` — e.g. from `Context::clear_tree()` at shutdown, with
  no GPU context current — so GPU sinks should queue the delete and perform
  it on the next `render()` (`OpenglSink` does). It is never called from
  `~Context`; a sink frees what it still holds when it is destroyed.
- After a sink loses its textures (GPU context lost or recreated), call
  `Context::invalidate_textures()`: the next `render()` re-uploads the font
  atlas and every live widget texture.

## DrawList semantics

The list is flat commands in paint order. Geometry is in framebuffer pixels,
y-down. Clipping is *not* pre-applied — apply the clip stack:

| Cmd | Meaning | Fields |
|-----|---------|--------|
| `Rect` | filled rect; `radius > 0` rounds corners with antialiasing; `hollow` draws a `thickness`-wide border instead of fill | rect, radius, thickness, hollow, color |
| `Line` | capsule from (x,y) to (x+w, y+h) with `thickness` | rect (endpoints), thickness, color |
| `Glyph` | alpha quad tinted by `color`; `uv` is in atlas pixels | rect (dst), uv, tex_id, color |
| `Image` | textured quad, straight alpha | rect (dst), uv, tex_id |
| `ClipPush`/`ClipPop` | push/pop a clip rect; **intersect** with the current clip on push | rect (push only) |

Guarantees you can rely on:

- `ClipPush`/`ClipPop` are balanced (`DrawList::clip_balanced()`).
- `uv` values for glyphs/images are in **texture pixels** — normalize by the
  texture's size from the last `upload` of that id.
- Whitespace glyphs are never emitted (widgets skip them).

## Event translation (window layer)

The window layer converts native events into `slikui::Event` and calls
`Context::handle` — one vocabulary regardless of platform:

| slikui event | GLFW source |
|--------------|-------------|
| `MouseMove` | cursor-pos callback (`pos`, `delta`) |
| `MousePress`/`MouseRelease` | mouse-button callback (`button`, `pos`) |
| `Scroll` | scroll callback (`scroll`) |
| `KeyPress`/`KeyRelease` | key callback (`key` via `map_key`, `mods`) |
| `TextInput` | char callback (`codepoint`, Unicode scalar) |
| `FocusLost` | window-focus callback (focus out) |
| `FrameResize` | framebuffer-size callback (`pos` = **pixel** size; Context converts to logical) |

Mouse positions are framebuffer pixels (not window coordinates), so divide by
the content scale on HiDPI before filling `pos`.

## A minimal new sink (sketch)

```cpp
class MySink : public slikui::IRenderSink {
    void upload(const slikui::TexUpload& up) override {
        textures[up.id] = make_texture(up); // create-or-replace
    }
    void render(const slikui::DrawList& list, slikui::Vec2 fb) override {
        begin_frame(fb);
        for (const auto& cmd : list.commands()) {
            switch (cmd.type) {
            case slikui::DrawCmd::Type::ClipPush: push_clip(cmd.rect); break;
            case slikui::DrawCmd::Type::ClipPop:  pop_clip();          break;
            default: draw(cmd); break; // rect/line/glyph/image
            }
        }
        end_frame();
    }
};
```

## GPU sinks: `GpuBatcher`

`slikui/render/batcher.hpp` does the API-independent half of a GPU sink.
`GpuBatcher::build(list, frame, scale)` turns a frame into two arrays:

- **instances** — one `GpuInstance` (20 floats: rect, rgba, uv in texels,
  shape `{kind, radius, border, line half-thickness}`, line endpoints) per
  drawable command, in logical px. Commands entirely outside their clip,
  transparent fills and empty quads are culled.
- **batches** — `{first, count, tex_id, scissor x/y/w/h}` runs sharing one
  scissor and one texture. Scissors are framebuffer px, bottom-left origin,
  ready for `glScissor`. Shapes never break a texture run, so a typical
  frame is a handful of batches.

A sink uploads the instances once per frame and, per batch, sets the
scissor, binds the texture and draws `count` instances of a 4-vertex
triangle strip. The reference GLSL 330 shaders (`kGpuVertexShader`,
`kGpuFragmentShader`) take the five vec4 attributes at locations 0–4 with
divisor 1, and the uniforms `u_res`, `u_scale`, `u_tex`. Blend with
`glBlendFuncSeparate(SRC_ALPHA, ONE_MINUS_SRC_ALPHA, ONE,
ONE_MINUS_SRC_ALPHA)` — the software sink's straight-alpha "over".

The shaders reproduce the software rasterizer's coverage (SDF rounded
rects/borders, capsule lines, bilinear Alpha8, nearest Rgba8):
`tests/gl_parity.cpp` renders through `OpenglSink` offscreen and requires
shapes to match the software sink to 2/255. Text differs at glyph edges
because the software sink's glyph/image loops start at `int(x)`, painting
pixels whose centers lie outside the quad (it can show faint dots from the
neighbouring atlas glyph); GPUs rasterize pixel centers only.

## Reference notes from the GL sink

- `OpenglSink` is a thin layer over `GpuBatcher`: one streamed VBO
  (orphaned each frame), one instanced draw per batch. GL 3.3 has no
  base-instance draw, so it re-points the attributes at `first`.
- `render()` sets viewport, program, VAO, texture unit 0, blending and
  scissor, disables depth/cull/stencil, and leaves them that way; it draws
  into the bound framebuffer. `shutdown()` frees everything (context
  current).
- Textures: R8 for Alpha8 (`GL_LINEAR`), RGBA8 for Rgba8 (`GL_NEAREST`, as
  the software sink samples). `release()` defers `glDeleteTextures` to the
  next `render()`.

## Python: your own renderer

`slikui.RenderSink` is a trampoline over C++ `IRenderSink`: subclass it,
implement `upload(tex)` and `render(commands, fb_size)`, and slikui hands you
every frame as a flat command list — point it at pygame, moderngl, PIL,
anything. `slikui.CanvasSink` is a worked example: a pure-numpy rasterizer
producing **pixel-identical** output to the C++ software backend (verified by
test). Details: [Python bindings](../reference/python-bindings.md).
