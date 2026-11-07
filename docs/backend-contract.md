# The slikui backend contract

Everything slikui needs from a graphics API is one interface:

```cpp
class IRenderSink {
public:
    virtual ~IRenderSink() = default;
    virtual void upload(const TexUpload&) = 0;
    virtual void render(const DrawList&, Vec2 framebuffer_size) = 0;
};
```

`slikui::software::Sink` (CPU, raw `uint32_t` pixels) and
`slikui::opengl::OpenglSink` (GL 3.3, system headers) are reference
implementations. A new backend — Vulkan, Metal, SDL_Renderer, a raylib batch —
implements the same two methods.

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
- `upload` may be called repeatedly for the same id (atlas regrows) — treat it
  as create-or-replace. Texture ids are never reused for different content.

## DrawList semantics

The list is flat commands in paint order. Geometry is in framebuffer pixels,
y-down, and **already clipped** is *not* guaranteed — apply the clip stack:

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

## Frame flow

```
ctx.render()
  ├─ upload() for dirty atlases
  ├─ build DrawList (root tree, then popups)
  └─ sink.render(list, framebuffer_size)
```

`render` should clear nothing by default — the list contains a background rect
(`Context::render(clear=true)` paints `Theme::background` first). Presenting
(swap buffers, blit) stays with the window layer, not the sink.

## Event translation

The window layer converts native events into `slikui::Event` and calls
`Context::handle` — one vocabulary regardless of platform:

| slikui event | GLFW source |
|------------|-------------|
| `MouseMove` | cursor-pos callback (`pos`, `delta`) |
| `MousePress`/`MouseRelease` | mouse-button callback (`button`, `pos`) |
| `Scroll` | scroll callback (`scroll`) |
| `KeyPress`/`KeyRelease` | key callback (`key` via `map_key`, `mods`) |
| `TextInput` | char callback (`codepoint`, Unicode scalar) |
| `FocusLost` | window-focus callback (focus out) |
| `FrameResize` | framebuffer-size callback (`pos` = **pixel** size; Context converts to logical) |

Mouse positions are framebuffer pixels (not window coordinates), so divide by
the content scale on HiDPI before filling `pos`.

## Reference notes from the GL sink

- One interleaved VBO (15 floats/vertex: pos, rgba, uv, tex-kind, center,
  half-extents, radius/border/dir), one SDF fragment shader: rounded rects and
  capsule lines get their AA for free from `sd_box` coverage.
- `ClipPush`/`ClipPop` flush the batch and call `glScissor` (y-flipped:
  GL's origin is bottom-left, slikui's is top-left).
- Glyphs sample the R8 atlas and multiply by `v_col`; images multiply the
  sampled RGBA into the vertex color.

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

The software sink (`backends/software/software.hpp`) is the smallest complete
example — start there when porting.
