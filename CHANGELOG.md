# Changelog

## 0.6.0

### Added

- **Python subclasses of `Panel`, `Column` and `Row`** dispatch overrides
  (`on_event`, `paint`, `measure`, ...) like `slikui.Widget` subclasses, and
  `super()` reaches the built-in behaviour. A `Panel` subclass used as a popup
  root now receives `PopupClosed`. `Column()` / `Row()` gain constructors for
  this.
- `FileBrowser::set_filters(globs)` (case-insensitive `*` / `?`; folders
  always listed), `set_show_hidden(bool)`, `commit()` and `list_scroll()`.
- `FileBrowser::Mode::Folder`: pick a directory. Only folders are listed,
  there is no name field, and Choose picks the selected folder (or the one
  being shown). `FileBrowser::mode()`.
- `IconButton::set_icon_painter(fn(list, area, color))` draws a custom
  icon in the tint a built-in would get. `set_icon` switches back. Python
  also gains `slikui.paint_icon(list, icon, area, color)` and
  `slikui.icon_name(icon)`.
- `Context::hold_while_open(widget, std::shared_ptr<void>)`: keep a token
  alive while a popup or dialog is open; it is released after the widget
  closes.
- **Per-part tooltips**: `Widget::tooltip_at(pos)` (overridable, Python
  too) gives the tooltip text for the pointer position. `TreeView::Row`
  gains `tooltip`, and `TreeView`, `ListView` and `Segmented` show the full
  text of a row, item or segment drawn cut off with "...".
  `Context::tooltip_text()` returns the text showing now.
- `ListView::on_activate(fn)`: double-click on an item.
- **Python: replayable `DrawList`s.** `slikui.DrawList()` can be created
  from Python. `append(other, offset)` replays a recorded list in one call,
  and `extend(array, offset)` appends from a `DRAWCMD_DTYPE` array. Both
  check the commands and reject unbalanced clips. `reserve(n)` and
  `clip_balanced` are also new. C++ gains
  `DrawList::append(const DrawCmd*, n, offset)`, and `append`'s offset now
  defaults to zero.
- `PaintEnv::visible_rect` (Python: `env.visible_rect`): the part of the
  frame a widget's drawing can show through, narrowed by every enclosing
  clip, so `paint()` can skip off-screen parts.
- `TreeView::on_toggle(fn(index, open))` for arrow clicks,
  `Row::expanded` (initial state, default open) and
  `set_natural_width(w)`.
- `Label::set_align(TextAlign)` / `set_valign(TextVAlign)` (Python: enum
  or `"center"` / `"middle"` / ...) place the text inside a label bigger
  than its text. Each wrapped line is aligned on its own. Default left/top
  as before.
- `Segmented::set_fill(bool)`: span the parent (Column width, Row leftover)
  with segments sharing the width evenly; `segment_rect(i)`.
- `MenuBar::set_background(Color)`.
- `TextField::scroll_x()`.
- `is_text_key(Key)`: whether a key types a character (letters, digits,
  punctuation, Space, keypad).
- Python: `TextArea.text` property; until now the text could only be read
  through `on_change`.
- Keypad keys: `Key::Kp0`-`Kp9`, `KpDecimal`, `KpDivide`, `KpMultiply`,
  `KpSubtract`, `KpAdd`, `KpEqual`, mapped by the GLFW and SDL backends (C++
  and Python) and labelled `Num0`, `Num+`, ... by `shortcut_label`.
- `Widget::destroy_hook`: a process-wide callback run from `~Widget` (null by
  default; the Python bindings use it).
- **130 new built-in icons** (156 in all), covering navigation (`ArrowLeft` -
  `ArrowDown`, `Home`, `Menu`, `MoreHorizontal`, `MoreVertical`,
  `ExternalLink`, `Maximize`, `Minimize`), editing (`Edit`, `Cut`, `Paste`,
  `Undo`, `Redo`, `Link`, `Filter`, `Sort`, `DragHandle`), files (`File`,
  `FolderOpen`, `Download`, `Upload`, `Share`, `Print`), view (`ZoomIn`,
  `ZoomOut`, `List`, `EyeOff`, `Image`, `Sidebar`, `Layers`), status
  (`Info`, `Warning`, `Error`, `Help`, `Bell`), marking (`Star`, `Heart`,
  `Pin`, `Bookmark`, `Flag`, `Tag`), media (`FastForward`, `Rewind`,
  `Repeat`, `Shuffle`, `Volume`, `Mute`, `Mic`, `Music`) and misc (`Unlock`,
  `Moon`, `User`, `Mail`, `Calendar`, `Clock`, `Globe`, `Power`, `Code`,
  `Terminal`) and 3D view manipulation (`Pan`, `RotateX`, `RotateY`,
  `RotateZ`, `RotateFree`; zoom uses `ZoomIn` / `ZoomOut` / `Search`) and
  3D editing (`Vertex`, `Edge`, `Face` selection modes; `Cube`, `Sphere`,
  `Cylinder`, `Cone`, `Plane` primitives; `Mesh`, `Wireframe`, `Solid`,
  `Texture`, `Material`, `UV`, `Normal`; `Modifier`, `Light`, `Bone`,
  `Keyframe`, `Curve`, `Extrude`, `Scale`, `Axes`, `Snap`, `Pivot`,
  `Hierarchy`, `Torus`), painting (`Brush`, `Eraser`, `Fill`, `Eyedropper`,
  `Airbrush`, `Select`, `Lasso`, `MagicWand`, `Crop`, `Gradient`, `Palette`,
  `Text`, `Stamp`, `Blur`) and vector drawing (`Pointer`, `DirectSelect`,
  `Pen`, `Rectangle`, `Ellipse`, `Polygon`, `Line`, `Union`, `Subtract`,
  `Intersect`, `AlignLeft` / `AlignCenter` / `AlignRight` / `AlignTop` /
  `AlignMiddle` / `AlignBottom`, `Group`, `Ungroup`, `BringForward`,
  `SendBackward`, `FlipH`, `FlipV`). Existing enum values are unchanged. `icon_count` gives the
  size of the set, and the docs show all icons in one image
  (`docs/images/icons.png`).

### Changed

- `Icon::Folder` and `Icon::Trash` are redrawn to be easier to recognise: a
  single folder outline with its tab, and a lidded bin with a handle.
- **`MenuBar` no longer fills `theme.surface`**: like `Toolbar`, it is
  transparent by default, so the window background shows (in the Light theme
  it used to be a white strip). `set_background` restores a fill.
- **`FileBrowser`: a single click on a folder selects it; double-click enters
  it** (it used to enter on one click). Double-clicking a file picks it, and
  Enter in the name field or Open with a folder selected acts likewise.
  Dotfiles are hidden by default.
- Python: defining a protocol method (`on_event`, `paint`, `measure`, ...) on
  a subclass of a built-in other than `Widget` / `Panel` / `Column` / `Row`
  raises `TypeError` at class creation; such overrides were silently ignored.
- Python: using a wrapper of a built-in widget after the tree destroyed it
  (`remove`, `clear_children`, `App.close()`) raises `TypeError` with a
  nanobind `RuntimeWarning`, instead of reading freed memory.
- **Python: `Context.tick`, `render` and `handle` release the GIL** while the
  C++ work runs, so other Python threads (audio callbacks, workers) keep
  running during a frame. Python overrides, callbacks and Python render sinks
  take the GIL back when called. Because the GIL no longer serialises other
  threads against the frame, touching widgets from a non-UI thread is now a
  real data race. It was never supported.
- Python: a widget passed to `push_popup`, `open_context_menu` or
  `open_dialog` is kept alive by the context while it is open. A temporary
  (`ctx.push_popup(Panel(), ...)`) used to be freed at once, and the next
  frame read freed memory. `set_context_menu` still borrows (keep the
  Menu alive), and now accepts `None` to clear.
- Python: `Context.set_cursor_host` takes a typed callable,
  `Callable[[Cursor], None]`.
- The vendored nanobind carries a small patch (`nb_inst_forget_refs`); see
  `third_party/nanobind/SLIKUI_PATCHES.md`.

### Fixed

- **`TreeView` cost scales with the rows on screen.** It used to draw every
  visible row each frame and measure every label (collapsed or not) on
  `set_rows`. A 40k-row tree's first frame took about 10 s and now takes
  about 20 ms. Paint skips rows outside `visible_rect`, and labels are
  measured lazily as rows become visible. The natural width is now the
  widest *visible* row, so it can change when rows are expanded or
  collapsed. `ListView` and `Table` also skip off-screen rows when
  painting.
- Text measuring slowed down linearly with the number of distinct
  strings: once the layout cache was full, every new string rescanned it.
  New strings now cost the same no matter how many have been seen.
- A focused `TextField`, `SearchBox`, `TextArea` or a `DragValue` being
  typed into now consumes the `KeyPress` of the characters it types. Before,
  the press bubbled up, so app shortcuts handled in an ancestor's
  `on_event` fired while the user typed (typing "g" ran a G shortcut).
  Ctrl/Cmd chords and keys like the F-keys still bubble.
- A showing tooltip ignored `set_tooltip` (new text or `""`) until the
  pointer left the widget. It now swaps or closes at once, and moving
  between rows of a list swaps per-row tips without the hover delay.
  Closing a tooltip no longer closes the app's other popups with it.
- `TextField` and `TextArea` typed two spaces per Space keystroke: one from
  the `KeyPress` and one from the `TextInput` the backends also send. Space
  is now inserted from `TextInput` only.
- `TextField` / `SearchBox`: text is clipped to the field. A long placeholder
  elides with an ellipsis instead of drawing over neighbouring widgets, and typed text
  wider than the field scrolls horizontally to keep the caret visible.
  Password fields place the caret and selection by the shown `*` glyphs.
- `Slider` and `Switch` no longer animate from the minimum / off position to
  their initial state: state set before the first tick snaps (a `tick(dt=0)`
  snapshot shows the real value). Later changes still animate.
- `FileBrowser` in a big folder grew past the window and hid the name field
  and Open/Cancel; the list now scrolls inside a dialog that fits the window.
  `list().set_selected(i)` now updates `selection()`.
- Python: `.children` (and any other widget-returning call) could hand back a
  stale wrapper of a different type when a new widget reused a destroyed
  one's address.

## 0.5.0

### Added

- **Custom widgets draw text** (C++ and Python): `env.text(pos, str, opts)`
  and `env.text_in(rect, str, opts)` draw in the theme font, size and color
  unless told otherwise, and return the rect the text occupies. Options
  (`TextOpts`) cover font family, size, color, bold, italic, underline,
  strikethrough, alignment, vertical anchor (top/middle/baseline/bottom),
  wrapping and overflow (clip/ellipsis). Python takes them as keyword
  arguments. Layouts are cached by the context, so per-frame calls are cheap.
- `Context::text_size`, `text_metrics` and `text_layout` (a kept
  `TextBlock` with `hit_test`, `caret_rect` and `selection_rects`, drawn with
  `env.draw`).
- **Font families**: `Context::add_font(family, path, style, face)` /
  `add_font_memory`, with roles `"body"`, `"heading"`, `"mono"` and custom
  names. Missing bold/italic faces are synthesized. `.ttc` collections load
  any face (`face = -1` picks it by style). `find_system_font_face(role,
  style)` finds installed sans/mono faces.
- `Label::set_bold`, `set_italic`, `set_underline`, `set_strike` and
  `set_font(family)`.
- `utf8_encode` / `utf8_append` in `core/utf8.hpp`.
- Text examples: `python -m slikui.examples.text` and its C++ twin
  `examples/07_text` (Label styles, font families, text-drawing custom
  widgets, drag-to-select on a `TextBlock`).

- `Widget::theme_changed()` (Python: override `theme_changed`): called on
  every widget on the tick after `Context::theme()` changed - UI scale,
  preset, palette or metrics. `Theme` gains `operator==`.
- `Context::toast_count()` / `toast(i)` (Python: `ctx.toast_count`,
  `ctx.toast(i)`); `push_toast` returns the toast widget.
- **`Widget::remove(child)` / `clear_children()`** (C++ and Python): destroy a
  subtree. Safe from event handlers and callbacks - a widget can remove
  itself in its own `on_click`; focus, hover, capture, press targets and
  popup anchors are dropped at once and destruction waits for the running
  `handle()`/`tick()`. `ScrollArea` forwards both to its content.
- **`Widget::set_expand(bool)`** (builder: `expand=True`): any widget shares
  leftover main-axis space like `Spacer` - e.g. a `ScrollArea` or `Splitter`
  filling the rest of a `Column`.
- **ScrollArea scroll API**: `scroll_y()`, `max_scroll()`, `set_scroll_y`,
  `scroll_to_top`, `scroll_to_bottom`, `scroll_to(rect)` /
  `scroll_to(widget)`; `TreeView::row_rect(i)` and `ListView::row_rect(i)`
  to reveal a row.
- `Key::Equal`, `LeftBracket`, `RightBracket`, `Backslash`, `Semicolon`,
  `Grave` (appended; existing values unchanged), mapped by the GLFW and SDL
  backends (C++ and Python). `shortcut_label` now names punctuation keys
  (`Ctrl+=`, `Ctrl+]`, ...) and `Insert`.
- Python builder: custom widget classes receive the kwargs their `__init__`
  names (`row.add(Knob, value=3, tooltip="t")` calls `Knob(value=3)`, then
  `set_tooltip`); previously every kwarg was treated as a setter.
- `Color::from_float(r, g, b, a = 1)` (Python `Color.from_float`) and
  Python `Constraints(min, max)`.
- Python `Container.set_padding(left, top, right, bottom)` (per side).
- **`Event::Type::PopupClosed`**: a popup's (or dialog's) root widget is told
  when it leaves the screen, whatever closed it - click outside, Escape,
  focus loss, a menu action, `close_popups`/`close_dialog`. Delivered after
  the closing event (reopening from the handler is safe) and skipped when the
  popup was reopened within the same pass. Owned popups are destroyed only
  after their notification.
- `Menu::on_close(fn)` (Python `Menu.on_close`), pairing `on_open`. Closed
  menus also drop their rows' hover highlight, so a reopened menu doesn't
  flash the last-hovered row.

### Changed

- **Toasts are their own passive layer** (above popups, below dialogs) instead
  of entries on the popup stack. They are never hit-tested, so clicks, hover
  and wheel reach whatever is under them, and they neither close nor are
  closed by popups (a press outside a menu, Escape, focus loss). Several
  toasts stack upward from the bottom-right corner, 12 px from the edges.
  `popup_count()` no longer counts toasts - use `toast_count()`.
- `Segmented` fits its frame: segments keep their natural width when there is
  room; otherwise short labels stay whole and the long ones shrink, eliding
  with "...".
- `TreeView` and `ListView` elide row labels that don't fit the frame.
- **Tighter `ScrollArea` spacing**: content sits 2 px inside the viewport
  (was 8) and 6 px from the scrollbar thumb (was 12), so scrolled content
  lines up with its siblings and highlights run close to the bar.
  `set_content_insets(left, right)` restores more room where wanted.

### Fixed

- **`Dialog::on_close` fires for every way a dialog closes**: Escape and
  `Context::close_dialog` skipped it (only the X button / `request_close`
  fired it). It now runs once, after the closing event.
- **An open toast no longer swallows clicks**: the first press anywhere
  outside it closed the toast and was dropped (the press never reached its
  widget, so no Click either).
- **Text widgets follow runtime UI-scale changes** (zoom): Button, Checkbox,
  Radio, Slider, Segmented, Tabs, Dropdown, Expander, DragValue, TreeView,
  ListView, Table, Plot, LogView, Menu/MenuBar, Toast and tooltips kept text
  laid out at the scale of their first layout, so their sizes and text
  drifted after a zoom.
- `Segmented` text no longer runs past the control (and its click targets
  past its frame) when it is narrower than its labels.
- `TreeView` / `ListView` row text no longer runs past the widget; `TreeView`
  no longer under-measures its width at UI scales above 1 (the indent part
  was divided by the scale).
- **Glyphs no longer vanish when the font atlas fills**: the atlas grows by
  512x512 pages (each its own texture) instead of silently dropping glyphs.
- Font sizes that differed by less than 1 px (e.g. 15.2 and 15.6) shared one
  glyph raster; the cache now keys on 1/4-px size steps. Text at fractional
  HiDPI scales can shift by up to 1/8 px as a result.
- `utf8_next` rejects overlong encodings, surrogates, values above U+10FFFF
  and sequences truncated at the end of the string (all decode as U+FFFD).
- **Single-line `Label`s no longer wrap at high UI scale** (about 2.5x and
  up): `measure` reported the ink width, but wrapping decides on the advance
  width, which is several raster px wider at large sizes, so the last word
  dropped onto a second line outside the label's frame.
- **`TreeView::set_rows` re-labels rows**: after the first layout, rows kept
  their old text, and growing the row count read past the end of the label
  cache.
- `TreeView` chevrons point down when a row is expanded (both states drew
  `>`).
- **`CrossMode::Center` centers every child**: children whose
  `stretch_cross()` is false (Button, Switch, most custom widgets) were
  top-aligned regardless of the cross mode.
- **Children added to an attached widget join its context immediately.**
  Widgets that build children lazily (e.g. in `measure`) work inside popups
  and dialogs - their labels never drew there - and draw on their first frame
  everywhere. Popups and dialogs are also re-attached every tick, like the
  root tree.
- `Menu` items and `MenuBar` titles clear their hover highlight when the
  pointer leaves (several items could show hover at once).
- **Menu rows share the menu's width**: each item used to be only as wide as
  its own text, so hover highlights had different widths and shortcut hints
  didn't line up. Labels sit 12 px in, keeping the 32 px checkmark gutter
  only in menus that have a checkable item; a hint on a submenu row no
  longer overlaps its chevron.
- The software renderers (`software::Sink`, `CanvasSink`) clip lines: lines
  bled a pixel past clip edges, and a line entirely outside the clip could
  paint a full row or column along its edge at fractional scales.

## 0.4.1

### Fixed

- **Clipped content is no longer pickable.** Hit-testing now honors
  `clip_rect()`: widgets scrolled out of a `ScrollArea` (e.g. behind a tab
  header) no longer steal clicks, hover, tooltips or context menus from what
  is actually drawn there.
- **Tab focus scrolls into view.** Moving keyboard focus with Tab/Shift+Tab
  scrolls every enclosing `ScrollArea` so the focused widget is visible.
- **Hover highlights clear on leave** in `ListView`, `Tabs`, `TreeView`,
  `Table`, `Segmented` and the `Splitter` divider.
- **`Splitter` clips each pane**: content overflowing a squeezed pane no longer
  paints or picks over the divider or the other pane.

### Added

- `Widget::child_clip_rect(child)` (per-child clip, honored by painting and
  hit-testing) and `Widget::scroll_into_view(rect)` hooks.

### Removed

- `Widget::dispatch(Event)`: an unused broadcast helper that ignored pointer
  position and clipping. Route events through `Context::handle`.

## 0.4.0

Added OpenGL renderer support in python. Batcher collects draw calls and is
reused across most renderers. Split input from windowing with GLFW. Added 
SDL3 support. Miscellaneous bug fixes.

### Added

- **`IRenderSink::release(id)`** (optional, default no-op): called when an
  `Image`, `ImageView` or `CompareView` is destroyed, so sinks can free its
  texture. Previously every image widget leaked its texture for the life of
  the sink. The software and OpenGL sinks implement it (OpenGL defers the
  `glDeleteTextures` to the next `render()`).
- **`Context::release_texture(id)`** and **`Context::invalidate_textures()`**
  (re-upload the font atlas and every live widget texture after a sink lost
  them).
- Python: **`DrawList.as_array()`** - zero-copy read-only structured view of
  the frame's commands (`slikui.DRAWCMD_DTYPE`), ~18x cheaper than iterating
  `DrawCmd` objects. **`RenderSink.release()`** is overridable;
  `Sink.texture_count` / `CanvasSink.texture_count` for leak checks.
- **`GpuBatcher`** (`render/batcher.hpp`, API-agnostic): DrawList -> one
  20-float instance per drawable command + scissor/texture batches, with
  reference GLSL 330 shaders matching the software rasterizer's coverage.
- Python: **`slikui.GLSink`** (PyOpenGL `RenderSink` on `GpuBatcher`) and
  **`slikui.GlApp`** (App-like shell over it): the UI draws straight into
  your GL context. ~0.5 ms/frame on the gallery vs ~3 ms for the C++
  software sink (~55 ms for `CanvasSink`). Also `slikui.GpuBatcher`,
  `GpuKind`, `GPU_VERTEX_SHADER` / `GPU_FRAGMENT_SHADER`,
  `read_framebuffer()`.
- Python: **`slikui.ModernGLSink`** - the same sink over a
  `moderngl.Context` (~0.36 ms/frame on the gallery).
- **GLFW input without the window**: `backends/glfw/glfw_input.hpp` -
  event builders (`mouse_button_event`, `cursor_pos_event`, `key_event`,
  ...), `map_button`, `GlfwInput` (installs slikui's callbacks on YOUR
  `GLFWwindow*`, chaining to existing ones and restoring them on
  destruction, without touching the user pointer) and `attach_services`
  (clipboard + cursors). `GlfwWindow` is now window creation on top of it
  (same API, plus `input()`). Python twin: `slikui.glfw_input` /
  `slikui.GlfwInput`.
- **SDL3 backend** (`backends/sdl/`, SDL 3.2+), same layering as GLFW:
  `sdl_input.hpp` - `translate()` (SDL_Event -> slikui events), `SdlInput`
  (one window's events, text input, queue/dispatch), `attach_services`;
  `sdl_window.hpp` - `SdlWindow` (window + GL 3.3 context + input).
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

- **`shortcut_label` is plain ASCII on every platform**: "Cmd+Shift+S",
  "Ctrl+S", "Alt+Left" (Apple used the symbol form with the command,
  option and arrow glyphs). Digit keys now label as "Ctrl+1".
  `fit_shortcut_label` always spells such symbols out (the font argument is
  unused); menu hints written with them render as text.
- **The repository is ASCII-only** - sources, tests, examples, docs and
  generated files (`dist/slikui.hpp`, the type stub). Example and showcase
  labels/transcripts use "-", "->", "..." instead of typographic symbols.
  `python/tests/test_ascii.py` enforces it.
- **`OpenglSink` rewritten on `GpuBatcher`**: instanced quads (80 bytes per
  command instead of 6 x 68-byte vertices), one draw per batch, blending
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
  past its atlas cell - faint dots from the neighbouring glyph above some
  letters (e.g. G, C). Text now matches the GPU sinks to 2/255. Text pixels
  at glyph edges shift slightly in software output.
- **GLFW mouse buttons: right and middle were swapped.** GLFW numbers them
  LEFT=0, RIGHT=1, MIDDLE=2; slikui uses MouseLeft=0, MouseMiddle=1,
  MouseRight=2. `GlfwWindow` passed GLFW's number through unmapped, as did
  the Python `--window` host and `python/examples/showcase.py` - a right
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
  one), only for presses the tree received - no orphan releases for Tab,
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
  symbol the font lacks (`CtrlShiftS` -> `Ctrl+Shift+S`, `<-` -> `Left`).
- Software renderer: RGBA image sampling clamped the row against the
  texture's width instead of its height.

### New

- Event types `Click`, `DoubleClick`, `MouseEnter`, `MouseLeave`,
  `FocusGained` (appended: existing enum values are unchanged).
- `Event::clicks` - consecutive-click count on press/release/click
  (`kMultiClickTime` 0.4 s on the tick clock, `kMultiClickSlop` 4 px);
  `Event::repeat` for auto-repeat key presses.
- `Key::Num0` ... `Key::Num9` (appended).
- `Widget::paint_over` - drawn after the children, for decorations.
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
