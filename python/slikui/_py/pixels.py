"""Pixel helpers.

slikui frames are `(H, W) uint32` in ABGR packing — 0xAABBGGRR values, i.e.
little-endian byte order B, G, R, A. `to_rgba` converts to the conventional
(H, W, 4) uint8 RGBA layout (a copy; the raw view is the zero-cost path).
`save_image` writes a frame to any path Pillow can save (PNG, JPEG, ...).
"""

import numpy as np
from PIL import Image


def to_rgba(frame):
    frame = np.asarray(frame)
    r = frame & 0xFF
    g = (frame >> 8) & 0xFF
    b = (frame >> 16) & 0xFF
    a = (frame >> 24) & 0xFF
    return np.stack([r, g, b, a], axis=-1).astype(np.uint8, copy=False)


def save_image(frame, path):
    """Write a rendered frame to `path` (any format Pillow supports)."""
    Image.fromarray(to_rgba(frame), "RGBA").save(path)
    return path
