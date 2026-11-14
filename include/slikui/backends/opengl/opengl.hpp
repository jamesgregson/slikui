// slikui/backends/opengl/opengl.hpp — OpenglSink: IRenderSink over OpenGL 3.3.
//
// Uses the SYSTEM OpenGL headers/libraries — no bundled loader. macOS links
// OpenGL.framework directly; Linux uses the glvnd/libGL dev headers with
// GL_GLEXT_PROTOTYPES. If they're missing, install them (see #error below).
//
// GpuBatcher (render/batcher.hpp) turns the DrawList into one instance per
// command plus scissor/texture batches; this sink uploads the instances once
// per frame and issues one instanced draw per batch. The SDF fragment shader
// gives rounded corners, borders and capsule lines their antialiasing.
// Setup:
//
//   slikui::opengl::OpenglSink sink;
//   sink.init();                      // compiles shaders, creates VAO/VBO
//   slikui::Context ctx(sink);
//
// GL state: render() sets viewport, program, VAO, texture unit 0, blending
// (straight-alpha over) and scissor, and disables depth test, culling and
// stencil test. It leaves them that way; reset what your next pass needs.
// It draws into whatever framebuffer is bound.
#pragma once

#include "slikui/render/batcher.hpp"
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

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <vector>

namespace slikui::opengl {

class OpenglSink : public IRenderSink {
public:
    // Needs a current GL 3.3 core context. Returns false on shader failure.
    bool init() {
        GLuint vs = compile(GL_VERTEX_SHADER, kGpuVertexShader);
        GLuint fs = compile(GL_FRAGMENT_SHADER, kGpuFragmentShader);
        if (!vs || !fs) {
            if (vs) glDeleteShader(vs);
            if (fs) glDeleteShader(fs);
            return false;
        }
        program_ = glCreateProgram();
        glAttachShader(program_, vs);
        glAttachShader(program_, fs);
        glLinkProgram(program_);
        glDeleteShader(vs);
        glDeleteShader(fs);
        GLint linked = GL_FALSE;
        glGetProgramiv(program_, GL_LINK_STATUS, &linked);
        if (!linked) {
            glDeleteProgram(program_);
            program_ = 0;
            return false;
        }

        glGenVertexArrays(1, &vao_);
        glBindVertexArray(vao_);
        glGenBuffers(1, &vbo_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        // Five vec4 per-instance attributes = the GpuInstance fields.
        for (GLuint i = 0; i < 5; ++i) {
            glEnableVertexAttribArray(i);
            glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, sizeof(GpuInstance),
                                  (const void*)(std::uintptr_t)(i * 4 * sizeof(float)));
            glVertexAttribDivisor(i, 1);
        }
        glBindVertexArray(0);

        u_res_ = glGetUniformLocation(program_, "u_res");
        u_scale_ = glGetUniformLocation(program_, "u_scale");
        u_tex_ = glGetUniformLocation(program_, "u_tex");
        return true;
    }

    // Frees every GL object. Needs the same context current as init().
    void shutdown() {
        flush_deletes();
        // GL defers deleting a program that is still current: unbind ours.
        GLint current = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &current);
        if (program_ && GLuint(current) == program_) glUseProgram(0);
        for (auto& [id, t] : textures_)
            if (t) glDeleteTextures(1, &t);
        textures_.clear();
        if (vbo_) glDeleteBuffers(1, &vbo_);
        if (vao_) glDeleteVertexArrays(1, &vao_);
        if (program_) glDeleteProgram(program_);
        vbo_ = vao_ = program_ = 0;
        vbo_capacity_ = 0;
    }

    // Logical px -> framebuffer px (HiDPI). DrawList coordinates are logical.
    void set_scale(float s) { scale_ = s; }
    float scale() const { return scale_; }

    void upload(const TexUpload& up) override {
        GLuint& tex = textures_[up.id];
        if (tex == 0) glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // R8 rows are tightly packed
        bool rgba = up.format == TexFormat::Rgba8;
        glTexImage2D(GL_TEXTURE_2D, 0, rgba ? GL_RGBA8 : GL_R8, up.width, up.height, 0,
                     rgba ? GL_RGBA : GL_RED, GL_UNSIGNED_BYTE, up.pixels);
        // Same sampling as the software sink: bilinear glyph coverage,
        // nearest-neighbour images.
        GLint filter = rgba ? GL_NEAREST : GL_LINEAR;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    // May run without a current GL context (Context::clear_tree at
    // shutdown), so the delete waits for the next render() or shutdown().
    void release(std::uint32_t id) override {
        auto it = textures_.find(id);
        if (it == textures_.end()) return;
        if (it->second) pending_delete_.push_back(it->second);
        textures_.erase(it);
    }

    void render(const DrawList& list, Vec2 framebuffer_size) override {
        if (!program_) return;
        flush_deletes();
        batcher_.build(list, framebuffer_size, scale_);
        int w = batcher_.framebuffer_width(), h = batcher_.framebuffer_height();
        if (w <= 0 || h <= 0) return;

        glViewport(0, 0, w, h);
        glUseProgram(program_);
        glUniform2f(u_res_, float(w), float(h));
        glUniform1f(u_scale_, batcher_.scale());
        glUniform1i(u_tex_, 0);
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                            GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_STENCIL_TEST);
        glEnable(GL_SCISSOR_TEST);
        glActiveTexture(GL_TEXTURE0);
        glBindVertexArray(vao_);

        const auto& inst = batcher_.instances();
        if (inst.empty()) return;
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        GLsizeiptr bytes = GLsizeiptr(inst.size() * sizeof(GpuInstance));
        if (bytes > vbo_capacity_) vbo_capacity_ = bytes + bytes / 2;
        // Grow or orphan: fresh storage each frame, so the driver never
        // stalls on last frame's draws.
        glBufferData(GL_ARRAY_BUFFER, vbo_capacity_, nullptr, GL_STREAM_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, inst.data());

        GLuint bound = 0;
        for (const GpuBatch& b : batcher_.batches()) {
            if (b.scissor[2] <= 0 || b.scissor[3] <= 0) continue;
            if (b.tex_id) {
                auto it = textures_.find(std::uint32_t(b.tex_id));
                if (it == textures_.end()) continue; // never uploaded
                if (it->second != bound) {
                    glBindTexture(GL_TEXTURE_2D, it->second);
                    bound = it->second;
                }
            }
            glScissor(b.scissor[0], b.scissor[1], b.scissor[2], b.scissor[3]);
            draw_instances(b.first, b.count);
        }
        glBindVertexArray(0);
    }

    const GpuBatcher& batcher() const { return batcher_; }

private:
    // GL 3.3 has no base-instance draw: re-point the attributes at `first`.
    void draw_instances(GLint first, GLsizei count) {
        for (GLuint i = 0; i < 5; ++i)
            glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, sizeof(GpuInstance),
                                  (const void*)(std::uintptr_t)(std::size_t(first) * sizeof(GpuInstance) +
                                                                i * 4 * sizeof(float)));
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, count);
    }

    void flush_deletes() {
        if (pending_delete_.empty()) return;
        glDeleteTextures(GLsizei(pending_delete_.size()), pending_delete_.data());
        pending_delete_.clear();
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
            char log[1024];
            log[0] = 0;
            glGetShaderInfoLog(s, sizeof(log), nullptr, log);
            std::fprintf(stderr, "slikui: shader compile failed: %s\n", log);
            glDeleteShader(s);
            return 0;
        }
        return s;
    }

    GpuBatcher batcher_;
    GLuint program_ = 0, vao_ = 0, vbo_ = 0;
    GLsizeiptr vbo_capacity_ = 0;
    GLint u_res_ = -1, u_scale_ = -1, u_tex_ = -1;
    float scale_ = 1.f;
    std::unordered_map<std::uint32_t, GLuint> textures_;
    std::vector<GLuint> pending_delete_; // released ids, deleted in render()
};

} // namespace slikui::opengl
