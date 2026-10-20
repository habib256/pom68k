// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// gui_machine_window_test -- the machine window, headless.
//
// What every runner puts on screen and what gui_windows_test leaves out:
// the menu bar (drawShellFrame — Machine, Peripherals, CPU, Display,
// Windows), the « Dashboard » window, the screen window with its
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
#include "GuiSessionMenu.h"
#include "GuiTypingWindow.h"
#include "GuiScreen.h"
#include "GuiSessionState.h"
#include "GuiShellMenu.h"
#include "MachineCatalog.h"
#include "MacMemory.h"
#include "MachineFactory.h"
#include "MachineHost.h"
#include "MscCpu.h"
#include "MscMemory.h"
#include "SaveStateMachines.h"
#include "Pb030ExternalGuiProbe.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <cstring>
#include <filesystem>
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
    std::vector<std::pair<std::string, pom68k::GuestLayout>> typed;
    int cancels = 0;
    std::size_t left = 0;
    void requestTyping(std::string text, pom68k::GuestLayout layout) {
        typed.push_back({std::move(text), layout});
    }
    void requestCancelTyping() { ++cancels; left = 0; }
    std::size_t typingLeft() const { return left; }
};

// A real MachineHost over a ROM-less Duo board, so the lid travels the
// product's whole path: menu → GuiMachineControls → the host's queue →
// MscMemory/PgePmu between two quanta → the published state the menu reads
// back. The quantum emulates nothing; no ROM, no firmware.
struct NoAudio {
    bool started() const { return false; }
    size_t buffered() const { return 0; }
    size_t targetBuffered() const { return 0; }
    void pushRaw(std::vector<float>&, int) {}
    void pushFrame(std::vector<float>&, int) {}
};
struct LidMachine : MachineHost<LidMachine, MscMemory, MscCpu, NoAudio> {
    using Base = MachineHost<LidMachine, MscMemory, MscCpu, NoAudio>;
    using Base::Base;
    static constexpr bool kStereo = false;
    int64_t frameCycles() const { return 1000; }
    void emulateQuantum() { this->framesRun_++; }
    bool drainAudio() { return false; }
    void renderFrame(std::vector<uint32_t>& fb, int& w, int& h) { w = h = 1; fb.assign(1, 0); }
    void publishStatus() {}
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
    state.startup.considered = true; // This fake machine has no session ROM.
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
        check(ui.find("Machine") && ui.find("Peripherals") && ui.find("CPU") &&
              ui.find("Display") && ui.find("Windows"),
              "the menu bar shows Machine, Peripherals, CPU, Display, Windows");
        ImRect r;
        check(windowShown(kMachineControlWindowTitle, &r) && statusDraws > 0,
              "« Dashboard » is shown with the runner's status lines");
        check(r.Min.x > 600 && r.Max.y <= float(ui.height()), "the dashboard is docked in the right column, wholly on screen");
        check(ui.find("Restart") && ui.find("Pause") && ui.find("Save state"),
              "the dashboard offers Restart, Pause, Save state");
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
        check(ui.find("Change machine") != nullptr, "the menu lists « Change machine »");
        check(!ui.find("Close lid") && !ui.find("Open lid") && !state.machine.lidOpen,
              "a machine without a clamshell offers no lid item");
        check(clickMenuItem(ui, "Restart", draw), "click Restart");
        check(machine.count(FakeMachine::Cmd::HardReset) == 1, "Restart pushes HardReset");
        ui.click("Machine", draw);
        check(clickMenuItem(ui, "Pause", draw) && !machine.running.load(), "Pause stops the machine");
        ui.frame(draw);
        check(ui.find("Resume") != nullptr, "the dashboard now offers Resume");
        check(ui.click("Resume", draw) && machine.running.load(), "Resume (dashboard button) runs it again");
        ui.click("Machine", draw);
        check(clickMenuItem(ui, "Save state", draw) && (machine.state.pending() & 1),
              "Save state queues a save for the machine thread");

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
        check(ui.click("Start recording", draw) && machine.recording,
              "Start recording asks the machine to record");
        ui.click("Machine", draw);
        check(ui.click("Stop recording", draw) && !machine.recording, "and Stop recording stops it");
        capture(ui, "machine-menu");
    }

    // ── Change machine: the catalogue, one submenu per group ─────
    {
        ui.click("Machine", draw);
        const headless::Item* sw = ui.find("Change machine");
        check(sw != nullptr, "« Change machine » is offered");
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

    // The Machine menu may still be open from the catalogue section — on
    // a host with no other ROM of the group nothing was clicked there (CI's
    // runner has none) — and clicking its title then CLOSES it. Open it
    // until one of its own items is on screen.
    auto openMachineMenu = [&](const char* item) {
        for (int attempt = 0; attempt < 2; ++attempt) {
            ui.click("Machine", draw);
            if (ui.find(item)) return true;
        }
        return false;
    };

    // ── Type du texte: paste, choose the layout, type, cancel ───────
    // The window queues; MachineHost types (clipboard_typing_test). Here:
    // the menu opens it, « Coller » reads the host clipboard, the radio
    // picks the layout explicitly, « Type » hands text and layout to the
    // machine, « Cancel typing » reaches it while typing is under way.
    {
        state.machine.typing.hostClipboard = [] { return std::string("Bonjour é\n"); };
        check(openMachineMenu("Type text...") &&
                  clickMenuItem(ui, "Type text...", draw) && state.machine.typing.showWindow,
              "« Type text... » opens the typing window");
        ui.frame(draw);
        check(windowShown(kTypingWindowTitle), "the typing window is drawn");
        check(ui.click("Paste clipboard", draw) &&
                  std::string(state.machine.typing.text.data()) == "Bonjour é\n",
              "« Paste clipboard » takes the host clipboard's text");
        check(ui.click("French (AZERTY)", draw) &&
                  state.machine.typing.layout == int(pom68k::GuestLayout::FrenchAzerty),
              "the guest layout is chosen explicitly");
        check(ui.click("Type", draw) && machine.typed.size() == 1 &&
                  machine.typed[0].first == "Bonjour é\n" &&
                  machine.typed[0].second == pom68k::GuestLayout::FrenchAzerty,
              "« Type » hands the text and the layout to the machine");
        machine.left = 4;
        ui.frame(draw);
        check(ui.click("Cancel typing", draw) && machine.cancels == 1,
              "« Cancel typing » reaches the machine while typing is under way");
        state.machine.typing.showWindow = false;
        ui.frame(draw);
    }

    // ── Machine → Session: save the running machine, reopen a file ───
    // The window writes what `capture` returns through writeSessionFile
    // (which reads it back as a launch would); the menu lists the
    // directory's `.pomsession` files and opening one stages a VERBATIM
    // relaunch on `--session=<file>` (GuiSessionMenu.h).
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::absolute("gui_session_menu");
        fs::remove_all(dir);
        fs::create_directories(dir);
        const fs::path rom = dir / "plus.rom";
        const fs::path disk = dir / "boot disk.dsk";
        std::ofstream(rom, std::ios::binary) << std::string(128u << 10, '\0');
        std::ofstream(disk, std::ios::binary) << "disk";
        state.sessionFile.directory = pom68k::app::utf8FromPath(dir);
        state.sessionFile.profile = pom68k::SnapMachine::Plus;
        state.sessionFile.capture = [&] {
            pom68k::app::SessionCapture capture;
            capture.profile = pom68k::SnapMachine::Plus;
            capture.rom = pom68k::app::utf8FromPath(rom);
            capture.media = {pom68k::app::utf8FromPath(disk)};
            return capture.entries();
        };
        auto hoverSession = [&] {
            const headless::Item* item = openMachineMenu("Session") ? ui.find("Session") : nullptr;
            if (!item) return false;
            const ImVec2 c = item->bb.GetCenter();
            ui.mouseTo(c.x, c.y);
            ui.frame(draw);
            ui.frame(draw);
            return true;
        };
        check(hoverSession() && ui.find("Save session...") != nullptr &&
                  ui.find("plus") == nullptr,
              "« Session » offers the save and lists nothing in an empty directory");
        check(clickMenuItem(ui, "Save session...", draw) &&
                  state.sessionFile.showSaveWindow,
              "« Save session... » opens the save window");
        ui.frame(draw);
        const fs::path saved = dir / "plus.pomsession";
        check(windowShown(kSessionWindowTitle) &&
                  std::string(state.sessionFile.savePath.data()) ==
                      pom68k::app::utf8FromPath(saved),
              "the window proposes <directory>/<profile>.pomsession");
        check(ui.click("Save", draw) && fs::is_regular_file(saved) &&
                  state.sessionFile.status.starts_with("Session saved"),
              "« Save » writes the file and says so");
        const pom68k::app::SessionParse reread = pom68k::app::loadSession(saved);
        check(reread.session && reread.session->rom == pom68k::app::utf8FromPath(rom) &&
                  reread.session->media.size() == 1,
              "the saved file reopens with its ROM and medium");
        state.sessionFile.showSaveWindow = false;
        ui.frame(draw);

        check(hoverSession() && clickMenuItem(ui, "plus", draw) &&
                  state.relaunch.verbatim && state.relaunch.closeWindow &&
                  state.relaunch.switchArguments ==
                      std::vector<std::string>{"--session=" +
                                               pom68k::app::utf8FromPath(saved)},
              "choosing a listed session stages a verbatim --session= relaunch");
        state.relaunch = {};
        state.sessionFile.current = pom68k::app::utf8FromPath(saved);
        check(hoverSession() && clickMenuItem(ui, "plus", draw) &&
                  state.relaunch.switchArguments.empty() && !state.relaunch.closeWindow,
              "the session already open is marked, not reopened");
        ui.frame(draw);
        state.sessionFile.capture = {};
        fs::remove_all(dir);
    }

    // ── CPU menu ─────────────────────────────────────────────────────
    {
        check(ui.click("CPU", draw), "open the CPU menu");
        check(ui.find("Interpreter (Moira)") != nullptr, "the interpreter is listed");
        check(ui.click("Accelerated engine - JIT a64", draw) && engine == 1, "choosing the accelerated engine reaches the callback");
    }

    // ── Display: presets, the CRT window, the cabinet mode ─────────
    {
        check(ui.click("Display", draw), "open the Display menu");
        check(ui.click("Arcade", draw) && state.display.crtOn && state.display.crtPreset == "arcade",
              "the Arcade preset turns the CRT pass on");
        const headless::Item* screen = ui.find("screen");
        check(screen && ui.pixel(int(screen->bb.Min.x + screen->bb.GetWidth() * 0.25f),
                                 int(screen->bb.GetCenter().y)) == kWhite,
              "without a GL stack the raw screen is still shown");
        ui.click("Display", draw);
        check(ui.click("CRT settings...", draw), "open « CRT settings »");
        ui.frame(draw);
        ImRect r;
        check(windowShown("CRT settings", &r) && ui.distinctColours(r) > 12, "the CRT window is drawn");
        capture(ui, "machine-crt");
        state.display.showCrtWindow = false;

        ui.click("Display", draw);
        check(ui.click("Kiosk mode (full screen)", draw) && state.display.kiosk, "Kiosk mode switches the cabinet mode on");
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

    // ── Windows ─────────────────────────────────────────────────────
    {
        check(ui.click("Windows", draw), "open the Windows menu");
        check(clickMenuItem(ui, kMachineControlWindowTitle, draw) && !state.machine.showWindow, "« Dashboard » toggles the dashboard off");
        ui.frame(draw);
        check(!windowShown(kMachineControlWindowTitle), "the dashboard is gone");
    }

    check(ui.click("Machine", draw) &&
              clickMenuItem(ui, "Set Up a Macintosh...", draw),
          "the Machine menu opens the preparation guide");
    check(windowShown(kStartupWindowTitle) && ui.click("Cancel", draw),
          "preparing another Macintosh can be cancelled without a relaunch");

    // ── The lid: only for a machine whose host reports a clamshell ─────
    // The capability comes from the host (MachineHost::hasClamshell), not
    // from a profile check here; the click crosses the host's queue and the
    // item then follows the machine. Waking is a key, not the lid
    // (docs/DUO_BRINGUP.md milestone 6), so the dashboard says so while
    // the board sleeps.
    {
        static MscMemory duoMem(pom68k::defaultCoreConfig(), 8u << 20);
        static MscCpu duoCpu(duoMem, jit::defaultResolvedConfig(),
                             pom68k::defaultCoreConfig().cpu, false);
        duoMem.setCpu(&duoCpu);
        NoAudio silence;
        LidMachine duo(duoMem, duoCpu, silence);
        duo.publish(true);
        bindMachineControls(state.machine, duo, [] { ImGui::Text("Duo (fake)"); });
        state.machine.showWindow = true;
        check(state.machine.lidOpen && state.machine.lidOpen(),
              "a clamshell machine binds the lid, open at power-on");
        ui.click("Machine", draw);
        check(ui.find("Close lid") && !ui.find("Open lid"),
              "the Machine menu offers « Close lid »");
        capture(ui, "machine-menu-lid");
        check(clickMenuItem(ui, "Close lid", draw), "click Close lid");
        check(duoMem.clamshellOpen() && state.machine.lidOpen(),
              "the click is queued: the switch waits for the machine thread");
        duo.stepTick();
        check(!duoMem.clamshellOpen() && !state.machine.lidOpen(),
              "between two quanta the PG&E's switch reads closed, and the "
              "published state follows");
        ui.frame(draw);
        {
            // Plain text registers no test-engine item, so the line the
            // dashboard draws is read from the function that draws it.
            const char* line = lidStatusText(state.machine);
            check(line && std::string(line) == "Lid closed",
                  "the dashboard's lid line says the lid is closed");
        }
        ui.click("Machine", draw);
        check(ui.find("Open lid") && !ui.find("Close lid"),
              "the item now offers « Open lid »");
        check(clickMenuItem(ui, "Open lid", draw), "click Open lid");
        duo.stepTick();
        check(duoMem.clamshellOpen() && state.machine.lidOpen(),
              "Open lid reaches the switch the same way");

        // The System's MSCKillPower: -1 to power_cycle_w ($50FA0000,
        // PowerMgrPrimitives.a:1365-1370) halts the 68030 for a full sleep.
        for (uint32_t a = 0x50FA0000; a < 0x50FA0004; ++a) duoMem.write8(a, 0xFF);
        duo.stepTick();
        ui.frame(draw);
        const char* hint = lidStatusText(state.machine);
        check(duoMem.asleep() && state.machine.asleep() && hint &&
              std::string(hint) == "Asleep - press a key to wake",
              "a sleeping Duo tells the user a key wakes it");
        capture(ui, "dashboard-asleep");
        duo.stop();
        bindMachineControls(state.machine, machine, [] {});
        ui.frame(draw);
        check(!state.machine.lidOpen && !lidStatusText(state.machine),
              "rebinding a lidless machine drops the lid controls");
    }

    // The first-run guide uses synthetic files: dispatch and preparation,
    // never a claim that a zero-filled ROM or volume can boot Mac OS.
    {
        namespace fs = std::filesystem;
        const fs::path directory = fs::temp_directory_path() /
            ("pom68k-startup-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(directory);
        struct Cleanup {
            fs::path path;
            ~Cleanup() { std::error_code error; fs::remove_all(path, error); }
        } cleanup{directory};
        const auto rom = directory / "Chosen ROM.rom";
        const auto disk = directory / "System disk.vhd";
        const auto floppy = directory / "System 400K.dsk";
        auto write = [](const fs::path& path, std::size_t size, std::uint32_t id = 0) {
            std::vector<char> bytes(size, 0);
            for (int i = 0; i < 4; ++i) bytes[std::size_t(i)] = char(id >> (24 - i * 8));
            std::ofstream file(path, std::ios::binary);
            file.write(bytes.data(), std::streamsize(bytes.size()));
        };
        write(rom, 128u << 10, 0x4D1F8172);
        write(disk, 4096);
        write(floppy, 409600);
        GuiSessionState guide;
        auto drawGuide = [&] { drawStartupWindow(guide, pom68k::SnapMachine::Plus); };
        ui.frame(drawGuide); ui.frame(drawGuide);
        check(guide.startup.open && guide.startup.missingRom &&
                  windowShown(kStartupWindowTitle),
              "no ROM path opens the first-run guide above the demo");
        check(ui.click("Continue demo", drawGuide), "the guide lets the user continue the demo");
        ui.frame(drawGuide);
        check(!guide.startup.open && !windowShown(kStartupWindowTitle),
              "the guide does not reopen after being dismissed");
        guide.startup.considered = false;
        guide.sessionFile.romName = (directory / "missing.rom").string();
        ui.frame(drawGuide); ui.frame(drawGuide);
        check(guide.startup.open && guide.startup.missingRom &&
                  windowShown(kStartupWindowTitle),
              "an unavailable ROM path also opens the guide");
        ui.click("Continue demo", drawGuide);

        guide.startup.missingRom = false;
        guide.sessionFile.romName = rom.string();
        openStartupWindow(guide, pom68k::SnapMachine::Plus);
        ui.frame(drawGuide); ui.frame(drawGuide);
        check(ui.click("Browse ROM...", drawGuide) && ui.find("Chosen ROM.rom"),
              "the file browser lists a ROM in the current folder");
        check(ui.click("Chosen ROM.rom", drawGuide) && guide.startup.browsing == 0,
              "choosing a ROM closes the browser and fills its path");
        check(ui.click("Check setup", drawGuide) && !guide.startup.ready &&
                  guide.startup.message.find("Startup disk") != std::string::npos,
              "verification explains a missing system disk without preparing a launch");
        check(ui.click("Browse disk...", drawGuide), "open the system-disk browser");
        std::snprintf(guide.startup.folder.data(), guide.startup.folder.size(), "%s", directory.string().c_str());
        ui.frame(drawGuide);
        check(ui.click("System disk.vhd", drawGuide), "select the system-disk file");
        check(ui.click("Check setup", drawGuide) && guide.startup.ready,
              "verification accepts a profile-matching ROM and a disk file");
        check(guide.startup.arguments == std::vector<std::string>{
                  "--machine-profile=plus", rom.string(), "", disk.string()},
              "the compact disk launch reserves the floppy position explicitly");
        capture(ui, "startup-ready");
        guide.relaunch.switchArguments = {"--session=old.pomsession"};
        guide.relaunch.daynaPortId = 3;
        check(ui.click("Start Macintosh", drawGuide) &&
                  guide.relaunch.closeWindow && guide.relaunch.verbatim &&
                  guide.relaunch.switchArguments == guide.startup.arguments,
              "launch uses only the prepared profile, ROM and media, without the old session");
        ui.frame(drawGuide);

        GuiStartupState invalid;
        invalid.profile = pom68k::SnapMachine::LcII;
        std::snprintf(invalid.rom.data(), invalid.rom.size(), "%s", rom.string().c_str());
        std::snprintf(invalid.media.data(), invalid.media.size(), "%s", disk.string().c_str());
        check(!validateStartup(invalid) && invalid.message.find("Macintosh Plus") != std::string::npos,
              "a ROM from another family names the actual model instead of launching");
        invalid.profile = pom68k::SnapMachine::Mac512Ke;
        std::snprintf(invalid.media.data(), invalid.media.size(), "%s", floppy.string().c_str());
        check(validateStartup(invalid) && invalid.arguments == std::vector<std::string>{
                  "--machine-profile=mac512ke", rom.string(), floppy.string()},
              "the shared Plus ROM can prepare a floppy-only 512Ke with no SCSI argument");
        std::snprintf(invalid.media.data(), invalid.media.size(), "%s", disk.string().c_str());
        check(!validateStartup(invalid), "the floppy-only model refuses a hard disk");
        invalid.profile = pom68k::SnapMachine::Plus;
        invalid.floppy = false;
        const auto sharedRom = directory / "Shared 040.rom";
        write(sharedRom, 1u << 20, 0xFF7439EE);
        invalid.profile = pom68k::SnapMachine::Q605;
        std::snprintf(invalid.rom.data(), invalid.rom.size(), "%s", sharedRom.string().c_str());
        check(validateStartup(invalid) && invalid.arguments.front() == "--machine-profile=q605",
              "a shared 040 ROM preserves the explicitly selected Quadra 605 identity");
        invalid.profile = pom68k::SnapMachine::Plus;
        std::snprintf(invalid.rom.data(), invalid.rom.size(), "%s", rom.string().c_str());
        fs::remove(disk);
        check(!validateStartup(invalid) && invalid.arguments.empty(),
              "a disappeared disk invalidates readiness and clears the launch arguments");
        write(rom, 4096);
        check(!validateStartup(invalid) && invalid.message.find("Unsupported ROM size") != std::string::npos,
              "a truncated ROM is refused before profile decoding");
    }

    check(pb030externalgui::check(ui), "PowerBook external monitor: queued restart, published state, frame and live session capture");
    std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
