"""ModernGLSink - slikui.RenderSink over moderngl (OpenGL 3.3).

Same design as slikui.GLSink (C++ GpuBatcher + the shared GLSL shaders: one
buffer write and one instanced draw per scissor/texture batch), through
moderngl instead of PyOpenGL - much lower per-call overhead, and it plugs
into a moderngl host's own Context.

    import moderngl
    mgl = moderngl.create_context()          # or your app's context
    sink = slikui.ModernGLSink(mgl, scale=content_scale).init()
    ctx = slikui.Context(sink)
    ...
    ctx.render()          # draws into mgl.fbo (the framebuffer moderngl has in use)
    ...
    ctx.clear_tree(); sink.shutdown()

Lifecycle, as GLSink: init() / shutdown() with the context current; release()
only queues texture deletes (done at the next render() or shutdown());
reset(context) after a lost/recreated GL context.

GL state: render() sets the target framebuffer's viewport and scissor, uses
`enable_only(BLEND)` (depth test and face culling off) with straight-alpha
"over" blending, and binds texture unit 0. It leaves them that way. To
keep your own state, wrap the call in a moderngl scope, e.g.
`with mgl.scope(enable_only=...)`, or reset what your next pass needs.

One sink per Context, one GL context per sink, one thread.
"""

from __future__ import annotations

from slikui._slikui import (GPU_FRAGMENT_SHADER, GPU_VERTEX_SHADER, GpuBatcher,
                            RenderSink, TexFormat)

_INSTANCE_BYTES = 20 * 4
_N_ATTRIBS = 5   # a_rect, a_color, a_uv, a_shape, a_line at locations 0..4


def _moderngl():
    try:
        import moderngl
    except ImportError as e:  # pragma: no cover - depends on the environment
        raise ImportError(
            "slikui.ModernGLSink needs moderngl: pip install moderngl") from e
    return moderngl


class ModernGLSink(RenderSink):
    """slikui.RenderSink over a moderngl.Context."""

    def __init__(self, mgl=None, *, scale=1.0):
        """`mgl`: a moderngl.Context; None creates one for the GL context
        current at init() (moderngl.create_context())."""
        super().__init__()
        self._moderngl = _moderngl()
        self.mgl = mgl
        self.scale = float(scale)
        self.batcher = GpuBatcher()
        self._forget()

    # -- lifecycle ------------------------------------------------------------

    def _forget(self):
        self._prog = None
        self._vbo = None
        self._vao = None
        self._textures = {}       # slikui tex id -> moderngl.Texture
        self._pending_release = []

    @property
    def initialized(self):
        return self._prog is not None

    def init(self):
        if self.mgl is None:
            self.mgl = self._moderngl.create_context()
        mgl = self.mgl
        self._prog = mgl.program(vertex_shader=GPU_VERTEX_SHADER,
                                 fragment_shader=GPU_FRAGMENT_SHADER)
        self._prog["u_tex"].value = 0
        self._vbo = mgl.buffer(reserve=_INSTANCE_BYTES * 256, dynamic=True)
        # No per-vertex data: corners come from gl_VertexID; the five
        # per-instance attributes are bound per batch (see _point_attribs).
        self._vao = mgl.vertex_array(self._prog, [])
        return self

    def shutdown(self):
        """Release every GL object. The same context must be current."""
        self._flush_releases()
        for tex in self._textures.values():
            tex.release()
        for obj in (self._vao, self._vbo, self._prog):
            if obj is not None:
                obj.release()
        self._forget()

    def reset(self, context=None, mgl=None):
        """GL context lost/recreated: drop the dead objects (not released -
        they went with the old context), init() on `mgl` (or a context
        created for the current one) and re-upload via `context`."""
        self.mgl = mgl
        self._forget()
        self.init()
        if context is not None:
            context.invalidate_textures()

    # -- RenderSink ---------------------------------------------------------------

    def upload(self, up):
        mgl = self.mgl
        m = self._moderngl
        rgba = up.format == TexFormat.Rgba8
        size = (up.width, up.height)
        px = up.pixels                      # borrowed view: upload right away
        tex = self._textures.get(up.id)
        if tex is not None and tex.size == size and tex.components == (4 if rgba else 1):
            tex.write(px)
            return
        if tex is not None:
            tex.release()
        tex = mgl.texture(size, 4 if rgba else 1, px, alignment=1)
        # Same sampling as the software sink: bilinear glyph coverage,
        # nearest-neighbour images; clamp to edge.
        filt = m.NEAREST if rgba else m.LINEAR
        tex.filter = (filt, filt)
        tex.repeat_x = tex.repeat_y = False
        self._textures[up.id] = tex

    def release(self, tex_id):
        tex = self._textures.pop(tex_id, None)
        if tex is not None:
            self._pending_release.append(tex)

    def render(self, cmds, framebuffer_size):
        if self._prog is None:
            raise RuntimeError("slikui.ModernGLSink.render before init()")
        mgl = self.mgl
        m = self._moderngl
        self._flush_releases()
        inst, batches = self.batcher.build(cmds, framebuffer_size, self.scale)
        w, h = self.batcher.framebuffer_width, self.batcher.framebuffer_height
        if w <= 0 or h <= 0:
            return
        fbo = mgl.fbo
        fbo.viewport = (0, 0, w, h)
        mgl.enable_only(m.BLEND)
        mgl.blend_func = (m.SRC_ALPHA, m.ONE_MINUS_SRC_ALPHA,
                          m.ONE, m.ONE_MINUS_SRC_ALPHA)
        self._prog["u_res"].value = (float(w), float(h))
        self._prog["u_scale"].value = self.batcher.scale
        if len(inst):
            if inst.nbytes > self._vbo.size:
                self._vbo.orphan(inst.nbytes + inst.nbytes // 2)
            else:
                self._vbo.orphan()  # fresh storage: no stall on last frame
            self._vbo.write(inst)
            self._draw(fbo, batches)
        fbo.scissor = None

    # -- drawing -------------------------------------------------------------------

    def _point_attribs(self, first):
        # bind() takes attribute LOCATIONS; the shader pins a_rect..a_line to
        # locations 0..4 (= the GpuInstance fields in order).
        base = first * _INSTANCE_BYTES
        for loc in range(_N_ATTRIBS):
            self._vao.bind(loc, "f", self._vbo, "4f", offset=base + loc * 16,
                           stride=_INSTANCE_BYTES, divisor=1)

    def _draw(self, fbo, batches):
        strip = self._moderngl.TRIANGLE_STRIP
        bound = None
        for first, count, tex_id, sx, sy, sw, sh in batches.tolist():
            if sw <= 0 or sh <= 0:
                continue
            if tex_id:
                tex = self._textures.get(tex_id)
                if tex is None:
                    continue  # never uploaded
                if tex is not bound:
                    tex.use(0)
                    bound = tex
            fbo.scissor = (sx, sy, sw, sh)
            self._point_attribs(first)
            self._vao.render(strip, vertices=4, instances=count)

    def _flush_releases(self):
        for tex in self._pending_release:
            tex.release()
        self._pending_release = []
