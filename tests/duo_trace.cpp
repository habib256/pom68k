// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// duo_trace — PowerBook Duo 210/230 ROM boot trace (milestone 1 of
// docs/DUO_BRINGUP.md, the q605_trace pattern): runs the real ECFA989B
// ROM on the MSC skeleton and reports PC coverage, the exception-vector
// histogram, the first I/O accesses and a VRAM snapshot. Not a CTest
// gate. Expected first stall: the ROM's PG&E (power manager) handshake —
// there is no PG&E yet.
//
// Usage: duo_trace <rom> [--cycles N] [--io N] [--pcring N]
//                  [--stop-at HEXPC [--stop-skip N]] [--disk PATH]
//                  [--duo210]

#include "MscCpu.h"
#include "MscMemory.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <vector>

namespace {

class TraceCpu : public MscCpu {
public:
    using MscCpu::MscCpu;
    std::map<int, long> vecHist;
    long irqCount[8] = {};
    void willInterrupt(moira::u8 level) override { irqCount[level & 7]++; }
    void willExecute(moira::M68kException, moira::u16 vector) override {
        vecHist[vector]++;
        if (vector == 2 || vector == 3 || vector == 4 || vector == 11) {
            static long n = 0;
            if (n++ < 20)
                std::printf("  EXC vec=%u pc=$%08X sr=$%04X D0=$%08X A0=$%08X "
                            "A1=$%08X SP=$%08X clk=%lld\n",
                            vector, getPC0(), getSR(), getD(0), getA(0),
                            getA(1), getSP(), (long long)getClock());
        }
    }
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
            "usage: %s <rom> [--cycles N] [--io N] [--pcring N] "
            "[--stop-at HEXPC [--stop-skip N]] [--disk PATH] [--duo210]\n",
            argv[0]);
        return 2;
    }
    long long cycles = 300000000;   // ~9 machine-seconds at 33 MHz
    int ioMax = 80;
    size_t pcRing = 48;
    uint32_t stopAt = 0;
    long stopSkip = 0;
    bool duo210 = false;
    std::string diskPath;
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--cycles" && i + 1 < argc) cycles = atoll(argv[++i]);
        else if (a == "--io" && i + 1 < argc) ioMax = atoi(argv[++i]);
        else if (a == "--pcring" && i + 1 < argc) pcRing = size_t(atoll(argv[++i]));
        else if (a == "--stop-at" && i + 1 < argc) stopAt = uint32_t(strtoul(argv[++i], nullptr, 16));
        else if (a == "--stop-skip" && i + 1 < argc) stopSkip = atol(argv[++i]);
        else if (a == "--disk" && i + 1 < argc) diskPath = argv[++i];
        else if (a == "--duo210") duo210 = true;
    }

    std::ifstream in(argv[1], std::ios::binary);
    if (!in) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());

    MscMemory mem(8u << 20,
                  duo210 ? MscMemory::kCpuHz210 : MscMemory::kCpuHz230,
                  duo210 ? MscMemory::kIdDuo210 : MscMemory::kIdDuo230);
    if (!mem.loadRom(rom)) {
        std::fprintf(stderr, "ROM must be 1 MB (got %zu)\n", rom.size());
        return 1;
    }
    TraceCpu cpu(mem);
    mem.setCpu(&cpu);
    if (!diskPath.empty())
        std::printf("SCSI disk: %s %s\n", diskPath.c_str(),
                    mem.attachScsi(diskPath) ? "attached" : "FAILED");

    int ioSeen = 0;
    mem.onIoAccess = [&](uint32_t a, bool w, uint32_t v) {
        if (ioSeen++ < ioMax)
            std::printf("  IO  %c $%08X = $%02X  (pc=$%08X clk=%lld)\n",
                        w ? 'W' : 'R', a, v, cpu.getPC0(),
                        (long long)cpu.getClock());
    };

    // --dumpat <hex>: static disassembly straight after reset (the ROM is
    // mapped through the overlay), then exit — reading a ROM routine
    // without having to hit a runtime breakpoint.
    if (const char* d = std::getenv("DUO_DUMPAT")) {
        uint32_t dpc = uint32_t(strtoul(d, nullptr, 16));
        cpu.hardReset();
        char db[128];
        std::printf("[duo_trace] disasm at $%08X:\n", dpc);
        for (int i = 0; i < 80; i++) {
            int len = 2;
            try { len = cpu.disassemble(db, dpc); }
            catch (...) { std::snprintf(db, sizeof db, "<fault>"); }
            std::printf("   $%08X  %s\n", dpc, db);
            dpc += len;
        }
        return 0;
    }

    cpu.hardReset();
    // The PMU boots first: run the machine until the PG&E releases the
    // 68030 (port E bit 2 — msc.cpp:151). Cap the wait so a wedged stub
    // is visible instead of a hang.
    long heldTicks = 0;
    while (mem.cpuHeld() && heldTicks < 400000) { mem.tick(1000); heldTicks++; }
    std::printf("[duo_trace] reset: overlay=%d pc=$%08X sp=$%08X pge=%d "
                "heldTicks=%ld\n",
                mem.overlay(), cpu.getPC0(), cpu.getSP(),
                mem.pgeActive(), heldTicks);
    if (mem.pgeActive()) {
        M68hc05Pge& p = mem.pmu().mcu();
        std::printf("[duo_trace] pge: pc=$%04X instr=%ld illegal=%d "
                    "(op $%02X @ $%04X) portE=$%02X portG=$%02X held=%d\n",
                    p.pc(), p.instructions, p.illegal(), p.illegalOp(),
                    p.illegalPc(), p.portLatch(M68hc05Pge::E),
                    p.portLatch(M68hc05Pge::G), mem.cpuHeld());
    }

    std::vector<uint32_t> ring(pcRing, 0);
    size_t rp = 0;
    std::map<uint32_t, long> pcCov;              // 64 KB granularity
    long long slice = 5000, done = 0;
    bool stop = false;
    // DUO_CKPT=<cycles>: progress line — PC, SCSI activity, PMU traffic and
    // the low-memory Ticks, so a boot that is advancing is distinguishable
    // from one that is spinning.
    long long ckpt = 0, nextCkpt = 0;
    if (const char* e = std::getenv("DUO_CKPT")) { ckpt = atoll(e); nextCkpt = ckpt; }
    uint32_t pmlog = 0;
    if (const char* e = std::getenv("DUO_PMLOG"))
        pmlog = uint32_t(strtoul(e, nullptr, 16));
    // DUO_PCCOUNT="hex,hex,…": how many times each PC is executed. Cheap
    // way to ask "does this branch ever run" without stopping the machine.
    std::vector<uint32_t> countPc;
    std::vector<long> countHits;
    if (const char* e = std::getenv("DUO_PCCOUNT")) {
        const char* p2 = e;
        while (*p2) {
            countPc.push_back(uint32_t(strtoul(p2, nullptr, 16)));
            countHits.push_back(0);
            const char* comma = strchr(p2, ',');
            if (!comma) break;
            p2 = comma + 1;
        }
    }
    auto peek32 = [&](uint32_t a) {
        return uint32_t(mem.peek8(a)) << 24 | uint32_t(mem.peek8(a + 1)) << 16
             | uint32_t(mem.peek8(a + 2)) << 8 | mem.peek8(a + 3);
    };
    while (done < cycles && !stop) {
        if (ckpt && done >= nextCkpt) {
            nextCkpt += ckpt;
            std::printf("  CKPT clk=%lld pc=$%08X Ticks=%u scsi(r=%ld sel=%ld cmd=%ld) "
                        "pmu(spi=%ld int=%ld) gscMode=$%02X\n",
                        done, cpu.getPC0(), peek32(0x016A),
                        mem.scsi().reads, mem.scsi().selects, mem.scsi().commands,
                        mem.pgeActive() ? mem.pmu().mcu().spiTransfers : 0,
                        mem.pgeActive() ? mem.pmu().pmuIntEdges : 0,
                        mem.gscReg(4));
            std::fflush(stdout);
        }
        moira::i64 t = cpu.getClock() + slice;
        while (cpu.getClock() < t && !cpu.isHalted()) {
            uint32_t pc = cpu.getPC0();
            ring[rp++ % ring.size()] = pc;
            pcCov[pc >> 16]++;
            // DUO_PMLOG=<hexPC>: log D0/A0 each time the PMU command
            // dispatcher is entered — the command stream the ROM issues.
            for (size_t ci = 0; ci < countPc.size(); ci++)
                if (pc == countPc[ci]) {
                    countHits[ci]++;
                    if (countHits[ci] <= 8)
                        std::printf("  PCHIT $%08X #%ld clk=%lld sr=$%04X\n",
                                    pc, countHits[ci], (long long)cpu.getClock(),
                                    cpu.getSR());
                }
            if (pmlog && pc == pmlog) {
                static long n = 0;
                if (n++ < 4000) {
                    const uint32_t a0 = cpu.getA(0);
                    std::printf("  PMCMD #%ld clk=%lld cmd=$%04X buf:", n,
                                (long long)cpu.getClock(),
                                uint16_t(mem.peek8(a0) << 8 | mem.peek8(a0 + 1)));
                    for (int i = 0; i < 10; i++) std::printf(" %02X", mem.peek8(a0 + uint32_t(i)));
                    std::printf("\n");
                }
            }
            if (stopAt && pc == stopAt) {
                if (stopSkip > 0) stopSkip--;
                else { stop = true; break; }
            }
            cpu.execute();
        }
        if (cpu.isHalted()) { std::printf("[duo_trace] CPU HALTED (double fault)\n"); break; }
        done += slice;
    }

    std::printf("\n[duo_trace] after %lld cycles: pc=$%08X sp=$%08X sr=$%04X "
                "halted=%d stopped=%d\n",
                done, cpu.getPC0(), cpu.getSP(), cpu.getSR(),
                cpu.isHalted(), cpu.isStopped());

    if (stop) {   // --stop-at: the caller chain is the point of stopping
        std::printf("[duo_trace] STOP at $%08X — ROM return addresses on the stack:\n  ",
                    stopAt);
        const uint32_t sp = cpu.getSP();
        for (int i = 0; i < 160; i += 2) {
            uint32_t w = uint32_t(mem.peek8(sp + uint32_t(i))) << 24
                       | uint32_t(mem.peek8(sp + uint32_t(i) + 1)) << 16
                       | uint32_t(mem.peek8(sp + uint32_t(i) + 2)) << 8
                       | mem.peek8(sp + uint32_t(i) + 3);
            if ((w & 0xFFF00000u) == 0x40800000u)
                std::printf(" [sp+%d]=$%08X", i, w);
        }
        std::printf("\n");
    }

    for (size_t ci = 0; ci < countPc.size(); ci++)
        std::printf("-- PCCOUNT $%08X: %ld hits --\n", countPc[ci], countHits[ci]);

    std::printf("-- registers at stop --\n");
    for (int r = 0; r < 8; r++)
        std::printf("  D%d=$%08X  A%d=$%08X\n", r, cpu.getD(r), r,
                    r == 7 ? cpu.getSP() : cpu.getA(r));
    {   // The PMU wait flag the ROM spins on: ($15D,A3) bit 5 — dump the
        // ADB/PMU globals block around it, and the CURRENT ADBBase, so a
        // waiter holding a stale globals pointer is visible.
        {   // ($130,A3) is the machine-specific ADB transmit vector the
            // queue driver jmp's through at $4080A458.
            const uint32_t base = peek32(0x0CF8);
            std::printf("-- ADB xmit vector ($130,ADBBase)=$%08X  "
                        "flags($15D)=$%02X\n",
                        peek32(base + 0x130), mem.peek8(base + 0x15D));
        }
        std::printf("-- jADBProc($05F0)=$%08X  jADBOp($05FC)=$%08X\n",
                    peek32(0x05F0), peek32(0x05FC));
        std::printf("-- ADBBase($0CF8)=$%08X  A3=$%08X  %s\n",
                    peek32(0x0CF8), cpu.getA(3),
                    peek32(0x0CF8) == cpu.getA(3) ? "(match)"
                                                  : "(A3 IS STALE)");
        const uint32_t a3 = cpu.getA(3);
        std::printf("-- [A3+$150..$17F] (PMU globals):");
        for (int i = 0x150; i < 0x180; i++) {
            if ((i & 0xF) == 0) std::printf("\n   $%08X:", a3 + uint32_t(i));
            std::printf(" %02X", mem.peek8(a3 + uint32_t(i)));
        }
        std::printf("\n");
    }

    std::printf("-- vector histogram --\n");
    for (auto& [v, n] : cpu.vecHist) std::printf("  vec %3d : %ld\n", v, n);
    std::printf("-- IRQ (willInterrupt) by level --\n");
    for (int l = 1; l < 8; l++)
        if (cpu.irqCount[l]) std::printf("  IPL %d : %ld\n", l, cpu.irqCount[l]);

    std::printf("-- PC coverage (64 KB regions, top 16) --\n");
    std::vector<std::pair<long, uint32_t>> cov;
    for (auto& [r, n] : pcCov) cov.push_back({n, r});
    std::sort(cov.rbegin(), cov.rend());
    int shown = 0;
    for (auto& [n, r] : cov) {
        if (shown++ >= 16) break;
        std::printf("  $%08X: %ld\n", r << 16, n);
    }

    {   // GSC VRAM snapshot: nonzero span (did the ROM draw anything?)
        const uint8_t* vr = mem.vram();
        uint32_t lo = 0xFFFFFFFF, hi = 0;
        for (uint32_t i = 0; i < MscMemory::kVramSize; i++)
            if (vr[i]) { if (i < lo) lo = i; hi = i; }
        if (hi) std::printf("-- VRAM nonzero span $%05X-$%05X --\n", lo, hi);
        else std::printf("-- VRAM all zero --\n");

        // Where does QuickDraw think the screen is? ScrnBase ($0824) and the
        // main GDevice's PixMap — if the guest is drawing somewhere other
        // than VRAM offset 0, a blank-looking dump means nothing.
        const uint32_t scrnBase = peek32(0x0824);
        const uint32_t mainDevH = peek32(0x08A4);
        const uint32_t mainDev = mainDevH ? peek32(mainDevH) : 0;
        const uint32_t pmapH = mainDev ? peek32(mainDev + 0x16) : 0;
        const uint32_t pmap = pmapH ? peek32(pmapH) : 0;
        std::printf("-- ScrnBase($824)=$%08X MainDevice=$%08X PixMap=$%08X",
                    scrnBase, mainDev, pmap);
        if (pmap)
            std::printf(" base=$%08X rowBytes=%u depth=%u",
                        peek32(pmap), (peek32(pmap + 4) >> 16) & 0x3FFF,
                        (peek32(pmap + 0x1C) >> 16) & 0xFFFF);
        std::printf("\n");

        // Screen render through the GSC mode (gsc.cpp screen_update_gsc):
        // reg 4 bits 0-1 = 0 → 1bpp, 1 → 2bpp, 2 → 4bpp; 640×400 DBLite.
        // P5 grayscale, MAME palette (pen p of 16 → (15-p)*16|15).
        const int mode = mem.gscReg(4) & 3;
        FILE* pf = std::fopen("duo_screen.pgm", "wb");
        if (pf) {
            std::fprintf(pf, "P5\n640 400\n255\n");
            for (int y = 0; y < 400; y++)
                for (int x = 0; x < 640; x++) {
                    uint8_t pen = 0;
                    if (mode == 0) {
                        uint8_t b = vr[y * 80 + x / 8];
                        pen = uint8_t(((b >> (7 - (x & 7))) & 1) ? 15 : 0);
                    } else if (mode == 1) {
                        uint8_t b = vr[y * 160 + x / 4];
                        pen = uint8_t(((b >> (6 - 2 * (x & 3))) & 3) << 2);
                    } else {
                        uint8_t b = vr[y * 320 + x / 2];
                        pen = (x & 1) ? uint8_t(b & 0xF) : uint8_t(b >> 4);
                    }
                    std::fputc(((15 - pen) << 4) | 0xF, pf);
                }
            std::fclose(pf);
            std::printf("-- wrote duo_screen.pgm (mode %d, GSC reg4=$%02X) --\n",
                        mode, mem.gscReg(4));
        }
    }

    std::printf("-- last %zu PCs --\n", ring.size());
    char da[128];
    for (size_t i = 0; i < ring.size(); i++) {
        uint32_t pc = ring[(rp + i) % ring.size()];
        if (!pc) continue;
        try { cpu.disassemble(da, pc); }
        catch (...) { std::snprintf(da, sizeof da, "<dasm fault>"); }
        std::printf("  $%08X  %s\n", pc, da);
    }
    std::printf("-- SCSI: reads=%ld writes=%ld selects=%ld commands=%ld --\n",
                mem.scsi().reads, mem.scsi().writes, mem.scsi().selects,
                mem.scsi().commands);
    if (mem.pgeActive()) {
        M68hc05Pge& p = mem.pmu().mcu();
        std::printf("-- VIA1: IFR=$%02X IER=$%02X ACR=$%02X SR=$%02X | "
                    "pvia2 IFR=$%02X IER=$%02X slotIFR=$%02X slotIER=$%02X --\n",
                    mem.via1().read(13) /* IFR peek via read is destructive-free */,
                    mem.via1().read(14), mem.via1().read(11), 0,
                    mem.pseudoVia().reg(3), mem.pseudoVia().reg(0x13),
                    mem.pseudoVia().reg(2), mem.pseudoVia().reg(0x12));
        std::printf("-- PGE: intEdges=%ld ackEdges=%ld ack=%d --\n",
                    mem.pmu().pmuIntEdges, mem.pmu().pmuAckEdges,
                    mem.pmu().pmuAck());
        std::printf("-- PGE: pc=$%04X (%s) instr=%ld spi=%ld option=$%02X "
                    "illegal=%d portE=$%02X portG=$%02X portH=$%02X --\n",
                    p.pc(), p.pc() >= 0x8000 && !(p.option() & 0x80 && p.pc() >= 0xFE00)
                                ? "SRAM firmware" : "boot stub",
                    p.instructions, p.spiTransfers, p.option(), p.illegal(),
                    p.portLatch(M68hc05Pge::E), p.portLatch(M68hc05Pge::G),
                    p.portLatch(M68hc05Pge::H));
        std::printf("-- PGE SRAM head:");
        for (int i = 0; i < 16; i++) std::printf(" %02X", p.sramByte(i));
        std::printf("\n-- PGE SRAM $7E00:");
        for (int i = 0; i < 16; i++) std::printf(" %02X", p.sramByte(0x7E00 + i));
        std::printf("\n-- PGE SRAM $7FF0:");
        for (int i = 0; i < 16; i++) std::printf(" %02X", p.sramByte(0x7FF0 + i));
        std::printf("\n-- PGE SRAM $3FF0:");
        for (int i = 0; i < 16; i++) std::printf(" %02X", p.sramByte(0x3FF0 + i));
        std::printf("\n");
        std::printf("-- PGE last PCs (newest first):\n  ");
        for (int i = 0; i < 48; i++) std::printf(" %04X", p.pcHistory(i));
        std::printf("\n-- PGE last SPI exchanges out:in (newest first):\n  ");
        for (int i = 0; i < 48; i++) {
            uint16_t e = p.spiHistory(i);
            std::printf(" %02X:%02X", e >> 8, e & 0xFF);
        }
        std::printf("\n");
        if (p.illegal()) {
            std::printf("-- PGE ILLEGAL op $%02X at $%04X; bytes at pc-8..+8:",
                        p.illegalOp(), p.illegalPc());
            for (int i = -8; i <= 8; i++) {
                uint16_t a = uint16_t(p.illegalPc() + i);
                uint8_t b = a >= 0x8000 ? p.sramByte(a - 0x8000)
                          : (a >= 0x40 && a < 0x400 ? p.ramByte(a - 0x40) : 0xEE);
                std::printf(" %02X", b);
            }
            std::printf("\n");
        }
    }
    return 0;
}
