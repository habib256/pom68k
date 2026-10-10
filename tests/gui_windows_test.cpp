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
#include "GuiDebuggerWindow.h"
#include "GuiDisplay.h"
#include "GuiEngineWindow.h"
#include "GuiSerialWindow.h"
#include "GuiSessionState.h"
#include "NetworkWindow.h"
#include "PeripheralWindow.h"
#include "AssetFingerprint.h"
#include "MacMemory.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
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

// The machine thread's side of the debugger, without a machine: a CPU that
// sits at $00002000 and records what the session asked of it.
struct FakeDebugTarget final : pom68k::dbg::Target {
    std::vector<std::uint32_t> bps;
    int steps = 0;
    void capture(pom68k::dbg::Snapshot& s) const override {
        s.model = "68040";
        s.regs.pc = 0x2000;
        s.regs.sr = 0x2700;
        s.regs.d[0] = 0x12345678;
        s.supervisor = true;
        s.requestedEngine = 1;
        s.effectiveEngine = bps.empty() ? 1 : 0;
    }
    std::int64_t clock() const override { return 1000; }
    std::uint32_t pc() const override { return 0x2000; }
    void readMemory(pom68k::dbg::Space, std::uint32_t addr, std::uint8_t* out,
                    pom68k::dbg::ByteState* st, std::size_t n) override {
        for (std::size_t i = 0; i < n; ++i) {
            out[i] = std::uint8_t(addr + i);
            st[i] = (addr + i) & 0x80 ? pom68k::dbg::ByteState::NotMemory
                                      : pom68k::dbg::ByteState::Ok;
        }
    }
    pom68k::dbg::DisasmLine disassemble(std::uint32_t addr) override {
        pom68k::dbg::DisasmLine l;
        l.addr = addr;
        l.readable = true;
        l.text = "nop";
        return l;
    }
    bool addBreakpoint(std::uint32_t a) override { bps.push_back(a); return true; }
    void removeBreakpoint(std::uint32_t a) override { std::erase(bps, a); }
    void clearBreakpoints() override { bps.clear(); }
    std::vector<std::uint32_t> breakpoints() const override { return bps; }
    void armStep() override { ++steps; }
    void armStepOver() override { ++stepOvers; }
    void armStepOut() override { ++stepOuts; }
    void cancelRun() override {}
    void maintain() override {}
    std::uint32_t romChecksum() const override { return 0x9779D2C4; }
    void devices(std::vector<pom68k::dev::Snapshot>& out) const override {
        out.clear();
        pom68k::dev::Snapshot v;
        v.kind = "VIA";
        v.name = "VIA1";
        v.fields.push_back({"ifr", 0x82, 8, {}});
        out.push_back(v);
    }
    bool hist = false;
    void setHistory(bool on) override { hist = on; }
    bool historyOn() const override { return hist; }
    void clearHistory() override {}
    std::uint64_t historyRecorded() const override { return hist ? 40000 : 0; }
    std::uint64_t trapsRecorded() const override { return hist ? 3 : 0; }
    void history(std::vector<pom68k::dbg::HistoryEntry>& out, std::size_t) const override {
        out.clear();
        if (!hist) return;
        for (std::uint32_t i = 0; i < 3; ++i) {
            pom68k::dbg::HistoryEntry e;
            e.clock = 100 + i;
            e.pc = 0x2000 + 2 * i;
            e.opcode = 0x4E71;
            e.d[0] = i;
            out.push_back(e);
        }
    }
    void traps(std::vector<pom68k::dbg::TrapEntry>& out, std::size_t) const override {
        out.clear();
        if (hist) out.push_back({200, 0x300A, 0xA31E, 10});
    }
    int stepOvers = 0, stepOuts = 0;
    bool stopsArmed() const override { return !bps.empty(); }
    bool setRegister(pom68k::dbg::Reg r, std::uint64_t v, std::string&) override {
        edits.push_back({int(r), std::uint32_t(v)});
        return true;
    }
    bool writeMemory(pom68k::dbg::Space, std::uint32_t addr, const std::uint8_t* d,
                     std::size_t n, std::string&) override {
        pokes.push_back({addr, std::vector<std::uint8_t>(d, d + n)});
        return true;
    }
    std::vector<pom68k::dbg::Watchpoint> watches;
    std::vector<pom68k::dbg::Catch> catchList;
    bool addWatchpoint(const pom68k::dbg::Watchpoint& w, std::string&) override {
        watches.push_back(w);
        return true;
    }
    void removeWatchpoint(std::uint32_t a) override {
        std::erase_if(watches, [&](const auto& w) { return w.addr == a; });
    }
    void clearWatchpoints() override { watches.clear(); }
    std::vector<pom68k::dbg::Watchpoint> watchpoints() const override { return watches; }
    bool addCatch(const pom68k::dbg::Catch& c, std::string&) override {
        catchList.push_back(c);
        return true;
    }
    void removeCatch(const pom68k::dbg::Catch& c) override { std::erase(catchList, c); }
    void clearCatches() override { catchList.clear(); }
    std::vector<pom68k::dbg::Catch> catches() const override { return catchList; }
    std::vector<std::pair<int, std::uint32_t>> edits;
    std::vector<std::pair<std::uint32_t, std::vector<std::uint8_t>>> pokes;
};

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

    // ── Débogueur ────────────────────────────────────────────────────
    // Every button is a queued command; what is drawn is the snapshot the
    // machine thread published (here: atBoundary() on this thread).
    {
        pom68k::dbg::Session session;
        FakeDebugTarget target;
        pom68k::gui::GuiDebuggerState state;
        state.session = &session;
        state.showWindow = true;
        // The window has grown a section per debugger feature; pin its
        // scroll to the top so a click finds what the folds leave visible.
        auto draw = [&] {
            ImGui::SetNextWindowScroll(ImVec2(0, 0));
            pom68k::gui::drawDebuggerWindow(state);
        };
        session.atBoundary(target);
        ui.frame(draw);
        ui.frame(draw);
        ImRect r;
        check(windowShown(pom68k::gui::kDebuggerWindowTitle, &r),
              "Débogueur opens when asked");
        check(ui.distinctColours(r) > 12, "the window is drawn, not blank");
        check(ui.find("Pause") != nullptr && ui.find("Continuer") == nullptr,
              "a running machine offers Pause only");
        check(ui.click("Pause", draw), "click « Pause »");
        check(!session.snapshot()->stopped, "the click alone stops nothing");
        check(session.atBoundary(target), "the machine thread applies the Pause");
        ui.frame(draw);
        check(ui.find("Continuer") != nullptr && ui.find("Pas à pas") != nullptr,
              "a stopped machine offers Continuer and Pas à pas");
        // A register edit: the window posts it, the machine thread applies it.
        state.editRegister = int(pom68k::dbg::Reg::A0) + 3;
        std::snprintf(state.editValue.data(), state.editValue.size(), "$CAFE");
        check(ui.click("Appliquer", draw), "click « Appliquer »");
        check(target.edits.empty(), "the click alone edits nothing");
        session.atBoundary(target);
        check(target.edits.size() == 1 && target.edits[0].first == 11 &&
                  target.edits[0].second == 0xCAFE,
              "« Appliquer » posts A3 = $CAFE to the machine thread");
        // Plain Text lines register no item; the sections and the
        // disassembly's selectable rows do.
        check(ui.find("Registres") != nullptr && ui.find("Points d'arrêt") != nullptr,
              "the register and breakpoint sections are drawn");
        const auto* line = ui.findContaining("00002000  nop");
        check(line != nullptr, "the disassembly lists the PC line");
        if (line) {
            check(ui.clickAt(line->bb.GetCenter(), draw), "click the PC line");
            session.atBoundary(target);
            ui.frame(draw);
            check(target.bps.size() == 1 && target.bps[0] == 0x2000,
                  "a line click posts a breakpoint at its address");
            const auto snap = session.snapshot();
            check(snap->requestedEngine == 1 && snap->effectiveEngine == 0,
                  "an armed stop publishes the effective engine beside the request");
        }
        check(ui.click("Par-dessus l'appel", draw), "click « Par-dessus l'appel »");
        session.atBoundary(target);
        check(target.stepOvers == 1 && !session.snapshot()->stopped,
              "« Par-dessus l'appel » arms a step over and resumes");
        session.post([] { pom68k::dbg::Command c; c.kind = pom68k::dbg::Command::Kind::Pause; return c; }());
        session.atBoundary(target);
        ui.frame(draw);
        check(ui.click("Jusqu'au retour", draw), "click « Jusqu'au retour »");
        session.atBoundary(target);
        check(target.stepOuts == 1 && !session.snapshot()->stopped,
              "« Jusqu'au retour » arms a step out and resumes");
        session.post([] { pom68k::dbg::Command c; c.kind = pom68k::dbg::Command::Kind::Pause; return c; }());
        session.atBoundary(target);
        ui.frame(draw);
        check(ui.click("Pas à pas", draw), "click « Pas à pas »");
        session.atBoundary(target);
        check(target.steps == 1 && !session.snapshot()->stopped,
              "Pas à pas arms one step and resumes");
        capture(ui, "debugger");
        dumpLabels("debugger");
        // The memory pane: a window the machine thread filled, then a
        // malformed address refused at the GUI without a command.
        pom68k::dbg::Command view;
        view.kind = pom68k::dbg::Command::Kind::ViewMemory;
        view.addr = 0x60;
        view.length = 64;
        session.post(view);
        session.atBoundary(target);
        check(ui.click("Registres", draw) && ui.click("Désassemblage", draw) &&
              ui.click("Points d'arrêt", draw) && ui.click("Mémoire", draw),
              "fold registers, breakpoints and disassembly, open the memory section");
        const auto mem = session.snapshot();
        check(mem->memory.bytes.size() == 64 &&
              mem->memory.state[0x1F] == pom68k::dbg::ByteState::Ok &&
              mem->memory.state[0x20] == pom68k::dbg::ByteState::NotMemory,
              "the snapshot carries the requested window and its verdicts");
        const auto acked = mem->acked;
        check(ui.click("Afficher", draw) && !state.inputError.empty(),
              "an empty address is refused in the window");
        session.atBoundary(target);
        check(session.snapshot()->acked == acked, "and posts nothing");
        check(ui.find("Écrire") == nullptr, "a running machine offers no memory write");
        session.post([] { pom68k::dbg::Command c; c.kind = pom68k::dbg::Command::Kind::Pause; return c; }());
        session.atBoundary(target);
        ui.frame(draw);
        std::snprintf(state.pokeAddress.data(), state.pokeAddress.size(), "$100");
        std::snprintf(state.pokeBytes.data(), state.pokeBytes.size(), "54 81");
        check(ui.click("Écrire", draw), "click « Écrire » on a stopped machine");
        session.atBoundary(target);
        check(target.pokes.size() == 1 && target.pokes[0].first == 0x100 &&
                  target.pokes[0].second == std::vector<std::uint8_t>{0x54, 0x81},
              "« Écrire » posts the parsed bytes at the parsed address");
        std::snprintf(state.pokeBytes.data(), state.pokeBytes.size(), "548");
        check(ui.click("Écrire", draw) && !state.inputError.empty(),
              "half a byte is refused in the window");
        // Access and exception stops: each button posts typed entries.
        check(ui.click("Mémoire", draw) && ui.click("Surveillances", draw),
              "fold memory, open the watchpoint section");
        std::snprintf(state.watchText.data(), state.watchText.size(), "$2100");
        check(ui.click("Surveiller", draw), "click « Surveiller »");
        session.atBoundary(target);
        check(target.watches.size() == 1 && target.watches[0].addr == 0x2100 &&
                  target.watches[0].length == 4 &&
                  target.watches[0].access == pom68k::dbg::Access::Write,
              "« Surveiller » posts a 4-byte write watchpoint");
        check(ui.click("Surveillances", draw) && ui.click("Exceptions", draw),
              "fold watchpoints, open the exception section");
        state.catchPreset = 4;                 // A-line
        std::snprintf(state.catchText.data(), state.catchText.size(), "$A11E");
        ui.frame(draw);                        // the preset moves the button
        check(ui.click("Arrêter sur", draw), "click « Arrêter sur » with an A-line filter");
        state.catchPreset = 6;                 // interrupts 24-31
        state.catchText[0] = 0;
        ui.frame(draw);
        check(ui.click("Arrêter sur", draw), "click « Arrêter sur » for interrupts");
        session.atBoundary(target);
        check(target.catchList.size() == 9 &&
                  target.catchList[0] == pom68k::dbg::Catch{10, 0xA11E} &&
                  target.catchList[1].vector == 24 && target.catchList[8].vector == 31,
              "the presets post vector 10 with its trap, then vectors 24-31");
        ui.frame(draw);
        check(ui.find("Retirer") != nullptr, "armed exception stops are listed");
        capture(ui, "debugger-stops");
        // The history section: a checkbox, the counters, an export.
        check(ui.click("Exceptions", draw) && ui.click("Historique", draw),
              "fold exceptions, open the history section");
        check(ui.click("Enregistrer", draw), "tick « Enregistrer »");
        session.atBoundary(target);
        ui.frame(draw);
        check(target.hist && session.snapshot()->historyOn &&
                  session.snapshot()->historyTail.size() == 3,
              "the checkbox switches the history on and the tail is published");
        const std::string histPath =
            (std::filesystem::temp_directory_path() / "pom68k_gui_history.txt").string();
        std::snprintf(state.historyPath.data(), state.historyPath.size(), "%s",
                      histPath.c_str());
        ui.frame(draw);
        check(ui.click("Exporter", draw), "click « Exporter »");
        session.atBoundary(target);
        check(session.snapshot()->message.find("exporté") != std::string::npos &&
                  std::filesystem::exists(histPath),
              "« Exporter » writes the file on the machine thread");
        std::filesystem::remove(histPath);
        capture(ui, "debugger-history");
        check(ui.click("Historique", draw) && ui.click("Registres", draw) &&
                  ui.click("Symboles", draw),
              "fold the history, open the symbol section");
        std::snprintf(state.symbolPath.data(), state.symbolPath.size(), "%s",
                      "/nonexistent/pom68k.sym");
        ui.frame(draw);
        check(ui.click("Charger", draw), "click « Charger »");
        session.atBoundary(target);
        check(session.snapshot()->romChecksum == 0x9779D2C4 &&
                  session.snapshot()->symbolCount == 0 &&
                  session.snapshot()->message.find("illisible") != std::string::npos,
              "« Charger » reports an unreadable file and loads nothing");
        check(ui.click("Exceptions", draw) && ui.click("Historique", draw) &&
                  ui.click("Symboles", draw) && ui.click("Composants", draw),
              "fold the open sections, open the component section");
        check(ui.click("Relever les composants", draw), "tick « Relever les composants »");
        session.atBoundary(target);
        ui.frame(draw);
        check(session.snapshot()->devicesOn && session.snapshot()->devices.size() == 1 &&
                  ui.find("VIA - VIA1") != nullptr,
              "the components are published and listed by kind and name");
        capture(ui, "debugger-memory");
        // Close it like a user would, so no focus or input state leaks
        // into the next window's scenario.
        state.showWindow = false;
        ui.frame(draw);
        ui.frame(draw);
        check(pom68k::gui::parseGuestAddress("$40800000") == 0x40800000u &&
              pom68k::gui::parseGuestAddress("0x2000") == 0x2000u &&
              !pom68k::gui::parseGuestAddress("zz") &&
              !pom68k::gui::parseGuestAddress("1FFFFFFFF"),
              "addresses parse as 32-bit hexadecimal");
        check(pom68k::gui::parseHexBytes("54 81") == std::vector<std::uint8_t>{0x54, 0x81} &&
              pom68k::gui::parseHexBytes("$54,$81") == std::vector<std::uint8_t>{0x54, 0x81} &&
              pom68k::gui::parseHexBytes("5481") == std::vector<std::uint8_t>{0x54, 0x81} &&
              !pom68k::gui::parseHexBytes("") && !pom68k::gui::parseHexBytes("5 4") &&
              !pom68k::gui::parseHexBytes("zz"),
              "edit bytes parse as whole hexadecimal bytes");
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

    // ── AppleTalk / Ethernet, hub attached: the services form ────────
    // The full window needs a hub attached to a machine (AtalkHub::attach
    // takes the board for its SCC); a Plus board with no ROM is enough for
    // the window, which only ever reads the hub's snapshot. Never rendered
    // before today (TODO § Services réseau, 2026-09-13).
    {
        ui.resize(1024, 1000);            // the full window runs past 768 px
        GuiNetworkState state;
        state.showWindow = true;
        state.ethernetScsiId = -1;
        MacMemory board(pom68k::defaultCoreConfig());
        state.atalk.attach(board, 7833600, nullptr);
        auto draw = [&] { pom68k::gui::drawAppleTalkWindow(state); };
        ui.frame(draw);
        ui.frame(draw);
        ImRect r;
        check(windowShown(pom68k::gui::kNetworkWindowTitle, &r) && state.atalk.snapshot().attached,
              "the full AppleTalk window renders on an attached hub");
        check(ui.find("Réseau AppleTalk actif") && ui.find("Activer AppleShare") &&
                  ui.find("Serveur AFP (nom NBP)") && ui.find("Dossier partagé"),
              "the services form is on screen");
        capture(ui, "network-services");
        dumpLabels("network-services");
        // A live service toggle goes straight to the hub.
        check(state.atalk.snapshot().cfg.afp, "AppleShare starts enabled");
        check(ui.click("Activer AppleShare", draw), "click « Activer AppleShare »");
        check(!state.atalk.snapshot().cfg.afp, "the checkbox turned AppleShare off in the hub");
        check(ui.click("Activer AppleShare", draw) && state.atalk.snapshot().cfg.afp,
              "and back on");
        // The form: rename the server, apply — reconfigure() reaches the hub.
        check(ui.click("Serveur AFP (nom NBP)", draw), "focus the AFP server name field");
        ui.key(ImGuiKey_End, true); ui.frame(draw); ui.key(ImGuiKey_End, false); ui.frame(draw);
        for (int i = 0; i < 16; i++) { ui.key(ImGuiKey_Backspace, true); ui.frame(draw); ui.key(ImGuiKey_Backspace, false); ui.frame(draw); }
        ui.type("POMTEST");
        ui.frame(draw);
        ui.frame(draw);
        check(ui.inputText(ui.activeId()) == "POMTEST", "the field's edit state holds the typed name");
        check(ui.find("Appliquer") != nullptr, "an edited field shows « Appliquer »");
        // The form refuses an empty shared folder (« Dossier partagé :
        // vide ») — a hub attached without a default share has none — so
        // the folder is typed too, the way a user would.
        check(ui.click("Dossier partagé", draw), "focus the shared-folder field");
        ui.type("AppleShare");
        ui.frame(draw);
        ui.frame(draw);
        capture(ui, "network-services-edited");
        dumpLabels("network-services-edited");
        check(ui.click("Appliquer", draw), "click « Appliquer »");
        const std::string server = state.atalk.snapshot().cfg.serverName;
        const std::string share = state.atalk.snapshot().cfg.shareDir;
        std::printf("  after apply: server '%s', shared folder '%s'\n", server.c_str(), share.c_str());
        check(server == "POMTEST" && share == "AppleShare",
              "the hub was reconfigured with the typed server name and folder");
        ui.resize(1024, 768);
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
        // Session images are listed after disks35/, which grew past the
        // popup's eight visible rows on 2026-09-16: scroll until it shows.
        const headless::Item* row = ui.scrollTo("gui_windows_test_dropped", draw);
        check(row != nullptr, "the dropped floppy is offered by the picker");
        if (row) ui.clickAt(row->bb.GetCenter(), draw);
        check(floppyIn && inserted == dropped, "choosing it inserts it live through the host hook");
        check(ui.find("Éjecter##fd") != nullptr, "the row now offers « Éjecter »");
        capture(ui, "disks-inserted");
        std::remove(dropped.c_str());
    }

    // ── Disques: the SE's second internal floppy ─────────────────────
    // Fitted: its own row, live like the others (a pick reaches the drive-2
    // hook). Fittable either way: the checkbox stages the board option for
    // the next boot through the relaunch hook, in both directions; a board
    // without the connector shows neither.
    {
        const std::string dropped = "gui_windows_test_second.dsk";
        { std::ofstream f(dropped, std::ios::binary); f << std::string(819200, '\0'); }
        pom68k::diskBaysOfferDroppedImage(dropped);
        pom68k::DiskBaysHost host;
        host.romName = "roms/macse.rom";
        host.bootPath = "hdv/boot.vhd";
        std::vector<std::string> extras;
        host.extras = &extras;
        host.hasFloppyDrive = true;
        host.floppyInserted = [] { return false; };
        host.secondFloppyFittable = true;
        host.hasSecondFloppyDrive = true;
        bool secondIn = false;
        std::string secondPicked;
        std::vector<bool> staged;
        host.secondFloppyInserted = [&] { return secondIn; };
        host.insertSecondFloppy = [&](const std::string& p) {
            secondPicked = p; secondIn = true; host.secondFloppyPath = p;
        };
        host.ejectSecondFloppy = [&] { secondIn = false; };
        host.stageSecondFloppy = [&](bool fitted) { staged.push_back(fitted); };
        auto draw = [&] { pom68k::diskBaysWindow(host); };
        ui.frame(draw);
        ui.frame(draw);
        const ImGuiID fd2 = ui.idIn(pom68k::kDiskWindowTitle, "##fd2pick");
        check(ui.clickId(fd2, draw), "a fitted second internal drive has its own picker");
        const headless::Item* row = ui.scrollTo("gui_windows_test_second", draw);
        if (row) ui.clickAt(row->bb.GetCenter(), draw);
        check(secondIn && secondPicked == dropped,
              "choosing an image inserts it live into drive 2 through its hook");
        check(ui.find("Éjecter##fd2") != nullptr && ui.click("Éjecter##fd2", draw) && !secondIn,
              "drive 2's « Éjecter » reaches its own hook");
        check(ui.click("Second lecteur interne (redémarre la machine)", draw) &&
                  staged == std::vector<bool>{false},
              "unticking the fitted drive stages its removal for the next boot");
        host.hasSecondFloppyDrive = false;
        ui.frame(draw);
        check(ui.findId(ui.idIn(pom68k::kDiskWindowTitle, "##fd2pick")) == nullptr,
              "without the mechanism the drive-2 row is gone");
        check(ui.click("Second lecteur interne (redémarre la machine)", draw) &&
                  staged == std::vector<bool>{false, true},
              "ticking it stages the fitting");
        host.secondFloppyFittable = false;
        ui.frame(draw);
        check(ui.find("Second lecteur interne (redémarre la machine)") == nullptr,
              "a board without the connector offers no checkbox");
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

    // ── Ports série: a refused port, an observed one, the endpoint ───
    // The printer port is configured but refused (AppleTalk owns channel
    // B): the window says why. The modem port is the window's own
    // terminal: the guest's bytes are shown, « Envoyer » queues the line
    // and its CR for the guest, and a full queue reports what it refused.
    {
        GuiSerialState st;
        st.showWindow = true;
        st.ports[0].name = "Imprimante (canal B)";
        st.ports[0].requested = "pty";
        st.ports[0].message = "unavailable while AppleTalk/LToUDP owns SCC channel B";
        st.ports[1].name = "Modem (canal A)";
        st.ports[1].requested = "terminal";
        st.ports[1].endpoint = "terminal intégré";
        st.ports[1].terminal = std::make_shared<SerialTerminal>(true);
        for (char c : std::string("ATZ\rOK\r"))
            st.ports[1].terminal->observe(SerialTerminal::Direction::FromGuest, std::uint8_t(c));
        auto draw = [&] { pom68k::gui::drawSerialWindow(st); };
        ui.frame(draw);
        ui.frame(draw);
        ImRect r;
        check(windowShown(pom68k::gui::kSerialWindowTitle, &r) && ui.distinctColours(r) > 8,
              "« Ports série » is drawn when asked");
        check(st.ports[1].terminal->view().text == "ATZ\nOK\n",
              "the endpoint port's log holds the guest's bytes");
        std::snprintf(st.ports[1].line.data(), st.ports[1].line.size(), "AT&F");
        check(ui.click("Envoyer", draw) && st.ports[1].terminal->view().pendingInput == 5 &&
                  st.ports[1].line[0] == 0,
              "« Envoyer » queues the line and its CR for the guest, and clears the field");
        st.ports[1].terminal->send(std::string(SerialTerminal::kInputBytes, 'x'));
        std::snprintf(st.ports[1].line.data(), st.ports[1].line.size(), "more");
        ui.click("Envoyer", draw);
        ui.frame(draw);
        check(st.ports[1].sendStatus.find("refus") != std::string::npos &&
                  std::string(st.ports[1].line.data()) == "more",
              "a full queue refuses the line, says so, and keeps it in the field");
        capture(ui, "serial");
        dumpLabels("serial");
    }

    // ── Display: the CRT presets and the kiosk letterbox (pure) ──────
    {
        pom68k::gui::CrtParams p;
        bool on = false;
        check(pom68k::gui::applyCrtPreset("arcade", p, on) && on &&
                  p.shadowMask == pom68k::gui::CrtParams::ShadowMask::Triad && p.scanlines > 0.4f,
              "the arcade preset turns the pass on with a triad mask");
        check(pom68k::gui::applyCrtPreset("off", p, on) && !on && p.scanlines > 0.4f,
              "« off » turns the pass off and keeps the sliders");
        check(!pom68k::gui::applyCrtPreset("plasma", p, on), "an unknown preset is refused");
        check(pom68k::gui::applyCrtPreset("light", p, on) && on && p.shadowMask == pom68k::gui::CrtParams::ShadowMask::Off,
              "the light preset has no mask");
        const pom68k::gui::Letterbox wide = pom68k::gui::letterbox(1920, 1080, 512, 342);
        check(wide.h == 1080 && wide.y == 0 && wide.x > 0 && std::abs(wide.w / wide.h - 512.0f / 342.0f) < 0.01f,
              "a 512x342 screen on a 1920x1080 monitor fills the height, pillarboxed");
        const pom68k::gui::Letterbox tall = pom68k::gui::letterbox(640, 1000, 640, 480);
        check(tall.w == 640 && tall.x == 0 && tall.y > 0, "a 640x480 screen on a tall surface fills the width, letterboxed");
        check(pom68k::gui::letterbox(0, 0, 512, 342).w == 0, "an empty surface yields an empty box");
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
                                 "src/GuiDebuggerWindow.cpp", "src/GuiSessionMenu.cpp",
                                 "src/GuiTypingWindow.cpp", "src/GuiSerialWindow.cpp",
                                 "src/GuiShellMenu.cpp",
                                 "src/AdbVia.cpp", "src/V8Memory.cpp", "src/Q605Memory.cpp",
                                 "src/Q630Memory.cpp", "src/RbvMemory.cpp", "src/Q700Memory.cpp",
                                 "src/VaspMemory.cpp", "src/SonoraMemory.cpp", "src/TobyDeclChoice.h"};
        auto windowSource = [](const std::string& rel) {
            return rel.find("Window") != std::string::npos || rel.find("DiskBays") != std::string::npos ||
                   rel.find("GuiShell") != std::string::npos || rel.find("GuiMachineControls") != std::string::npos ||
                   rel.find("GuiDebugger") != std::string::npos ||
                   rel.find("GuiSessionMenu") != std::string::npos;
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
