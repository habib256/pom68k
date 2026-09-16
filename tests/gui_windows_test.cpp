// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Gate: the four product windows, drawn by their real functions with no
// window system (tests/ImGuiHeadless.h), driven by injected clicks, and
// captured to PPM for the eye.
//
// Until 2026-09-16 the GUI had one gate, gui_smoke_test — a hidden GLFW
// window that skips on every runner without a GL surface and never looked
// at a window's contents. The AppleTalk/Ethernet window with its DaynaPort
// selector and the services form had never been RENDERED anywhere but on
// the author's screen (TODO § Services réseau). This gate renders them
// everywhere.
//
// What is asserted is behaviour above the pure functions the component
// tests already cover: a window opens when its contract says so, has a
// size, is not blank; a click on a radio stages a change, a click on
// « Appliquer » hands the typed override to the host callback. What is
// captured — gui_<window>.ppm beside the binary — is the layout, for the
// dated manual pass DEV.md § 6 still owes.

#define POM68K_IMGUI_HEADLESS_HOOKS
#include "ImGuiHeadless.h"

#include "DiskBays.h"
#include "GuiEngineWindow.h"
#include "GuiSessionState.h"
#include "NetworkWindow.h"
#include "PeripheralWindow.h"
#include "AssetFingerprint.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace {
int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-70s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}

// Every labelled item of the last frame — the map a new scenario is
// written from (POM68K_GUI_LABELS=1).
void dumpLabels(const char* when) {
    if (!getenv("POM68K_GUI_LABELS")) return;
    std::printf("  labels [%s]:\n", when);
    for (const auto& [id, it] : headless::items())
        if (!it.label.empty() && it.visible)
            std::printf("    %08X (%4.0f,%4.0f %3.0fx%3.0f) '%s'\n", id, it.bb.Min.x, it.bb.Min.y,
                        it.bb.GetWidth(), it.bb.GetHeight(), it.label.c_str());
}

// A window as ImGui keeps it: present, active this frame, with a size.
bool windowShown(const char* title, ImRect* rect = nullptr) {
    ImGuiWindow* w = ImGui::FindWindowByName(title);
    if (!w || !w->WasActive || w->Collapsed || w->Hidden) return false;
    if (w->Size.x < 100 || w->Size.y < 40) return false;
    if (rect) *rect = w->Rect();
    return true;
}

void capture(headless::Context& ui, const char* name) {
    const std::string path = std::string("gui_") + name + ".ppm";
    if (!ui.savePpm(path)) std::printf("  (could not write %s)\n", path.c_str());
}
} // namespace

int main() {
    std::printf("gui_windows_test — the product windows, headless\n");
    headless::Context ui(1024, 768);

    // ── Périphériques (LLE / HLE) ────────────────────────────────────
    {
        pom68k::lle::Registry reg;
        reg.beginSession();
        pom68k::lle::Device cuda;
        cuda.module = pom68k::lle::HleEgretCuda;
        cuda.target = pom68k::FirmwareTarget::Cuda;
        cuda.name = "Cuda - MCU ADB / PRAM / horloge";
        cuda.knob = "POM68K_CUDA_LLE";
        cuda.mode = pom68k::lle::Mode::Lle;
        cuda.why = pom68k::lle::Why::LleFirmware;
        cuda.firmware = "roms/cuda/341s0788.bin";
        cuda.candidates = {"roms/cuda/341s0788.bin", "../roms/cuda/341s0788.bin"};
        cuda.pathKnob = "POM68K_CUDA_FW";
        reg.report(cuda);
        pom68k::lle::Device toby;
        toby.module = pom68k::lle::HleTobyDeclRom;
        toby.target = pom68k::FirmwareTarget::TobyDecl;
        toby.name = "Toby - ROM de déclaration de la carte vidéo Macintosh II (342-0008-a)";
        toby.knob = "POM68K_TOBY_DECL_LLE";
        toby.mode = pom68k::lle::Mode::Hle;
        toby.why = pom68k::lle::Why::HleNoDump;
        toby.candidates = {"gui_windows_test.nodump/342-0008-a.bin"};
        toby.pathKnob = "POM68K_TOBY_DECL";
        toby.consequence = "Sans le dump 342-0008-a : System 6 et 7.0 démarrent, "
                           "System 7.5.5 reste sur « Welcome to Macintosh ».";
        reg.report(toby);

        std::vector<pom68k::FirmwareOverride> applied;
        int relaunches = 0;
        pom68k::PeripheralHost host;
        host.registry = &reg;
        host.relaunch = [&](std::vector<pom68k::FirmwareOverride> o) {
            applied = std::move(o);
            relaunches++;
        };
        auto draw = [&] { pom68k::peripheralWindow(host); };
        ui.frame(draw);
        ui.frame(draw);
        ImRect r;
        check(windowShown(pom68k::kPeripheralWindowTitle, &r),
              "Périphériques opens by itself on a machine with an HLE substitute");
        check(ui.distinctColours(r) > 12, "the window is drawn, not blank");
        check(ui.find("LLE (composant d'origine)", 0) && ui.find("HLE (substitut, non conformant)", 1),
              "both rows show their LLE / HLE radios");
        check(ui.find("Chemin", 0) != nullptr, "the Cuda row offers its dump path field");
        capture(ui, "peripherals");
        dumpLabels("peripherals");

        // Stage HLE on the Cuda row (first HLE radio), then apply.
        check(ui.click("HLE (substitut, non conformant)", draw, 0), "click the Cuda row's HLE radio");
        check(ui.find("Appliquer et redémarrer") != nullptr,
              "a staged change shows « Appliquer et redémarrer »");
        capture(ui, "peripherals-staged");
        check(ui.click("Appliquer et redémarrer", draw), "click « Appliquer et redémarrer »");
        bool cudaHle = false, tobyKept = false;
        for (const pom68k::FirmwareOverride& o : applied) {
            if (o.target == pom68k::FirmwareTarget::Cuda && !o.lle) cudaHle = true;
            if (o.target == pom68k::FirmwareTarget::TobyDecl && !o.lle) tobyKept = true;
        }
        check(relaunches == 1 && applied.size() == 2 && cudaHle && tobyKept,
              "the host receives the complete typed set: Cuda HLE, Toby unchanged");
        // Annuler-less reopen: staging was dropped with the apply.
        ui.frame(draw);
        check(ui.find("Appliquer et redémarrer") == nullptr, "nothing pending after the apply");
    }

    // ── AppleTalk / Ethernet ─────────────────────────────────────────
    {
        GuiNetworkState state;
        state.showWindow = true;
        state.ethernetScsiId = -1;
        state.scsiOccupied = 0b0000'0001;      // a disk on SCSI 0
        std::optional<std::optional<int>> staged;
        state.relaunchWithDaynaPort = [&](std::optional<int> id) { staged = id; };
        auto draw = [&] { pom68k::gui::drawAppleTalkWindow(state); };
        ui.frame(draw);
        ui.frame(draw);
        ImRect r;
        check(windowShown(pom68k::gui::kNetworkWindowTitle, &r),
              "AppleTalk / Ethernet is shown when the state says so");
        check(ui.distinctColours(r) > 12, "the window is drawn, not blank");
        capture(ui, "network");
        dumpLabels("network");
        // The selector: open the combo (no label of its own), pick ID 3,
        // apply — the host receives the typed choice.
        const ImGuiID combo = ui.idIn(pom68k::gui::kNetworkWindowTitle, "Carte au prochain démarrage");
        check(ui.clickId(combo, draw), "open the DaynaPort selector");
        dumpLabels("network-combo");
        check(ui.find("ID SCSI 2") && ui.find("ID SCSI 6"), "the selector lists SCSI 2..6");
        check(ui.click("ID SCSI 3", draw), "choose ID SCSI 3");
        capture(ui, "network-staged");
        check(ui.click("Appliquer et redémarrer", draw), "click « Appliquer et redémarrer »");
        check(staged && *staged && **staged == 3, "the host is asked to relaunch with the card at SCSI 3");
    }

    // ── Disques ──────────────────────────────────────────────────────
    {
        const std::string dropped = "gui_windows_test_dropped.dsk";
        { std::ofstream f(dropped, std::ios::binary); f << std::string(819200, '\0'); }
        pom68k::DiskBaysHost host;
        host.romName = "roms/macplus.rom";
        host.bootPath = "hdv/boot.vhd";
        std::vector<std::string> extras;
        host.extras = &extras;
        host.hasFloppyDrive = true;
        bool floppyIn = false;
        std::string inserted;
        host.floppyInserted = [&] { return floppyIn; };
        host.insertFloppy = [&](const std::string& p) {
            inserted = p;
            floppyIn = true;
            host.floppyPath = p;       // what a runner does after the insert
        };
        host.ejectFloppy = [&] { floppyIn = false; };
        check(!pom68k::diskBaysOfferDroppedImage("notes.txt"), "a dropped non-image is ignored");
        check(pom68k::diskBaysOfferDroppedImage(dropped), "a dropped .dsk is accepted and opens the window");
        auto draw = [&] { pom68k::diskBaysWindow(host); };
        ui.frame(draw);
        ui.frame(draw);
        ImRect r;
        check(windowShown(pom68k::kDiskWindowTitle, &r), "Disques is shown after a drop");
        check(ui.distinctColours(r) > 12, "the window is drawn, not blank");
        capture(ui, "disks");
        dumpLabels("disks");
        const ImGuiID fd = ui.idIn(pom68k::kDiskWindowTitle, "##fdpick");
        check(ui.clickId(fd, draw), "open the internal floppy picker");
        dumpLabels("disks-combo");
        const headless::Item* row = ui.findContaining("gui_windows_test_dropped");
        check(row != nullptr, "the dropped floppy is offered by the picker");
        if (row) ui.clickAt(row->bb.GetCenter(), draw);
        check(floppyIn && inserted == dropped, "choosing it inserts it live through the host hook");
        check(ui.find("Éjecter##fd") != nullptr, "the row now offers « Éjecter »");
        capture(ui, "disks-inserted");
        std::remove(dropped.c_str());
    }

    // ── Moteur accéléré ──────────────────────────────────────────────
    {
        GuiCpuPanelState st;
        st.showJit = true;
        int engine = 1, sets = 0, last = -1;
        st.getCpuEngine = [&] { return engine; };
        st.setCpuEngine = [&](int e) { engine = e; sets++; last = e; };
        st.jitStats = [] { jit::Stats::Snapshot s{}; s.instrs = 1000; s.interpInstrs = 10; return s; };
        st.speedSample = [] { return std::pair<long long, long long>{0, 0}; };
        st.jitBackend = "a64";
        auto draw = [&] { pom68k::gui::drawEngineWindow(st); };
        ui.frame(draw);
        ui.frame(draw);
        ImRect r;
        check(windowShown("Moteur accéléré", &r), "Moteur accéléré is shown when the state says so");
        check(ui.distinctColours(r) > 12, "the window is drawn, not blank");
        capture(ui, "engine");
        dumpLabels("engine");
    }

    // ── Glyphs: every non-ASCII character in the windows' string literals
    //    exists in the font in use. ImGui's default font has no em-dash
    //    and no arrows — they rendered as '?' in the product until
    //    2026-09-16 — so the sources spell them in ASCII, and this keeps
    //    the next one out.
    {
        // The windows, plus the device names the boards report to the
        // Périphériques window (fw::Request::name) — those are drawn too.
        const char* sources[] = {"src/PeripheralWindow.cpp", "src/NetworkWindow.cpp",
                                 "src/DiskBays.cpp", "src/GuiEngineWindow.cpp",
                                 "src/GuiShell.cpp", "src/GuiMachineControls.cpp",
                                 "src/AdbVia.cpp", "src/V8Memory.cpp", "src/Q605Memory.cpp",
                                 "src/Q630Memory.cpp", "src/RbvMemory.cpp", "src/Q700Memory.cpp",
                                 "src/VaspMemory.cpp", "src/SonoraMemory.cpp", "src/TobyDeclChoice.h"};
        auto windowSource = [](const std::string& rel) {
            return rel.find("Window") != std::string::npos || rel.find("DiskBays") != std::string::npos ||
                   rel.find("GuiShell") != std::string::npos || rel.find("GuiMachineControls") != std::string::npos;
        };
        int missing = 0, scanned = 0;
        ui.frame([&] {
            ImFontBaked* baked = ImGui::GetFontBaked();
            check(baked->FindGlyphNoFallback(0x2014) == nullptr,
                  "the font in use has no em-dash (so the sources must not use one)");
            for (const char* rel : sources) {
                const std::string path = testasset::find(rel);
                if (path.empty()) { std::printf("  (%s not found)\n", rel); continue; }
                std::ifstream in(path, std::ios::binary);
                const std::string text((std::istreambuf_iterator<char>(in)), {});
                // Walk string literals; decode UTF-8; ask the font. In a
                // board file only the `name =` lines are window text.
                const bool whole = windowSource(rel);
                for (size_t i = 0; i < text.size(); i++) {
                    if (text[i] != '"') continue;
                    if (!whole) {
                        const size_t bol = text.rfind('\n', i);
                        const std::string line = text.substr(bol == std::string::npos ? 0 : bol, i - (bol == std::string::npos ? 0 : bol));
                        if (line.find("name =") == std::string::npos) {
                            for (i++; i < text.size() && text[i] != '"'; i++) if (text[i] == '\\') i++;
                            continue;
                        }
                    }
                    for (i++; i < text.size() && text[i] != '"'; i++) {
                        if (text[i] == '\\') { i++; continue; }
                        const unsigned char c = (unsigned char)text[i];
                        if (c < 0x80) continue;
                        unsigned cp = 0; int extra = 0;
                        if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
                        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
                        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
                        for (int k = 0; k < extra && i + 1 < text.size(); k++) cp = (cp << 6) | ((unsigned char)text[++i] & 0x3F);
                        scanned++;
                        if (!baked->FindGlyphNoFallback(ImWchar(cp))) {
                            if (missing < 8) std::printf("  %s: U+%04X has no glyph\n", rel, cp);
                            missing++;
                        }
                    }
                }
            }
        });
        std::printf("  glyphs: %d non-ASCII characters scanned, %d without a glyph\n", scanned, missing);
        check(missing == 0, "every non-ASCII character the windows show has a glyph");
    }

    std::printf("%s\n", gFails ? "FAILED" : "PASSED");
    return gFails ? 1 : 0;
}
