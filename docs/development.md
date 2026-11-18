# Development

Building, testing and regenerating everything in the repo.

## Build & test

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build          # golden-image pixel checks included
```

CI matrices C++ across gcc/clang/MSVC on Linux/macOS/Windows, checks that
every header compiles standalone, runs the suite under ASan + UBSan, and
compile-checks the GL/GLFW backends.
Python CI builds the sdist + wheels (cibuildwheel) and runs pytest inside
each wheel.

## Before opening a PR

Run what CI runs, plus the regeneration steps CI can't do for you. From the
repo root:

1. **C++ build and tests**:
   `cmake --build build -j && ctest --test-dir build --output-on-failure`.
   Golden-image tests fail on any pixel change, so a rendering change needs
   its goldens re-captured on purpose, never just to make the test pass.
2. **Sanitizers**: run the suite in an ASan + UBSan build. This is the build
   that catches use-after-free when widgets are removed or popups closed
   mid-event.
   ```sh
   cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DSLIKUI_BUILD_EXAMPLES=OFF \
     "-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer"
   cmake --build build-asan -j && ctest --test-dir build-asan --output-on-failure
   ```
3. **Standalone headers**: `./tools/check_headers.sh`. Each header must
   include what it uses.
4. **Python**: rebuild the extension first (`pip install -e .`) so the tests
   run against your bindings, not a stale build. Then run
   `python -m pytest python/tests`.
   - If you changed bindings, run `python tools/gen_stubs.py` and commit
     the regenerated stub. `test_stubs.py` fails if the stub drifts
     from the built extension.
   - C++ and Python showcase `--smoke` transcripts must stay byte-identical.
     Change both twins together.
5. **Amalgamation**: run `python tools/amalgamate.py` and commit
   `dist/slikui.hpp`. CI doesn't rebuild it, so a stale copy ships as-is.
6. **Docs**:
   - Run `python tools/doc_check.py`, and `mkdocs build --strict` if you
     edited `docs/`.
   - Update the reference page for any public API you added or changed.
   - If you edited example code, run `tools/extract_snippets.py`.
   - If a widget looks different, regenerate its screenshot (see
     [Regenerating the docs images](#regenerating-the-docs-images)).
7. **Changelog**: add an entry under `## Unreleased` in `CHANGELOG.md`, in
   Added / Changed / Fixed. Call out behaviour changes that existing apps
   might notice, such as callback ordering or timing.
8. **Version**: leave `include/slikui/version.hpp` alone unless the PR is a
   release. It's the single source of truth for the version, and the
   release workflow checks the tag against it.

New behaviour needs tests: C++ in `tests/` and, if the feature is exposed to
Python, in `python/tests/`. Bug fixes belong in
`python/tests/test_regressions.py` or the matching C++ test file. Each new
test should fail without the fix.

## Examples

```
./build/examples/01_hello                 # GLFW+GL hello window
./build/examples/02_raw_pixels            # headless, writes a PPM
./build/examples/03_gallery/slikui_gallery
./build/examples/04_glfw_opengl           # 3D shape viewer: orbit + zoom + accordion panel
./build/examples/04_glfw_software         # same UI, CPU-rasterized scene
./build/examples/05_showcase/slikui_showcase
./build/examples/05_showcase/slikui_showcase --smoke showcase.png   # headless scripted pass
./build/examples/07_text                  # text styles, fonts, custom widgets drawing text
./build/examples/07_text --smoke text.ppm # headless scripted pass
```

- `04_glfw_opengl` renders a lit reference shape (cube, sphere or torus) over
  a procedural sky, editing material/environment/light/camera live from an
  accordion control panel.
- `05_showcase` puts every component in one window - menu bar, toolbar,
  splitter with six tabbed pages, status bar - logging each state change
  on-screen, plus two custom widgets (`Fill`, `LevelMeter`) demonstrating the
  measure/paint/event/tick contract. Its `--smoke` transcript is
  byte-identical to the Python twin's (`python/examples/showcase.py`).
- `07_text` is the C++ twin of `python -m slikui.examples.text`: Label
  styles and font families, plus four custom widgets that draw text with
  `env.text_in` and a kept `TextBlock` (drag to select).

## Python

```sh
pip install -e .
python -m pytest python/tests
python -m slikui.examples --smoke all       # shipped suite: scripted self-checks
python -m slikui.examples.gallery           # full-surface gallery -> gallery.png
python python/examples/pyrenderer_demo.py   # C++ vs CanvasSink pixel diff (0)
python -m slikui.examples.paint --docs-image custom_widget docs/images/custom_widget.png
python tools/extract_snippets.py            # regenerate docs/includes/examples/ after editing demo code
python tools/gen_stubs.py                   # regenerate the committed type stub after binding changes
for n in hello gallery events paint controls keyboard renderer window text; do
  python -m slikui.examples.$n --smoke docs/images/examples-$n.png
done                                        # the examples page's screenshots
python tools/examples_gif.py                # examples-snake.gif, examples-clock.gif
```

`python/tests/test_stubs.py` fails when the committed stub drifts from the
built extension; `test_examples.py` runs every smoke pass in a fresh
interpreter and fails on nanobind's shutdown leak report.

## Regenerating the docs images

Every screenshot in this site is real output:
`tools/components_capture.cpp` builds each tree with the **software
backend**, simulates a second of interaction, and dumps the framebuffer -
the snippets on the widget pages are the actual capture code.

```sh
clang++ -std=c++20 -Iinclude tools/components_capture.cpp -o /tmp/ccap
/tmp/ccap docs/images            # writes PPMs
for f in docs/images/*.ppm; do python3 tools/ppm2png.py "$f" "${f%.ppm}.png"; done
```

## Amalgamation & release

- `tools/amalgamate.py` produces `dist/slikui.hpp` (bakes in the ~35 KB
  DejaVu Sans subset).
- `tools/check_headers.sh` - every public header must compile standalone.
- `tools/embed_font.py` - regenerate the embedded font subset.
- Tagging `v*` triggers the Python workflow: GitHub Release with sdist +
  wheels + `dist/slikui.hpp`, then PyPI via Trusted Publishing.

## Docs

The site is MkDocs Material; sources live in `docs/`, nav in `mkdocs.yml`.
Build locally with `mkdocs serve` or `mkdocs build --strict` (CI deploys to
GitHub Pages on push to main). `tools/doc_check.py` verifies the widget pages
stay in sync with the headers and `python/slikui/__init__.py`.
