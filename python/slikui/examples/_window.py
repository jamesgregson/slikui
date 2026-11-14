"""Shared GLFW host for the shipped examples' ``--window`` mode.

Port of the repo-only showcase's live loop. GLFW callbacks are translated
into ``slikui`` events and fed to the app. By default the UI is drawn on the
GPU by ``slikui.GlApp`` (GLSink: a few instanced draws per frame); with
``--software`` the core rasterizes into its numpy framebuffer and we blit
that onto a fullscreen quad each frame. Needs the optional ``demo`` extra:

    pip install "slikui-python[demo]"

Examples stay headless by default — this module is imported lazily, so
``import slikui.examples.hello`` never pulls in glfw or PyOpenGL.
"""

import numpy as np

import slikui

_BLIT_VS = """
#version 330 core
layout(location=0) in vec2 a_pos;
layout(location=1) in vec2 a_uv;
out vec2 v_uv;
void main(){ v_uv=a_uv; gl_Position=vec4(a_pos,0,1); }
"""

_BLIT_FS = """
#version 330 core
in vec2 v_uv;
uniform sampler2D u_tex;
out vec4 frag;
void main(){ frag = texture(u_tex, v_uv); }
"""

# "gl" (GlApp, default) or "software" (App + texture blit); main_entry's
# --software flag sets it before calling an example's interactive().
RENDERER = "gl"

# Cap window height so tall trees (gallery) still open on a laptop; the
# tree scrolls if it is taller than the window.
_MAX_W, _MAX_H = 900.0, 720.0


def run(name, size, build, scale=1.0):
    """Open a live window for ``build(app)`` at logical ``size``."""
    import glfw
    from OpenGL import GL

    def compile_shader(kind, src):
        sh = GL.glCreateShader(kind)
        GL.glShaderSource(sh, src)
        GL.glCompileShader(sh)
        if not GL.glGetShaderiv(sh, GL.GL_COMPILE_STATUS):
            raise RuntimeError(GL.glGetShaderInfoLog(sh).decode())
        return sh

    def build_program(vs_src, fs_src):
        prog = GL.glCreateProgram()
        for kind, src in ((GL.GL_VERTEX_SHADER, vs_src),
                          (GL.GL_FRAGMENT_SHADER, fs_src)):
            sh = compile_shader(kind, src)
            GL.glAttachShader(prog, sh)
            GL.glDeleteShader(sh)
        GL.glLinkProgram(prog)
        if not GL.glGetProgramiv(prog, GL.GL_LINK_STATUS):
            raise RuntimeError(GL.glGetProgramInfoLog(prog).decode())
        return prog

    lw, lh = size
    w_px = min(lw * scale, _MAX_W)
    h_px = min(lh * scale, _MAX_H)

    if not glfw.init():
        raise SystemExit("glfw.init failed")
    glfw.window_hint(glfw.CONTEXT_VERSION_MAJOR, 3)
    glfw.window_hint(glfw.CONTEXT_VERSION_MINOR, 3)
    glfw.window_hint(glfw.OPENGL_PROFILE, glfw.OPENGL_CORE_PROFILE)
    try:  # macOS needs forward compatibility for 3.3 core
        glfw.window_hint(glfw.OPENGL_FORWARD_COMPAT, True)
    except Exception:
        pass
    win = glfw.create_window(int(w_px), int(h_px), f"slikui example: {name}",
                             None, None)
    if not win:
        glfw.terminate()
        raise SystemExit("could not create GLFW window")
    glfw.make_context_current(win)
    glfw.set_window_size_limits(win, 320, 240, glfw.DONT_CARE, glfw.DONT_CARE)

    if RENDERER == "gl":
        fw, fh = glfw.get_framebuffer_size(win)
        ww, _ = glfw.get_window_size(win)
        app = slikui.GlApp(int(lw), int(lh), scale=fw / ww if ww > 0 else 1.0)
        build(app)

        def prepare(fw, fh, ww, wh, ui_scale):
            app.set_scale(ui_scale)
            app.resize(ww, wh)          # logical points

        def draw(fw, fh):
            GL.glBindFramebuffer(GL.GL_FRAMEBUFFER, 0)
            app.render()                # clear rect + UI, straight to the window
    else:
        app, prepare, draw = _software_host(GL, build_program, lw, lh, build)

    _loop(glfw, win, app, prepare, draw)
    app.close()  # GL objects + UI callbacks, while the context and interpreter live
    glfw.destroy_window(win)
    glfw.terminate()
    return 0


def _software_host(GL, build_program, lw, lh, build):
    """App + CPU framebuffer blitted as one texture (the --software path)."""
    # Blit program + fullscreen quad.
    prog = build_program(_BLIT_VS, _BLIT_FS)
    u_tex = GL.glGetUniformLocation(prog, "u_tex")
    vao = GL.glGenVertexArrays(1)
    GL.glBindVertexArray(vao)
    quad_pos = np.array([-1, 1, 1, 1, -1, -1, 1, 1, 1, -1, -1, -1], np.float32)
    quad_uv = np.array([0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 0, 1], np.float32)
    for loc, data in ((0, quad_pos), (1, quad_uv)):
        buf = GL.glGenBuffers(1)
        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, buf)
        GL.glBufferData(GL.GL_ARRAY_BUFFER, data.nbytes, data, GL.GL_STATIC_DRAW)
        GL.glEnableVertexAttribArray(loc)
        GL.glVertexAttribPointer(loc, 2, GL.GL_FLOAT, GL.GL_FALSE, 0, None)
    tex = GL.glGenTextures(1)
    GL.glBindTexture(GL.GL_TEXTURE_2D, tex)
    GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_MIN_FILTER, GL.GL_LINEAR)
    GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_MAG_FILTER, GL.GL_LINEAR)
    tex_size = [0, 0]

    app = slikui.App(int(lw), int(lh))
    build(app)

    def prepare(fw, fh, ww, wh, ui_scale):
        if (fw, fh) != (app.target.width, app.target.height):
            app.target.resize(fw, fh)
        app.ctx.theme.scale = ui_scale
        app.sink.set_scale(ui_scale)
        app.ctx.set_frame_size(float(ww), float(wh))  # logical points

    def draw(fw, fh):
        GL.glViewport(0, 0, fw, fh)
        GL.glClearColor(0.0, 0.0, 0.0, 1.0)
        GL.glClear(GL.GL_COLOR_BUFFER_BIT)
        pix = app.render()  # (H, W) uint32 ABGR
        hh, wwd = pix.shape
        GL.glEnable(GL.GL_BLEND)
        GL.glBlendFunc(GL.GL_ONE, GL.GL_ONE_MINUS_SRC_ALPHA)  # premultiplied
        GL.glBindTexture(GL.GL_TEXTURE_2D, tex)
        if tex_size[0] != wwd or tex_size[1] != hh:
            GL.glTexImage2D(GL.GL_TEXTURE_2D, 0, GL.GL_RGBA, wwd, hh, 0,
                            GL.GL_RGBA, GL.GL_UNSIGNED_BYTE, None)
            tex_size[:] = [wwd, hh]
        GL.glTexSubImage2D(GL.GL_TEXTURE_2D, 0, 0, 0, wwd, hh, GL.GL_RGBA,
                           GL.GL_UNSIGNED_BYTE, pix.view(np.uint8))
        GL.glUseProgram(prog)
        GL.glUniform1i(u_tex, 0)
        GL.glBindVertexArray(vao)
        GL.glDrawArrays(GL.GL_TRIANGLES, 0, 6)
        GL.glDisable(GL.GL_BLEND)

    return app, prepare, draw


def _loop(glfw, win, app, prepare, draw):
    """Input callbacks + the frame loop, shared by both renderers."""
    # slikui.GlfwInput translates GLFW callbacks into slikui events and hands
    # them to the app as they arrive; attach_services wires clipboard and
    # cursor shapes. The same two calls work on any GLFW window you create.
    input_ = slikui.GlfwInput(win, app)
    slikui.glfw_input.attach_services(app.ctx, win)

    import time as _time
    last = _time.perf_counter()
    # An app closes its window by setting `app.quit = True` (e.g. a shortcut).
    while not glfw.window_should_close(win) and not getattr(app, "quit", False):
        glfw.poll_events()
        fw, fh = glfw.get_framebuffer_size(win)
        ww, wh = glfw.get_window_size(win)
        ui_scale = fw / ww if ww > 0 else 1.0
        prepare(fw, fh, ww, wh, ui_scale)  # size + scale before layout

        now = _time.perf_counter()
        dt = min(now - last, 0.05)
        last = now
        app.tick(now, dt)
        draw(fw, fh)
        glfw.swap_buffers(win)
    input_.close()
