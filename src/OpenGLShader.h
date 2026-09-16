// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// OpenGLShader -- compile and link one (vertex, fragment) GLSL program.
// Ported from NeoST (gui/OpenGLShader, itself from POM2) on 2026-09-16, on
// the entry points of GlEntryPoints.h. The `#version` line is chosen at run
// time from GL_SHADING_LANGUAGE_VERSION and tried in cascade 150 → 140 → 130
// → 120 (« 300 es » on a GLES context): a driver that stops at 1.40, like
// the Raspberry Pi's V3D under Mesa, is served too. Bodies are passed without
// their #version line.

#pragma once

#include <string>

namespace pom68k::gui {

// Returns the GL program object, 0 on failure; compile/link errors land in
// `errorOut` (cleared on success).
unsigned int compileShaderProgram(const char* vertexBody, const char* fragmentBody,
                                  std::string* errorOut = nullptr);

} // namespace pom68k::gui
