# Getting started — C++

Header-only: include `slikui.hpp` (multi-header, `include/` on your include
path) or `dist/slikui.hpp` (amalgamated single file — also attached to every
GitHub Release). No linking, no code generation, C++20.

## A first window

Minimal GLFW + OpenGL app — `examples/01_hello` is exactly this:

```cpp
#include "slikui.hpp"                             // the library
#include <slikui/backends/opengl/opengl.hpp>      // GL 3.3 core renderer
#include <slikui/backends/glfw/glfw_window.hpp>   // window + event translation
#include <slikui/text/system_fonts.hpp>           // find_system_font()

int main() {
    slikui::glfw::GlfwWindow window(480, 320, "hello");
    slikui::opengl::OpenglSink sink(window);
    slikui::Context ctx(sink);
    ctx.load_font_file(slikui::find_system_font());

    window.attach(ctx);
    auto& root = ctx.set_root<slikui::Column>();
    root.set_padding(16).set_gap(8);
    root.add<slikui::Label>("Hello, slikui!");
    root.add<slikui::Button>("Save")
        .set_variant(slikui::Button::Variant::Primary);

    while (!window.should_close()) {
        window.poll_events();            // -> ctx.handle(event)
        ctx.tick(slikui::Time{});        // layout + animation
        ctx.render();                    // -> DrawList -> GL
    }
}
```

![The same tree, rendered headless by the capture tool — the software backend
consumes the identical DrawList the GL sink gets](../images/hello.png)

## Headless — raw pixels

The software backend needs no window or GPU: it antialiases into an owned or
borrowed `uint32_t` framebuffer. `examples/02_raw_pixels` writes a PPM this
way — this is also how the docs' component screenshots are produced.

```cpp
#include <slikui/backends/software/software.hpp>

slikui::software::Target target(480, 320);   // raw pixels
slikui::software::Sink sink(target);
slikui::Context ctx(sink);
ctx.set_frame_size({480, 320});
ctx.load_font_file(slikui::find_system_font());
// ... build the tree, tick, render ...
// target.pixels() -> const uint32_t*
```

## The include tree

```
include/slikui/
├── core/            math (Vec2/Rect/Padding), Color, Event, Time, utf8
├── layout/          Constraints, CrossMode, flex distribution
├── render/          DrawList (display list), IRenderSink
├── style/           Theme (palette + metrics), Anim (tween), easing
├── text/            Font (stb_truetype), glyph atlas, wrap/measure
├── widgets/         Widget base + the widget set (all.hpp = everything)
├── context.hpp      Context: tree ownership, input, popups, frame loop
└── backends/        software/ · opengl/ · glfw/ (self-gating on __has_include)
```

Every public header compiles standalone (`tools/check_headers.sh` enforces it
in CI). The GL/GLFW backends self-gate: without the system headers installed
they compile to nothing.

## Next

- [Core concepts](../guide/core-concepts.md) — the frame loop in detail
- [Widget index](../reference/index.md) — everything you can put on screen
- [Building the demos](../development.md) — cmake targets, the gallery,
  the showcase
- [Render backends](../guide/backends.md) — port to Vulkan/Metal/SDL
