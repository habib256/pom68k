// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Gate: the « ROM Status » window (src/GuiRomWindow.*), drawn headless by its
// real draw function (tests/ImGuiHeadless.h), with its worker thread real
// and everything below it faked or confined:
//
//   empty tree        the real scanner over an empty temporary folder: every
//                     row Missing, installs would go to <tmp>/work/roms
//   partly populated  a fake scanner: verified, shadowed, mismatched and
//                     missing rows for the Mac II, the filter, the text filter
//   downloading       a fake fetcher that reports progress and holds until
//                     released: the progress bar, the label, Cancel
//   finished          the same fetcher's mixed result (POM68K archive and
//                     RetroBIOS, one failure): counts, the rescan, the
//                     running-machine note
//   cancel            Cancel stops a held fetch within a poll, and the
//                     window rescans what a cancelled run may have installed
//   no archive        the REAL fetchRoms with a fake transport: the archive
//                     answers 404, RetroBIOS serves wrong bytes; nothing is
//                     installed and the archive's reason is shown
//   quit mid-run      destroying the state cancels and joins at once
//   drops             romDropWanted / diskBaysRouteDrop: a dump-sized .bin
//                     is a ROM, a floppy image a disk, a folder and a zip ROM
//                     inputs; a dropped file is imported by the window
//
// No network access and no ROM: the dumps are the catalogue's rows, never
// their bytes. Captures land beside the binary as gui_roms-*.ppm.

#define POM68K_IMGUI_HEADLESS_HOOKS
#include "ImGuiHeadless.h"

#include "DiskBays.h"
#include "GuiRomWindow.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {
namespace fs = std::filesystem;
using namespace pom68k;
using namespace pom68k::gui;
using Clock = std::chrono::steady_clock;

int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-70s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}

void capture(headless::Context& ui, const char* name) {
    const std::string path = std::string("gui_") + name + ".ppm";
    if (!ui.savePpm(path)) std::printf("  (could not write %s)\n", path.c_str());
}

bool windowShown(const char* title, ImRect* rect = nullptr) {
    ImGuiWindow* w = ImGui::FindWindowByName(title);
    if (!w || !w->WasActive || w->Collapsed || w->Hidden) return false;
    if (w->Size.x < 100 || w->Size.y < 40) return false;
    if (rect) *rect = w->Rect();
    return true;
}

// Frames until the worker is done (or 10 s), then two to settle the layout.
template <class Draw>
bool settle(headless::Context& ui, GuiRomState& st, Draw&& draw) {
    const auto until = Clock::now() + std::chrono::seconds(10);
    while (st.job && Clock::now() < until) {
        ui.frame(draw);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ui.frame(draw);
    ui.frame(draw);
    return !st.job;
}

void writeFile(const fs::path& p, std::size_t size, char fill = 'x') {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    const std::string bytes(size, fill);
    out.write(bytes.data(), std::streamsize(bytes.size()));
}

roms::RomStatus row(const char* id, roms::RomState state, std::string foundAt = {},
                    std::string detail = {}) {
    roms::RomStatus s;
    s.dump = roms::findRomDump(id);
    s.state = state;
    s.foundAt = std::move(foundAt);
    s.verifiedAt = state == roms::RomState::Verified ? s.foundAt : std::string();
    s.detail = std::move(detail);
    return s;
}

// The whole catalogue Missing, then `overrides` on top.
std::vector<roms::RomStatus> catalogueWith(const std::vector<roms::RomStatus>& overrides) {
    std::vector<roms::RomStatus> out;
    for (const roms::RomDump& d : roms::kRomDumps) {
        roms::RomStatus s;
        s.dump = &d;
        for (const roms::RomStatus& o : overrides)
            if (o.dump == &d) s = o;
        out.push_back(s);
    }
    return out;
}

roms::FetchItem item(const char* id, bool ok, const char* source, const char* error = "") {
    roms::FetchItem it;
    it.dump = roms::findRomDump(id);
    it.ok = ok;
    it.source = ok ? source : "";
    it.error = error;
    return it;
}

// A fetcher that reports progress, then holds until released or cancelled
// (polling as the library does), then answers `result`.
struct HeldFetch {
    std::atomic<bool> release{false};
    std::atomic<bool> sawCancel{false};
    std::atomic<int> calls{0};
    std::vector<std::string> only;
    roms::FetchResult result;

    roms::FetchResult operator()(const roms::RomSearch&, const roms::FetchOptions& o) {
        ++calls;
        only = o.only;
        if (o.progress) o.progress({3, 8, "Toby video 342-0008-a"});
        while (!release.load()) {
            if (o.cancelled && o.cancelled()) {
                sawCancel = true;
                roms::FetchResult r;
                r.cancelled = true;
                r.items = {item("decl-toby", true, "RetroBIOS")};
                r.saved = r.fromRetroBios = 1;
                r.summary = "Cancelled: saved 1 ROM before stopping.";
                return r;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return result;
    }
};

} // namespace

int main() {
    std::printf("gui_rom_window_test - the ROM Status window, headless\n");
    headless::Context ui(1024, 768);

    const fs::path base = fs::temp_directory_path() /
        ("pom68k-rom-window-" + std::to_string(Clock::now().time_since_epoch().count()));
    const fs::path work = base / "work";
    fs::create_directories(work);
    auto confined = [&] {
        roms::RomSearch s;
        s.workingDir = work;   // no executable dir, no firmware root
        return s;
    };

    // ── Empty tree: the real scanner ────────────────────────────────────
    {
        GuiRomState st;
        st.search = confined();
        st.showWindow = true;
        auto draw = [&] { drawRomWindow(st, SnapMachine::LcII); };
        ui.frame(draw);
        check(st.job != nullptr, "opening the window starts a scan, off the frame");
        check(settle(ui, st, draw) && st.scanned, "the scan finishes");
        int missing = 0;
        for (const roms::RomStatus& r : st.rows) missing += r.state == roms::RomState::Missing;
        check(st.rows.size() == roms::kRomDumpCount && missing == int(st.rows.size()),
              "an empty tree: every catalogued dump is Missing");
        check(st.destination.ok() && st.destination.romsDir == work / "roms",
              "installs would go to <working dir>/roms");
        check(!fs::exists(work / "roms"), "scanning created nothing");
        ImRect r;
        check(windowShown(kRomWindowTitle, &r) && ui.distinctColours(r) > 12,
              "the window is shown and drawn");
        check(ui.findContaining("Download missing for Macintosh LC II") != nullptr &&
                  ui.find("Download all missing") && ui.find("Import...") && ui.find("Rescan"),
              "the four actions are offered");
        check(!st.fetched && !st.imported, "nothing was downloaded by opening the window");
        capture(ui, "roms-empty");
        check(ui.click("All dumps", draw) && st.allMachines, "« All dumps » widens the table");
        capture(ui, "roms-empty-all");
    }

    // ── Partly populated: a fake scanner, the Mac II in focus ───────────
    {
        GuiRomState st;
        st.search = confined();
        st.showWindow = true;
        const std::string romsDir = (work / "roms").string();
        st.scanner = [&](const roms::RomSearch&) {
            return catalogueWith({
                row("rom-9779d2c4", roms::RomState::Verified,
                    romsDir + "/256KB ROMs/1987-12 - 9779D2C4 - MacII (800k v2).ROM"),
                row("decl-toby", roms::RomState::Shadowed, romsDir + "/342-0008-a.bin",
                    "a different 32768-byte file is found first"),
                row("decl-824", roms::RomState::Mismatch, romsDir + "/341-0868.bin",
                    "wrong size: 4096 bytes, the dump is 32768"),
                row("rom-4d1f8172", roms::RomState::Verified, romsDir + "/macplus.rom"),
            });
        };
        auto draw = [&] { drawRomWindow(st, SnapMachine::MacII); };
        check(settle(ui, st, draw) && st.scanned, "the fake scan is collected");
        int verified = 0;
        for (const roms::RomStatus& r : st.rows) verified += r.state == roms::RomState::Verified;
        check(verified == 2, "two verified rows");
        capture(ui, "roms-partial");
        std::snprintf(st.filter.data(), st.filter.size(), "toby");
        ui.frame(draw);
        ui.frame(draw);
        capture(ui, "roms-partial-filtered");
        st.filter[0] = 0;
        check(ui.click("All dumps", draw) && st.allMachines, "« All dumps » shows the catalogue");
        capture(ui, "roms-partial-all");
    }

    // ── Downloading, then finished with mixed sources ───────────────────
    {
        GuiRomState st;
        st.search = confined();
        st.showWindow = true;
        int scans = 0;
        bool installedRows = false;
        st.scanner = [&](const roms::RomSearch&) {
            ++scans;
            if (!installedRows)
                return catalogueWith({row("rom-9779d2c4", roms::RomState::Verified,
                                          (work / "roms/macii.rom").string())});
            return catalogueWith({
                row("rom-9779d2c4", roms::RomState::Verified, (work / "roms/macii.rom").string()),
                row("rom-97851db6", roms::RomState::Verified,
                    (work / "roms/256KB ROMs/1987-03 - 97851DB6 - MacII (800k v1).ROM").string()),
                row("decl-toby", roms::RomState::Verified, (work / "roms/342-0008-a.bin").string()),
                row("adb-342s0440b", roms::RomState::Verified,
                    (work / "roms/adbmodem/342s0440-b.bin").string()),
            });
        };
        HeldFetch held;
        held.result.items = {
            item("rom-97851db6", true, "archive"),
            item("decl-toby", true, "RetroBIOS"),
            item("adb-342s0440b", true, "RetroBIOS"),
            item("decl-824", false, "", "not in the POM68K archive (member absent)"),
        };
        held.result.saved = 3;
        held.result.failed = 1;
        held.result.fromArchive = 1;
        held.result.fromRetroBios = 2;
        held.result.romsDir = (work / "roms").string();
        held.result.summary = "Saved 3, failed 1";
        st.fetcher = [&](const roms::RomSearch& s, const roms::FetchOptions& o) {
            auto r = held(s, o);
            installedRows = !r.cancelled;
            return r;
        };
        auto draw = [&] { drawRomWindow(st, SnapMachine::MacII); };
        check(settle(ui, st, draw), "initial scan");
        const auto* mine = ui.findContaining("Download missing for Macintosh II");
        check(mine && ui.clickAt(mine->bb.GetCenter(), draw), "click « Download missing for Macintosh II »");
        for (int i = 0; i < 20 && romJobView(st).done == 0; ++i) {
            ui.frame(draw);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        ui.frame(draw);
        const RomJobView v = romJobView(st);
        check(v.running && v.kind == RomJobKind::Fetch && v.done == 3 && v.total == 8,
              "the progress reaches the frame");
        check(held.only.size() == roms::romDumpsForProfile("macii").size(),
              "the fetch is limited to the Mac II's dumps");
        check(ui.find("Cancel") != nullptr, "a running download offers Cancel");
        check(ui.find("Rescan") && ui.findContaining("Download all missing"),
              "the actions stay drawn while it runs");
        capture(ui, "roms-downloading");
        held.release = true;
        check(settle(ui, st, draw), "the download finishes");
        check(st.fetched && st.fetched->saved == 3 && st.fetched->fromArchive == 1 &&
                  st.fetched->fromRetroBios == 2 && st.fetched->failed == 1,
              "the mixed result is kept for the panel");
        check(st.installs == 1, "an install is recorded (signature cache forgotten)");
        int verified = 0;
        for (const roms::RomStatus& r : st.rows) verified += r.state == roms::RomState::Verified;
        check(scans == 2 && verified == 4, "the window rescanned after the download");
        capture(ui, "roms-fetched");
        if (ui.findContaining("Not installed (1)")) {
            ui.click("Not installed (1)", draw);
            capture(ui, "roms-fetched-failures");
        }
    }

    // ── Cancel ───────────────────────────────────────────────────────────
    {
        GuiRomState st;
        st.search = confined();
        st.showWindow = true;
        int scans = 0;
        st.scanner = [&](const roms::RomSearch&) { ++scans; return catalogueWith({}); };
        HeldFetch held;
        st.fetcher = [&](const roms::RomSearch& s, const roms::FetchOptions& o) { return held(s, o); };
        auto draw = [&] { drawRomWindow(st, SnapMachine::MacII); };
        settle(ui, st, draw);
        check(ui.click("Download all missing", draw) && st.job, "a download of everything starts");
        for (int i = 0; i < 20 && romJobView(st).done == 0; ++i) {
            ui.frame(draw);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        const auto t0 = Clock::now();
        check(ui.click("Cancel", draw), "click Cancel");
        const double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        check(held.sawCancel && ms < 1000.0, "Cancel stops the held download within a poll");
        check(held.only.empty(), "« Download all missing » asks for every unverified dump");
        check(settle(ui, st, draw), "the window settles");
        check(st.fetched && st.fetched->cancelled && st.fetched->saved == 1,
              "the cancelled run's result is shown");
        check(scans == 2, "a cancelled run that installed still gets a rescan");
        capture(ui, "roms-cancelled");
    }

    // ── The real library: no archive yet, RetroBIOS bytes wrong ─────────
    {
        GuiRomState st;
        st.search = confined();
        st.showWindow = true;
        std::vector<std::string> asked;
        st.download = [&](const std::string& url, const fs::path& dest, std::string& err) {
            asked.push_back(url);
            if (url.find("archive.org") != std::string::npos) {
                err = "the server answered HTTP 404";
                return false;
            }
            writeFile(dest, 64, 'j');   // never the dump: refused by size/SHA
            return true;
        };
        auto draw = [&] { drawRomWindow(st, SnapMachine::MacII); };
        settle(ui, st, draw);
        const auto* mine = ui.findContaining("Download missing for Macintosh II");
        check(mine && ui.clickAt(mine->bb.GetCenter(), draw), "download the Mac II's dumps");
        check(settle(ui, st, draw), "the run finishes");
        check(st.fetched && st.fetched->saved == 0 && st.fetched->failed > 0 &&
                  !st.fetched->archiveError.empty(),
              "nothing installed, and the archive's reason is kept");
        bool archiveAsked = false, retroAsked = false;
        for (const std::string& u : asked) {
            archiveAsked |= u.find("archive.org") != std::string::npos;
            retroAsked |= u.find("githubusercontent") != std::string::npos;
        }
        check(archiveAsked && retroAsked, "the archive, then RetroBIOS, were asked (fake transport)");
        int files = 0;
        if (fs::exists(work / "roms"))
            for (const auto& e : fs::recursive_directory_iterator(work / "roms"))
                files += e.is_regular_file();
        check(files == 0, "no file was installed");
        check(st.installs == 0, "no install recorded");
        // The « Not installed » fold is still open from the mixed run (one
        // window, one ImGui id): the capture shows each dump's reason.
        capture(ui, "roms-archive-missing");
    }

    // ── Quitting mid-download: the destructor cancels and joins ──────────
    {
        auto held = std::make_shared<HeldFetch>();
        auto st = std::make_unique<GuiRomState>();
        st->search = confined();
        st->scanner = [](const roms::RomSearch&) { return catalogueWith({}); };
        st->fetcher = [held](const roms::RomSearch& s, const roms::FetchOptions& o) {
            return (*held)(s, o);
        };
        check(romWindowStartFetch(*st, std::nullopt), "a download starts without the window");
        check(!romWindowRescan(*st), "one job at a time");
        while (held->calls.load() == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const auto t0 = Clock::now();
        st.reset();
        const double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        std::printf("  destructor joined in %.1f ms\n", ms);
        check(held->sawCancel && ms < 500.0, "destroying the state cancels and joins at once");
    }

    // ── Drops ────────────────────────────────────────────────────────────
    {
        const fs::path drops = base / "drops";
        writeFile(drops / "341s0850.bin", 4352);            // an Egret-sized .bin
        writeFile(drops / "rom.bin", 1u << 20);             // a 1 MB ROM-sized .bin
        writeFile(drops / "System.dsk", 819200);            // an 800K floppy
        writeFile(drops / "notes.txt", 10);
        writeFile(drops / "set.zip", 22);
        fs::create_directories(drops / "collection");
        check(romDropWanted(drops / "341s0850.bin"), "a dump-sized .bin is a ROM input");
        check(romDropWanted(drops / "rom.bin"), "a 1 MB .bin is a ROM input, not a CD");
        check(!romDropWanted(drops / "System.dsk"), "a floppy image is not");
        check(romDropWanted(drops / "collection"), "a folder is a ROM input");
        check(romDropWanted(drops / "set.zip"), "a zip is a ROM input");
        check(!romDropWanted(drops / "notes.txt"), "a small text file is not claimed first");
        check(!romDropWanted(drops / "absent.rom"), "a path that does not exist is not");
        writeFile(drops / "tiny.dsk", 65536);              // a disk image of a dump's size
        check(!romDropWanted(drops / "tiny.dsk"), "a disk image of a dump's size stays a disk");

        (void)romWindowTakeDrops();
        check(diskBaysRouteDrop((drops / "rom.bin").string()) == DropRoute::Rom,
              "the drop router sends the .bin to the ROM window");
        check(diskBaysRouteDrop((drops / "System.dsk").string()) == DropRoute::Disk,
              "and the floppy image to the Disks window");
        check(diskBaysRouteDrop((drops / "notes.txt").string()) == DropRoute::Rom,
              "a stray small file goes to the importer, which will say what it is");
        const auto queued = romWindowTakeDrops();
        check(queued.size() == 2, "two paths queued for the ROM window");

        // The window imports what was dropped: the real importer, which
        // recognises nothing here and says so.
        GuiRomState st;
        st.search = confined();
        st.pendingImport = queued;
        st.showWindow = true;
        auto draw = [&] { drawRomWindow(st, SnapMachine::LcII); };
        ui.frame(draw);
        check(settle(ui, st, draw), "the import finishes");
        check(st.imported && st.imported->installed == 0 && st.imported->unrecognised.size() == 2,
              "both dropped files are reported as not recognised");
        check(st.pendingImport.empty(), "the queue was consumed");
        if (const auto* fold = ui.findContaining("Not recognised (")) {
            const std::string label = fold->label;
            ui.click(label.c_str(), draw);
        }
        capture(ui, "roms-imported");
        std::snprintf(st.folder.data(), st.folder.size(), "%s", drops.string().c_str());
        check(ui.click("Import...", draw) && st.browsing, "« Import... » opens the browser");
        check(ui.click("341s0850.bin", draw) && !st.browsing &&
                  fs::path(st.importPath.data()).filename() == "341s0850.bin",
              "picking a file fills the import path");
        check(ui.click("Import...", draw) && ui.click("collection /", draw) &&
                  ui.click("Use this folder", draw) &&
                  fs::path(st.importPath.data()).filename() == "collection",
              "a folder can be chosen as the import input");
        check(ui.click("Import...", draw) && st.browsing, "the browser reopens");
        capture(ui, "roms-import-browser");
    }

    std::error_code ec;
    fs::remove_all(base, ec);
    std::printf("%s\n", gFails ? "FAILED" : "PASSED");
    return gFails ? 1 : 0;
}
