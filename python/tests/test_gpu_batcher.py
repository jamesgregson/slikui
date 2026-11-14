"""slikui.GpuBatcher: DrawList -> (instances, batches) zero-copy arrays.

Pure CPU — no GL needed. The GL side is covered by tests/gl_parity.cpp
(C++ OpenglSink) and test_gl_sink.py (Python sinks, skipped without GL).
"""

import numpy as np

import slikui
from slikui import DrawCmdType, GpuKind
from slikui._slikui import Context


class BatchingSink(slikui.RenderSink):
    def __init__(self, scale=1.0):
        super().__init__()
        self.batcher = slikui.GpuBatcher()
        self.scale = scale

    def upload(self, up):
        pass

    def render(self, cmds, frame):
        self.cmds = cmds.as_array().copy()
        inst, bat = self.batcher.build(cmds, frame, self.scale)
        self.flags = (inst.flags.writeable, bat.flags.writeable)
        self.inst, self.bat = inst.copy(), bat.copy()


def _render(build, w=240, h=200, scale=1.0):
    sink = BatchingSink(scale)
    ctx = Context(sink)
    ctx.set_frame_size(float(w), float(h))
    ctx.load_default_font()
    try:
        build(ctx)
        ctx.tick(0.0, 1 / 60)
        ctx.render()
        return sink
    finally:
        ctx.clear_tree()


def _tree(ctx):
    root = ctx.set_root(slikui.Column, padding=8, gap=4)
    root.add(slikui.Label, "batched text")
    root.add(slikui.Button, "Go")
    root.add(slikui.Checkbox, "on", checked=True)
    root.add(slikui.Image, "img").set_pixels(np.full((4, 4, 4), 200, np.uint8))
    scroll = root.add(slikui.ScrollArea)
    for i in range(10):
        scroll.add(slikui.Label, f"row {i}")


def test_shapes_and_dtypes():
    s = _render(_tree)
    assert s.inst.dtype == np.float32 and s.inst.shape[1] == 20
    assert s.bat.dtype == np.int32 and s.bat.shape[1] == 7
    assert s.flags == (False, False)


def test_every_instance_is_in_exactly_one_batch():
    s = _render(_tree)
    first, count = s.bat[:, 0], s.bat[:, 1]
    assert (count > 0).all()
    assert first[0] == 0
    assert (first[1:] == first[:-1] + count[:-1]).all()
    assert first[-1] + count[-1] == len(s.inst)


def test_instance_kinds_follow_commands():
    s = _render(_tree)
    kinds = s.inst[:, 12].astype(int)
    n_glyph = int((s.cmds["type"] == DrawCmdType.Glyph.value).sum())
    n_image = int((s.cmds["type"] == DrawCmdType.Image.value).sum())
    # Glyphs/images can only be culled (outside a clip), never invented.
    assert 0 < (kinds == GpuKind.Glyph.value).sum() <= n_glyph
    assert (kinds == GpuKind.Image.value).sum() == n_image == 1
    assert len(s.inst) <= len(s.cmds)


def test_textured_batches_bind_the_right_texture():
    s = _render(_tree)
    for first, count, tex, *_ in s.bat:
        kinds = s.inst[first:first + count, 12].astype(int)
        textured = (kinds == GpuKind.Glyph.value) | (kinds == GpuKind.Image.value)
        if textured.any():
            assert tex != 0
        else:
            assert tex == 0


def test_far_fewer_batches_than_commands():
    s = _render(_tree)
    assert len(s.bat) * 5 < len(s.cmds)


def test_scissor_flips_and_scales():
    s1 = _render(_tree, 240, 200, 1.0)
    s2 = _render(_tree, 240, 200, 2.0)
    full1 = s1.bat[0, 3:]
    assert tuple(full1) == (0, 0, 240, 200)
    assert tuple(s2.bat[0, 3:]) == (0, 0, 480, 400)
    # Instances stay logical: the geometry is not scaled. (The cull margin
    # is 1 framebuffer px, so the culled set can differ slightly by scale.)
    r1 = {tuple(r) for r in s1.inst[:, :4].tolist()}
    r2 = {tuple(r) for r in s2.inst[:, :4].tolist()}
    assert len(r1 & r2) >= 0.9 * max(len(r1), len(r2))
    assert tuple(s1.inst[0, :4]) == tuple(s2.inst[0, :4]) == (0, 0, 240, 200)


def test_views_track_the_batcher():
    b = slikui.GpuBatcher()
    seen = {}

    class Sink(BatchingSink):
        def render(self, cmds, frame):
            inst, bat = b.build(cmds, frame, 1.0)
            seen["n"] = len(inst)
            seen["fb"] = (b.framebuffer_width, b.framebuffer_height)

    sink = Sink()
    ctx = Context(sink)
    ctx.set_frame_size(100.0, 60.0)
    ctx.tick(0.0, 1 / 60)
    ctx.render()  # clear rect only
    assert seen == {"n": 1, "fb": (100, 60)}
    ctx.render(False)  # empty list
    assert seen["n"] == 0


def test_shader_sources_exported():
    assert slikui.GPU_VERTEX_SHADER.startswith("#version 330 core")
    assert "a_line" in slikui.GPU_VERTEX_SHADER
    assert "sd_box" in slikui.GPU_FRAGMENT_SHADER
