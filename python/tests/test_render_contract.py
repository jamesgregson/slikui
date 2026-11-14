"""Render-contract fast paths and texture lifetime.

* DrawList.as_array(): zero-copy structured view matching the DrawCmd objects
* TexUpload.pixels: zero-copy read-only ndarray view
* RenderSink.release / Context.release_texture: dead widget textures freed
* Context.invalidate_textures: every live texture re-uploaded
"""

import sys

import numpy as np
import pytest

import slikui
from slikui._slikui import Context, DrawCmdType, Sink, Target, TexFormat


class RecordingSink(slikui.RenderSink):
    """Records uploads/releases and snapshots each frame both ways."""

    def __init__(self):
        super().__init__()
        self.uploads = []    # (id, format, shape, dtype, writeable, first bytes)
        self.released = []
        self.objects = []    # list of DrawCmd field tuples, last frame
        self.array = None    # copy of as_array(), last frame

    def upload(self, up):
        px = up.pixels
        self.uploads.append((up.id, up.format, px.shape, px.dtype,
                             px.flags.writeable, bytes(px.reshape(-1)[:16])))

    def release(self, tex_id):
        self.released.append(tex_id)

    def render(self, cmds, framebuffer_size):
        view = cmds.as_array()
        self.view_flags = (view.flags.writeable, view.flags.owndata)
        self.array = view.copy()
        self.objects = [
            (c.type.value, (c.rect.x, c.rect.y, c.rect.w, c.rect.h), c.radius,
             c.thickness, c.hollow, (c.color.r, c.color.g, c.color.b, c.color.a),
             c.tex_id, (c.uv.x, c.uv.y, c.uv.w, c.uv.h))
            for c in cmds
        ]


def _ctx(sink, w=240, h=200):
    ctx = Context(sink)
    ctx.set_frame_size(float(w), float(h))
    ctx.load_default_font()
    return ctx


def _frame(ctx, t=0.0):
    ctx.tick(t, 1 / 60)
    ctx.render()


def _rgba(w, h, value=200):
    return np.full((h, w, 4), value, np.uint8)


def _rich(ctx):
    root = ctx.set_root(slikui.Column, padding=8, gap=4)
    root.add(slikui.Label, "as_array parity")
    root.add(slikui.Button, "Go")
    root.add(slikui.Checkbox, "on", checked=True)
    root.add(slikui.Slider, 0.3)
    scroll = root.add(slikui.ScrollArea)
    for i in range(6):
        scroll.add(slikui.Label, f"row {i}")
    root.add(slikui.Divider)
    root.add(slikui.Image, "img").set_pixels(_rgba(8, 6))
    return root


# -- as_array -----------------------------------------------------------------

def test_dtype_layout():
    dt = slikui.DRAWCMD_DTYPE
    assert dt.itemsize == 56
    assert dt.names == ("type", "rect", "radius", "thickness", "hollow",
                        "color", "tex_id", "uv")
    assert dt.fields["color"][1] == 29  # packed after the bool, unaligned


def test_as_array_matches_draw_cmd_objects():
    sink = RecordingSink()
    ctx = _ctx(sink)
    try:
        _rich(ctx)
        _frame(ctx)
        a, objs = sink.array, sink.objects
        assert len(a) == len(objs) > 20
        kinds = set(int(t) for t in a["type"])
        assert {DrawCmdType.Rect.value, DrawCmdType.Glyph.value,
                DrawCmdType.Image.value, DrawCmdType.ClipPush.value,
                DrawCmdType.ClipPop.value} <= kinds
        for row, o in zip(a, objs):
            got = (int(row["type"]), tuple(float(v) for v in row["rect"]),
                   float(row["radius"]), float(row["thickness"]),
                   bool(row["hollow"]), tuple(int(v) for v in row["color"]),
                   int(row["tex_id"]), tuple(float(v) for v in row["uv"]))
            assert got == o
    finally:
        ctx.clear_tree()


def test_as_array_is_read_only_and_zero_copy():
    sink = RecordingSink()
    ctx = _ctx(sink)
    try:
        _rich(ctx)
        _frame(ctx)
        assert sink.view_flags == (False, False)  # borrowed, read-only
        assert sink.array.dtype == slikui.DRAWCMD_DTYPE
        # The clear rect comes first: full frame, theme background.
        first = sink.array[0]
        assert first["type"] == DrawCmdType.Rect.value
        assert tuple(first["rect"]) == (0, 0, 240, 200)
    finally:
        ctx.clear_tree()


def test_as_array_empty():
    sink = RecordingSink()
    ctx = _ctx(sink)
    ctx.tick(0.0, 1 / 60)
    ctx.render(False)  # no root, no clear rect: an empty list
    assert sink.array.shape == (0,)
    assert sink.array.dtype == slikui.DRAWCMD_DTYPE


# -- TexUpload.pixels -----------------------------------------------------------

def test_upload_pixels_are_read_only_views():
    sink = RecordingSink()
    ctx = _ctx(sink)
    try:
        root = ctx.set_root(slikui.Column)
        root.add(slikui.Label, "atlas")
        root.add(slikui.Image, "img").set_pixels(_rgba(5, 3, value=77))
        _frame(ctx)
        by_fmt = {u[1]: u for u in sink.uploads}
        a8 = by_fmt[TexFormat.Alpha8]
        assert len(a8[2]) == 2 and a8[3] == np.uint8 and not a8[4]
        rgba = by_fmt[TexFormat.Rgba8]
        assert rgba[2] == (3, 5, 4) and rgba[3] == np.uint8 and not rgba[4]
        assert rgba[5] == bytes([77]) * 16
    finally:
        ctx.clear_tree()


# -- texture lifetime -------------------------------------------------------------

def _image_ids(sink):
    return {u[0] for u in sink.uploads if u[1] == TexFormat.Rgba8}


def test_release_on_widget_destruction():
    sink = RecordingSink()
    ctx = _ctx(sink)
    try:
        root = ctx.set_root(slikui.Column)
        root.add(slikui.Image, "img").set_pixels(_rgba(4, 4))
        root.add(slikui.ImageView).set_pixels(_rgba(4, 4))
        cv = root.add(slikui.CompareView)
        cv.set_pixels_a(_rgba(4, 4))
        cv.set_pixels_b(_rgba(4, 4))
        _frame(ctx)
        ids = _image_ids(sink)
        assert len(ids) == 4
        assert sink.released == []
        ctx.clear_tree()
        assert sorted(sink.released) == sorted(ids)
    finally:
        ctx.clear_tree()


def test_release_on_root_replace_and_repeat_does_not_grow():
    target = Target(120, 90)
    sink = Sink(target)
    ctx = _ctx(sink, 120, 90)
    try:
        counts = []
        for i in range(5):
            root = ctx.set_root(slikui.Column)
            root.add(slikui.Label, "x")
            root.add(slikui.Image, "img").set_pixels(_rgba(4, 4, value=i))
            _frame(ctx, i / 60)
            counts.append(sink.texture_count)
        assert counts == [2] * 5  # font atlas + the one live image
        ctx.clear_tree()
        assert sink.texture_count == 1  # font atlas only
    finally:
        ctx.clear_tree()


def test_no_release_from_context_destructor():
    sink = RecordingSink()
    ctx = _ctx(sink)
    ctx.set_root(slikui.Column).add(slikui.Image, "img").set_pixels(_rgba(4, 4))
    _frame(ctx)
    del ctx
    import gc
    gc.collect()
    assert sink.released == []


def test_release_exception_is_unraisable(monkeypatch):
    class Boom(RecordingSink):
        def release(self, tex_id):
            raise RuntimeError("boom")

    seen = []
    # Keep only the type: the record's traceback would pin the sink alive.
    monkeypatch.setattr(sys, "unraisablehook",
                        lambda u: seen.append(type(u.exc_value)))
    sink = Boom()
    ctx = _ctx(sink)
    try:
        ctx.set_root(slikui.Column).add(slikui.Image, "img").set_pixels(_rgba(4, 4))
        _frame(ctx)
        ctx.clear_tree()  # must not raise or abort
        assert seen == [RuntimeError]
    finally:
        ctx.clear_tree()


def test_default_release_is_noop():
    class Minimal(slikui.RenderSink):
        def upload(self, up):
            pass

        def render(self, cmds, fb):
            pass

    sink = Minimal()
    ctx = _ctx(sink)
    ctx.set_root(slikui.Column).add(slikui.Image, "img").set_pixels(_rgba(4, 4))
    _frame(ctx)
    ctx.clear_tree()


def test_canvas_sink_release():
    sink = slikui.CanvasSink(120, 90)
    ctx = _ctx(sink, 120, 90)
    try:
        ctx.set_root(slikui.Column).add(slikui.Image, "img").set_pixels(_rgba(4, 4))
        _frame(ctx)
        assert sink.texture_count == 2
        ctx.clear_tree()
        assert sink.texture_count == 1
    finally:
        ctx.clear_tree()


# -- invalidate_textures ------------------------------------------------------------

def test_invalidate_reuploads_everything_once():
    sink = RecordingSink()
    ctx = _ctx(sink)
    try:
        root = ctx.set_root(slikui.Column)
        root.add(slikui.Label, "text")
        root.add(slikui.Image, "img").set_pixels(_rgba(4, 4))
        _frame(ctx)
        first = {u[0] for u in sink.uploads}
        assert len(first) == 2
        sink.uploads.clear()
        _frame(ctx, 1 / 60)
        assert sink.uploads == []          # nothing dirty
        ctx.invalidate_textures()
        _frame(ctx, 2 / 60)
        assert {u[0] for u in sink.uploads} == first
        sink.uploads.clear()
        _frame(ctx, 3 / 60)
        assert sink.uploads == []
    finally:
        ctx.clear_tree()


def test_image_in_dialog_is_uploaded():
    sink = RecordingSink()
    ctx = _ctx(sink)
    dlg = slikui.Dialog("Pic")
    try:
        ctx.set_root(slikui.Column).add(slikui.Label, "app")
        dlg.content().add(slikui.Image, "img").set_pixels(_rgba(6, 6))
        ctx.open_dialog(dlg)
        _frame(ctx)
        assert len(_image_ids(sink)) == 1
        drawn = {int(t) for t in sink.array["tex_id"]
                 [sink.array["type"] == DrawCmdType.Image.value]}
        assert drawn == _image_ids(sink)
    finally:
        ctx.close_dialog(dlg)
        ctx.clear_tree()
