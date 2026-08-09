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
//   cdrom   — NOT registered as a CTest yet: the guest reads the disc but
//             does not mount it (4 READ commands = 8 KB of probes, no
//             catalog). Kept as the reproducer — see TODO "CD: the guest
//             does not mount the disc". Guest-level CD-ROM mount: attach
//             an ISO/.toast at SCSI 3
//             (the Apple CD address) and require the System to notice the
//             disc, read it, and put its volume on the desktop. Proves
//             the whole target — INQUIRY $05, the Apple magic MODE SENSE
//             page $30, READ TOC, 2048-byte READ(10) — against a real
//             guest driver rather than a unit-test CDB.
//   floppy  — guest-level 800K floppy insert + MOUNT + guest write over
//             the real SWIM1: insert after the Finder is up; the System
//             must poll the drive, read the medium, put the volume's
//             icon on the desktop (asserted on the icon strip), clear
//             the MDB's "cleanly unmounted" bit (the deterministic
//             read-write-mount write), and the medium must survive
//             eject + re-insert intact. The mount is the 2026-08-05
//             floppy-boost-gate fix (CHANGELOG (eighth)/(ninth)): the
//             CPU wrappers freeze the i-cache boost to 1 while the
//             motor runs, restoring Apple's hand-tuned denibble timing
//             against the IWM's 14-tick hold. Negative control:
//             POM68K_FLOPPY_BOOST_GATE=0 brings back the "unreadable —
//             Initialize?" dialog (which is what this gate saw — and
//             mistook for a window — for ten months; retraction in
//             CHANGELOG 2026-08-05 (sixth)). `floppy_persist_test`
//             covers the device-side write→eject→flush plumbing.
// POM68K_DUMP=1 writes lcii_beyond_<mode>.ppm for eyeballing/calibration.
// Soft-skips without the LC II ROM + a bootable hdv/ image.

#include "AssetFingerprint.h"
#include "FolderProbe.h"
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

// The menu bar stays visible whatever windows are open — so this is the
// liveness check to use once the guest has been made to open something.
// `finderUp()` also samples the DESKTOP, which a new window covers: that
// is a signature of "idle Finder", not of "Finder alive".
static bool menuBarUp() {
    std::vector<uint32_t> fb;
    screen(fb);
    return blackRatio(fb, 0, 512, 2, 16) < 0.30;
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
    testasset::report({ rom, img });

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
    // A CD is attached BEFORE the boot: that is how a disc is actually
    // present (and the only way an install CD can be), and a SCSI target
    // appearing mid-run wedges the guest to a black screen — worth
    // knowing, and noted in TODO rather than papered over.
    if (mode == "cdrom") {
        std::string iso = find("hdv/Apeiron_1_0_3.toast");
        if (iso.empty()) iso = find("hdv/GliderPRO_1_1_2.toast");
        if (iso.empty()) iso = find("hdv/TIM_3.iso");
        if (iso.empty()) {
            std::printf("SKIP: needs a CD image in hdv/ (.iso/.toast)\n");
            return 0;
        }
        if (!mem.attachCdrom(iso)) {
            std::fprintf(stderr, "FAIL: could not mount %s as a CD\n", iso.c_str());
            return 1;
        }
        std::printf("cdrom: %s at SCSI 3 (present at power-on)\n", iso.c_str());
    }

    std::string floppyCopy, floppySrc;
    std::vector<uint8_t> floppyOrig;
    if (mode == "floppy") {
        // POM68K_FLOPPY_IMG crosses image against machine (the 2026-08-02
        // lesson): Rogue.dsk mounts on the Q605/SWIM2, so running it here
        // separates "bad image" from "bad SWIM1-IWM read path".
        if (const char* img = getenv("POM68K_FLOPPY_IMG"))
            floppySrc = find(img);
        if (floppySrc.empty()) floppySrc = find("disks35/Disk605.dsk");
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
        long before[folderprobe::kCount];
        folderprobe::sample(disk, before, "before");
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
        long after[folderprobe::kCount];
        folderprobe::sample(disk, after, "after");
        const size_t grew = folderprobe::grew(before, after);
        bool wrote = disk != snap;
        std::printf("persist: %s, image %s\n",
                    grew < folderprobe::kCount
                        ? (std::string("'") + folderprobe::kNames[grew] + "' " +
                           std::to_string(before[grew]) + " -> " +
                           std::to_string(after[grew])).c_str()
                        : "NO candidate folder name appeared",
                    wrote ? "modified" : "UNCHANGED");
        std::vector<uint32_t> fb;
        screen(fb);
        dump("lcii_beyond_persist.ppm", fb);
        // Reboot on the modified volume: it must still reach the Finder.
        cpu.hardReset();
        while (mem.cpuHeld()) mem.tick(1000);
        runFrames(16000);
        long survived[folderprobe::kCount];
        folderprobe::sample(disk, survived, "reboot");
        bool rebooted = !cpu.isHalted() && finderUp();
        const bool kept = grew < folderprobe::kCount &&
                          survived[grew] > before[grew];
        std::printf("persist: reboot %s, folder %s\n",
                    rebooted ? "reached the Finder" : "FAILED",
                    kept ? "survived" : "did NOT survive");
        ok = wrote && grew < folderprobe::kCount && rebooted && kept;
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
    } else if (mode == "cdrom") {
        // The disc was in the drive at power-on, so by the time the Finder
        // is up the System has already read it through the real 2048-byte
        // CD path (INQUIRY $05, the Apple magic page $30, READ TOC,
        // READ(10)) and put its volume on the desktop.
        long cdReads = mem.scsiDiskAt(3).readCommands;
        std::vector<uint32_t> fb;
        screen(fb);
        dump("lcii_beyond_cdrom.ppm", fb);
        // Reading the disc's catalog costs real traffic on top of a
        // hard-disk-only boot; the desktop keeps its Finder signature
        // because a mounted CD adds an icon, not a full-screen window.
        std::printf("cdrom: %ld READs served BY THE CD, menu bar %s, "
                    "Finder %s\n", cdReads, menuBarUp() ? "up" : "GONE",
                    finderUp() ? "up" : "desktop covered");
        // Count reads at the CD TARGET, never the controller total: the
        // boot volume's ~9600 commands drown the difference between a
        // mounted disc and an ignored one (9619 vs 9618 — measured).
        // Mounting a volume costs its MDB, catalog and desktop database:
        // dozens of reads, not a handful of probes.
        ok = !cpu.isHalted() && menuBarUp() && cdReads > 30;
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
        // Which personality served that read, and where the head ended up.
        // A driver that never finds an address mark gives up at a fixed
        // track; one that mis-selects the chip half reads garbage from
        // the ISM registers instead (TODO §1 hunt).
        std::printf("floppy: SWIM1 personality=%s mode=$%02X, head at track "
                    "%d, motor %s, sense CSTIN=%d TKO=%d\n",
                    mem.swim().ism() ? "ISM" : "IWM", mem.swim().ismModeReg(),
                    drv.currentTrack(), drv.motorOn() ? "on" : "off",
                    drv.sense(0x1), drv.sense(0x5));
        // Did the driver actually GET the stream? `overwritten` counts
        // nibbles replaced before the CPU read them (polling too slow for
        // the pacing), dataHits/dataReads is the poll success rate, and
        // the `consumed` ring holds the last 512 nibbles the CPU took —
        // a healthy GCR read shows sync $FF runs and the $D5 $AA $96
        // address mark. No marks in 512 nibbles = the driver is being fed
        // a stream it cannot frame.
        {
            const Iwm& iwm = mem.iwm();
            long marks = 0, syncs = 0;
            for (int i = 0; i < 512; i++) {
                int j = (iwm.consumedPos + i) & 511;
                if (iwm.consumed[j] == 0xFF) syncs++;
                if (iwm.consumed[j] == 0xD5 &&
                    iwm.consumed[(j + 1) & 511] == 0xAA &&
                    (iwm.consumed[(j + 2) & 511] == 0x96 ||
                     iwm.consumed[(j + 2) & 511] == 0xAD)) marks++;
            }
            std::printf("floppy: IWM re-reads of a latched byte: %ld\n",
                        iwm.reReads);
            std::printf("floppy: IWM polls %ld, hits %ld (%.1f%%), "
                        "overwritten %ld; last 512 consumed: %ld sync $FF, "
                        "%ld address/data marks\n",
                        iwm.dataReads, iwm.dataHits,
                        iwm.dataReads ? 100.0 * double(iwm.dataHits)
                                      / double(iwm.dataReads) : 0.0,
                        iwm.overwritten, syncs, marks);
            std::printf("floppy: consumed tail:");
            for (int i = 512 - 24; i < 512; i++)
                std::printf(" %02X", iwm.consumed[(iwm.consumedPos + i) & 511]);
            std::printf("\n");
        }
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
        // "Screen changed + disk in drive + nibbles read" is NOT a mount:
        // for ten months that exact signature was the System 7.5 INIT
        // DIALOG ("This disk is unreadable — Initialize?"), whose box
        // fills the same changed region a volume window would (retraction
        // 2026-08-05; the 2026-07-29 "mounts and opens its window" claim
        // was this dialog). Tell the two apart the way a user would:
        // the dialog is a solid white box dead centre; a mounted volume
        // paints its icon on the desktop strip instead.
        bool responded = n > 200 && drv.hasDisk() && nibRead > 1000;
        double centreWhite = 1.0 - blackRatio(afterIns, 120, 380, 90, 200);
        long stripDelta = 0;
        for (int y = 50; y < 115; y++)
            for (int x = 445; x < 510; x++)
                if (beforeIns[size_t(y) * 512 + x] != afterIns[size_t(y) * 512 + x])
                    stripDelta++;
        // The strip is the primary judge (an icon = a mount, whatever the
        // centre shows); the centre-white cue names the dialog. 0.80, not
        // higher: the dialog's inverted "Name:" edit field pulls its own
        // box down to ~0.85 white (measured).
        const char* verdict =
            stripDelta >= 50  ? "volume icon appeared (MOUNTED)"
            : centreWhite > 0.80 ? "INIT DIALOG (volume NOT mounted — the "
                                   "open SWIM1-IWM mount bug, TODO §1)"
                                 : "no window and no dialog";
        std::printf("floppy: System responded: %s — centre white %.2f, icon "
                    "strip Δ%ld px → %s\n", responded ? "yes" : "NO",
                    centreWhite, stripDelta, verdict);
        // Mounting opens the volume's window, so it is frontmost and Cmd-N
        // creates the folder ON THE FLOPPY — the same gesture `persist`
        // uses on the hard disk, no desktop-icon hunting needed.
        long before[folderprobe::kCount];
        folderprobe::sample(floppyOrig, before, "floppy/before");
        std::vector<uint8_t> hdSnap = mem.scsiDisk().image();
        std::printf("floppy: write-protect sense = %d\n",
                    drv.isWriteProtected());
        // Diagnosis knobs for the "Cmd-N reaches neither volume" symptom
        // (TODO §2 "guest-INITIATED write", unchanged since 2026-07-29):
        // POM68K_FLOPPY_SETTLE adds frames between the mount and the
        // gesture (Finder-still-busy hypothesis), and the gesture itself
        // samples the 8-byte KeyMap ($0174) every frame — KeyMap silent
        // means the keystroke never reached the guest; KeyMap set with no
        // catalog write means the Finder saw it and did nothing.
        if (const char* s = getenv("POM68K_FLOPPY_SETTLE")) {
            int extra = atoi(s);
            std::printf("floppy: +%d settle frames before Cmd-N\n", extra);
            runFrames(extra);
        }
        bool keymapSaw = false;
        auto runWatched = [&](long n) {
            for (long f = 0; f < n && !gCpu->isHalted(); f++) {
                gCpu->runCycles(kFrame);
                for (int i = 0; i < 8 && !keymapSaw; i++)
                    if (gMem->peek8(0x0174 + uint32_t(i)) != 0) keymapSaw = true;
            }
        };
        std::vector<uint32_t> preGesture;
        screen(preGesture);
        mem.keyEvent(0x37, true);            // Cmd down
        runWatched(6);
        mem.keyEvent(0x2D, true);            // 'n'
        runWatched(4);
        mem.keyEvent(0x2D, false);
        runWatched(4);
        mem.keyEvent(0x37, false);
        runWatched(120);                     // rename field appears
        mem.keyEvent(0x24, true);            // Return — commit the name
        runWatched(4);
        mem.keyEvent(0x24, false);
        runWatched(4);
        runFrames(900);                      // ~15 s: create + flush catalog
        std::printf("floppy: KeyMap %s the gesture\n",
                    keymapSaw ? "saw" : "NEVER saw");
        // The observation closest to the user: what did the SCREEN do in
        // response to Cmd-N? A new folder icon is a small change inside
        // the window; an alert ("volume is locked"?) is a large centered
        // box; zero change means the Finder swallowed the keystroke.
        {
            std::vector<uint32_t> postGesture;
            screen(postGesture);
            long cgx = 0, cgy = 0, ng = 0;
            int gx0 = 512, gx1 = -1, gy0 = 384, gy1 = -1;
            for (int y = 0; y < 384; y++)
                for (int x = 0; x < 512; x++) {
                    size_t i = size_t(y) * 512 + x;
                    if (preGesture[i] != postGesture[i]) {
                        cgx += x; cgy += y; ng++;
                        gx0 = std::min(gx0, x); gx1 = std::max(gx1, x);
                        gy0 = std::min(gy0, y); gy1 = std::max(gy1, y);
                    }
                }
            std::printf("floppy: Cmd-N changed the screen by %ld px "
                        "(region x %d..%d, y %d..%d)\n", ng, gx0, gx1, gy0, gy1);
            dump("lcii_beyond_floppy_cmdn.ppm", postGesture);
        }
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
        long got[folderprobe::kCount];
        folderprobe::sample(after, got, "floppy/after");
        const size_t grewF = folderprobe::grew(before, got);
        std::printf("floppy: %s in the host file\n",
                    grewF < folderprobe::kCount
                        ? (std::string("'") + folderprobe::kNames[grewF] + "' " +
                           std::to_string(before[grewF]) + " -> " +
                           std::to_string(got[grewF])).c_str()
                        : "NO candidate folder name appeared");
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
        // Asserted since the boost-gate fix: the volume MOUNTS (desktop
        // icon strip — the judge a user would use, never a changed-region
        // diff), the guest WRITES to the medium (drAtrb, committed
        // sectors), the write reaches the host file, and the medium
        // survives the round trip. This is the TODO §1 mount gate: it was
        // run against the pre-fix defect (POM68K_FLOPPY_BOOST_GATE=0
        // reproduces it) and fails there — init dialog, no icon, no write.
        // `got` (the Cmd-N folder ON the floppy) stays printed-not-asserted:
        // the catalog write is a separate open question (TODO §2).
        ok = !cpu.isHalted() && responded && stripDelta >= 50 && guestWrote
             && changed && sizeOk && stillHfs && reinsert;
        std::remove(floppyCopy.c_str());
    } else {
        std::fprintf(stderr, "FAIL: unknown POM68K_BEYOND=%s\n", mode.c_str());
        return 1;
    }

    std::printf("%s — LC II beyond-boot %s\n", ok ? "PASSED" : "FAILED",
                mode.c_str());
    return ok ? 0 : 1;
}
