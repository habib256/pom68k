// POM68K — LC II System 7.1 → Finder gate (SPConfig AppleTalk-inactive).
// Soft-skips without LC II ROM + System 7.1 HD .dsk.

#include "V8Memory.h"
#include "V8Video.h"
#include "Cpu030.h"

#include <cstdint>
#include <cstdio>
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
        img[dst + 6] = 0x00;
        img[dst + 7] = 0x6A;
        img[0x10] = uint8_t((count + 1) >> 8);
        img[0x11] = uint8_t(count + 1);
    }
}

int main() {
    std::string rom = find("roms/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM");
    if (rom.empty()) rom = find("docs/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM");
    std::string img = find("hdv/System 7.1 HD.dsk");
    if (img.empty()) img = find("hdv/System 7.5.5 HD.dsk");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs LC II ROM + hdv/System 7.1|7.5.5 HD.dsk\n");
        return 0;
    }

    std::ifstream in(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)), {});
    if (romData.size() != V8Memory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM size\n");
        return 1;
    }

    V8Memory mem;
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    Cpu030 cpu(mem, /*withFpu=*/true);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk\n"); return 1; }
    ensureBootDriverType(mem.scsiDisk().image());
    while (mem.cpuHeld()) mem.tick(1000);

    const int64_t kFrame = 640 * 407;
    const long kFrames = 25000;
    for (long f = 0; f < kFrames && !cpu.isHalted(); f++)
        cpu.runCycles(kFrame);

    if (cpu.isHalted()) {
        std::fprintf(stderr, "FAIL: CPU halted PC=$%08X\n", cpu.getPC());
        return 1;
    }

    V8Video video(mem);
    std::vector<uint32_t> fb;
    video.decode(fb);
    const int W = 512;
    auto blackRatio = [&](int x0, int x1, int y0, int y1) {
        long black = 0;
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++)
                if ((fb[size_t(y) * W + x] & 0xFF) < 0x80) black++;
        return double(black) / double(x1 - x0) / double(y1 - y0);
    };
    double menu = blackRatio(0, W, 2, 16);
    double desk = blackRatio(400, W, 40, 340);
    std::printf("menu bar black %.2f, desktop %.2f, SCSI commands %ld\n",
                menu, desk, mem.scsi().commands);

    // Stall at AppleTalk alert: SCSI≈277, menu≈0.50. Finder: SCSI>500.
    bool ok = menu < 0.30 && desk > 0.35 && desk < 0.65 && mem.scsi().commands > 500;
    std::printf("%s\n", ok ? "PASSED — Sys7 Finder" : "FAILED");
    return ok ? 0 : 1;
}
