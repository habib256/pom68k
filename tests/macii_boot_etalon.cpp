// POM68K — Mac II boot gate: SCSI → Finder (640×480 menu/desktop metrics).
// Soft-skips without ROM + bootable hdv/ image.

#include "MacIIMemory.h"
#include "TobyVideo.h"
#include "Cpu020.h"
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

static std::string find(const char* rel) {
    for (const std::string base : { "", "../" }) {
        std::string p = base + rel;
        if (std::ifstream(p, std::ios::binary)) return p;
    }
    return {};
}

// MacIIMemory::loadRom forces StartBoot wantType=1 so a stock type-$0001
// Apple_Driver DDM entry matches (virgin PRAM otherwise seeks $FF).

int main() {
    std::string rom = find("roms/256KB ROMs/1987-12 - 9779D2C4 - MacII (800k v2).ROM");
    if (rom.empty()) rom = find("roms/256KB ROMs/1987-03 - 97851DB6 - MacII (800k v1).ROM");
    // Prefer System 6 (HD20SC) — original Mac II target; System 7.5 next.
    std::string img = find("hdv/HD20SC.vhd");
    if (img.empty()) img = find("hdv/GISTPERSO-boot.vhd");
    if (img.empty()) img = find("hdv/boot.vhd");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs Mac II ROM + bootable hdv/ image\n");
        return 0;
    }

    std::ifstream rin(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(rin)), {});
    if (romData.size() != MacIIMemory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM size\n");
        return 1;
    }

    MacIIMemory mem;
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    mem.installTobyVideo();
    Cpu020 cpu(mem, true);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk\n"); return 1; }

    const int64_t kFrame = 800 * 525;
    const long kFrames = 20000;
    for (long f = 0; f < kFrames && !cpu.isHalted(); f++)
        cpu.runCycles(kFrame);

    if (cpu.isHalted()) {
        std::fprintf(stderr, "FAIL: CPU halted\n");
        return 1;
    }

    TobyVideo* tv = mem.toby();
    std::vector<uint32_t> fb;
    tv->decode(fb);
    const int W = tv->hres();
    const int H = tv->vres();
    auto blackRatio = [&](int x0, int x1, int y0, int y1) {
        long black = 0;
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++)
                if (x < W && y < H && (fb[y * W + x] & 0xFF) < 0x80) black++;
        return double(black) / double(x1 - x0) / double(y1 - y0);
    };
    double menuBar = blackRatio(0, W, 2, 20);
    double desktop = blackRatio(W / 2, W, 40, H - 40);

    std::printf("menu bar black %.2f, desktop %.2f, SCSI commands %ld\n",
                menuBar, desktop, mem.scsi().commands);

    // Jailbars after a stalled Welcome scored ~0.22/0.22 with only ~235 SCSI
    // cmds; a real Finder boot keeps reading the volume (thousands of cmds).
    bool ok = menuBar < 0.35 && desktop > 0.20 && desktop < 0.70
           && mem.scsi().commands > 500;
    std::printf("%s\n", ok ? "PASSED — booted to Finder" : "FAILED");
    return ok ? 0 : 1;
}
