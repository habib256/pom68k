// DockLayout -- see DockLayout.h for the shape of the shell.

#include "DockLayout.h"

#include "imgui.h"
#include "imgui_internal.h"     // DockBuilder* lives in the internal API

#include <cstring>
#include <string>

namespace pom68k {

const char* kDiskWindowTitle = "Bibliothèque de disques";

namespace {

bool        gRebuild = true;        // first frame always lays out
std::string gLastScreen;            // rebuild when the machine changes

} // namespace

void dockLayoutInit()
{
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // Undocking a window into its own OS window is deliberately NOT enabled
    // (ImGuiConfigFlags_ViewportsEnable): the emulator already owns one GLFW
    // window and a second GL context per viewport is a needless risk on the
    // machines this runs on.
}

void dockLayoutReset()
{
    gRebuild = true;
}

void dockLayoutMenu()
{
    if (!ImGui::BeginMenu("Fenêtres")) return;
    if (ImGui::MenuItem("Réinitialiser la disposition"))
        dockLayoutReset();
    ImGui::EndMenu();
}

void dockLayoutScreenWindow(const char* title)
{
    if (!title || !*title) return;
    // A machine switch renames the screen window; the old node would keep a
    // window that no longer exists, so re-split when the name moves.
    if (gLastScreen != title) {
        gLastScreen = title;
        gRebuild = true;
    }
}

void dockLayoutFrame()
{
    const ImGuiID root = ImGui::DockSpaceOverViewport(
        0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

    // Nothing to lay out until the runner has named its screen window; that
    // happens later in the same frame, so the first split lands on frame 2.
    if (!gRebuild || gLastScreen.empty()) return;
    gRebuild = false;
    const char* screenWindow = gLastScreen.c_str();

    // Rebuild from scratch: screen on the left, disk library on the right.
    ImGui::DockBuilderRemoveNode(root);
    ImGui::DockBuilderAddNode(root, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(root, ImGui::GetMainViewport()->WorkSize);

    // The library needs a real column: a file name, its size, and a button
    // on the same row. 0.72 clipped "<vide>" to "vid" at 1348 px wide, so the
    // split is driven by a pixel floor and only falls back to a ratio on very
    // wide screens.
    const float total = ImGui::GetMainViewport()->WorkSize.x;
    float ratio = 0.72f;
    if (total > 0.0f) {
        const float wanted = 420.0f;                 // measured, not guessed
        ratio = 1.0f - wanted / total;
        if (ratio < 0.45f) ratio = 0.45f;            // never starve the screen
        if (ratio > 0.80f) ratio = 0.80f;
    }
    ImGuiID right = 0;
    ImGuiID left  = ImGui::DockBuilderSplitNode(root, ImGuiDir_Left, ratio,
                                                nullptr, &right);

    ImGui::DockBuilderDockWindow(screenWindow, left);
    ImGui::DockBuilderDockWindow(kDiskWindowTitle, right);
    ImGui::DockBuilderFinish(root);
}

} // namespace pom68k
