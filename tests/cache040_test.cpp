// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// M1 gate (docs/CACHE_040.md § M1) — 68040 architectural cache-TAG
// state, POM68K_040_DCACHE. Two halves:
//
//   1. The Cache040 struct driven directly: set indexing and tags
//      (PA[9:4] / PA[31:10]), allocation policy per CM mode (reads
//      allocate in both cachable modes, copyback write misses line-fill,
//      writethrough write misses do not allocate), per-longword dirty
//      marking, line-crossing spans, MOVE16 (no allocation, line write
//      invalidates), 2-bit replacement with invalid-ways-first, and the
//      CINV (discard) vs CPUSH (push count + invalidate) scopes —
//      MC68040UM § 4.
//
//   2. The model behind a bare 68040 Moira on a flat bus: CACR DE/IE
//      gate allocation (MOVEC), the MMU-off default is writethrough
//      (UM § 3.5.1), a DTT0 transparent region carries its CM field
//      into the model, and the real $F4xx CINV/CPUSH opcodes reach the
//      tags with line/page/all scope. Data is still served by the bus —
//      the model must observe, never interfere.
//
// Exit 0 = pass, 1 = fail.

#include "Moira.h"
#include "MoiraCache040.h"

#include <cstdint>
#include <cstdio>
#include <vector>

using moira::Cache040;

namespace {

int gFails = 0;

void check(bool ok, const char* what) {
    std::printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}

// ---------------------------------------------------------------- part 1

void testStruct() {
    std::printf("part 1 — Cache040 struct (MC68040UM § 4 semantics)\n");

    constexpr int WT = Cache040::CM_WRITETHROUGH;
    constexpr int CB = Cache040::CM_COPYBACK;

    {   // Set indexing: PA[9:4]; tag: PA[31:10]
        check(Cache040::setOf(0x1000) == 0 && Cache040::setOf(0x1010) == 1,
              "consecutive lines land in consecutive sets");
        check(Cache040::setOf(0x1000) == Cache040::setOf(0x1400) &&
              Cache040::tagOf(0x1000) != Cache040::tagOf(0x1400),
              "1 KB apart = same set, different tag");
    }
    {   // Read allocation, both cachable modes
        Cache040 c;
        c.touch(0x1000, false, WT, 4);
        c.touch(0x2000, false, CB, 4);
        check(c.validCount() == 2 && c.lookup(0x1000) && c.lookup(0x2000),
              "reads allocate in writethrough AND copyback");
        check(!c.lookup(0x1000)->dirty && !c.lookup(0x2000)->dirty,
              "read-allocated lines are clean");
    }
    {   // Write-miss policy
        Cache040 c;
        c.touch(0x1000, true, WT, 4);
        check(c.validCount() == 0, "writethrough write miss: no allocate");
        c.touch(0x2008, true, CB, 4);
        check(c.lookup(0x2000) && c.lookup(0x2000)->dirty == 0x4,
              "copyback write miss: line fill + dirty longword 2");
    }
    {   // Write hits and per-longword dirty
        Cache040 c;
        c.touch(0x1000, false, CB, 4);
        c.touch(0x1000, true, WT, 4);
        check(c.lookup(0x1000)->dirty == 0,
              "writethrough write hit leaves the line clean");
        c.touch(0x1003, true, CB, 2);       // word spanning LW0/LW1
        check(c.lookup(0x1000)->dirty == 0x3,
              "misaligned word write dirties both spanned longwords");
    }
    {   // Non-cacheable modes bypass; a span crosses lines
        Cache040 c;
        c.touch(0x1000, false, Cache040::CM_SERIAL_NC, 4);
        c.touch(0x1000, false, Cache040::CM_NC, 4);
        check(c.validCount() == 0, "non-cacheable reads never allocate");
        c.touch(0x100C, true, CB, 8);
        check(c.lookup(0x1000) && c.lookup(0x1000)->dirty == 0x8 &&
              c.lookup(0x1010) && c.lookup(0x1010)->dirty == 0x1,
              "line-crossing write allocates and dirties both lines");
    }
    {   // MOVE16: no allocation; a line write invalidates a match
        Cache040 c;
        c.touch(0x1000, false, CB, 16, true);
        check(c.validCount() == 0, "MOVE16 read does not allocate");
        c.touch(0x1000, false, CB, 4);
        c.touch(0x1000, true, CB, 16, true);
        check(c.validCount() == 0, "MOVE16 line write invalidates a hit");
    }
    {   // Replacement: invalid ways first, then the 2-bit counter
        Cache040 c;
        c.touch(0x1000, false, CB, 4);
        c.touch(0x1400, false, CB, 4);
        c.touch(0x1800, false, CB, 4);
        c.touch(0x1C00, false, CB, 4);
        check(c.validCount() == 4, "four tags fill the four ways of a set");
        c.touch(0x2400, true, CB, 4);       // 5th: evicts (dirty drop = M1)
        check(c.validCount() == 4 && c.lookup(0x2400),
              "a fifth tag replaces a resident way");
    }
    {   // CINV vs CPUSH scopes
        Cache040 c;
        c.touch(0x1000, true, CB, 4);       // dirty
        c.touch(0x1010, false, CB, 4);      // clean, same page
        c.touch(0x2000, true, CB, 4);       // dirty, other page
        c.invalidateLine(0x1000);
        check(!c.lookup(0x1000) && c.pushes == 0,
              "CINV line discards dirty state without a push");
        c.touch(0x1000, true, CB, 4);
        check(c.pushLine(0x1000) == 1 && !c.lookup(0x1000) && c.pushes == 1,
              "CPUSH line pushes the dirty line, then invalidates");
        c.touch(0x1000, true, CB, 4);
        check(c.pushPage(0x1000, 0xFFFFF000) == 1 && !c.lookup(0x1010) &&
              c.lookup(0x2000),
              "CPUSH page pushes 1, invalidates clean too, spares others");
        check(c.pushAll() == 1 && c.validCount() == 0,
              "CPUSH all sweeps the remaining dirty line");
        c.touch(0x1000, true, CB, 4);
        c.touch(0x2000, false, CB, 4);
        c.invalidateAll();
        check(c.validCount() == 0 && c.pushes == 3,
              "CINV all empties the cache, push count untouched");
    }
}

// ---------------------------------------------------------------- part 2

class Cpu : public moira::Moira {
public:
    Cpu() : mem(1 << 24, 0) {
        setModel(moira::Model::M68040);
        setPomCache040(true);
    }
    std::vector<uint8_t> mem;

private:
    moira::u8 read8(moira::u32 a) const override { return mem[a & 0xFFFFFF]; }
    moira::u16 read16(moira::u32 a) const override {
        return moira::u16((mem[a & 0xFFFFFF] << 8) | mem[(a + 1) & 0xFFFFFF]);
    }
    void write8(moira::u32 a, moira::u8 v) const override {
        const_cast<Cpu*>(this)->mem[a & 0xFFFFFF] = v;
    }
    void write16(moira::u32 a, moira::u16 v) const override {
        write8(a, moira::u8(v >> 8));
        write8(a + 1, moira::u8(v));
    }
};

struct Img {
    Cpu cpu;
    uint32_t p = 0;

    void w16(uint16_t v) { cpu.mem[p++] = uint8_t(v >> 8); cpu.mem[p++] = uint8_t(v); }
    void w32(uint32_t v) { w16(uint16_t(v >> 16)); w16(uint16_t(v)); }
    void at(uint32_t a) { p = a; }
    void step(int n) { while (n--) cpu.execute(); }
};

void testCpu() {
    std::printf("part 2 — behind a bare 68040 Moira (flat 16 MB bus)\n");

    Img m;
    m.at(0); m.w32(0x2000); m.w32(0x1000);      // SSP, PC

    m.at(0x1000);
    // A: caches disabled out of reset (CACR = 0)
    m.w16(0x4E71);                              // NOP
    m.w16(0x4E71);                              // NOP
    // B: enable both caches
    m.w16(0x203C); m.w32(0x80008000);           // MOVE.L #DE|IE,D0
    m.w16(0x4E7B); m.w16(0x0002);               // MOVEC D0,CACR
    m.w16(0x4E71);                              // NOP
    // C: MMU off = writethrough default
    m.w16(0x41F9); m.w32(0x00008000);           // LEA $8000.L,A0
    m.w16(0x223C); m.w32(0x11223344);           // MOVE.L #$11223344,D1
    m.w16(0x2081);                              // MOVE.L D1,(A0)  wr miss
    m.w16(0x2410);                              // MOVE.L (A0),D2  rd alloc
    m.w16(0x2081);                              // MOVE.L D1,(A0)  wr hit
    // D: DTT0 copyback region at $40xxxxxx
    m.w16(0x203C); m.w32(0x4000C020);           // MOVE.L #dtt,D0
    m.w16(0x4E7B); m.w16(0x0006);               // MOVEC D0,DTT0
    m.w16(0x43F9); m.w32(0x40008000);           // LEA $40008000.L,A1
    m.w16(0x2281);                              // MOVE.L D1,(A1)  CB wr miss
    // E/F: CPUSH then CINV, line scope
    m.w16(0xF469);                              // CPUSH DC,(A1)
    m.w16(0x2281);                              // MOVE.L D1,(A1)
    m.w16(0xF449);                              // CINV DC,(A1)
    // G: page scope
    m.w16(0x2281);                              // MOVE.L D1,(A1)
    m.w16(0x23C1); m.w32(0x40008400);           // MOVE.L D1,$40008400.L
    m.w16(0x23C1); m.w32(0x40009000);           // MOVE.L D1,$40009000.L
    m.w16(0xF471);                              // CPUSH DC,(A1) page
    // H: CINV BC,all
    m.w16(0xF4D8);                              // CINV BC,ALL
    m.w16(0x4E71);                              // NOP
    // I: DC off, IE on
    m.w16(0x203C); m.w32(0x00008000);           // MOVE.L #IE,D0
    m.w16(0x4E7B); m.w16(0x0002);               // MOVEC D0,CACR
    m.w16(0x2410);                              // MOVE.L (A0),D2
    m.w16(0x4E71);                              // NOP

    auto& dc = m.cpu.pomCache040Data();
    auto& ic = m.cpu.pomCache040Inst();

    m.cpu.reset();

    m.step(2);                                  // A
    check(ic.validCount() == 0 && dc.validCount() == 0,
          "CACR = 0 out of reset: no allocation at all");

    m.step(3);                                  // B
    check(ic.validCount() > 0, "IE set: instruction fetches allocate");

    m.step(3);                                  // C: LEA/MOVE.L #/wr miss
    check(dc.lookup(0x8000) == nullptr,
          "MMU off = writethrough: write miss does not allocate");
    m.step(1);                                  //    rd
    check(dc.lookup(0x8000) && dc.lookup(0x8000)->dirty == 0,
          "read allocates a clean line");
    m.step(1);                                  //    wr hit
    check(dc.lookup(0x8000)->dirty == 0,
          "writethrough write hit stays clean");

    m.step(4);                                  // D
    check(dc.lookup(0x40008000) && (dc.lookup(0x40008000)->dirty & 1),
          "DTT0 copyback: write miss line-fills dirty");

    m.step(1);                                  // E: CPUSH line
    check(!dc.lookup(0x40008000) && dc.pushes == 1,
          "CPUSH DC,(An): push + invalidate through the opcode");
    m.step(2);                                  // F: re-dirty, CINV line
    check(!dc.lookup(0x40008000) && dc.pushes == 1,
          "CINV DC,(An): discard, no push");

    m.step(4);                                  // G
    check(!dc.lookup(0x40008000) && !dc.lookup(0x40008400) &&
              dc.lookup(0x40009000) && dc.pushes == 3,
          "CPUSH page: both page lines pushed, other page spared");

    m.step(1);                                  // H
    check(dc.validCount() == 0 && ic.validCount() == 0,
          "CINV BC,ALL empties both caches");
    m.step(1);                                  //    NOP refetches
    check(ic.validCount() > 0, "fetches re-allocate after CINV");

    m.step(3);                                  // I
    check(dc.validCount() == 0,
          "DC disabled: data reads no longer allocate");
    m.step(1);
    check(m.cpu.getD(2) == 0x11223344,
          "data is served by the bus, model observes only");
}

} // namespace

int main() {
    std::printf("cache040_test — M1 architectural tag state "
                "(docs/CACHE_040.md)\n");
    testStruct();
    testCpu();
    std::printf(gFails ? "FAILED (%d)\n" : "PASSED\n", gFails);
    return gFails ? 1 : 0;
}
