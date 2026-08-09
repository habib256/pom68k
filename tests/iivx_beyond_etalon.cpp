// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Beyond-boot gates on the Macintosh IIvx (VASP + Egret 341S0851 + 68030 @
// 32 MHz, 640×480×8) — the THIRD machine to get them, after the LC II
// (`lcii_beyond_etalon.cpp`) and the Quadra 605 (`q605_beyond_etalon.cpp`).
// The boot etalons prove the Finder appears; these prove the machine is
// USABLE. `POM68K_BEYOND` picks the scenario, one CTest entry each:
//
//   soak    — after the Finder, idle ~3 emulated minutes: the low-memory
//             Time global ($20C) must advance in step, the CPU must not
//             halt, and the Finder signature must still hold at the end.
//             This is the scenario that catches the MCU-overclock class of
//             bug (`CHANGELOG` 2026-07-26, the Egret/Cuda clock drift): a
//             boot gate cannot see a clock running 37 % fast, and a soak
//             gate sees nothing else.
//   persist — Cmd-N in the Finder creates a folder: the SCSI image bytes
//             must change, the new catalog name must appear, and after a
//             hard reset the machine must boot back to the Finder off the
//             modified volume. The only IIvx gate that drives the SCSI
//             WRITE path end to end from a real guest.
//
// Why the IIvx and not the IIsi: both are in the "freshness" tail
// (`TODO.md` § 2 — booted once, not hardened), but the RBV machines put
// PHYSICAL low RAM inside the framebuffer while the PMMU moves logical low
// memory, so `peek8(0x20C)` there reads desktop PIXELS, not the Time global
// (`CHANGELOG` 2026-07-29 — the mistake was made three times). VASP has its
// own VRAM, so the low-memory observable is real. A soak gate on the RBV
// needs a logical-address read first, and is worth its own entry.
//
// POM68K_DUMP=1 writes iivx_beyond_<mode>.ppm for eyeballing.
// Soft-skips without the 4957EB49 ROM + a bootable hdv/ image.

#include "AssetFingerprint.h"
#include "FolderProbe.h"
#include "VaspMemory.h"
#include "VaspVideo.h"
#include "VaspCpu.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

// The boot etalon's own fixup: some images carry no driver descriptor of
// type $6A, which the ROM's boot scan requires.
void ensureBootDriverType(std::vector<uint8_t>& img) {
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

VaspMemory* gMem;
VaspCpu* gCpu;
int64_t gFrame = 0;
int gW = 640, gH = 480;

void runFrames(long n) {
    for (long f = 0; f < n && !gCpu->isHalted(); f++)
        gCpu->runCycles(gFrame);
}

// Low-memory Time global ($20C, seconds since 1904) — proves the one-second
// interrupt chain (VIA → Egret RTC → Time Manager) stays alive. Physical on
// VASP, which is what makes this readable here and not on the RBV.
uint32_t macTime() {
    return uint32_t(gMem->peek8(0x20C)) << 24 | uint32_t(gMem->peek8(0x20D)) << 16
         | uint32_t(gMem->peek8(0x20E)) << 8 | gMem->peek8(0x20F);
}

void keyTap(uint8_t code) {                  // ADB code, press + release
    gMem->keyEvent(code, true);
    runFrames(4);
    gMem->keyEvent(code, false);
    runFrames(4);
}

void screen(std::vector<uint32_t>& fb) {
    VaspVideo video(*gMem);
    video.decode(fb);
    video.size(gW, gH);
}

// Luminance-weighted, like every 8-bpp colour gate in the tree: the desktop
// weave is a 50 % dither of two colours, and a flat RGB average calls it
// light or dark depending on the palette (rationale in lc520_boot_etalon).
double darkRatio(const std::vector<uint32_t>& fb, int x0, int x1, int y0, int y1) {
    if (fb.size() < size_t(gW) * gH) return -1.0;
    long dark = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            uint32_t p = fb[size_t(y) * gW + x];
            int luma = (2 * int((p >> 16) & 0xFF) + 5 * int((p >> 8) & 0xFF)
                      + int(p & 0xFF)) / 8;
            if (luma < 0x80) dark++;
        }
    return double(dark) / (double(x1 - x0) * (y1 - y0));
}

void dump(const char* name, const std::vector<uint32_t>& fb) {
    if (!getenv("POM68K_DUMP")) return;
    FILE* fp = fopen(name, "wb");
    if (!fp) return;
    std::fprintf(fp, "P6\n%d %d\n255\n", gW, gH);
    for (int y = 0; y < gH; y++)
        for (int x = 0; x < gW; x++) {
            uint32_t p = fb[size_t(y) * gW + x];
            uint8_t rgb[3] = { uint8_t(p >> 16), uint8_t(p >> 8), uint8_t(p) };
            fwrite(rgb, 1, 3, fp);
        }
    fclose(fp);
}


// Same signature as iivx_boot_etalon: light menu bar, dithered desktop in
// the right-hand strip the icons do not cover.
bool finderUp() {
    std::vector<uint32_t> fb;
    screen(fb);
    double menu = darkRatio(fb, 0, gW, 2, 16);
    double desk = darkRatio(fb, gW - 112, gW, 40, gH - 44);
    return menu >= 0.0 && menu < 0.30 && desk > 0.35 && desk < 0.85;
}

}  // namespace

int main() {
    const std::string mode = getenv("POM68K_BEYOND") ? getenv("POM68K_BEYOND")
                                                     : "soak";
    std::string rom = testasset::find("roms/maciivx.rom");
    if (rom.empty())
        rom = testasset::find("roms/1MB ROMs/1992-10 - 4957EB49 - Mac IIvx & "
                              "IIvi or Performa 600.ROM");
    std::string img = testasset::find("hdv/lc3-boot.vhd");
    if (img.empty()) img = testasset::find("hdv/GISTPERSO-boot.vhd");
    if (img.empty()) img = testasset::find("hdv/boot.vhd");
    if (img.empty()) img = testasset::find("hdv/System 7.5 HD.dsk");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs the 1 MB 4957EB49 ROM + a bootable hdv/ image\n");
        return 0;
    }
    testasset::report({ rom, img });

    std::ifstream in(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
    if (romData.size() != VaspMemory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM is %zu bytes, want 1 MB\n", romData.size());
        return 1;
    }

    const int64_t cpuHz = VaspMemory::kCpuHzVx;
    VaspMemory mem(0x800000, cpuHz, VaspMemory::kIdIIvx);
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    mem.setMonitorSense(6);                  // 13" 640×480 RGB
    VaspCpu cpu(mem, /*withFpu=*/true);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk image\n"); return 1; }
    ensureBootDriverType(mem.scsiDisk().image());
    gMem = &mem; gCpu = &cpu; gFrame = cpuHz / 60;

    std::printf("ADB: %s\n", mem.egretLleActive() ? "Egret firmware LLE" : "HLE");

    while (mem.cpuHeld()) mem.tick(1000);
    runFrames(16000);                        // boot to a settled Finder
    if (cpu.isHalted()) { std::fprintf(stderr, "FAIL: halted during boot\n"); return 1; }
    if (!finderUp()) { std::fprintf(stderr, "FAIL: no Finder after boot\n"); return 1; }
    std::printf("Finder up %dx%d depth %d (SCSI %ld)\n", gW, gH,
                mem.videoDepth(), mem.scsi().commands);

    bool ok = false;

    if (mode == "soak") {
        // ~3 emulated minutes idle. The Mac clock must track wall-emulated
        // time (±25 %: the 60.15 Hz tick against whole frames), the CPU must
        // not halt, and the desktop must still be there at the end — a soak
        // that ends on a dead screen is not a pass.
        const long kSoak = 10800;            // 180 s of 60 Hz frames
        const uint32_t t0 = macTime();
        const int64_t mcu0 = mem.egretLleActive()
                           ? mem.egretLle().mcu().cycleCount() : 0;
        runFrames(kSoak);
        const uint32_t t1 = macTime();
        const long dt = long(t1 - t0);
        if (mem.egretLleActive())
            std::fprintf(stderr, "[soak] mcu cycles %lld over 180 s\n",
                         (long long)(mem.egretLle().mcu().cycleCount() - mcu0));
        std::vector<uint32_t> fb;
        screen(fb);
        dump("iivx_beyond_soak.ppm", fb);
        const bool alive = finderUp();
        std::printf("soak: %ld s elapsed on the Mac clock (want 135-225), "
                    "halted=%d, Finder %s\n",
                    dt, cpu.isHalted(), alive ? "still up" : "GONE");
        ok = !cpu.isHalted() && dt >= 135 && dt <= 225 && alive;
    } else if (mode == "persist") {
        std::vector<uint8_t>& disk = mem.scsiDisk().image();
        // Full folder-name phrases only — the HFS catalog stores the whole
        // Pascal name, so this is a clean 0→1 signal. The bare "untitled"
        // substring occurs hundreds of times in a stock volume and would
        // drown it out. Accept whichever localization the volume's Finder
        // writes.
        long before[folderprobe::kCount];
        folderprobe::sample(disk, before, "before");
        const std::vector<uint8_t> snap = disk;
        // Cmd-N in the frontmost Finder window, Return to commit the still-
        // editable name, then let the catalog flush.
        mem.keyEvent(0x37, true);            // Cmd down
        runFrames(6);
        keyTap(0x2D);                        // 'n'
        mem.keyEvent(0x37, false);
        runFrames(120);                      // let the rename field appear
        keyTap(0x24);                        // Return — commit the name
        runFrames(900);                      // ~15 s: create + flush catalog
        long after[folderprobe::kCount];
        folderprobe::sample(disk, after, "after");
        const bool wrote = disk != snap;
        // Which name did the Finder actually write? The one that moved.
        const size_t grew = folderprobe::grew(before, after);
        std::printf("persist: %s, image %s\n",
                    grew < folderprobe::kCount
                        ? (std::string("'") + folderprobe::kNames[grew] + "' " +
                           std::to_string(before[grew]) + " -> " +
                           std::to_string(after[grew])).c_str()
                        : "NO candidate folder name appeared",
                    wrote ? "modified" : "UNCHANGED");
        std::vector<uint32_t> fb;
        screen(fb);
        dump("iivx_beyond_persist.ppm", fb);
        // Reboot on the modified volume: it must still reach the Finder, and
        // the folder must still be in the catalog afterwards.
        cpu.hardReset();
        while (mem.cpuHeld()) mem.tick(1000);
        runFrames(16000);
        long survived[folderprobe::kCount];
        folderprobe::sample(disk, survived, "reboot");
        const bool rebooted = !cpu.isHalted() && finderUp();
        // The whole point: the folder must still be there after a hard reset
        // off the SAME volume — i.e. the catalog write reached the medium,
        // not just the System's disk cache.
        const bool kept = grew < folderprobe::kCount && survived[grew] > before[grew];
        std::printf("persist: reboot %s, folder %s\n",
                    rebooted ? "reached the Finder" : "FAILED",
                    kept ? "survived" : "did NOT survive");
        ok = wrote && grew < folderprobe::kCount && rebooted && kept;
    } else {
        std::fprintf(stderr, "FAIL: unknown POM68K_BEYOND=%s\n", mode.c_str());
        return 1;
    }

    std::printf("%s — Macintosh IIvx %s\n", ok ? "PASSED" : "FAILED",
                mode.c_str());
    return ok ? 0 : 1;
}
