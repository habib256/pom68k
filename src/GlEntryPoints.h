// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// GlEntryPoints -- the OpenGL 2.0/3.0 functions the CRT pass needs, on every
// host, without GLEW/GLAD or <GL/glext.h>.
//
//   * macOS: the GUI runs a 3.2 core context and <OpenGL/gl3.h> declares
//     everything directly.
//   * Linux / Windows: <GL/gl.h> is 1.1; the rest is resolved once through
//     glfwGetProcAddress (GLFW is linked everywhere) into the pointers below,
//     with the typedefs written here so the MSVC SDK's missing glext.h is not
//     a build dependency.
//
// Included by CrtEffectStack.cpp and OpenGLShader.cpp only.

#pragma once

#if defined(__APPLE__)
#  define GL_SILENCE_DEPRECATION 1
#  include <OpenGL/gl3.h>
namespace pom68k::gl {
inline bool load() { return true; }
}
#else
#  include <GLFW/glfw3.h>
#  include <GL/gl.h>
#  include <cstddef>

#ifndef APIENTRY
#  define APIENTRY
#endif
#ifndef GL_FRAMEBUFFER
#  define GL_FRAMEBUFFER 0x8D40
#  define GL_COLOR_ATTACHMENT0 0x8CE0
#  define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#  define GL_FRAMEBUFFER_BINDING 0x8CA6
#endif
#ifndef GL_ARRAY_BUFFER
#  define GL_ARRAY_BUFFER 0x8892
#  define GL_STATIC_DRAW 0x88E4
#endif
#ifndef GL_TEXTURE0
#  define GL_TEXTURE0 0x84C0
#  define GL_TEXTURE1 0x84C1
#endif
#ifndef GL_FRAGMENT_SHADER
#  define GL_FRAGMENT_SHADER 0x8B30
#  define GL_VERTEX_SHADER 0x8B31
#  define GL_COMPILE_STATUS 0x8B81
#  define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_SHADING_LANGUAGE_VERSION
#  define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#endif
#ifndef GL_CLAMP_TO_EDGE
#  define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_RGBA8
#  define GL_RGBA8 0x8058
#endif
// The four the CRT pass reads back to leave the host's GL state exactly as
// it found it. Mesa's <GL/gl.h> pulls <GL/glext.h> in by default, so Linux
// never noticed they were missing here; the Windows SDK's gl.h stops at 1.1
// and the release build is the only job that compiles with it (found on the
// 0.3.0 dry run, 2026-09-18).
#ifndef GL_ACTIVE_TEXTURE
#  define GL_ACTIVE_TEXTURE 0x84E0          // 1.3
#endif
#ifndef GL_ARRAY_BUFFER_BINDING
#  define GL_ARRAY_BUFFER_BINDING 0x8894    // 1.5
#endif
#ifndef GL_CURRENT_PROGRAM
#  define GL_CURRENT_PROGRAM 0x8B8D         // 2.0
#endif
#ifndef GL_VERTEX_ARRAY_BINDING
#  define GL_VERTEX_ARRAY_BINDING 0x85B5    // 3.0
#endif

namespace pom68k::gl {
typedef char GLcharT;
typedef std::ptrdiff_t GLsizeiptrT;
typedef void (APIENTRY *PFN_GenFramebuffers)(GLsizei, GLuint*);
typedef void (APIENTRY *PFN_BindFramebuffer)(GLenum, GLuint);
typedef void (APIENTRY *PFN_FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum (APIENTRY *PFN_CheckFramebufferStatus)(GLenum);
typedef void (APIENTRY *PFN_DeleteFramebuffers)(GLsizei, const GLuint*);
typedef void (APIENTRY *PFN_GenVertexArrays)(GLsizei, GLuint*);
typedef void (APIENTRY *PFN_BindVertexArray)(GLuint);
typedef void (APIENTRY *PFN_DeleteVertexArrays)(GLsizei, const GLuint*);
typedef void (APIENTRY *PFN_GenBuffers)(GLsizei, GLuint*);
typedef void (APIENTRY *PFN_BindBuffer)(GLenum, GLuint);
typedef void (APIENTRY *PFN_BufferData)(GLenum, GLsizeiptrT, const void*, GLenum);
typedef void (APIENTRY *PFN_DeleteBuffers)(GLsizei, const GLuint*);
typedef void (APIENTRY *PFN_EnableVertexAttribArray)(GLuint);
typedef void (APIENTRY *PFN_VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
typedef void (APIENTRY *PFN_UseProgram)(GLuint);
typedef GLint (APIENTRY *PFN_GetUniformLocation)(GLuint, const GLcharT*);
typedef void (APIENTRY *PFN_Uniform1i)(GLint, GLint);
typedef void (APIENTRY *PFN_Uniform1f)(GLint, GLfloat);
typedef void (APIENTRY *PFN_Uniform2f)(GLint, GLfloat, GLfloat);
typedef void (APIENTRY *PFN_ActiveTexture)(GLenum);
typedef GLuint (APIENTRY *PFN_CreateShader)(GLenum);
typedef void (APIENTRY *PFN_ShaderSource)(GLuint, GLsizei, const GLcharT* const*, const GLint*);
typedef void (APIENTRY *PFN_CompileShader)(GLuint);
typedef void (APIENTRY *PFN_GetShaderiv)(GLuint, GLenum, GLint*);
typedef void (APIENTRY *PFN_GetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLcharT*);
typedef void (APIENTRY *PFN_DeleteShader)(GLuint);
typedef GLuint (APIENTRY *PFN_CreateProgram)(void);
typedef void (APIENTRY *PFN_AttachShader)(GLuint, GLuint);
typedef void (APIENTRY *PFN_LinkProgram)(GLuint);
typedef void (APIENTRY *PFN_GetProgramiv)(GLuint, GLenum, GLint*);
typedef void (APIENTRY *PFN_GetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLcharT*);
typedef void (APIENTRY *PFN_DeleteProgram)(GLuint);
typedef void (APIENTRY *PFN_BindAttribLocation)(GLuint, GLuint, const GLcharT*);

struct Table {
    PFN_GenFramebuffers genFramebuffers = nullptr;
    PFN_BindFramebuffer bindFramebuffer = nullptr;
    PFN_FramebufferTexture2D framebufferTexture2D = nullptr;
    PFN_CheckFramebufferStatus checkFramebufferStatus = nullptr;
    PFN_DeleteFramebuffers deleteFramebuffers = nullptr;
    PFN_GenVertexArrays genVertexArrays = nullptr;
    PFN_BindVertexArray bindVertexArray = nullptr;
    PFN_DeleteVertexArrays deleteVertexArrays = nullptr;
    PFN_GenBuffers genBuffers = nullptr;
    PFN_BindBuffer bindBuffer = nullptr;
    PFN_BufferData bufferData = nullptr;
    PFN_DeleteBuffers deleteBuffers = nullptr;
    PFN_EnableVertexAttribArray enableVertexAttribArray = nullptr;
    PFN_VertexAttribPointer vertexAttribPointer = nullptr;
    PFN_UseProgram useProgram = nullptr;
    PFN_GetUniformLocation getUniformLocation = nullptr;
    PFN_Uniform1i uniform1i = nullptr;
    PFN_Uniform1f uniform1f = nullptr;
    PFN_Uniform2f uniform2f = nullptr;
    PFN_ActiveTexture activeTexture = nullptr;
    PFN_CreateShader createShader = nullptr;
    PFN_ShaderSource shaderSource = nullptr;
    PFN_CompileShader compileShader = nullptr;
    PFN_GetShaderiv getShaderiv = nullptr;
    PFN_GetShaderInfoLog getShaderInfoLog = nullptr;
    PFN_DeleteShader deleteShader = nullptr;
    PFN_CreateProgram createProgram = nullptr;
    PFN_AttachShader attachShader = nullptr;
    PFN_LinkProgram linkProgram = nullptr;
    PFN_GetProgramiv getProgramiv = nullptr;
    PFN_GetProgramInfoLog getProgramInfoLog = nullptr;
    PFN_DeleteProgram deleteProgram = nullptr;
    PFN_BindAttribLocation bindAttribLocation = nullptr;
    bool loaded = false;
};

inline Table& table() { static Table t; return t; }

inline bool load() {
    Table& t = table();
    if (t.loaded) return true;
    auto get = [](const char* n) { return reinterpret_cast<void*>(glfwGetProcAddress(n)); };
#define POM68K_GL_LOAD(field, type, name) t.field = reinterpret_cast<type>(get(name))
    POM68K_GL_LOAD(genFramebuffers, PFN_GenFramebuffers, "glGenFramebuffers");
    POM68K_GL_LOAD(bindFramebuffer, PFN_BindFramebuffer, "glBindFramebuffer");
    POM68K_GL_LOAD(framebufferTexture2D, PFN_FramebufferTexture2D, "glFramebufferTexture2D");
    POM68K_GL_LOAD(checkFramebufferStatus, PFN_CheckFramebufferStatus, "glCheckFramebufferStatus");
    POM68K_GL_LOAD(deleteFramebuffers, PFN_DeleteFramebuffers, "glDeleteFramebuffers");
    POM68K_GL_LOAD(genVertexArrays, PFN_GenVertexArrays, "glGenVertexArrays");
    POM68K_GL_LOAD(bindVertexArray, PFN_BindVertexArray, "glBindVertexArray");
    POM68K_GL_LOAD(deleteVertexArrays, PFN_DeleteVertexArrays, "glDeleteVertexArrays");
    POM68K_GL_LOAD(genBuffers, PFN_GenBuffers, "glGenBuffers");
    POM68K_GL_LOAD(bindBuffer, PFN_BindBuffer, "glBindBuffer");
    POM68K_GL_LOAD(bufferData, PFN_BufferData, "glBufferData");
    POM68K_GL_LOAD(deleteBuffers, PFN_DeleteBuffers, "glDeleteBuffers");
    POM68K_GL_LOAD(enableVertexAttribArray, PFN_EnableVertexAttribArray, "glEnableVertexAttribArray");
    POM68K_GL_LOAD(vertexAttribPointer, PFN_VertexAttribPointer, "glVertexAttribPointer");
    POM68K_GL_LOAD(useProgram, PFN_UseProgram, "glUseProgram");
    POM68K_GL_LOAD(getUniformLocation, PFN_GetUniformLocation, "glGetUniformLocation");
    POM68K_GL_LOAD(uniform1i, PFN_Uniform1i, "glUniform1i");
    POM68K_GL_LOAD(uniform1f, PFN_Uniform1f, "glUniform1f");
    POM68K_GL_LOAD(uniform2f, PFN_Uniform2f, "glUniform2f");
    POM68K_GL_LOAD(activeTexture, PFN_ActiveTexture, "glActiveTexture");
    POM68K_GL_LOAD(createShader, PFN_CreateShader, "glCreateShader");
    POM68K_GL_LOAD(shaderSource, PFN_ShaderSource, "glShaderSource");
    POM68K_GL_LOAD(compileShader, PFN_CompileShader, "glCompileShader");
    POM68K_GL_LOAD(getShaderiv, PFN_GetShaderiv, "glGetShaderiv");
    POM68K_GL_LOAD(getShaderInfoLog, PFN_GetShaderInfoLog, "glGetShaderInfoLog");
    POM68K_GL_LOAD(deleteShader, PFN_DeleteShader, "glDeleteShader");
    POM68K_GL_LOAD(createProgram, PFN_CreateProgram, "glCreateProgram");
    POM68K_GL_LOAD(attachShader, PFN_AttachShader, "glAttachShader");
    POM68K_GL_LOAD(linkProgram, PFN_LinkProgram, "glLinkProgram");
    POM68K_GL_LOAD(getProgramiv, PFN_GetProgramiv, "glGetProgramiv");
    POM68K_GL_LOAD(getProgramInfoLog, PFN_GetProgramInfoLog, "glGetProgramInfoLog");
    POM68K_GL_LOAD(deleteProgram, PFN_DeleteProgram, "glDeleteProgram");
    POM68K_GL_LOAD(bindAttribLocation, PFN_BindAttribLocation, "glBindAttribLocation");
#undef POM68K_GL_LOAD
    t.loaded = t.genFramebuffers && t.bindFramebuffer && t.framebufferTexture2D &&
               t.checkFramebufferStatus && t.deleteFramebuffers && t.genVertexArrays &&
               t.bindVertexArray && t.deleteVertexArrays && t.genBuffers && t.bindBuffer &&
               t.bufferData && t.deleteBuffers && t.enableVertexAttribArray &&
               t.vertexAttribPointer && t.useProgram && t.getUniformLocation &&
               t.uniform1i && t.uniform1f && t.uniform2f && t.activeTexture &&
               t.createShader && t.shaderSource && t.compileShader && t.getShaderiv &&
               t.getShaderInfoLog && t.deleteShader && t.createProgram && t.attachShader &&
               t.linkProgram && t.getProgramiv && t.getProgramInfoLog && t.deleteProgram &&
               t.bindAttribLocation;
    return t.loaded;
}
} // namespace pom68k::gl

// The GL names the two translation units use, routed to the table.
#define glGenFramebuffers        pom68k::gl::table().genFramebuffers
#define glBindFramebuffer        pom68k::gl::table().bindFramebuffer
#define glFramebufferTexture2D   pom68k::gl::table().framebufferTexture2D
#define glCheckFramebufferStatus pom68k::gl::table().checkFramebufferStatus
#define glDeleteFramebuffers     pom68k::gl::table().deleteFramebuffers
#define glGenVertexArrays        pom68k::gl::table().genVertexArrays
#define glBindVertexArray        pom68k::gl::table().bindVertexArray
#define glDeleteVertexArrays     pom68k::gl::table().deleteVertexArrays
#define glGenBuffers             pom68k::gl::table().genBuffers
#define glBindBuffer             pom68k::gl::table().bindBuffer
#define glBufferData             pom68k::gl::table().bufferData
#define glDeleteBuffers          pom68k::gl::table().deleteBuffers
#define glEnableVertexAttribArray pom68k::gl::table().enableVertexAttribArray
#define glVertexAttribPointer    pom68k::gl::table().vertexAttribPointer
#define glUseProgram             pom68k::gl::table().useProgram
#define glGetUniformLocation     pom68k::gl::table().getUniformLocation
#define glUniform1i              pom68k::gl::table().uniform1i
#define glUniform1f              pom68k::gl::table().uniform1f
#define glUniform2f              pom68k::gl::table().uniform2f
#define glActiveTexture          pom68k::gl::table().activeTexture
#define glCreateShader           pom68k::gl::table().createShader
#define glShaderSource           pom68k::gl::table().shaderSource
#define glCompileShader          pom68k::gl::table().compileShader
#define glGetShaderiv            pom68k::gl::table().getShaderiv
#define glGetShaderInfoLog       pom68k::gl::table().getShaderInfoLog
#define glDeleteShader           pom68k::gl::table().deleteShader
#define glCreateProgram          pom68k::gl::table().createProgram
#define glAttachShader           pom68k::gl::table().attachShader
#define glLinkProgram            pom68k::gl::table().linkProgram
#define glGetProgramiv           pom68k::gl::table().getProgramiv
#define glGetProgramInfoLog      pom68k::gl::table().getProgramInfoLog
#define glDeleteProgram          pom68k::gl::table().deleteProgram
#define glBindAttribLocation     pom68k::gl::table().bindAttribLocation
#endif
