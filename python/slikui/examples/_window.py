"""Shared GLFW host for the shipped examples' ``--window`` mode.

Port of the repo-only showcase's live loop. GLFW callbacks are translated
into ``slikui`` events and fed to the app; the core renders into its numpy
framebuffer and we blit that premultiplied onto a fullscreen quad each
frame. Needs the optional ``demo`` extra:

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

# GLFW key-name → slikui.Key, built lazily from glfw's constants (glfw is an
# optional dependency): every slikui.Key name maps to KEY_<NAME>, with these
# spellings differing.
_GLFW_KEY_NAMES = {"PageUp": "PAGE_UP", "PageDown": "PAGE_DOWN",
                   "CapsLock": "CAPS_LOCK", **{f"Num{d}": str(d) for d in range(10)}}


def _key_map(glfw):
    out = {}
    for name in dir(slikui.Key):
        if name.startswith("_") or name == "Unknown":
            continue
        code = getattr(glfw, "KEY_" + _GLFW_KEY_NAMES.get(name, name.upper()), None)
        if isinstance(code, int):
            out[code] = getattr(slikui.Key, name)
    out[glfw.KEY_KP_ENTER] = slikui.Key.Enter
    return out


_BUTTON_MAP = {0: slikui.MouseButton.MouseLeft,
               1: slikui.MouseButton.MouseMiddle,
               2: slikui.MouseButton.MouseRight}

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

    def on_button(_w, button, action, _mods):
        x, y = glfw.get_cursor_pos(win)
        if button not in _BUTTON_MAP:
            return
        if action == glfw.PRESS:
            app.handle(slikui.mouse_press((x, y), _BUTTON_MAP[button]))
        elif action == glfw.RELEASE:
            app.handle(slikui.mouse_release((x, y), _BUTTON_MAP[button]))

    def held_mods():
        # GLFW's move/scroll callbacks carry no modifiers: read key state so
        # Shift-drag and Ctrl-wheel work.
        mask = 0
        for keys_, bit in (((glfw.KEY_LEFT_SHIFT, glfw.KEY_RIGHT_SHIFT), slikui.Mod.ModShift),
                           ((glfw.KEY_LEFT_CONTROL, glfw.KEY_RIGHT_CONTROL), slikui.Mod.ModCtrl),
                           ((glfw.KEY_LEFT_ALT, glfw.KEY_RIGHT_ALT), slikui.Mod.ModAlt),
                           ((glfw.KEY_LEFT_SUPER, glfw.KEY_RIGHT_SUPER), slikui.Mod.ModSuper)):
            if any(glfw.get_key(win, k) == glfw.PRESS for k in keys_):
                mask |= int(bit)
        return mask

    def on_cursor(_w, x, y):
        e = slikui.mouse_move((x, y))
        e.mods = held_mods()
        app.handle(e)

    def on_scroll(_w, xoff, yoff):
        x, y = glfw.get_cursor_pos(win)
        e = slikui.scroll((x, y), (xoff, yoff))
        e.mods = held_mods()
        app.handle(e)

    keys = _key_map(glfw)

    def on_key(_w, key, _sc, action, mods):
        if key not in keys:
            return
        if action in (glfw.PRESS, glfw.REPEAT):  # REPEAT = auto-repeat press
            app.handle(slikui.key_press(keys[key], mods,
                                        repeat=action == glfw.REPEAT))
        elif action == glfw.RELEASE:
            app.handle(slikui.key_release(keys[key], mods))

    def on_focus(_w, focused):
        if not focused:  # drags, hover and held keys cancel
            app.handle(slikui.focus_lost())

    # Cursor shapes widgets request (set_cursor) and the system clipboard.
    shapes = {slikui.Cursor.Arrow: glfw.ARROW_CURSOR,
              slikui.Cursor.IBeam: glfw.IBEAM_CURSOR,
              slikui.Cursor.Hand: glfw.HAND_CURSOR,
              slikui.Cursor.Grab: glfw.HAND_CURSOR,
              slikui.Cursor.Crosshair: glfw.CROSSHAIR_CURSOR,
              slikui.Cursor.ResizeH: glfw.HRESIZE_CURSOR,
              slikui.Cursor.ResizeV: glfw.VRESIZE_CURSOR}
    cursors = {}

    def on_cursor_shape(shape):
        kind = shapes.get(shape, glfw.ARROW_CURSOR)
        if kind not in cursors:
            cursors[kind] = glfw.create_standard_cursor(kind)
        glfw.set_cursor(win, cursors[kind])

    app.ctx.set_cursor_host(on_cursor_shape)
    def clipboard_text():
        raw = glfw.get_clipboard_string(win)  # bytes, or None when empty
        return raw.decode("utf-8", "replace") if raw else ""

    app.ctx.set_clipboard_source(clipboard_text)
    app.ctx.set_clipboard_sink(lambda text: glfw.set_clipboard_string(win, text))

    def on_char(_w, cp):
        app.handle(slikui.text_input(chr(cp)))

    glfw.set_mouse_button_callback(win, on_button)
    glfw.set_cursor_pos_callback(win, on_cursor)
    glfw.set_scroll_callback(win, on_scroll)
    glfw.set_key_callback(win, on_key)
    glfw.set_char_callback(win, on_char)
    glfw.set_window_focus_callback(win, on_focus)

    import time as _time
    last = _time.perf_counter()
    # An app closes its window by setting `app.quit = True` (e.g. a shortcut).
    while not glfw.window_should_close(win) and not getattr(app, "quit", False):
        glfw.poll_events()
        fw, fh = glfw.get_framebuffer_size(win)
        ww, wh = glfw.get_window_size(win)
        if (fw, fh) != (app.target.width, app.target.height):
            app.target.resize(fw, fh)
        ui_scale = fw / ww if ww > 0 else 1.0
        app.ctx.theme.scale = ui_scale
        app.sink.set_scale(ui_scale)
        app.ctx.set_frame_size(float(ww), float(wh))  # logical points

        now = _time.perf_counter()
        dt = min(now - last, 0.05)
        last = now
        app.tick(now, dt)

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
        glfw.swap_buffers(win)

    glfw.destroy_window(win)
    app.close()  # release UI callbacks while the interpreter is alive
    glfw.terminate()
    return 0
