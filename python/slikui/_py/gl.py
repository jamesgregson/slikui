"""GLSink - a slikui.RenderSink that draws straight into an OpenGL 3.3 context.

The fast path for PyOpenGL hosts (glfw, pygame, Qt, ...): slikui's C++
GpuBatcher turns each frame into one instance per command plus a handful of
scissor/texture batches, so Python makes one buffer upload and one draw call
per batch (a few per frame), not one per command. Same shaders as the C++
OpenglSink; output matches the software rasterizer (tests/gl_parity.cpp).

    sink = slikui.GLSink(scale=content_scale)
    sink.init()                    # GL context must be current
    ctx = slikui.Context(sink)
    ...
    ctx.render()                   # draws into the bound framebuffer
    ...
    ctx.clear_tree(); sink.shutdown()   # context still current

Lifecycle:
    init()      compiles the program, creates VAO/VBO. Needs a current context.
    shutdown()  deletes every GL object. Needs the same context current. Not
                done in __del__: the GC may run with no context current.
    reset(ctx)  context was lost/recreated: forget the old handles (without
                deleting them), init() again and ctx.invalidate_textures().

Textures: upload() is create-or-replace (glTexSubImage2D when the size is
unchanged). release(tex_id) only queues the delete; it runs at the start of
the next render() or in shutdown(), when a context is current.

GL state: render() sets viewport, program, VAO, texture unit 0, blending
(straight-alpha over), scissor test, and disables depth/cull/stencil. With
preserve_state=False (default) it leaves them that way - set what your next
pass needs. preserve_state=True saves and restores them with glGet* (slower).
It draws into whatever framebuffer is bound.

One sink per Context, one GL context per sink, one thread.
"""

from __future__ import annotations

import ctypes

import numpy as np

from slikui._slikui import (GPU_FRAGMENT_SHADER, GPU_VERTEX_SHADER, GpuBatcher,
                            RenderSink, TexFormat)

_INSTANCE_BYTES = 20 * 4   # GpuInstance: five vec4
_N_ATTRIBS = 5


def _gl():
    try:
        from OpenGL import GL
    except ImportError as e:  # pragma: no cover - depends on the environment
        raise ImportError(
            "slikui.GLSink needs PyOpenGL: pip install PyOpenGL "
            "(or pip install 'slikui-python[demo]')") from e
    return GL


class GLSink(RenderSink):
    """slikui.RenderSink over OpenGL 3.3 core, via PyOpenGL."""

    def __init__(self, *, scale=1.0, preserve_state=False):
        super().__init__()
        self.scale = float(scale)
        self.preserve_state = bool(preserve_state)
        self.batcher = GpuBatcher()
        self._GL = _gl()
        self._forget()

    # -- lifecycle ---------------------------------------------------------

    def _forget(self):
        self._program = 0
        self._vao = 0
        self._vbo = 0
        self._vbo_capacity = 0
        self._uniforms = {}
        self._textures = {}        # slikui tex id -> (gl name, w, h, format)
        self._pending_delete = []

    @property
    def initialized(self):
        return bool(self._program)

    def init(self):
        """Compile shaders, create VAO/VBO. A GL 3.3 context must be current."""
        GL = self._GL
        vs = self._compile(GL.GL_VERTEX_SHADER, GPU_VERTEX_SHADER)
        fs = self._compile(GL.GL_FRAGMENT_SHADER, GPU_FRAGMENT_SHADER)
        prog = GL.glCreateProgram()
        GL.glAttachShader(prog, vs)
        GL.glAttachShader(prog, fs)
        GL.glLinkProgram(prog)
        GL.glDeleteShader(vs)
        GL.glDeleteShader(fs)
        if not GL.glGetProgramiv(prog, GL.GL_LINK_STATUS):
            log = GL.glGetProgramInfoLog(prog)
            GL.glDeleteProgram(prog)
            raise RuntimeError(f"slikui.GLSink: program link failed: {log!r}")
        self._program = prog
        self._uniforms = {n: GL.glGetUniformLocation(prog, n)
                          for n in ("u_res", "u_scale", "u_tex")}

        self._vao = GL.glGenVertexArrays(1)
        self._vbo = GL.glGenBuffers(1)
        GL.glBindVertexArray(self._vao)
        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, self._vbo)
        for i in range(_N_ATTRIBS):
            GL.glEnableVertexAttribArray(i)
            GL.glVertexAttribDivisor(i, 1)
        self._point_attribs(0)
        GL.glBindVertexArray(0)
        return self

    def shutdown(self):
        """Delete every GL object. The same context must be current."""
        GL = self._GL
        self._flush_deletes()
        # GL defers deleting a program that is still current: unbind ours.
        if self._program and int(GL.glGetIntegerv(GL.GL_CURRENT_PROGRAM)) == self._program:
            GL.glUseProgram(0)
        names = [t[0] for t in self._textures.values()]
        if names:
            GL.glDeleteTextures(names)
        if self._vbo:
            GL.glDeleteBuffers(1, [self._vbo])
        if self._vao:
            GL.glDeleteVertexArrays(1, [self._vao])
        if self._program:
            GL.glDeleteProgram(self._program)
        self._forget()

    def reset(self, context=None):
        """The GL context was lost or recreated: drop the old handles (they
        died with the old context), init() on the current one, and ask
        `context` (a slikui.Context) to re-upload every texture."""
        self._forget()
        self.init()
        if context is not None:
            context.invalidate_textures()

    # -- RenderSink ----------------------------------------------------------

    def upload(self, up):
        GL = self._GL
        px = up.pixels                      # borrowed view: upload right away
        rgba = up.format == TexFormat.Rgba8
        w, h = up.width, up.height
        fmt = GL.GL_RGBA if rgba else GL.GL_RED
        entry = self._textures.get(up.id)
        GL.glPixelStorei(GL.GL_UNPACK_ALIGNMENT, 1)
        if entry is not None and entry[1:] == (w, h, rgba):
            GL.glBindTexture(GL.GL_TEXTURE_2D, entry[0])
            GL.glTexSubImage2D(GL.GL_TEXTURE_2D, 0, 0, 0, w, h, fmt,
                               GL.GL_UNSIGNED_BYTE, px)
            return
        tex = entry[0] if entry is not None else GL.glGenTextures(1)
        GL.glBindTexture(GL.GL_TEXTURE_2D, tex)
        GL.glTexImage2D(GL.GL_TEXTURE_2D, 0, GL.GL_RGBA8 if rgba else GL.GL_R8,
                        w, h, 0, fmt, GL.GL_UNSIGNED_BYTE, px)
        # Same sampling as the software sink: bilinear glyph coverage,
        # nearest-neighbour images.
        filt = GL.GL_NEAREST if rgba else GL.GL_LINEAR
        for p, v in ((GL.GL_TEXTURE_MIN_FILTER, filt), (GL.GL_TEXTURE_MAG_FILTER, filt),
                     (GL.GL_TEXTURE_WRAP_S, GL.GL_CLAMP_TO_EDGE),
                     (GL.GL_TEXTURE_WRAP_T, GL.GL_CLAMP_TO_EDGE)):
            GL.glTexParameteri(GL.GL_TEXTURE_2D, p, v)
        self._textures[up.id] = (tex, w, h, rgba)

    def release(self, tex_id):
        entry = self._textures.pop(tex_id, None)
        if entry is not None:
            self._pending_delete.append(entry[0])

    def render(self, cmds, framebuffer_size):
        if not self._program:
            raise RuntimeError("slikui.GLSink.render before init()")
        GL = self._GL
        self._flush_deletes()
        inst, batches = self.batcher.build(cmds, framebuffer_size, self.scale)
        w, h = self.batcher.framebuffer_width, self.batcher.framebuffer_height
        if w <= 0 or h <= 0:
            return
        saved = self._save_state() if self.preserve_state else None
        try:
            GL.glViewport(0, 0, w, h)
            GL.glUseProgram(self._program)
            GL.glUniform2f(self._uniforms["u_res"], float(w), float(h))
            GL.glUniform1f(self._uniforms["u_scale"], self.batcher.scale)
            GL.glUniform1i(self._uniforms["u_tex"], 0)
            GL.glEnable(GL.GL_BLEND)
            GL.glBlendFuncSeparate(GL.GL_SRC_ALPHA, GL.GL_ONE_MINUS_SRC_ALPHA,
                                   GL.GL_ONE, GL.GL_ONE_MINUS_SRC_ALPHA)
            GL.glDisable(GL.GL_DEPTH_TEST)
            GL.glDisable(GL.GL_CULL_FACE)
            GL.glDisable(GL.GL_STENCIL_TEST)
            GL.glEnable(GL.GL_SCISSOR_TEST)
            GL.glActiveTexture(GL.GL_TEXTURE0)
            GL.glBindVertexArray(self._vao)
            if len(inst):
                self._upload_instances(inst)
                self._draw(batches)
            GL.glBindVertexArray(0)
        finally:
            if saved is not None:
                self._restore_state(saved)

    # -- drawing -------------------------------------------------------------

    def _upload_instances(self, inst):
        GL = self._GL
        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, self._vbo)
        nbytes = inst.nbytes
        if nbytes > self._vbo_capacity:
            self._vbo_capacity = nbytes + nbytes // 2
        # Grow or orphan: fresh storage, no stall on last frame's draws.
        GL.glBufferData(GL.GL_ARRAY_BUFFER, self._vbo_capacity, None, GL.GL_STREAM_DRAW)
        GL.glBufferSubData(GL.GL_ARRAY_BUFFER, 0, nbytes, inst)

    def _point_attribs(self, first):
        # GL 3.3 has no base-instance draw: re-point the five vec4 attributes
        # at instance `first` (ctypes pointers: PyOpenGL treats a bare int
        # offset as client memory).
        GL = self._GL
        base = first * _INSTANCE_BYTES
        for i in range(_N_ATTRIBS):
            GL.glVertexAttribPointer(i, 4, GL.GL_FLOAT, GL.GL_FALSE, _INSTANCE_BYTES,
                                     ctypes.c_void_p(base + i * 16))

    def _draw(self, batches):
        GL = self._GL
        bound = 0
        for first, count, tex_id, sx, sy, sw, sh in batches.tolist():
            if sw <= 0 or sh <= 0:
                continue
            if tex_id:
                entry = self._textures.get(tex_id)
                if entry is None:
                    continue  # never uploaded
                if entry[0] != bound:
                    GL.glBindTexture(GL.GL_TEXTURE_2D, entry[0])
                    bound = entry[0]
            GL.glScissor(sx, sy, sw, sh)
            self._point_attribs(first)
            GL.glDrawArraysInstanced(GL.GL_TRIANGLE_STRIP, 0, 4, count)

    def _flush_deletes(self):
        if self._pending_delete:
            self._GL.glDeleteTextures(self._pending_delete)
            self._pending_delete = []

    def _compile(self, kind, src):
        GL = self._GL
        sh = GL.glCreateShader(kind)
        if not sh:
            raise RuntimeError("slikui.GLSink: glCreateShader failed - "
                               "is a GL 3.3 context current on this thread?")
        GL.glShaderSource(sh, src)
        GL.glCompileShader(sh)
        if not GL.glGetShaderiv(sh, GL.GL_COMPILE_STATUS):
            log = GL.glGetShaderInfoLog(sh)
            GL.glDeleteShader(sh)
            raise RuntimeError(f"slikui.GLSink: shader compile failed: {log!r}")
        return sh

    # -- preserve_state ----------------------------------------------------------

    _CAPS = ("GL_BLEND", "GL_DEPTH_TEST", "GL_CULL_FACE", "GL_STENCIL_TEST",
             "GL_SCISSOR_TEST")

    def _save_state(self):
        GL = self._GL
        gi = GL.glGetIntegerv
        return {
            "caps": {c: bool(GL.glIsEnabled(getattr(GL, c))) for c in self._CAPS},
            "viewport": [int(v) for v in gi(GL.GL_VIEWPORT)],
            "scissor": [int(v) for v in gi(GL.GL_SCISSOR_BOX)],
            "program": int(gi(GL.GL_CURRENT_PROGRAM)),
            "vao": int(gi(GL.GL_VERTEX_ARRAY_BINDING)),
            "array_buffer": int(gi(GL.GL_ARRAY_BUFFER_BINDING)),
            "active_texture": int(gi(GL.GL_ACTIVE_TEXTURE)),
            "blend": [int(gi(p)) for p in (GL.GL_BLEND_SRC_RGB, GL.GL_BLEND_DST_RGB,
                                           GL.GL_BLEND_SRC_ALPHA, GL.GL_BLEND_DST_ALPHA)],
            # texture bound on unit 0 (the one render() changes)
            "texture0": self._texture0(),
            "unpack_alignment": int(gi(GL.GL_UNPACK_ALIGNMENT)),
        }

    def _texture0(self):
        GL = self._GL
        active = int(GL.glGetIntegerv(GL.GL_ACTIVE_TEXTURE))
        GL.glActiveTexture(GL.GL_TEXTURE0)
        tex = int(GL.glGetIntegerv(GL.GL_TEXTURE_BINDING_2D))
        GL.glActiveTexture(active)
        return tex

    def _restore_state(self, s):
        GL = self._GL
        for c, on in s["caps"].items():
            (GL.glEnable if on else GL.glDisable)(getattr(GL, c))
        GL.glViewport(*s["viewport"])
        GL.glScissor(*s["scissor"])
        GL.glUseProgram(s["program"])
        GL.glBindVertexArray(s["vao"])
        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, s["array_buffer"])
        GL.glBlendFuncSeparate(*s["blend"])
        GL.glActiveTexture(GL.GL_TEXTURE0)
        GL.glBindTexture(GL.GL_TEXTURE_2D, s["texture0"])
        GL.glActiveTexture(s["active_texture"])
        GL.glPixelStorei(GL.GL_UNPACK_ALIGNMENT, s["unpack_alignment"])


def read_framebuffer(width, height):
    """Read the bound framebuffer as (H, W) uint32 0xAABBGGRR, top row first -
    the same layout as slikui.Target.pixels() / CanvasSink.buffer."""
    GL = _gl()
    GL.glPixelStorei(GL.GL_PACK_ALIGNMENT, 1)
    raw = GL.glReadPixels(0, 0, width, height, GL.GL_RGBA, GL.GL_UNSIGNED_BYTE)
    px = np.frombuffer(raw, np.uint8).reshape(height, width, 4)[::-1]
    return np.ascontiguousarray(px).view(np.uint32).reshape(height, width)
