// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// "System 7.5.5 refuses a hot-inserted GCR floppy on SWIM2" — the § 1 item,
// reported in the GUI and never reproduced headless. Before hunting through
// the GUI path, this checks the assumption the item rests on.
//
// What the item assumes: "the plain tree mounts the disk under 7.5.5 on the
// Quadra", so the difference must live in the GUI. But the headless runs that
// established that were on the gates' own volume — Mac OS 8.1 — while the
// report names 7.5.5, and 7.5.5 is a different System with a different
// .Sony and a different Finder. The combination in the report has never been
// run headless at all.
//
// So: boot the Q605 on the 7.5.5 volume, wait for the Finder, then insert an
// 800K GCR image the way a user does — mid-run, into a machine that is
// already up — and judge on the DESKTOP, never on `nibblesRead` (an IWM-only
// counter that reads 0 on SWIM2 and bought a night of false negatives,
// CHANGELOG 2026-08-04 (soir)). A mounted volume paints an icon; a refusal
// paints a dialog; nothing at all paints nothing.
//
// It began as a question, not a verdict — and the answer promoted it: it is
// the registered gate `q605_hotfloppy_etalon`
// (`cmake/Pom68kMachineGates.cmake:297-301`), built by `all`, which is why
// the missing <cmath> above stopped the whole tree on g++ and not one target.
//
// POM68K_DUMP=1 writes q605_hotfloppy_*.ppm at each step.

#include "AssetFingerprint.h"
#include "Cpu040.h"
#include "JitTestConfig.h"
#include "Q605Memory.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

Q605Memory* gMem;
Cpu040* gCpu;
constexpr int kFrameCycles = 416667;           // 25 MHz / ~60 Hz

void runFrames(long n) {
    for (long f = 0; f < n && !gCpu->isHalted(); f++)
        gCpu->runCycles(kFrameCycles);
}

// Screen decode lifted verbatim from q605_beyond_etalon.cpp: the guest's
// own PixMap through the DAFB CLUT, not a hardcoded mode. A probe that
// invented its own decode would be measuring its own arithmetic.
uint32_t peek32(uint32_t addr) {
    return uint32_t(gMem->peek8(addr)) << 24 |
           uint32_t(gMem->peek8(addr + 1)) << 16 |
           uint32_t(gMem->peek8(addr + 2)) << 8 |
           gMem->peek8(addr + 3);
}

// Low-memory Time global ($20C, seconds since 1904) — proves the one-
// second chain (VIA → Cuda RTC → Time Manager) stays alive.
uint32_t macTime() { return peek32(0x20C); }

struct Screen {
    int width = 0, height = 0, depth = 0;
    uint32_t stride = 0, offset = 0;
    std::vector<uint32_t> pixels;
};

// Same decode as q605_boot_etalon: the main GDevice's PixMap through the
// DAFB CLUT — the guest's own idea of the screen, not a hardcoded mode.
Screen decodeScreen(const Q605Memory& mem) {
    Screen s;
    uint32_t scrnBase = peek32(0x0824);
    uint32_t mainDevH = peek32(0x08A4);
    uint32_t mainDev = mainDevH ? peek32(mainDevH) : 0;
    uint32_t pmapH = mainDev ? peek32(mainDev + 0x16) : 0;
    uint32_t pmap = pmapH ? peek32(pmapH) : 0;
    if (!pmap) return s;

    uint32_t pmBase = peek32(pmap);
    uint32_t boundsA = peek32(pmap + 0x06);
    uint32_t boundsB = peek32(pmap + 0x0A);
    int top = int(boundsA >> 16), left = int(boundsA & 0xFFFF);
    int bottom = int(boundsB >> 16), right = int(boundsB & 0xFFFF);
    s.width = right - left;
    s.height = bottom - top;
    s.depth = mem.dafbDepth();
    s.stride = mem.dafbStride();
    s.offset = (pmBase ? pmBase : scrnBase) & (Q605Memory::kVramSize - 1);
    if (s.width <= 0 || s.width > 1600 || s.height <= 0 || s.height > 1200 ||
        (s.depth != 1 && s.depth != 2 && s.depth != 4 && s.depth != 8) ||
        s.stride < uint32_t((s.width * s.depth + 7) / 8) ||
        uint64_t(s.offset) + uint64_t(s.height) * s.stride > Q605Memory::kVramSize) {
        s = {};
        return s;
    }

    const uint8_t* vram = mem.vram();
    const uint8_t (*clut)[3] = mem.clut();
    s.pixels.resize(size_t(s.width) * s.height);
    for (int y = 0; y < s.height; y++) {
        uint32_t row = s.offset + uint32_t(y) * s.stride;
        for (int x = 0; x < s.width; x++) {
            uint8_t packed = vram[row + uint32_t(x * s.depth / 8)];
            uint8_t pen;
            if (s.depth == 1) pen = (packed >> (7 - (x & 7))) & 1;
            else if (s.depth == 2) pen = (packed >> (6 - 2 * (x & 3))) & 3;
            else if (s.depth == 4) pen = (x & 1) ? packed & 0x0F : packed >> 4;
            else pen = packed;
            const uint8_t* c = clut[pen];
            s.pixels[size_t(y) * s.width + x] =
                uint32_t(c[0]) << 16 | uint32_t(c[1]) << 8 | c[2];
        }
    }
    return s;
}

struct Stats { double mean = 0, deviation = 0; };
Stats luminanceStats(const Screen& s, int x0, int x1, int y0, int y1) {
    if (x1 > s.width) x1 = s.width;
    if (y1 > s.height) y1 = s.height;
    if (x0 >= x1 || y0 >= y1) return {};
    double sum = 0, sum2 = 0;
    long count = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            uint32_t p = s.pixels[size_t(y) * s.width + x];
            double lum = ((p >> 16) * 54 + ((p >> 8) & 0xFF) * 183 +
                          (p & 0xFF) * 19) / 256.0;
            sum += lum; sum2 += lum * lum; count++;
        }
    Stats r;
    if (count) {
        r.mean = sum / count;
        r.deviation = std::sqrt(sum2 / count - r.mean * r.mean);
    }
    return r;
}

// The full q605_boot_etalon Finder signature (menu bar AND desktop): the
// idle-Finder check used at boot and after the persist reboot.
bool finderUp(const Screen& s) {
    if (s.width != 640 || s.height != 480 || s.depth != 8) return false;
    Stats m = luminanceStats(s, 0, s.width, 2, 16);
    Stats d = luminanceStats(s, 520, 630, 40, 430);
    return m.mean > 170 && m.mean < 235 &&
           m.deviation > 40 && m.deviation < 100 &&
           d.mean > 100 && d.mean < 190 &&
           d.deviation > 30 && d.deviation < 90 &&
           m.mean - d.mean > 35;
}

// The menu bar stays visible whatever windows are open — the liveness
// check once the guest has been made to open/create something (a new
// folder's window or icon changes the desktop sample).

double changedRatio(const std::vector<uint32_t>& a,
                    const std::vector<uint32_t>& b) {
    long n = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); i++)
        if (a[i] != b[i]) n++;
    return a.empty() ? 0.0 : double(n) / double(a.size());
}

void dump(const char* name, const Screen& s) {
    if (!getenv("POM68K_DUMP") || s.pixels.empty()) return;
    std::FILE* f = std::fopen(name, "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", s.width, s.height);
    for (uint32_t p : s.pixels) {
        unsigned char rgb[3] = {(unsigned char)(p >> 16), (unsigned char)(p >> 8),
                                (unsigned char)p};
        std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
}

}  // namespace

int main() {
    const std::string rom = testasset::findAny({
        "roms/1MB ROMs/1993-10 - FF7439EE - LC475,575,Quadra 605,Performa 475,"
        "476,575,577,578.ROM",
        "roms/mame/macqd605/ff7439ee.bin"});
    // The volume the REPORT names, not the volume the gates use.
    const std::string disk = testasset::findAny({
        "hdv/System 7.5.5 HD.dsk", "hdv/ref/System 7.5.5 HD.dsk"});
    const std::string floppy = testasset::findAny({
        "disks35/Rogue.dsk", "disks35/Disk605.dsk",
        "disks35/Stuffit_Expander_5.5.dsk"});
    if (rom.empty() || disk.empty() || floppy.empty()) {
        std::printf("SKIP: needs the FF7439EE ROM, a 7.5.5 boot volume and an "
                    "800K GCR image in disks35/\n");
        return 0;
    }
    testasset::report({ rom, disk, floppy });

    std::vector<uint8_t> romData;
    {
        std::FILE* f = std::fopen(rom.c_str(), "rb");
        if (!f) { std::fprintf(stderr, "FAIL: ROM unreadable\n"); return 1; }
        std::fseek(f, 0, SEEK_END);
        romData.resize(size_t(std::ftell(f)));
        std::fseek(f, 0, SEEK_SET);
        if (std::fread(romData.data(), 1, romData.size(), f) != romData.size()) {
            std::fclose(f);
            std::fprintf(stderr, "FAIL: short ROM read\n");
            return 1;
        }
        std::fclose(f);
    }

    Q605Memory mem(pom68k::defaultCoreConfig());
    // ROM **and disk** before the CPU exists, exactly like q605_beyond_etalon:
    // attaching the volume after hardReset() leaves the machine running with
    // no boot device — 0 SCSI commands and a 0x0 screen, which is what this
    // probe printed on its first run.
    if (!mem.loadRom(romData) || !mem.attachScsi(disk)) {
        std::fprintf(stderr, "FAIL: could not load ROM/disk\n");
        return 1;
    }
    const jit::ResolvedConfig jitConfig = testjit::resolveFromEnvironment();
    Cpu040 cpu(mem, jitConfig, pom68k::defaultCoreConfig().cpu,
               pom68k::defaultCoreConfig().diagnostics);
    mem.setCpu(&cpu);
    cpu.hardReset();
    gMem = &mem; gCpu = &cpu;

    // The Cuda holds the CPU at power-on: release it first, or the machine
    // runs for ever without a single SCSI command. (Second time this trap has
    // been paid today — it is now in DEV.md § 2.12's check-list.)
    while (mem.cpuHeld()) mem.tick(1000);
    for (int frame = 0; frame < 20000 && !cpu.isHalted(); frame++) {
        cpu.runCycles(kFrameCycles);
        if (frame >= 3600 && !(frame % 60) && mem.scsi().commands > 4000 &&
            finderUp(decodeScreen(mem))) {
            runFrames(300);
            break;
        }
    }
    // This 7.5.5 volume opens a modal alert at every boot -- "the alias
    // 'Infinite HD' could not be opened" -- and a machine sitting in a modal
    // dialog polls nothing. The first runs of this probe measured exactly that
    // and called it "the guest never saw the insert". Tap Return (the default
    // button) until the alert is gone, the same reflex BeyondBoot.h's
    // focusFinder/frontApp hooks exist for.
    for (int tap = 0; tap < 4; tap++) {
        mem.keyEvent(0x24, true);
        runFrames(20);
        mem.keyEvent(0x24, false);
        runFrames(120);
    }
    runFrames(300);

    Screen before = decodeScreen(mem);
    dump("q605_hotfloppy_desktop.ppm", before);
    std::printf("desktop: %dx%d depth %d, SCSI %ld commands\n",
                before.width, before.height, before.depth, mem.scsi().commands);

    // The gesture: insert while the machine is RUNNING, which is the only way
    // a user can do it and the only thing the report describes.
    const int trackBefore = mem.internalDrive().currentTrack();
    // Mid-run, which is the only way a user can do it and the only thing the
    // report describes. (A "present at power-on" control existed while this
    // was a probe; it answered its question -- the medium was never the
    // problem -- and left with the knob it needed.)
    const bool accepted = mem.insertDisk(floppy);
    std::printf("insert: drive %s the image\n",
                accepted ? "accepted" : "REFUSED");
    runFrames(1800);                           // 30 s for .Sony to poll+mount
    const int trackAfter = mem.internalDrive().currentTrack();

    Screen after = decodeScreen(mem);
    dump("q605_hotfloppy_after.ppm", after);
    const double moved = changedRatio(before.pixels, after.pixels);
    std::printf("after insert: %.2f%% of the desktop changed, halted=%d\n",
                moved * 100.0, gCpu->isHalted());

    // No verdict encoded here beyond "did the desktop react at all". A mounted
    // volume, an initialise dialog and a silent refusal are three different
    // pictures, and the screenshots are how they are told apart.
    // Two-sided evidence, because either half alone lies: the desktop can
    // change for reasons that are not a mount (a dialog), and the head can
    // step without anything appearing. A mounted volume needs both — the
    // guest DROVE the drive, and the picture changed.
    std::printf("head: track %d -> %d\n", trackBefore, trackAfter);
    if (moved < 0.001) {
        std::printf("VERDICT: the desktop did not react. Check the screenshot "
                    "for a modal alert first: this volume opens one at every "
                    "boot, and a machine sitting in a dialog polls nothing\n");
        return 1;
    }
    if (trackAfter == trackBefore) {
        std::printf("VERDICT: the desktop changed but the head never moved — "
                    "that is a dialog, not a mount\n");
        return 1;
    }
    std::printf("VERDICT: mounted — the guest stepped the head and the desktop "
                "gained an icon\n");
    return 0;
}
