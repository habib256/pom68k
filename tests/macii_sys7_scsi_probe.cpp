// POM68K — Mac II System 7 SCSI stall probe (not a CTest gate).
// Logs CDBs around the ~274 command freeze and dumps 5380/VIA/CPU state
// once commands stop advancing.
#include "MacIIMemory.h"
#include "TobyVideo.h"
#include "Cpu020.h"
#include "Ncr5380.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

static const char* phaseName(int /*unused*/) { return "?"; }

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <rom> <disk>\n", argv[0]);
        return 2;
    }
    std::ifstream rin(argv[1], std::ios::binary);
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(rin)), {});
    MacIIMemory mem;
    if (!mem.loadRom(rom) || !mem.installTobyVideo()) return 1;
    Cpu020 cpu(mem, true);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(argv[2])) return 1;

    long cmdLogStart = 250;   // log CDBs from here to stall
    mem.scsi().onCommand = [&](const std::vector<uint8_t>& cdb) {
        long n = mem.scsi().commands;  // already incremented in execute()
        if (n >= cmdLogStart) {
            std::printf("CMD#%ld op=$%02X", n, cdb.empty() ? 0 : cdb[0]);
            for (size_t i = 1; i < cdb.size() && i < 10; i++)
                std::printf(" %02X", cdb[i]);
            std::printf("  PC=$%08X\n", cpu.getPC());
            std::fflush(stdout);
        }
    };

    const int64_t kFrame = 800 * 525;
    long lastCmds = 0;
    long stallFrames = 0;
    for (long f = 0; f < 30000 && !cpu.isHalted(); f++) {
        cpu.runCycles(kFrame);
        long cmds = mem.scsi().commands;
        if (cmds == lastCmds && cmds > 100) stallFrames++;
        else stallFrames = 0;
        lastCmds = cmds;

        if (f % 5000 == 0)
            std::printf("f=%ld PC=$%08X SCSI=%ld irq=%d drq=%d\n",
                        f, cpu.getPC(), cmds,
                        mem.scsi().irqAsserted() ? 1 : 0,
                        mem.scsi().drqActive() ? 1 : 0);

        if (stallFrames == 300) {
            std::printf("\n=== SCSI STALL 300 frames ===\n");
            std::printf("PC=$%08X SCSI=%ld lastCmd=$%02X clk=%lld\n",
                        cpu.getPC(), cmds, mem.scsi().lastCmd,
                        (long long)cpu.getClock());
            for (int i = 0; i < 8; i++)
                std::printf("D%d=$%08X ", i, (unsigned)cpu.getD(i));
            std::printf("\n");
            for (int i = 0; i < 8; i++)
                std::printf("A%d=$%08X ", i, (unsigned)cpu.getA(i));
            std::printf("\n");
            // Peek 5380 live regs via bus (side-effecting — snapshot via
            // counters + a non-destructive peek of CSR/BSR carefully).
            // Use onAccess counters already collected; read CSR/BSR once.
            uint8_t csr = mem.scsi().read(Ncr5380::R_CSR);
            uint8_t bsr = mem.scsi().read(Ncr5380::R_BSR);
            uint8_t mode = mem.scsi().read(Ncr5380::R_MODE);
            uint8_t icr = mem.scsi().read(Ncr5380::R_ICR);
            uint8_t tcr = mem.scsi().read(Ncr5380::R_TCR);
            std::printf("5380 CSR=$%02X BSR=$%02X MODE=$%02X ICR=$%02X TCR=$%02X "
                        "irq=%d drq=%d dmaBytes=%ld\n",
                        csr, bsr, mode, icr, tcr,
                        mem.scsi().irqAsserted() ? 1 : 0,
                        mem.scsi().drqActive() ? 1 : 0,
                        mem.scsi().dmaBytes);
            std::printf("VIA1 IFR/IER=%02X/%02X VIA2=%02X/%02X\n",
                        mem.via1().ifrRaw(), mem.via1().ierRaw(),
                        mem.via2().ifrRaw(), mem.via2().ierRaw());
            auto p8 = [&](uint32_t a) { return mem.peek8(a); };
            auto p16 = [&](uint32_t a) {
                return uint16_t(p8(a) << 8 | p8(a + 1));
            };
            auto p32 = [&](uint32_t a) {
                return uint32_t(p8(a) << 24 | p8(a + 1) << 16 |
                               p8(a + 2) << 8 | p8(a + 3));
            };
            std::printf("BootDrive=%d MemTop=$%08X CurrentA5=$%08X\n",
                        (int16_t)p16(0x210), p32(0x108), p32(0x904));
            std::printf("bytes@PC:");
            uint32_t pc = cpu.getPC();
            for (int i = 0; i < 32; i++) std::printf(" %02X", p8(pc + i));
            std::printf("\n");
            // Low-mem SCSIMgr / driver hints
            std::printf("SCSIVars UTable=$%08X UnitN=$%04X\n",
                        p32(0x0B30), p16(0x0D00));
            (void)phaseName;
            break;
        }
    }
    if (stallFrames < 300)
        std::printf("no stall; final SCSI=%ld PC=$%08X\n",
                    mem.scsi().commands, cpu.getPC());
    return 0;
}
