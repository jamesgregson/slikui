#!/usr/bin/env python3
"""Render real frames of glfw_demo.py and capture per-pass images.

Maintenance tool: runs the demo's actual GL passes (sky, mesh, grid, UI blit)
in a live window, captures the framebuffer BETWEEN passes (before swap —
reading after swap sees an undefined buffer), prints GL errors and pixel
probes. Use it to diagnose compositing without a human watching the window.

Run:  .venv/bin/python python/examples/capture_check.py [outdir]
Then: python3 tools/ppm2png.py <outdir>/*.ppm ...
"""

import importlib.util
import sys
import time
from pathlib import Path

import glfw
import numpy as np
from OpenGL import GL

import slikui

HERE = Path(__file__).resolve().parent
_spec = importlib.util.spec_from_file_location("glfw_demo", HERE / "glfw_demo.py")
demo = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(demo)


def find_widget(root, cls):
    """Depth-first search for the first widget of `cls` (testing helper)."""
    if isinstance(root, cls):
        return root
    for child in root.children:
        hit = find_widget(child, cls)
        if hit is not None:
            return hit
    return None


def read_fb():
    fw, fh = glfw.get_framebuffer_size(win)
    buf = np.empty((fh, fw, 4), dtype=np.uint8)
    GL.glPixelStorei(GL.GL_PACK_ALIGNMENT, 1)
    GL.glReadPixels(0, 0, fw, fh, GL.GL_RGBA, GL.GL_UNSIGNED_BYTE, buf)
    return buf[::-1]  # GL rows are bottom-up


def save(tag):
    buf = read_fb()
    path = out / f"{tag}.ppm"
    with open(path, "wb") as f:
        h, w = buf.shape[:2]
        f.write(f"P6\n{w} {h}\n255\n".encode())
        f.write(buf[..., :3].tobytes())
    print(f"captured {path}")
    # probes: center (cube), top-middle (sky), panel area (UI)
    h, w = buf.shape[:2]
    for name, (px, py) in (("center", (w // 2, h // 2)), ("sky", (w // 2, h // 8)),
                           ("panel", (int(w * 0.87), int(h * 0.25)))):
        print(f"  {name:6s} rgb = {tuple(int(v) for v in buf[py, px, :3])}")


def check_gl(tag):
    err = GL.glGetError()
    if err != 0:
        print(f"GL ERROR after {tag}: 0x{err:04x}")


def main():
    global win, out
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "shots")
    out.mkdir(exist_ok=True)
    slikui.set_leak_warnings(False)

    glfw.init()
    glfw.window_hint(glfw.CONTEXT_VERSION_MAJOR, 3)
    glfw.window_hint(glfw.CONTEXT_VERSION_MINOR, 3)
    glfw.window_hint(glfw.OPENGL_PROFILE, glfw.OPENGL_CORE_PROFILE)
    try:
        glfw.window_hint(glfw.OPENGL_FORWARD_COMPAT, True)
    except Exception:
        pass
    win = glfw.create_window(960, 640, "slikui capture check", None, None)
    glfw.make_context_current(win)

    g = demo.setup_gl()
    state = demo.State()
    app = slikui.App(960, 640)
    panel = demo.build_ui(app, state)
    tex_size = [0, 0]

    last, now = time.perf_counter(), 0.0
    dropdown = find_widget(panel, slikui.Dropdown)

    def frame(n, tags=()):
        nonlocal last, now
        glfw.poll_events()
        fw, fh = glfw.get_framebuffer_size(win)
        ww, wh = glfw.get_window_size(win)
        if (fw, fh) != (app.target.width, app.target.height):
            app.target.resize(fw, fh)  # match the demo: HiDPI framebuffer
        ui_scale = fw / ww if ww else 1.0
        app.ctx.theme.scale = ui_scale
        app.sink.set_scale(ui_scale)
        app.ctx.set_frame_size(float(ww), float(wh))
        dt = min(time.perf_counter() - last, 0.05)
        last, now = time.perf_counter(), now + dt
        state.yaw += state.spin_speed * dt
        app.tick(now, dt)

        demo.draw_scene(g, state, fw, fh)
        check_gl("scene")
        if "scene" in tags:
            save(f"{n:02d}_scene")

        demo.draw_ui(g, app, tex_size)
        check_gl("ui")
        if "ui" in tags:
            save(f"{n:02d}_ui")
        glfw.swap_buffers(win)

    for i in range(30):  # settle: atlas uploads, auto-rotate advances
        frame(i)
    frame(30, tags=("scene", "ui"))

    # open the dropdown popup through the real event path
    b = dropdown.bounds
    pos = (b.x + b.w / 2, b.y + b.h / 2)
    app.handle(slikui.mouse_press(pos))
    app.handle(slikui.mouse_release(pos))
    for i in range(31, 40):
        frame(i)
    frame(40, tags=("ui",))
    app.handle(slikui.key_press(slikui.Key.Escape))
    for i in range(41, 50):
        frame(i)

    # diagnostic: cube-only pass on magenta — isolates the mesh draw path
    GL.glClearColor(1.0, 0.0, 1.0, 1.0)
    GL.glClear(GL.GL_COLOR_BUFFER_BIT | GL.GL_DEPTH_BUFFER_BIT)
    proj = demo.perspective(45 * demo.DEG2RAD, 960 / 640, 0.1, 100.0)
    GL.glUseProgram(g.mesh_prog)
    GL.glUniformMatrix4fv(g.mu["u_proj"], 1, GL.GL_FALSE, proj)
    GL.glUniformMatrix4fv(g.mu["u_view"], 1, GL.GL_FALSE, demo.look_at((0, 0, 5), (0, 0, 0)))
    GL.glUniformMatrix4fv(g.mu["u_model"], 1, GL.GL_FALSE, np.eye(4, dtype=np.float32))
    GL.glUniform1f(g.mu["u_unlit"], 1.0)  # pure a_color: white if fetch works
    GL.glVertexAttrib4f(2, 1.0, 1.0, 1.0, 1.0)
    GL.glDisable(GL.GL_DEPTH_TEST)
    GL.glDisable(GL.GL_BLEND)
    GL.glDisable(GL.GL_CULL_FACE)
    GL.glBindVertexArray(g.shapes[0]["vao"])
    GL.glDrawElements(GL.GL_TRIANGLES, g.shapes[0]["count"], GL.GL_UNSIGNED_SHORT, None)
    GL.glBindVertexArray(0)
    save("cube_only")

    glfw.destroy_window(win)
    glfw.terminate()


if __name__ == "__main__":
    main()
