# Input & events

Backends translate native events into one `slikui::Event` vocabulary;
`Context::handle` routes them. Your application code rarely constructs events
by hand in C++ — the [window layer](backends.md) does it — but the vocabulary
is the same on every platform and it is exactly what you translate (or
synthesize in tests) yourself.

## The event vocabulary

| Event | Carries | Notes |
|-------|---------|-------|
| `MouseMove` | `pos`, `delta` | framebuffer pixels, y-down |
| `MousePress` / `MouseRelease` | `button`, `pos`, `mods` | routed through hit-testing |
| `Scroll` | `pos`, `scroll` | wheel ticks, may carry mods |
| `KeyPress` / `KeyRelease` | `key`, `mods` | focus-routed |
| `TextInput` | `codepoint` | Unicode scalar, focus-routed |
| `FocusLost` | — | clears focus state |
| `FrameResize` | `pos` = pixel size | converted to the logical frame |

=== "C++"

    ```cpp
    // You normally receive these from the window layer; synthesizing works
    // the same way (headless tests do exactly this):
    ctx.handle(slikui::Event::mouse_press({60, 40}));
    ctx.handle(slikui::Event::text_input('h'));
    ```

=== "Python"

    ```python
    ui.handle(slikui.mouse_press((60, 40)))   # click through hit-testing
    ui.handle(slikui.text_input("h"))         # into the focused TextField
    ui.handle(slikui.scroll(pos, (0, -1)))    # wheel, "ctrl+shift" mods supported
    ```

Python event constructors (`slikui.mouse_press`, `text_input`, `scroll`,
`key_press`, ...) build real `Event` values that drive the real dispatch path
— the simulated-input tests use them.

![A synthesized press opens this Dropdown popup — the capture harness drives
exactly these constructors through the real dispatch path](../images/views.png)

## Routing rules

1. **Hit-testing** — the deepest visible widget containing the pointer wins.
2. **Bubble up** — the event dispatches up the parent chain until a widget
   consumes it (widgets return `true` from `on_event`).
3. **Focus** — key/text events go to the focused widget. Clicking focuses the
   nearest focusable ancestor of the hit; <kbd>Tab</kbd> cycles focusables in
   tree order.
4. **Popups** — clicking outside a popup dismisses it. Popups are owned by
   the context and destroyed at the end of `handle`, never during dispatch.
5. **Mouse capture** — widgets can `ctx.capture_mouse(this)` to receive
   every subsequent move/release (drag interactions) and must
   `release_mouse` when done.

## Watching events: the shipped EventProbe demo

`slikui.examples.events` is a custom widget whose whole job is narrating
every event it receives into a LogView — hover, click, drag, scroll,
right-click context menu, focus-routed keys and text, and a manual
double-click detector (slikui events carry no timestamps; the probe times
press pairs against the tick clock). Its `--smoke` pass drives the whole
vocabulary synthetically and asserts each event arrives exactly where the
routing rules above say it should. The probe itself is plain Python:

--8<-- "includes/examples/eventprobe.md"

Two contract facts the demo teaches the hard way:

- **KeyRelease is not dispatched** today — `Context::handle` drops it
  (presses, text and shortcuts route; releases don't), so key-up-driven
  widgets must plan around it.
- **No mouse capture in the Python bindings yet** (the C++ `capture_mouse`
  above has no trampoline) — moves and releases outside a widget's bounds
  are never delivered, so a drag ends when the pointer leaves and a lost
  release must be handled defensively; the probe logs a warning when a
  press arrives while it is still "dragging".

Run it: `python -m slikui.examples.events` (headless screenshot) or
`--smoke` for the scripted pass.

## Menus, context menus and shortcuts

`MenuBar`/`Menu` are built once and opened as borrowed popups; any widget can
carry a right-click menu; global accelerators run before widget dispatch (a
modifier is required, so typing never triggers one). Full details and code:
[Menus & toolbars](../reference/widgets-menus.md).

=== "C++"

    ```cpp
    viewport.set_context_menu(&view_menu);          // right-click
    ctx.add_shortcut(slikui::Key::R, slikui::ModCtrl, render_fn);
    ```

=== "Python"

    ```python
    viewport.set_context_menu(view_menu)            # right-click
    ctx.add_shortcut(slikui.Key.R, slikui.mods("ctrl"), render_fn)
    ```

![The View menu opened as a borrowed popup over a viewport — the capture calls
`open_context_menu`, which is exactly what a right-click does](../images/contextmenu.png)

## Clipboard

`TextField`/`TextArea` read and write the host clipboard through
`ctx.set_clipboard_source` / `ctx.set_clipboard_sink`. The GLFW backend wires
the system clipboard; without a host the Ctrl/Cmd+A/C/X/V shortcuts are
silent no-ops.

## Tooltips

Any widget carries a tooltip with `set_tooltip("...")`. It appears after a
~0.6 s hover delay and dismisses on mouse-away or click. Because setters
return `Widget&` (no deducing-this before C++23), base-class setters like
`set_tooltip` must come **last** in a chain.

=== "C++"

    ```cpp
    auto& row = root.add<slikui::Row>();
    row.set_gap(10);
    row.add<slikui::Button>("Save");
    row.add<slikui::Button>("Delete all")
        .set_tooltip("Removes every item — this cannot be undone");
    ```

=== "Python"

    ```python
    row = root.add(slikui.Row, gap=10)
    row.add(slikui.Button, "Save")
    row.add(slikui.Button, "Delete all",
            tooltip="Removes every item — this cannot be undone")
    ```

![tooltip](../images/tooltip.png)

## Cursor shapes

Widgets request cursor shapes with `set_cursor(slikui::Cursor::...)` — hosts
map them to system cursors via `ctx.set_cursor_host` (`GlfwWindow::attach`
does this; in Python `ctx.set_cursor_host(fn)`).
