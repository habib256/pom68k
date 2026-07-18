// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// q605_trace — LC 475 / Quadra 605 ROM boot trace (Q5 dev tool, the
// lcii_trace pattern): runs the real FF7439EE ROM on the Q605 machine
// and reports PC coverage, the exception-vector histogram, bus-error
// sites and the first I/O accesses. Not a CTest gate.
//
// Usage: q605_trace <rom> [--cycles N] [--io N] [--berr N] [--pcring N]

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
    }
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s <rom> [--cycles N] [--io N] [--berr N] [--pcring N]\n", argv[0]); return 2; }
    long long cycles = 200000000;   // 8 machine-seconds at 25 MHz
    int ioMax = 60, berrMax = 40; size_t pcRing = 32;
    uint32_t stopAt = 0, watch = 0;
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--cycles" && i + 1 < argc) cycles = atoll(argv[++i]);
        else if (a == "--io" && i + 1 < argc) ioMax = atoi(argv[++i]);
        else if (a == "--berr" && i + 1 < argc) berrMax = atoi(argv[++i]);
        else if (a == "--pcring" && i + 1 < argc) pcRing = size_t(atoll(argv[++i]));
        else if (a == "--stop-at" && i + 1 < argc) stopAt = uint32_t(strtoul(argv[++i], nullptr, 16));
        else if (a == "--watch" && i + 1 < argc) watch = uint32_t(strtoul(argv[++i], nullptr, 16));
    }

    std::ifstream in(argv[1], std::ios::binary);
    if (!in) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());

    Q605Memory mem;
    if (!mem.loadRom(rom)) { std::fprintf(stderr, "ROM must be 1 MB (got %zu)\n", rom.size()); return 1; }
    TraceCpu cpu(mem);
    mem.setCpu(&cpu);

    int ioSeen = 0, berrSeen = 0;
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
            if (stopAt && pc == stopAt) { stop = true; break; }
            if (watch && pc == watch) {
                static int wn = 0;
                if (wn++ < 120)
                    std::printf("  WATCH $%08X #%d: D1=$%08X D6=$%08X D7=$%08X A0=$%08X A1=$%08X A6=$%08X clk=%lld\n",
                                watch, wn, cpu.getD(1), cpu.getD(6), cpu.getD(7),
                                cpu.getA(0), cpu.getA(1), cpu.getA(6),
                                (long long)cpu.getClock());
            }
            cpu.execute();
        }
        if (stop) {
            std::printf("[q605_trace] STOP at $%08X\n", stopAt);
            for (int r = 0; r < 8; r++)
                std::printf("  D%d=$%08X  A%d=$%08X\n", r, cpu.getD(r), r,
                            r == 7 ? cpu.getSP() : cpu.getA(r));
            uint32_t a1 = cpu.getA(1);
            std::printf("  [A1-32..A1+64]:");
            for (int i = -32; i < 64; i++) {
                if (i % 16 == 0) std::printf("\n   $%08X:", a1 + i);
                std::printf(" %02X", mem.peek8(a1 + i));
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

    std::printf("-- last %zu PCs --\n", ring.size());
    char da[128];
    for (size_t i = 0; i < ring.size(); i++) {
        uint32_t pc = ring[(rp + i) % ring.size()];
        if (!pc) continue;
        try { cpu.disassemble(da, pc); }
        catch (...) { std::snprintf(da, sizeof da, "<dasm fault>"); }
        std::printf("  $%08X  %s\n", pc, da);
    }
    return 0;
}
