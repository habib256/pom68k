// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The driver side of uploadFrameTexture (GuiScreen.h), and the only place
// in the GUI that sends an emulated frame to OpenGL. It is its own header
// so that GuiScreen.h — the machine window, which gui_machine_window_test
// compiles and drives with a fake — needs no context at all.
//
// GL_RGBA is the INTERNAL format (what the texture stores), GL_BGRA the
// source order: the decoders pack 00RRGGBB in a little-endian word, so the
// bytes arrive B, G, R, A, and every platform's renderFrame() forces A to
// $FF because ImGui blends. GL_BGRA is OpenGL 1.2 and the Windows SDK's
// <GL/gl.h> stops at 1.1 (CHANGELOG 2026-09-18), hence the shim.

#pragma once

#include <GLFW/glfw3.h>

#include <cstdint>

#ifdef _WIN32
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#endif

namespace pom68k::gui {

struct GlTextureHost {
    void bindTexture(unsigned int texture) const {
        glBindTexture(GL_TEXTURE_2D, GLuint(texture));
    }
    void uploadBgra(int w, int h, const std::uint32_t* pixels) const {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                     GL_BGRA, GL_UNSIGNED_BYTE, pixels);
    }
};

} // namespace pom68k::gui
