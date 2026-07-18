// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// q605_trace — LC 475 / Quadra 605 ROM boot trace (Q5 dev tool, the
// lcii_trace pattern): runs the real FF7439EE ROM on the Q605 machine
// and reports PC coverage, the exception-vector histogram, bus-error
// sites and the first I/O accesses. Not a CTest gate.
//
// Usage: q605_trace <rom> [--cycles N] [--io N] [--berr N] [--pcring N]
//                   [--stop-at HEXPC [--stop-skip N]] [--watch HEXPC]
// --stop-skip N ignores the first N hits of --stop-at, to catch a
// specific call of a routine that runs many times.

#include "Cpu040.h"
#include "Q605Memory.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <vector>

namespace {

class TraceCpu : public Cpu040 {
public:
    using Cpu040::Cpu040;
    std::map<int, long> vecHist;
    void willExecute(moira::M68kException, moira::u16 vector) override {
        vecHist[vector]++;
        if (vector == 2) {
            std::printf("  VEC2 #%ld pc=$%08X sr=$%04X A0=$%08X A1=$%08X A3=$%08X A7=$%08X clk=%lld\n",
                        vecHist[2], getPC0(), getSR(), getA(0), getA(1), getA(3), getSP(),
                        (long long)getClock());
            justFaulted = true;
            std::printf("       D1=$%08X D2=$%08X D3=$%08X\n", getD(1), getD(2), getD(3));
        }
    }
    bool justFaulted = false;
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s <rom> [--cycles N] [--io N] [--berr N] [--pcring N]\n", argv[0]); return 2; }
    long long cycles = 200000000;   // 8 machine-seconds at 25 MHz
    int ioMax = 60, berrMax = 40; size_t pcRing = 32;
    uint32_t stopAt = 0, watch = 0, wwatch = 0;
    long stopSkip = 0;                 // ignore the first N hits of --stop-at
    std::string diskPath;             // --disk / --scsi: boot image at ID 0
    long long scsiFrom = 0, scsiTo = 0;   // SCSI register-trace clock window
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--cycles" && i + 1 < argc) cycles = atoll(argv[++i]);
        else if (a == "--io" && i + 1 < argc) ioMax = atoi(argv[++i]);
        else if (a == "--berr" && i + 1 < argc) berrMax = atoi(argv[++i]);
        else if (a == "--pcring" && i + 1 < argc) pcRing = size_t(atoll(argv[++i]));
        else if (a == "--stop-at" && i + 1 < argc) stopAt = uint32_t(strtoul(argv[++i], nullptr, 16));
        else if (a == "--stop-skip" && i + 1 < argc) stopSkip = atol(argv[++i]);
        else if (a == "--watch" && i + 1 < argc) watch = uint32_t(strtoul(argv[++i], nullptr, 16));
        else if ((a == "--disk" || a == "--scsi") && i + 1 < argc) diskPath = argv[++i];
        else if (a == "--scsi-log" && i + 2 < argc) { scsiFrom = atoll(argv[++i]); scsiTo = atoll(argv[++i]); }
        else if (a == "--wwatch" && i + 1 < argc) wwatch = uint32_t(strtoul(argv[++i], nullptr, 16));
    }

    std::ifstream in(argv[1], std::ios::binary);
    if (!in) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());

    uint32_t ramMb = 32;
    for (int i = 2; i < argc - 1; i++)
        if (std::string(argv[i]) == "--ram") ramMb = uint32_t(atoi(argv[i+1]));
    Q605Memory mem(ramMb << 20);
    if (!mem.loadRom(rom)) { std::fprintf(stderr, "ROM must be 1 MB (got %zu)\n", rom.size()); return 1; }
    TraceCpu cpu(mem);
    mem.setCpu(&cpu);

    // SCSI boot disk at ID 0 (the ROM's boot scan needs the $6A DDM entry;
    // wrap bare images with tools/wrap_hfs.py). Seed factory XPRAM + a
    // persisted PRAM the same way the LC II tracer does so the cold-boot
    // full-RAM burn-in is skipped on re-runs.
    if (!diskPath.empty())
        std::printf("SCSI disk: %s %s\n", diskPath.c_str(),
                    mem.attachScsi(diskPath) ? "attached" : "FAILED");
    if (mem.cuda().loadPram("q605_trace.pram"))
        std::printf("PRAM: q605_trace.pram loaded\n");
    mem.cuda().factoryDefaults();

    if (wwatch) {
        mem.ramWatch_ = wwatch;
        mem.onRamWrite = [&](uint32_t a, int sz, uint32_t v) {
            static long n = 0;
            if (n++ > 200) return;
            std::printf("  WWATCH $%08X sz%d = $%0*X  pc=$%08X A0=$%08X A1=$%08X A2=$%08X A3=$%08X A4=$%08X clk=%lld\n",
                        a, sz, sz * 2, v, cpu.getPC0(), cpu.getA(0), cpu.getA(1),
                        cpu.getA(2), cpu.getA(3), cpu.getA(4), (long long)cpu.getClock());
        };
    }

    int ioSeen = 0, berrSeen = 0;
    long scsiRegLog = 0, lastDma = 0;
    mem.scsi().onCommand = [&](const std::vector<uint8_t>& cdb) {
        std::printf("  SCSI CDB [");
        for (uint8_t b : cdb) std::printf("%02X ", b);
        std::printf("] (data since prev: %ld) clk=%lld\n",
                    mem.scsi().dmaBytes - lastDma, (long long)cpu.getClock());
        lastDma = mem.scsi().dmaBytes;
    };
    if (scsiTo)
        mem.scsi().onAccess = [&](int reg, bool w, uint8_t v) {
            long long c = cpu.getClock();
            if (c < scsiFrom || c > scsiTo || scsiRegLog++ > 400) return;
            std::printf("  SCSI %c reg%X = $%02X  (pc=$%08X clk=%lld)\n",
                        w ? 'W' : 'R', reg, v, cpu.getPC0(), c);
        };
    mem.onIoAccess = [&](uint32_t a, bool w, uint32_t v) {
        if (ioSeen++ < ioMax)
            std::printf("  IO  %c $%08X = $%02X  (pc=$%08X clk=%lld)\n", w ? 'W' : 'R', a, v, cpu.getPC0(),
                        (long long)cpu.getClock());
    };
    mem.cuda().onByte = [&](bool toCuda, uint8_t b) {
        std::printf("  CUDA %s $%02X  (clk=%lld)\n", toCuda ? "<-" : "->", b,
                    (long long)cpu.getClock());
    };
    mem.cuda().onEdge = [&](uint8_t pb, int phase, bool xcvr) {
        static int n = 0;
        if (n++ < 40)
            std::printf("  EDGE pb=$%02X phase=%d xcvr=%d (clk=%lld)\n", pb, phase, xcvr,
                        (long long)cpu.getClock());
    };
    mem.onBusError = [&](uint32_t a, bool w) {
        if (berrSeen++ < berrMax)
            std::printf("  BERR %c $%08X  (pc=$%08X)\n", w ? 'W' : 'R', a, cpu.getPC0());
    };

    cpu.hardReset();
    std::printf("[q605_trace] reset: cpuHeld=%d overlay=%d pc=$%08X sp=$%08X\n",
                mem.cpuHeld(), mem.overlay(), cpu.getPC0(), cpu.getSP());

    std::vector<uint32_t> ring(pcRing, 0);
    size_t rp = 0;
    std::map<uint32_t, long> pcCov;               // 64 KB granularity
    long long slice = 5000;
    long long done = 0;
    while (done < cycles) {
        if (mem.cpuHeld()) { mem.tick(int(slice)); done += slice; continue; }
        moira::i64 t = cpu.getClock() + slice;
        bool stop = false;
        while (cpu.getClock() < t && !cpu.isHalted()) {
            uint32_t pc = cpu.getPC0();
            ring[rp++ % ring.size()] = pc;
            pcCov[pc >> 16]++;
            if (stopAt && pc == stopAt) {
                if (stopSkip > 0) stopSkip--;
                else { stop = true; break; }
            }
            if (watch && pc == watch) {
                static int wn = 0;
                uint32_t a0 = cpu.getA(0);
                uint32_t spSize = uint32_t(mem.peek8(a0+8))<<24 | uint32_t(mem.peek8(a0+9))<<16 |
                                  uint32_t(mem.peek8(a0+10))<<8 | mem.peek8(a0+11);
                if (wn++ < 400)
                    std::printf("  WATCH $%08X #%d: A0=$%08X spSize=$%08X A1=$%08X A3=$%08X A6=$%08X clk=%lld\n",
                                watch, wn, a0, spSize, cpu.getA(1), cpu.getA(3), cpu.getA(6),
                                (long long)cpu.getClock());
            }
            cpu.execute();
            if (cpu.justFaulted) {
                cpu.justFaulted = false;
                uint32_t fsp = cpu.getSP();
                std::printf("  VEC2-FRAME sp=$%08X:", fsp);
                for (int i = 0; i < 60; i++) {
                    if (i % 20 == 0) std::printf("\n   ");
                    std::printf("%02X ", mem.peek8(fsp + i));
                }
                std::printf("\n");
                auto peek32 = [&](uint32_t a) {
                    return uint32_t(mem.peek8(a)) << 24 | uint32_t(mem.peek8(a+1)) << 16 |
                           uint32_t(mem.peek8(a+2)) << 8 | mem.peek8(a+3);
                };
                uint32_t tc = cpu.getTC040(), urp = cpu.getURP040();
                std::printf("  MMU: TC040=$%08X URP=$%08X SRP=$%08X ITT0=$%08X ITT1=$%08X DTT0=$%08X DTT1=$%08X\n",
                            tc, urp, cpu.getSRP040(),
                            cpu.getITT0(), cpu.getITT1(), cpu.getDTT0(), cpu.getDTT1());
                uint32_t a0 = cpu.getA(0);
                std::printf("  [A0=spBlock]:");
                for (int i = 0; i < 64; i++) {
                    if (i % 16 == 0) std::printf("\n   $%08X:", a0 + i);
                    std::printf(" %02X", mem.peek8(a0 + i));
                }
                std::printf("\n");
                bool p8k = (tc >> 14) & 1;
                uint32_t root = (cpu.getSR() & 0x2000) ? cpu.getSRP040() : urp;
                for (uint32_t va : {0x40900000u, 0x408FE000u, 0x40800000u}) {
                    uint32_t ri = va >> 25, pi = (va >> 18) & 0x7F;
                    uint32_t pgi = p8k ? (va >> 13) & 0x1F : (va >> 12) & 0x3F;
                    uint32_t rd = peek32((root & ~0x1FFu) + ri * 4);
                    std::printf("  walk $%08X: root[$%02X]=$%08X", va, ri, rd);
                    if (rd & 2) {
                        uint32_t pd = peek32((rd & ~0x1FFu) + pi * 4);
                        std::printf(" ptr[$%02X]=$%08X", pi, pd);
                        if (pd & 2) {
                            uint32_t pg = peek32((pd & (p8k ? ~0x7Fu : ~0xFFu)) + pgi * 4);
                            std::printf(" page[$%02X]=$%08X", pgi, pg);
                        }
                    }
                    std::printf("\n");
                }
            }
        }
        if (stop) {
            std::printf("[q605_trace] STOP at $%08X\n", stopAt);
            std::printf("  RING (oldest->newest):");
            for (size_t i = 0; i < ring.size(); i++) {
                uint32_t rpc = ring[(rp + i) % ring.size()];
                if (rpc) std::printf(" $%08X", rpc);
            }
            std::printf("\n");
            for (int r = 0; r < 8; r++)
                std::printf("  D%d=$%08X  A%d=$%08X\n", r, cpu.getD(r), r,
                            r == 7 ? cpu.getSP() : cpu.getA(r));
            auto peekL = [&](uint32_t a) {
                return uint32_t(mem.peek8(a))<<24 | uint32_t(mem.peek8(a+1))<<16 |
                       uint32_t(mem.peek8(a+2))<<8 | mem.peek8(a+3);
            };
            std::printf("  LOWMEM MemTop($108)=$%08X BufPtr($10C)=$%08X "
                        "MemSize($1EF8?)=%08X\n",
                        peekL(0x108), peekL(0x10C), peekL(0x1EF8));
            uint32_t a1 = cpu.getA(1);
            std::printf("  [A1-32..A1+64]:");
            for (int i = -32; i < 64; i++) {
                if (i % 16 == 0) std::printf("\n   $%08X:", a1 + i);
                std::printf(" %02X", mem.peek8(a1 + i));
            }
            std::printf("\n  disasm from stop:");
            {
                char da2[128];
                uint32_t dpc = stopAt;
                for (int i = 0; i < 40; i++) {
                    int len = 2;
                    try { len = cpu.disassemble(da2, dpc); }
                    catch (...) { std::snprintf(da2, sizeof da2, "<fault>"); }
                    std::printf("\n   $%08X  %s", dpc, da2);
                    dpc += len;
                }
            }
            std::printf("\n  [SP..SP+96]:");
            uint32_t sp = cpu.getSP();
            for (int i = 0; i < 96; i++) {
                if (i % 16 == 0) std::printf("\n   $%08X:", sp + i);
                std::printf(" %02X", mem.peek8(sp + i));
            }
            std::printf("\n");
            break;
        }
        if (cpu.isHalted()) { std::printf("[q605_trace] CPU HALTED (double fault)\n"); break; }
        done += slice;
    }

    std::printf("\n[q605_trace] after %lld cycles: pc=$%08X sp=$%08X sr=$%04X halted=%d stopped=%d\n",
                done, cpu.getPC0(), cpu.getSP(), cpu.getSR(), cpu.isHalted(), cpu.isStopped());

    std::printf("-- vector histogram --\n");
    for (auto& [v, n] : cpu.vecHist)
        std::printf("  vec %3d : %ld\n", v, n);

    std::printf("-- PC coverage (64 KB regions, top 24) --\n");
    std::vector<std::pair<long, uint32_t>> cov;
    for (auto& [r, n] : pcCov) cov.push_back({n, r});
    std::sort(cov.rbegin(), cov.rend());
    int shown = 0;
    for (auto& [n, r] : cov) {
        if (shown++ >= 24) break;
        std::printf("  $%08X: %ld\n", r << 16, n);
    }

    {   // VRAM snapshot: nonzero span + a coarse 64×24 ASCII view (1bpp
        // assumption is fine for "did the ROM paint anything" checks)
        const uint8_t* vr = mem.vram();
        uint32_t lo = 0xFFFFFFFF, hi = 0;
        for (uint32_t i = 0; i < Q605Memory::kVramSize; i++)
            if (vr[i]) { if (i < lo) lo = i; hi = i; }
        if (hi) {
            std::printf("-- VRAM nonzero span $%05X-$%05X --\n", lo, hi);
            std::map<uint8_t, long> hist;
            for (uint32_t i = 0; i < Q605Memory::kVramSize; i++) hist[vr[i]]++;
            std::vector<std::pair<long, int>> hv;
            for (auto& [b, n] : hist) hv.push_back({n, b});
            std::sort(hv.rbegin(), hv.rend());
            std::printf("  top bytes:");
            for (size_t i = 0; i < hv.size() && i < 8; i++)
                std::printf(" $%02X:%ld", hv[i].second, hv[i].first);
            std::printf("\n  first row: ");
            for (int i = 0; i < 32; i++) std::printf("%02X", vr[i]);
            std::printf("\n  row 100:   ");
            for (int i = 0; i < 32; i++) std::printf("%02X", vr[100*640+i]);
            std::printf("\n");
            for (int row = 0; row < 24; row++) {
                char line[65] = {};
                for (int col = 0; col < 64; col++) {
                    // sample 640x480 @8bpp stride 640: cell = 10x20 px
                    uint32_t px = uint32_t(row * 20) * 640 + col * 10;
                    int n = 0;
                    for (int dy = 0; dy < 20; dy += 4)
                        for (int dx = 0; dx < 10; dx += 2)
                            if (px + dy * 640 + dx < Q605Memory::kVramSize &&
                                vr[px + dy * 640 + dx]) n++;
                    line[col] = n == 0 ? '.' : (n < 8 ? '+' : '#');
                }
                std::printf("  %s\n", line);
            }
        } else std::printf("-- VRAM all zero --\n");
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

    std::printf("-- SCSI: reads=%ld writes=%ld selects=%ld commands=%ld dmaBytes=%ld lastCmd=$%02X --\n",
                mem.scsi().reads, mem.scsi().writes, mem.scsi().selects,
                mem.scsi().commands, mem.scsi().dmaBytes, mem.scsi().lastCmd);
    mem.cuda().savePram("q605_trace.pram");
    return 0;
}
