# Bundled default font

`DejaVuSans-subset.ttf` is a subset of **DejaVu Sans 2.37**, embedded by
`include/slikui/text/default_font.hpp` so text renders identically on every
platform, including minimal containers with no system fonts (e.g. manylinux).

Licence: Bitstream Vera / Arev licence; DejaVu changes are public domain. See
`LICENSE-DejaVu.txt`, which must ship with every copy. Modified copies (this
subset counts) must not be named with "Bitstream", "Vera" or "Arev"; the
font keeps its "DejaVu Sans" name and its copyright/licence name records.

## Provenance

- Source: https://github.com/dejavu-fonts/dejavu-fonts/releases/download/version_2_37/dejavu-fonts-ttf-2.37.tar.bz2
  - archive sha256 `fa9ca4d13871dd122f61258a80d01751d603b4d3ee14095d65453b4e846e17d7`
  - `ttf/DejaVuSans.ttf` sha256 `7da195a74c55bef988d0d48f9508bd5d849425c1770dba5d7bfc6ce9ed848954`
- Subset sha256 `e6135d7f694c1c781631d4ced3d297d8ed2e15c27544f1e9ce8c2c9cc54ca063`
  (fonttools 4.68.0, 316 glyphs, ~35 KB)

## Regenerating

Coverage: printable ASCII, Latin-1, General Punctuation, €, ™, arrows ←↑→↓, ✓.
Hinting is dropped (stb_truetype ignores it); GPOS `kern` is kept
(stb_truetype ≥ 1.22 reads GPOS pair kerning).

```sh
pip install fonttools
pyftsubset DejaVuSans.ttf --output-file=third_party/fonts/DejaVuSans-subset.ttf \
  --unicodes="U+0020-007E,U+00A0-00FF,U+2000-206F,U+20AC,U+2122,U+2190-2193,U+2713" \
  --layout-features=kern --no-hinting --name-IDs='*' --name-languages='*' \
  --drop-tables+=MATH,FFTM
python3 tools/embed_font.py   # rewrites include/slikui/text/default_font.hpp
python3 tools/amalgamate.py   # refreshes dist/
```
