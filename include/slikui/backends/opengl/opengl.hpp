// slikui/backends/opengl/opengl.hpp — OpenglSink: IRenderSink over OpenGL 3.3.
//
// Uses the SYSTEM OpenGL headers/libraries — no bundled loader. macOS links
// OpenGL.framework directly; Linux uses the glvnd/libGL dev headers with
// GL_GLEXT_PROTOTYPES. If they're missing, install them (see #error below).
//
// Batches the DrawList into quads with an SDF fragment shader providing
// rounded corners, borders and capsule lines with antialiasing.
// Clipping maps to glScissor. Setup:
//
//   slikui::opengl::OpenglSink sink;
//   sink.init();                      // compiles shaders, creates VAO/VBO
//   slikui::Context ctx(sink);
#pragma once

#include "slikui/render/sink.hpp"

#if defined(__APPLE__)
#  define GL_SILENCE_DEPRECATION 1 // OpenGL still works on macOS; Apple just moans
#  include <OpenGL/gl3.h>
#elif defined(__linux__)
#  define GL_GLEXT_PROTOTYPES 1
#  include <GL/gl.h>
#  include <GL/glext.h>
#else
#  error \
"slikuis OpenGL backend uses your operating system's OpenGL headers, which were not found. \
macOS: ships with Xcode Command Line Tools (xcode-select --install). \
Linux: sudo apt install libgl-dev (or the libglvnd equivalent for your distro). \
Windows: install libgl/glext headers or use the software backend."
#endif

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <vector>

namespace slikui::opengl {

// One vertex: pos + color + uv + tex-kind + SDF params (15 floats).
struct Vertex {
    float pos[2];
    float col[4];
    float uv[2];
    float tex;      // 0 flat, 1 alpha8 atlas, 2 rgba image
    float center[2];
    float half[2];
    float params[4]; // radius, border, dir.x, dir.y
};

class OpenglSink : public IRenderSink {
public:
    bool init() {
        GLuint vs = compile(GL_VERTEX_SHADER, kVertexSrc);
        GLuint fs = compile(GL_FRAGMENT_SHADER, kFragmentSrc);
        if (!vs || !fs) return false;
        program_ = glCreateProgram();
        glAttachShader(program_, vs);
        glAttachShader(program_, fs);
        glLinkProgram(program_);
        glDeleteShader(vs);
        glDeleteShader(fs);
        GLint linked = GL_FALSE;
        glGetProgramiv(program_, GL_LINK_STATUS, &linked);
        if (!linked) return false;

        glGenVertexArrays(1, &vao_);
        glBindVertexArray(vao_);
        glGenBuffers(1, &vbo_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);

        auto attr = [&](GLuint index, GLint size, int float_offset) {
            glEnableVertexAttribArray(index);
            glVertexAttribPointer(index, size, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                  (const void*)(std::intptr_t)(float_offset * 4));
        };
        attr(0, 2, 0);            // pos
        attr(1, 4, 2);            // color
        attr(2, 2, 6);            // uv
        attr(3, 1, 8);            // tex kind
        attr(4, 2, 9);            // center
        attr(5, 2, 11);           // half extents
        attr(6, 4, 13);           // params

        glUseProgram(program_);
        u_res_ = glGetUniformLocation(program_, "u_res");
        u_scale_ = glGetUniformLocation(program_, "u_scale");
        u_tex_ = glGetUniformLocation(program_, "u_tex");
        return true;
    }

    // Logical px -> framebuffer px (HiDPI). DrawList coordinates are logical.
    void set_scale(float s) { scale_ = s; }

    void upload(const TexUpload& up) override {
        GLuint tex = 0;
        auto& info = textures_[up.id];
        if (info.tex == 0) glGenTextures(1, &tex);
        else tex = info.tex;
        info.tex = tex;
        info.w = up.width;
        info.h = up.height;

        glBindTexture(GL_TEXTURE_2D, tex);
        GLint internal = up.format == TexFormat::Rgba8 ? GL_RGBA8 : GL_R8;
        GLenum fmt = up.format == TexFormat::Rgba8 ? GL_RGBA : GL_RED;
        glTexImage2D(GL_TEXTURE_2D, 0, internal, up.width, up.height, 0, fmt,
                     GL_UNSIGNED_BYTE, up.pixels);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    void render(const DrawList& list, Vec2 framebuffer_size) override {
        if (!program_) return;
        px_ = {framebuffer_size.x * scale_, framebuffer_size.y * scale_};
        glViewport(0, 0, int(px_.x), int(px_.y));
        glUseProgram(program_);
        glUniform1f(u_scale_, scale_);
        glUniform2f(u_res_, px_.x, px_.y);
        glUniform1i(u_tex_, 0); // sampler reads unit 0
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBindVertexArray(vao_);
        bound_tex_ = 0; // nothing bound yet for this frame

        clip_stack_.clear();
        clip_stack_.push_back({0, 0, px_.x, px_.y});
        apply_scissor(clip_stack_.back());

        for (const auto& cmd : list.commands()) {
            switch (cmd.type) {
            case DrawCmd::Type::ClipPush:
                flush();
                clip_stack_.push_back(cmd.rect.intersect(clip_stack_.back()));
                apply_scissor(clip_stack_.back());
                break;
            case DrawCmd::Type::ClipPop:
                flush();
                if (clip_stack_.size() > 1) clip_stack_.pop_back();
                apply_scissor(clip_stack_.back());
                break;
            default:
                push(cmd);
                break;
            }
        }
        flush();
    }

private:
    struct TextureInfo {
        GLuint tex = 0;
        int w = 0, h = 0;
    };

    void apply_scissor(const Rect& r) {
        glScissor(int(r.x * scale_), int(px_.y - r.bottom() * scale_),
                  int(r.w * scale_), int(r.h * scale_));
        glEnable(GL_SCISSOR_TEST);
    }

    void push(const DrawCmd& cmd) {
        switch (cmd.type) {
        case DrawCmd::Type::Rect:
            quad(cmd.rect, cmd.color, cmd.radius, cmd.hollow ? cmd.thickness : 0.f,
                 {}, {}, 0.f, nullptr);
            break;
        case DrawCmd::Type::Line: {
            Vec2 a{cmd.rect.x, cmd.rect.y};
            Vec2 b{cmd.rect.x + cmd.rect.w, cmd.rect.y + cmd.rect.h};
            Vec2 d{b.x - a.x, b.y - a.y};
            float len = d.length();
            if (len <= 0) break;
            d = d * (1.f / len);
            // Bounding box padded by the thickness: axis-aligned lines have
            // zero-area endpoint rects, which would rasterize nothing.
            Rect bb{std::fmin(a.x, b.x) - cmd.thickness, std::fmin(a.y, b.y) - cmd.thickness,
                    std::fabs(b.x - a.x) + 2 * cmd.thickness, std::fabs(b.y - a.y) + 2 * cmd.thickness};
            quad(bb, cmd.color, std::fmax(cmd.thickness * .5f, 0.01f), 0.f,
                 {len * .5f, cmd.thickness * .5f}, {(a.x + b.x) * .5f, (a.y + b.y) * .5f},
                 cmd.thickness * .5f, &d);
            break;
        }
        case DrawCmd::Type::Glyph:
        case DrawCmd::Type::Image: {
            auto it = textures_.find(cmd.tex_id);
            if (it == textures_.end() || it->second.tex == 0) break;
            float tw = float(it->second.w), th = float(it->second.h);
            if (tw <= 0 || th <= 0) break;
            // Bind the command's texture before batching it. Without this the
            // sampler kept whatever upload() left bound — glyphs silently
            // sampled the most recently uploaded texture (broken mosaics).
            if (it->second.tex != bound_tex_) {
                flush();
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, it->second.tex);
                bound_tex_ = it->second.tex;
            }
            int kind = cmd.type == DrawCmd::Type::Glyph ? 1 : 2;
            Vec2 uv0{cmd.uv.x / tw, cmd.uv.y / th};
            Vec2 uv1{(cmd.uv.x + cmd.uv.w) / tw, (cmd.uv.y + cmd.uv.h) / th};
            textured_quad(cmd.rect, cmd.color, uv0, uv1, kind);
            break;
        }
        default:
            break;
        }
    }

    // Filled/rounded rect, border ring, or capsule (when dir != null).
    void quad(const Rect& r, Color c, float radius, float border,
              Vec2 half, Vec2 center, float end_radius, const Vec2* dir = nullptr) {
        float dx = 0, dy = 0;
        if (dir) {
            dx = dir->x;
            dy = dir->y;
            radius = end_radius;
        } else {
            center = r.center();
            half = {r.w * .5f, r.h * .5f};
        }
        float pr = radius, pb = border;
        auto vert = [&](float px, float py, float u, float v) {
            Vertex vt{{px, py},
                      {c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f},
                      {u, v}, 0.f, {center.x, center.y}, {half.x, half.y},
                      {pr, pb, dx, dy}};
            verts_.push_back(vt);
        };
        vert(r.x, r.y, 0, 0);
        vert(r.right(), r.y, 1, 0);
        vert(r.x, r.bottom(), 0, 1);
        vert(r.right(), r.y, 1, 0);
        vert(r.right(), r.bottom(), 1, 1);
        vert(r.x, r.bottom(), 0, 1);
    }

    void textured_quad(const Rect& r, Color c, Vec2 uv0, Vec2 uv1, int kind) {
        auto vert = [&](float px, float py, float u, float v) {
            Vertex vt{{px, py},
                      {c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f},
                      {u, v}, float(kind), {0, 0}, {0, 0}, {0, 0, 0, 0}};
            verts_.push_back(vt);
        };
        vert(r.x, r.y, uv0.x, uv0.y);
        vert(r.right(), r.y, uv1.x, uv0.y);
        vert(r.x, r.bottom(), uv0.x, uv1.y);
        vert(r.right(), r.y, uv1.x, uv0.y);
        vert(r.right(), r.bottom(), uv1.x, uv1.y);
        vert(r.x, r.bottom(), uv0.x, uv1.y);
    }

    void flush() {
        if (verts_.empty()) return;
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(verts_.size() * sizeof(Vertex)),
                     verts_.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, GLsizei(verts_.size()));
        verts_.clear();
    }

    GLuint compile(GLenum type, const char* src) {
        GLuint s = glCreateShader(type);
        if (s == 0) {
            std::fprintf(stderr,
                         "slikui: glCreateShader returned 0 — is a GL context current on this thread?\n");
            return 0;
        }
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = GL_FALSE;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512];
            log[0] = 0;
            glGetShaderInfoLog(s, sizeof(log), nullptr, log);
            std::fprintf(stderr, "slikui: shader compile failed: %s\n", log);
            glDeleteShader(s);
            return 0;
        }
        return s;
    }

    static constexpr const char* kVertexSrc = R"(#version 330 core
layout(location=0) in vec2 a_pos;
layout(location=1) in vec4 a_col;
layout(location=2) in vec2 a_uv;
layout(location=3) in float a_tex;
layout(location=4) in vec2 a_center;
layout(location=5) in vec2 a_half;
layout(location=6) in vec4 a_params; // radius, border, dir.x, dir.y
uniform vec2 u_res;
uniform float u_scale;
out vec4 v_col; out vec2 v_uv; out float v_tex;
out vec2 v_center; out vec2 v_half; out vec4 v_params; out vec2 v_pos;
void main() {
    v_col = a_col; v_uv = a_uv; v_tex = a_tex;
    vec2 sp = a_pos * u_scale;
    v_pos = sp;
    v_center = a_center * u_scale;
    v_half = a_half * u_scale;
    v_params = vec4(a_params.x * u_scale, a_params.y * u_scale, a_params.zw);
    vec2 ndc = vec2(sp.x / u_res.x * 2.0 - 1.0, 1.0 - sp.y / u_res.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
}
)";

    static constexpr const char* kFragmentSrc = R"(#version 330 core
in vec4 v_col; in vec2 v_uv; in float v_tex;
in vec2 v_center; in vec2 v_half; in vec4 v_params; in vec2 v_pos;
uniform sampler2D u_tex;
out vec4 frag;
float sd_box(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - (b - vec2(r));
    vec2 d = max(q, 0.0);
    return length(d) + min(max(q.x, q.y), 0.0) - r;
}
void main() {
    float alpha = 1.0;
    vec4 col = v_col;
    float radius = v_params.x;
    float border = v_params.y;
    vec2 dir = v_params.zw;
    vec2 p = v_pos - v_center;
    if (dir.x != 0.0 || dir.y != 0.0) {
        // capsule: rotate into line space
        vec2 d = normalize(dir);
        p = vec2(dot(p, d), dot(p, vec2(-d.y, d.x)));
        float by = v_half.y;
        vec2 q = abs(p) - vec2(max(v_half.x - by, 0.0), 0.0);
        float dd = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - by;
        alpha *= clamp(0.5 - dd, 0.0, 1.0);
    } else if (v_tex < 0.5) {
        // rounded (or square, radius 0) rect with optional border band.
        // Textured quads skip the SDF: their center/half are unused.
        float d = sd_box(p, v_half, radius);
        alpha *= clamp(0.5 - d, 0.0, 1.0);
        if (border > 0.0) {
            vec2 inner = max(v_half - vec2(border), vec2(0.0));
            float di = sd_box(p, inner, max(radius - border, 0.0));
            alpha *= clamp(0.5 + di, 0.0, 1.0);
        }
    }
    if (v_tex > 0.5) {
        vec4 s = texture(u_tex, v_uv);
        if (v_tex < 1.5) alpha *= s.r;        // alpha8 atlas (glyphs)
        else col = v_col * s;                 // rgba image
    }
    frag = vec4(col.rgb, col.a * alpha);
}
)";

    GLuint program_ = 0, vao_ = 0, vbo_ = 0;
    GLint u_res_ = -1, u_scale_ = -1, u_tex_ = -1;
    float scale_ = 1.f;
    Vec2 px_{}; // framebuffer px = logical * scale
    std::unordered_map<std::uint32_t, TextureInfo> textures_;
    std::vector<Vertex> verts_;
    std::vector<Rect> clip_stack_;
    GLuint bound_tex_ = 0; // texture currently bound on unit 0
};

} // namespace slikui::opengl
