"""07 — The render contract: one widget tree, any renderer.

slikui never draws pixels itself — a ``Context`` hands each frame to a
*sink*: texture uploads (the font atlas, images) and a flat list of draw
commands (Rect, Line, Glyph, Image, ClipPush, ClipPop). This example:

* renders the same tree through the C++ ``Sink`` and the pure-Python
  ``CanvasSink`` and proves the frames are pixel-identical;
* shows the seam for your own renderer — ``TraceSink`` subclasses
  ``slikui.RenderSink``, receives every upload and command, and forwards
  them to ``CanvasSink`` so the skeleton runs as-is. Replace the
  forwarding with your GPU/pygame/Qt calls (notes inline).

Run:

    python -m slikui.examples.renderer            # writes ./renderer.png
    python -m slikui.examples.renderer --smoke    # parity + skeleton checks
"""

from collections import Counter

import numpy as np

import slikui
from slikui import Context, Sink, Target, find_system_font
from slikui.examples import _support

NAME = "renderer"
SIZE = (420, 300)


def build_tree(root):
    root.set_padding(14).set_gap(8)
    root.add(slikui.Label, "One tree, any renderer").set_font_size(22)
    row = root.add(slikui.Row, gap=6)
    row.add(slikui.Button, "Primary", variant=slikui.ButtonVariant.Primary)
    row.add(slikui.Button, "Secondary")
    row.add(slikui.Button, "Ghost", variant=slikui.ButtonVariant.Ghost)
    root.add(slikui.Checkbox, "checkbox", checked=True)
    root.add(slikui.Slider).set_value(0.35)
    root.add(slikui.ProgressBar).set_value(0.62)
    panel = root.add(slikui.Panel)
    panel.set_padding(8)
    panel.add(slikui.Label, "panel with rounded corners")
    scroll = root.add(slikui.ScrollArea)  # exercises ClipPush/ClipPop
    for i in range(8):
        scroll.add(slikui.Label, f"clipped row {i}")


def build(app):
    build_tree(app.set_root(slikui.Column))
    return _support.State()


# doc: tracesink
from collections import Counter

import slikui


class TraceSink(slikui.RenderSink):
    """The skeleton of a third-party renderer.

    ``upload`` gets each texture once (and again when it changes):
    ``up.id``, ``up.width``, ``up.height``, ``up.format`` (Alpha8 for the
    font atlas, Rgba8 for images) and ``up.pixels``. Create your GPU
    texture here, keyed by ``up.id``.

    ``render`` gets the whole frame, in paint order. Per command:

    * Rect  — ``rect``, ``color``, ``radius``; ``hollow`` + ``thickness``
      for outlines (a rounded-rect SDF quad on the GPU).
    * Line  — from ``(rect.x, rect.y)`` to ``(rect.x + rect.w,
      rect.y + rect.h)``, ``thickness``, ``color`` (a capsule).
    * Glyph — textured quad: ``rect`` on screen, ``uv`` in *texel* units
      of texture ``tex_id`` (alpha8), tinted by ``color``.
    * Image — like Glyph, rgba texture, untinted.
    * ClipPush / ClipPop — a scissor stack: intersect ``rect`` with the top.

    Coordinates are logical points; multiply by your HiDPI scale. To draw
    over your own 3D scene, call ``ctx.render(False)`` so slikui skips its
    full-frame background rect.
    """

    def __init__(self, width, height):
        super().__init__()
        self.inner = slikui.CanvasSink(width, height)  # stand-in backend
        self.uploads = {}       # tex id -> (w, h, format)
        self.counts = Counter()

    def upload(self, up):
        self.uploads[up.id] = (up.width, up.height, up.format)
        self.inner.upload(up)          # → glTexImage2D / pygame.Surface ...

    def render(self, cmds, framebuffer_size):
        self.counts = Counter(c.type.name for c in cmds)
        self.inner.render(cmds, framebuffer_size)  # → your draw loop


# doc: end


def render_with(sink_factory, w, h):
    """Build the tree into a fresh Context over `sink`; return the sink."""
    sink = sink_factory(w, h)
    ctx = Context(sink)
    ctx.set_frame_size(float(w), float(h))
    ctx.load_font_file(find_system_font()) or ctx.load_default_font()
    build_tree(ctx.set_root_column())
    ctx.tick(0.0, 1 / 60)
    ctx.render()
    ctx.clear_tree()
    return sink


class _CppSink:
    """C++ software.Sink + its Target, kept together (the Sink borrows it)."""

    def __init__(self, w, h):
        self.target = Target(w, h)
        self.sink = Sink(self.target)


def smoke(app, state, session, log):
    c = _support.Checks(log)
    w, h = SIZE

    cpp_holder = {}

    def cpp_factory(w_, h_):
        cpp_holder["o"] = _CppSink(w_, h_)
        return cpp_holder["o"].sink

    render_with(cpp_factory, w, h)
    a = cpp_holder["o"].target.pixels().copy()
    b = render_with(slikui.CanvasSink, w, h).buffer.copy()
    diff = int(np.count_nonzero(a != b))
    log.say(f"C++ Sink vs CanvasSink: {diff} differing pixels of {a.size}")
    c.ok(diff == 0, "pure-Python renderer is pixel-identical to C++")

    trace = render_with(TraceSink, w, h)
    c.ok(any(f == slikui.TexFormat.Alpha8 for *_, f in trace.uploads.values()),
         "the font atlas arrives as an Alpha8 upload")
    for kind in ("Rect", "Line", "Glyph", "ClipPush", "ClipPop"):
        c.ok(trace.counts[kind] > 0, f"{kind} commands in the frame")
    c.ok(int(np.count_nonzero(trace.inner.buffer != b)) == 0,
         "the skeleton's forwarded frame matches")
    return {"diff": diff, "commands": dict(trace.counts)}


def main(argv=None):
    return _support.main_entry(name=NAME, size=SIZE, build=build, smoke=smoke,
                               argv=argv)


if __name__ == "__main__":
    raise SystemExit(main())
