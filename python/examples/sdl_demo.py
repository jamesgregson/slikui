#!/usr/bin/env python3
"""slikui over an SDL3 window (PySDL3 + PyOpenGL), UI routed against a scene.

The host owns the window, the GL context and the event loop. slikui's
SdlInput translates this window's events; each goes to the UI when the UI
wants it (ctx.wants_mouse / wants_keyboard) and to the "scene" otherwise.
The scene is just the clear color: click outside the panel to pick a hue,
scroll to change brightness. GlApp draws the UI over it on the GPU.

Run:  python python/examples/sdl_demo.py   (deps: pysdl3, PyOpenGL, numpy)

PySDL3 downloads its own SDL binaries unless told otherwise; to use a system
SDL3 (e.g. Homebrew), run with
SDL_DOWNLOAD_BINARIES=0 SDL_CHECK_VERSION=0 SDL_FIND_BINARIES=1 SDL_DISABLE_METADATA=1
"""

import argparse
import ctypes
import math
import time

import sdl3
from OpenGL import GL

import slikui
from slikui.sdl_input import is_pointer

KEY_EVENTS = (slikui.EventType.KeyPress, slikui.EventType.KeyRelease,
              slikui.EventType.TextInput)


def create_window(w, h, hidden):
    if not sdl3.SDL_Init(sdl3.SDL_INIT_VIDEO):
        raise SystemExit(f"SDL_Init failed: {sdl3.SDL_GetError()}")
    for attr, value in ((sdl3.SDL_GL_CONTEXT_MAJOR_VERSION, 3),
                        (sdl3.SDL_GL_CONTEXT_MINOR_VERSION, 3),
                        (sdl3.SDL_GL_CONTEXT_PROFILE_MASK, sdl3.SDL_GL_CONTEXT_PROFILE_CORE),
                        (sdl3.SDL_GL_CONTEXT_FLAGS, sdl3.SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG),
                        (sdl3.SDL_GL_DOUBLEBUFFER, 1)):
        sdl3.SDL_GL_SetAttribute(attr, value)
    flags = (sdl3.SDL_WINDOW_OPENGL | sdl3.SDL_WINDOW_RESIZABLE
             | sdl3.SDL_WINDOW_HIGH_PIXEL_DENSITY
             | (sdl3.SDL_WINDOW_HIDDEN if hidden else 0))
    win = sdl3.SDL_CreateWindow(b"slikui + SDL3 (python)", w, h, flags)
    if not win:
        raise SystemExit(f"SDL_CreateWindow failed: {sdl3.SDL_GetError()}")
    gl = sdl3.SDL_GL_CreateContext(win)
    if not gl:
        raise SystemExit(f"SDL_GL_CreateContext failed: {sdl3.SDL_GetError()}")
    sdl3.SDL_GL_MakeCurrent(win, gl)
    sdl3.SDL_GL_SetSwapInterval(1)
    return win, gl


def sizes(win):
    w, h, pw, ph = (ctypes.c_int() for _ in range(4))
    sdl3.SDL_GetWindowSize(win, ctypes.byref(w), ctypes.byref(h))
    sdl3.SDL_GetWindowSizeInPixels(win, ctypes.byref(pw), ctypes.byref(ph))
    return (w.value, h.value), (pw.value, ph.value)


def build_ui(app, scene):
    root = app.set_root(slikui.Row, padding=16)  # pure layout: pointer falls through
    root.add(slikui.Spacer)
    panel = root.add(slikui.Panel, padding=14, gap=8, radius=10,
                     background=slikui.Color(20, 22, 26, 220))
    panel.add(slikui.Label, "SDL3 backend", font_size=20)
    panel.add(slikui.Label, "Click or scroll outside this panel:")
    panel.add(slikui.Label, "the scene gets it, the UI doesn't.")
    scene["info"] = panel.add(slikui.Label, "scene clicks: 0")
    scene["slider"] = panel.add(slikui.Slider, 0.0, 1.0)
    scene["slider"].set_value(scene["brightness"], False)
    scene["slider"].on_change(lambda v: scene.__setitem__("brightness", v))
    panel.add(slikui.TextField).set_placeholder("type here (keys go to the UI)")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--frames", type=int, default=0, help="quit after N frames (smoke test)")
    ap.add_argument("--hidden", action="store_true", help="don't show the window")
    args = ap.parse_args(argv)

    win, gl = create_window(900, 600, args.hidden)
    (ww, wh), (fw, fh) = sizes(win)
    app = slikui.GlApp(ww, wh, scale=fw / ww if ww else 1.0)
    slikui.sdl_input.attach_services(app.ctx)
    inp = slikui.SdlInput(win)  # no target: we route the queue ourselves

    scene = {"hue": 0.6, "brightness": 0.25, "clicks": 0}
    build_ui(app, scene)

    ev = sdl3.SDL_Event()
    running, frame, last = True, 0, time.perf_counter()
    while running:
        # 1. Pump SDL ourselves; SdlInput translates this window's events.
        while sdl3.SDL_PollEvent(ctypes.byref(ev)):
            if ev.type in (sdl3.SDL_EVENT_QUIT, sdl3.SDL_EVENT_WINDOW_CLOSE_REQUESTED):
                running = False
            inp.process(ev)
        # 2. Route: UI where it wants the event, the scene otherwise.
        for e in inp.take():
            if is_pointer(e):
                to_ui = app.ctx.wants_mouse(e.pos.x, e.pos.y)
            elif e.type in KEY_EVENTS:
                to_ui = app.ctx.wants_keyboard()
            else:
                to_ui = True  # resize, focus loss
            if to_ui:
                app.handle(e)
            elif e.type == slikui.EventType.MousePress and e.button == int(slikui.MouseButton.MouseLeft):
                scene["hue"] = (e.pos.x / max(ww, 1)) % 1.0
                scene["clicks"] += 1
                scene["info"].set_text(f"scene clicks: {scene['clicks']}")
            elif e.type == slikui.EventType.Scroll:
                scene["brightness"] = min(1.0, max(0.0, scene["brightness"] + e.scroll.y * 0.03))
                scene["slider"].set_value(scene["brightness"], False)

        (ww, wh), (fw, fh) = sizes(win)
        app.set_scale(fw / ww if ww else 1.0)
        app.resize(ww, wh)
        now = time.perf_counter()
        app.tick(now, min(now - last, 0.05))
        last = now

        # 3. Scene, then the UI over it.
        GL.glViewport(0, 0, fw, fh)
        GL.glDisable(GL.GL_SCISSOR_TEST)
        b, h = scene["brightness"], scene["hue"]
        GL.glClearColor(*(b * (0.5 + 0.5 * math.cos(6.2831853 * (h + o))) for o in (0, .33, .67)), 1.0)
        GL.glClear(GL.GL_COLOR_BUFFER_BIT)
        app.render(clear=False)
        sdl3.SDL_GL_SwapWindow(win)

        frame += 1
        if args.frames and frame >= args.frames:
            running = False

    app.close()      # GL objects, while the context is alive
    inp.close()
    sdl3.SDL_GL_DestroyContext(gl)
    sdl3.SDL_DestroyWindow(win)
    sdl3.SDL_Quit()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
