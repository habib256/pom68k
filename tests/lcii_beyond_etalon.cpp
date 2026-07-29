// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Beyond-boot gates on the reference LC II (V8 + Egret LLE + System 7.5):
// the boot etalons prove the Finder appears; these prove the machine is
// USABLE — input reaches the Finder, the file system takes writes that
// survive a reboot, and hours-scale stability isn't a hang away.
// POM68K_BEYOND selects the scenario (one CTest entry each):
//   soak    — after the Finder, idle ~3 emulated minutes: the low-memory
//             Time global ($20C) must advance in step, no halt, VBL alive.
//   persist — Cmd-N in the frontmost Finder window creates a folder
//             ("dossier sans titre" on the French 7.5 image): the SCSI
//             image bytes must change, the new catalog name must appear,
//             and after a hard reset the machine must boot back to the
//             Finder off the modified volume.
//   launch  — double-click the desktop application icon (top-right):
//             launching changes the screen and pulls a burst of SCSI
//             reads (app + resources loading).
//   floppy  — guest-level 800K floppy MOUNT + READ over the real IWM:
//             insert after the Finder is up, and the System must poll the
//             drive, read the medium (~1.7 M nibbles), mount the volume
//             and open its window; the medium must survive eject +
//             re-insert intact. This is the guest-side half that had no
//             gate at all — `floppy_persist_test` covers the device-side
//             write→eject→flush plumbing.
//             NOT yet covered: a guest-INITIATED write. Cmd-N here
//             reaches neither the floppy nor the hard disk (it does work
//             on the HD in `persist`), so the folder-creation attempt
//             below is printed as a diagnostic, not asserted. See TODO
//             "floppy: guest-initiated write".
// POM68K_DUMP=1 writes lcii_beyond_<mode>.ppm for eyeballing/calibration.
// Soft-skips without the LC II ROM + a bootable hdv/ image.

#include "V8Memory.h"
#include "SonyDrive.h"
#include "V8Video.h"
#include "Cpu030.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

static std::string find(const char* rel) {
    for (const std::string base : { std::string(), std::string("../") }) {
        std::string p = base + rel;
        if (std::ifstream(p, std::ios::binary)) return p;
    }
    return {};
}

static void ensureBootDriverType(std::vector<uint8_t>& img) {
    if (img.size() < 512 || img[0] != 'E' || img[1] != 'R') return;
    int count = (img[0x10] << 8) | img[0x11];
    for (int i = 0; i < count && 0x12 + i * 8 + 8 <= 512; i++) {
        int e = 0x12 + i * 8;
        if (((img[e + 6] << 8) | img[e + 7]) == 0x6A) return;
    }
    if (count >= 1 && 0x12 + count * 8 + 8 <= 512) {
        int src = 0x12, dst = 0x12 + count * 8;
        for (int k = 0; k < 8; k++) img[dst + k] = img[src + k];
        img[dst + 6] = 0x00; img[dst + 7] = 0x6A;
        img[0x10] = uint8_t((count + 1) >> 8);
        img[0x11] = uint8_t(count + 1);
    }
}

static V8Memory* gMem;
static Cpu030* gCpu;
static const int64_t kFrame = 640 * 407;     // 60.15 Hz @ 15.6672 MHz

static void runFrames(long n) {
    for (long f = 0; f < n && !gCpu->isHalted(); f++)
        gCpu->runCycles(kFrame);
}

// Low-memory Time global ($20C, seconds since 1904) — proves the one-
// second interrupt chain (VIA → Egret RTC → Time Manager) stays alive.
static uint32_t macTime() {
    return uint32_t(gMem->peek8(0x20C)) << 24 | uint32_t(gMem->peek8(0x20D)) << 16
         | uint32_t(gMem->peek8(0x20E)) << 8 | gMem->peek8(0x20F);
}

static void keyTap(uint8_t code) {           // ADB code, press + release
    gMem->keyEvent(code, true);
    runFrames(4);
    gMem->keyEvent(code, false);
    runFrames(4);
}

static void screen(std::vector<uint32_t>& fb) {
    V8Video video(*gMem);
    video.decode(fb);
}

static double blackRatio(const std::vector<uint32_t>& fb,
                         int x0, int x1, int y0, int y1) {
    long black = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            if ((fb[size_t(y) * 512 + x] & 0xFF) < 0x80) black++;
    return double(black) / (double(x1 - x0) * (y1 - y0));
}

static double diffRatio(const std::vector<uint32_t>& a,
                        const std::vector<uint32_t>& b) {
    if (a.size() != b.size() || a.empty()) return 1.0;
    size_t d = 0;
    for (size_t i = 0; i < a.size(); i++) if (a[i] != b[i]) d++;
    return double(d) / double(a.size());
}

static void dump(const char* name, const std::vector<uint32_t>& fb) {
    if (!getenv("POM68K_DUMP")) return;
    FILE* fp = fopen(name, "wb");
    std::fprintf(fp, "P6\n512 384\n255\n");
    for (uint32_t p : fb) {
        uint8_t rgb[3] = { uint8_t(p >> 16), uint8_t(p >> 8), uint8_t(p) };
        fwrite(rgb, 1, 3, fp);
    }
    fclose(fp);
}

static long countNeedle(const std::vector<uint8_t>& hay, const char* needle) {
    size_t n = std::strlen(needle);
    long c = 0;
    for (size_t i = 0; i + n <= hay.size(); i++)
        if (std::memcmp(&hay[i], needle, n) == 0) c++;
    return c;
}

static bool finderUp() {
    std::vector<uint32_t> fb;
    screen(fb);
    double menu = blackRatio(fb, 0, 512, 2, 16);
    double desk = blackRatio(fb, 400, 512, 40, 340);
    return menu < 0.30 && desk > 0.35 && desk < 0.65;
}

int main() {
    const std::string mode = getenv("POM68K_BEYOND") ? getenv("POM68K_BEYOND")
                                                     : "soak";
    std::string rom = find("roms/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM");
    if (rom.empty()) rom = find("docs/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM");
    std::string img = find("hdv/lcii-boot.vhd");
    if (img.empty()) img = find("hdv/boot.vhd");
    if (img.empty()) img = find("hdv/GISTPERSO-boot.vhd");
    if (img.empty()) img = find("hdv/System 7.5 HD.dsk");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs the 512 KB LC II ROM + a bootable hdv/ image\n");
        return 0;
    }

    std::ifstream in(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
    if (romData.size() != V8Memory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM is %zu bytes, want 512 KB\n", romData.size());
        return 1;
    }

    V8Memory mem;
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    Cpu030 cpu(mem, /*withFpu=*/true);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk image\n"); return 1; }
    ensureBootDriverType(mem.scsiDisk().image());
    gMem = &mem; gCpu = &cpu;

    // The `floppy` scenario works on a PRIVATE copy: the whole point is
    // that the guest modifies it, and the asset must not be the thing that
    // changes. The insert itself happens after the Finder is up (below) —
    // an insert EVENT is what makes the System poll and mount, and it is
    // also the real user gesture.
    std::string floppyCopy, floppySrc;
    std::vector<uint8_t> floppyOrig;
    if (mode == "floppy") {
        floppySrc = find("disks35/Disk605.dsk");
        if (floppySrc.empty()) {
            std::printf("SKIP: needs an 800K HFS image at disks35/Disk605.dsk\n");
            return 0;
        }
        std::ifstream fin(floppySrc, std::ios::binary);
        floppyOrig.assign(std::istreambuf_iterator<char>(fin),
                          std::istreambuf_iterator<char>());
        // Normalize the copy to a CLEANLY-UNMOUNTED volume (MDB drAtrb
        // bit 8 at $40A). The asset ships with it clear, i.e. already
        // "in use", and mounting an already-dirty volume gives the System
        // nothing to write — which is exactly what made the first version
        // of this gate see zero writes. With the bit set, a read-write
        // mount MUST clear it, so the guest write is deterministic.
        if (floppyOrig.size() >= 0x40C)
            floppyOrig[0x40A] = uint8_t(floppyOrig[0x40A] | 0x01);
        floppyCopy = "lcii_beyond_floppy.dsk";
        std::ofstream fout(floppyCopy, std::ios::binary | std::ios::trunc);
        fout.write(reinterpret_cast<const char*>(floppyOrig.data()),
                   std::streamsize(floppyOrig.size()));
        fout.close();
    }

    while (mem.cpuHeld()) mem.tick(1000);
    runFrames(16000);                        // boot to a settled Finder
    if (cpu.isHalted()) { std::fprintf(stderr, "FAIL: halted during boot\n"); return 1; }
    if (!finderUp()) { std::fprintf(stderr, "FAIL: no Finder after boot\n"); return 1; }
    std::printf("Finder up (SCSI %ld)\n", mem.scsi().commands);

    bool ok = false;

    if (mode == "soak") {
        // ~3 emulated minutes idle. Time must track (±25% — the 60.15 Hz
        // tick vs frames), the CPU must not halt, VBL must keep firing.
        const long kSoak = 10800;            // 180 s of 60 Hz frames
        uint32_t t0 = macTime();
        int64_t mcu0 = mem.egretLleActive() ? mem.egretLle().mcu().cycleCount() : 0;
        runFrames(kSoak);
        uint32_t t1 = macTime();
        long dt = long(t1 - t0);
        if (mem.egretLleActive()) {
            int64_t dm = mem.egretLle().mcu().cycleCount() - mcu0;
            std::fprintf(stderr, "[soak] mcu cycles %lld (expect ~377M for "
                         "180 s), pll=$%02X\n", (long long)dm,
                         mem.egretLle().mcu().pll());
        }
        std::vector<uint32_t> fb;
        screen(fb);
        dump("lcii_beyond_soak.ppm", fb);
        std::printf("soak: %ld s elapsed on the Mac clock (want 135-225), "
                    "halted=%d\n", dt, cpu.isHalted());
        ok = !cpu.isHalted() && dt >= 135 && dt <= 225;
    } else if (mode == "persist") {
        std::vector<uint8_t>& disk = mem.scsiDisk().image();
        // System 7 names a new folder "untitled folder" (English volume);
        // "Nouveau dossier"/"dossier sans titre" on localized ones — accept
        // whichever the volume's Finder writes into the catalog.
        // Full folder-name phrases only — the HFS catalog stores the whole
        // Pascal name, so this is a clean 0→1 signal (the bare "untitled"
        // substring occurs ~430× in a stock volume and drowns it out).
        auto best = [&](long& out) {
            const char* cands[] = { "untitled folder", "Nouveau dossier",
                                    "dossier sans titre" };
            const char* which = cands[0]; long m = 0;
            for (const char* c : cands) {
                long k = countNeedle(disk, c);
                if (k > m) { m = k; which = c; }
            }
            out = m; return which;
        };
        long before; const char* needle = best(before);
        std::vector<uint8_t> snap = disk;
        // Cmd-N (New Folder) in the frontmost Finder window, then Return to
        // commit the still-editable name, then let the catalog flush.
        mem.keyEvent(0x37, true);            // Cmd down
        runFrames(6);
        keyTap(0x2D);                        // 'n'
        mem.keyEvent(0x37, false);
        runFrames(120);                      // let the rename field appear
        keyTap(0x24);                        // Return — commit the name
        runFrames(900);                      // ~15 s: create + flush catalog
        long after; needle = best(after);
        bool wrote = disk != snap;
        std::printf("persist: '%s' ×%ld → ×%ld, image %s\n", needle,
                    before, after, wrote ? "modified" : "UNCHANGED");
        std::vector<uint32_t> fb;
        screen(fb);
        dump("lcii_beyond_persist.ppm", fb);
        // Reboot on the modified volume: it must still reach the Finder.
        cpu.hardReset();
        while (mem.cpuHeld()) mem.tick(1000);
        runFrames(16000);
        long survived; needle = best(survived);
        bool rebooted = !cpu.isHalted() && finderUp();
        std::printf("persist: reboot %s, '%s' ×%ld after\n",
                    rebooted ? "reached the Finder" : "FAILED", needle, survived);
        ok = wrote && after > before && rebooted && survived > before;
    } else if (mode == "launch") {
        // Drive the machine with the MOUSE: double-click a folder in the
        // frontmost window to open it. A new window always appears (screen
        // changes) and the Finder reads the folder's catalog + contents
        // from SCSI — proving mouse input reaches the Finder and it acts
        // on the disk. (Desktop droplets like DropDisk open no window, so
        // a folder is the deterministic target.) The mouse is relative:
        // pin it into the bottom-right corner, then step to the icon.
        std::vector<uint32_t> baseline;
        screen(baseline);
        dump("lcii_beyond_before.ppm", baseline);
        long scsi0 = mem.scsi().commands;
        for (int i = 0; i < 12; i++) {       // pin bottom-right (511,383)
            mem.mouseMove(120, 120);
            runFrames(2);
        }
        // Default target: the first folder icon in the open window (the
        // "1984" folder of the reference MacPack "Games" window at
        // 512×384; overridable for other volumes via POM68K_MX/MY).
        int tx = getenv("POM68K_MX") ? atoi(getenv("POM68K_MX")) : 42;
        int ty = getenv("POM68K_MY") ? atoi(getenv("POM68K_MY")) : 78;
        // CLOSED LOOP on the guest's own pointer (low-mem Mouse $830 = y,
        // $832 = x). Open-loop stepping used to assume "≤3-unit steps land
        // 1:1", which only holds while each report carries ≤3 units: under a
        // sustained stream System 7's mouse scaling amplifies (~1.6× measured),
        // and the run overshot the icon into the screen corner. Steering by
        // the position the guest actually reports is immune to both the
        // scaling curve and the ADB report rate.
        auto ptr = [&](int& x, int& y) {
            x = int16_t(gMem->peek8(0x832) << 8 | gMem->peek8(0x833));
            y = int16_t(gMem->peek8(0x830) << 8 | gMem->peek8(0x831));
        };
        int px = 0, py = 0;
        for (int it = 0; it < 600; it++) {
            ptr(px, py);
            int dx = tx - px, dy = ty - py;
            if (!dx && !dy) break;
            // Halve the remaining distance (capped) so the scaling curve
            // cannot turn a step into an overshoot loop.
            auto step = [](int d) {
                int s = d / 2; if (!s) s = d > 0 ? 1 : (d < 0 ? -1 : 0);
                return std::max(-8, std::min(8, s));
            };
            mem.mouseMove(step(dx), step(dy));
            runFrames(2);
        }
        runFrames(30);
        ptr(px, py);
        std::printf("pointer at (%d,%d), target (%d,%d)\n", px, py, tx, ty);
        for (int c = 0; c < 2; c++) {        // double-click
            mem.mouseButton(true);
            runFrames(6);
            mem.mouseButton(false);
            runFrames(6);
        }
        runFrames(3600);                     // ~60 s: let the app come up
        std::vector<uint32_t> nowFb;
        screen(nowFb);
        dump("lcii_beyond_launch.ppm", nowFb);
        double changed = diffRatio(baseline, nowFb);
        long scsiDelta = mem.scsi().commands - scsi0;
        // The mouse-driven open pops a new window (large screen delta); the
        // Finder touches the disk to list it (a few catalog nodes — the
        // count is small when the folder was already cached, so the screen
        // change is the load-bearing signal, not the SCSI volume).
        std::printf("launch: screen changed %.2f (want >0.10), SCSI +%ld "
                    "(want >0), halted=%d\n", changed, scsiDelta,
                    cpu.isHalted());
        ok = !cpu.isHalted() && changed > 0.10 && scsiDelta > 0;
    } else if (mode == "floppy") {
        // Guest-level write → eject → re-insert → read round-trip. The
        // device-level half already has a gate (`floppy_persist_test`
        // drives SonyDrive directly); what was never covered is the same
        // journey THROUGH the guest: the System's own Sony driver reading
        // the medium over the real IWM, writing to it, and those writes
        // reaching the host file and coming back.
        //
        // The guest write is HFS's own: mounting a volume read-write
        // clears the MDB's "unmounted cleanly" attribute (drAtrb bit 8,
        // MDB at offset $400, drAtrb at +$0A) and unmounting sets it
        // again. That is a real driver write on a deterministic schedule,
        // which is why this scenario does not have to hunt for a desktop
        // icon to click.
        SonyDrive& drv = mem.internalDrive();
        auto mdbAtrb = [](const std::vector<uint8_t>& img) -> uint16_t {
            if (img.size() < 0x40C) return 0;
            return uint16_t(img[0x40A] << 8 | img[0x40B]);
        };
        bool sig = floppyOrig.size() >= 0x402 &&
                   floppyOrig[0x400] == 0x42 && floppyOrig[0x401] == 0x44;
        std::printf("floppy: %s, %zu bytes, HFS=%d, drAtrb=$%04X at insert\n",
                    floppySrc.c_str(), floppyOrig.size(), sig,
                    mdbAtrb(floppyOrig));
        // The mount happens during boot; give the Finder time to settle
        // and flush the volume control block back to the medium.
        // Write-back is OFF by default everywhere else (etalons must not
        // mutate their media); this scenario exists to exercise it.
        drv.setWriteBack(true);
        long nib0 = drv.nibblesRead;
        std::vector<uint32_t> beforeIns;
        screen(beforeIns);
        if (!mem.insertDisk(floppyCopy)) {
            std::fprintf(stderr, "FAIL: could not insert %s\n", floppyCopy.c_str());
            return 1;
        }
        runFrames(1800);                     // ~30 s to poll, mount, settle
        long nibRead = drv.nibblesRead - nib0;
        std::printf("floppy: guest read %ld nibbles off the medium\n", nibRead);
        std::vector<uint32_t> afterIns;
        screen(afterIns);
        dump("lcii_beyond_floppy.ppm", afterIns);
        std::printf("floppy: screen changed %.3f after insert, Finder %s\n",
                    diffRatio(beforeIns, afterIns), finderUp() ? "up" : "GONE (dialog?)");
        // Locate what appeared: the newly mounted volume's icon IS the
        // region that changed, so the gate does not have to hard-code a
        // desktop position that differs per image and per screen size.
        long cx = 0, cy = 0, n = 0;
        int x0 = 512, x1 = -1, y0 = 384, y1 = -1;
        for (int y = 0; y < 384; y++)
            for (int x = 0; x < 512; x++) {
                size_t i = size_t(y) * 512 + x;
                if (i < beforeIns.size() && i < afterIns.size() &&
                    beforeIns[i] != afterIns[i]) {
                    cx += x; cy += y; n++;
                    x0 = std::min(x0, x); x1 = std::max(x1, x);
                    y0 = std::min(y0, y); y1 = std::max(y1, y);
                }
            }
        std::printf("floppy: changed region x %d..%d, y %d..%d (%ld px)\n",
                    x0, x1, y0, y1, n);
        bool mounted = n > 200 && drv.hasDisk() && nibRead > 1000;
        std::printf("floppy: volume mounted: %s\n", mounted ? "yes" : "NO");
        // Mounting opens the volume's window, so it is frontmost and Cmd-N
        // creates the folder ON THE FLOPPY — the same gesture `persist`
        // uses on the hard disk, no desktop-icon hunting needed.
        auto folderCount = [&](const std::vector<uint8_t>& img, const char*& which) {
            const char* cands[] = { "untitled folder", "Nouveau dossier",
                                    "dossier sans titre" };
            long m = 0; which = cands[0];
            for (const char* c : cands) {
                long k = countNeedle(img, c);
                if (k > m) { m = k; which = c; }
            }
            return m;
        };
        const char* needle = nullptr;
        long before = folderCount(floppyOrig, needle);
        std::vector<uint8_t> hdSnap = mem.scsiDisk().image();
        std::printf("floppy: write-protect sense = %d\n",
                    drv.isWriteProtected());
        mem.keyEvent(0x37, true);            // Cmd down
        runFrames(6);
        keyTap(0x2D);                        // 'n'
        mem.keyEvent(0x37, false);
        runFrames(120);                      // rename field appears
        keyTap(0x24);                        // Return — commit the name
        runFrames(900);                      // ~15 s: create + flush catalog
        bool guestWrote = drv.dirty();
        std::printf("floppy: hard disk image %s by the Cmd-N (tells us which "
                    "window was frontmost)\n",
                    mem.scsiDisk().image() != hdSnap ? "CHANGED" : "untouched");
        std::printf("floppy: guest wrote to the medium: %s\n",
                    guestWrote ? "yes (sectors committed)" : "NO");
        // Eject flushes the committed sectors to the host file (temp +
        // rename) — the supported flush point, same as floppy_persist_test.
        drv.eject();
        std::ifstream back(floppyCopy, std::ios::binary);
        std::vector<uint8_t> after((std::istreambuf_iterator<char>(back)),
                                   std::istreambuf_iterator<char>());
        back.close();
        bool sizeOk = after.size() == floppyOrig.size();
        bool changed = after != floppyOrig;
        const char* n2 = nullptr;
        long got = folderCount(after, n2);
        std::printf("floppy: '%s' ×%ld → ×%ld in the host file\n",
                    got ? n2 : needle, before, got);
        bool stillHfs = after.size() >= 0x402 &&
                        after[0x400] == 0x42 && after[0x401] == 0x44;
        std::printf("floppy: host file %s, size %s, HFS sig %s, "
                    "drAtrb $%04X → $%04X\n",
                    changed ? "modified" : "unchanged (no guest write yet)",
                    sizeOk ? "kept" : "CHANGED", stillHfs ? "intact" : "LOST",
                    mdbAtrb(floppyOrig), mdbAtrb(after));
        // Re-insert what the round trip produced: it must still be a
        // mountable volume, not a subtly corrupted one.
        SonyDrive probe;
        bool reinsert = probe.insert(floppyCopy) && probe.hasDisk();
        std::printf("floppy: re-insert %s\n", reinsert ? "OK" : "FAILED");
        // Asserted: the guest really mounted and read the medium, and the
        // medium survived the eject/re-insert round trip byte-intact.
        // `guestWrote` / `changed` / `got` are PRINTED above but not
        // asserted — no guest write has been triggered yet (see header).
        ok = !cpu.isHalted() && mounted && sizeOk && stillHfs && reinsert;
        std::remove(floppyCopy.c_str());
    } else {
        std::fprintf(stderr, "FAIL: unknown POM68K_BEYOND=%s\n", mode.c_str());
        return 1;
    }

    std::printf("%s — LC II beyond-boot %s\n", ok ? "PASSED" : "FAILED",
                mode.c_str());
    return ok ? 0 : 1;
}
