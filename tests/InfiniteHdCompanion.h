// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The Infinite Mac System images carry an alias to a volume named
// "Infinite HD" — the site's software library, mounted as a second disk
// at every session — in their Startup Items. Booted alone, the Finder
// opens the alias, cannot find the disk, and puts up « The alias "Infinite
// HD" could not be opened, because the disk "Infinite HD" could not be
// found » (Stop / Continue) over the finished desktop — measured on the
// LC 520 on stock 7.5.3, 2026-09-16 (lc520_screen.ppm). A gate that judges
// the screen sees an alert; a gate that types sends its keys into it.
//
// The honest companion: an EMPTY HFS volume of that name on the next SCSI
// ID, built by the host (HfsBlankVolume.h). The alias resolves, no alert,
// and the Finder shows a second, empty disk — the image itself stays the
// pinned bytes. Attached only when the boot volume is one of those images.

#pragma once

#include "HfsBlankVolume.h"
#include "PortableEnv.h"

#include <cstdio>
#include <string>
#include <vector>
#if defined(_WIN32)
#include <process.h>
#define POM68K_GETPID _getpid
#else
#include <unistd.h>
#define POM68K_GETPID getpid
#endif

namespace infinitehd {

// True for the Infinite Mac System images (Images/System *.dsk).
inline bool wants(const std::string& imagePath) {
    return imagePath.find("System ") != std::string::npos &&
           imagePath.find(" HD.dsk") != std::string::npos;
}

// A 5 MB blank "Infinite HD" on SCSI `id`. Returns false only when the
// volume could not be written or attached; a machine that does not want
// one returns true and attaches nothing.
template <class Mem>
inline bool attach(Mem& mem, const std::string& imagePath, int id = 1) {
    if (!wants(imagePath)) return true;
    // One file per process: gates run in parallel under ctest -j, and a
    // shared name lost a race twice (a 1 s red, 2026-09-16). The bytes are
    // in memory once attached (write-back off), so the file goes at once.
    const std::string path = pom68kTempPath(
        ("infinite_hd_companion_" + std::to_string(long(POM68K_GETPID())) + ".img").c_str());
    std::vector<uint8_t> blank = hfsblank::build(5ull << 20, "Infinite HD");
    blank[1024 + 10] |= 0x01;                       // unmounted cleanly
    const bool ok = hfsblank::writeFile(path, blank) && mem.attachScsi(path, false, id);
    std::remove(path.c_str());
    if (!ok) {
        std::fprintf(stderr, "FAIL: cannot attach the \"Infinite HD\" companion\n");
        return false;
    }
    std::printf("companion: blank \"Infinite HD\" on SCSI %d for the Startup Items alias\n", id);
    return true;
}

} // namespace infinitehd
