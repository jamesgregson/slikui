"""DRAWCMD_DTYPE — numpy structured dtype matching C++ `slikui::DrawCmd`.

`DrawList.as_array()` returns a zero-copy, read-only `(N,)` array of this
dtype over the list's command storage. Field offsets come from the compiled
extension (`_drawcmd_layout()`), so they always match the C++ struct; the
binding static_asserts the field types and sizes.

Fields:
    type       uint8          DrawCmdType value (compare with DrawCmdType.X.value)
    rect       float32[4]     x, y, w, h (Line: x, y, dx, dy)
    radius     float32
    thickness  float32
    hollow     bool
    color      uint8[4]       r, g, b, a (straight alpha)
    tex_id     uint32
    uv         float32[4]     x, y, w, h in texture pixels
"""

import numpy as np

from slikui._slikui import _drawcmd_layout

_FORMATS = {
    "type": np.uint8,
    "rect": (np.float32, 4),
    "radius": np.float32,
    "thickness": np.float32,
    "hollow": np.bool_,
    "color": (np.uint8, 4),
    "tex_id": np.uint32,
    "uv": (np.float32, 4),
}


def _make_dtype():
    layout = dict(_drawcmd_layout())
    itemsize = layout.pop("itemsize")
    names = list(_FORMATS)
    if set(names) != set(layout):
        raise RuntimeError(f"DrawCmd layout mismatch: {sorted(layout)}")
    return np.dtype({
        "names": names,
        "formats": [_FORMATS[n] for n in names],
        "offsets": [layout[n] for n in names],
        "itemsize": itemsize,
    })


DRAWCMD_DTYPE = _make_dtype()
