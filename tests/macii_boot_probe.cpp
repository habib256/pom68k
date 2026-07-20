#include "MacIIMemory.h"
#include "TobyVideo.h"
#include "Cpu020.h"
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

static std::string find(const char* rel) {
    for (const std::string b : {"", "../"})
        if (std::ifstream(b + rel, std::ios::binary)) return b + rel;
    return {};
}
static uint32_t rd32(MacIIMemory& m, uint32_t a) {
    return (uint32_t(m.read8(a) << 24) | (uint32_t(m.read8(a + 1)) << 16) |
            (uint32_t(m.read8(a + 2)) << 8) | m.read8(a + 3));
}
// Slot int queue header: ROM uses move.l 2(a2) as first-task pointer.
static uint32_t qHead(MacIIMemory& m, int bitIndex) {
    uint32_t base = rd32(m, 0xD04);
    if (!base) return 0;
    uint32_t hdr = rd32(m, base + bitIndex * 4);
    if (!hdr) return 0;
    return rd32(m, hdr + 2);
}

int main() {
    auto rom = find("roms/256KB ROMs/1987-12 - 9779D2C4 - MacII (800k v2).ROM");
    auto img = find("hdv/HD20SC.vhd");
    std::ifstream rin(rom, std::ios::binary);
    std::vector<uint8_t> rd((std::istreambuf_iterator<char>(rin)), {});
    MacIIMemory mem;
    mem.loadRom(rd);
    mem.installTobyVideo();
    Cpu020 cpu(mem, true);
    mem.setCpu(&cpu);
    cpu.hardReset();
    mem.attachScsi(img);
    const int64_t kFrame = 800 * 525;
    uint32_t lastCa1 = 0xFFFFFFFFu, lastSh = 0xFFFFFFFFu;
    uint8_t lastIer = 0xFF, lastAcr = 0xFF;
    for (long f = 0; f < 500 && !cpu.isHalted(); f++) {
        cpu.runCycles(kFrame);
        uint32_t ca1 = qHead(mem, 4); // $D08[4]=bit1=CA1
        uint32_t sh  = qHead(mem, 3); // bit2=SHIFT
        uint8_t ier = mem.via2().ierRaw();
        uint8_t acr = mem.via2().read(Via6522::ACR);
        uint8_t ifr = mem.via2().ifrRaw();
        if (ca1 != lastCa1 || sh != lastSh || ier != lastIer || acr != lastAcr ||
            (f % 50 == 0) || mem.scsi().commands == 1 || mem.scsi().commands == 235) {
            std::printf("f=%ld cmds=%ld PC=$%08X IER=%02X IFR=%02X ACR=%02X "
                        "CA1q=$%08X SHIFTq=$%08X nubus=%02X\n",
                        f, mem.scsi().commands, cpu.getPC(), ier, ifr, acr,
                        ca1, sh, mem.nubusIrqState());
            lastCa1 = ca1;
            lastSh = sh;
            lastIer = ier;
            lastAcr = acr;
        }
        if ((cpu.getPC() & 0xFF000000) == 0x20000000) break;
    }
}
