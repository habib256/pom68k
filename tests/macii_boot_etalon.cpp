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

int main() {
    std::string rom = find("roms/256KB ROMs/1987-12 - 9779D2C4 - MacII (800k v2).ROM");
    if (rom.empty()) rom = find("roms/256KB ROMs/1987-03 - 97851DB6 - MacII (800k v1).ROM");
    std::string img = find("hdv/GISTPERSO-boot.vhd");
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
    ensureBootDriverType(mem.scsiDisk().image());

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

    bool ok = menuBar < 0.35 && desktop > 0.20 && desktop < 0.70
           && mem.scsi().commands > 50;
    std::printf("%s\n", ok ? "PASSED — booted to Finder" : "FAILED");
    return ok ? 0 : 1;
}
