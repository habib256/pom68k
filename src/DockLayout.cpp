// DockLayout -- see DockLayout.h for the shape of the shell.

#include "DockLayout.h"

#include "imgui.h"
#include "imgui_internal.h"     // DockBuilder* lives in the internal API

#include <cstring>
#include <string>

namespace pom68k {

const char* kDiskWindowTitle = "Bibliothèque de disques";

namespace {

bool        gRebuild = true;        // first frame lays out (unless imgui.ini
                                    // already carries a layout — see below)
bool        gFirstLayout = true;    // consulted once, on the first real frame
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
    gFirstLayout = false;              // an explicit reset outranks the file
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

    // ── Persist the arrangement the moment it changes ────────────────────
    // ImGui's own auto-save runs on a timer (`io.IniSavingRate`, 5 s), so a
    // window dragged and the emulator closed a second later lost the whole
    // layout — which from the user's seat is "POM68K does not remember my
    // docking". Writing on the flag instead costs one small file write per
    // gesture and makes every exit correct: the twelve runner shutdowns, the
    // relaunch a machine switch performs, and a crash alike, without a
    // teardown hook in each of them.
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantSaveIniSettings && io.IniFilename) {
        ImGui::SaveIniSettingsToDisk(io.IniFilename);
        io.WantSaveIniSettings = false;
    }

    // Nothing to lay out until the runner has named its screen window; that
    // happens later in the same frame, so the first split lands on frame 2.
    if (!gRebuild || gLastScreen.empty()) return;
    const char* screenWindow = gLastScreen.c_str();

    // ── …and do not clobber it on the way back in ────────────────────────
    // Saving is only half of remembering. `gRebuild` starts true, so the
    // first real frame used to re-split unconditionally and threw away the
    // layout ImGui had just restored from imgui.ini. Ask the settings
    // instead: a window the file knows comes back carrying the dock node it
    // was in, and that is the arrangement the user made. Only the first
    // frame consults it — afterwards `gRebuild` means what it says (an
    // explicit « Réinitialiser », or a machine switch onto a screen-window
    // name the file has never seen, which must be laid out).
    if (gFirstLayout) {
        gFirstLayout = false;
        const ImGuiWindowSettings* saved =
            ImGui::FindWindowSettingsByID(ImHashStr(screenWindow));
        if (saved && saved->DockId != 0) {
            gRebuild = false;
            return;
        }
    }
    gRebuild = false;

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
