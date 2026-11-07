#!/usr/bin/env python3
"""slikui cube viewer — Python-native GLFW + OpenGL host, slikui software UI.

The Python twin of examples/04_glfw_opengl: a lit cube/sphere/torus over a
procedural sky, with a slikui controls panel. The difference is who owns the
window: here GLFW and OpenGL are driven from Python (glfw + PyOpenGL), while
every UI pixel is rasterized on the CPU by slikui's software backend and
blitted as one texture:

    scene pass  — plain GL (sky + grid + shape)
    UI pass     — ctx.render(clear=False) into a uint32 Target, alpha keeps
                  whatever slikui painted (0 where untouched), uploaded as one
                  texture and blended over the scene
    input       — GLFW callbacks → slikui.Event for UI hits, camera ops else

Run:  python examples/glfw_demo.py     (deps: glfw, PyOpenGL, numpy)
"""

import math
import time
from types import SimpleNamespace

import glfw
import numpy as np
from OpenGL import GL

import slikui

DEG2RAD = math.pi / 180.0
K_MIN_DIST, K_MAX_DIST = 2.4, 10.0


# ---------------------------------------------------------------------------
# Shaders — one lit program for the shape, one procedural sky, one blit for
# the UI texture. Same GLSL as the C++ example.
# ---------------------------------------------------------------------------
MESH_VS = """
#version 330 core
layout(location=0) in vec3 a_pos;
layout(location=1) in vec3 a_normal;
layout(location=2) in vec4 a_color;
uniform mat4 u_proj, u_view, u_model;
out vec3 v_world;
out vec3 v_normal;
out vec4 v_color;
void main() {
    vec4 world = u_model * vec4(a_pos, 1.0);
    v_world = world.xyz;
    v_normal = mat3(u_model) * a_normal;
    v_color = a_color;
    gl_Position = u_proj * u_view * world;
}
"""

MESH_FS = """
#version 330 core
in vec3 v_world;
in vec3 v_normal;
in vec4 v_color;
uniform vec3 u_albedo, u_light_color, u_light_dir, u_cam_pos;
uniform float u_ambient, u_intensity, u_roughness, u_metallic, u_unlit;
out vec4 frag;
void main() {
    if (u_unlit > 0.5) { frag = v_color; return; }
    vec3 N = normalize(v_normal);
    vec3 L = normalize(u_light_dir);
    vec3 V = normalize(u_cam_pos - v_world);
    vec3 H = normalize(L + V);
    float ndl = max(dot(N, L), 0.0);
    float shine = mix(8.0, 160.0, pow(1.0 - u_roughness, 2.0));
    float spec = pow(max(dot(N, H), 0.0), shine) * mix(1.0, 0.25, u_roughness);
    vec3 diff = u_albedo * (1.0 - u_metallic);
    vec3 spec_col = mix(vec3(0.04), u_albedo, u_metallic);
    vec3 col = u_ambient * u_albedo
             + u_light_color * u_intensity * (diff * ndl + spec_col * spec);
    frag = vec4(col, 1.0);
}
"""

SKY_VS = """
#version 330 core
out vec2 v_ndc;
void main() {
    // Fullscreen triangle from gl_VertexID: (-1,-1), (3,-1), (-1,3).
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    v_ndc = p * 2.0 - 1.0;
    gl_Position = vec4(v_ndc, 0.0, 1.0);
}
"""

SKY_FS = """
#version 330 core
in vec2 v_ndc;
uniform vec3 u_zenith, u_horizon, u_ground, u_sun_color, u_sun_dir;
uniform vec3 u_cam_fwd, u_cam_right, u_cam_up;
uniform float u_tan_half_fov, u_aspect, u_haze, u_sun_glow;
out vec4 frag;
void main() {
    vec3 dir = normalize(u_cam_fwd + v_ndc.x * u_tan_half_fov * u_aspect * u_cam_right
                               + v_ndc.y * u_tan_half_fov * u_cam_up);
    float t = clamp(dir.y, -1.0, 1.0);
    vec3 sky = mix(u_horizon, u_zenith, pow(max(t, 0.0), 0.55));
    vec3 gnd = u_ground * mix(0.45, 1.15, clamp(-t * 2.5, 0.0, 1.0));
    vec3 col = t >= 0.0 ? sky : gnd;
    // Haze: exponential band that tightens toward the horizon.
    float h = exp(-abs(t) * mix(16.0, 3.5, u_haze));
    vec3 haze_col = mix(u_horizon, vec3(0.60, 0.64, 0.70), 0.4 * u_haze);
    col = mix(col, haze_col, h * u_haze);
    // Sun disc + wide glow along the light direction.
    float sd = max(dot(dir, normalize(u_sun_dir)), 0.0);
    col += u_sun_color * (smoothstep(0.9993, 0.9998, sd) * 3.0 + pow(sd, 180.0) * 0.45)
         * u_sun_glow;
    frag = vec4(col, 1.0);
}
"""

BLIT_VS = """
#version 330 core
layout(location=0) in vec2 a_pos;
layout(location=1) in vec2 a_uv;
out vec2 v_uv;
void main(){ v_uv=a_uv; gl_Position=vec4(a_pos,0,1); }
"""

BLIT_FS = """
#version 330 core
in vec2 v_uv;
uniform sampler2D u_tex;
out vec4 frag;
void main(){ frag = texture(u_tex, v_uv); }  // straight alpha: blended over the scene
"""


# ---------------------------------------------------------------------------
# Tiny column-major mat4 (flat 16-float arrays, same layout as the C++ demo).
# ---------------------------------------------------------------------------
def perspective(fov_y, aspect, zn, zf):
    f = 1.0 / math.tan(fov_y * 0.5)
    m = [0.0] * 16
    m[0], m[5] = f / aspect, f
    m[10] = (zf + zn) / (zn - zf)
    m[11] = -1.0
    m[14] = 2.0 * zf * zn / (zn - zf)
    return np.array(m, dtype=np.float32)


def look_at(eye, target):
    z = np.array(eye, dtype=np.float32) - np.array(target, dtype=np.float32)
    z /= max(np.linalg.norm(z), 1e-6)
    x = np.array([z[2], 0.0, -z[0]], dtype=np.float32)
    x /= max(np.linalg.norm(x), 1e-6)
    y = np.cross(z, x)
    m = np.zeros(16, dtype=np.float32)
    m[0], m[4], m[8] = x
    m[1], m[5], m[9] = y
    m[2], m[6], m[10] = z
    m[12] = -float(np.dot(x, eye))
    m[13] = -float(np.dot(y, eye))
    m[14] = -float(np.dot(z, eye))
    m[15] = 1.0  # w row — C++ Mat4 identity-initializes this; zeros would
                 # make clip.w = 0 for every vertex (NaN after the divide)
    return m


# ---------------------------------------------------------------------------
# Geometry: cube / UV sphere / torus with analytic normals, plus a fading
# floor grid. numpy builds the buffers; upload_mesh hands them to GL.
# ---------------------------------------------------------------------------
def fix_winding(idx, verts):
    """Orient each triangle so its geometric normal agrees with the average
    vertex normal (vectorized port of the C++ post-pass). `verts` may be
    (N, 6) or a grid-shaped array; both are contiguous, so row order matches
    the flat vertex ids used by the index buffer."""
    verts = verts.reshape(-1, 6)
    t = idx.reshape(-1, 3)
    a, b, c = t[:, 0], t[:, 1], t[:, 2]
    e1 = verts[b, :3] - verts[a, :3]
    e2 = verts[c, :3] - verts[a, :3]
    g = np.cross(e1, e2)
    d = (verts[a, 3:] + verts[b, 3:] + verts[c, 3:]) / 3.0
    swap = (g * d).sum(axis=1) < 0
    tmp = t[swap, 2].copy()
    t[swap, 2] = t[swap, 1]
    t[swap, 1] = tmp
    return t.reshape(-1)


def make_cube():
    s, v, idx = 1.0, [], []
    for axis in range(3):
        u, w = (axis + 1) % 3, (axis + 2) % 3
        for sign in (0, 1):
            n = [0.0, 0.0, 0.0]
            n[axis] = 1.0 if sign else -1.0
            first = len(v)  # v holds one 6-float entry per vertex (C++: size()/6)
            for corner in range(4):
                p = [0.0, 0.0, 0.0]
                p[axis] = n[axis] * s
                p[u] = s if corner & 1 else -s
                p[w] = s if corner & 2 else -s
                v.append(p + n)
            # Corners are in Z-order; split along the c0-c3 diagonal so the
            # two triangles cover the face (the c1-c2 split leaves a hole).
            quads = [(0, 1, 3), (0, 3, 2)]
            for a, b, c in quads:
                A, B, C = (np.array(v[first + k][:3]) for k in (a, b, c))
                cr = np.cross(B - A, C - A)
                if float(np.dot(cr, n)) >= 0:
                    idx += [first + a, first + b, first + c]
                else:
                    idx += [first + a, first + c, first + b]
    verts = np.array(v, dtype=np.float32)
    return verts, np.array(idx, dtype=np.uint16)


def make_sphere(radius=1.0, sectors=36, rings=22):
    phi = np.linspace(0.0, math.pi, rings + 1)
    th = np.linspace(0.0, 2.0 * math.pi, sectors + 1)
    p, t = np.meshgrid(phi, th, indexing="ij")
    n = np.stack([np.sin(p) * np.cos(t), np.cos(p), np.sin(p) * np.sin(t)], axis=-1)
    verts = np.concatenate([n * radius, n], axis=-1).astype(np.float32)
    j, i = np.meshgrid(np.arange(rings), np.arange(sectors), indexing="ij")
    at = lambda jj, ii: (jj * (sectors + 1) + ii)
    quads = np.stack([at(j, i), at(j + 1, i), at(j, i + 1),
                      at(j, i + 1), at(j + 1, i), at(j + 1, i + 1)], axis=-1)
    idx = fix_winding(quads.ravel(), verts)
    return verts, idx.astype(np.uint16)


def make_torus(R=0.8, r=0.35, u_segs=44, v_segs=22):
    u = np.linspace(0.0, 2.0 * math.pi, u_segs + 1)
    t = np.linspace(0.0, 2.0 * math.pi, v_segs + 1)
    U, T = np.meshgrid(u, t, indexing="ij")
    ct, st = np.cos(T), np.sin(T)
    cu, su = np.cos(U), np.sin(U)
    n = np.stack([ct * cu, st, ct * su], axis=-1)
    ring = R + r * ct
    pos = np.stack([ring * cu, r * st, ring * su], axis=-1)
    verts = np.concatenate([pos, n], axis=-1).astype(np.float32)
    i, j = np.meshgrid(np.arange(u_segs), np.arange(v_segs), indexing="ij")
    at = lambda ii, jj: (ii * (v_segs + 1) + jj)
    quads = np.stack([at(i, j), at(i + 1, j), at(i, j + 1),
                      at(i, j + 1), at(i + 1, j), at(i + 1, j + 1)], axis=-1)
    idx = fix_winding(quads.ravel(), verts)
    return verts, idx.astype(np.uint16)


def make_grid():
    y, half, step = -1.25, 3.0, 0.5
    lines = []
    for k in range(-6, 7):
        p = k * step
        a = 0.45 * (1.0 - abs(p) / half) + 0.06
        lines += [[p, y, -half, .62, .68, .75, a], [p, y, half, .62, .68, .75, a],
                  [-half, y, p, .62, .68, .75, a], [half, y, p, .62, .68, .75, a]]
    return np.array(lines, dtype=np.float32)


def upload_mesh(verts, idx=None):
    """verts: (N, 6) float32 [pos|normal] for indexed shapes, or (N, 7)
    [pos|color] for the grid. One VBO per attribute — PyOpenGL's
    glVertexAttribPointer only accepts None (offset 0) pointers on this
    stack, so interleaved layouts with non-zero offsets silently break
    vertex fetch."""
    vao = GL.glGenVertexArrays(1)
    GL.glBindVertexArray(vao)
    pos = np.ascontiguousarray(verts[:, :3], dtype=np.float32)
    pb = GL.glGenBuffers(1)
    GL.glBindBuffer(GL.GL_ARRAY_BUFFER, pb)
    GL.glBufferData(GL.GL_ARRAY_BUFFER, pos.nbytes, pos, GL.GL_STATIC_DRAW)
    GL.glEnableVertexAttribArray(0)
    GL.glVertexAttribPointer(0, 3, GL.GL_FLOAT, GL.GL_FALSE, 12, None)
    if idx is not None:  # pos + normal (shape)
        nrm = np.ascontiguousarray(verts[:, 3:], dtype=np.float32)
        nb_ = GL.glGenBuffers(1)
        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, nb_)
        GL.glBufferData(GL.GL_ARRAY_BUFFER, nrm.nbytes, nrm, GL.GL_STATIC_DRAW)
        GL.glEnableVertexAttribArray(1)
        GL.glVertexAttribPointer(1, 3, GL.GL_FLOAT, GL.GL_FALSE, 12, None)
        ib = GL.glGenBuffers(1)
        GL.glBindBuffer(GL.GL_ELEMENT_ARRAY_BUFFER, ib)
        GL.glBufferData(GL.GL_ELEMENT_ARRAY_BUFFER, idx.nbytes, idx, GL.GL_STATIC_DRAW)
        count, mode, ibo = int(idx.size), GL.GL_TRIANGLES, ib
    else:  # pos + color (grid lines)
        col = np.ascontiguousarray(verts[:, 3:], dtype=np.float32)
        cb = GL.glGenBuffers(1)
        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, cb)
        GL.glBufferData(GL.GL_ARRAY_BUFFER, col.nbytes, col, GL.GL_STATIC_DRAW)
        GL.glEnableVertexAttribArray(2)
        GL.glVertexAttribPointer(2, 4, GL.GL_FLOAT, GL.GL_FALSE, 16, None)
        count, mode, ibo = verts.shape[0], GL.GL_LINES, None
    GL.glBindVertexArray(0)
    return {"vao": vao, "ibo": ibo, "count": count, "mode": mode}


def compile_shader(kind, src):
    sh = GL.glCreateShader(kind)
    GL.glShaderSource(sh, src)
    GL.glCompileShader(sh)
    if not GL.glGetShaderiv(sh, GL.GL_COMPILE_STATUS):
        raise RuntimeError(GL.glGetShaderInfoLog(sh).decode())
    return sh


def build_program(vs_src, fs_src):
    prog = GL.glCreateProgram()
    for kind, src in ((GL.GL_VERTEX_SHADER, vs_src), (GL.GL_FRAGMENT_SHADER, fs_src)):
        sh = compile_shader(kind, src)
        GL.glAttachShader(prog, sh)
        GL.glDeleteShader(sh)
    GL.glLinkProgram(prog)
    if not GL.glGetProgramiv(prog, GL.GL_LINK_STATUS):
        raise RuntimeError(GL.glGetProgramInfoLog(prog).decode())
    return prog


def uniforms(prog, *names):
    return {n: GL.glGetUniformLocation(prog, n) for n in names}


# ---------------------------------------------------------------------------
# App state — everything the panel edits, plus the camera.
# ---------------------------------------------------------------------------
class State:
    shape = 0  # 0 cube, 1 sphere, 2 torus
    albedo = [0.75, 0.36, 0.22]
    roughness = 0.35
    metallic = 0.15
    sky_zenith = [0.16, 0.32, 0.62]
    sky_horizon = [0.62, 0.74, 0.88]
    ground = [0.20, 0.19, 0.17]
    haze = 0.35
    sun_glow = 0.6
    light_color = (1.0, 0.96, 0.90)
    light_intensity = 1.6
    ambient = 0.22
    light_azimuth = 45.0
    light_elevation = 55.0
    auto_rotate = True
    spin_speed = 0.6
    yaw, pitch, distance = 0.6, 0.5, 4.6

    def reset_camera(self):
        self.yaw, self.pitch, self.distance = 0.6, 0.5, 4.6


SKY_PRESETS = {  # zenith, horizon, ground
    "Day": ((0.16, 0.32, 0.62), (0.62, 0.74, 0.88), (0.20, 0.19, 0.17)),
    "Sunset": ((0.16, 0.18, 0.36), (0.94, 0.55, 0.28), (0.14, 0.11, 0.10)),
    "Night": ((0.02, 0.03, 0.08), (0.08, 0.10, 0.18), (0.05, 0.05, 0.06)),
    "Overcast": ((0.45, 0.49, 0.55), (0.72, 0.75, 0.80), (0.30, 0.30, 0.29)),
    "Dust": ((0.55, 0.44, 0.30), (0.85, 0.70, 0.50), (0.35, 0.28, 0.20)),
}


def vec3_close(target, color, tol=1.5 / 255.0):
    return all(abs(t - c / 255.0) < tol for t, c in zip(target, (color.r, color.g, color.b)))


def set_vec3(target, color):
    target[:] = (color.r / 255.0, color.g / 255.0, color.b / 255.0)


def add_slider(col, label, lo, hi, init, fmt, on_change):
    """Label + live value on one row, slider underneath (mirrors the C++ helper)."""
    row = col.add(slikui.Row, gap=8)
    row.add(slikui.Label, label)
    row.add(slikui.Spacer)
    val = row.add(slikui.Label, fmt % init, color=slikui.Color.from_hex(0x8A919C))
    s = col.add(slikui.Slider, lo, hi)
    s.set_value(init, False)
    s.on_change(lambda v: (val.set_text(fmt % v), on_change(v)))
    return s


def add_swatches(col, target, colors):
    """Row of Swatch chips editing one vec3; the matching chip shows a ring.

    Ctor args go positionally: `add(cls, *ctor_args)` routes positionals to
    the bound factory and kwargs to setters (Swatch's callables are ctor
    args, not setters).
    """
    row = col.add(slikui.Row, gap=6)
    for c in colors:  # hex ints or Color instances both work
        c = c if isinstance(c, slikui.Color) else slikui.Color.from_hex(c)
        row.add(slikui.Swatch, c, lambda c=c, t=target: vec3_close(t, c),
                lambda c=c: set_vec3(target, c))


# ---------------------------------------------------------------------------
# slikui UI: docked panel (top-right) + hint bar (bottom-left) via spacers.
# ---------------------------------------------------------------------------
def build_ui(app, state):
    # Docking without a custom root widget: a full-window Row holds
    #   [hint column | expanding spacer | controls panel]
    # The hint column hugs the left at natural width; its own expanding
    # Spacer pushes the hint panel to the bottom (mirrors the C++
    # CubeViewport::arrange dock). The panel keeps hint_h=1e5 so its
    # ScrollArea always fills the window height.
    root = app.set_root(slikui.Row, padding=14, gap=0)

    left = root.add(slikui.Column, gap=0)
    left.add(slikui.Spacer)  # expands vertically: hint hugs the bottom-left
    hint = left.add(slikui.Panel, padding=10, background=slikui.Color(0, 0, 0, 120),
                    radius=8)
    hint.id = "hint"  # findable for tests/capture
    hint.add(slikui.Label, "drag: orbit | scroll: zoom",
             color=slikui.Color(255, 255, 255, 160))

    root.add(slikui.Spacer)  # expands horizontally: panel hugs the top-right
    panel = root.add(slikui.Panel, padding=14, gap=10,
                     background=slikui.Color(0, 0, 0, 185),
                     border=slikui.Color(255, 255, 255, 30), radius=10)
    panel.draw_border = True

    scroll = panel.add(slikui.ScrollArea)
    scroll.hint_h = 1e5  # always fill the panel: it tracks the window size
    scroll.content.set_gap(10)
    scroll.add(slikui.Label, "slikui cube viewer").set_font_size(18)

    acc = scroll.add(slikui.Accordion, gap=6)
    acc.set_exclusive(False)

    sec = acc.add_section("Shape", True)
    sec.set_gap(8)
    sec.add(slikui.Dropdown, items=["Cube", "Sphere", "Torus"],
            placeholder="Reference shape").on_select(
        lambda i: setattr(state, "shape", i))

    sec = acc.add_section("Material")
    sec.set_gap(8)
    add_swatches(sec, state.albedo, [0xBF5C38, 0x4E9E58, 0x3F6FBF, 0xC9C2B5, 0xE8E8E8])
    add_slider(sec, "Roughness", 0.05, 1.0, state.roughness, "%.2f",
               lambda v: setattr(state, "roughness", v))
    add_slider(sec, "Metallic", 0.0, 1.0, state.metallic, "%.2f",
               lambda v: setattr(state, "metallic", v))

    sec = acc.add_section("Environment")
    sec.set_gap(8)
    sec.add(slikui.Dropdown, items=list(SKY_PRESETS),
            placeholder="Sky preset").on_select(lambda i: apply_sky(state, i))
    sec.add(slikui.Label, "Sky zenith", color=slikui.Color.from_hex(0x8A919C))
    add_swatches(sec, state.sky_zenith, [0x29529E, 0x292E5C, 0x737D8C, 0x8C704D, 0x050814])
    sec.add(slikui.Label, "Horizon", color=slikui.Color.from_hex(0x8A919C))
    add_swatches(sec, state.sky_horizon, [0x9EBDE0, 0xF08C47, 0xB8BFCC, 0xD9B380, 0x141A2E])
    sec.add(slikui.Label, "Ground", color=slikui.Color.from_hex(0x8A919C))
    add_swatches(sec, state.ground, [0x33312B, 0x241C1A, 0x4D4D4A, 0x594733, 0x0D0D0F])
    add_slider(sec, "Haze", 0.0, 1.0, state.haze, "%.2f", lambda v: setattr(state, "haze", v))
    add_slider(sec, "Sun glow", 0.0, 1.0, state.sun_glow, "%.2f",
               lambda v: setattr(state, "sun_glow", v))

    sec = acc.add_section("Light")
    sec.set_gap(8)
    sec.add(slikui.Label, "Color", color=slikui.Color.from_hex(0x8A919C))
    add_swatches(sec, state.light_color, [0xFFF5E8, 0xFFFFFF, 0xB8D8FF, 0xE8A0FF])
    add_slider(sec, "Intensity", 0.0, 4.0, state.light_intensity, "%.2f",
               lambda v: setattr(state, "light_intensity", v))
    add_slider(sec, "Elevation", -85.0, 85.0, state.light_elevation, "%.0f deg",
               lambda v: setattr(state, "light_elevation", v))
    add_slider(sec, "Azimuth", -180.0, 180.0, state.light_azimuth, "%.0f deg",
               lambda v: setattr(state, "light_azimuth", v))
    add_slider(sec, "Ambient", 0.0, 1.0, state.ambient, "%.2f",
               lambda v: setattr(state, "ambient", v))

    sec = acc.add_section("Camera")
    sec.set_gap(8)
    cb = sec.add(slikui.Checkbox, "Auto-rotate", state.auto_rotate)
    cb.on_change(lambda v: setattr(state, "auto_rotate", v))
    add_slider(sec, "Spin speed", 0.0, 2.0, state.spin_speed, "%.2f rad/s",
               lambda v: setattr(state, "spin_speed", v))
    sec.add(slikui.Button, "Reset camera").on_click(lambda: state.reset_camera())

    return panel


def apply_sky(state, i):
    z, h, g = list(SKY_PRESETS.values())[i]
    state.sky_zenith, state.sky_horizon, state.ground = list(z), list(h), list(g)


def over_ui(app, panel, x, y):
    """True when a point (logical) is over the panel or any open popup."""
    if panel.bounds.contains(slikui.Vec2(x, y)):
        return True
    return any(app.ctx.popup(i).bounds.contains(slikui.Vec2(x, y))
               for i in range(app.ctx.popup_count))


# ---------------------------------------------------------------------------
# GL setup
# ---------------------------------------------------------------------------


def setup_gl():
    g = SimpleNamespace()
    g.mesh_prog = build_program(MESH_VS, MESH_FS)
    g.mu = uniforms(g.mesh_prog, "u_proj", "u_view", "u_model", "u_albedo",
                    "u_light_color", "u_light_dir", "u_cam_pos", "u_ambient",
                    "u_intensity", "u_roughness", "u_metallic", "u_unlit")
    g.sky_prog = build_program(SKY_VS, SKY_FS)
    g.su = uniforms(g.sky_prog, "u_zenith", "u_horizon", "u_ground", "u_sun_color",
                    "u_sun_dir", "u_cam_fwd", "u_cam_right", "u_cam_up",
                    "u_tan_half_fov", "u_aspect", "u_haze", "u_sun_glow")
    g.blit_prog = build_program(BLIT_VS, BLIT_FS)
    g.bu = uniforms(g.blit_prog, "u_tex")

    g.shapes = [upload_mesh(*make_cube()), upload_mesh(*make_sphere()),
                upload_mesh(*make_torus())]
    g.grid = upload_mesh(make_grid())

    # Blit quad: separate pos/uv VBOs (see upload_mesh — None-offset rule);
    # v=0 is the top row of the CPU pixels.
    g.blit_vao = GL.glGenVertexArrays(1)
    GL.glBindVertexArray(g.blit_vao)
    quad_pos = np.array([-1, 1, 1, 1, -1, -1, 1, 1, 1, -1, -1, -1], dtype=np.float32)
    quad_uv = np.array([0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 0, 1], dtype=np.float32)
    pb = GL.glGenBuffers(1)
    GL.glBindBuffer(GL.GL_ARRAY_BUFFER, pb)
    GL.glBufferData(GL.GL_ARRAY_BUFFER, quad_pos.nbytes, quad_pos, GL.GL_STATIC_DRAW)
    GL.glEnableVertexAttribArray(0)
    GL.glVertexAttribPointer(0, 2, GL.GL_FLOAT, GL.GL_FALSE, 8, None)
    ub = GL.glGenBuffers(1)
    GL.glBindBuffer(GL.GL_ARRAY_BUFFER, ub)
    GL.glBufferData(GL.GL_ARRAY_BUFFER, quad_uv.nbytes, quad_uv, GL.GL_STATIC_DRAW)
    GL.glEnableVertexAttribArray(1)
    GL.glVertexAttribPointer(1, 2, GL.GL_FLOAT, GL.GL_FALSE, 8, None)

    g.sky_vao = GL.glGenVertexArrays(1)  # empty: sky reads only gl_VertexID

    g.ui_tex = GL.glGenTextures(1)
    GL.glBindTexture(GL.GL_TEXTURE_2D, g.ui_tex)
    GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_MIN_FILTER, GL.GL_LINEAR)
    GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_MAG_FILTER, GL.GL_LINEAR)
    GL.glBindVertexArray(0)
    return g


def draw_scene(g, state, fw, fh):
    aspect = fw / float(fh) if fh > 0 else 1.0
    proj = perspective(45.0 * DEG2RAD, aspect, 0.1, 100.0)
    cp, sp = math.cos(state.pitch), math.sin(state.pitch)
    cy, sy = math.cos(state.yaw), math.sin(state.yaw)
    eye = (state.distance * cp * sy, state.distance * sp, state.distance * cp * cy)
    target = (0.0, -0.15, 0.0)
    view = look_at(eye, target)

    fwd = np.array(target) - np.array(eye)
    fwd /= np.linalg.norm(fwd)
    right = np.array([-fwd[2], 0.0, fwd[0]])
    right /= np.linalg.norm(right)
    up = np.cross(right, fwd)

    az, el = state.light_azimuth * DEG2RAD, state.light_elevation * DEG2RAD
    L = (math.cos(el) * math.sin(az), math.sin(el), math.cos(el) * math.cos(az))

    GL.glViewport(0, 0, fw, fh)
    GL.glDisable(GL.GL_SCISSOR_TEST)
    GL.glDisable(GL.GL_BLEND)
    GL.glDepthMask(GL.GL_TRUE)
    GL.glDisable(GL.GL_DEPTH_TEST)
    GL.glClearColor(*state.sky_horizon, 1.0)
    GL.glClear(GL.GL_COLOR_BUFFER_BIT | GL.GL_DEPTH_BUFFER_BIT)
    GL.glDepthMask(GL.GL_FALSE)

    # --- sky pass: fullscreen triangle under everything ---
    GL.glUseProgram(g.sky_prog)
    GL.glBindVertexArray(g.sky_vao)
    for name, val in (("u_zenith", state.sky_zenith), ("u_horizon", state.sky_horizon),
                      ("u_ground", state.ground), ("u_sun_color", state.light_color),
                      ("u_sun_dir", L), ("u_cam_fwd", fwd), ("u_cam_right", right),
                      ("u_cam_up", up)):
        GL.glUniform3f(g.su[name], *val)
    GL.glUniform1f(g.su["u_tan_half_fov"], math.tan(0.5 * 45.0 * DEG2RAD))
    GL.glUniform1f(g.su["u_aspect"], aspect)
    GL.glUniform1f(g.su["u_haze"], state.haze)
    GL.glUniform1f(g.su["u_sun_glow"], state.sun_glow)
    GL.glDrawArrays(GL.GL_TRIANGLES, 0, 3)
    GL.glDepthMask(GL.GL_TRUE)

    GL.glUseProgram(g.mesh_prog)
    GL.glUniformMatrix4fv(g.mu["u_proj"], 1, GL.GL_FALSE, proj)
    GL.glUniformMatrix4fv(g.mu["u_view"], 1, GL.GL_FALSE, view)
    GL.glUniformMatrix4fv(g.mu["u_model"], 1, GL.GL_FALSE,
                          np.eye(4, dtype=np.float32))
    GL.glUniform3f(g.mu["u_cam_pos"], *eye)
    GL.glUniform3f(g.mu["u_light_dir"], *L)
    GL.glUniform3f(g.mu["u_light_color"], *state.light_color)
    GL.glUniform3f(g.mu["u_albedo"], *state.albedo)
    GL.glUniform1f(g.mu["u_intensity"], state.light_intensity)
    GL.glUniform1f(g.mu["u_ambient"], state.ambient)
    GL.glUniform1f(g.mu["u_roughness"], state.roughness)
    GL.glUniform1f(g.mu["u_metallic"], state.metallic)

    # Grid: unlit + blended, below the shape in depth.
    GL.glUniform1f(g.mu["u_unlit"], 1.0)
    GL.glVertexAttrib3f(1, 0.0, 1.0, 0.0)  # a_normal constant for grid verts
    GL.glEnable(GL.GL_DEPTH_TEST)
    GL.glEnable(GL.GL_BLEND)
    GL.glBlendFunc(GL.GL_SRC_ALPHA, GL.GL_ONE_MINUS_SRC_ALPHA)
    GL.glBindVertexArray(g.grid["vao"])
    GL.glDrawArrays(g.grid["mode"], 0, g.grid["count"])

    # Shape: opaque, lit, back-face culled.
    shape = g.shapes[state.shape]
    GL.glUniform1f(g.mu["u_unlit"], 0.0)
    GL.glVertexAttrib4f(2, 1.0, 1.0, 1.0, 1.0)  # a_color constant for shape verts
    GL.glDisable(GL.GL_BLEND)
    GL.glEnable(GL.GL_CULL_FACE)
    GL.glCullFace(GL.GL_BACK)
    GL.glBindVertexArray(shape["vao"])
    GL.glDrawElements(GL.GL_TRIANGLES, shape["count"], GL.GL_UNSIGNED_SHORT, None)
    GL.glDisable(GL.GL_CULL_FACE)
    GL.glDisable(GL.GL_DEPTH_TEST)
    GL.glBindVertexArray(0)


def draw_ui(g, app, tex_size):
    """Rasterize the UI on the CPU and blend it over the scene."""
    pix = app.render(clear=False)  # zero-copy (H, W) uint32; alpha 0 where bare
    h, w = pix.shape
    GL.glEnable(GL.GL_BLEND)
    GL.glBlendFunc(GL.GL_SRC_ALPHA, GL.GL_ONE_MINUS_SRC_ALPHA)
    GL.glDisable(GL.GL_DEPTH_TEST)
    GL.glActiveTexture(GL.GL_TEXTURE0)
    GL.glBindTexture(GL.GL_TEXTURE_2D, g.ui_tex)
    if tex_size[0] != w or tex_size[1] != h:  # (re)allocate storage...
        GL.glTexImage2D(GL.GL_TEXTURE_2D, 0, GL.GL_RGBA, w, h, 0, GL.GL_RGBA,
                        GL.GL_UNSIGNED_BYTE, None)
        tex_size[:] = [w, h]
    # ...and upload every frame — an allocate-only frame would show undefined
    # texture contents (and glTexImage2D with data=None does not upload).
    GL.glTexSubImage2D(GL.GL_TEXTURE_2D, 0, 0, 0, w, h, GL.GL_RGBA,
                       GL.GL_UNSIGNED_BYTE, pix.view(np.uint8))
    GL.glUseProgram(g.blit_prog)
    GL.glUniform1i(g.bu["u_tex"], 0)
    GL.glBindVertexArray(g.blit_vao)
    GL.glDrawArrays(GL.GL_TRIANGLES, 0, 6)
    GL.glDisable(GL.GL_BLEND)
    GL.glBindVertexArray(0)


# GLFW keys we forward to slikui (popup navigation, Escape, ...).
KEY_MAP = {
    glfw.KEY_ESCAPE: slikui.Key.Escape, glfw.KEY_ENTER: slikui.Key.Enter,
    glfw.KEY_TAB: slikui.Key.Tab, glfw.KEY_BACKSPACE: slikui.Key.Backspace,
    glfw.KEY_DELETE: slikui.Key.Delete, glfw.KEY_SPACE: slikui.Key.Space,
    glfw.KEY_LEFT: slikui.Key.Left, glfw.KEY_RIGHT: slikui.Key.Right,
    glfw.KEY_UP: slikui.Key.Up, glfw.KEY_DOWN: slikui.Key.Down,
    glfw.KEY_HOME: slikui.Key.Home, glfw.KEY_END: slikui.Key.End,
    glfw.KEY_PAGE_UP: slikui.Key.PageUp, glfw.KEY_PAGE_DOWN: slikui.Key.PageDown,
}
BUTTON_MAP = {  # GLFW: L=0 R=1 M=2 — slikui: L=0 M=1 R=2
    glfw.MOUSE_BUTTON_LEFT: slikui.MouseButton.MouseLeft,
    glfw.MOUSE_BUTTON_MIDDLE: slikui.MouseButton.MouseMiddle,
    glfw.MOUSE_BUTTON_RIGHT: slikui.MouseButton.MouseRight,
}


def main():
    slikui.set_leak_warnings(False)  # teardown-order noise, not a real leak
    if not glfw.init():
        raise SystemExit("glfw.init failed")
    glfw.window_hint(glfw.CONTEXT_VERSION_MAJOR, 3)
    glfw.window_hint(glfw.CONTEXT_VERSION_MINOR, 3)
    glfw.window_hint(glfw.OPENGL_PROFILE, glfw.OPENGL_CORE_PROFILE)
    try:  # macOS needs forward compatibility for 3.3 core
        glfw.window_hint(glfw.OPENGL_FORWARD_COMPAT, True)
    except Exception:
        pass
    win = glfw.create_window(960, 640, "slikui cube viewer (python)", None, None)
    if not win:
        glfw.terminate()
        raise SystemExit("could not create GLFW window")
    glfw.make_context_current(win)
    glfw.set_window_size_limits(win, 560, 380, glfw.DONT_CARE, glfw.DONT_CARE)

    g = setup_gl()
    state = State()
    app = slikui.App(960, 640)
    panel = build_ui(app, state)

    # --- input routing state (slikui owns UI hits, the app owns the camera) ---
    ctl = {"dragging": False, "last": (0.0, 0.0)}

    def pt():
        return glfw.get_cursor_pos(win)

    def on_button(_w, button, action, _mods):
        x, y = pt()
        ui = over_ui(app, panel, x, y)
        if action == glfw.PRESS:
            if ui:
                if button in BUTTON_MAP:
                    app.handle(slikui.mouse_press((x, y), BUTTON_MAP[button]))
            elif button == glfw.MOUSE_BUTTON_LEFT:
                ctl["dragging"], ctl["last"] = True, (x, y)
        elif action == glfw.RELEASE:
            if ctl["dragging"]:  # a drag owns the mouse until it ends
                ctl["dragging"] = False
            elif button in BUTTON_MAP:
                # Always deliver releases that slikui saw a press for (e.g. a
                # slider press that slid off the panel) — slikui ignores
                # releases it did not pair.
                app.handle(slikui.mouse_release((x, y), BUTTON_MAP[button]))

    def on_cursor(_w, x, y):
        if ctl["dragging"]:
            lx, ly = ctl["last"]
            state.yaw -= (x - lx) * 0.008
            state.pitch = max(-1.45, min(1.45, state.pitch + (y - ly) * 0.008))
            ctl["last"] = (x, y)
        else:
            app.handle(slikui.mouse_move((x, y)))

    def on_scroll(_w, xoff, yoff):
        x, y = pt()
        if over_ui(app, panel, x, y):
            app.handle(slikui.scroll((x, y), (xoff, yoff)))
        else:
            state.distance = max(K_MIN_DIST,
                                 min(K_MAX_DIST, state.distance * (1.0 - 0.12 * yoff)))

    def on_key(_w, key, _sc, action, mods):
        if key in KEY_MAP and action in (glfw.PRESS, glfw.RELEASE):
            e = slikui.key_press(KEY_MAP[key], mods) if action == glfw.PRESS \
                else slikui.key_release(KEY_MAP[key], mods)
            app.handle(e)

    glfw.set_mouse_button_callback(win, on_button)
    glfw.set_cursor_pos_callback(win, on_cursor)
    glfw.set_scroll_callback(win, on_scroll)
    glfw.set_key_callback(win, on_key)

    tex_size = [0, 0]
    last, now = time.perf_counter(), 0.0
    while not glfw.window_should_close(win):
        glfw.poll_events()
        fw, fh = glfw.get_framebuffer_size(win)
        ww, wh = glfw.get_window_size(win)
        if (fw, fh) != (app.target.width, app.target.height):
            app.target.resize(fw, fh)  # reallocates the CPU framebuffer

        ui_scale = fw / ww if ww > 0 else 1.0
        app.ctx.theme.scale = ui_scale
        app.sink.set_scale(ui_scale)
        app.ctx.set_frame_size(float(ww), float(wh))  # LOGICAL: layout in points

        dt = min(time.perf_counter() - last, 0.05)
        last, now = time.perf_counter(), now + dt
        if state.auto_rotate and not ctl["dragging"]:
            state.yaw += state.spin_speed * dt
        app.tick(now, dt)

        draw_scene(g, state, fw, fh)
        draw_ui(g, app, tex_size)
        glfw.swap_buffers(win)

    glfw.destroy_window(win)
    app.close()  # release UI callbacks while the interpreter is alive
    glfw.terminate()


if __name__ == "__main__":
    main()
