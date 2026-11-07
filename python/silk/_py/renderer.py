"""CanvasSink — a pure-Python (numpy) renderer implementing the silk contract.

This is the "your renderer" seam: subclass `silk.RenderSink`, and silk hands
you every frame as a flat command list (Rect/Line/Glyph/Image/ClipPush/ClipPop)
plus texture uploads. CanvasSink rasterizes that list into a numpy uint32
buffer with the exact same math as the C++ software backend in
`include/silk/backends/software/software.hpp` — same blend formula, same SDF
coverage, same sampling — so frames come out pixel-identical.

Pixel format: 0xAABBGGRR uint32, straight alpha, row-major (H, W).

Usage:
    sink = CanvasSink(320, 240)
    ctx = silk.Context(sink)
    ctx.set_frame_size(320, 240)
    ctx.load_font_file(silk.find_system_font())
    ...
    ctx.tick(0.0, 1 / 60)
    ctx.render()          # silk calls sink.upload/render with the draw list
    frame = sink.buffer   # (240, 320) uint32 view

Coordinates: DrawList commands are logical points; `set_scale(s)` maps them
to device pixels exactly like software.Sink (HiDPI).
"""

from __future__ import annotations

import numpy as np

from silk._silk import DrawCmdType, RenderSink, TexFormat

_F32 = np.float32
_U32 = np.uint32


def _clamp01(v):
    # C++ clamp01: v < 0 ? 0 : (v > 1 ? 1 : v)
    return np.where(v < _F32(0), _F32(0), np.where(v > _F32(1), _F32(1), v))


def _sd_rounded_box(px, py, bx, by, r):
    """Approximate SDF of a rounded box, half-extents (bx, by), radius r.

    Mirrors detail::sd_rounded_box operation-for-operation (float32) so edge
    coverage matches the C++ rasterizer bit for bit.
    """
    qx = np.abs(px) - (bx - r)
    qy = np.abs(py) - (by - r)
    ax = np.maximum(qx, _F32(0))
    ay = np.maximum(qy, _F32(0))
    outside = np.sqrt(ax * ax + ay * ay)
    return (outside + np.minimum(np.maximum(qx, qy), _F32(0))) - r


def _intersect(ax, ay, aw, ah, bx, by, bw, bh):
    # Rect::intersect in float32
    nx = np.maximum(ax, bx)
    ny = np.maximum(ay, by)
    nr = np.minimum(ax + aw, bx + bw)
    nb = np.minimum(ay + ah, by + bh)
    return nx, ny, np.maximum(_F32(0), nr - nx), np.maximum(_F32(0), nb - ny)


class CanvasSink(RenderSink):
    """Software renderer in numpy. Subclass of silk.RenderSink."""

    def __init__(self, width, height, *, scale=1.0):
        super().__init__()
        self._scale = _F32(scale)
        self._buf = np.zeros((int(height), int(width)), np.uint32)
        self._textures = {}  # id -> (h, w) or (h, w, 4) uint8
        self._clips = []     # list of (x, y, w, h) float32

    # -- surface ------------------------------------------------------------

    @property
    def buffer(self):
        """The (H, W) uint32 framebuffer, 0xAABBGGRR. Zero-copy view."""
        return self._buf

    @property
    def width(self):
        return self._buf.shape[1]

    @property
    def height(self):
        return self._buf.shape[0]

    def resize(self, width, height):
        self._buf = np.zeros((int(height), int(width)), np.uint32)

    def set_scale(self, scale):
        self._scale = _F32(scale)

    @property
    def scale(self):
        return float(self._scale)

    # -- IRenderSink ---------------------------------------------------------

    def upload(self, up):
        n = up.width * up.height * (4 if up.format == TexFormat.Rgba8 else 1)
        data = np.frombuffer(bytes(up.pixels), np.uint8, n).copy()
        if up.format == TexFormat.Rgba8:
            data = data.reshape(up.height, up.width, 4)
        else:
            data = data.reshape(up.height, up.width)
        self._textures[up.id] = data

    def render(self, cmds, framebuffer_size):
        self._clips = [(_F32(0), _F32(0), _F32(self.width), _F32(self.height))]
        for c in cmds:
            self._execute(c)

    # -- command dispatch -----------------------------------------------------

    def _execute(self, c):
        s = self._scale
        x = _F32(c.rect.x) * s
        y = _F32(c.rect.y) * s
        w = _F32(c.rect.w) * s
        h = _F32(c.rect.h) * s
        t = c.type
        if t == DrawCmdType.ClipPush:
            self._clips.append(_intersect(x, y, w, h, *self._clips[-1]))
        elif t == DrawCmdType.ClipPop:
            if len(self._clips) > 1:
                self._clips.pop()
        elif t == DrawCmdType.Rect:
            if c.hollow:
                self._stroke_rect(x, y, w, h, c.color,
                                  _F32(c.thickness) * s, _F32(c.radius) * s)
            else:
                self._fill_rect(x, y, w, h, c.color, _F32(c.radius) * s)
        elif t == DrawCmdType.Line:
            self._stroke_line(x, y, w, h, c.color, _F32(c.thickness) * s)
        elif t == DrawCmdType.Glyph:
            self._draw_glyph(c, x, y, w, h)
        elif t == DrawCmdType.Image:
            self._draw_image(c, x, y, w, h)

    # -- blending --------------------------------------------------------------

    def _blend(self, X, Y, mask, sr, sg, sb, sa):
        """C++ blend() over selected pixels.

        sr..sa: scalar _U32 or full-grid arrays (per-pixel src color/alpha);
        anything array-valued is masked down to the selected pixels here.
        """
        mask = mask & (X >= 0) & (Y >= 0) & (X < self.width) & (Y < self.height)
        if not mask.any():
            return
        ys = np.broadcast_to(Y, mask.shape)[mask]
        xs = np.broadcast_to(X, mask.shape)[mask]
        dst = self._buf[ys, xs]
        dr = dst & _U32(0xFF)
        dg = (dst >> _U32(8)) & _U32(0xFF)
        db = (dst >> _U32(16)) & _U32(0xFF)
        da = dst >> _U32(24)
        sr, sg, sb, sa = (_v[mask] if np.ndim(_v) else _v
                          for _v in (sr, sg, sb, sa))
        ia = _U32(255) - sa
        # (s*a + d*ia + 127) / 255 with uint32 floor division == C++ integer math
        out_r = (sr * sa + dr * ia + _U32(127)) // _U32(255)
        out_g = (sg * sa + dg * ia + _U32(127)) // _U32(255)
        out_b = (sb * sa + db * ia + _U32(127)) // _U32(255)
        out_a = sa + da * ia // _U32(255)
        self._buf[ys, xs] = (
            (out_a << _U32(24)) | (out_b << _U32(16)) |
            (out_g << _U32(8)) | out_r)

    def _faded_alpha(self, color_a, f):
        """Color::faded: with_alpha(clamp01(a/255 * f)) -> uint8 (truncating)."""
        a = (_F32(color_a) / _F32(255)) * f
        a = np.minimum(np.maximum(a, _F32(0)), _F32(1))
        return (_F32(255) * a).astype(np.uint8)

    # -- primitives --------------------------------------------------------------

    def _grid(self, y0, y1, x0, x1):
        """Inclusive integer pixel grid as broadcastable (Y, X) int64 arrays."""
        Y = np.arange(int(y0), int(y1) + 1, dtype=np.int64)[:, None]
        X = np.arange(int(x0), int(x1) + 1, dtype=np.int64)[None, :]
        return Y, X

    def _clip_mask(self, X, Y):
        cx, cy, cw, ch = self._clips[-1]
        return (X >= cx) & (X < cx + cw) & (Y >= cy) & (Y < cy + ch)

    def _full_mask(self, X, Y):
        return np.ones(np.broadcast_shapes(Y.shape, X.shape), dtype=bool)

    def _fill_rect(self, x, y, w, h, color, radius):
        clip = self._clips[-1]
        x, y, w, h = _intersect(x, y, w, h, *clip)
        if w <= 0 or h <= 0:
            return
        if radius <= 0:
            # NOTE: matches C++ exactly — the fast path loops from int(r.x)
            # with no per-pixel clip check, so a fractional origin paints the
            # pixel below clip.x too. Quirk kept on purpose.
            Y, X = self._grid(y, np.ceil(y + h) - 1, x, np.ceil(x + w) - 1)
            self._blend(X, Y, self._full_mask(X, Y),
                        _U32(color.r), _U32(color.g), _U32(color.b),
                        _U32(color.a))
            return
        # Per-pixel SDF coverage (this path does check the clip per pixel).
        Y, X = self._grid(y - 1, np.ceil(y + h), x - 1, np.ceil(x + w))
        px = (X.astype(_F32) + _F32(0.5) - x) - w * _F32(0.5)
        py = (Y.astype(_F32) + _F32(0.5) - y) - h * _F32(0.5)
        d = _sd_rounded_box(px, py, w * _F32(0.5), h * _F32(0.5), radius)
        cov = _clamp01(_F32(0.5) - d)
        mask = (cov > _F32(0)) & self._clip_mask(X, Y)
        sa = self._faded_alpha(color.a, cov).astype(_U32)
        self._blend(X, Y, mask, _U32(color.r), _U32(color.g), _U32(color.b), sa)

    def _stroke_rect(self, x, y, w, h, color, thickness, radius):
        inner_r = np.maximum(radius - thickness, _F32(0))
        Y, X = self._grid(y - 1, np.ceil(y + h), x - 1, np.ceil(x + w))
        px = (X.astype(_F32) + _F32(0.5) - x) - w * _F32(0.5)
        py = (Y.astype(_F32) + _F32(0.5) - y) - h * _F32(0.5)
        d_out = _sd_rounded_box(px, py, w * _F32(0.5), h * _F32(0.5), radius)
        bi_x = np.maximum(w * _F32(0.5) - thickness, _F32(0))
        bi_y = np.maximum(h * _F32(0.5) - thickness, _F32(0))
        d_in = _sd_rounded_box(px, py, bi_x, bi_y, inner_r)
        cov = _clamp01(_F32(0.5) - d_out) * _clamp01(_F32(0.5) + d_in)
        mask = (cov > _F32(0)) & self._clip_mask(X, Y)
        sa = self._faded_alpha(color.a, cov).astype(_U32)
        self._blend(X, Y, mask, _U32(color.r), _U32(color.g), _U32(color.b), sa)

    def _stroke_line(self, x, y, dx, dy, color, thickness):
        ax, ay = x, y
        bx = x + dx
        by = y + dy
        half = thickness * _F32(0.5)
        bx0 = np.minimum(ax, bx) - half - _F32(1)
        by0 = np.minimum(ay, by) - half - _F32(1)
        bw = np.abs(bx - ax) + _F32(2) * half + _F32(2)
        bh = np.abs(by - ay) + _F32(2) * half + _F32(2)
        cx, cy, cw, ch = self._clips[-1]
        bx0, by0, bw, bh = _intersect(bx0, by0, bw, bh, cx, cy, cw, ch)
        Y, X = self._grid(by0, np.ceil(by0 + bh), bx0, np.ceil(bx0 + bw))
        px = X.astype(_F32) + _F32(0.5)
        py = Y.astype(_F32) + _F32(0.5)
        ddx = bx - ax
        ddy = by - ay
        len2 = ddx * ddx + ddy * ddy
        with np.errstate(divide="ignore", invalid="ignore"):
            t = ((px - ax) * ddx + (py - ay) * ddy) / len2
        t = np.where(len2 > _F32(0), t, _F32(0))
        t = _clamp01(t)
        proj_x = ax + ddx * t
        proj_y = ay + ddy * t
        dist = np.sqrt((px - proj_x) * (px - proj_x) +
                       (py - proj_y) * (py - proj_y))
        cov = _clamp01((half + _F32(0.5)) - dist)
        mask = cov > _F32(0)
        sa = self._faded_alpha(color.a, cov).astype(_U32)
        self._blend(X, Y, mask, _U32(color.r), _U32(color.g), _U32(color.b), sa)

    def _uv_maps(self, c, x, y, w, h, X, Y):
        # shared uv math for glyph/image: sample point from pixel center
        fx = X.astype(_F32) + _F32(0.5)
        fy = Y.astype(_F32) + _F32(0.5)
        u = _F32(c.uv.x) + ((fx - x) / w) * _F32(c.uv.w)
        v = _F32(c.uv.y) + ((fy - y) / h) * _F32(c.uv.h)
        return u, v

    def _draw_glyph(self, c, x, y, w, h):
        tex = self._textures.get(c.tex_id)
        if tex is None or c.uv.w <= 0 or c.uv.h <= 0:
            return
        clip = self._clips[-1]
        cx, cy, cw, ch = _intersect(x, y, w, h, *clip)
        if cw <= 0 or ch <= 0:
            return
        Y, X = self._grid(cy, np.ceil(cy + ch) - 1, cx, np.ceil(cx + cw) - 1)
        u, v = self._uv_maps(c, x, y, w, h, X, Y)
        alpha = self._sample_alpha(tex, u, v)
        # Color c = cmd.color.faded(a.a / 255); blend only where a > 0.
        f = alpha.astype(_F32) / _F32(255)
        sa = self._faded_alpha(c.color.a, f).astype(_U32)
        mask = sa > _U32(0)
        self._blend(X, Y, mask, _U32(c.color.r), _U32(c.color.g),
                    _U32(c.color.b), sa)

    def _sample_alpha(self, tex, u, v):
        """detail::sample_alpha — bilinear alpha, clamped, rounded (+0.5)."""
        th, tw = tex.shape
        fx = np.maximum(np.minimum(u, _F32(tw - 1)), _F32(0))
        fy = np.maximum(np.minimum(v, _F32(th - 1)), _F32(0))
        x0 = fx.astype(np.int64)
        y0 = fy.astype(np.int64)
        x1 = np.minimum(x0 + 1, tw - 1)
        y1 = np.minimum(y0 + 1, th - 1)
        tx = fx - x0.astype(_F32)
        ty = fy - y0.astype(_F32)
        a00 = tex[y0, x0].astype(_F32)
        a10 = tex[y0, x1].astype(_F32)
        a01 = tex[y1, x0].astype(_F32)
        a11 = tex[y1, x1].astype(_F32)
        a = (a00 * (_F32(1) - tx) + a10 * tx) * (_F32(1) - ty) + \
            (a01 * (_F32(1) - tx) + a11 * tx) * ty
        return (a + _F32(0.5)).astype(np.uint8)

    def _draw_image(self, c, x, y, w, h):
        tex = self._textures.get(c.tex_id)
        if tex is None:
            return
        clip = self._clips[-1]
        cx, cy, cw, ch = _intersect(x, y, w, h, *clip)
        if cw <= 0 or ch <= 0:
            return
        Y, X = self._grid(cy, np.ceil(cy + ch) - 1, cx, np.ceil(cx + cw) - 1)
        u, v = self._uv_maps(c, x, y, w, h, X, Y)
        if tex.ndim == 3:
            # Rgba8: nearest sample, blend the texel's own color/alpha.
            th, tw = tex.shape[:2]
            sx = np.clip(u.astype(np.int64), 0, tw - 1)
            sy = np.clip(v.astype(np.int64), 0, th - 1)
            px = tex[sy, sx]
            self._blend(X, Y, self._full_mask(X, Y),
                        px[..., 0].astype(_U32), px[..., 1].astype(_U32),
                        px[..., 2].astype(_U32), px[..., 3].astype(_U32))
        else:
            # Alpha8 image: white with the sampled alpha (sample_alpha path).
            alpha = self._sample_alpha(tex, u, v)
            self._blend(X, Y, alpha > 0, _U32(255), _U32(255), _U32(255),
                        alpha.astype(_U32))
