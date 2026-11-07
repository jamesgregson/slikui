// examples/04_glfw_opengl/main.cpp — GLFW + system OpenGL: a lit cube viewer.
//
// Shows silk living alongside raw GL:
//   * The cube and floor grid render through their own shader/VAOs BEFORE the
//     UI pass. ctx.render(false) skips the background fill so the scene shows
//     through under the widgets.
//   * A docked silk panel edits material + environment uniforms every frame
//     (sliders, checkboxes, dropdowns, swatches).
//   * The CubeViewport widget consumes mouse events: left-drag orbits the
//     camera (Context::capture_mouse keeps the drag alive outside bounds),
//     the wheel zooms. Silk widgets on top still get their own events.
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/gl3.h> // GL first: silenced deprecations, no legacy gl.h

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include <silk/backends/glfw/glfw_window.hpp>
#include <silk/backends/opengl/opengl.hpp>
#include <silk/silk.hpp>
#include <silk/text/system_fonts.hpp>

// ---------------------------------------------------------------------------
// Tiny column-major 4x4 math — the only GL math this example needs.
// ---------------------------------------------------------------------------
constexpr float kDeg2Rad = 3.14159265f / 180.f;
constexpr float kPi = 3.14159265f;

struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    static Mat4 zero() { return {}; }
};

inline Mat4 mul(const Mat4& a, const Mat4& b) {
    Mat4 r = Mat4::zero();
    for (int c = 0; c < 4; ++c)
        for (int rw = 0; rw < 4; ++rw) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a.m[k * 4 + rw] * b.m[c * 4 + k];
            r.m[c * 4 + rw] = s;
        }
    return r;
}

inline Mat4 perspective(float fov_y_rad, float aspect, float zn, float zf) {
    float f = 1.f / std::tan(fov_y_rad * 0.5f);
    Mat4 r = Mat4::zero();
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zf + zn) / (zn - zf);
    r.m[11] = -1.f;
    r.m[14] = 2.f * zf * zn / (zn - zf);
    return r;
}

// Right-handed look-at with world up (0,1,0); eye must not be straight above
// the target — the camera pitch is clamped well short of vertical.
inline Mat4 look_at(const float eye[3], const float target[3]) {
    float zx = eye[0] - target[0], zy = eye[1] - target[1], zz = eye[2] - target[2];
    float zl = std::sqrt(zx * zx + zy * zy + zz * zz);
    if (zl > 1e-6f) { zx /= zl; zy /= zl; zz /= zl; }
    // x = normalize(up × z), y = z × x  (up = (0,1,0))
    float xx = zz, xy = 0.f, xz = -zx;
    float xl = std::sqrt(xx * xx + xz * xz);
    if (xl > 1e-6f) { xx /= xl; xz /= xl; }
    float yx = zy * xz - zz * xy, yy = zz * xx - zx * xz, yz = zx * xy - zy * xx;
    Mat4 r;
    r.m[0] = xx; r.m[4] = xy; r.m[8]  = xz;
    r.m[1] = yx; r.m[5] = yy; r.m[9]  = yz;
    r.m[2] = zx; r.m[6] = zy; r.m[10] = zz;
    r.m[12] = -(xx * eye[0] + xy * eye[1] + xz * eye[2]);
    r.m[13] = -(yx * eye[0] + yy * eye[1] + yz * eye[2]);
    r.m[14] = -(zx * eye[0] + zy * eye[1] + zz * eye[2]);
    return r;
}

// ---------------------------------------------------------------------------
// Shaders: one program handles both the lit cube and the unlit grid.
// ---------------------------------------------------------------------------
static const char* kVs = R"(
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
)";

static const char* kFs = R"(
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
)";

// Procedural sky, drawn as ONE fullscreen triangle with depth writes off:
// a zenith/horizon gradient above the horizon, a shaded ground half-space
// below it, an exponential haze band hugging the horizon, and a sun disc +
// glow that shares the scene light's direction. Parameters come straight
// from the UI panel.
static const char* kSkyVs = R"(
#version 330 core
out vec2 v_ndc;
void main() {
    // Fullscreen triangle from gl_VertexID: (-1,-1), (3,-1), (-1,3).
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    v_ndc = p * 2.0 - 1.0;
    gl_Position = vec4(v_ndc, 0.0, 1.0);
}
)";

static const char* kSkyFs = R"(
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
)";

static GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = GL_FALSE;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        std::fprintf(stderr, "shader error: %s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint build_program(const char* vs_src, const char* fs_src) {
    GLuint vs = compile(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fs_src);
    if (!vs || !fs) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = GL_FALSE;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, sizeof log, nullptr, log);
        std::fprintf(stderr, "program error: %s\n", log);
        return 0;
    }
    return p;
}

// ---------------------------------------------------------------------------
// Geometry: a cube (pos + normal, indexed) and a fading floor grid (pos +
// color, drawn as GL_LINES). Attribute slots not fed by a VAO get constant
// values via glVertexAttrib* just before drawing.
// ---------------------------------------------------------------------------
struct Mesh {
    GLuint vao = 0, vbo = 0, ibo = 0;
    int count = 0; // index count for the cube, vertex count for the grid
};

static Mesh upload_mesh(const std::vector<float>& v, const std::vector<std::uint16_t>& idx) {
    Mesh m;
    glGenVertexArrays(1, &m.vao);
    glBindVertexArray(m.vao);
    glGenBuffers(1, &m.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, std::streamsize(v.size() * sizeof(float)), v.data(),
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (const void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (const void*)12);
    glGenBuffers(1, &m.ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, std::streamsize(idx.size() * sizeof(std::uint16_t)),
                 idx.data(), GL_STATIC_DRAW);
    m.count = int(idx.size());
    glBindVertexArray(0);
    return m;
}

// Final sanity pass shared by every generated shape: orient each triangle so
// its geometric normal agrees with the average of its vertex normals (the
// outward direction). The cube generator self-corrects at emit time; for the
// parametric shapes a post-pass over the index buffer is simpler and just as
// reliable.
static void fix_winding(std::vector<std::uint16_t>& idx, const std::vector<float>& v) {
    for (size_t t = 0; t + 2 < idx.size(); t += 3) {
        const float* A = &v[size_t(idx[t]) * 6];
        const float* B = &v[size_t(idx[t + 1]) * 6];
        const float* C = &v[size_t(idx[t + 2]) * 6];
        float e1[3] = {B[0] - A[0], B[1] - A[1], B[2] - A[2]};
        float e2[3] = {C[0] - A[0], C[1] - A[1], C[2] - A[2]};
        float g[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                      e1[0] * e2[1] - e1[1] * e2[0]};
        // Desired direction: the average vertex normal (already normalized
        // analytically for these shapes; the average stays near-unit).
        float d[3] = {(A[3] + B[3] + C[3]) / 3.f, (A[4] + B[4] + C[4]) / 3.f,
                      (A[5] + B[5] + C[5]) / 3.f};
        if (g[0] * d[0] + g[1] * d[1] + g[2] * d[2] < 0.f)
            std::swap(idx[t + 1], idx[t + 2]);
    }
}

static Mesh make_cube() {
    const float s = 1.f;
    std::vector<float> v;
    std::vector<std::uint16_t> idx;
    for (int axis = 0; axis < 3; ++axis) {
        int u = (axis + 1) % 3, w = (axis + 2) % 3;
        for (int sign = 0; sign < 2; ++sign) {
            float n[3] = {0, 0, 0};
            n[axis] = sign ? 1.f : -1.f;
            auto first = std::uint16_t(v.size() / 6);
            for (int corner = 0; corner < 4; ++corner) {
                float p[3] = {0, 0, 0};
                p[axis] = n[axis] * s;
                p[u] = (corner & 1) ? s : -s;
                p[w] = (corner & 2) ? s : -s;
                v.insert(v.end(), {p[0], p[1], p[2], n[0], n[1], n[2]});
            }
            // Emit each triangle in the corner order whose edge cross
            // product agrees with the face normal — i.e. CCW seen from
            // OUTSIDE the cube, as GL's default glFrontFace(GL_CCW) expects
            // for front faces under back-face culling. Deriving the order
            // from the actual vertex data makes this self-correcting: no
            // per-face sign reasoning to get backwards.
            auto emit_tri = [&](std::uint16_t a, std::uint16_t b, std::uint16_t c) {
                const float* A = &v[size_t(a) * 6];
                const float* B = &v[size_t(b) * 6];
                const float* C = &v[size_t(c) * 6];
                float e1[3] = {B[0] - A[0], B[1] - A[1], B[2] - A[2]};
                float e2[3] = {C[0] - A[0], C[1] - A[1], C[2] - A[2]};
                float cr[3] = {e1[1] * e2[2] - e1[2] * e2[1],
                               e1[2] * e2[0] - e1[0] * e2[2],
                               e1[0] * e2[1] - e1[1] * e2[0]};
                float d = cr[0] * n[0] + cr[1] * n[1] + cr[2] * n[2];
                if (d >= 0.f)
                    idx.insert(idx.end(), {a, b, c});
                else
                    idx.insert(idx.end(), {a, c, b});
            };
            // Corners are laid out in Z-order (c0=(-,-), c1=(+,-), c2=(-,+),
            // c3=(+,+)), so the quad must be split along the c0-c3 diagonal:
            // triangles (0,1,3) and (0,3,2). Splitting along the c1-c2 pair
            // instead (the "natural" (0,1,2)+(0,2,3) for ring order) leaves a
            // hole in every face.
            emit_tri(first, std::uint16_t(first + 1), std::uint16_t(first + 3));
            emit_tri(first, std::uint16_t(first + 3), std::uint16_t(first + 2));
        }
    }
    return upload_mesh(v, idx);
}

// UV sphere centered at the origin, poles on +Y, with analytic normals.
static Mesh make_sphere(float radius = 1.f, int sectors = 36, int rings = 22) {
    std::vector<float> v;
    std::vector<std::uint16_t> idx;
    for (int j = 0; j <= rings; ++j) {       // phi: 0 at the +Y pole down to pi
        float phi = float(j) / float(rings) * kPi;
        float sp = std::sin(phi), cp = std::cos(phi);
        for (int i = 0; i <= sectors; ++i) { // theta around +Y
            float th = float(i) / float(sectors) * 2.f * kPi;
            float n[3] = {sp * std::cos(th), cp, sp * std::sin(th)};
            v.insert(v.end(), {n[0] * radius, n[1] * radius, n[2] * radius, n[0], n[1], n[2]});
        }
    }
    auto at = [sectors](int j, int i) { return std::uint16_t(j * (sectors + 1) + i); };
    for (int j = 0; j < rings; ++j)
        for (int i = 0; i < sectors; ++i) {
            idx.push_back(at(j, i));
            idx.push_back(at(j + 1, i));
            idx.push_back(at(j, i + 1));
            idx.push_back(at(j, i + 1));
            idx.push_back(at(j + 1, i));
            idx.push_back(at(j + 1, i + 1));
        }
    fix_winding(idx, v); // also repairs the zero-area pole triangles' bookkeeping
    return upload_mesh(v, idx);
}

// Torus centered at the origin, tube axis in the XZ plane (+Y up), analytic
// normals. Sized so it clears the floor grid like the other shapes.
static Mesh make_torus(float R = 0.8f, float r = 0.35f, int u_segs = 44, int v_segs = 22) {
    std::vector<float> v;
    std::vector<std::uint16_t> idx;
    for (int i = 0; i <= u_segs; ++i) {      // u: around the main ring
        float u = float(i) / float(u_segs) * 2.f * kPi;
        float cu = std::cos(u), su = std::sin(u);
        for (int j = 0; j <= v_segs; ++j) {  // v: around the tube
            float t = float(j) / float(v_segs) * 2.f * kPi;
            float ct = std::cos(t), st = std::sin(t);
            float n[3] = {ct * cu, st, ct * su};
            float ring = R + r * ct;
            v.insert(v.end(), {ring * cu, r * st, ring * su, n[0], n[1], n[2]});
        }
    }
    auto at = [v_segs](int i, int j) { return std::uint16_t(i * (v_segs + 1) + j); };
    for (int i = 0; i < u_segs; ++i)
        for (int j = 0; j < v_segs; ++j) {
            idx.push_back(at(i, j));
            idx.push_back(at(i + 1, j));
            idx.push_back(at(i, j + 1));
            idx.push_back(at(i, j + 1));
            idx.push_back(at(i + 1, j));
            idx.push_back(at(i + 1, j + 1));
        }
    fix_winding(idx, v);
    return upload_mesh(v, idx);
}

static Mesh make_grid() {
    std::vector<float> v;
    const float y = -1.25f, half = 3.f, step = 0.5f;
    auto push = [&](float x, float z, float a) {
        v.insert(v.end(), {x, y, z, 0.62f, 0.68f, 0.75f, a});
    };
    for (int i = -6; i <= 6; ++i) {
        float p = i * step;
        float a = 0.45f * (1.f - std::fabs(p) / half) + 0.06f;
        push(p, -half, a); push(p, half, a); // line along z
        push(-half, p, a); push(half, p, a); // line along x
    }
    Mesh m;
    glGenVertexArrays(1, &m.vao);
    glBindVertexArray(m.vao);
    glGenBuffers(1, &m.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, std::streamsize(v.size() * sizeof(float)), v.data(),
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 28, (const void*)0);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 28, (const void*)12);
    m.count = int(v.size() / 7);
    glBindVertexArray(0);
    return m;
}

// ---------------------------------------------------------------------------
// App state: everything the panel edits, plus the camera the viewport drives.
// ---------------------------------------------------------------------------
struct CubeState {
    // geometry: 0 cube, 1 sphere, 2 torus (index into the shapes[] table)
    int shape = 0;
    // material
    float albedo[3] = {0.75f, 0.36f, 0.22f};
    float roughness = 0.35f;
    float metallic = 0.15f;
    // sky model: procedural dome with a ground half-space and haze band
    float sky_zenith[3] = {0.16f, 0.32f, 0.62f};
    float sky_horizon[3] = {0.62f, 0.74f, 0.88f};
    float ground[3] = {0.20f, 0.19f, 0.17f};
    float haze = 0.35f;      // density of the horizon haze band
    float sun_glow = 0.6f;   // sun disc/glow strength along the light direction
    // lighting
    float light_color[3] = {1.0f, 0.96f, 0.90f};
    float light_intensity = 1.6f;
    float ambient = 0.22f;
    float light_azimuth = 45.f;   // degrees around +Y
    float light_elevation = 55.f; // degrees above the horizon
    // animation + camera (owned by the viewport widget)
    bool auto_rotate = true;
    float spin_speed = 0.6f; // rad/s
    float yaw = 0.6f, pitch = 0.5f;
    float distance = 4.6f;
    static constexpr float kMinDist = 2.4f, kMaxDist = 10.f;
    void reset_camera() { yaw = 0.6f; pitch = 0.5f; distance = 4.6f; }
};

// ---------------------------------------------------------------------------
// Swatch — a small clickable color chip that highlights when it matches the
// value it edits. Demonstrates a minimal custom silk widget.
// ---------------------------------------------------------------------------
class Swatch : public silk::Widget {
public:
    Swatch(silk::Color c, float* target, std::string widget_id = {})
        : silk::Widget(std::move(widget_id)), color_(c), target_(target) {}

    silk::Size measure(const silk::Constraints& c) override { return c.clamp({22.f, 22.f}); }
    bool stretch_cross() const override { return false; }

    void paint(const silk::PaintEnv& env) const override {
        // Tolerance covers float<->u8 rounding so preset colors light up
        // the matching swatch, while staying far tighter than distinct chips.
        bool sel = target_ && std::fabs(target_[0] * 255.f - color_.r) < 1.5f &&
                   std::fabs(target_[1] * 255.f - color_.g) < 1.5f &&
                   std::fabs(target_[2] * 255.f - color_.b) < 1.5f;
        env.list.rect(bounds, color_, 4.f);
        env.list.hollow_rect(bounds, env.theme.border, 1, 4.f);
        if (sel) env.list.hollow_rect(bounds.inflate(2, 2), env.theme.accent, 2, 6.f);
    }

    bool on_event(const silk::Event& e) override {
        if (e.type == silk::Event::Type::MousePress && enabled && bounds.contains(e.pos)) {
            target_[0] = color_.r / 255.f;
            target_[1] = color_.g / 255.f;
            target_[2] = color_.b / 255.f;
            return true;
        }
        return false;
    }

    const char* widget_type() const override { return "Swatch"; }

private:
    silk::Color color_;
    float* target_;
};

// ---------------------------------------------------------------------------
// CubeViewport — fills the window, transparent, and handles the 3D mouse
// interaction: left-drag orbits (via mouse capture), wheel zooms.
// ---------------------------------------------------------------------------
class CubeViewport : public silk::Widget {
public:
    CubeState* state = nullptr;

    silk::Size measure(const silk::Constraints& c) override { return c.clamp({640.f, 420.f}); }

    // Dock the panel children: the controls panel hugs the top-right corner,
    // every other child hugs the bottom-left (the hint bar).
    void arrange(const silk::Rect& area) override {
        bounds = area;
        silk::Rect inner = area.inset(14);
        for (auto& ch : children) {
            // Measure at natural (unbounded) width: docked panels hug their
            // content, so a label is never force-wrapped by the window width.
            silk::Size s = ch->measure(silk::Constraints::loose({silk::kInfinite, inner.h}));
            if (ch.get() == panel_)
                ch->arrange({inner.right() - s.w, inner.y, s.w, s.h});
            else
                ch->arrange({inner.x, inner.bottom() - s.h, s.w, s.h});
        }
    }

    bool on_event(const silk::Event& e) override {
        using T = silk::Event::Type;
        switch (e.type) {
        case T::MousePress:
            if (e.button != silk::MouseLeft || !state || !bounds.contains(e.pos)) return false;
            if (panel_ && panel_->bounds.contains(e.pos)) return false; // press on the panel
            dragging_ = true;
            ctx->capture_mouse(this);
            return true;
        case T::MouseMove:
            if (dragging_ && state) {
                state->yaw -= e.delta.x * 0.008f;
                state->pitch = std::clamp(state->pitch + e.delta.y * 0.008f, -1.45f, 1.45f);
                return true;
            }
            return false;
        case T::MouseRelease:
            if (dragging_) {
                dragging_ = false;
                ctx->release_mouse(this);
                return true;
            }
            return false;
        case T::Scroll:
            if (state && bounds.contains(e.pos)) {
                float zoom = 1.f - 0.12f * e.scroll.y;
                state->distance = std::clamp(state->distance * zoom, CubeState::kMinDist,
                                             CubeState::kMaxDist);
                return true;
            }
            return false;
        default:
            return false;
        }
    }

    void tick(silk::Time t) override {
        if (state && state->auto_rotate && !dragging_) state->yaw += state->spin_speed * t.dt;
    }

    const char* widget_type() const override { return "CubeViewport"; }

    bool dragging_ = false;
    silk::Widget* panel_ = nullptr;
};

// ---------------------------------------------------------------------------
// Panel helper: label + live value on one row, slider underneath.
// ---------------------------------------------------------------------------
struct SliderRef {
    silk::Slider* slider;
};

static SliderRef add_slider(silk::Container& col, const char* label, float min, float max,
                            float init, const char* fmt, std::function<void(float)> on_change) {
    auto& row = col.add<silk::Row>().set_gap(8);
    row.add<silk::Label>(label);
    row.add<silk::Spacer>();
    auto& val = row.add<silk::Label>("");
    val.set_color(silk::Color::from_hex(0x8A919C));
    auto set_text = [vp = &val, fmt](float v) {
        char buf[48];
        std::snprintf(buf, sizeof buf, fmt, v);
        vp->set_text(buf);
    };
    set_text(init);
    auto& s = col.add<silk::Slider>(min, max);
    s.set_value(init, false);
    s.on_change([set_text, on_change = std::move(on_change)](float v) {
        set_text(v);
        if (on_change) on_change(v);
    });
    return {&s};
}

static void add_swatches(silk::Container& col, float* target,
                         std::initializer_list<silk::Color> colors) {
    auto& row = col.add<silk::Row>().set_gap(6);
    for (auto c : colors) row.add<Swatch>(c, target);
}

int main() {
    silk::glfw::GlfwWindow win(960, 640, "silk cube viewer");
    win.set_min_size(560, 380);
    if (!win.ok()) return 1;

    silk::opengl::OpenglSink sink; // system OpenGL, no loader
    if (!sink.init()) return 1;

    GLuint prog = build_program(kVs, kFs);
    if (!prog) return 1;
    struct Uniforms {
        GLint proj, view, model, albedo, light_color, light_dir, cam_pos,
              ambient, intensity, roughness, metallic, unlit;
    } u{
        glGetUniformLocation(prog, "u_proj"),   glGetUniformLocation(prog, "u_view"),
        glGetUniformLocation(prog, "u_model"),  glGetUniformLocation(prog, "u_albedo"),
        glGetUniformLocation(prog, "u_light_color"), glGetUniformLocation(prog, "u_light_dir"),
        glGetUniformLocation(prog, "u_cam_pos"), glGetUniformLocation(prog, "u_ambient"),
        glGetUniformLocation(prog, "u_intensity"), glGetUniformLocation(prog, "u_roughness"),
        glGetUniformLocation(prog, "u_metallic"),  glGetUniformLocation(prog, "u_unlit"),
    };
    Mesh shapes[3] = {make_cube(), make_sphere(), make_torus()};
    Mesh grid = make_grid();

    GLuint sky_prog = build_program(kSkyVs, kSkyFs);
    if (!sky_prog) return 1;
    struct {
        GLint zenith, horizon, ground, sun_color, sun_dir;
        GLint cam_fwd, cam_right, cam_up, tan_half_fov, aspect, haze, sun_glow;
    } su{
        glGetUniformLocation(sky_prog, "u_zenith"),
        glGetUniformLocation(sky_prog, "u_horizon"),
        glGetUniformLocation(sky_prog, "u_ground"),
        glGetUniformLocation(sky_prog, "u_sun_color"),
        glGetUniformLocation(sky_prog, "u_sun_dir"),
        glGetUniformLocation(sky_prog, "u_cam_fwd"),
        glGetUniformLocation(sky_prog, "u_cam_right"),
        glGetUniformLocation(sky_prog, "u_cam_up"),
        glGetUniformLocation(sky_prog, "u_tan_half_fov"),
        glGetUniformLocation(sky_prog, "u_aspect"),
        glGetUniformLocation(sky_prog, "u_haze"),
        glGetUniformLocation(sky_prog, "u_sun_glow"),
    };
    GLuint sky_vao = 0; // empty: the fullscreen triangle reads only gl_VertexID
    glGenVertexArrays(1, &sky_vao);

    silk::Context ctx(sink);
    ctx.load_font_file(silk::find_system_font());

    CubeState state;
    auto& root = ctx.set_root<CubeViewport>();
    root.state = &state;

    // ----- controls panel (docked top-right by CubeViewport::arrange) -----
    auto& panel = root.add<silk::Panel>();
    root.panel_ = &panel;
    panel.set_padding(14).set_gap(10);
    panel.set_background(silk::Color(0, 0, 0, 185));
    panel.set_border(silk::Color(255, 255, 255, 30));
    panel.set_radius(10);
    panel.draw_border_ = true;

    // All controls live inside a ScrollArea: on a short window the panel
    // content scrolls (with a thin scrollbar) instead of spilling widgets
    // past the panel's rounded bottom edge.
    auto& scroll = panel.add<silk::ScrollArea>();
    scroll.hint_h_ = 1e5f; // always fill the panel: it tracks the window size,
                           // so resizing larger grows the panel instead of
                           // leaving a fixed-height one that looks smaller
    scroll.content().set_gap(10);
    scroll.add<silk::Label>("silk cube viewer").set_font_size(18);

    // Contextual control groups in an accordion: exactly one section open at
    // a time keeps the panel compact at any window size.
    auto& acc = scroll.add<silk::Accordion>();
    acc.set_gap(6);
    acc.set_exclusive(false); // sections can be opened independently

    auto& sec_shape = acc.add_section("Shape", true);
    sec_shape.set_gap(8);
    sec_shape.add<silk::Dropdown>()
        .set_items({"Cube", "Sphere", "Torus"})
        .set_placeholder("Reference shape")
        .set_selected(size_t(state.shape))
        .on_select([&](size_t i) { state.shape = int(i); });

    auto& sec_material = acc.add_section("Material");
    sec_material.set_gap(8);
    add_swatches(sec_material, state.albedo,
                 {silk::Color::from_hex(0xBF5C38), silk::Color::from_hex(0x4E9E58),
                  silk::Color::from_hex(0x3F6FBF), silk::Color::from_hex(0xC9C2B5),
                  silk::Color::from_hex(0xE8E8E8)});
    add_slider(sec_material, "Roughness", 0.05f, 1.f, state.roughness, "%.2f",
               [&](float v) { state.roughness = v; });
    add_slider(sec_material, "Metallic", 0.f, 1.f, state.metallic, "%.2f",
               [&](float v) { state.metallic = v; });

    auto& sec_env = acc.add_section("Environment");
    sec_env.set_gap(8);
    // Sky presets set all three dome colors at once; the swatch rows below
    // fine-tune them individually.
    sec_env.add<silk::Dropdown>()
        .set_items({"Day", "Sunset", "Night", "Overcast", "Dust"})
        .set_placeholder("Sky preset")
        .set_selected(0)
        .on_select([&](size_t i) {
            static const float kSkyPresets[5][3][3] = {
                /* Day */      {{0.16f, 0.32f, 0.62f}, {0.62f, 0.74f, 0.88f}, {0.20f, 0.19f, 0.17f}},
                /* Sunset */   {{0.16f, 0.18f, 0.36f}, {0.94f, 0.55f, 0.28f}, {0.14f, 0.11f, 0.10f}},
                /* Night */    {{0.02f, 0.03f, 0.08f}, {0.08f, 0.10f, 0.18f}, {0.05f, 0.05f, 0.06f}},
                /* Overcast */ {{0.45f, 0.49f, 0.55f}, {0.72f, 0.75f, 0.80f}, {0.30f, 0.30f, 0.29f}},
                /* Dust */     {{0.55f, 0.44f, 0.30f}, {0.85f, 0.70f, 0.50f}, {0.35f, 0.28f, 0.20f}},
            };
            for (int k = 0; k < 3; ++k) {
                state.sky_zenith[k] = kSkyPresets[i][0][k];
                state.sky_horizon[k] = kSkyPresets[i][1][k];
                state.ground[k] = kSkyPresets[i][2][k];
            }
        });
    sec_env.add<silk::Label>("Sky zenith").set_color(silk::Color::from_hex(0x8A919C));
    add_swatches(sec_env, state.sky_zenith,
                 {silk::Color::from_hex(0x29529E), silk::Color::from_hex(0x292E5C),
                  silk::Color::from_hex(0x737D8C), silk::Color::from_hex(0x8C704D),
                  silk::Color::from_hex(0x050814)});
    sec_env.add<silk::Label>("Horizon").set_color(silk::Color::from_hex(0x8A919C));
    add_swatches(sec_env, state.sky_horizon,
                 {silk::Color::from_hex(0x9EBDE0), silk::Color::from_hex(0xF08C47),
                  silk::Color::from_hex(0xB8BFCC), silk::Color::from_hex(0xD9B380),
                  silk::Color::from_hex(0x141A2E)});
    sec_env.add<silk::Label>("Ground").set_color(silk::Color::from_hex(0x8A919C));
    add_swatches(sec_env, state.ground,
                 {silk::Color::from_hex(0x33312B), silk::Color::from_hex(0x241C1A),
                  silk::Color::from_hex(0x4D4D4A), silk::Color::from_hex(0x594733),
                  silk::Color::from_hex(0x0D0D0F)});
    add_slider(sec_env, "Haze", 0.f, 1.f, state.haze, "%.2f",
               [&](float v) { state.haze = v; });
    add_slider(sec_env, "Sun glow", 0.f, 1.f, state.sun_glow, "%.2f",
               [&](float v) { state.sun_glow = v; });

    auto& sec_light = acc.add_section("Light");
    sec_light.set_gap(8);
    sec_light.add<silk::Label>("Color").set_color(silk::Color::from_hex(0x8A919C));
    add_swatches(sec_light, state.light_color,
                 {silk::Color::from_hex(0xFFF5E8), silk::Color::from_hex(0xFFFFFF),
                  silk::Color::from_hex(0xB8D8FF), silk::Color::from_hex(0xE8A0FF)});
    add_slider(sec_light, "Intensity", 0.f, 4.f, state.light_intensity, "%.2f",
               [&](float v) { state.light_intensity = v; });
    add_slider(sec_light, "Elevation", -85.f, 85.f, state.light_elevation, "%.0f deg",
               [&](float v) { state.light_elevation = v; });
    add_slider(sec_light, "Azimuth", -180.f, 180.f, state.light_azimuth, "%.0f deg",
               [&](float v) { state.light_azimuth = v; });
    add_slider(sec_light, "Ambient", 0.f, 1.f, state.ambient, "%.2f",
               [&](float v) { state.ambient = v; });

    auto& sec_camera = acc.add_section("Camera");
    sec_camera.set_gap(8);
    sec_camera.add<silk::Checkbox>("Auto-rotate", state.auto_rotate)
        .on_change([&](bool v) { state.auto_rotate = v; });
    add_slider(sec_camera, "Spin speed", 0.f, 2.f, state.spin_speed, "%.2f rad/s",
               [&](float v) { state.spin_speed = v; });
    sec_camera.add<silk::Button>("Reset camera").on_click([&] { state.reset_camera(); });

    // ----- hint bar (docked bottom-left by CubeViewport::arrange) -----
    auto& hint = root.add<silk::Panel>();
    hint.set_padding(silk::Padding::sym(10, 6));
    hint.set_background(silk::Color(0, 0, 0, 120));
    hint.set_radius(8);
    hint.add<silk::Label>("drag: orbit | scroll: zoom")
        .set_color(silk::Color(255, 255, 255, 160));

    // ----- frame loop -----
    auto last = std::chrono::steady_clock::now();
    double now = 0;
    while (!win.should_close()) {
        win.dispatch(ctx);
        ctx.set_frame_size(win.point_size()); // LOGICAL frame: layout in points
        ctx.theme().scale = win.ui_scale();
        sink.set_scale(win.ui_scale());

        auto pt = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(pt - last).count();
        last = pt;
        now += dt;

        ctx.tick(silk::Time{now, silk::clamp_dt(dt)});

        // --- scene pass: cube + grid, then the UI on top ---
        int fw = 0, fh = 0;
        glfwGetFramebufferSize(win.handle(), &fw, &fh);
        float aspect = fh > 0 ? float(fw) / float(fh) : 1.f;
        Mat4 proj = perspective(45.f * kDeg2Rad, aspect, 0.1f, 100.f);

        float cp = std::cos(state.pitch), sp = std::sin(state.pitch);
        float cy = std::cos(state.yaw), sy = std::sin(state.yaw);
        float eye[3] = {state.distance * cp * sy, state.distance * sp,
                        state.distance * cp * cy};
        const float target[3] = {0.f, -0.15f, 0.f};
        Mat4 view = look_at(eye, target);

        // Camera basis for the sky's per-pixel view rays (right-handed,
        // world up +Y). |pitch| is clamped well short of vertical, so
        // right = fwd x (0,1,0) never degenerates.
        float fwd[3] = {target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
        float fl = std::sqrt(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
        fwd[0] /= fl; fwd[1] /= fl; fwd[2] /= fl;
        float right[3] = {-fwd[2], 0.f, fwd[0]};
        float rl = std::sqrt(right[0] * right[0] + right[2] * right[2]);
        right[0] /= rl; right[2] /= rl;
        float up[3] = {right[1] * fwd[2] - right[2] * fwd[1],
                       right[2] * fwd[0] - right[0] * fwd[2],
                       right[0] * fwd[1] - right[1] * fwd[0]};
        float ul = std::sqrt(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
        if (ul > 1e-5f) { up[0] /= ul; up[1] /= ul; up[2] /= ul; }

        float az = state.light_azimuth * kDeg2Rad, el = state.light_elevation * kDeg2Rad;
        float L[3] = {std::cos(el) * std::sin(az), std::sin(el), std::cos(el) * std::cos(az)};

        glViewport(0, 0, fw, fh);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_BLEND);
        // Clear FIRST with the depth mask on — glClear honours glDepthMask,
        // so clearing after disabling writes would leave every previous
        // frame's depth values in place and make the cube z-fight with
        // itself (and the grid) one frame at a time.
        glDepthMask(GL_TRUE);
        glDisable(GL_DEPTH_TEST);
        glClearColor(state.sky_horizon[0], state.sky_horizon[1], state.sky_horizon[2], 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDepthMask(GL_FALSE);

        // --- sky pass: fullscreen triangle under everything ---
        glUseProgram(sky_prog);
        glBindVertexArray(sky_vao);
        glUniform3f(su.zenith, state.sky_zenith[0], state.sky_zenith[1], state.sky_zenith[2]);
        glUniform3f(su.horizon, state.sky_horizon[0], state.sky_horizon[1],
                    state.sky_horizon[2]);
        glUniform3f(su.ground, state.ground[0], state.ground[1], state.ground[2]);
        glUniform3f(su.sun_color, state.light_color[0], state.light_color[1],
                    state.light_color[2]);
        glUniform3f(su.sun_dir, L[0], L[1], L[2]);
        glUniform3f(su.cam_fwd, fwd[0], fwd[1], fwd[2]);
        glUniform3f(su.cam_right, right[0], right[1], right[2]);
        glUniform3f(su.cam_up, up[0], up[1], up[2]);
        glUniform1f(su.tan_half_fov, std::tan(0.5f * 45.f * kDeg2Rad));
        glUniform1f(su.aspect, aspect);
        glUniform1f(su.haze, state.haze);
        glUniform1f(su.sun_glow, state.sun_glow);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glDepthMask(GL_TRUE);

        glUseProgram(prog);
        glUniformMatrix4fv(u.proj, 1, GL_FALSE, proj.m);
        glUniformMatrix4fv(u.view, 1, GL_FALSE, view.m);
        glUniformMatrix4fv(u.model, 1, GL_FALSE, Mat4{}.m);
        glUniform3f(u.cam_pos, eye[0], eye[1], eye[2]);
        glUniform3f(u.light_dir, L[0], L[1], L[2]);
        glUniform3f(u.light_color, state.light_color[0], state.light_color[1],
                    state.light_color[2]);
        glUniform3f(u.albedo, state.albedo[0], state.albedo[1], state.albedo[2]);
        glUniform1f(u.intensity, state.light_intensity);
        glUniform1f(u.ambient, state.ambient);
        glUniform1f(u.roughness, state.roughness);
        glUniform1f(u.metallic, state.metallic);

        // Grid: unlit + blended, below the cube in depth.
        glUniform1f(u.unlit, 1.f);
        glVertexAttrib3f(1, 0.f, 1.f, 0.f); // a_normal constant for grid verts
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBindVertexArray(grid.vao);
        glDrawArrays(GL_LINES, 0, grid.count);

        // Shape: opaque, lit, back-face culled (indices wound CCW outward).
        Mesh& shape = shapes[state.shape];
        glUniform1f(u.unlit, 0.f);
        glVertexAttrib4f(2, 1.f, 1.f, 1.f, 1.f); // a_color constant for shape verts
        glDisable(GL_BLEND);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glBindVertexArray(shape.vao);
        glDrawElements(GL_TRIANGLES, shape.count, GL_UNSIGNED_SHORT, nullptr);
        glDisable(GL_CULL_FACE);

        glDisable(GL_DEPTH_TEST);
        glBindVertexArray(0);

        ctx.render(false); // skip the background fill: the scene shows through
        win.swap();
    }
    return 0;
}
