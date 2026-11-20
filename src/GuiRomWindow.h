// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The « ROM Status » window: the ROM catalogue (RomCatalog.h) against this
// host. One row per catalogued dump — verified, shadowed, mismatched or
// missing, where it was found, who uses it and what its absence costs —
// and the two ways to fill the gaps: download what is missing (RomFetch.h)
// or import what the user already has (RomImport.h, also by dropping files
// on the application window).
//
// Threads. Scanning hashes every candidate (~24 MB), a download waits on the
// network and an import walks folders and zips: each runs on ONE worker
// thread the window owns (`RomJob`), never in the ImGui frame. The frame
// reads a mutex-guarded progress copy and collects the result when the
// worker says it is done. Cancel sets an atomic the library polls, then
// joins; the state's destructor does the same, so quitting mid-download
// takes one curl poll interval (30 ms), not the download. The browser build
// has no threads: there the job runs inline and the download is refused.
//
// Network. Nothing here reaches the network unless the user clicked a
// download button (`romWindowStartFetch`); opening the window only scans
// the local folders.
//
// After a run that installed a file the window rescans, forgets
// MachineFactory's signature cache (the Machine menu's greyed profiles and
// the Set Up window's ROM discovery) and says that a running machine keeps
// the dumps it started with until it next starts.

#pragma once

#include "MachineCatalog.h"
#include "RomFetch.h"
#include "RomImport.h"
#include "RomStore.h"

#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct GuiSessionState;

namespace pom68k::gui {

inline constexpr const char* kRomWindowTitle = "ROM Status";

struct RomJob;  // the worker (GuiRomWindow.cpp)

enum class RomJobKind : std::uint8_t { Scan, Fetch, Import };

struct GuiRomState {
    GuiRomState();
    ~GuiRomState();             // cancels and joins a running job
    GuiRomState(const GuiRomState&) = delete;
    GuiRomState& operator=(const GuiRomState&) = delete;

    bool showWindow = false;
    bool allMachines = false;   // false: the rows the focused profile uses
    std::array<char, 64> filter{};
    // « Import… »: a typed path, or the folder browser's pick.
    std::array<char, 1024> importPath{}, folder{};
    bool browsing = false;
    std::string browseError;
    // The profile the window is about. Unset: the running machine. The Set
    // Up window sets it to the model being prepared, and asks for a way
    // back once the dumps are in (`returnToStartup`).
    std::optional<SnapMachine> focus;
    bool returnToStartup = false;
    // Set by « Back to Set Up a Macintosh »: the shell reopens the guide on
    // this model (GuiShellMenu.cpp; the guide depends on this window, not
    // the other way round).
    std::optional<SnapMachine> backToStartup;

    // Where to look and write: the process's working and executable
    // directories plus the session's firmware root (GuiHostServices binds
    // it). Tests point it at a temporary tree.
    roms::RomSearch search = roms::RomSearch::current();
    // Seams for the gates, all called on the worker thread. Empty = the
    // library: `download` the system curl (inside roms::fetchRoms),
    // `scanner` roms::scanRoms, `fetcher` roms::fetchRoms, `importer`
    // roms::importRoms. No Apple byte is needed to drive the window: a
    // fake fetcher reports dumps installed, a fake scanner shows them.
    roms::DownloadFn download;
    std::function<std::vector<roms::RomStatus>(const roms::RomSearch&)> scanner;
    std::function<roms::FetchResult(const roms::RomSearch&, const roms::FetchOptions&)> fetcher;
    std::function<roms::ImportResult(const std::vector<std::filesystem::path>&,
                                     const roms::RomSearch&, const roms::ImportOptions&)> importer;

    // The last completed scan, and where installs go.
    std::vector<roms::RomStatus> rows;
    bool scanned = false;
    roms::RomDestination destination;
    // The last download / import, kept until the next one.
    std::optional<roms::FetchResult> fetched;
    std::optional<roms::ImportResult> imported;
    // Bumped by every run that installed a file; the note about running
    // machines shows while it is non-zero.
    int installs = 0;
    // Dropped inputs waiting for the worker to be free.
    std::vector<std::filesystem::path> pendingImport;

    std::unique_ptr<RomJob> job;
};

// What the worker is doing, copied for the frame.
struct RomJobView {
    bool running = false;
    RomJobKind kind = RomJobKind::Scan;
    int done = 0, total = 0;
    std::string label;
    bool cancelling = false;
};
RomJobView romJobView(const GuiRomState& state);

// Start a job; false (and nothing started) while another one runs, or —
// for a download — when romFetchAvailable() is false. `only`: one
// profile's dumps; unset: every catalogued dump that is not verified.
bool romWindowRescan(GuiRomState& state);
bool romWindowStartFetch(GuiRomState& state, std::optional<SnapMachine> only);
bool romWindowStartImport(GuiRomState& state, std::vector<std::filesystem::path> inputs);
// Ask the running job to stop and wait for it. No-op when idle.
void romWindowCancel(GuiRomState& state);
// Collect a finished job into the state (rows, results, cache refresh).
// Called by the window every frame; true the frame a job was collected.
bool romWindowPoll(GuiRomState& state);

// The shell's entry point: takes dropped ROM inputs, then draws when
// `state.roms.showWindow` is set.
void drawRomWindow(GuiSessionState& state, SnapMachine current);
// The window over its own state, for the headless gate.
void drawRomWindow(GuiRomState& state, SnapMachine current);

// The Set Up window's offer: open the ROM window on `profile` and start
// downloading that profile's missing dumps. The click that calls this is
// the user's consent to the network access.
void romWindowDownloadFor(GuiSessionState& state, SnapMachine profile);
// Same, without starting anything: show the window on `profile`.
void romWindowShowFor(GuiSessionState& state, SnapMachine profile);

// ── Dropped files ────────────────────────────────────────────────────────
// Which dropped paths are the ROM window's rather than the Disks window's.
// A `.bin` is a CD image to the Disks window, and firmware dumps are
// `.bin` files too, so extension alone cannot route: a regular file whose
// SIZE is a catalogued dump's (none is a floppy or CD size) and whose
// extension is not another disk-image one is a ROM, and so are a folder and
// a `.zip`. One stat.
bool romDropWanted(const std::filesystem::path& p);
// The drop callback's half (DiskBaysDrop.cpp): queue paths for the next
// frame's import. Any thread.
void romWindowOfferDrop(const std::vector<std::string>& paths);
// The frame's half: everything queued since the last call.
std::vector<std::filesystem::path> romWindowTakeDrops();

} // namespace pom68k::gui
