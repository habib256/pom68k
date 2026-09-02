// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Dev tool (not a gate): guest-driven Shut Down of an LC II boot volume —
// the 030 sibling of q605_shutdown_flush, for the references only 030-class
// machines boot (GISTPERSO, System 7.1). Same contract: a reference fixture
// must be recorded from a volume the guest itself unmounted, and no host
// tool can set drAtrb bit 8 honestly.
//
//   POM68K_SHUTDOWN_OUT=path   where the flushed image lands (required)
//   POM68K_SHUTDOWN_IMG        image to boot (default hdv/GISTPERSO-boot.vhd)
//   POM68K_SHUTDOWN_PHASE=N    stop after phase N (calibration)
//   POM68K_SHUTDOWN_SPECIAL_X / POM68K_SHUTDOWN_ITEM_Y
//                              Special title / Shut Down row clicks
//   POM68K_SHUTDOWN_SHIFT=0    do NOT hold Shift through the boot
//
// Shift is held by default from the extension phase to the Finder: the
// GISTPERSO volume auto-launches SimCity 2000 from Startup Items and that
// launch races Finder init (pom68k-gistperso-75-hang) — a flush tool wants
// a quiet desktop, and skipping Startup Items does not change what a clean
// unmount writes. Steering and screen decode are the LC II launch leg's.

#include "AssetFingerprint.h"
#include "FinderSignature.h"
#include "LciiApplicationHarness.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

using lciiapp::gCpu;
using lciiapp::gMem;

int main() {
    const char* outPath = getenv("POM68K_SHUTDOWN_OUT");
    const int stopPhase = getenv("POM68K_SHUTDOWN_PHASE")
                        ? atoi(getenv("POM68K_SHUTDOWN_PHASE")) : 99;
    if (!outPath && stopPhase > 2) {
        std::fprintf(stderr, "usage: POM68K_SHUTDOWN_OUT=<flushed image> "
                             "[POM68K_SHUTDOWN_IMG=hdv/...] "
                             "lcii_shutdown_flush\n");
        return 2;
    }

    std::string rom = lciiapp::find("roms/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM");
    std::string img = getenv("POM68K_SHUTDOWN_IMG")
                    ? testasset::find(getenv("POM68K_SHUTDOWN_IMG"))
                    : testasset::find("hdv/GISTPERSO-boot.vhd");
    if (rom.empty() || img.empty()) {
        std::fprintf(stderr, "FAIL: needs the LC II ROM + boot image\n");
        return 1;
    }
    testasset::report({ rom, img });

    const testasset::HfsInfo hfs = testasset::probeHfs(img);
    if (!hfs.found) {
        std::fprintf(stderr, "FAIL: no HFS MDB in %s\n", img.c_str());
        return 1;
    }
    std::printf("volume '%s' atrb=$%04X (%s) MDB@%llu\n", hfs.name.c_str(),
                hfs.atrb, hfs.cleanlyUnmounted() ? "clean" : "DIRTY",
                (unsigned long long)hfs.mdbOffset);
    std::fflush(stdout);

    std::ifstream in(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
    if (romData.size() != V8Memory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM is %zu bytes\n", romData.size());
        return 1;
    }
    V8Memory mem(pom68k::defaultCoreConfig());
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    Cpu030 cpu(mem, jit::defaultResolvedConfig(),
               pom68k::defaultCoreConfig().cpu, /*withFpu=*/true);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk\n"); return 1; }
    lciiapp::ensureBootDriverType(mem.scsiDisk().image());
    // The Egret holds the CPU at power-on; without this release the machine
    // runs forever without issuing one SCSI command (lcii_simcity_census
    // learned it first, and this tool re-learned it on 2026-09-01).
    while (mem.cpuHeld()) mem.tick(1000);
    gMem = &mem;
    gCpu = &cpu;

    // A FLAT (bare-HFS .dsk) input is wrapped in a 96-block façade by the
    // SCSI attach, so the file's MDB offset no longer addresses the vector:
    // the volume's bytes sit AFTER the synthetic prefix, and the prefix
    // must never become part of the recorded reference. An APM .vhd keeps
    // its offsets 1:1.
    const uint64_t fileSize = testasset::fileSize(img);
    const uint64_t facade = mem.scsiDisk().image().size() > fileSize
                          ? mem.scsiDisk().image().size() - fileSize : 0;
    const uint64_t vecMdb = facade + hfs.mdbOffset;
    if (facade)
        std::printf("flat input: %llu façade bytes, MDB in memory at %llu\n",
                    (unsigned long long)facade, (unsigned long long)vecMdb);
    auto cleanBit = [&]() {
        const std::vector<uint8_t>& i = mem.scsiDisk().image();
        return vecMdb + 12 <= i.size() && (i[vecMdb + 10] & 0x01) != 0;
    };
    auto curApp = [&]() { return findersig::curApName(mem); };

    // ── Phase 0: boot to a quiet Finder, Shift held past Startup Items ───
    // A Shift pressed before the ADB link is alive is swallowed, and one
    // pressed after the Finder starts is too late for Startup Items. The
    // observable in between: the boot ROM's own SCSI reads — press when
    // the disk is being read, hold until the Finder is up.
    const bool holdShift = !getenv("POM68K_SHUTDOWN_SHIFT") ||
                           atoi(getenv("POM68K_SHUTDOWN_SHIFT")) != 0;
    bool shiftDown = false, finder = false;
    for (long frame = 0; frame < 60000 && !cpu.isHalted(); frame += 60) {
        lciiapp::runFrames(60);
        if (holdShift && !shiftDown && mem.scsiDisk().readCommands > 50) {
            mem.keyEvent(0x38, true);              // Shift down, and HELD
            shiftDown = true;
            std::printf("boot: Shift held at frame %ld (%ld reads)\n",
                        frame, mem.scsiDisk().readCommands);
            std::fflush(stdout);
        }
        if (lciiapp::finderUp() && curApp() == "Finder") { finder = true; break; }
        // Periodic Return past 12000 frames: CautionAlerts and the
        // dirty-volume dialog, the beyond gates' adaptive-tap trick.
        if (frame >= 12000 && frame % 1200 == 0) lciiapp::keyHold(0x24, 150);
    }
    if (shiftDown) mem.keyEvent(0x38, false);
    if (!finder || cpu.isHalted()) {
        lciiapp::dump("lcii_shutdown_fail.ppm");
        std::fprintf(stderr,
                     "FAIL: no Finder (halted=%d, finderUp=%d, front \"%s\", "
                     "disk reads %ld writes %ld)\n",
                     cpu.isHalted(), lciiapp::finderUp(), curApp().c_str(),
                     mem.scsiDisk().readCommands, mem.scsiDisk().writeCommands);
        return 1;
    }
    // Let the desktop stop redrawing before aiming a menu click at it.
    for (int poll = 0; poll < 40; poll++) {
        const uint64_t a = lciiapp::screenFingerprint();
        lciiapp::runFrames(120);
        if (lciiapp::screenFingerprint() == a) break;
    }
    lciiapp::runFrames(300);
    lciiapp::dump("lcii_shutdown_0_finder.ppm");
    std::printf("phase 0: Finder up, SCSI=%ld, front \"%s\", clean=%d\n",
                mem.scsi().commands, curApp().c_str(), cleanBit());
    std::fflush(stdout);
    if (stopPhase <= 0) return 0;

    // POM68K_SHUTDOWN_CLOSEWIN=N: close N Finder windows (Cmd-W) before
    // shutting down. The Finder REOPENS at next boot whatever was open at
    // shutdown, so a window left in a reference image becomes a permanent
    // 350-px light run that the roster's dialog detector rejects (the
    // System 7.5 volume shipped with its System Folder window open —
    // sonora_beyond_bootfail.ppm, 2026-09-02).
    if (const char* cw = getenv("POM68K_SHUTDOWN_CLOSEWIN")) {
        for (int i = atoi(cw); i > 0; i--) {
            mem.keyEvent(0x37, true);          // Cmd
            lciiapp::runFrames(6);
            lciiapp::keyHold(0x0D, 75);        // W, held past Slow Keys
            mem.keyEvent(0x37, false);
            lciiapp::runFrames(300);
        }
        lciiapp::dump("lcii_shutdown_0b_closed.ppm");
    }

    // ── Phase 1: Special → Shut Down (the launch leg's closed loop) ──────
    auto steer = [&](int tx, int ty) {
        int px = 0, py = 0;
        auto ptr = [&]() {
            px = int16_t(mem.peek8(0x832) << 8 | mem.peek8(0x833));
            py = int16_t(mem.peek8(0x830) << 8 | mem.peek8(0x831));
        };
        for (int it = 0; it < 600; it++) {
            ptr();
            const int dx = tx - px, dy = ty - py;
            if (!dx && !dy) return true;
            auto step = [](int d) {
                int s = d / 2; if (!s) s = d > 0 ? 1 : (d < 0 ? -1 : 0);
                return std::max(-8, std::min(8, s));
            };
            mem.mouseMove(step(dx), step(dy));
            lciiapp::runFrames(2);
        }
        ptr();
        std::fprintf(stderr, "steer: wanted (%d,%d), reached (%d,%d)\n",
                     tx, ty, px, py);
        return std::abs(tx - px) <= 3 && std::abs(ty - py) <= 3;
    };
    // System 7 menus are NOT Mac OS 8's sticky menus: a press-release on
    // the title opens and immediately closes them. Choosing an item is
    // press on the title → DRAG with the button held → release on the row
    // (measured 2026-09-01: the first walk clicked "Présentation" shut).
    // On the French GISTPERSO 7.5 the title is "Spécial" at ~330 and the
    // item is "Éteindre", the last row.
    const int spX = getenv("POM68K_SHUTDOWN_SPECIAL_X")
                  ? atoi(getenv("POM68K_SHUTDOWN_SPECIAL_X")) : 330;
    if (!steer(spX, 8)) { std::fprintf(stderr, "FAIL: Special title\n"); return 1; }
    mem.mouseButton(true);
    lciiapp::runFrames(18);
    lciiapp::dump("lcii_shutdown_1_menu.ppm");
    if (stopPhase <= 1) { mem.mouseButton(false); return 0; }
    const int itY = getenv("POM68K_SHUTDOWN_ITEM_Y")
                  ? atoi(getenv("POM68K_SHUTDOWN_ITEM_Y")) : 110;
    if (!steer(spX + 10, itY)) {
        mem.mouseButton(false);
        std::fprintf(stderr, "FAIL: drag to Shut Down\n");
        return 1;
    }
    lciiapp::runFrames(12);
    mem.mouseButton(false);
    lciiapp::runFrames(60);
    std::printf("phase 1: Shut Down chosen (drag release at %d,%d)\n",
                spX + 10, itY);
    std::fflush(stdout);

    // ── Phase 2: the guest's own flush lands the clean bit ───────────────
    long waited = 0;
    for (; waited < 14400 && !cleanBit(); waited += 60) {
        lciiapp::runFrames(60);
        if (cpu.isHalted()) break;             // "safe to switch off" halt
    }
    lciiapp::dump("lcii_shutdown_2_done.ppm");
    const std::vector<uint8_t>& image = mem.scsiDisk().image();
    const uint16_t atrb = vecMdb + 12 <= image.size()
        ? uint16_t(uint16_t(image[vecMdb + 10]) << 8 | image[vecMdb + 11])
        : 0;
    std::printf("phase 2: %ld frames, atrb=$%04X (%s), halted=%d, writes=%ld\n",
                waited, atrb, (atrb & 0x0100) ? "CLEAN" : "still dirty",
                cpu.isHalted(), mem.scsiDisk().writeBlocks);
    std::fflush(stdout);
    if (!(atrb & 0x0100)) {
        std::fprintf(stderr, "FAIL: the guest never landed the clean bit\n");
        return 1;
    }
    if (stopPhase <= 2) return 0;

    // ── Phase 3: write the flushed image in the FILE's own format ────────
    // Flat input: drop the synthetic façade — the reference stays bare HFS
    // and every harness re-wraps it. APM input: restore the file's block 0,
    // because ensureBootDriverType() patched a type-$6A DDM entry into the
    // in-memory copy and that patch must NOT become part of the recorded
    // reference (every LC II harness re-applies it).
    std::vector<uint8_t> out(image.begin() + std::ptrdiff_t(facade),
                             image.end());
    if (!facade) {
        std::ifstream src(img, std::ios::binary);
        src.read(reinterpret_cast<char*>(out.data()),
                 std::min<std::streamsize>(512, std::streamsize(out.size())));
    }
    std::ofstream o(outPath, std::ios::binary | std::ios::trunc);
    o.write(reinterpret_cast<const char*>(out.data()),
            std::streamsize(out.size()));
    if (!o) { std::fprintf(stderr, "FAIL: cannot write %s\n", outPath); return 1; }
    std::printf("PASSED — flushed '%s' (%zu bytes) to %s\n",
                hfs.name.c_str(), out.size(), outPath);
    return 0;
}
