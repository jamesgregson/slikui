# Development

Building, testing and regenerating everything in the repo.

## Build & test

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build          # golden-image pixel checks included
```

CI matrices C++ across gcc/clang/MSVC on Linux/macOS/Windows, checks that
every header compiles standalone, and compile-checks the GL/GLFW backends.
Python CI builds the sdist + wheels (cibuildwheel) and runs pytest inside
each wheel.

## Examples

```
./build/examples/01_hello                 # GLFW+GL hello window
./build/examples/02_raw_pixels            # headless, writes a PPM
./build/examples/03_gallery/slikui_gallery
./build/examples/04_glfw_opengl           # 3D shape viewer: orbit + zoom + accordion panel
./build/examples/04_glfw_software         # same UI, CPU-rasterized scene
./build/examples/05_showcase/slikui_showcase
./build/examples/05_showcase/slikui_showcase --smoke showcase.png   # headless scripted pass
```

- `04_glfw_opengl` renders a lit reference shape (cube, sphere or torus) over
  a procedural sky, editing material/environment/light/camera live from an
  accordion control panel.
- `05_showcase` puts every component in one window — menu bar, toolbar,
  splitter with six tabbed pages, status bar — logging each state change
  on-screen, plus two custom widgets (`Fill`, `LevelMeter`) demonstrating the
  measure/paint/event/tick contract. Its `--smoke` transcript is
  byte-identical to the Python twin's (`python/examples/showcase.py`).

## Python

```sh
pip install -e .
python -m pytest python/tests
python -m slikui.examples --smoke all       # shipped suite: scripted self-checks
python -m slikui.examples.gallery           # full-surface gallery -> gallery.png
python python/examples/pyrenderer_demo.py   # C++ vs CanvasSink pixel diff (0)
python -m slikui.examples.paint --docs-image custom_widget docs/images/custom_widget.png
python tools/extract_snippets.py            # regenerate docs/includes/examples/ after editing demo code
```

## Regenerating the docs images

Every screenshot in this site is real output:
`tools/components_capture.cpp` builds each tree with the **software
backend**, simulates a second of interaction, and dumps the framebuffer —
the snippets on the widget pages are the actual capture code.

```sh
clang++ -std=c++20 -Iinclude tools/components_capture.cpp -o /tmp/ccap
/tmp/ccap docs/images            # writes PPMs
for f in docs/images/*.ppm; do python3 tools/ppm2png.py "$f" "${f%.ppm}.png"; done
```

## Amalgamation & release

- `tools/amalgamate.py` produces `dist/slikui.hpp` (bakes in the ~35 KB
  DejaVu Sans subset).
- `tools/check_headers.sh` — every public header must compile standalone.
- `tools/embed_font.py` — regenerate the embedded font subset.
- Tagging `v*` triggers the Python workflow: GitHub Release with sdist +
  wheels + `dist/slikui.hpp`, then PyPI via Trusted Publishing.

## Docs

The site is MkDocs Material; sources live in `docs/`, nav in `mkdocs.yml`.
Build locally with `mkdocs serve` or `mkdocs build --strict` (CI deploys to
GitHub Pages on push to main). `tools/doc_check.py` verifies the widget pages
stay in sync with the headers and `python/slikui/__init__.py`.
