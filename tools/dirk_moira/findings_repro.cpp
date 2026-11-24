// MIT (see LICENSE-MIT). Build: c++ -std=c++20 -DMOIRA_EMULATE_BUS_ERROR=true -I<Moira> findings_repro.cpp <Moira sources>
// Reproducers for the findings reported to Dirk Hoffmann, run against his
// unmodified Moira (FPU branch). Each case prints the manual's expectation
// and what the core does. Host: flat 1 MB RAM; addresses >= $F00000 assert
// /BERR (throw BusError with the address only, as the interface documents).
#include "Moira.h"
#include <cstdio>
#include <cstring>
#include <vector>

using namespace moira;

struct Cpu : public Moira {
    std::vector<u8> mem = std::vector<u8>(1 << 20, 0);
    bool port32 = false;
    mutable long progReads = 0;          // instruction-space reads in [lo, hi)
    u32 lo = 0, hi = 0;
    mutable long berrCount = 0;

    void w16(u32 a, u16 v) { mem[a] = u8(v >> 8); mem[a + 1] = u8(v); }
    void w32(u32 a, u32 v) { w16(a, u16(v >> 16)); w16(a + 2, u16(v)); }
    u16 r16(u32 a) const { return u16(mem[a] << 8 | mem[a + 1]); }
    u32 r32(u32 a) const { return u32(r16(a)) << 16 | r16(a + 2); }

    void berr(u32 a) const { if (a >= 0xF00000) { berrCount++; throw BusError(a); } }
    void note(u32 a) const { if (a >= lo && a < hi) progReads++; }

    u8 read8(u32 a) const override { berr(a); note(a); return mem[a & 0xFFFFF]; }
    u16 read16(u32 a) const override { berr(a); note(a); return r16(a & 0xFFFFF); }
    u32 read32(u32 a) const override { berr(a); note(a); return r32(a & 0xFFFFF); }
    void write8(u32 a, u8 v) const override { berr(a); const_cast<Cpu*>(this)->mem[a & 0xFFFFF] = v; }
    void write16(u32 a, u16 v) const override { berr(a); const_cast<Cpu*>(this)->w16(a & 0xFFFFF, v); }
    void write32(u32 a, u32 v) const override { berr(a); const_cast<Cpu*>(this)->w32(a & 0xFFFFF, v); }
    u8 dsack(u32) const override { return port32 ? DSACK_32 : DSACK_16; }
};

static const u32 kCode = 0x1000, kHandler = 0x3000, kStack = 0x8000;

static void boot(Cpu& c, CPUModel m) {
    c.setModel(m);
    c.w32(0, kStack); c.w32(4, kCode);
    for (int v = 2; v < 64; v++) c.w32(4 * v, kHandler);
}
static void runUntil(Cpu& c, u32 pc, int max = 100000) {
    for (int i = 0; i < max && c.getPC0() != pc; i++) c.execute();
}

// 1. Host /BERR on a data read: the stacked SSW.
static void hostBerr(CPUModel m, const char* name) {
    Cpu c; boot(c, m);
    c.w16(kCode, 0x1039); c.w32(kCode + 2, 0x00F00000);   // MOVE.B $F00000,D0
    c.w16(kCode + 6, 0x60FE);
    c.w16(kHandler, 0x60FE);                               // BRA.S *
    c.reset();
    runUntil(c, kHandler);
    u32 sp = c.getSP();
    u16 fmt = c.r16(sp + 6);
    if ((fmt >> 12) == 0x7) {                              // 68040 access error frame
        u16 ssw = c.r16(sp + 0xC); u32 fa = c.r32(sp + 0x14);
        std::printf("  %-8s format $%X  SSW=$%04X  fault address=$%08X   (MC68040UM-1993 §8.4.6: RW=1 read, SIZ=byte, TT/TM)\n",
                    name, fmt >> 12, ssw, fa);
    } else {
        u16 ssw = c.r16(sp + 0xA); u32 fa = c.r32(sp + 0x10);
        std::printf("  %-8s format $%X  SSW=$%04X  fault address=$%08X   manual: DF|RW|SIZ=byte|FC=5 = $0155\n",
                    name, fmt >> 12, ssw, fa);
    }
}

// 2. Software completion: the handler supplies the data input buffer and
//    clears DF; RTE must not rerun the read (MC68030UM-1989 §8.2.2).
static void dfCleared() {
    Cpu c; boot(c, CPUModel::M68030);
    c.w16(kCode, 0x1039); c.w32(kCode + 2, 0x00F00000);   // MOVE.B $F00000,D0
    c.w16(kCode + 6, 0x60FE);
    c.w16(kHandler + 0, 0x08AF); c.w16(kHandler + 2, 0x0000); c.w16(kHandler + 4, 0x000A); // BCLR #0,($A,A7): DF
    c.w16(kHandler + 6, 0x2F7C); c.w32(kHandler + 8, 0xCAFEBABE); c.w16(kHandler + 12, 0x002C); // MOVE.L #..,($2C,A7): DIB
    c.w16(kHandler + 14, 0x4E73);                                                        // RTE
    c.reset();
    c.setD(0, 0);
    for (int i = 0; i < 60 && c.getPC0() != kCode + 6; i++) c.execute();
    std::printf("  68030    bus errors taken: %ld, reached next instruction: %s, D0=$%08X   manual: 1, yes, $000000BE\n",
                c.berrCount, c.getPC0() == kCode + 6 ? "yes" : "no", c.getD(0));
}

// 3. 68020 timing, cache on, 32-bit port: 8 x NOP + DBF per iteration.
static long loopClocks(int n, u16 extra) {
    Cpu c; c.port32 = true; boot(c, CPUModel::M68020);
    u32 a = kCode;
    c.w16(a, 0x7001); a += 2;                     // MOVEQ #1,D0
    c.w16(a, 0x4E7B); c.w16(a + 2, 0x0002); a += 4; // MOVEC D0,CACR (E)
    c.w16(a, 0x323C); c.w16(a + 2, u16(n - 1)); a += 4; // MOVE.W #n-1,D1
    u32 loop = a;
    if (extra) { c.w16(a, 0x46FC); c.w16(a + 2, 0x2700); a += 4; } // MOVE #$2700,SR
    else for (int i = 0; i < 8; i++) { c.w16(a, 0x4E71); a += 2; }  // 8 x NOP
    c.w16(a, 0x51C9); c.w16(a + 2, u16(loop - (a + 2))); a += 4;     // DBF D1,loop
    c.w16(a, 0x60FE);
    c.reset();
    runUntil(c, a);
    return long(c.getClock());
}
static void cacheTiming() {
    double per = double(loopClocks(200, 0) - loopClocks(100, 0)) / 100.0;
    std::printf("  68020    8 x NOP + DBF, cached: %.2f clocks/iteration   manual cache case: 22 (MC68020UM-1992 §8.2)\n", per);
}

// 4. Bus reads of a cached loop that refills SR (MOVE #$2700,SR + DBF).
static void cachedReads() {
    Cpu c; c.port32 = true; boot(c, CPUModel::M68020);
    u32 a = kCode;
    c.w16(a, 0x7001); c.w16(a + 2, 0x4E7B); c.w16(a + 4, 0x0002); a += 6;
    c.w16(a, 0x323C); c.w16(a + 2, 199); a += 4;
    u32 loop = a;
    c.w16(a, 0x46FC); c.w16(a + 2, 0x2700); a += 4;
    c.w16(a, 0x51C9); c.w16(a + 2, u16(loop - (a + 2))); a += 4;
    c.w16(a, 0x60FE);
    c.reset();
    // Warm up 100 iterations, then count instruction-range reads for the rest.
    for (int i = 0; i < 2 + 100 * 2; i++) c.execute();
    c.lo = kCode; c.hi = a + 2; c.progReads = 0;
    for (int i = 0; i < 99 * 2; i++) c.execute();
    std::printf("  68020    MOVE #$2700,SR + DBF, cached: %.2f bus reads/iteration   manual: 0 (instruction in the cache)\n",
                double(c.progReads) / 99.0);
}

// 5. 68040 unimplemented FPU instruction, then FSAVE in the handler.
static void fsaveUnimp() {
    Cpu c; boot(c, CPUModel::M68040);
    c.w16(kCode, 0xF200); c.w16(kCode + 2, 0x0421);   // FMOD.X FP1,FP0
    c.w16(kCode + 4, 0x60FE);
    c.w16(kHandler, 0xF327);                          // FSAVE -(A7)
    c.w16(kHandler + 2, 0x60FE);
    c.reset();
    runUntil(c, kHandler);
    u32 sp0 = c.getSP();
    u16 fmt = c.r16(sp0 + 6);
    c.execute();                                      // FSAVE
    u32 hdr = c.r32(c.getSP());
    std::printf("  68040    exception format $%X vector %u; FSAVE header $%08X (version $%02X, size $%02X)   manual: UNIMP frame, version $41, size $30 (§9.8)\n",
                fmt >> 12, (fmt & 0xFFF) / 4, hdr, hdr >> 24, (hdr >> 16) & 0xFF);
}

int main() {
    std::puts("1. Host /BERR on a byte read: special status word");
    hostBerr(CPUModel::M68020, "68020");
    hostBerr(CPUModel::M68030, "68030");
    hostBerr(CPUModel::M68040, "68040");
    std::puts("2. Handler completes the read and clears DF, then RTE");
    dfCleared();
    std::puts("3. 68020 instruction cache timing");
    cacheTiming();
    std::puts("4. Instruction reads of a cached loop");
    cachedReads();
    std::puts("5. 68040 FMOD and FSAVE");
    fsaveUnimp();
}
