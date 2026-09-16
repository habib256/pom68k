// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// See OpenGLShader.h.

#include "OpenGLShader.h"
#include "GlEntryPoints.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace pom68k::gui {
namespace {

struct GlslDialect {
    const char* version;
    const char* precision;
};

// Richest first. Some stacks do NOT expose 1.50 — the Raspberry Pi's V3D
// under Mesa stops at 1.40 — hence the cascade rather than a hard-coded
// version.
std::vector<GlslDialect> glslDialects() {
    const char* kEsPrecision = "precision highp float;\nprecision highp int;\n";
    const char* sl = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));
    if (sl && std::strstr(sl, "ES ") != nullptr)
        return {{"#version 300 es\n", kEsPrecision}};
    int major = 0, minor = 0;
    if (sl) std::sscanf(sl, "%d.%d", &major, &minor);
    if (minor < 10) minor *= 10;
    const int ver = major * 100 + minor;
    std::vector<GlslDialect> out;
    if (ver == 0 || ver >= 150) out.push_back({"#version 150\n", "\n"});
    if (ver == 0 || ver >= 140) out.push_back({"#version 140\n", "\n"});
    if (ver == 0 || ver >= 130) out.push_back({"#version 130\n", "\n"});
    if (ver == 0 || ver >= 120) out.push_back({"#version 120\n", "\n"});
    if (out.empty()) out.push_back({"#version 120\n", "\n"});
    return out;
}

unsigned int compileOne(unsigned int kind, const char* versionLine, const char* precisionLine,
                        const char* body, std::string* errorOut, bool quiet) {
    unsigned int sh = glCreateShader(kind);
    if (!sh) {
        if (errorOut) *errorOut = "glCreateShader returned 0";
        return 0;
    }
    const char* parts[3] = {versionLine, precisionLine, body};
    glShaderSource(sh, 3, parts, nullptr);
    glCompileShader(sh);
    int ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = {0};
        int len = 0;
        glGetShaderInfoLog(sh, sizeof(log) - 1, &len, log);
        std::string msg = "shader compile failed: ";
        msg.append(log, size_t(len));
        if (errorOut) *errorOut = msg;
        if (!quiet) std::fprintf(stderr, "[CRT] %s\n", msg.c_str());
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

} // namespace

unsigned int compileShaderProgram(const char* vertexBody, const char* fragmentBody,
                                  std::string* errorOut) {
    if (!pom68k::gl::load()) {
        if (errorOut) *errorOut = "GL 3.x entry points unavailable";
        std::fprintf(stderr, "[CRT] GL 3.x entry points unavailable — CRT effects disabled\n");
        return 0;
    }
    const std::vector<GlslDialect> dialects = glslDialects();
    const char* versionLine = nullptr;
    unsigned int vs = 0, fs = 0;
    for (std::size_t i = 0; i < dialects.size(); ++i) {
        const bool last = (i + 1 == dialects.size());
        vs = compileOne(GL_VERTEX_SHADER, dialects[i].version, dialects[i].precision,
                        vertexBody, errorOut, !last);
        if (vs) {
            fs = compileOne(GL_FRAGMENT_SHADER, dialects[i].version, dialects[i].precision,
                            fragmentBody, errorOut, !last);
            if (fs) { versionLine = dialects[i].version; break; }
            glDeleteShader(vs);
            vs = 0;
        }
    }
    if (!versionLine) return 0;
    if (errorOut) errorOut->clear();
    {
        const char* sl = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));
        std::string chosen(versionLine + std::strlen("#version "));
        while (!chosen.empty() && chosen.back() == '\n') chosen.pop_back();
        std::fprintf(stderr, "[CRT] GLSL %s (driver: %s)\n", chosen.c_str(), sl ? sl : "?");
    }
    unsigned int prog = glCreateProgram();
    if (!prog) {
        glDeleteShader(vs);
        glDeleteShader(fs);
        if (errorOut) *errorOut = "glCreateProgram returned 0";
        return 0;
    }
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "aPos");
    glLinkProgram(prog);
    int ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!ok) {
        char log[2048] = {0};
        int len = 0;
        glGetProgramInfoLog(prog, sizeof(log) - 1, &len, log);
        std::string msg = "shader link failed: ";
        msg.append(log, size_t(len));
        if (errorOut) *errorOut = msg;
        std::fprintf(stderr, "[CRT] %s\n", msg.c_str());
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

} // namespace pom68k::gui
