# Python bindings for slikui — plan

> **Status: M0 + M1 + M2 complete; M3 in progress (sdist + version
> single-sourcing + C++ install/export done; cibuildwheel/GH Actions tabled);
> M4 tabled.** M0/M1: scaffold, values,
> software backend, `Context`, **every widget class**, Theme + 8 presets,
> HiDPI scale; 17 tests. M2: full event vocabulary bound (`Event`,
> `EventType`, `Key`, `Mod`, `MouseButton`, `Rect`, `Widget.bounds`/`children`
> views, `Context.handle`/`popup*`), Python event factories in
> `slikui/_py/events.py` (`mouse_press`, `mouse_click`, `scroll`, `key_press`,
> `text_input`, `mods("ctrl+shift")`, ...), and 8 simulated-input tests:
> dropdown popup open→select→dismiss (outside click + Escape), accordion> header clicks (exclusive toggle), tooltip after the 0.6 s hover, TextField
> focus + typing + backspace + Enter submit, ScrollArea wheel, FrameResize.
> Plus a bonus before M3: **`python/examples/glfw_demo.py`** — the cube viewer
> hosted by Python-native GLFW + OpenGL (`glfw` + `PyOpenGL`), slikui software
> UI blitted as one texture with `render(clear=False)` straight-alpha
> blending. New bindings for it: `Target.resize` (window resizes), a C++-side
> `Swatch` color-chip widget (Python can't subclass C++ widgets without a
> trampoline), `Panel.draw_border`, `ScrollArea.hint_w/hint_h`, `App.resize`.
> **29/29 tests pass** (17 M0/M1, 8 M2 events, 4 demo headless); panel layout
> verified in `shots/py_demo_panel.png`, popup/tooltip in
> `shots/py_dropdown_open.png`, `shots/py_tooltip.png`.
> **Update: 34/34.** The "Python-implemented sinks" item is done early:
> `slikui.RenderSink` trampoline + `DrawList`/`DrawCmd`/`TexUpload` bindings, and
> `slikui.CanvasSink` — a pure-numpy renderer producing pixel-identical frames
> to the C++ software backend (5 new parity tests; `shots/pyrenderer_demo.png`).
> Builder contract note: `add(cls, *args, **props)` sends positionals to the
> bound `add_<name>` factory and kwargs to setters — ctor-only callables
> (like Swatch's callbacks) go in positionally.
> Known behavior (locked by test): glyph atlas uploads at frame start —
> render a couple of frames before capturing.
> Gotcha found: nanobind's `Python_INCLUDE_DIRS` is directory-scoped — the
> consumer must `find_package(Python ... Development.Module)` before
> `nanobind_add_module` (done in `python/CMakeLists.txt`).

Decisions made: **nanobind** (vendored), **software backend first** (GLFW+GL
later), **scikit-build-core + cibuildwheel** packaging, **Pythonic layer on top
of a thin binding**.

Deliverable: distribution **`slikui`** on PyPI, import package **`slikui`**,
compiled module **`slikui._slikui`**. The C++ library stays header-only and
dependency-free — all binding machinery lives under `python/`.

---

## 1. Architecture

```
python/
├── pyproject.toml          # scikit-build-core, cmake>=3.20, nanobind
├── CMakeLists.txt          # one target: slikui._slikui (links nothing)
├── src/
│   ├── slikui_py.cpp         # module init + Context/backends/values
│   ├── widgets_py.cpp      # all widget classes (split to keep TUs fast)
│   └── helpers_py.hpp      # setter-chaining / add<>() macros
├── slikui/
│   ├── __init__.py         # re-exports _slikui + pythonic layer
│   ├── _py/                # pure-Python layer (see §3)
│   └── py.typed
├── examples/               # gallery.py, hello.py, snapshot tests input
└── tests/                  # pytest
third_party/nanobind/       # vendored copy (same policy as stb)
```

`src/*.cpp` includes the multi-header `slikui.hpp` directly (not
`dist/slikui.hpp`) — no amalgamation step in the build path. Since the extension
needs `IRenderSink`, `Context`, and every widget, and the library is
header-only, the whole binding is a handful of translation units with **no
libraries to link**.

## 2. Thin binding (`_slikui`, nanobind)

What gets bound, 1:1 with the C++ names:

| Group | Items |
|-------|-------|
| Values | `Color` (+`from_hex`), `Vec2`, `Padding`, `Constraints`, `Time` |
| Core | `Context` (`set_root`, `handle`, `tick`, `render`, `find`, `theme`, `load_font_file`, `next_texture_id`, `push_popup`), `Theme` (+ all preset statics, `apply_preset`, `use_theme`, `theme_presets`), `Anim` |
| Input | `Event` (struct: `type`, `pos`, `delta`, `scroll`, `button`, `key`, `codepoint`, `mods`), `Key`/`Mod`/`MouseButton` enums, `map_key` |
| Backends | `software::Target` (owning **and** borrowed-from-numpy ctor), `software::Sink`; `DrawList`/`DrawCmd` read-only (for tests + future Python sinks) |
| Widgets | every class in `widgets/*.hpp` |

Binding rules:

- **Setters return the same Python object.** C++ fluent setters return
  `Container&`/`Widget&` (no deducing-this), so each setter is bound to return
  `this` with `nb::rv_policy::reference`. nanobind re-attaches the existing
  Python wrapper for an object it already wrapped, so
  `col.set_gap(8).set_padding(16)` chains and identity is stable. A small
  macro (`.def("set_gap", [](Container& s, float v) { s.set_gap(v); return &s; },
  nb::rv_policy::reference)`) keeps this terse across ~200 setters.
- **Tree owns children; Python holds views.** `add<T>`-equivalents return
  `nb::rv_policy::reference_internal` (keeps the parent alive). Wrappers are
  non-owning; no `__del__`, no delete. `Context.set_root` keeps the root in a
  `unique_ptr` on the C++ side.
- **`Target` from numpy:** bind a borrowed-buffer constructor
  (`Target(uint32_t*, w, h)`) taking an `nb::ndarray<uint32_t, nb::ndim<2>>`
  with `nb::rv_policy::reference_internal`-style keep-alive, so a Python array
  can *be* the framebuffer: render, then read pixels with zero copies. Also
  expose `Target.pixels` as a zero-copy `np.ndarray((h, w), uint32)` view.
- **No GIL games:** every call holds the GIL. The software rasterizer renders
  a gallery frame in milliseconds; releasing the GIL only matters for huge
  frames — revisit if it ever shows up in profiles.
- Widgets that need host-side glue (`Image::set_pixels`, font memory loading)
  take `nb::ndarray<uint8_t>` / `bytes` directly.

## 3. Pythonic layer (`slikui/_py/`, pure Python)

Kept deliberately thin — it must never re-implement widget logic, only
reshape the surface:

- **kwargs → setters.** A single `add()` helper on containers:
  `col.add(slikui.Button, "Save", variant="primary", tooltip="…")` constructs
  the widget and calls `set_variant(...)`, `set_tooltip(...)`. Names map 1:1 to
  `set_*` methods, so there is **no registry to keep in sync** — a missing
  setter is a plain `AttributeError`, not a silent no-op.
- **`Event` factories** (module-level functions): `mouse_move(pos, delta)`,
  `mouse_press(button, pos)`, `key_press(key, mods=0)`, `text_input(ch)`, …
  building the bound struct. Mods accept `"ctrl|shift"` strings.
- **`Context` wrapper as context manager** with conveniences:
  `ui = slikui.App(480, 320)` builds Target+Sink+Context, loads the system font
  (`slikui.find_system_font`), and offers `ui.tick(t)`, `ui.render() -> np.ndarray`
  (zero-copy view), `ui.frame_size = (w, h)`.
- **Theme ergonomics:** `slikui.Theme.nord()` etc. come from the binding;
  the Python layer adds `theme.scale = 2.0` passthrough and hex-string
  coercion for `Color` fields (`theme.accent = "#E060B0"`).
- **py.typed** — the bound classes get annotations from nanobind
  (`nb::sig` signatures), the Python layer is fully typed.

Anything richer (properties with getters, dataclass Theme mirrors) is added
only where the C++ side already exposes a getter — no invented state.

## 4. Backends

- **v1: software only.** Everything render-to-numpy, fully testable headless.
  This covers "generate UI screenshots", "drive a UI from pytest", embedding
  into pygame (blit the numpy frame) — the primary Python use cases.
- **Later: GLFW + OpenGL.** Bind `slikui::glfw::GlfwWindow` + `opengl::OpenglSink`
  (link system glfw, ship `map_key`). Consider the `glfw` PyPI package
  (ctypes) for windowing and only bind the sink — decide at that milestone.
- **Later: Python-implemented sinks.** ~~nanobind trampoline for `IRenderSink`
  lets Python back the two-method contract~~ **DONE (ahead of M4):**
  `slikui.RenderSink` trampoline + read-only `DrawList`/`DrawCmd`/`TexUpload`
  bindings are in, and `slikui.CanvasSink` (`slikui/_py/renderer.py`) is a
  pure-numpy rasterizer over the contract, pixel-identical to the C++
  software backend (parity tests in `python/tests/test_pyrenderer.py`, demo in
  `python/examples/pyrenderer_demo.py`). Remaining in this bucket: making
  `TexUpload` constructible from Python (to push textures *into* C++ sinks).

## 5. Testing

- **pytest, headless, numpy asserts.** Port the ideas of
  `tools/components_capture.cpp`: build a tree, feed synthetic events,
  `tick` for a second of animation, render, compare against golden PNG/PPMs
  (reuse `tests/` golden-image conventions and `tools/ppm2png.py`).
- Cases: hello render, all-widget gallery, accordion open/close via simulated
  `Event`, dropdown popup open (needs `tick` after the click — same as C++),
  scroll clipping, theme presets pixel-diff, numpy-buffer borrow (render into
  a user-owned array), label wrap regression (the `y`-shift bug).
- CI: GitHub Actions — `cmake` ctest suite (unchanged) + pytest matrix
  (CPython 3.10–3.13 × macOS arm64/x86_64, Linux x86_64/aarch64, Windows
  amd64) via cibuildwheel.

## 6. Packaging

- `pyproject.toml` with `scikit-build-core`; `nanobind` vendored under
  `third_party/nanobind` (pip never fetches it; build deps: cmake, ninja via
  scikit-build).
- Wheel = compiled `_slikui.<abi>.so` + the `slikui/` package; headers don't ship.
- sdist includes `include/`, `python/src`, vendored nanobind so
  `pip install slikui` builds from source on unusual platforms.
- Name check before publishing: `slikui` on PyPI is likely taken; `slikui`
  (import `slikui`) is the proposal.

## 7. Milestones

| M | Content | Done when |
|---|---------|-----------|
| **M0 — scaffold** | `python/` tree, vendored nanobind, CMake, module `slikui._slikui` imports; `Context`, `Target`, `Sink`, `Color`, `Vec2`, `Column`, `Row`, `Label`, `Button`; numpy framebuffer; App wrapper; 1 pytest | `pip install -e python/` works; smoke test renders "Hello, slikui!" to numpy |
| **M1 — full widget surface** | every class in `widgets/*.hpp` + theme/presets + Image-from-numpy; kwargs `add()` layer; `py.typed` | gallery.py renders the whole widget set from Python |
| **M2 — interaction** | Event binding + factories, popups, tooltips, simulated-input tests, golden images | accordion/dropdown/scroll scenarios pass pixel checks |
| **M3 — packaging & CI** | cibuildwheel matrix, GH Actions, `docs/python-bindings.md`, sdist build | wheels for the matrix build green; docs merged |
| **M4 — later** | GLFW+GL sink/window ~~(superseded: the demo uses a Python-native glfw/PyOpenGL host)~~, ~~Python `IRenderSink` trampoline~~ **(done: `slikui.RenderSink` + `CanvasSink`, see §4)**, free-threaded wheel (opt-in) | ~~demo rendering slikui UI into a live GL window from Python~~ (achieved by `python/examples/glfw_demo.py`) |

## 8. Known pitfalls (from the C++ work)

- **Setter chaining / base-class collisions** — the demo hit
  `no member named 'add_section' in 'slikui::Container'` because fluent setters
  return the base. In bindings, every setter is rebound per most-derived class
  returning `self`, so Python never sees the C++ base-type problem.
- **Dangling views:** wrappers are non-owning; docs must say "widgets are
  owned by their parent; don't hold references across `set_root`".
- **Popups need a tick:** a simulated dropdown click only lays out the popup
  after the next `tick` — mirrors C++; tests must do click → tick → render.
- **Ink vs advance / wrap fixes** are already in the C++; the Python golden
  tests lock the label-wrap behavior in.
- **ABGR packing:** `Target` pixels are `uint32` ABGR (little-endian → BGRA
  bytes). Provide `slikui.to_rgba(frame)` helper; keep the raw view the primary
  API (zero-copy).
- **stb internal linkage** means the font atlas compiles into the extension —
  no symbol clashes with anything else, nothing to export.
