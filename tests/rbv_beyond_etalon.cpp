// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Beyond-boot gates on the Macintosh IIsi (RBV + Egret + 68030 @ 20 MHz) —
// the FOURTH machine to get them, and the first RAM-based-video one. The
// RBV siblings were skipped when the IIvx got its pair precisely because
// physical low RAM is the framebuffer here while the ROM's PMMU relocates
// the System's logical low memory: `peek8(0x20C)` reads desktop pixels, not
// the Time global. The missing piece was a LOGICAL-address read, and it now
// exists — `tests/Mmu030Peek.h`, a side-effect-free walk of the live page
// tables through `peek8` (which is exactly right for descriptor addresses:
// those are physical). `TODO.md` § 2 named this prerequisite; this gate
// discharges it.
//
//   soak    — after the Finder, idle ~3 emulated minutes: the Time global
//             (logical $20C) must advance in step through the PMMU, the CPU
//             must not halt, and the Finder must still be up at the end.
//             This is the gate that catches the MCU-overclock class — and on
//             an RBV machine it doubles as a standing regression test for
//             the physical-vs-logical trap itself: it FAILS loudly if the
//             PMMU is off when the Finder is up, rather than quietly reading
//             pixels.
//   persist — Cmd-N creates a folder, the image bytes change, the catalog
//             name appears, and a hard reset boots back off the modified
//             volume with the folder still there.
//
// POM68K_DUMP=1 writes rbv_beyond_<mode>.ppm. Soft-skips without the IIsi
// ROM + a bootable hdv/ image.

#include "AssetFingerprint.h"
#include "FolderProbe.h"
#include "Mmu030Peek.h"
#include "RbvCpu.h"
#include "RbvMemory.h"
#include "RbvVideo.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

// The boot etalons' shared fixup: some images carry no $6A driver entry.
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

RbvMemory* gMem;
RbvCpu* gCpu;
int64_t gFrame = 0;
int gW = 640, gH = 480;

void runFrames(long n) {
    for (long f = 0; f < n && !gCpu->isHalted(); f++)
        gCpu->runCycles(gFrame);
}

// One logical byte through the live page tables, side-effect-free. Returns
// -1 when the walk fails — the caller decides what that means.
int peekLogical(uint32_t laddr) {
    uint32_t phys = 0;
    const bool ok = mmu030peek::translate(
        gCpu->getTC(), gCpu->getCRP(), gCpu->getSRP(), laddr, /*fc=*/5,
        [](uint32_t a) { return gMem->peek8(a); }, &phys);
    return ok ? int(gMem->peek8(phys)) : -1;
}

// The low-memory Time global (logical $20C, seconds since 1904). On this
// machine the read is only meaningful once the PMMU is on — TC.E clear with
// the Finder up would mean this gate is back to reading pixels, which is a
// FAILURE of the instrument, not a value.
bool macTime(uint32_t* out) {
    if (!(gCpu->getTC() & 0x80000000)) return false;
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        const int b = peekLogical(0x20C + uint32_t(i));
        if (b < 0) return false;
        v = v << 8 | uint32_t(b);
    }
    *out = v;
    return true;
}

void keyTap(uint8_t code) {
    gMem->keyEvent(code, true);
    runFrames(4);
    gMem->keyEvent(code, false);
    runFrames(4);
}

void screen(std::vector<uint32_t>& fb) {
    RbvVideo video(*gMem);
    video.decode(fb);
    video.size(gW, gH);
}

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

// 8×8-block mid-tone ratio — the iisi_boot_etalon signature's second half,
// for colour desktops that are a solid mid-tone rather than a B&W weave.
double midBlocks(const std::vector<uint32_t>& fb, int x0, int x1, int y0, int y1) {
    long mid = 0, total = 0;
    for (int by = y0; by + 8 <= y1; by += 8)
        for (int bx = x0; bx + 8 <= x1; bx += 8) {
            long sum = 0;
            for (int y = by; y < by + 8; y++)
                for (int x = bx; x < bx + 8; x++) {
                    uint32_t p = fb[size_t(y) * gW + x];
                    sum += (2 * int((p >> 16) & 0xFF) + 5 * int((p >> 8) & 0xFF)
                          + int(p & 0xFF)) / 8;
                }
            int avg = int(sum / 64);
            total++;
            if (avg >= 0x20 && avg <= 0xDF) mid++;
        }
    return total ? double(mid) / total : 0.0;
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

// Same signature as iisi_boot_etalon: light menu bar, and a desktop that is
// either a structured dither or a solid mid-tone colour.
bool finderUp() {
    std::vector<uint32_t> fb;
    screen(fb);
    const double menu = darkRatio(fb, 0, gW, 2, 16);
    const double desk = darkRatio(fb, gW / 4, 3 * gW / 4, gH - 140, gH - 60);
    const double gray = midBlocks(fb, gW / 4, 3 * gW / 4, gH - 140, gH - 60);
    return menu >= 0.0 && menu < 0.30
        && ((desk > 0.15 && desk < 0.95) || gray > 0.60);
}

}  // namespace

int main() {
    const std::string mode = getenv("POM68K_BEYOND") ? getenv("POM68K_BEYOND")
                                                     : "soak";
    std::string rom = testasset::find("roms/maciisi.rom");
    if (rom.empty())
        rom = testasset::find("roms/512KB ROMs/1990-10 - 36B7FB6C - Mac IIsi.ROM");
    std::string img = testasset::find("hdv/iisi-boot.vhd");
    if (img.empty()) img = testasset::find("hdv/lc3-boot.vhd");
    if (img.empty()) img = testasset::find("hdv/GISTPERSO-boot.vhd");
    if (img.empty()) img = testasset::find("hdv/boot.vhd");
    if (img.empty()) img = testasset::find("hdv/System 7.5 HD.dsk");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs the 512 KB IIsi ROM + a bootable hdv/ image\n");
        return 0;
    }
    testasset::report({ rom, img });

    std::ifstream in(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
    if (romData.size() != RbvMemory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM is %zu bytes, want 512 KB\n", romData.size());
        return 1;
    }

    RbvMemory mem(0x800000);
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    mem.setMonitorSense(6);                  // 13" RGB 640×480
    RbvCpu cpu(mem, /*withFpu=*/true);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk image\n"); return 1; }
    ensureBootDriverType(mem.scsiDisk().image());
    gMem = &mem; gCpu = &cpu; gFrame = RbvMemory::kCpuHz / 60;

    std::printf("ADB: %s\n", mem.egretLleActive() ? "Egret firmware LLE" : "HLE");

    while (mem.cpuHeld()) mem.tick(1000);
    runFrames(16000);                        // boot to a settled Finder
    if (cpu.isHalted()) { std::fprintf(stderr, "FAIL: halted during boot\n"); return 1; }
    if (!finderUp()) { std::fprintf(stderr, "FAIL: no Finder after boot\n"); return 1; }
    std::printf("Finder up %dx%d, TC=$%08X (PMMU %s), SCSI %ld\n", gW, gH,
                cpu.getTC(), (cpu.getTC() >> 31) ? "on" : "OFF",
                mem.scsi().commands);

    bool ok = false;

    if (mode == "soak") {
        uint32_t t0 = 0;
        if (!macTime(&t0)) {
            // The whole reason this gate exists on an RBV machine: a Finder
            // with the PMMU off (or an unwalkable table) means the probe
            // would be reading pixels — fail the INSTRUMENT, loudly.
            std::fprintf(stderr, "FAIL: Time global unreadable through the "
                                 "PMMU (TC=$%08X)\n", cpu.getTC());
            return 1;
        }
        const long kSoak = 10800;            // 180 s of 60 Hz frames
        const int64_t mcu0 = mem.egretLleActive()
                           ? mem.egretLle().mcu().cycleCount() : 0;
        runFrames(kSoak);
        uint32_t t1 = 0;
        const bool readable = macTime(&t1);
        const long dt = readable ? long(t1 - t0) : -1;
        if (mem.egretLleActive())
            std::fprintf(stderr, "[soak] mcu cycles %lld over 180 s\n",
                         (long long)(mem.egretLle().mcu().cycleCount() - mcu0));
        std::vector<uint32_t> fb;
        screen(fb);
        dump("rbv_beyond_soak.ppm", fb);
        const bool alive = finderUp();
        std::printf("soak: %ld s elapsed on the Mac clock (want 135-225), "
                    "halted=%d, Finder %s\n",
                    dt, cpu.isHalted(), alive ? "still up" : "GONE");
        ok = readable && !cpu.isHalted() && dt >= 135 && dt <= 225 && alive;
    } else if (mode == "persist") {
        std::vector<uint8_t>& disk = mem.scsiDisk().image();
        long before[folderprobe::kCount];
        folderprobe::sample(disk, before, "before");
        const std::vector<uint8_t> snap = disk;
        mem.keyEvent(0x37, true);            // Cmd down
        runFrames(6);
        keyTap(0x2D);                        // 'n'
        mem.keyEvent(0x37, false);
        runFrames(120);                      // rename field appears
        keyTap(0x24);                        // Return — commit the name
        runFrames(900);                      // ~15 s: create + flush catalog
        long after[folderprobe::kCount];
        folderprobe::sample(disk, after, "after");
        const bool wrote = disk != snap;
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
        dump("rbv_beyond_persist.ppm", fb);
        cpu.hardReset();
        while (mem.cpuHeld()) mem.tick(1000);
        runFrames(16000);
        long survived[folderprobe::kCount];
        folderprobe::sample(disk, survived, "reboot");
        const bool rebooted = !cpu.isHalted() && finderUp();
        const bool kept = grew < folderprobe::kCount && survived[grew] > before[grew];
        std::printf("persist: reboot %s, folder %s\n",
                    rebooted ? "reached the Finder" : "FAILED",
                    kept ? "survived" : "did NOT survive");
        ok = wrote && grew < folderprobe::kCount && rebooted && kept;
    } else {
        std::fprintf(stderr, "FAIL: unknown POM68K_BEYOND=%s\n", mode.c_str());
        return 1;
    }

    std::printf("%s — Macintosh IIsi %s\n", ok ? "PASSED" : "FAILED",
                mode.c_str());
    return ok ? 0 : 1;
}
