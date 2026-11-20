// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The one GLFW dependency of the Disques window: dropping files onto the
// application window. Kept apart from DiskBays.cpp so the window itself
// links without a window system (gui_windows_test renders it headless).
// It is the application's only drop callback: `diskBaysRouteDrop`
// (DiskBays.cpp) sends each path to the Disks window or the ROM window.

#include "DiskBays.h"

#include <GLFW/glfw3.h>

namespace pom68k {

void diskBaysInstallDrop(GLFWwindow* window) {
    glfwSetDropCallback(window, [](GLFWwindow*, int count, const char** paths) {
        for (int i = 0; i < count; i++) diskBaysRouteDrop(paths[i]);
    });
}

} // namespace pom68k
