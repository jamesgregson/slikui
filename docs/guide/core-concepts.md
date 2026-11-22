# Core concepts

slikui is a **retained-mode widget tree** driven by a three-call frame loop.
You build the tree once, keep it alive, and each frame the context routes
input, relayouts, animates, and paints. There is no immediate-mode API, no
per-frame widget construction, and no cached styled state to invalidate.

## The frame loop

Every application drives a `slikui::Context` with the same three calls:

```cpp
ctx.handle(event);   // 1. input  - hit-test, focus, popups
ctx.tick({now, dt}); // 2. layout - attach -> measure -> arrange -> animation
ctx.render();        // 3. paint  - DrawList -> your sink
```

=== "C++"

    ```cpp
    while (running) {
        while (auto event = window.poll_event())
            ctx.handle(event);
        ctx.tick(slikui::Time{now, dt});
        ctx.render();
        sink.present();
    }
    ```

=== "Python"

    ```python
    ui = slikui.App(480, 320)          # Target + Sink + Context + font
    # ... build the tree ...
    while running:
        ui.handle(slikui.mouse_press((60, 40)))   # translated input events
        ui.tick(now, dt)
        ui.render()                               # -> (H, W) uint32 ndarray
    ```

![The loop drives everything - the GLFW + OpenGL demo is exactly these three
calls around a window](../images/cube_viewer.png)

The Python [`App`](../getting-started/python.md) shell wraps Target + Sink +
Context (plus font loading) and returns the framebuffer as a zero-copy
`numpy` array from `render()`.

## What each stage does

**1. Input (`handle`).** Backend/window layers translate native events into
one `slikui::Event` vocabulary (see [Input & events](input-events.md)).
`Context::handle` hit-tests the deepest visible widget under the pointer, then
dispatches the event *up the parent chain* until some widget consumes it.
Key and text events go to the focused widget. Clicking outside a popup
dismisses it - popups are destroyed at the end of `handle`, never during
dispatch.

**2. Tick (`tick(Time{now, dt})`).** First `attach_tree` hands every widget a
`Context*` (fonts, theme) on first sight. Then the root is measured and
arranged top-down: every widget implements

```cpp
Size measure(const Constraints&);  // intrinsic size under bounds
void arrange(const Rect& abs);     // assign own absolute frame + children
```

Frames are **absolute framebuffer coordinates**, which makes hit-testing,
painting and clipping trivial. Finally each widget's `tick(Time)` runs -
hover/press transitions and the `Anim` tween helper advance here.

**3. Paint (`render`).** `paint_tree` walks the tree; widgets append commands
to a flat `DrawList` through `PaintEnv` (draw list + theme + font). Containers
may provide `clip_rect()` to clip their subtree; hit-testing honors the same
clip, so content clipped out of view can't be clicked. The finished list goes to the
[render sink](backends.md); popups paint after the root, then toasts, then
dialogs on top.

## Layout in one paragraph

Flexbox-flavoured: `Column`/`Row` stack children along their main axis with a
`gap` and `padding`; cross-axis alignment is `Stretch`/`Start`/`Center`.
Children whose `expands_main()` is true (e.g. `Spacer`) split leftover
main-axis space. Layout is recomputed from scratch every tick, so window
resizes reflow automatically - see [Layout](layout.md) and the full
[architecture](../reference/architecture.md) notes.

## Ownership and lifetime

- The tree is `std::unique_ptr`-owned: `ctx.set_root<T>(...)` and
  `widget.add<T>(...)` construct and own. You keep references, not owners.
- Callbacks (`on_click`, `on_change`, ...) capture app state by
  reference/pointer - **keep captures valid** for the tree's lifetime, or
  remove the widget first.
- `widget.remove(child)` / `clear_children()` destroy a subtree - immediately
  between frames, or at the end of the running `handle()`/`tick()` when
  called from a handler, so a widget may remove itself in its own callback.
- Popup widgets are owned by the context until the end of the event that
  closes them. Menus are the exception: they are *borrowed* popups you build
  once and open many times.
- In Python the bindings are non-owning wrappers over the C++ tree;
  `App.close()` (also run by `with App(...)`) destroys the tree and releases
  the Python callbacks it holds. A widget you pass to `push_popup`,
  `open_context_menu` or `open_dialog` is kept alive by the context while
  it is open, so a temporary is fine. A `Menu` given to `set_context_menu` is
  not: keep that Python object alive yourself, typically as an attribute of
  the widget it belongs to.

## Threading

Single-threaded. One `Context` per thread; never touch a context from another
thread. Drive everything from the main thread (or the thread that owns the
window).

In Python, `Context.tick`, `render` and `handle` (and the `App` methods that
call them) release the GIL while the C++ work runs. Other Python threads,
such as an audio callback or a worker, keep running during a frame. Your
`paint` / `on_event` overrides, callbacks and a Python `RenderSink` still run
with the GIL, on the UI thread.

That also means the GIL no longer stops another thread from changing the
widget tree mid-frame, so the single-thread rule matters more. A worker
thread must not touch widgets. Send its results to the UI thread (a
`queue.Queue` you drain each frame, say) and update widgets there.

## Where to go next

- [Getting started - C++](../getting-started/cpp.md) or
  [Python](../getting-started/python.md) for a runnable first window.
- [Layout](layout.md) for the container set.
- [Input & events](input-events.md) for the event vocabulary, focus and
  shortcuts.
- [Render backends](backends.md) to run on GL, raw pixels, or your own API.
