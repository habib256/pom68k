// POM68K — Mac II POST gate: ROM through Slot Manager + live Toby framebuffer.
// Soft-skips without the 256 KB Mac II ROM.

#include "MacIIMemory.h"
#include "TobyVideo.h"
#include "Cpu020.h"
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

static std::string findRom() {
    for (const std::string base : { "", "../" }) {
        for (const char* rel : {
            "roms/256KB ROMs/1987-12 - 9779D2C4 - MacII (800k v2).ROM",
            "roms/256KB ROMs/1987-03 - 97851DB6 - MacII (800k v1).ROM",
        }) {
            std::string p = base + rel;
            if (std::ifstream(p, std::ios::binary)) return p;
        }
    }
    return {};
}

int main() {
    std::string romPath = findRom();
    if (romPath.empty()) {
        std::printf("SKIP: needs Mac II 256 KB ROM in roms/256KB ROMs/\n");
        return 0;
    }

    std::ifstream in(romPath, std::ios::binary);
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)), {});
    if (rom.size() != MacIIMemory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM size %zu\n", rom.size());
        return 1;
    }

    MacIIMemory mem;
    if (!mem.loadRom(rom)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    mem.installTobyVideo();
    Cpu020 cpu(mem, true);
    mem.setCpu(&cpu);
    cpu.hardReset();

    const int64_t kFrame = 800 * 525;
    const long kFrames = 50000;
    for (long f = 0; f < kFrames && !cpu.isHalted(); f++)
        cpu.runCycles(kFrame);

    if (cpu.isHalted()) {
        std::fprintf(stderr, "FAIL: CPU halted\n");
        return 1;
    }

    TobyVideo* tv = mem.toby();
    if (!tv) { std::fprintf(stderr, "FAIL: no Toby\n"); return 1; }

    std::vector<uint32_t> fb;
    tv->decode(fb);
    long nonWhite = 0;
    for (uint32_t px : fb)
        if ((px & 0xFF) < 0xF0) nonWhite++;

    double ratio = double(nonWhite) / double(fb.size());
    std::printf("non-white pixels %.4f (want >0.001), PC=$%08X\n",
                ratio, cpu.getPC());

    bool ok = ratio > 0.001;
    std::printf("%s\n", ok ? "PASSED — Slot Manager + video alive" : "FAILED");
    return ok ? 0 : 1;
}
