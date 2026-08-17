// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// M1-M3 gate (docs/CACHE_040.md) — complete 68040 architectural cache
// state, data, copyback, snooping and transaction timing. Three halves:
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
//      tags/data with line/page/all scope.
//
//   3. Observable stale copyback data, dirty replacement/CPUSH, both
//      snoop behaviours (including disabled-cache and cross-line cases),
//      and configurable hit/fill timing.
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
        // PA[9] must reach the index — a 32-set cache passes the two
        // checks above (bughunt 2026-08-05: half-pinned geometry)
        check(Cache040::setOf(0x1000) != Cache040::setOf(0x1200),
              "PA[9] is an index bit (64 sets, not 32)");
    }
    {   // Top-of-address-space spans must terminate (bughunt 2026-08-05:
        // the u32 span loop was a tautology at 0xFFFFFFFF and hung)
        Cache040 c;
        c.touch(0xFFFFFFFC, false, WT, 4);
        c.touch(0xFFFFFFFF, true, CB, 1);
        check(c.lookup(0xFFFFFFF0) && (c.lookup(0xFFFFFFF0)->dirty & 0x8),
              "access ending at $FFFFFFFF terminates and allocates");
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
    {   // Replacement: invalid ways first, then the 2-bit counter.
        // The VICTIM is pinned (bughunt 2026-08-05: validCount alone let
        // a rotation-only or never-rotating policy pass): the fifth tag
        // must evict way 0 (counter at 0), the sixth way 1.
        Cache040 c;
        c.touch(0x1000, false, CB, 4);
        c.touch(0x1400, false, CB, 4);
        c.touch(0x1800, false, CB, 4);
        c.touch(0x1C00, false, CB, 4);
        check(c.validCount() == 4, "four tags fill the four ways of a set");
        c.touch(0x2400, true, CB, 4);       // 5th: evicts (dirty drop = M1)
        check(c.validCount() == 4 && c.lookup(0x2400) &&
              !c.lookup(0x1000) && c.lookup(0x1400) && c.lookup(0x1800) &&
              c.lookup(0x1C00),
              "the fifth tag evicts way 0, the counter's pick");
        c.touch(0x2800, false, CB, 4);      // 6th: counter advanced
        check(!c.lookup(0x1400) && c.lookup(0x2400) && c.lookup(0x2800) &&
              c.lookup(0x1800) && c.lookup(0x1C00),
              "the sixth tag evicts way 1: the 2-bit counter rotates");
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

// Flat 16 MB bus with a /BERR hole at $F00000-$F0FFFF (the BerrCpu
// pattern): phase L steers a page-table walk into it to prove a
// bus-erroring descriptor chain is swallowed, not delivered.
class Cpu : public moira::Moira {
public:
    Cpu() : mem(1 << 24, 0) {
        setModel(moira::Model::M68040);
        setPomCache040(true);
    }
    std::vector<uint8_t> mem;

    static bool inHole(moira::u32 a) {
        return (a & 0xFFFFFF) >= 0xF00000 && (a & 0xFFFFFF) < 0xF10000;
    }

private:
    moira::u8 read8(moira::u32 a) const override {
        if (inHole(a)) const_cast<Cpu*>(this)->extBusError040();
        return mem[a & 0xFFFFFF];
    }
    moira::u16 read16(moira::u32 a) const override {
        if (inHole(a)) const_cast<Cpu*>(this)->extBusError040();
        return moira::u16((mem[a & 0xFFFFFF] << 8) | mem[(a + 1) & 0xFFFFFF]);
    }
    void write8(moira::u32 a, moira::u8 v) const override {
        if (inHole(a)) const_cast<Cpu*>(this)->extBusError040();
        const_cast<Cpu*>(this)->mem[a & 0xFFFFFF] = v;
    }
    void write16(moira::u32 a, moira::u16 v) const override {
        if (inHole(a)) const_cast<Cpu*>(this)->extBusError040();
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
    // J: retention while disabled + CINV/CPUSH on a disabled cache
    m.w16(0x203C); m.w32(0x80008000);           // MOVE.L #DE|IE,D0
    m.w16(0x4E7B); m.w16(0x0002);               // MOVEC D0,CACR
    m.w16(0x2410);                              // MOVE.L (A0),D2  WT alloc
    m.w16(0x2281);                              // MOVE.L D1,(A1)  CB dirty
    m.w16(0x203C); m.w32(0x00008000);           // MOVE.L #IE,D0
    m.w16(0x4E7B); m.w16(0x0002);               // MOVEC D0,CACR   DC off
    m.w16(0x2410);                              // MOVE.L (A0),D2  no alloc
    m.w16(0xF469);                              // CPUSH DC,(A1)   disabled
    m.w16(0xF448);                              // CINV DC,(A0)    disabled
    // K: MMU on — ATC-hit, peek-walk and unmapped-skip resolver paths
    m.w16(0x203C); m.w32(0x0000C000);           // MOVE.L #tt,D0
    m.w16(0x4E7B); m.w16(0x0004);               // MOVEC D0,ITT0  ($00xxxxxx)
    m.w16(0x4E7B); m.w16(0x0007);               // MOVEC D0,DTT1  ($00xxxxxx)
    m.w16(0x203C); m.w32(0x80008000);           // MOVE.L #DE|IE,D0
    m.w16(0x4E7B); m.w16(0x0002);               // MOVEC D0,CACR  both on
    m.w16(0x203C); m.w32(0x00004000);           // MOVE.L #root,D0
    m.w16(0x4E7B); m.w16(0x0807);               // MOVEC D0,SRP
    m.w16(0x203C); m.w32(0x00008000);           // MOVE.L #E|4K,D0
    m.w16(0x4E7B); m.w16(0x0003);               // MOVEC D0,TC    MMU ON
    m.w16(0x47F9); m.w32(0x01000000);           // LEA $01000000.L,A3
    m.w16(0x2681);                              // MOVE.L D1,(A3) walk+fill
    m.w16(0xF46B);                              // CPUSH DC,(A3)  ATC hit
    m.w16(0x2681);                              // MOVE.L D1,(A3) re-dirty
    m.w16(0x4E7B); m.w16(0x0003);               // MOVEC D0,TC    ATC flush
    m.w16(0xF44B);                              // CINV DC,(A3)   peek walk
    m.w16(0x49F9); m.w32(0x01800000);           // LEA $01800000.L,A4
    m.w16(0xF44C);                              // CINV DC,(A4)   unmapped
    // L: a resident descriptor chain into the /BERR hole must be
    // swallowed (treated as unmapped), never delivered to the guest
    m.w16(0x4BF9); m.w32(0x02000000);           // LEA $02000000.L,A5
    m.w16(0xF44D);                              // CINV DC,(A5)   berr chain
    m.w16(0x7E2A);                              // MOVEQ #42,D7

    // Phase K page tables (4K pages, SRP = $4000): logical $01000000 →
    // phys $9000, copyback (CM = 01); $01800000 hits an invalid pointer
    // entry; $02000000's ROOT descriptor is resident but points into
    // the /BERR hole (phase L)
    m.at(0x4000); m.w32(0x00004202);            // root[0] → pointer table
    m.w32(0x00F00002);                          // root[1] → the hole
    m.at(0x4300); m.w32(0x00004402);            // pointer[$01xxxxxx] → page
    m.at(0x4400); m.w32(0x00009021);            // page[0] = $9000 | CB | R

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

    m.step(4);                                  // J: re-enable, alloc 2 lines
    check(dc.validCount() == 2 && dc.lookup(0x8000) &&
              dc.lookup(0x40008000) && (dc.lookup(0x40008000)->dirty & 1),
          "two lines resident before the disable");
    m.step(3);                                  //    DC off + a read
    check(dc.validCount() == 2,
          "UM 4.4: a disabled cache RETAINS its contents");
    m.step(1);                                  //    CPUSH while disabled
    check(!dc.lookup(0x40008000) && dc.validCount() == 1 && dc.pushes == 4,
          "CPUSH acts on a disabled cache (no CACR gate)");
    m.step(1);                                  //    CINV while disabled
    check(dc.validCount() == 0 && dc.pushes == 4,
          "CINV acts on a disabled cache too");

    m.step(11);                                 // K: MMU on, walked write
    check(dc.lookup(0x9000) && (dc.lookup(0x9000)->dirty & 1),
          "MMU on: the walk feeds copyback CM into the model");
    m.step(1);                                  //    CPUSH via ATC hit
    check(!dc.lookup(0x9000) && dc.pushes == 5,
          "CPUSH resolves its operand through the ATC");
    m.step(3);                                  //    re-dirty, flush, CINV
    check(!dc.lookup(0x9000) && dc.pushes == 5,
          "CINV resolves through the read-only peek walk after a flush");
    m.step(2);                                  //    unmapped operand
    check(dc.validCount() == 0,
          "an unmapped CINV operand is skipped, not faulted");

    m.step(3);                                  // L: berr chain + marker
    check(m.cpu.getD(7) == 42,
          "a bus-erroring descriptor chain is swallowed (no exception)");
}

void testDataPath() {
    std::printf("part 3 — M2 line data, copyback, snoop and timing\n");

    Cpu c;
    c.setCACR(0x80000000);                    // DE
    c.setDTT0(0x4000C020);                    // $40xxxxxx, both S/U, copyback
    c.setPomCache040Timing({0, 8, 2});

    auto put32 = [&](uint32_t a, uint32_t v) {
        a &= 0xFFFFFF;
        c.mem[a + 0] = uint8_t(v >> 24); c.mem[a + 1] = uint8_t(v >> 16);
        c.mem[a + 2] = uint8_t(v >> 8);  c.mem[a + 3] = uint8_t(v);
    };
    auto get32 = [&](uint32_t a) {
        a &= 0xFFFFFF;
        return uint32_t(c.mem[a]) << 24 | uint32_t(c.mem[a + 1]) << 16 |
               uint32_t(c.mem[a + 2]) << 8 | c.mem[a + 3];
    };

    constexpr uint32_t A = 0x40008000;
    constexpr uint32_t OLD = 0x11223344;
    constexpr uint32_t NEW = 0xA1B2C3D4;
    put32(A, OLD);

    uint32_t value = 0;
    check(c.pomJitWriteData(A, 4, NEW) && get32(A) == OLD,
          "copyback write updates the line but leaves memory stale");
    auto &jitLine = c.pomJitCache040R.e[
        (A >> 4) & (moira::Moira::PomJitCache040Table::kEntries - 1)];
    auto &jitWriteLine = c.pomJitCache040W.e[
        (A >> 4) & (moira::Moira::PomJitCache040Table::kEntries - 1)];
    check(jitLine.tag == c.pomJitCache040Tag(A, false) &&
          jitLine.generation == c.pomJitCache040Gen &&
          jitLine.line == c.pomCache040Data().lookup(A) &&
          jitLine.physicalTag == moira::Cache040::tagOf(A),
          "an exact copyback access publishes a validated JIT read line");
    check(jitWriteLine.tag == c.pomJitCache040Tag(A, false) &&
          jitWriteLine.generation == c.pomJitCache040Gen &&
          jitWriteLine.line == jitLine.line,
          "an exact copyback write publishes a stronger JIT write line");
    const uint32_t publishedGen = jitLine.generation;
    c.pomJitDtlbFlush();
    check(jitLine.generation == publishedGen &&
          jitWriteLine.generation == publishedGen &&
          jitLine.generation != c.pomJitCache040Gen,
          "a data-translation flush invalidates both published lines by epoch");
    check(c.pomJitReadData(A, 4, value) && value == NEW,
          "a CPU read observes dirty cached data, not stale memory");
    check(jitLine.generation == c.pomJitCache040Gen,
          "the next exact hit republishes the line under the current epoch");

    // Four more tags in the same set evict the original way-0 line.
    for (int i = 1; i <= 4; i++) {
        put32(A + 0x400u * uint32_t(i), 0x01010101u * uint32_t(i));
        check(c.pomJitReadData(A + 0x400u * uint32_t(i), 4, value),
              "same-set fill succeeds");
    }
    check(get32(A) == NEW,
          "dirty replacement writes the displaced longword back");

    // Re-dirty, then prove both snoop encodings and disabled-cache snooping.
    put32(A, OLD);
    check(c.pomJitWriteData(A, 4, NEW), "re-dirty line for snoop checks");
    c.setCACR(0);                              // snooper still searches it
    uint8_t snooped[4] = {};
    check(c.pomSnoop040Read(A, snooped, 4, false) &&
          snooped[0] == 0xA1 && snooped[3] == 0xD4,
          "SC=01 read supplies dirty data even while DC is disabled");
    uint8_t cleanSnoop[4] = {0x7A, 0x7A, 0x7A, 0x7A};
    check(!c.pomSnoop040Read(A + 4, cleanSnoop, 4, false) &&
          cleanSnoop[0] == 0x7A,
          "snoop read does not supply a clean longword in a dirty line");
    const uint8_t incoming[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    check(c.pomSnoop040Write(A, incoming, 4, true),
          "SC=01 write sinks data and inhibits memory on a dirty hit");
    check(!c.pomSnoop040Write(A + 4, incoming, 4, true) &&
          c.pomCache040Data().lookup(A)->dirty == 1,
          "write sink updates a clean longword without asserting MI");
    c.setCACR(0x80000000);
    check(c.pomJitReadData(A, 4, value) && value == 0xDEADBEEF &&
          get32(A) == OLD,
          "sunk snoop data remains dirty and CPU-visible");
    check(!c.pomSnoop040Write(A, incoming, 4, false) &&
          jitLine.line && !jitLine.line->valid,
          "SC=10 invalidation makes a published native line fail validation");
    check(c.pomJitReadData(A, 4, value) && value == OLD,
          "the following CPU read misses and sees memory");

    // A sink range may cross from a dirty line into a clean one. MI is
    // asserted for the transaction, but the clean line must not become
    // dirty merely because an earlier byte hit dirty data.
    c.pomCache040Data().invalidateAll();
    put32(A + 12, OLD); put32(A + 16, OLD);
    check(c.pomJitWriteData(A + 12, 4, NEW) &&
          c.pomJitReadData(A + 16, 4, value),
          "prepare adjacent dirty and clean lines");
    const uint8_t crossing[2] = {0x55, 0x66};
    check(c.pomSnoop040Write(A + 15, crossing, 2, true) &&
          c.pomCache040Data().lookup(A + 12)->dirty != 0 &&
          c.pomCache040Data().lookup(A + 16)->dirty == 0,
          "cross-line sink does not propagate dirty state to a clean line");

    // The timing overlay charges the four-beat fill once and a hit zero.
    c.pomCache040Data().invalidateAll();
    c.setClock(0);
    check(c.pomJitReadData(A, 4, value) && c.getClock() == 8,
          "cache miss charges one four-beat line-fill latency");
    check(c.pomJitReadData(A, 4, value) && c.getClock() == 8,
          "cache hit adds no external-bus latency");
}

} // namespace

int main() {
    std::printf("cache040_test — M1-M3 architectural cache state "
                "(docs/CACHE_040.md)\n");
    testStruct();
    testCpu();
    testDataPath();
    std::printf(gFails ? "FAILED (%d)\n" : "PASSED\n", gFails);
    return gFails ? 1 : 0;
}
