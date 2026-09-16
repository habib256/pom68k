// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The one GLFW dependency of the Disques window: dropping files onto the
// application window. Kept apart from DiskBays.cpp so the window itself
// links without a window system (gui_windows_test renders it headless).

#include "DiskBays.h"

#include <GLFW/glfw3.h>

namespace pom68k {

void diskBaysInstallDrop(GLFWwindow* window) {
    glfwSetDropCallback(window, [](GLFWwindow*, int count, const char** paths) {
        for (int i = 0; i < count; i++) diskBaysOfferDroppedImage(paths[i]);
    });
}

} // namespace pom68k
