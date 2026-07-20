// Dev probe: Mac II System 7 stall — ASC / VIA2 / Sound Manager state.
#include "MacIIMemory.h"
#include "TobyVideo.h"
#include "Cpu020.h"
#include <cstdio>
#include <fstream>
#include <vector>
#include <string>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <rom> <disk>\n", argv[0]);
        return 2;
    }
    std::ifstream rin(argv[1], std::ios::binary);
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(rin)), {});
    MacIIMemory mem;
    mem.loadRom(rom);
    mem.installTobyVideo();
    Cpu020 cpu(mem, true);
    mem.setCpu(&cpu);
    cpu.hardReset();
    mem.attachScsi(argv[2]);

    long ascWr = 0, ascRd = 0, fifoWr = 0, modeWr = 0;
    uint8_t lastMode = 0xFF, lastStat = 0xFF;
    mem.asc().onWrite = [&](uint32_t off, uint8_t v) {
        ascWr++;
        if (off < 0x400) fifoWr++;
        if (off == 0x801) {
            modeWr++;
            if (modeWr <= 20)
                std::printf("[%lld] ASC mode=$%02X (was tracking)\n",
                            (long long)cpu.getClock(), v);
            lastMode = v;
        }
        if (off == 0x802 || off == 0x803)
            if (ascWr < 40)
                std::printf("[%lld] ASC write $%03X=$%02X\n",
                            (long long)cpu.getClock(), off, v);
    };
    mem.asc().onRead = [&](uint32_t off, uint8_t v) {
        ascRd++;
        if (off == 0x804 && v != lastStat) {
            if (ascRd < 80)
                std::printf("[%lld] ASC status=$%02X irq=%d cap=%d\n",
                            (long long)cpu.getClock(), v,
                            mem.asc().irqAsserted() ? 1 : 0,
                            mem.asc().fifoCap());
            lastStat = v;
        }
    };

    const int64_t kFrame = 800 * 525;
    uint32_t lastPc = 0;
    long stuck = 0;
    for (long f = 0; f < 20000 && !cpu.isHalted(); f++) {
        cpu.runCycles(kFrame);
        uint32_t pc = cpu.getPC();
        // Only treat as hung once the System is loading from SCSI — ROM
        // RAM-test loops sit on one PC for many frames legitimately.
        if (mem.scsi().commands > 100 && pc == lastPc) stuck++;
        else stuck = 0;
        lastPc = pc;
        if (stuck == 200) {
            std::printf("STUCK 200 frames at PC=$%08X SCSI=%ld\n",
                        pc, mem.scsi().commands);
            break;
        }
        if (f % 5000 == 0)
            std::printf("f=%ld PC=$%08X SCSI=%ld ASC irq=%d cap=%d mode=$%02X fifoWr=%ld\n",
                        f, pc, mem.scsi().commands,
                        mem.asc().irqAsserted() ? 1 : 0, mem.asc().fifoCap(),
                        mem.asc().read(0x801), fifoWr);
    }

    auto p8 = [&](uint32_t a) { return mem.peek8(a); };
    auto p16 = [&](uint32_t a) { return uint16_t(p8(a) << 8 | p8(a + 1)); };
    auto p32 = [&](uint32_t a) {
        return uint32_t(p8(a) << 24 | p8(a + 1) << 16 | p8(a + 2) << 8 | p8(a + 3));
    };
    uint32_t pc = cpu.getPC();
    std::printf("\n=== FINAL ===\n");
    std::printf("PC=$%08X SCSI=%ld clk=%lld\n", pc, mem.scsi().commands,
                (long long)cpu.getClock());
    for (int i = 0; i < 8; i++) std::printf("D%d=$%08X ", i, (unsigned)cpu.getD(i));
    std::printf("\n");
    for (int i = 0; i < 8; i++) std::printf("A%d=$%08X ", i, (unsigned)cpu.getA(i));
    std::printf("\n");
    std::printf("VIA1 IFR/IER=%02X/%02X VIA2=%02X/%02X CB1pin=%d\n",
                mem.via1().ifrRaw(), mem.via1().ierRaw(),
                mem.via2().ifrRaw(), mem.via2().ierRaw(),
                mem.via2().cb1() ? 1 : 0);
    std::printf("ASC irq=%d cap=%d status=$%02X version=$%02X mode=$%02X ctl=$%02X fifoMode=$%02X\n",
                mem.asc().irqAsserted() ? 1 : 0, mem.asc().fifoCap(),
                mem.asc().read(0x804), mem.asc().read(0x800),
                mem.asc().read(0x801), mem.asc().read(0x802),
                mem.asc().read(0x803));
    std::printf("ASC traffic: wr=%ld rd=%ld fifoWr=%ld modeWr=%ld lastMode=$%02X\n",
                ascWr, ascRd, fifoWr, modeWr, lastMode);
    std::printf("bytes@PC:");
    for (int i = 0; i < 32; i++) std::printf(" %02X", p8(pc + i));
    std::printf("\n");
    // Sound Manager globals (classic): SoundPtr $0102?  SoundBase $0266
    std::printf("SoundBase=$%08X SDEnable=$%04X\n", p32(0x266), p16(0x260));
    // Compare A0/A1 distance if CMPA wait
    std::printf("A1-A0=%d op@PC=$%04X op@A0=$%04X\n",
                int(cpu.getA(1) - cpu.getA(0)), p16(pc), p16(cpu.getA(0)));
    return 0;
}
