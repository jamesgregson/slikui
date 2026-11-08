# Text & fonts

slikui rasterizes TrueType text itself — vendored public-domain
stb_truetype/stb_rect_pack compiled with internal linkage, no system
dependencies, no icon fonts.

## Loading a font

A `Context` has exactly one active font. Load it from a file, or from memory:

=== "C++"

    ```cpp
    #include <slikui/text/system_fonts.hpp>   // find_system_font()
    #include <slikui/text/default_font.hpp>   // optional bundled subset

    ctx.load_font_file(slikui::find_system_font());          // system TTF
    // or, for identical text everywhere — even on machines with no fonts:
    ctx.load_font_memory(slikui::text::default_font_data(),
                         slikui::text::default_font_size());
    ```

=== "Python"

    ```python
    import slikui

    ctx.load_font_file(slikui.find_system_font())
    ctx.load_default_font()                   # bundled DejaVu Sans subset
    ```

`slikui::find_system_font()` searches the usual per-platform paths and returns
an empty string when nothing is found — check the return of `load_font_file`
and fall back to the bundled font.

![Text rendered by this pipeline: the Label capture loads a system font and
rasterizes glyphs on demand into the atlas](../images/labels.png)

## The bundled font

The single-header `dist/slikui.hpp` bakes in a ~35 KB DejaVu Sans subset
(ASCII, Latin-1 + punctuation) so text renders identically everywhere, even
on headless containers. Building from the include tree the same font stays
**opt-in**: `#include <slikui/text/default_font.hpp>`. The Python wheels use
it as the fallback whenever no system font exists.

Licence: `third_party/fonts/LICENSE-DejaVu.txt` — ship it with your app.

## How glyphs reach the screen

- The font owns the TTF bytes, an stb_truetype fontinfo, a 512×512 alpha8
  atlas and a glyph cache keyed by `(codepoint, size)`. Glyphs rasterize on
  demand; the atlas is pushed to the sink when dirty.
- `layout(text, px, max_width)` produces positioned glyphs with word
  wrapping; `advance_upto` powers caret hit-testing in text fields.
- Layouts are cached per widget (`Label`, `ListView`, `Tabs`) and invalidated
  on text or size changes.
- Whitespace glyphs are never emitted — widgets skip them.
- Glyphs re-rasterize automatically when `theme().scale` changes (window
  dragged between displays).

## Sizing and wrapping

Theme metrics set the base size (`theme().base_font_px`); individual widgets
override with `set_font_size`. `Label` word-wraps with `set_wrap_width`; with
no explicit width it wraps to the constraint width, so narrower windows
produce taller text blocks automatically.

API details: [Label](../reference/widgets-controls.md#label),
[TextField](../reference/widgets-inputs.md).
