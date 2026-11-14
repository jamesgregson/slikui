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

- **`slikui::software::Sink`** - antialiased CPU rasterizer over a raw
  `uint32_t` framebuffer (owned or borrowed). Works headless, on embedded
  targets, or blitted into any engine. `sink.set_scale()` renders logical
  coordinates at any pixel density.
- **`slikui::opengl::OpenglSink`** - GL 3.3 core renderer using your **system
  OpenGL headers** (no vendored loader).

Window/input layers are separate and optional: GLFW (`backends/glfw/`) and
SDL3 (`backends/sdl/`), each split into input translation for a window you
own and a ready-made window. All self-gate on `__has_include`, so they
compile to nothing when the headers are absent.

Anything else - Vulkan, Metal, SDL_Renderer, a raylib batch - implements the
same two methods. A minimal sink is ~100-400 lines; the software sink is the
smallest complete example, start there when porting. Every screenshot on
this site is real `IRenderSink` output from the software backend - the two
methods are the entire contract.

## Frame flow

```
ctx.render()
  |- upload() for dirty atlases (font atlas, images)
  |- build DrawList (root tree, then popups, then dialogs/toasts)
  `- sink.render(list, framebuffer_size)
```

`render` should clear nothing by default - the list contains a background
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
- `upload` may be called repeatedly for the same id (atlas regrows) - treat
  it as create-or-replace. Texture ids are never reused for different
  content.
- `release(id)` is called when the widget owning a texture (`Image`,
  `ImageView`, `CompareView`) is destroyed; free it then. It can arrive
  outside `render()` - e.g. from `Context::clear_tree()` at shutdown, with
  no GPU context current - so GPU sinks should queue the delete and perform
  it on the next `render()` (`OpenglSink` does). It is never called from
  `~Context`; a sink frees what it still holds when it is destroyed.
- After a sink loses its textures (GPU context lost or recreated), call
  `Context::invalidate_textures()`: the next `render()` re-uploads the font
  atlas and every live widget texture.

## DrawList semantics

The list is flat commands in paint order. Geometry is in framebuffer pixels,
y-down. Clipping is *not* pre-applied - apply the clip stack:

| Cmd | Meaning | Fields |
|-----|---------|--------|
| `Rect` | filled rect; `radius > 0` rounds corners with antialiasing; `hollow` draws a `thickness`-wide border instead of fill | rect, radius, thickness, hollow, color |
| `Line` | capsule from (x,y) to (x+w, y+h) with `thickness` | rect (endpoints), thickness, color |
| `Glyph` | alpha quad tinted by `color`; `uv` is in atlas pixels | rect (dst), uv, tex_id, color |
| `Image` | textured quad, straight alpha | rect (dst), uv, tex_id |
| `ClipPush`/`ClipPop` | push/pop a clip rect; **intersect** with the current clip on push | rect (push only) |

Guarantees you can rely on:

- `ClipPush`/`ClipPop` are balanced (`DrawList::clip_balanced()`).
- `uv` values for glyphs/images are in **texture pixels** - normalize by the
  texture's size from the last `upload` of that id.
- Whitespace glyphs are never emitted (widgets skip them).

## Event translation (window layer)

The window layer converts native events into `slikui::Event` and calls
`Context::handle` - one vocabulary regardless of platform:

| slikui event | GLFW source | builder (`slikui::glfw::`) |
|--------------|-------------|----------------------------|
| `MouseMove` | cursor-pos callback (`pos`, `delta`, held `mods`) | `cursor_pos_event` |
| `MousePress`/`MouseRelease` | mouse-button callback (`button` via `map_button`, `pos`, `mods`) | `mouse_button_event` |
| `Scroll` | scroll callback (`scroll`, held `mods`) | `scroll_event` |
| `KeyPress`/`KeyRelease` | key callback (`key` via `map_key`, `mods`, `repeat`) | `key_event` |
| `TextInput` | char callback (`codepoint`, Unicode scalar) | `char_event` |
| `FocusLost` | window-focus callback (focus out) | `focus_event` |
| `FrameResize` | framebuffer-size callback (`pos` = **pixel** size; Context converts to logical) | `framebuffer_size_event` |

Mouse positions are **logical points** - GLFW's cursor coordinates (window
space) pass through unchanged, matching the layout space Context works in.
Only `FrameResize` carries framebuffer pixels.

Buttons need mapping: GLFW numbers them LEFT=0, **RIGHT=1, MIDDLE=2**;
slikui uses `MouseLeft=0`, **`MouseMiddle=1`, `MouseRight=2`**.
`map_button` does it (extra buttons pass through).

### Using your own GLFW window

`slikui/backends/glfw/glfw_input.hpp` works on any `GLFWwindow*` - it never
creates a window or touches GL. Pick the level that fits your app:

```cpp
#include <slikui/backends/glfw/glfw_input.hpp>

// (a) You own the callbacks: forward what the UI should see.
glfwSetMouseButtonCallback(win, [](GLFWwindow* w, int b, int a, int m) {
    if (ui_under_cursor(w)) ctx().handle(slikui::glfw::mouse_button_event(w, b, a, m));
    else camera_button(w, b, a, m);
});

// (b) GlfwInput installs slikui's callbacks, CHAINING to the ones you set
//     before it (yours run first) and restoring them in its destructor. It
//     does not use the window user pointer.
slikui::glfw::GlfwInput input(win);        // after your own glfwSet*Callback
slikui::glfw::attach_services(ctx, win);   // clipboard + cursor shapes
while (!glfwWindowShouldClose(win)) {
    glfwPollEvents();
    input.dispatch(ctx);                   // or route input.events() yourself
    ...
}
```

`GlfwWindow` (`glfw_window.hpp`) is the ready-made option: it creates a
GL 3.3 core window and owns a `GlfwInput` (`win.input()`). One `GlfwInput`
per window; destroy it before the window (or call `release_window()`).

Python has the same three levels in `slikui.glfw_input` (pyGLFW):
`mouse_button_event(...)` and friends, `slikui.GlfwInput(win, app)` (chains,
`close()` restores; no target = queue, `take()`), and
`attach_services(ctx, win)`.

### SDL3

`slikui/backends/sdl/` mirrors the GLFW layers for SDL 3.2+. SDL delivers
input through its event queue rather than callbacks, so there is nothing to
install or chain - you hand slikui the events you poll:

```cpp
#include <slikui/backends/sdl/sdl_input.hpp>

slikui::sdl::SdlInput input(window);         // filters by window id, starts text input
slikui::sdl::attach_services(ctx);           // clipboard + cursor shapes (global in SDL)
SDL_Event ev;
while (SDL_PollEvent(&ev)) {
    if (!input.process(ev)) my_app(ev);      // quit, gamepads, other windows, ...
}
input.dispatch(ctx);                         // or route input.events() yourself
```

- `translate(const SDL_Event&, std::vector<Event>&)` is the stateless level
  (one SDL event can yield several: TEXT_INPUT is UTF-8, one `TextInput` per
  codepoint).
- `SdlWindow` (`sdl_window.hpp`) creates a resizable HiDPI window with a GL
  3.3 core context and owns an `SdlInput`; `dispatch(ctx)` pumps and
  translates, `on_event` gets what slikui doesn't use.
- **Buttons** are 1-based in SDL (LEFT=1, MIDDLE=2, RIGHT=3); `map_button`
  converts. **Keys** map by keycode, so shortcuts follow the user's layout
  (AZERTY Ctrl+Z is the key labelled Z). GLFW maps by physical position, so
  the two differ on non-US layouts.
- Python: `slikui.sdl_input` (PySDL3) - `translate(ev)`, `slikui.SdlInput(win,
  target)` with `process(ev)` / `take()`, `attach_services(ctx)`;
  `python/examples/sdl_demo.py`. PySDL3 downloads SDL binaries unless
  `SDL_DOWNLOAD_BINARIES=0 SDL_FIND_BINARIES=1 SDL_DISABLE_METADATA=1` (use a
  system SDL3).

### Sharing input with your scene

Hosts that draw their own scene under the UI ask the Context where each
event belongs, instead of hit-testing panels themselves:

```cpp
using T = slikui::Event::Type;
bool pointer = e.type == T::MouseMove || e.type == T::MousePress ||
               e.type == T::MouseRelease || e.type == T::Scroll;
bool to_ui = pointer ? ctx.wants_mouse(e.pos) : ctx.wants_keyboard();
```

`wants_mouse(pos)` is true during a UI press or drag (so the release comes
back to the UI), with a modal dialog open, or over any popup, dialog or
widget that is not `mouse_transparent()`. Pure layout - `Column`, `Row`,
`Spacer` - is transparent, so a full-window layout with a docked panel lets
the pointer through everywhere except the panel. Custom layout-only widgets
override `mouse_transparent()` to return true (Python too).
`wants_keyboard()` is true when a widget has focus or a modal is open.
`examples/06_sdl_opengl` and `python/examples/glfw_demo.py` route this way.

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

- **instances** - one `GpuInstance` (20 floats: rect, rgba, uv in texels,
  shape `{kind, radius, border, line half-thickness}`, line endpoints) per
  drawable command, in logical px. Commands entirely outside their clip,
  transparent fills and empty quads are culled.
- **batches** - `{first, count, tex_id, scissor x/y/w/h}` runs sharing one
  scissor and one texture. Scissors are framebuffer px, bottom-left origin,
  ready for `glScissor`. Shapes never break a texture run, so a typical
  frame is a handful of batches.

A sink uploads the instances once per frame and, per batch, sets the
scissor, binds the texture and draws `count` instances of a 4-vertex
triangle strip. The reference GLSL 330 shaders (`kGpuVertexShader`,
`kGpuFragmentShader`) take the five vec4 attributes at locations 0-4 with
divisor 1, and the uniforms `u_res`, `u_scale`, `u_tex`. Blend with
`glBlendFuncSeparate(SRC_ALPHA, ONE_MINUS_SRC_ALPHA, ONE,
ONE_MINUS_SRC_ALPHA)` - the software sink's straight-alpha "over".

The shaders reproduce the software rasterizer's coverage (SDF rounded
rects/borders, capsule lines, bilinear Alpha8, nearest Rgba8):
`tests/gl_parity.cpp` renders shapes and a full widget tree through
`OpenglSink` offscreen and requires them to match the software sink to
2/255 per channel. Both sinks cover a glyph/image quad by **pixel center**:
pixel (x, y) is drawn when (x + .5, y + .5) lies inside the quad and its
index lies inside the clip (the glScissor rule) - a CPU sink should do the
same, or fractional glyphs pick up a row of their atlas neighbour.

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
every frame as a flat command list - point it at pygame, moderngl, PIL,
anything. `slikui.CanvasSink` is a worked example: a pure-numpy rasterizer
producing **pixel-identical** output to the C++ software backend (verified by
test). Details: [Python bindings](../reference/python-bindings.md).
