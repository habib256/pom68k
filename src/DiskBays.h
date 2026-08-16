#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// DiskBays -- the one "Disques" window, shared by every platform runner.
//
// It replaces ten copy-pasted ImGui menus in main.cpp, and it fixes the two
// things that made the old menu hostile:
//
//   1. **Every click rebooted the machine.** The menu called relaunch()
//      directly from a MenuItem, so a mis-aimed click cold-started the
//      emulator -- and a *series* of mis-aimed clicks walked the user through
//      several boot volumes, leaving each one dirty. Here, changes that need
//      a reboot are STAGED and applied by an explicit button.
//   2. **`.dsk` images were invisible.** listDiskImages() only accepted
//      .vhd/.hda/.img while the command line happily attached .dsk, so
//      `System 7.1 HD.dsk` could boot the machine but never appear in its own
//      disk menu. Discovery here matches what the CLI accepts.
//
// Hot-swap contract -- the part worth understanding before using it:
//
//   Classic Mac OS scans the SCSI bus once, at boot. A *fixed* disk that
//   appears afterwards is never noticed. A *removable* one is: the driver
//   polls TEST UNIT READY, and a medium change answers CHECK CONDITION /
//   UNIT ATTENTION ($28, not-ready-to-ready), which is what makes the Finder
//   mount the volume. So:
//
//     - a bay that is OCCUPIED at boot exists as a removable target from the
//       ROM's probe onward, and its medium can be swapped live, forever;
//     - a bay that is EMPTY at boot has no target to probe, so filling it is
//       staged and takes a reboot.
//
//   Which is why, since 2026-08-15, every machine that can hold a CD drive
//   BOOTS with one (`ensureCdDrive` below) and the window carries a CD row
//   beside the floppy's. Reserving it used to be an opt-in checkbox at the
//   bottom of the window, off "because the boot-etalon gates are timed
//   against the bus as it stands today" — which was not true of the gates,
//   only of the GUI's usability.
//
//   What the contract still cannot do for you: **the guest needs a driver**.
//   A disc mounts because something on the guest side polls that id, and on
//   a System with no CD stack (no `Foreign File Access`) nothing does — the
//   medium goes in, the window says so, and no icon appears. That is guest
//   software, not the bus (`CHANGELOG.md` 2026-08-15 (fifth)).
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

struct GLFWwindow;

namespace pom68k {

// What one SCSI bay looks like to the window.
struct DiskBay {
    int         id       = 0;       // SCSI target id
    std::string path;               // empty = no medium
    bool        cd       = false;   // CD-ROM target (read-only, 2048-B blocks)
    bool        live     = false;   // medium can be swapped without a reboot
};

// Everything the window needs from a platform runner. Hooks left null mean
// "this machine cannot do that", and the window degrades to staged changes
// rather than hiding the control.
struct DiskBaysHost {
    // --- Current configuration (owned by the runner) ---
    std::string  romName;                       // argv[1] for a relaunch
    std::string  bootPath;                      // SCSI 0
    std::vector<std::string>* extras = nullptr; // SCSI 1..N, live view
    std::string  floppyPath;                    // "" when no drive / no disk
    bool         hasFloppyDrive = false;
    bool         supportsEmptyCdDrive = true;   // false on the undocked Duo

    // --- Hot-swap hooks (null => staged + reboot) ---
    // insertBay returns false when the bay exists but refused the image.
    std::function<bool(int id, const std::string& path)> insertBay;
    std::function<void(int id)>                          ejectBay;
    // True when SCSI id holds a CD-ROM target (the removable kind — the
    // only kind whose medium can change without a reboot). Null = the
    // runner cannot tell, and no bay swaps live.
    std::function<bool(int id)>                          bayIsCd;

    // --- Floppy hooks (already live on every machine that has a drive) ---
    std::function<void(const std::string&)> insertFloppy;
    std::function<void()>                   ejectFloppy;
    std::function<bool()>                   floppyInserted;

    // --- Machine control ---
    std::function<void()> hardReset;            // power cycle; ROM re-probes
    std::function<void(const std::string& boot,
                       const std::vector<std::string>& extras)> relaunch;
};

// Draw the "Disques…" entry inside an already-open menu. Toggles the window.
void diskBaysMenuItem();

// Draw the window itself (no-op while closed). Call once per frame, after
// the menu bar, from any runner's frame lambda.
void diskBaysWindow(DiskBaysHost& host);

// Install the GLFW drop callback so images can be dragged onto the window.
// Safe to call once per runner at start-up.
void diskBaysInstallDrop(GLFWwindow* window);

// Images discoverable from the usual places (hdv/, disks35/, the boot image's
// own directory) plus anything the user has dropped or typed this session.
// Accepts every extension the command line accepts.
std::vector<std::string> diskBaysKnownImages(const std::string& nearPath);

// CD image by extension (.iso/.cdr/.toast/.cue/.bin) — the ONE list, shared
// by the window and every runner's argv loop. Name-based on purpose: a .dsk
// that happens to be 2048-aligned is still a hard disk.
bool diskBaysPathIsCd(const std::string& path);

// The reserved-bay placeholder: an extras entry equal to this names an empty
// CD drive that must exist on the bus at boot (runners attachCdromEmpty it).
inline const char* kCdBayToken = "cdbay";

// ── Every machine that can have a CD drive boots with one ──────────────────
// Call once per runner, straight after the argv extras loop.
//
// This is the whole reason the window's CD row can behave like its floppy
// row. The hot-swap contract above is not negotiable — Classic Mac OS probes
// the bus once — so a disc can only be inserted live into a drive that was
// already there at boot. Leaving that to the user meant: find the checkbox at
// the bottom of the window, apply, restart, *then* pick the .iso. An empty
// drive costs one SCSI id and nothing else: the ROM enumerates the same seven
// targets either way, and a System with no CD driver simply never talks to it.
//
// It was OFF by default until 2026-08-15, on the argument that "it adds SCSI
// targets to every machine's bus, and the boot-etalon gates are timed against
// the bus as it stands today". The second half of that does not hold: the
// gates build their machines directly in `tests/`, and never run a line of
// this file. What it really cost was the GUI's usability.
//
// A machine whose memory has no `attachCdromEmpty` (the 68000 compacts, the
// undocked Duo) compiles this out and keeps `supportsEmptyCdDrive = false`.
// `POM68K_NO_CDBAY=1` opts out for a session — for anyone comparing a bus
// against a pre-2026-08-15 capture.
template <class Mem>
inline void ensureCdDrive(Mem& mem, std::vector<std::string>& extras) {
    if (std::getenv("POM68K_NO_CDBAY")) return;
    if (extras.size() >= 6) return;
    for (const std::string& e : extras)
        if (e == kCdBayToken || diskBaysPathIsCd(e)) return;   // already one
    const int id = int(extras.size()) + 1;
    if constexpr (requires { mem.attachCdromEmpty(id); }) {
        if (mem.attachCdromEmpty(id)) {
            extras.push_back(kCdBayToken);
            std::printf("SCSI CD %d: <vide> (lecteur par défaut)\n", id);
        }
    }
}

} // namespace pom68k
