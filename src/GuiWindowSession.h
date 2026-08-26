// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// RAII owner for the native handles shared by every GUI runner. close() is
// idempotent so native runners may release the window before a process relaunch;
// the destructor is the fallback for every early return and exception path.

#pragma once

#include "DiskBays.h"
#include "DockLayout.h"
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <cstdio>
#include <utility>

class GuiWindowSession {
public:
    GuiWindowSession() = default;
    GuiWindowSession(const GuiWindowSession&) = delete;
    GuiWindowSession& operator=(const GuiWindowSession&) = delete;
    ~GuiWindowSession() { close(); }

    template <class ConfigureOpenGl>
    bool open(int width, int height, const char* title,
              ConfigureOpenGl&& configureOpenGl, bool visible = true) {
        if (!glfwInit()) {
            std::fprintf(stderr, "GLFW init failed\n");
            return false;
        }
        glfwInitialized_ = true;
        const char* glslVersion =
            std::forward<ConfigureOpenGl>(configureOpenGl)();
        glfwWindowHint(GLFW_VISIBLE, visible ? GLFW_TRUE : GLFW_FALSE);
        window_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
        if (!window_) return false;

        glfwMakeContextCurrent(window_);
        // A hidden smoke window is not presented by the compositor on every
        // host (notably macOS), so waiting for its vertical blank can stall
        // forever. Visible product windows retain normal vsync pacing.
        glfwSwapInterval(visible ? 1 : 0);
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        imguiContext_ = true;
        ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;
        ImGui::StyleColorsDark();
        pom68k::dockLayoutInit();
        ImGui_ImplGlfw_InitForOpenGL(window_, true);
        glfwBackend_ = true;
        ImGui_ImplOpenGL3_Init(glslVersion);
        openGlBackend_ = true;
        pom68k::diskBaysInstallDrop(window_);

        glGenTextures(1, &texture_);
        glBindTexture(GL_TEXTURE_2D, texture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        return true;
    }

    void close() noexcept {
        if (texture_) {
            glDeleteTextures(1, &texture_);
            texture_ = 0;
        }
        if (openGlBackend_) {
            ImGui_ImplOpenGL3_Shutdown();
            openGlBackend_ = false;
        }
        if (glfwBackend_) {
            ImGui_ImplGlfw_Shutdown();
            glfwBackend_ = false;
        }
        if (imguiContext_) {
            ImGui::DestroyContext();
            imguiContext_ = false;
        }
        if (window_) {
            glfwDestroyWindow(window_);
            window_ = nullptr;
        }
        if (glfwInitialized_) {
            glfwTerminate();
            glfwInitialized_ = false;
        }
    }

    GLFWwindow* window() const noexcept { return window_; }
    GLuint texture() const noexcept { return texture_; }

private:
    GLFWwindow* window_ = nullptr;
    GLuint texture_ = 0;
    bool glfwInitialized_ = false;
    bool imguiContext_ = false;
    bool glfwBackend_ = false;
    bool openGlBackend_ = false;
};
