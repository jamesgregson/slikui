# Text & fonts

slikui rasterizes TrueType text itself - vendored public-domain
stb_truetype/stb_rect_pack compiled with internal linkage, no system
dependencies, no icon fonts.

## Loading a font

A `Context` has exactly one active font. Load it from a file, or from memory:

=== "C++"

    ```cpp
    #include <slikui/text/system_fonts.hpp>   // find_system_font()
    #include <slikui/text/default_font.hpp>   // optional bundled subset

    ctx.load_font_file(slikui::find_system_font());          // system TTF
    // or, for identical text everywhere - even on machines with no fonts:
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
an empty string when nothing is found - check the return of `load_font_file`
and fall back to the bundled font.

## Font families and styles

Beyond the body font, register more faces by family name. Three names are
roles every text call understands - `"body"` (what `load_font_file` loads),
`"heading"` and `"mono"` - and any other name is your own. An unregistered
family falls back to body.

=== "C++"

    ```cpp
    ctx.add_font("mono", "/path/JetBrainsMono-Regular.ttf");
    ctx.add_font("body", "/path/Inter-Bold.ttf", slikui::text::FontStyle::Bold);

    // Installed fonts by role and style (.ttc faces resolved for you):
    if (auto f = slikui::find_system_font_face("mono"))
        ctx.add_font("mono", f.path, slikui::text::FontStyle::Regular, f.face);
    ```

=== "Python"

    ```python
    ctx.add_font("mono", "/path/JetBrainsMono-Regular.ttf")
    ctx.add_font("body", "/path/Inter-Bold.ttf", slikui.FontStyle.Bold)

    f = slikui.find_system_font_face("mono", slikui.FontStyle.Bold)
    if f:
        ctx.add_font("mono", f.path, slikui.FontStyle.Bold, f.face)
    ```

Bold and italic never need a font file: when a family has no face for the
requested style, slikui synthesizes it from the closest face it has (italic
slants the outline, bold thickens the glyph). Real faces look better, so
register them when you ship them.

Underline and strikethrough use the font's own placement metrics and draw in
the text color. `Label` takes all of these as setters:

=== "C++"

    ```cpp
    root.add<slikui::Label>("Warning").set_bold().set_underline();
    root.add<slikui::Label>("x = 3.14").set_font("mono");
    ```

=== "Python"

    ```python
    root.add(slikui.Label, "Warning", bold=True, underline=True)
    root.add(slikui.Label, "x = 3.14", font="mono")
    ```

Custom widgets draw text with the same options through `env.text` - see
[Custom widgets](custom-widgets.md#drawing-text).

![Text rendered by this pipeline: the Label capture loads a system font and
rasterizes glyphs on demand into the atlas](../images/labels.png)

## The bundled font

The single-header `dist/slikui.hpp` bakes in a ~35 KB DejaVu Sans subset
(ASCII, Latin-1 + punctuation) so text renders identically everywhere, even
on headless containers. Building from the include tree the same font stays
**opt-in**: `#include <slikui/text/default_font.hpp>`. The Python wheels use
it as the fallback whenever no system font exists.

Licence: `third_party/fonts/LICENSE-DejaVu.txt` - ship it with your app.

## How glyphs reach the screen

- Each font face owns its TTF bytes, an stb_truetype fontinfo, a glyph
  cache and an alpha8 atlas of 512x512 pages. Glyphs rasterize on demand;
  when a page fills, a new page (with its own texture id) is added, and
  dirty pages are pushed to the sink.
- The cache is keyed by glyph, size (rounded to 1/4 px) and synthesized
  style, so bold and regular text at the same size coexist.
- `layout(text, px, max_width, flags)` produces positioned glyphs with word
  wrapping, per-line byte ranges, and underline/strikethrough bars;
  `advance_upto` powers caret hit-testing in text fields.
- Layouts are cached per widget (`Label`, `ListView`, `Tabs`) and invalidated
  on text or size changes; `env.text` layouts are cached by the context and
  dropped after about two seconds unused.
- Whitespace glyphs are never drawn.
- Glyphs re-rasterize automatically when `theme().scale` changes (window
  dragged between displays).

## Sizing and wrapping

Theme metrics set the base size (`theme().base_font_px`); individual widgets
override with `set_font_size`. `Label` word-wraps with `set_wrap_width`; with
no explicit width it wraps to the constraint width, so narrower windows
produce taller text blocks automatically.

API details: [Label](../reference/widgets-controls.md#label),
[TextField](../reference/widgets-inputs.md).
