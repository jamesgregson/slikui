# Changelog

## Unreleased

### Added

- **`IRenderSink::release(id)`** (optional, default no-op): called when an
  `Image`, `ImageView` or `CompareView` is destroyed, so sinks can free its
  texture. Previously every image widget leaked its texture for the life of
  the sink. The software and OpenGL sinks implement it (OpenGL defers the
  `glDeleteTextures` to the next `render()`).
- **`Context::release_texture(id)`** and **`Context::invalidate_textures()`**
  (re-upload the font atlas and every live widget texture after a sink lost
  them).
- Python: **`DrawList.as_array()`** — zero-copy read-only structured view of
  the frame's commands (`slikui.DRAWCMD_DTYPE`), ~18× cheaper than iterating
  `DrawCmd` objects. **`RenderSink.release()`** is overridable;
  `Sink.texture_count` / `CanvasSink.texture_count` for leak checks.
- **`GpuBatcher`** (`render/batcher.hpp`, API-agnostic): DrawList → one
  20-float instance per drawable command + scissor/texture batches, with
  reference GLSL 330 shaders matching the software rasterizer's coverage.
- Python: **`slikui.GLSink`** (PyOpenGL `RenderSink` on `GpuBatcher`) and
  **`slikui.GlApp`** (App-like shell over it): the UI draws straight into
  your GL context. ~0.5 ms/frame on the gallery vs ~3 ms for the C++
  software sink (~55 ms for `CanvasSink`). Also `slikui.GpuBatcher`,
  `GpuKind`, `GPU_VERTEX_SHADER` / `GPU_FRAGMENT_SHADER`,
  `read_framebuffer()`.
- Python: **`slikui.ModernGLSink`** — the same sink over a
  `moderngl.Context` (~0.36 ms/frame on the gallery).
- **GLFW input without the window**: `backends/glfw/glfw_input.hpp` —
  event builders (`mouse_button_event`, `cursor_pos_event`, `key_event`,
  ...), `map_button`, `GlfwInput` (installs slikui's callbacks on YOUR
  `GLFWwindow*`, chaining to existing ones and restoring them on
  destruction, without touching the user pointer) and `attach_services`
  (clipboard + cursors). `GlfwWindow` is now window creation on top of it
  (same API, plus `input()`). Python twin: `slikui.glfw_input` /
  `slikui.GlfwInput`.
- **SDL3 backend** (`backends/sdl/`, SDL 3.2+), same layering as GLFW:
  `sdl_input.hpp` — `translate()` (SDL_Event → slikui events), `SdlInput`
  (one window's events, text input, queue/dispatch), `attach_services`;
  `sdl_window.hpp` — `SdlWindow` (window + GL 3.3 context + input).
  Python: `slikui.sdl_input` / `slikui.SdlInput` (PySDL3). New
  `examples/06_sdl_opengl`, `python/examples/sdl_demo.py`; tests
  `slikui_sdl_input` (SDL's real event queue, dummy video driver) and
  `slikui_sdl_gl_parity`.
- **`Context::wants_mouse(pos)` / `wants_keyboard()`** for hosts that share
  input with their own scene, and `Widget::mouse_transparent()` (true for
  `Column`, `Row`, `Spacer`; overridable, also from Python).
  `python/examples/glfw_demo.py` uses them instead of its own panel test,
  so its hint box now counts as UI.
- `tests/gl_parity.cpp` (ctest `slikui_gl_parity`, built when GLFW is
  found; skips without a display): `OpenglSink` vs software sink offscreen,
  2/255 per channel.
- `python/benchmarks/bench_render.py`: ms/frame per sink on the gallery.

### Changed

- **`OpenglSink` rewritten on `GpuBatcher`**: instanced quads (80 bytes per
  command instead of 6 × 68-byte vertices), one draw per batch, blending
  via `glBlendFuncSeparate` so destination alpha is correct when rendering
  into a transparent FBO. RGBA images now sample `GL_NEAREST` (as the
  software sink does; was `GL_LINEAR`). New `shutdown()`; disables
  depth/cull/stencil in `render()`.
- Python `--window` examples and `python/examples/glfw_demo.py` draw the UI
  with `GlApp`; `--software` keeps the CPU rasterizer + texture blit.
- Python: **`TexUpload.pixels` is a zero-copy read-only ndarray view**
  (`(H, W)` uint8 for Alpha8, `(H, W, 4)` for Rgba8) instead of `bytes`.
  `bytes(up.pixels)` still works; the view is valid only during `upload()`.

### Fixes

- **Software sink (and `CanvasSink`): glyph/image quads cover pixels by
  center.** They looped from `int(x)`/`int(y)`, so a fractionally placed
  glyph also painted the row/column whose center lies outside it, sampling
  past its atlas cell — faint dots from the neighbouring glyph above some
  letters ("Ġ", "Ċ"). Text now matches the GPU sinks to 2/255. Text pixels
  at glyph edges shift slightly in software output.
- **GLFW mouse buttons: right and middle were swapped.** GLFW numbers them
  LEFT=0, RIGHT=1, MIDDLE=2; slikui uses MouseLeft=0, MouseMiddle=1,
  MouseRight=2. `GlfwWindow` passed GLFW's number through unmapped, as did
  the Python `--window` host and `python/examples/showcase.py` — a right
  click arrived as a middle click and vice versa, so context menus opened
  on middle-click and never on right-click. `python/examples/glfw_demo.py` was already correct.
- `GlfwWindow` dropped the modifier state of mouse presses/releases
  (Shift-click arrived without Shift).
- `python/examples/showcase.py` only forwarded a handful of keys and only
  vertical scroll; it now uses `GlfwInput` like the other hosts.
- Images inside a `Dialog` are now uploaded (dialogs were skipped when
  collecting texture uploads, so they drew nothing).
- A texture widget moved to another `Context` re-uploads its pixels under
  its new id (it was never marked dirty, so it drew nothing).
- `OpenglSink` drew nothing for zero-length lines; the software sink draws
  a dot, and now both do.
- `python/examples/glfw_demo.py` (software UI path): translucent panels
  accumulated alpha frame over frame until opaque (the UI target was never
  cleared), and the blit multiplied alpha twice.

## 0.3.0

The input model now delivers what custom widgets expect, and the Python
package documents itself.

### Behavior changes (C++ and Python)

- **Every mouse button reaches widgets.** Middle and right presses are now
  dispatched (previously only left presses were; right *releases* already
  were, unpaired). Built-in widgets act on the left button only, so a
  right- or middle-click no longer activates buttons, drags sliders, etc.
  A right press on a widget with a context menu still opens the menu; that
  press and its release are not delivered.
- **`KeyRelease` is dispatched** to the focused widget (else the hovered
  one), only for presses the tree received — no orphan releases for Tab,
  Escape or shortcut chords.
- **Key presses with nothing focused** go to the hovered widget instead of
  hit-testing the event's unset position.
- **`FocusLost` reaches widgets**: the widget losing focus receives it, and
  so does the mouse-capture widget when the app loses focus (cancel drags
  there). Previously it only reset Context state.
- **`Button` and the toolbar's `IconButton` fire on `Click`** and un-hover /
  disarm on `MouseLeave` (previously the hover highlight and pressed state
  could stick after the pointer left).
- **Shift+Tab** now moves focus backwards (it re-selected the current
  widget).

### Fixes

- **Text at 1x no longer picks up slivers of neighboring glyphs.** The
  software renderers (C++ and Python) sampled glyphs half a texel off, which
  also softened all text slightly; they now sample at texel centers like
  the OpenGL backend.
- **Text laid out during paint shows on its first frame.** The font atlas
  was uploaded before painting, so glyphs first needed in `paint` (the
  Slider's value, TextField metrics) were missing for a frame, and a
  Slider's value text vanished on frames that showed new digits.
- **Shortcut hints never show missing-glyph boxes.** Menus spell out any
  symbol the font lacks (`⌃⇧S` → `Ctrl+Shift+S`, `←` → `Left`).
- Software renderer: RGBA image sampling clamped the row against the
  texture's width instead of its height.

### New

- Event types `Click`, `DoubleClick`, `MouseEnter`, `MouseLeave`,
  `FocusGained` (appended: existing enum values are unchanged).
- `Event::clicks` — consecutive-click count on press/release/click
  (`kMultiClickTime` 0.4 s on the tick clock, `kMultiClickSlop` 4 px);
  `Event::repeat` for auto-repeat key presses.
- `Key::Num0` … `Key::Num9` (appended).
- `Widget::paint_over` — drawn after the children, for decorations.
- Destroyed widgets are dropped from the Context's focus / hover / capture
  / press state (no dispatch into freed widgets); widgets that outlive their
  Context are safe to destroy. `Context` has a destructor and is
  non-copyable.
- `Context::hovered()`.
- `Font::has_glyph(cp)`; `fit_shortcut_label(font, label)` and
  `shortcut_label(key, mods, font)`.
- `TextField`: double-click selects a word, triple-click selects all.
- GLFW backend: digit keys; auto-repeat is a press with `repeat` set (it
  was translated as a release); mouse move and scroll events carry the
  current modifiers.

### Python

- Mouse capture: `Context.capture_mouse(w)` / `release_mouse(w)`;
  `Widget.context`, `Widget.focused`, `Widget.request_focus()`,
  `Widget.set_cursor(...)`; `Context.set_focus`, `.focused`, `.hovered`,
  `.root`.
- Python overrides for `paint_over`, `clip_rect` and `attached`.
- `Event.clicks`, `.repeat`, `.super`, `.shortcut_mod`;
  `Event.MULTI_CLICK_TIME` / `MULTI_CLICK_SLOP`; `key_press(..., repeat=)`.
- **Custom widgets can keep references to their own children**
  (`self.slider = self.add(slikui.Slider)`, callbacks capturing them)
  without leaking. Widget-returning methods on widgets no longer keep
  their host's wrapper alive (a link the garbage collector could not
  see); wrappers are views, valid until the tree drops the widget.
- `shortcut_label(key, mods, context)` spells out symbols the context's
  font lacks.
- Every example file runs when copied out of the package, and every
  snippet the docs quote is self-contained (imports included).
- `slikui.Event` is exported (it was listed in `__all__` but missing, which
  broke `from slikui import *`).
- `EventType` is arithmetic (`int(e.type)` works); readable `repr()` for
  `Event`, `Rect`, `Vec2`, `Size`; `Label.text` and `Button.text` getters.
- Type stubs: `slikui/_slikui.pyi` ships in the wheel (generated by
  `tools/gen_stubs.py`, drift-checked by the test suite). Docstrings on the
  `Widget` protocol methods describe event delivery.
- Shipped examples: new `controls`, `keyboard`, `renderer` and `window`;
  `events` and `paint` rewritten for the new model.
  `python -m slikui.examples --where` and `--contract` (the event contract,
  measured live); a `slikui-examples` command. The wheel CI runs every
  example's smoke pass.
