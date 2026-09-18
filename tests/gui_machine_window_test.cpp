// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// gui_machine_window_test -- the machine window, headless.
//
// What every runner puts on screen and what gui_windows_test leaves out:
// the menu bar (drawShellFrame — Machine, Périphériques, CPU, Affichage,
// Fenêtres), the « Tableau de bord » window, the screen window with its
// mouse surface (ScreenInput) and the compact keyboard, the cabinet mode
// and the CRT presets. The machine is a fake that records what the GUI
// pushes; the screen is a texture the harness draws for the runner's GL
// name, so a pixel of the emulated screen can be read back where the
// window put it. The GLFW side (monitor switch, cursor capture, close) is
// a fake host here and GuiShell.cpp in the product.

#define POM68K_IMGUI_HEADLESS_HOOKS
#include "ImGuiHeadless.h"

#include "Cpu68k.h"
#include "DockLayout.h"
#include "GuiMachineControls.h"
#include "GuiScreen.h"
#include "GuiSessionState.h"
#include "GuiShellMenu.h"
#include "MachineCatalog.h"
#include "MacMemory.h"
#include "MachineFactory.h"
#include "SaveStateMachines.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

using namespace pom68k::gui;

namespace {

int failures = 0;
void check(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

bool windowShown(const char* title, ImRect* rect = nullptr) {
    ImGuiWindow* w = ImGui::FindWindowByName(title);
    if (!w || !w->WasActive || w->Hidden) return false;
    if (rect) *rect = w->Rect();
    return true;
}

// A menu item by label, distinguished from a window title or button that
// reads the same: the one drawn inside an open menu popup ("##Menu_NN").
template <class Draw>
bool clickMenuItem(headless::Context& ui, const char* label, Draw&& draw) {
    for (int nth = 0; nth < 8; nth++) {
        const headless::Item* it = ui.find(label, nth);
        if (!it) break;
        const ImGuiContext& g = *ImGui::GetCurrentContext();
        for (ImGuiWindow* w : g.Windows) {
            if (!w->WasActive || !(w->Flags & ImGuiWindowFlags_Popup)) continue;
            if (w->Rect().Contains(it->bb.GetCenter())) return ui.clickAt(it->bb.GetCenter(), draw);
        }
    }
    std::printf("  click: no menu item labelled '%s' in an open menu\n", label);
    return false;
}

void capture(headless::Context& ui, const char* name) {
    const std::string path = std::string("gui_") + name + ".ppm";
    if (ui.savePpm(path)) std::printf("  capture %s\n", path.c_str());
}

// The machine as the GUI sees it: MachineHost's command queue, atomics,
// save slot and recording surface — nothing behind them.
struct FakeMachine {
    struct Cmd {
        enum T { MouseMove, MouseButton, Key, HardReset, CpuEngine } t;
        int a = 0, b = 0;
    };
    std::vector<Cmd> cmds;
    void push(Cmd c) { cmds.push_back(c); }
    int count(Cmd::T t) const {
        int n = 0;
        for (const Cmd& c : cmds) if (c.t == t) n++;
        return n;
    }
    const Cmd* last(Cmd::T t, int a = -1) const {
        for (size_t i = cmds.size(); i-- > 0;)
            if (cmds[i].t == t && (a < 0 || cmds[i].a == a)) return &cmds[i];
        return nullptr;
    }
    std::atomic<bool> running{true}, turbo{false};
    SaveStateSlot state;
    bool recording = false;
    bool recordingActive() const { return recording; }
    void requestRecordingStart(std::string = {}) { recording = true; }
    void requestRecordingStop() { recording = false; }
    std::string recordingMessage() { return recording ? "enregistrement en cours" : ""; }
};

// The window's side of ScreenInput: what GlfwScreenHost reads from GLFW.
struct HostState {
    bool middle = false, left = false, right = false, grab = false;
    double cx = 100, cy = 100;
    bool captured = false;
    int captureCalls = 0;
};
struct FakeHost {
    HostState* s;
    bool middleButton() const { return s->middle; }
    bool leftButton() const { return s->left; }
    bool rightButton() const { return s->right; }
    bool grabChord() const { return s->grab; }
    void cursorPos(double& x, double& y) const { x = s->cx; y = s->cy; }
    void setCursorCaptured(bool on) const { s->captured = on; s->captureCalls++; }
};

// The driver side of uploadFrameTexture (GuiScreen.h): GlTextureHost in
// the product, here a recorder that also feeds the harness's own texture
// store — so the pixels the runner would have sent through GL are the
// pixels this window is then checked to draw.
struct TextureCalls {
    int binds = 0, uploads = 0;
    unsigned int tex = 0;
    int w = 0, h = 0;
    std::uint32_t first = 0;
};
struct FakeTextureHost {
    TextureCalls* calls;
    headless::Context* ui = nullptr;
    void bindTexture(unsigned int t) const { calls->binds++; calls->tex = t; }
    void uploadBgra(int w, int h, const std::uint32_t* px) const {
        calls->uploads++; calls->w = w; calls->h = h; calls->first = px[0];
        if (ui) ui->setTexture(calls->tex, w, h,
                               std::vector<std::uint32_t>(px, px + size_t(w) * size_t(h)));
    }
};

constexpr std::uintptr_t kScreenTex = 0x1000;
constexpr int kW = 512, kH = 342;
constexpr std::uint32_t kWhite = 0xFFFFFFFFu, kRed = 0xFF0000FFu;

} // namespace

int main() {
    std::printf("gui_machine_window_test — the machine window, headless\n");
    // The runner opens 1100x800; the dashboard's first position assumes it.
    headless::Context ui(1100, 800);
    pom68k::dockLayoutInit();   // what GuiWindowSession does before the first frame
    // The emulated screen: left half white, right half red, 512x342 — the
    // frame a runner latches. It reaches the texture through the SAME call
    // the six runners make (uploadFrameTexture), so the pixel check further
    // down reads back what the product's upload put there.
    TextureCalls texCalls;
    {
        std::vector<std::uint32_t> px(size_t(kW) * kH);
        for (int y = 0; y < kH; y++)
            for (int x = 0; x < kW; x++) px[size_t(y) * kW + x] = x < kW / 2 ? kWhite : kRed;
        check(uploadFrameTexture(FakeTextureHost{&texCalls, &ui},
                                 unsigned(kScreenTex), px, kW, kH),
              "a published frame reaches the texture");
        check(texCalls.binds == 1 && texCalls.uploads == 1 &&
              texCalls.tex == unsigned(kScreenTex) &&
              texCalls.w == kW && texCalls.h == kH && texCalls.first == kWhite,
              "the upload carries the runner's texture name, the published "
              "geometry and the published pixels");
    }

    // ── What the upload REFUSES ──────────────────────────────────────
    // glTexImage2D takes a bare pointer and a geometry and reads w × h × 4
    // bytes from it: a frame published shorter than its own geometry is an
    // overread with no diagnostic anywhere. Four of the six runners had no
    // guard at all and two guarded only the dimensions; the one function
    // they now share refuses all three shapes before the driver sees them.
    {
        TextureCalls t;
        const std::vector<std::uint32_t> none;
        const std::vector<std::uint32_t> full(size_t(kW) * kH, kWhite);
        const std::vector<std::uint32_t> shortOfIt(size_t(kW) * kH - 1, kWhite);
        check(!uploadFrameTexture(FakeTextureHost{&t}, 7, none, kW, kH) &&
              t.binds == 0, "an empty frame is not uploaded");
        check(!uploadFrameTexture(FakeTextureHost{&t}, 7, full, 0, kH) &&
              !uploadFrameTexture(FakeTextureHost{&t}, 7, full, kW, 0) &&
              t.binds == 0, "a zero geometry is not uploaded");
        check(!uploadFrameTexture(FakeTextureHost{&t}, 7, shortOfIt, kW, kH) &&
              t.binds == 0,
              "a frame shorter than its own geometry is refused, not read past");
        check(uploadFrameTexture(FakeTextureHost{&t}, 7, full, kW, kH) &&
              t.binds == 1 && t.uploads == 1 && t.tex == 7,
              "and the frame that does hold its geometry goes through");
    }

    GuiSessionState state;
    FakeMachine machine;
    int statusDraws = 0;
    bindMachineControls(state.machine, machine, [&] {
        statusDraws++;
        ImGui::Text("68000 @ 7.8336 MHz (fake)  PC=%06X", 0x400000);
    });
    int engine = 0;
    state.cpu.setCpuEngine = [&](int e) { engine = e; };
    state.cpu.getCpuEngine = [&] { return engine; };
    state.cpu.jitBackend = "a64";
    state.cpu.jitStats = [] { return jit::Stats::Snapshot{}; };

    HostState host;
    ScreenInput input;
    CompactKeyboard keyboard;
    const char* screenTitle = "Macintosh Plus";
    auto draw = [&] {
        drawShellFrame(state, pom68k::SnapMachine::Plus);
        ImGui::SetNextWindowPos(ImVec2(20, 40), ImGuiCond_FirstUseEver);
        screenWindowBegin(state.display, screenTitle);
        input.frame(state.display, FakeHost{&host}, unsigned(kScreenTex),
                    ImVec2(float(kW * 2), float(kH * 2)),
                    [&](int dx, int dy) { machine.push({FakeMachine::Cmd::MouseMove, dx, dy}); },
                    [&](int button, bool down) {
                        machine.push({FakeMachine::Cmd::MouseButton, button, down ? 1 : 0});
                    });
        ImGui::End();
        keyboard.frame(machine, [](std::uint8_t, bool) {});
    };
    for (int i = 0; i < 4; i++) ui.frame(draw);   // the dock layout settles

    // ── The bar, the dashboard and the screen ────────────────────────
    ImRect screenRect;
    {
        check(ui.find("Machine") && ui.find("Périphériques") && ui.find("CPU") &&
              ui.find("Affichage") && ui.find("Fenêtres"),
              "the menu bar shows Machine, Périphériques, CPU, Affichage, Fenêtres");
        ImRect r;
        check(windowShown(kMachineControlWindowTitle, &r) && statusDraws > 0,
              "« Tableau de bord » is shown with the runner's status lines");
        check(r.Min.x > 600 && r.Max.y <= float(ui.height()), "the dashboard is docked in the right column, wholly on screen");
        check(ui.find("Redémarrer") && ui.find("Pause") && ui.find("Sauver l'état"),
              "the dashboard offers Redémarrer, Pause, Sauver l'état");
        check(windowShown(screenTitle, &r), "the screen window is shown");
        const headless::Item* screen = ui.find("screen");
        check(screen != nullptr, "the screen surface is drawn");
        if (screen) {
            screenRect = screen->bb;
            const float w = screenRect.GetWidth(), h = screenRect.GetHeight();
            const std::uint32_t left = ui.pixel(int(screenRect.Min.x + w * 0.25f), int(screenRect.Min.y + h * 0.5f));
            const std::uint32_t right = ui.pixel(int(screenRect.Min.x + w * 0.75f), int(screenRect.Min.y + h * 0.5f));
            check(left == kWhite && right == kRed,
                  "the emulated screen's pixels land where the window put them (white | red)");
            check(std::fabs(w / h - float(kW) / float(kH)) < 0.02f, "the screen keeps its 512:342 ratio");
        }
        capture(ui, "machine-window");
    }

    // ── The mouse surface: hover, move, click, capture ───────────────
    {
        const ImVec2 c = screenRect.GetCenter();
        ui.mouseTo(c.x, c.y);
        ui.frame(draw);
        ui.frame(draw);
        const float zoom = input.zoom;
        check(zoom > 0.1f, "the surface knows its zoom");
        machine.cmds.clear();
        ui.mouseTo(c.x + 10.25f * zoom, c.y + 6.25f * zoom);
        ui.frame(draw);
        const FakeMachine::Cmd* mv = machine.last(FakeMachine::Cmd::MouseMove);
        check(mv && mv->a == 10 && mv->b == 6, "a hover move of 10.25x6.25 Mac pixels reaches the machine as (10, 6)");
        ui.mouseDown();
        ui.frame(draw);
        const FakeMachine::Cmd* bt = machine.last(FakeMachine::Cmd::MouseButton, 0);
        check(bt && bt->a == 0 && bt->b == 1, "a left press reaches the machine");
        ui.mouseUp();
        ui.frame(draw);
        bt = machine.last(FakeMachine::Cmd::MouseButton, 0);
        check(bt && bt->a == 0 && bt->b == 0, "the release follows");

        // Middle click over the screen: hard capture; the host's cursor
        // then drives the mouse and Delete releases.
        host.middle = true;
        ui.frame(draw);
        host.middle = false;
        check(input.captured && host.captured, "a middle click over the screen captures the cursor");
        check((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NoMouse) != 0, "ImGui gives the mouse up while captured");
        machine.cmds.clear();
        host.cx += 20.25 * zoom;
        host.cy += 4.25 * zoom;
        ui.frame(draw);
        mv = machine.last(FakeMachine::Cmd::MouseMove);
        check(mv && mv->a == 20 && mv->b == 4, "captured: the host cursor's delta reaches the machine as (20, 4)");
        host.left = true;
        ui.frame(draw);
        bt = machine.last(FakeMachine::Cmd::MouseButton, 0);
        check(bt && bt->a == 0 && bt->b == 1, "captured: the host's left button reaches the machine");
        host.left = false;
        ui.key(ImGuiKey_Delete, true);
        ui.frame(draw);
        ui.key(ImGuiKey_Delete, false);
        ui.frame(draw);
        check(!input.captured && !host.captured, "Delete releases the capture");
        check((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NoMouse) == 0, "ImGui has the mouse back");
    }

    // ── The keyboard ─────────────────────────────────────────────────
    {
        machine.cmds.clear();
        ui.key(ImGuiKey_A, true);
        ui.frame(draw);
        const FakeMachine::Cmd* k = machine.last(FakeMachine::Cmd::Key);
        check(k && k->a == 0x00 && k->b == 1, "host A down reaches the machine as Mac key $00 down");
        ui.key(ImGuiKey_A, false);
        ui.frame(draw);
        k = machine.last(FakeMachine::Cmd::Key);
        check(k && k->a == 0x00 && k->b == 0, "host A up follows");
    }

    // ── Machine menu: reset, pause, save state, recording ────────────
    {
        machine.cmds.clear();
        check(ui.click("Machine", draw), "open the Machine menu");
        check(ui.find("Changer de machine") != nullptr, "the menu lists « Changer de machine »");
        check(clickMenuItem(ui, "Redémarrer", draw), "click Redémarrer");
        check(machine.count(FakeMachine::Cmd::HardReset) == 1, "Redémarrer pushes HardReset");
        ui.click("Machine", draw);
        check(clickMenuItem(ui, "Pause", draw) && !machine.running.load(), "Pause stops the machine");
        ui.frame(draw);
        check(ui.find("Reprendre") != nullptr, "the dashboard now offers Reprendre");
        check(ui.click("Reprendre", draw) && machine.running.load(), "Reprendre (dashboard button) runs it again");
        ui.click("Machine", draw);
        check(clickMenuItem(ui, "Sauver l'état", draw) && (machine.state.pending() & 1),
              "Sauver l'état queues a save for the machine thread");

        // ── And the other half of the pass: the FILE ────────────────────
        // The click above only proves the request reached the slot. What
        // the machine thread does with it — write a snapshot, atomically,
        // and read it back — was outside every GUI gate until now, so the
        // same slot is handed to a REAL machine here (a Plus with no ROM:
        // a snapshot carries RAM and devices, never the ROM).
        {
            const std::string path = "gui_machine_window_state.pomss";
            std::remove(path.c_str());
            std::remove((path + ".tmp").c_str());
            pom68k::CoreConfig core;
            MacMemory mem(core, MacMemory::Model::Plus);
            Cpu68k cpu(mem, jit::defaultResolvedConfig());
            machine.state.kind = pom68k::SnapMachine::Plus;
            machine.state.setPath(path);
            // With no ROM the overlay still maps the ROM image over low
            // memory, where writes drop; RAM answers at its alias (the
            // classic compact overlay), so the marker goes there.
            mem.write8(0x600000, 0x5A);

            const int saved = machine.state.apply(mem, cpu);
            check((saved & 1) != 0, "the machine thread takes the queued save");
            std::ifstream f(path, std::ios::binary | std::ios::ate);
            const long size = f ? long(f.tellg()) : -1;
            check(size > 0, "and a state file is on disk afterwards");
            check(!std::ifstream(path + ".tmp").good(),
                  "written through a temp file that no longer exists");
            check(machine.state.pending() == 0, "the slot is empty again");

            // Change the machine, then load: the byte must come back.
            mem.write8(0x600000, 0xA5);
            machine.state.request(true);
            const int restored = machine.state.apply(mem, cpu);
            check((restored & 2) != 0, "a queued load is taken too");
            check(mem.read8(0x600000) == 0x5A,
                  "and the guest's RAM is what the snapshot held");

            // A corrupt file is refused with a message, not a crash: this
            // is a user-facing path (they pick the file).
            { std::ofstream bad(path, std::ios::binary);
              bad << "not a snapshot at all"; }
            machine.state.request(true);
            const int refused = machine.state.apply(mem, cpu);
            check((refused & 2) == 0 && !machine.state.message().empty(),
                  "a corrupt file is refused, and the slot says why");
            std::remove(path.c_str());
        }
        ui.click("Machine", draw);
        check(ui.click("Démarrer l'enregistrement", draw) && machine.recording,
              "Démarrer l'enregistrement asks the machine to record");
        ui.click("Machine", draw);
        check(ui.click("Arrêter l'enregistrement", draw) && !machine.recording, "and Arrêter stops it");
        capture(ui, "machine-menu");
    }

    // ── Changer de machine: the catalogue, one submenu per group ─────
    {
        ui.click("Machine", draw);
        const headless::Item* sw = ui.find("Changer de machine");
        check(sw != nullptr, "« Changer de machine » is offered");
        if (sw) {
            const ImVec2 c = sw->bb.GetCenter();
            ui.mouseTo(c.x, c.y);
            ui.frame(draw);
            ui.frame(draw);
            const char* group = pom68k::kMachineProfiles[0].group;
            const headless::Item* g = ui.find(group);
            check(g != nullptr, "the first catalogue group is a submenu");
            if (g) {
                const ImVec2 gc = g->bb.GetCenter();
                ui.mouseTo(gc.x, gc.y);
                ui.frame(draw);
                ui.frame(draw);
                check(ui.find("Macintosh Plus") != nullptr, "the group lists the current profile");
                const pom68k::MachineProfile* other = nullptr;
                for (const pom68k::MachineProfile& p : pom68k::kMachineProfiles) {
                    if (std::string(p.group) != group || p.snapshot == pom68k::SnapMachine::Plus) continue;
                    std::string path = pom68k::app::MachineFactory::findPath(p.romPath);
                    if (path.empty() && p.romCrc32)
                        path = pom68k::app::MachineFactory::findRomBySignature(p.romCrc32);
                    if (!path.empty()) { other = &p; break; }
                }
                if (other) {
                    check(ui.click(other->label, draw) &&
                          state.relaunch.targetProfile == other->snapshot &&
                          state.relaunch.closeWindow,
                          "choosing a profile with a ROM stages the switch and asks the window to close");
                    state.relaunch.closeWindow = false;
                    state.relaunch.targetProfile.reset();
                } else {
                    std::printf("  note: no other ROM of group %s on this host - the switch click is not exercised\n", group);
                }
            }
        }
        ui.frame(draw);
    }

    // ── CPU menu ─────────────────────────────────────────────────────
    {
        check(ui.click("CPU", draw), "open the CPU menu");
        check(ui.find("Interpréteur (Moira)") != nullptr, "the interpreter is listed");
        check(ui.click("Moteur accéléré - JIT a64", draw) && engine == 1, "choosing the accelerated engine reaches the callback");
    }

    // ── Affichage: presets, the CRT window, the cabinet mode ─────────
    {
        check(ui.click("Affichage", draw), "open the Affichage menu");
        check(ui.click("Arcade", draw) && state.display.crtOn && state.display.crtPreset == "arcade",
              "the Arcade preset turns the CRT pass on");
        const headless::Item* screen = ui.find("screen");
        check(screen && ui.pixel(int(screen->bb.Min.x + screen->bb.GetWidth() * 0.25f),
                                 int(screen->bb.GetCenter().y)) == kWhite,
              "without a GL stack the raw screen is still shown");
        ui.click("Affichage", draw);
        check(ui.click("Réglages CRT...", draw), "open « Réglages CRT »");
        ui.frame(draw);
        ImRect r;
        check(windowShown("Réglages CRT", &r) && ui.distinctColours(r) > 12, "the CRT window is drawn");
        capture(ui, "machine-crt");
        state.display.showCrtWindow = false;

        ui.click("Affichage", draw);
        check(ui.click("Mode borne (plein écran)", draw) && state.display.kiosk, "« Mode borne » switches the cabinet mode on");
        ui.frame(draw);
        check(ui.find("Machine") == nullptr && !windowShown(kMachineControlWindowTitle), "kiosk: no menu bar, no dashboard");
        check(kioskActive(), "kiosk: the process-wide flag is set for the runner's windows");
        screen = ui.find("screen");
        const Letterbox box = letterbox(float(ui.width()), float(ui.height()), float(kW * 2), float(kH * 2));
        check(screen && std::fabs(screen->bb.Min.x - box.x) < 1.5f && std::fabs(screen->bb.Min.y - box.y) < 1.5f &&
              std::fabs(screen->bb.GetWidth() - box.w) < 1.5f && std::fabs(screen->bb.GetHeight() - box.h) < 1.5f,
              "kiosk: the screen fills the monitor, letterboxed and centred");
        check(input.captured && host.captured, "kiosk: the cursor is captured");
        check(screen && ui.pixel(int(box.x + box.w * 0.25f), int(box.y + box.h * 0.5f)) == kWhite &&
              ui.pixel(int(box.x + box.w * 0.75f), int(box.y + box.h * 0.5f)) == kRed &&
              ui.pixel(4, 4) != kWhite && ui.pixel(4, 4) != kRed,
              "kiosk: the screen's pixels are on the monitor, the bezel is not");
        capture(ui, "machine-kiosk");
        // Ctrl+Alt+F leaves.
        ui.key(ImGuiKey_LeftCtrl, true);
        ui.key(ImGuiKey_LeftAlt, true);
        ui.frame(draw);
        ui.key(ImGuiKey_F, true);
        ui.frame(draw);
        ui.key(ImGuiKey_F, false);
        ui.key(ImGuiKey_LeftAlt, false);
        ui.key(ImGuiKey_LeftCtrl, false);
        ui.frame(draw);
        ui.frame(draw);
        check(!state.display.kiosk, "Ctrl+Alt+F switches the cabinet mode off");
        check(!kioskActive(), "the process-wide flag follows");
        check(ui.find("Machine") != nullptr, "the menu bar is back");
        check(!input.captured && !host.captured, "leaving the kiosk releases the capture it imposed");
        // Alt+F4 in kiosk asks the window to close.
        state.display.kiosk = true;
        ui.frame(draw);
        ui.key(ImGuiKey_LeftAlt, true);
        ui.frame(draw);
        ui.key(ImGuiKey_F4, true);
        ui.frame(draw);
        check(state.relaunch.closeWindow, "kiosk: Alt+F4 asks the window to close");
        ui.key(ImGuiKey_F4, false);
        ui.key(ImGuiKey_LeftAlt, false);
        state.relaunch.closeWindow = false;
        state.display.kiosk = false;
        ui.frame(draw);
    }

    // ── Fenêtres ─────────────────────────────────────────────────────
    {
        check(ui.click("Fenêtres", draw), "open the Fenêtres menu");
        check(clickMenuItem(ui, kMachineControlWindowTitle, draw) && !state.machine.showWindow, "« Tableau de bord » toggles the dashboard off");
        ui.frame(draw);
        check(!windowShown(kMachineControlWindowTitle), "the dashboard is gone");
    }

    std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
