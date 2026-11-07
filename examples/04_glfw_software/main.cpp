// examples/04_glfw_software/main.cpp — GLFW window + CPU rasterizer.
//
// Demonstrates the dual-backend story: the exact same widget tree rendered by
// the software rasterizer into a raw pixel buffer, then blitted through a
// single GL texture. Useful for engines that want UI pixels without wiring
// the full GL renderer.
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/gl3.h> // GL first: silenced deprecations, no legacy gl.h
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <chrono>
#include <silk/backends/software/software.hpp>
#include <silk/silk.hpp>
#include <silk/text/system_fonts.hpp>

#include <vector>

// Minimal core-profile blit shader (the only GL this example needs).
static const char* kVs = R"(
#version 330 core
layout(location=0) in vec2 a_pos;
layout(location=1) in vec2 a_uv;
out vec2 v_uv;
void main(){ v_uv=a_uv; gl_Position=vec4(a_pos,0,1); }
)";
static const char* kFs = R"(
#version 330 core
in vec2 v_uv;
uniform sampler2D u_tex;
out vec4 frag;
void main(){ vec4 c = texture(u_tex, v_uv); frag = vec4(c.rgb, 1.0); }
)";

static GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    return s;
}

int main() {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    GLFWwindow* win = glfwCreateWindow(640, 420, "silk software blit", nullptr, nullptr);
    if (!win) return 1;
    glfwMakeContextCurrent(win);

    silk::software::Target target(640, 420);   // owned raw pixel buffer
    silk::software::Sink sink(target);
    silk::Context ctx(sink);
    ctx.load_font_file(silk::find_system_font());

    int clicks = 0;
    auto& root = ctx.set_root<silk::Column>();
    root.set_padding(20).set_gap(10);
    root.add<silk::Label>("Software rasterizer in a window").set_font_size(20);
    root.add<silk::Label>("Every pixel was blended on the CPU into a uint32_t buffer,");
    root.add<silk::Label>("then uploaded as one texture and blitted.");
    auto& status = root.add<silk::Label>("clicks: 0");
    root.add<silk::Button>("Click me").on_click([&] {
        status.set_text("clicks: " + std::to_string(++clicks));
    });

    // Blit program.
    GLuint vs = compile(GL_VERTEX_SHADER, kVs);
    GLuint fs = compile(GL_FRAGMENT_SHADER, kFs);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    glUseProgram(prog);

    GLuint vao, vbo, tex;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    float quad[] = { // clip-space pos + uv; v=0 is the top row of the CPU pixels
        -1, 1, 0, 0,   1, 1, 1, 0,  -1, -1, 0, 1,
        1, 1, 1, 0,    1, -1, 1, 1,  -1, -1, 0, 1,
    };
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, (void*)8);
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    auto last = std::chrono::steady_clock::now();
    double now = 0;
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        int w = 0, h = 0;
        glfwGetFramebufferSize(win, &w, &h);
        if (w != target.width() || h != target.height()) {
            target.resize(w, h); // re-allocates (owned mode)
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        }
        int ww = 0, wh = 0;
        glfwGetWindowSize(win, &ww, &wh);
        float ui = ww > 0 ? float(w) / float(ww) : 1.f;
        ctx.theme().scale = ui;
        sink.set_scale(ui);
        ctx.set_frame_size({float(ww), float(wh)}); // LOGICAL frame: layout in points

        auto pt = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(pt - last).count();
        last = pt;
        now += dt;

        ctx.tick(silk::Time{now, silk::clamp_dt(dt)});
        ctx.render(); // rasterize into target

        // Blit raw pixels through one texture.
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, target.width(), target.height(), 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, target.data());
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glfwSwapBuffers(win);
    }
    return 0;
}
