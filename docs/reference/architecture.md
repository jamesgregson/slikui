# slikui architecture

## Module map

```
include/slikui/
├── core/            math (Vec2/Rect/Padding), Color, Event, Time, utf8
├── layout/          Constraints, CrossMode, flex distribution
├── render/          DrawList (display list), IRenderSink (the only backend contract)
├── style/           Theme (palette + metrics), Anim (tween), easing
├── text/            Font (stb_truetype), glyph atlas, layout/wrap/measure
├── widgets/         Widget base + the widget set (containers, controls,
│                    inputs, views, Image; textures flow through collect_uploads)
├── context.hpp      Context: tree ownership, input routing, focus, popups,
│                    hover tooltips, frame loop
└── backends/
    ├── software/    CPU rasterizer over raw uint32_t pixels
    ├── opengl/      GL 3.3 core renderer (system headers, no loader)
    └── glfw/        optional window + event translation (guarded by __has_include)
```

Dependency direction is strictly downward: widgets depend on render/text/style,
nothing depends on a specific backend except `context.hpp` (which only knows
the `IRenderSink` interface).

## The frame pipeline

```cpp
ctx.handle(event);   // 1. input
ctx.tick({now, dt}); // 2. attach -> layout -> animation
ctx.render();        // 3. paint -> sink
```

1. **Input** — backends translate native events into one `slikui::Event`
   vocabulary. `Context::handle` routes them: hit-test the deepest visible
   widget containing the pointer, then dispatch the event **up the parent
   chain** until consumed. Key/text events go to the focused widget. Clicking
   focuses the nearest focusable ancestor of the hit; Tab cycles focusables in
   tree order. Clicking outside a popup dismisses it (destroyed at the end of
   `handle`, never during dispatch).

2. **Tick** — `attach_tree` gives every widget a `Context*` (fonts/theme) on
   first sight. Then the root is measured and arranged top-down: every widget
   implements

   ```cpp
   Size measure(const Constraints&); // intrinsic size under bounds
   void arrange(const Rect& abs);    // assign own absolute frame + children
   ```

   Frames are **absolute framebuffer coordinates**, which makes hit-testing,
   painting and clipping trivial. Finally each widget's `tick(Time)` runs —
   animation state (`Anim`) advances here.

3. **Paint** — `paint_tree` walks the tree; widgets append commands to a
   `DrawList` via `PaintEnv` (draw list + theme + font). Containers may
   provide `clip_rect()` to clip their subtree. The finished list goes to the
   sink; popups are painted after the root.

## Layout model

Flexbox-flavoured: `Column`/`Row` stack children along their main axis with a
`gap` and `padding`; cross-axis alignment is `Stretch`/`Start`/`Center`.
Children that report `expands_main()` (Spacer) split leftover main-axis space.
`distribute()` in `layout/layout.hpp` holds the shared math and is unit-tested.

## Text pipeline

`text::Font` owns the TTF bytes, an stb_truetype fontinfo, a 512x512 alpha8
atlas and a glyph cache keyed by (codepoint, size). Glyphs rasterize on
demand; the atlas is pushed to the sink when dirty. `layout(text, px,
max_width)` produces positioned glyphs with word wrapping; `advance_upto`
powers caret hit-testing. Layouts are cached per widget (`Label`, `ListView`,
`Tabs`) and invalidated on text/size changes.

## Extension points

- **New widget**: subclass `Widget`, implement `measure`, `arrange` (default
  assigns `bounds`), `paint`, `on_event`, optionally `tick`, `focusable`,
  `clip_rect`, `expands_main`.
- **New backend**: implement `IRenderSink` — see
  [guide/backends.md](../guide/backends.md). ~100-400 lines.
- **Theming**: mutate `Context::theme()`; widgets read it every paint. There
  is no cached styled state to invalidate.

## Threading

Single-threaded. One `Context` per thread; don't touch a context from other
threads. The example loop drives everything from the main thread.

## Resize

Layout is recomputed from scratch every tick, so window resizes reflow
automatically — there is no cached geometry to invalidate. The pieces that
make resize correct:

- The backend emits `Event::FrameResize` with the **framebuffer** size;
  `Context::handle` converts it to the logical layout frame by dividing by
  `Theme::scale` (apps may also call `set_frame_size(logical)` directly).
- Labels re-wrap to their available width: with no explicit `wrap_width`, a
  Label wraps to `Constraints::max.x`, so narrower windows produce taller
  blocks.
- `ScrollArea` clamps its scroll offset whenever content/viewport heights
  change; popups are re-measured and re-clamped to the frame every tick.
- `GlfwWindow::set_min_size(w, h)` optionally floors the window size.
- HiDPI scale changes (window dragged between displays) are picked up on the
  next frame; glyphs re-rasterize at the new size automatically.

## Memory

The tree is `std::unique_ptr`-owned (`Context::set_root`, `Widget::add`).
Callbacks (`on_click`, `on_change`, ...) capture app state by
reference/pointer — keep captures valid or remove the widget first. Popup
widgets are owned by the context until the end of the event that closes them.
