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
#include <cstdarg>
#include <cstdio>
#include <set>
#include <string>
#include <string_view>
#include <vector>

using moira::Cache040;

namespace {

int gFails = 0;

// The matrices run tens of thousands of cases. Each assertion prints once
// when it first passes and every time it fails, after the case line that
// caseContext() recorded — 590 000 lines of "ok" made the ASan/UBSan
// nightly leg time out (run 42802857141).
char gCase[256] = {};
std::set<std::string> gPassed;

void caseContext(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(gCase, sizeof gCase, fmt, args);
    va_end(args);
}

void check(bool ok, const char* what) {
    if (ok) {
        if (gPassed.insert(what).second) std::printf("  %-58s ok\n", what);
        return;
    }
    if (gCase[0]) std::fputs(gCase, stdout);
    std::printf("  %-58s FAIL\n", what);
    gFails++;
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
    mutable unsigned tableReads = 0;

    static bool inHole(moira::u32 a) {
        return (a & 0xFFFFFF) >= 0xF00000 && (a & 0xFFFFFF) < 0xF10000;
    }

private:
    moira::u8 read8(moira::u32 a) const override {
        if (a >= 0x4000 && a < 0x5000) ++tableReads;
        if (inHole(a)) const_cast<Cpu*>(this)->extBusError040();
        return mem[a & 0xFFFFFF];
    }
    moira::u16 read16(moira::u32 a) const override {
        if (a >= 0x4000 && a < 0x5000) ++tableReads;
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
    // K: MMU on — cache management must spare a translated alias,
    // both while its ATC entry is present and after an ATC flush.
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
    m.w16(0xF46B);                              // CPUSH physical $01000000
    m.w16(0x2681);                              // MOVE.L D1,(A3) re-dirty
    m.w16(0x4E7B); m.w16(0x0003);               // MOVEC D0,TC    ATC flush
    m.w16(0xF44B);                              // CINV physical $01000000
    m.w16(0x49F9); m.w32(0x01800000);           // LEA $01800000.L,A4
    m.w16(0xF44C);                              // CINV physical $01800000
    // L: the operand is physical even if interpreting it as logical
    // would lead the table walker into the /BERR hole.
    m.w16(0x4BF9); m.w32(0x02000000);           // LEA $02000000.L,A5
    m.w16(0xF44D);                              // CINV physical $02000000
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
    m.step(1);                                  //    physical CPUSH, no match
    check(dc.lookup(0x9000) && dc.lookup(0x9000)->dirty && dc.pushes == 4,
          "CPUSH does not select the operand's translated alias");
    m.step(3);                                  //    re-dirty, flush, CINV
    check(dc.lookup(0x9000) && dc.lookup(0x9000)->dirty && dc.pushes == 4,
          "CINV spares the translated alias after an ATC flush");
    dc.touch(0x01800000, false, Cache040::CM_WRITETHROUGH, 4);
    m.step(2);                                  //    unmapped operand
    check(!dc.lookup(0x01800000) && dc.lookup(0x9000),
          "CINV selects a physical line with no logical mapping");

    dc.touch(0x02000000, false, Cache040::CM_WRITETHROUGH, 4);
    const auto tableReads = m.cpu.tableReads;
    m.step(3);                                  // L: berr chain + marker
    check(m.cpu.getD(7) == 42 && !dc.lookup(0x02000000) &&
          m.cpu.tableReads == tableReads,
          "CINV never follows a descriptor chain into the bus-error hole");
}

void testPhysicalOperands() {
    // [B] MC68040UM-1993 §4.2: CINV/CPUSH take a physical address in An.
    // Keep both the physical target and its unrelated logical translation
    // resident. A no-op, an ATC lookup and a cold table walk all differ from
    // the required cache selection; CPUSH must also write the physical RAM.
    std::printf("part 2b — physical cache operands with paging enabled\n");
    constexpr uint32_t target = 0x00808000;
    constexpr uint32_t old = 0x11223344;
    constexpr uint32_t fresh = 0xA1B2C3D4;
    for (bool page8k : {false, true})
    for (bool warm : {false, true})
    for (bool enabled : {false, true})
    for (bool push : {false, true})
    for (unsigned caches : {0u, 1u, 2u, 3u})
    for (unsigned scope : {1u, 2u}) {
        Img m;
        const uint32_t alias = page8k ? 0x8000 : 0x9000;
        const uint32_t addresses[] = {
            target, target + 0x10, target + 0x1000, target + 0x2000, alias
        };
        m.at(0); m.w32(0x2000); m.w32(0x1110);
        m.at(0x1110);                         // fetch uses a different set
        m.w16(uint16_t(0xF400 | (push ? 0x20 : 0) |
                       (caches << 6) | (scope << 3))); // (A0)
        m.w16(0x4E71); m.w16(0x4E71);
        m.at(0x4000); m.w32(0x4202);
        m.at(0x4280); m.w32(0x4402);
        m.at(page8k ? 0x4410 : 0x4420); m.w32(alias | 0x21);
        for (auto address : addresses) {
            m.at(address); m.w32(old);
        }
        m.cpu.reset();
        m.cpu.setITT0(0x0000C000);             // instruction fetch is TT
        m.cpu.setSRP040(0x4000);
        m.cpu.setTC040(page8k ? 0xC000 : 0x8000);
        m.cpu.setCACR(enabled ? 0x80008000 : 0);
        m.cpu.setA(0, target + 3);            // low line bits are ignored
        if (warm) {
            uint32_t value = 0;
            check(m.cpu.pomJitReadData(target, 4, value) && value == old,
                  "prime the different logical-to-physical translation");
        }
        auto &dc = m.cpu.pomCache040Data();
        auto &ic = m.cpu.pomCache040Inst();
        for (auto address : addresses) {
            dc.touch(address, false, Cache040::CM_COPYBACK, 4);
            ic.touch(address, false, Cache040::CM_WRITETHROUGH, 4);
            auto *line = dc.lookup(address);
            line->dirty = 1;
            for (unsigned byte = 0; byte < 4; ++byte)
                line->data[byte] = uint8_t(fresh >> (24 - 8 * byte));
        }
        // Management must also act on retained lines with CACR disabled.
        const auto readsBefore = m.cpu.tableReads;
        const auto tablesBefore = std::vector<uint8_t>(
            m.cpu.mem.begin() + 0x4000, m.cpu.mem.begin() + 0x5000);
        m.step(1);
        bool selected = true, memory = true;
        for (auto address : addresses) {
            const bool inScope = scope == 1 ? address == target :
                address >= target && address < target + (page8k ? 8192 : 4096);
            selected &= (dc.lookup(address) == nullptr) ==
                        (inScope && (caches & 1));
            selected &= (ic.lookup(address) == nullptr) ==
                        (inScope && (caches & 2));
            const auto &ram = m.cpu.mem;
            const uint32_t value = uint32_t(ram[address]) << 24 |
                uint32_t(ram[address + 1]) << 16 |
                uint32_t(ram[address + 2]) << 8 | ram[address + 3];
            memory &= value == (push && inScope && (caches & 1) ? fresh : old);
        }
        std::printf("  %s cache=%u scope=%u %uK %s ATC, cache %s\n",
                    push ? "CPUSH" : "CINV", caches, scope,
                    page8k ? 8 : 4, warm ? "warm" : "cold",
                    enabled ? "enabled" : "disabled");
        check(selected, "select physical cache lines, spare translated aliases");
        check(memory, "only CPUSH data writes selected physical memory");
        check(m.cpu.tableReads == readsBefore &&
              std::vector<uint8_t>(m.cpu.mem.begin() + 0x4000,
                                   m.cpu.mem.begin() + 0x5000) == tablesBefore,
              "cache operand neither reads nor updates page tables");
    }
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
        if (i == 4) c.pomCache040Data().rot = 0;  // pin way 0; the counter has its own cell
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


// Records only operand-line transfers; opcode and descriptor reads remain
// real bus accesses but cannot mask a missing push or an accidental fill.
class CoherenceCpu : public moira::Moira {
public:
    explicit CoherenceCpu(moira::Model model) : mem(1 << 20) { setModel(model); }
    std::vector<uint8_t> mem;
    struct Transfer {
        uint32_t address, value;
        unsigned bytes;
        bool write;
        bool operator==(const Transfer&) const = default;
    };
    mutable std::vector<Transfer> transfers;
    bool recording = false, recordTables = false;
    uint32_t failRead = ~0u, failWrite = ~0u;
    unsigned failBytes = 0;
    mutable unsigned skipWrites = 0, busErrors = 0, codeReads = 0;
    // Test handler's software completion of a pending physical writeback.
    void completeWrite(uint32_t a, unsigned bytes, uint32_t value) {
        if (bytes == 1) write8(a, uint8_t(value));
        else if (bytes == 2) write16(a, uint16_t(value));
        else { write16(a, uint16_t(value >> 16)); write16(a + 2, uint16_t(value)); }
    }
    void put16(uint32_t a, uint16_t v) { mem[a] = uint8_t(v >> 8); mem[a + 1] = uint8_t(v); }
    void put32(uint32_t a, uint32_t v) { put16(a, uint16_t(v >> 16)); put16(a + 2, uint16_t(v)); }
private:
    void record(uint32_t a, uint32_t v, unsigned bytes, bool write) const {
        if (recording && !write && a >= 0x1000 && a < 0x1020) ++codeReads;
        if (recording && ((a >= 0x8000 && a < 0xA010) ||
                          (recordTables && ((a >= 0x4000 && a < 0x5000) ||
                                            (a >= 0xA100 && a < 0xA110)))))
            transfers.push_back({a, v, bytes, write});
    }
    void checkWrite(uint32_t a, unsigned bytes) const {
        if (!recording || a != failWrite || bytes != failBytes) return;
        if (skipWrites) { --skipWrites; return; }
        ++busErrors;
        auto& cpu = *const_cast<CoherenceCpu*>(this);
        if (getModel() >= moira::Model::M68EC040) cpu.extBusError040();
        else cpu.extBusError();
    }
    void checkRead(uint32_t a) const {
        if (!recording || a != failRead) return;
        ++busErrors; const_cast<CoherenceCpu*>(this)->extBusError040();
    }
    moira::u8 read8(moira::u32 a) const override {
        const auto v = mem[a & 0xFFFFF]; record(a, v, 1, false); checkRead(a); return v;
    }
    moira::u16 read16(moira::u32 a) const override {
        const auto v = uint16_t(mem[a & 0xFFFFF] << 8 | mem[(a + 1) & 0xFFFFF]);
        record(a, v, 2, false); checkRead(a); return v;
    }
    void write8(moira::u32 a, moira::u8 v) const override {
        record(a, v, 1, true); checkWrite(a, 1);
        const_cast<CoherenceCpu*>(this)->mem[a & 0xFFFFF] = v;
    }
    void write16(moira::u32 a, moira::u16 v) const override {
        record(a, v, 2, true); checkWrite(a, 2);
        const_cast<CoherenceCpu*>(this)->put16(a & 0xFFFFF, v);
    }
};

void testImplicitNoncacheable() {
    // [B] MC68040UM-1993 §§7.4.1/7.4.3: MOVES and RMW are implicitly NC;
    // §§4.3.2/7.4.5: matching dirty data is pushed before the RMW starts;
    // a clean line is invalidated.
    // Distinguish this from disabling CACR.DE: the controller must still
    // resolve the resident line before the external operand transfer.
    unsigned cases = 0;
    for (auto model : {moira::Model::M68EC040, moira::Model::M68LC040,
                       moira::Model::M68040})
    for (unsigned mapping = 0; mapping < (model == moira::Model::M68EC040 ? 1u : 3u); ++mapping)
    for (unsigned resident = 0; resident < 3; ++resident)
    for (unsigned op = 0; op < 12; ++op)
    for (unsigned fc : {1u, 2u, 5u, 6u}) {
        if (op >= 6 && fc != 5) continue; // TAS and successful CAS/CAS2 use current SR
        CoherenceCpu cpu(model);
        const bool paged = mapping != 0, page8k = mapping == 2;
        const uint32_t la = paged ? 0x10000000 : 0x8000;
        const unsigned bytes = op == 6 ? 1 :
            1u << (op < 6 ? op % 3 : op < 10 ? op - 7 : op - 9);
        const bool movesRead = op < 3, movesWrite = op >= 3 && op < 6;
        const bool tas = op == 6, cas = op >= 7, cas2 = op >= 10;
        uint32_t pc = 0x1000;
        auto emit = [&](uint16_t word) { cpu.put16(pc, word); pc += 2; };
        if (op < 6) {
            emit(uint16_t(0x0E10 | ((op % 3) << 6))); // MOVES.B/W/L
            emit(movesWrite ? 0x1800 : 0x0000);       // D1 -> memory / memory -> D0
        } else if (tas) emit(0x4AD0);                // TAS (A0)
        else if (!cas2) { emit(uint16_t(0x0AD0 + (op - 7) * 0x200)); emit(0x0040); } // CAS D0,D1,(A0)
        else { emit(op == 10 ? 0x0CFC : 0x0EFC); emit(0x8040); emit(0x90C2); } // CAS2 D0:D2,D1:D3,(A0):(A1)
        const uint32_t next = pc;
        emit(0x2E10); emit(0x4E71); emit(0x4E71);    // MOVE.L (A0),D7; NOPs
        cpu.put32(0, 0x70000); cpu.put32(4, 0x1000);
        for (unsigned off = 0; off < 16; off += 4) cpu.put32(0x8000 + off, 0xAABBCCDD);
        cpu.reset(); cpu.setSR(0x2000); cpu.setPomCache040(true);
        cpu.setCACR(0x80008000);
        if (paged) {
            cpu.put32(0x4020, 0x4202); cpu.put32(0x4200, 0x4402);
            cpu.put32(0x4400, 0x8039);              // copyback, U/M/R
            cpu.setITT0(0x0000C000);               // instruction driver remains transparent
            cpu.setURP040(0x4000); cpu.setSRP040(0x4000);
            cpu.setTC040(page8k ? 0xC000 : 0x8000);
        } else cpu.setDTT0(0x0000C020);            // copyback transparent RAM
        uint32_t value = 0;
        if (resident == 1) {
            check(cpu.pomJitReadData(la, 4, value), "implicit NC: prepare clean line");
            cpu.put32(0x8000, 0xDEADBEEF);          // newer RAM must win over clean cache
        } else if (resident == 2) {
            check(cpu.pomJitWriteData(la, 4, 0x11223344) &&
                  cpu.pomJitWriteData(la + 8, 4, 0x55667788),
                  "implicit NC: prepare two dirty longwords outside RAM");
        }
        std::vector<uint8_t> expected(cpu.mem.begin() + 0x8000, cpu.mem.begin() + 0x8010);
        if (resident == 2) {
            const uint8_t dirty[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
            for (unsigned i = 0; i < 4; ++i) { expected[i] = dirty[i]; expected[8 + i] = dirty[4 + i]; }
        }
        std::vector<CoherenceCpu::Transfer> bus;
        auto readValue = [&](unsigned offset, unsigned n) {
            uint32_t v = 0; for (unsigned i = 0; i < n; ++i) v = v << 8 | expected[offset + i];
            return v;
        };
        if (resident == 2)
            for (unsigned off = 0; off < 16; off += 2) bus.push_back({0x8000 + off, readValue(off, 2), 2, true});
        const uint32_t operand = readValue(0, bytes);
        const uint32_t operand2 = readValue(8, bytes);
        auto transfer = [&](unsigned offset, bool write) {
            if (bytes < 4) bus.push_back({0x8000 + offset, readValue(offset, bytes), bytes, write});
            else for (unsigned off : {offset, offset + 2})
                bus.push_back({0x8000 + off, readValue(off, 2), 2, write});
        };
        if (!movesWrite) { transfer(0, false); if (cas2) transfer(8, false); }
        if (!movesRead) {
            const uint32_t output = tas ? operand | 0x80 : 0xCAFEBABE;
            for (unsigned i = 0; i < bytes; ++i) {
                expected[i] = uint8_t(output >> (8 * (bytes - 1 - i)));
                if (cas2) expected[8 + i] = uint8_t(0x0BADF00D >> (8 * (bytes - 1 - i)));
            }
            transfer(0, true); if (cas2) transfer(8, true);
        }
        cpu.setA(0, la); cpu.setA(1, la + 8); cpu.setD(0, cas ? operand : 0xCCDD0000);
        cpu.setD(1, 0xCAFEBABE); cpu.setD(2, operand2); cpu.setD(3, 0x0BADF00D);
        cpu.setSFC(fc); cpu.setDFC(fc);
        const auto fills = cpu.pomCache040Data().fills;
        cpu.recording = true; cpu.execute(); cpu.recording = false;
        caseContext("  implicit NC model=%u map=%u resident=%u op=%u fc=%u\n",
                    unsigned(model), mapping, resident, op, fc);
        check(cpu.getPC0() == next && !cpu.isHalted(), "implicit NC: instruction completes");
        check(cpu.transfers == bus, "implicit NC: dirty pushes precede exact external operand transfers");
        check(std::vector<uint8_t>(cpu.mem.begin() + 0x8000, cpu.mem.begin() + 0x8010) == expected,
              "implicit NC: RAM includes unmodified dirty neighbours");
        check(!cpu.pomCache040Data().lookup(0x8000) && cpu.pomCache040Data().fills == fills,
              "implicit NC: invalidate matching line without allocating");
        const uint32_t mask = bytes == 4 ? ~0u : (1u << (8 * bytes)) - 1;
        check(!movesRead || (cpu.getD(0) & mask) == operand, "implicit NC: MOVES reads current data");
        check(!cas || (cpu.getSR() & 4), "implicit NC: CAS compares current copyback value");
        cpu.execute();                            // ordinary data access must cache again
        check(cpu.getD(7) == readValue(0, 4) && cpu.pomCache040Data().lookup(0x8000),
              "implicit NC: next ordinary access sees current RAM and allocates");
        ++cases;
    }
    std::printf("implicit NC: %u executed sequences\n", cases);
}



void testCas2WriteProtection() {
    // CAS2's read stage needs write permission even when comparison would
    // fail. Cover either operand, paging sizes, and the cacheless product
    // path as well as the optional cache model.
    for (auto model : {moira::Model::M68LC040, moira::Model::M68040})
    for (bool page8k : {false, true})
    for (bool cache : {false, true})
    for (unsigned protectedOperand : {0u, 1u}) {
        CoherenceCpu cpu(model);
        const uint32_t la = 0x10000000, second = la + (page8k ? 8192 : 4096);
        cpu.put32(0, 0x70000); cpu.put32(4, 0x1000); cpu.put32(8, 0x3000);
        cpu.put16(0x1000, 0x0EFC); cpu.put16(0x1002, 0x8040); cpu.put16(0x1004, 0x90C2);
        cpu.put16(0x1006, 0x4E71); cpu.put16(0x1008, 0x4E71);
        cpu.put16(0x3000, 0x4E71); cpu.put16(0x3002, 0x4E71);
        cpu.put32(0x8000, 0x11223344); cpu.put32(0xA000, 0x55667788);
        cpu.reset(); cpu.setSR(0x2000); cpu.setPomCache040(cache);
        cpu.setCACR(cache ? 0x80008000 : 0);
        cpu.setITT0(0x0000C000); cpu.setDTT0(0x0000C040); // low driver/vector/stack
        cpu.put32(0x4020, 0x4202); cpu.put32(0x4200, 0x4402);
        cpu.put32(0x4400, 0x8039 | (protectedOperand == 0 ? 4 : 0));
        cpu.put32(0x4404, 0xA039 | (protectedOperand == 1 ? 4 : 0));
        cpu.setURP040(0x4000); cpu.setSRP040(0x4000);
        cpu.setTC040(page8k ? 0xC000 : 0x8000);
        cpu.setA(0, la); cpu.setA(1, second); cpu.setD(0, 0); cpu.setD(2, 0);
        cpu.recording = true; cpu.execute(); cpu.recording = false;
        auto word = [&](uint32_t a) { return uint16_t(cpu.mem[a] << 8 | cpu.mem[a + 1]); };
        auto lng = [&](uint32_t a) { return uint32_t(word(a)) << 16 | word(a + 2); };
        const uint32_t sp = cpu.getSP();
        check(cpu.getPC0() == 0x3000 && !cpu.isHalted() && sp == 0x70000 - 60 &&
              word(sp + 6) == 0x7008 && (word(sp + 12) & 0x0700) == 0x0600 &&
              lng(sp + 20) == (protectedOperand ? second : la) && lng(sp + 2) == 0x1000,
              "CAS2 protected read: format 7, LK/ATC, write semantics and logical operand");
        bool readsOnly = true;
        for (const auto& transfer : cpu.transfers) readsOnly &= !transfer.write;
        check(readsOnly && lng(0x8000) == 0x11223344 && lng(0xA000) == 0x55667788,
              "CAS2 protected read: no operand store, even when comparison would fail");
    }
}


void testCasMismatchTerminalWrite() {
    // [B] MC68040UM-1993 §7.4.5: a failed CAS writes its operand back;
    // failed CAS2 writes only the second operand, even if comparison 1 failed.
    unsigned cases = 0;
    for (auto model : {moira::Model::M68EC020, moira::Model::M68020, moira::Model::M68030,
                       moira::Model::M68EC040, moira::Model::M68LC040, moira::Model::M68040})
    for (unsigned mapping = 0; mapping < (model < moira::Model::M68LC040 ? 1u : 3u); ++mapping)
    for (bool super : {false, true})
    for (unsigned cache = 0; cache < (model < moira::Model::M68EC040 ? 1u : 4u); ++cache)
    for (unsigned op = 0; op < 13; ++op)
    for (int fault = -1; fault < 2; ++fault) {
        const bool is040 = model >= moira::Model::M68EC040, cas2 = op >= 9;
        const unsigned bytes = cas2 ? (op < 11 ? 2 : 4) : 1u << (op / 3);
        const unsigned mode = cas2 ? 0 : op % 3; // (A0), (A0)+, -(A0)
        const bool failFirst = !cas2 || !(op & 1); // both CAS2 mismatch branches
        if ((!is040 && fault >= 0) || (bytes < 4 && fault == 1)) continue;
        CoherenceCpu cpu(model);
        const bool paged = mapping != 0, page8k = mapping == 2;
        const uint32_t la = paged ? 0x10000000 : 0x8000;
        const uint32_t la2 = paged ? la + (page8k ? 8192 : 4096) : 0xA000;
        const uint32_t data[2] = {0x11223344, 0x55667788};
        const uint32_t neighbours[2] = {0xDEADBEEF, 0xC001D00D};
        uint32_t pc = 0x1000;
        auto emit = [&](uint16_t v) { cpu.put16(pc, v); pc += 2; };
        if (!cas2) { emit(uint16_t(0x0AD0 + (op / 3) * 0x200 + mode * 8)); emit(0x0040); }
        else { emit(bytes == 2 ? 0x0CFC : 0x0EFC); emit(0x8040); emit(0x90C2); }
        const uint32_t next = pc;
        emit(0x4E71); emit(0x4E71); emit(0x4E71);
        cpu.put32(0, 0x70000); cpu.put32(4, 0x1000); cpu.put32(8, 0x3000);
        cpu.put16(0x3000, 0x4E73); cpu.put16(0x3002, 0x4E71); // RTE handler
        for (unsigned line = 0; line < (cas2 ? 2u : 1u); ++line) {
            const uint32_t pa = line ? 0xA000 : 0x8000;
            for (unsigned off = 0; off < 16; off += 4) cpu.put32(pa + off, 0xAABBCCDD);
            cpu.put32(pa, cache == 3 ? 0x99AABBCC : data[line]);
        }
        cpu.reset(); cpu.setISP(0x70000); cpu.setUSP(0x60000);
        cpu.setSR(uint16_t((super ? 0x2000 : 0) | 0x10));
        cpu.setPomCache040(cache != 0); cpu.setCACR(cache ? 0x80008000 : 0);
        if (paged) {
            cpu.put32(0x4020, 0x4202); cpu.put32(0x4200, 0x4402);
            cpu.put32(0x4400, 0x8039); cpu.put32(0x4404, 0xA039);
            cpu.setITT0(0x0000C000); cpu.setDTT0(0x0000C040);
            cpu.setURP040(0x4000); cpu.setSRP040(0x4000);
            cpu.setTC040(page8k ? 0xC000 : 0x8000);
        } else if (is040) cpu.setDTT0(0x0000C020);
        uint32_t ignored = 0;
        std::vector<CoherenceCpu::Transfer> bus;
        for (unsigned line = 0; line < (cas2 ? 2u : 1u); ++line) {
            const uint32_t address = line ? la2 : la, pa = line ? 0xA000 : 0x8000;
            if (cache == 2) check(cpu.pomJitReadData(address, 4, ignored), "terminal write: seed clean line");
            if (cache == 3) {
                check(cpu.pomJitWriteData(address, 4, data[line]) &&
                      cpu.pomJitWriteData(address + 8, 4, neighbours[line]), "terminal write: seed dirty line");
                for (unsigned off = 0; off < 16; off += 2) {
                    const uint32_t value = off < 4 ? data[line] : off >= 8 && off < 12 ? neighbours[line] : 0xAABBCCDD;
                    bus.push_back({pa + off, (value >> (off % 4 ? 0 : 16)) & 0xFFFF, 2, true});
                }
            }
            const uint32_t operand = data[line] >> (32 - bytes * 8);
            if (bytes < 4) bus.push_back({pa, operand, bytes, false});
            else { bus.push_back({pa, data[line] >> 16, 2, false}); bus.push_back({pa + 2, data[line] & 0xFFFF, 2, false}); }
        }
        const uint32_t mask = bytes == 4 ? ~0u : (1u << (bytes * 8)) - 1;
        const uint32_t operand1 = data[0] >> (32 - bytes * 8), operand2 = data[1] >> (32 - bytes * 8);
        const uint32_t compare1 = (0xCAFE0000 & ~mask) | ((operand1 + (failFirst ? 1 : 0)) & mask);
        const uint32_t compare2 = (0xBEEF0000 & ~mask) | ((operand2 + (failFirst ? 0 : 1)) & mask);
        cpu.setD(0, compare1); cpu.setD(1, 0xCAFEBABE); cpu.setD(2, compare2); cpu.setD(3, 0x0BADF00D);
        cpu.setA(0, la + (mode == 2 ? bytes : 0)); cpu.setA(1, la2);
        const uint32_t target = cas2 ? 0xA000 : 0x8000, logicalTarget = cas2 ? la2 : la;
        const uint32_t payload = cas2 ? operand2 : operand1;
        if (is040) {
            if (bytes < 4) bus.push_back({target, payload, bytes, true});
            else {
                bus.push_back({target, payload >> 16, 2, true});
                if (fault != 0) bus.push_back({target + 2, payload & 0xFFFF, 2, true});
            }
        }
        // The target can be written once by a dirty push before the final
        // transfer. Fault only the terminal write, including either host
        // word of a longword. Legacy cores arm the trap as a no-write control.
        cpu.failWrite = target + (fault == 1 ? 2 : 0);
        cpu.failBytes = bytes == 1 ? 1 : 2;
        cpu.skipWrites = cache == 3 && cpu.failBytes == 2 ? 1 : 0;
        if (fault < 0 && is040) cpu.failWrite = ~0u;
        cpu.recording = true; cpu.execute(); cpu.recording = false;
        caseContext("  terminal CAS%s bytes=%u model=%u map=%u cache=%u super=%u mode=%u first=%u fault=%d\n",
                    cas2 ? "2" : "", bytes, unsigned(model), mapping, cache, super, mode, failFirst, fault);
        check(cpu.transfers == bus, "terminal write: exact operand order, width and original read value");
        check(cpu.getCCR() == 0x19 && cpu.getA(0) == la + (mode == 1 ? bytes : 0),
              "terminal write: failed comparison flags and single EA update");
        check(cpu.getD(1) == 0xCAFEBABE && cpu.getD(3) == 0x0BADF00D &&
              cpu.getD(0) == (fault < 0 ? (compare1 & ~mask) | operand1 : compare1) &&
              (!cas2 || cpu.getD(2) == (fault < 0 ? (compare2 & ~mask) | operand2 : compare2)),
              "terminal write: compare-register commit and unchanged update registers");
        auto peek = [&](uint32_t a) {
            if (cache) if (const auto* line = cpu.pomCache040Data().lookup(a)) return line->data[a & 15];
            return cpu.mem[a];
        };
        auto word = [&](uint32_t a) { return uint16_t(peek(a) << 8 | peek(a + 1)); };
        auto lng = [&](uint32_t a) { return uint32_t(word(a)) << 16 | word(a + 2); };
        if (fault < 0) {
            check(cpu.busErrors == 0 && cpu.getPC0() == next && !cpu.isHalted(),
                  "terminal write: normal completion, legacy cores have no failed-compare write");
        } else {
            const uint32_t sp = cpu.getSP();
            const uint16_t attributes = uint16_t((bytes == 1 ? 0x20 : bytes == 2 ? 0x40 : 0) | (super ? 5 : 1));
            check(cpu.busErrors == 1 && cpu.getPC0() == 0x3000 && !cpu.isHalted() && sp == 0x70000 - 60 &&
                  word(sp + 6) == 0x7008 && lng(sp + 2) == next && (word(sp) & 0x201F) == ((super ? 0x2000 : 0) | 0x19) &&
                  word(sp + 12) == (attributes | 0x200) && word(sp + 14) == (attributes | 0x80) &&
                  lng(sp + 20) == logicalTarget && lng(sp + 24) == logicalTarget && lng(sp + 28) == payload,
                  "terminal write: /BERR format 7, next PC, LK/write, logical address and WB3 read value");
            if (cpu.busErrors == 1 && cpu.getPC0() == 0x3000) {
                cpu.failWrite = ~0u; cpu.transfers.clear(); cpu.recording = true;
                cpu.completeWrite(target, bytes, payload); // handler completes WB3
                cpu.execute(); cpu.execute();             // RTE, then next NOP
                cpu.recording = false;
                check(cpu.getPC0() == next + 2 && cpu.getCCR() == 0x19 &&
                      cpu.transfers.size() == (bytes == 4 ? 2u : 1u),
                      "terminal write: WB3 completion and RTE do not repeat operand reads");
            }
        }
        for (unsigned line = 0; line < (cas2 ? 2u : 1u); ++line) {
            const uint32_t pa = line ? 0xA000 : 0x8000;
            check(lng(pa) == data[line] && lng(pa + 8) == (cache == 3 ? neighbours[line] : 0xAABBCCDD),
                  "terminal write: operand and dirty neighbours preserved");
        }
        ++cases;
    }
    std::printf("terminal CAS/CAS2: %u executed sequences\n", cases);
}


void testMove16FaultContext() {
    // [B] MC68040UM-1993 §§8.4.6.2/8.4.6.4: line-size MOVE16 data
    // transfer, logical initial line address, independent of physical beat.
    // WB2/PD and restart checks pin the existing WinUAE-derived fault model.
    unsigned cases = 0;
    for (auto model : {moira::Model::M68EC040, moira::Model::M68LC040, moira::Model::M68040})
    for (unsigned mapping = 0; mapping < (model == moira::Model::M68EC040 ? 1u : 3u); ++mapping)
    for (bool super : {false, true})
    for (unsigned path = 0; path < 3; ++path) // bus, instruction window, cache model (misses only)
    for (unsigned form = 0; form < 5; ++form)
    for (unsigned kind = 0; kind < 5; ++kind) // success, read/write BERR, read/write ATC fault
    for (unsigned beat = 0; beat < 8; ++beat) {
        const bool paged = mapping != 0, page8k = mapping == 2;
        if ((kind >= 3 && !paged) || ((kind == 0 || kind >= 3) && beat != 0)) continue;
        CoherenceCpu cpu(model);
        const uint32_t src = paged ? 0x10000000 : 0x8000;
        const uint32_t dst = paged ? src + (page8k ? 8192 : 4096) : 0xA000;
        const uint32_t a0 = (form == 2 || form == 4) ? dst + 7 : src + 11, a1 = dst + 7;
        const uint16_t ops[] = {0xF620, 0xF600, 0xF608, 0xF610, 0xF618};
        cpu.put32(0, 0x70000); cpu.put32(4, 0x1000); cpu.put32(8, 0x3000);
        cpu.put16(0x1000, ops[form]);
        const uint32_t next = form == 0 ? 0x1004 : 0x1006;
        if (form == 0) cpu.put16(0x1002, 0x9000);
        else cpu.put32(0x1002, form == 2 || form == 4 ? src + 11 : dst + 7);
        cpu.put16(next, 0x4E71); cpu.put16(next + 2, 0x4E71);
        cpu.put16(0x3000, model == moira::Model::M68EC040 ? 0x4E71 : 0xF518);
        cpu.put16(0x3002, 0x4E73); // flush repaired MMU tables (NOP on EC040); RTE
        cpu.put16(0x3004, 0x4E71);
        uint32_t data[4];
        for (unsigned i = 0; i < 4; ++i) {
            data[i] = 0x10203040 + 0x11111111 * i;
            cpu.put32(0x8000 + 4 * i, data[i]); cpu.put32(0xA000 + 4 * i, 0xAABBCCDD);
        }
        const auto sourceBytes = std::vector<uint8_t>(cpu.mem.begin() + 0x8000, cpu.mem.begin() + 0x8010);
        auto expectedDest = std::vector<uint8_t>(cpu.mem.begin() + 0xA000, cpu.mem.begin() + 0xA010);
        cpu.reset(); cpu.setISP(0x70000); cpu.setUSP(0x60000);
        cpu.setSR(uint16_t((super ? 0x2000 : 0) | 0x1F));
        cpu.setPomCache040(path == 2); cpu.setCACR(path == 2 ? 0x80008000 : 0);
        if (paged) {
            cpu.put32(0x4020, 0x4202); cpu.put32(0x4200, 0x4402);
            cpu.put32(0x4400, kind == 3 ? 0 : 0x8039);
            cpu.put32(0x4404, kind == 4 ? 0xA03D : 0xA039);
            cpu.setITT0(0x0000C000); cpu.setDTT0(0x0000C040);
            cpu.setURP040(0x4000); cpu.setSRP040(0x4000);
            cpu.setTC040(page8k ? 0xC000 : 0x8000);
        }
        cpu.setA(0, a0); cpu.setA(1, a1);
        if (path == 1) cpu.pomJitArm(cpu.mem.data() + 0x1000, 0x1000, 0x1000, 16, super);
        if (kind == 1) cpu.failRead = 0x8000 + 2 * beat;
        if (kind == 2) { cpu.failWrite = 0xA000 + 2 * beat; cpu.failBytes = 2; }
        const unsigned reads = kind == 3 ? 0 : kind == 1 ? beat + 1 : 8;
        const unsigned writes = kind == 2 ? beat + 1 : kind == 0 ? 8 : 0;
        std::vector<CoherenceCpu::Transfer> bus;
        for (unsigned i = 0; i < reads; ++i)
            bus.push_back({0x8000 + 2 * i, (data[i / 2] >> (i & 1 ? 0 : 16)) & 0xFFFF, 2, false});
        for (unsigned i = 0; i < writes; ++i)
            bus.push_back({0xA000 + 2 * i, (data[i / 2] >> (i & 1 ? 0 : 16)) & 0xFFFF, 2, true});
        const unsigned completed = kind == 0 ? 16 : kind == 2 ? 2 * beat : 0;
        for (unsigned i = 0; i < completed; ++i) expectedDest[i] = sourceBytes[i];
        cpu.recording = true; cpu.execute(); cpu.recording = false;
        caseContext("  MOVE16 model=%u map=%u super=%u path=%u form=%u kind=%u beat=%u\n",
                    unsigned(model), mapping, super, path, form, kind, beat);
        check(cpu.transfers == bus && cpu.busErrors == ((kind == 1 || kind == 2) ? 1u : 0u),
              "MOVE16: exact read/write prefix and bus-error position");
        check(cpu.getCCR() == 0x1F && (path != 1 || cpu.codeReads == 0),
              "MOVE16: flags preserved, instruction-window prefix really bypasses fetch bus");
        check(std::vector<uint8_t>(cpu.mem.begin() + 0xA000, cpu.mem.begin() + 0xA010) == expectedDest &&
              std::vector<uint8_t>(cpu.mem.begin() + 0x8000, cpu.mem.begin() + 0x8010) == sourceBytes,
              "MOVE16: completed bytes and untouched source/suffix");
        if (kind) {
            const uint32_t sp = cpu.getSP();
            auto word = [&](uint32_t a) { return uint16_t(cpu.mem[a] << 8 | cpu.mem[a + 1]); };
            auto lng = [&](uint32_t a) { return uint32_t(word(a)) << 16 | word(a + 2); };
            const bool write = kind == 2 || kind == 4;
            const uint16_t fc = super ? 5 : 1;
            const uint16_t ssw = uint16_t(0x68 | fc | (write ? 0 : 0x100) | (kind >= 3 ? 0x400 : 0));
            check(!cpu.isHalted() && cpu.getPC0() == 0x3000 && sp == 0x70000 - 60 &&
                  word(sp + 6) == 0x7008 && lng(sp + 2) == 0x1000 && word(sp + 12) == ssw &&
                  lng(sp + 20) == (write ? dst : src),
                  "MOVE16: format 7 identifies original logical data line, size, direction and FC");
            bool pending = !(word(sp + 14) & 0x80) && word(sp + 18) == 0 &&
                           word(sp + 16) == (write ? 0xE8 | fc : 0);
            if (write) {
                pending &= lng(sp + 32) == dst;
                for (unsigned i = 0; i < 4; ++i) pending &= lng(sp + 44 + 4 * i) == data[i];
            }
            check(pending, "MOVE16: write uses destination WB2 address and complete PD0-3, no WB3 write");
            check(cpu.getA(0) == a0 && cpu.getA(1) == a1, "MOVE16: fault defers postincrements");
            if (cpu.getPC0() == 0x3000) {
                cpu.failRead = cpu.failWrite = ~0u;
                if (paged) { cpu.put32(0x4400, 0x8039); cpu.put32(0x4404, 0xA039); }
                cpu.execute(); cpu.execute(); cpu.execute(); // flush repaired tables, RTE, retry on safe RAM
            }
        }
        check(cpu.getPC0() == next && !cpu.isHalted() && cpu.getCCR() == 0x1F &&
              cpu.getA(0) == a0 + (form <= 2 ? 16 : 0) && cpu.getA(1) == a1 + (form == 0 ? 16 : 0) &&
              std::vector<uint8_t>(cpu.mem.begin() + 0xA000, cpu.mem.begin() + 0xA010) == sourceBytes &&
              !cpu.pomCache040Data().lookup(0x8000) && !cpu.pomCache040Data().lookup(0xA000),
              "MOVE16: normal completion or repaired RAM retry copies the line and increments once");
        ++cases;
    }
    std::printf("MOVE16 fault contexts: %u executed sequences\n", cases);
}


void testFaultAfterSuccessfulPush() {
    // Keep the bus-error stamp of the logical operand after resolving a
    // dirty physical alias. The push itself succeeds; push-fault frames
    // need their own physical-address/PD contract and are not tested here.
    unsigned cases = 0;
    for (auto model : {moira::Model::M68EC040, moira::Model::M68LC040, moira::Model::M68040})
    for (unsigned mapping = 0; mapping < (model == moira::Model::M68EC040 ? 1u : 3u); ++mapping)
    for (unsigned fc : {1u, 5u})
    for (unsigned op = 0; op < 4; ++op)
    for (unsigned beat = 0; beat < (op == 2 ? 2u : 1u); ++beat) {
        CoherenceCpu cpu(model);
        const bool tas = op == 3, paged = mapping != 0, page8k = mapping == 2;
        const bool super = !tas || fc == 5;
        const unsigned bytes = tas ? 1 : 1u << op;
        const uint32_t la = paged ? 0x10000000 : 0x8000;
        cpu.put32(0, 0x70000); cpu.put32(4, 0x1000); cpu.put32(8, 0x3000);
        cpu.put16(0x1000, tas ? 0x4AD0 : uint16_t(0x0E10 + op * 0x40));
        cpu.put16(0x1002, tas ? 0x4E71 : 0); cpu.put16(0x1004, 0x4E71); cpu.put16(0x1006, 0x4E71);
        cpu.put16(0x3000, model == moira::Model::M68EC040 ? 0x4E71 : 0xF518);
        cpu.put16(0x3002, 0x4E73); cpu.put16(0x3004, 0x4E71);
        cpu.put32(0x8000, 0x99AABBCC); cpu.put32(0x8008, 0xAABBCCDD);
        cpu.reset(); cpu.setISP(0x70000); cpu.setUSP(0x60000);
        cpu.setSR(uint16_t((super ? 0x2000 : 0) | 0x1F));
        cpu.setPomCache040(true); cpu.setCACR(0x80008000);
        if (paged) {
            cpu.put32(0x4020, 0x4202); cpu.put32(0x4200, 0x4402); cpu.put32(0x4400, 0x8039);
            cpu.setITT0(0x0000C000); cpu.setDTT0(0x0000C040);
            cpu.setURP040(0x4000); cpu.setSRP040(0x4000); cpu.setTC040(page8k ? 0xC000 : 0x8000);
        } else cpu.setDTT0(0x0000C020);
        check(cpu.pomJitWriteData(la, 4, 0x11223344) && cpu.pomJitWriteData(la + 8, 4, 0xDEADBEEF),
              "post-push fault: prepare two dirty longwords");
        if (!paged) cpu.setDTT0(0x0000C000); // stack writes stay WT; MOVES/TAS are implicitly NC
        cpu.setA(0, la); cpu.setD(0, 0xFACE0000); cpu.setSFC(fc);
        cpu.failRead = 0x8000 + 2 * beat;
        std::vector<CoherenceCpu::Transfer> bus;
        for (unsigned off = 0; off < 16; off += 2) {
            const uint32_t value = off < 4 ? 0x11223344 : off >= 8 && off < 12 ? 0xDEADBEEF : 0;
            bus.push_back({0x8000 + off, (value >> (off % 4 ? 0 : 16)) & 0xFFFF, 2, true});
        }
        if (bytes == 1) bus.push_back({0x8000, 0x11, 1, false});
        else {
            bus.push_back({0x8000, 0x1122, 2, false});
            if (beat) bus.push_back({0x8002, 0x3344, 2, false});
        }
        cpu.recording = true; cpu.execute(); cpu.recording = false;
        auto word = [&](uint32_t a) { return uint16_t(cpu.mem[a] << 8 | cpu.mem[a + 1]); };
        auto lng = [&](uint32_t a) { return uint32_t(word(a)) << 16 | word(a + 2); };
        const uint32_t sp = cpu.getSP();
        const uint16_t ssw = uint16_t(fc | (bytes == 1 ? 0x20 : bytes == 2 ? 0x40 : 0) | (tas ? 0x200 : 0x100));
        caseContext("  post-push fault model=%u map=%u fc=%u op=%u beat=%u\n", unsigned(model), mapping, fc, op, beat);
        check(cpu.transfers == bus && cpu.busErrors == 1 && lng(0x8000) == 0x11223344 &&
              lng(0x8008) == 0xDEADBEEF && !cpu.pomCache040Data().lookup(0x8000),
              "post-push fault: push completes before the operand read fails");
        check(cpu.getPC0() == 0x3000 && !cpu.isHalted() && sp == 0x70000 - 60 &&
              word(sp + 6) == 0x7008 && lng(sp + 2) == 0x1000 && word(sp + 12) == ssw &&
              lng(sp + 20) == la && word(sp + 14) == 0 && word(sp + 16) == 0 &&
              cpu.getD(0) == 0xFACE0000 && cpu.getCCR() == 0x1F,
              "post-push fault: original logical read size/FC survives, no stale push writeback");
        if (cpu.getPC0() == 0x3000) {
            cpu.failRead = ~0u; cpu.execute(); cpu.execute(); cpu.execute();
            const uint32_t value = 0x11223344 >> (32 - bytes * 8);
            const uint32_t mask = bytes == 4 ? ~0u : (1u << (bytes * 8)) - 1;
            check(cpu.getPC0() == (tas ? 0x1002 : 0x1004) && !cpu.isHalted() &&
                  (tas ? cpu.mem[0x8000] == 0x91 : (cpu.getD(0) & mask) == value),
                  "post-push fault: repaired RAM retry completes the original instruction");
        }
        ++cases;
    }
    std::printf("post-push read faults: %u executed sequences\n", cases);
}


void testReadFillFaults() {
    // [B] MC68040UM-1993 §§4.6.1/7.6.1 and §§8.4.6.2/8.4.6.4:
    // fault only demanded longwords; an aborted line is not cached. Dirty
    // victims survive an aborted replacement read without being pushed.
    unsigned cases = 0;
    for (auto model : {moira::Model::M68EC040, moira::Model::M68LC040, moira::Model::M68040})
    for (unsigned mapping = 0; mapping < (model == moira::Model::M68EC040 ? 1u : 3u); ++mapping)
    for (bool super : {false, true})
    for (unsigned size = 0; size < 3; ++size)
    for (unsigned off : {0u, 3u, 12u, 15u})
    for (unsigned victim = 0; victim < 3; ++victim) // absent, clean, dirty
    for (int fault = -1; fault < 12; ++fault) {
        const unsigned bytes = 1u << size;
        const bool crossing = off + bytes > 16, paged = mapping != 0, page8k = mapping == 2;
        if (fault >= 8 && !crossing) continue;
        CoherenceCpu cpu(model);
        const uint32_t la = paged ? 0x10000000 : 0x8000;
        const uint32_t address = la + off;
        const uint16_t ops[] = {0x1010, 0x3010, 0x2010}; // MOVE.B/W/L (A0),D0
        cpu.put32(0, 0x70000); cpu.put32(4, 0x1000); cpu.put32(8, 0x3000);
        cpu.put16(0x1000, ops[size]); cpu.put16(0x1002, 0x4E71); cpu.put16(0x1004, 0x4E71);
        cpu.put16(0x3000, model == moira::Model::M68EC040 ? 0x4E71 : 0xF518);
        cpu.put16(0x3002, 0x4E73); cpu.put16(0x3004, 0x4E71);
        for (unsigned i = 0; i < 32; ++i) cpu.mem[0x8000 + i] = uint8_t(0x20 + i);
        for (unsigned way = 0; way < 4; ++way)
            for (unsigned i = 0; i < 16; ++i) cpu.mem[0x9000 + way * 0x400 + i] = uint8_t(0x60 + i + way);
        cpu.reset(); cpu.setPomCache040(true); cpu.setCACR(0x80008000);
        if (paged) {
            cpu.put32(0x4020, 0x4202); cpu.put32(0x4200, 0x4402);
            cpu.put32(0x4400, 0x8019 | ((size + off) & 1 ? 0x20 : 0)); // WT and CB reads
            cpu.setITT0(0x0000C000); cpu.setURP040(0x4000); cpu.setSRP040(0x4000);
            cpu.setTC040(page8k ? 0xC000 : 0x8000);
        }
        cpu.setDTT0(0x0000C020); // transparent CB region for physical victim setup
        uint32_t ignored = 0;
        if (victim) {
            for (unsigned way = 0; way < 4; ++way)
                check(cpu.pomJitReadData(0x9000 + way * 0x400, 4, ignored), "read fill: seed same-set victim");
            if (victim == 2)
                check(cpu.pomJitWriteData(0x9000, 4, 0xD1D2D3D4) &&
                      cpu.pomJitWriteData(0x9008, 4, 0xE1E2E3E4), "read fill: dirty victim remains outside RAM");
        }
        cpu.pomCache040Data().rot = 0;  // pin the victim; the §4.1 counter has its own cell
        cpu.setDTT0(paged ? 0x0000C040 : 0x0000C000); // stack stays uncached/WT
        cpu.setISP(0x70000); cpu.setUSP(0x60000); cpu.setSR(uint16_t((super ? 0x2000 : 0) | 0x1F));
        cpu.setA(0, address); cpu.setD(0, 0xABCD0000);
        const unsigned failedLine = fault >= 8 ? 1 : 0;
        const unsigned failedWord = fault >= 8 ? unsigned(fault - 8) : unsigned(fault);
        if (fault >= 0) cpu.failRead = 0x8000 + failedLine * 16 + failedWord * 2;
        const unsigned start = failedLine ? 0 : off;
        const unsigned length = failedLine ? off + bytes - 16 : std::min(bytes, 16 - off);
        const bool demanded = fault >= 0 && failedWord / 2 >= start / 4 &&
                              failedWord / 2 <= (start + length - 1) / 4;
        std::vector<CoherenceCpu::Transfer> bus;
        bool firstAborted = false;
        for (unsigned line = 0; line < (crossing ? 2u : 1u); ++line) {
            const unsigned first = (line ? 0 : off) / 4;
            bool aborted = false;
            for (unsigned beat = 0; beat < 4 && !aborted; ++beat) {
                const unsigned lw = (first + beat) & 3;
                for (unsigned half = 0; half < 2; ++half) {
                    const uint32_t pa = 0x8000 + line * 16 + lw * 4 + half * 2;
                    bus.push_back({pa, uint32_t(cpu.mem[pa]) << 8 | cpu.mem[pa + 1], 2, false});
                    if (pa == cpu.failRead) { aborted = true; break; }
                }
            }
            if (line == 0) {
                firstAborted = aborted;
                if (!aborted && victim == 2) {
                    for (unsigned off = 0; off < 16; off += 2) {
                        const uint32_t v = off < 4 ? 0xD1D2D3D4 : off >= 8 && off < 12 ? 0xE1E2E3E4 : 0;
                        const uint32_t half = (off >= 4 && off < 8) || off >= 12 ?
                            uint32_t(cpu.mem[0x9000 + off]) << 8 | cpu.mem[0x9001 + off] : (v >> (off % 4 ? 0 : 16)) & 0xFFFF;
                        bus.push_back({0x9000 + off, half, 2, true});
                    }
                }
            }
            if (aborted && demanded) break;
        }
        cpu.recording = true; cpu.execute(); cpu.recording = false;
        caseContext("  read fill model=%u map=%u super=%u size=%u off=%u victim=%u fault=%d demanded=%u\n",
                    unsigned(model), mapping, super, size, off, victim, fault, demanded);
        check(cpu.transfers == bus && cpu.busErrors == (fault < 0 ? 0u : 1u),
              "read fill: wrapped read prefix precedes any successful dirty push");
        const auto* original = cpu.pomCache040Data().lookup(0x9000);
        const auto* fresh = cpu.pomCache040Data().lookup(0x8000);
        bool state = bool(fresh) == !firstAborted &&
                     bool(original) == (firstAborted && victim == 2);
        if (original) {
            state &= original->dirty == 5 && original->data[0] == 0xD1 && original->data[11] == 0xE4;
        }
        auto word = [&](uint32_t a) { return uint16_t(cpu.mem[a] << 8 | cpu.mem[a + 1]); };
        auto lng = [&](uint32_t a) { return uint32_t(word(a)) << 16 | word(a + 2); };
        state &= lng(0x9000) == (victim == 2 && !firstAborted ? 0xD1D2D3D4 : 0x60616263);
        check(state, "read fill: aborted new line invalid; only dirty victim restored, RAM preserved");
        if (demanded) {
            const uint32_t sp = cpu.getSP();
            const uint16_t ssw = uint16_t(0x100 | (size == 0 ? 0x20 : size == 1 ? 0x40 : 0) | (super ? 5 : 1));
            check(!cpu.isHalted() && cpu.getPC0() == 0x3000 && sp == 0x70000 - 60 &&
                  word(sp + 6) == 0x7008 && word(sp + 12) == ssw && lng(sp + 20) == address &&
                  lng(sp + 2) == 0x1000 && word(sp + 14) == 0 && word(sp + 16) == 0 &&
                  cpu.getD(0) == 0xABCD0000 && cpu.getCCR() == 0x1F,
                  "read fill: demanded fault has logical initial address, original size/FC and no writeback");
            if (cpu.getPC0() == 0x3000) {
                cpu.failRead = ~0u; cpu.execute(); cpu.execute(); cpu.execute(); // repair, RTE, retry on RAM
            }
        }
        uint32_t value = 0;
        for (unsigned i = 0; i < bytes; ++i) value = value << 8 | cpu.mem[0x8000 + off + i];
        const uint32_t mask = bytes == 4 ? ~0u : (1u << (8 * bytes)) - 1;
        check(cpu.getPC0() == 0x1002 && !cpu.isHalted() && cpu.getCCR() == 0x10 &&
              cpu.getD(0) == ((0xABCD0000 & ~mask) | value),
              "read fill: unused beat does not fault; demanded fault repairs and retries");
        ++cases;
    }
    std::printf("read-fill faults: %u executed sequences\n", cases);
}


void testWriteAllocateFaults() {
    // [B] MC68040UM-1993 §§4.6.2/8.4.6.2/8.4.6.4 and Table 8-6:
    // read before push; a reported read fault retains the logical operand
    // descriptor and may carry a pending write in WB3, never WB1/WB2.
    // [F] The synchronous model assigns the outstanding MOVE store to WB3
    // and still reports unused write-fill beat errors. Silicon stage/delivery
    // policy needs a captured write-miss fault, not an assertion in this gate.
    unsigned cases = 0;
    for (auto model : {moira::Model::M68EC040, moira::Model::M68LC040, moira::Model::M68040})
    for (unsigned mapping = 0; mapping < (model == moira::Model::M68EC040 ? 1u : 3u); ++mapping)
    for (bool super : {false, true})
    for (unsigned size = 0; size < 3; ++size)
    for (unsigned off : {0u, 3u, 12u, 15u})
    for (unsigned victim = 0; victim < 3; ++victim)
    for (unsigned mode = 0; mode < 3; ++mode) // (A0), (A0)+, -(A0)
    for (int fault = -1; fault < 12; ++fault) {
        const unsigned bytes = 1u << size;
        const bool crossing = off + bytes > 16, paged = mapping != 0;
        if (fault >= 8 && !crossing) continue;
        CoherenceCpu cpu(model);
        const uint32_t address = (paged ? 0x10000000 : 0x8000) + off;
        const uint32_t value = 0xA1B2C3D4, mask = bytes == 4 ? ~0u : (1u << (8 * bytes)) - 1;
        const uint16_t ops[] = {0x1080, 0x3080, 0x2080}; // MOVE.B/W/L D0,<EA>
        cpu.put32(0, 0x70000); cpu.put32(4, 0x1000); cpu.put32(8, 0x3000);
        cpu.put16(0x1000, uint16_t(ops[size] + mode * 0x40));
        cpu.put16(0x1002, 0x4E71); cpu.put16(0x1004, 0x4E71);
        cpu.put16(0x3000, 0x4E73); cpu.put16(0x3002, 0x4E71);
        for (unsigned i = 0; i < 32; ++i) cpu.mem[0x8000 + i] = uint8_t(0x20 + i);
        for (unsigned way = 0; way < 4; ++way)
            for (unsigned i = 0; i < 16; ++i) cpu.mem[0x9000 + way * 0x400 + i] = uint8_t(0x60 + i + way);
        cpu.reset(); cpu.setPomCache040(true); cpu.setCACR(0x80008000);
        if (paged) {
            cpu.put32(0x4020, 0x4202); cpu.put32(0x4200, 0x4402); cpu.put32(0x4400, 0x8039);
            cpu.setITT0(0x0000C040); cpu.setURP040(0x4000); cpu.setSRP040(0x4000);
            cpu.setTC040(mapping == 2 ? 0xC000 : 0x8000);
        }
        cpu.setDTT0(0x0000C020);
        uint32_t ignored = 0;
        if (victim) {
            for (unsigned way = 0; way < 4; ++way)
                check(cpu.pomJitReadData(0x9000 + way * 0x400, 4, ignored), "write fill: seed victim");
            if (victim == 2)
                check(cpu.pomJitWriteData(0x9000, 4, 0xD1D2D3D4) &&
                      cpu.pomJitWriteData(0x9008, 4, 0xE1E2E3E4), "write fill: dirty victim");
        }
        cpu.pomCache040Data().rot = 0;  // pin the victim; the §4.1 counter has its own cell
        cpu.setDTT0(paged ? 0x0000C040 : 0x0000C020);
        cpu.setISP(0x70000); cpu.setUSP(0x60000); cpu.setSR(uint16_t((super ? 0x2000 : 0) | 0x1F));
        cpu.setA(0, address + (mode == 2 ? bytes : 0)); cpu.setD(0, value);
        const unsigned failedLine = fault >= 8 ? 1 : 0;
        if (fault >= 0) cpu.failRead = 0x8000 + failedLine * 16 + unsigned(fault % 8) * 2;
        std::vector<CoherenceCpu::Transfer> bus;
        for (unsigned line = 0; line < (crossing ? 2u : 1u); ++line) {
            bool aborted = false;
            for (unsigned beat = 0; beat < 4 && !aborted; ++beat) {
                const unsigned lw = (((line ? 0 : off) / 4) + beat) & 3;
                for (unsigned half = 0; half < 2; ++half) {
                    const uint32_t pa = 0x8000 + line * 16 + lw * 4 + half * 2;
                    bus.push_back({pa, uint32_t(cpu.mem[pa]) << 8 | cpu.mem[pa + 1], 2, false});
                    if (pa == cpu.failRead) { aborted = true; break; }
                }
            }
            if (aborted) break;
            if (line == 0 && victim == 2) {
                for (unsigned off = 0; off < 16; off += 2) {
                    const uint32_t v = off < 4 ? 0xD1D2D3D4 : off >= 8 && off < 12 ? 0xE1E2E3E4 : 0;
                    const uint32_t half = (off >= 4 && off < 8) || off >= 12 ?
                        uint32_t(cpu.mem[0x9000 + off]) << 8 | cpu.mem[0x9001 + off] : (v >> (off % 4 ? 0 : 16)) & 0xFFFF;
                    bus.push_back({0x9000 + off, half, 2, true});
                }
            }
        }
        cpu.recording = true; cpu.execute(); cpu.recording = false;
        caseContext("  write fill model=%u map=%u super=%u size=%u off=%u victim=%u mode=%u fault=%d\n",
                    unsigned(model), mapping, super, size, off, victim, mode, fault);
        check(cpu.transfers == bus && cpu.busErrors == (fault < 0 ? 0u : 1u),
              "write fill: wrapped read precedes dirty push, failed read does not push");
        const bool firstAborted = fault >= 0 && fault < 8;
        const auto* original = cpu.pomCache040Data().lookup(0x9000);
        const auto* fresh = cpu.pomCache040Data().lookup(0x8000);
        auto peek = [&](uint32_t a) {
            // Flat CB also caches the exception frame; the handler reads
            // architectural memory, while victim checks deliberately read RAM.
            if (a >= 0x6FF00 && a < 0x70000)
                if (const auto* line = cpu.pomCache040Data().lookup(a)) return line->data[a & 15];
            return cpu.mem[a];
        };
        auto word = [&](uint32_t a) { return uint16_t(peek(a) << 8 | peek(a + 1)); };
        auto lng = [&](uint32_t a) { return uint32_t(word(a)) << 16 | word(a + 2); };
        check(bool(fresh) == !firstAborted && bool(original) == (firstAborted && victim == 2) &&
              (!original || (original->dirty == 5 && original->data[0] == 0xD1 && original->data[11] == 0xE4)) &&
              lng(0x9000) == (victim == 2 && !firstAborted ? 0xD1D2D3D4 : 0x60616263),
              "write fill: aborted line invalid, dirty victim restored without changing RAM");
        check(!cpu.isHalted() && cpu.getCCR() == 0x18 && cpu.getD(0) == value &&
              cpu.getA(0) == address + (mode == 1 ? bytes : 0),
              "write fill: store flags and EA update commit once");
        if (fault >= 0) {
            const uint32_t sp = cpu.getSP();
            const uint16_t attr = uint16_t((size == 0 ? 0x20 : size == 1 ? 0x40 : 0) | (super ? 5 : 1));
            check(cpu.getPC0() == 0x3000 && sp == 0x70000 - 60 && word(sp + 6) == 0x7008 &&
                  lng(sp + 2) == 0x1002 && word(sp + 12) == (attr | 0x100) &&
                  word(sp + 14) == (attr | 0x80) && word(sp + 16) == 0 && word(sp + 18) == 0 &&
                  lng(sp + 20) == address && lng(sp + 24) == address && lng(sp + 28) == (value & mask),
                  "write fill: read fault with logical address/original size and register-aligned pending WB3");
            if (cpu.getPC0() == 0x3000) {
                cpu.failRead = ~0u; cpu.transfers.clear(); cpu.recording = true;
                cpu.completeWrite(0x8000 + off, bytes, lng(sp + 28)); // software consumes WB3
                cpu.execute(); cpu.execute(); // RTE, next NOP, never repeat MOVE
                cpu.recording = false;
                check(cpu.getPC0() == 0x1004 && cpu.getCCR() == 0x18 &&
                      cpu.getA(0) == address + (mode == 1 ? bytes : 0) &&
                      cpu.transfers.size() == (bytes == 4 ? 2u : 1u),
                      "write fill: software WB3 completion and RTE do not refill or repeat EA update");
            }
        } else check(cpu.getPC0() == 0x1002, "write fill: successful copyback store advances");
        bool contents = true;
        for (unsigned i = 0; i < 32; ++i) {
            const auto* line = cpu.pomCache040Data().lookup(0x8000 + i);
            const uint8_t actual = line ? line->data[i & 15] : cpu.mem[0x8000 + i];
            const uint8_t expected = i >= off && i < off + bytes ? uint8_t(value >> (8 * (off + bytes - i - 1))) : uint8_t(0x20 + i);
            contents &= actual == expected;
        }
        check(contents, "write fill: completed value and untouched neighbours survive fault/return");
        ++cases;
    }
    std::printf("write-allocate faults: %u executed sequences\n", cases);
}


void testPushFaults() {
    // [B] MC68040UM-1993 §§4.6.2/7.6.1/8.4.6.4-8.4.6.6:
    // one dirty LW pushes a long; two or more push the whole line. A
    // failed push exports physical FA/WB1A and all PD, and does not undo
    // a successful replacement read. CPUSH invalidates even on failure.
    // Pending ordinary stores retain the fork's WB3/final-store policy.
    unsigned cases = 0;
    for (auto model : {moira::Model::M68EC040, moira::Model::M68LC040, moira::Model::M68040})
    for (unsigned mapping = 0; mapping < (model == moira::Model::M68EC040 ? 1u : 3u); ++mapping)
    for (bool super : {false, true})
    for (unsigned kind = 0; kind < 8; ++kind) // CPUSH L/P/A, replacement R/W, NC R/W, TAS
    for (unsigned size = 0; size < ((kind == 4 || kind == 6) ? 3u : 1u); ++size)
    for (unsigned mode = 0; mode < ((kind == 4 || kind == 6) ? 3u : 1u); ++mode)
    for (unsigned dirty : {1u, 2u, 4u, 8u, 5u, 10u, 15u})
    for (int fault = -1; fault < 8; ++fault) {
        if (kind < 3 && !super) continue;
        const bool replacement = kind == 3 || kind == 4;
        const bool store = kind == 4 || kind == 6;
        const bool linePush = (dirty & (dirty - 1)) != 0;
        if (fault >= 0 && !linePush && !(dirty & (1u << (unsigned(fault) / 2)))) continue;
        CoherenceCpu cpu(model);
        const uint32_t address = mapping ? 0x10000000 : 0x8000;
        const uint32_t victim = replacement ? 0x9000 : 0x8000;
        const uint32_t payload = 0x11223344;
        const unsigned bytes = store ? 1u << size : 4;
        const uint32_t mask = bytes == 4 ? ~0u : (1u << (8 * bytes)) - 1;
        auto updated = [&](uint32_t original) {
            return bytes == 4 ? payload : (original & ((1u << (8 * (4 - bytes))) - 1)) | ((payload & mask) << (8 * (4 - bytes)));
        };
        const uint16_t ops[] = {0xF468, 0xF470, 0xF478, 0x2010, 0x2080, 0x2010, 0x2080, 0x4AD0};
        cpu.put32(0, 0x70000); cpu.put32(4, 0x1000); cpu.put32(8, 0x3000);
        const uint16_t stores[] = {0x1080, 0x3080, 0x2080};
        cpu.put16(0x1000, uint16_t((store ? stores[size] : ops[kind]) + mode * 0x40));
        cpu.put16(0x1002, 0x4E71); cpu.put16(0x1004, 0x4E71);
        cpu.put16(0x3000, 0x4E73); cpu.put16(0x3002, 0x4E71);
        for (unsigned i = 0; i < 16; ++i) cpu.mem[0x8000 + i] = uint8_t(0x20 + i);
        for (unsigned way = 0; way < 4; ++way)
            for (unsigned i = 0; i < 16; ++i) cpu.mem[0x9000 + way * 0x400 + i] = uint8_t(0x60 + i + way);
        cpu.reset(); cpu.setPomCache040(true); cpu.setCACR(0x80008000);
        if (mapping) {
            cpu.put32(0x4020, 0x4202); cpu.put32(0x4200, 0x4402);
            cpu.put32(0x4400, 0x8039); cpu.setITT0(0x0000C040);
            cpu.setURP040(0x4000); cpu.setSRP040(0x4000); cpu.setTC040(mapping == 2 ? 0xC000 : 0x8000);
        }
        cpu.setDTT0(0x0000C020);
        uint32_t ignored = 0;
        for (unsigned way = 0; way < (replacement ? 4u : 1u); ++way)
            check(cpu.pomJitReadData(victim + way * 0x400, 4, ignored), "push fault: seed physical victim");
        for (unsigned lw = 0; lw < 4; ++lw)
            if (dirty & (1u << lw))
                check(cpu.pomJitWriteData(victim + 4 * lw, 4, 0xA0B0C000 + lw), "push fault: dirty selected longword");
        uint32_t pd[4];
        const auto* resident = cpu.pomCache040Data().lookup(victim);
        for (unsigned lw = 0; lw < 4; ++lw) {
            const unsigned o = 4 * lw;
            pd[lw] = uint32_t(resident->data[o]) << 24 | uint32_t(resident->data[o + 1]) << 16 |
                     uint32_t(resident->data[o + 2]) << 8 | resident->data[o + 3];
        }
        cpu.pomCache040Data().rot = 0;  // pin the victim; the §4.1 counter has its own cell
        if (mapping) {
            cpu.setDTT0(0x0000C040);
            cpu.put32(0x4400, 0x8019 | ((kind == 4) ? 0x20 : kind >= 5 ? 0x40 : 0));
        } else cpu.setDTT0(kind == 4 ? 0x0000C020 : kind >= 5 ? 0x0000C040 : 0x0000C000);
        cpu.setISP(0x70000); cpu.setUSP(0x60000); cpu.setSR(uint16_t((super ? 0x2000 : 0) | 0x1F));
        cpu.setA(0, kind < 3 ? victim : address + (mode == 2 ? bytes : 0)); cpu.setD(0, payload);
        const uint32_t first = linePush ? victim : victim + 4 * (dirty == 1 ? 0 : dirty == 2 ? 1 : dirty == 4 ? 2 : 3);
        if (fault >= 0) { cpu.failWrite = victim + 2 * unsigned(fault); cpu.failBytes = 2; }
        std::vector<CoherenceCpu::Transfer> bus;
        if (replacement)
            for (unsigned o = 0; o < 16; o += 2) bus.push_back({0x8000 + o, uint32_t(cpu.mem[0x8000 + o]) << 8 | cpu.mem[0x8001 + o], 2, false});
        for (unsigned o = 0; o < 16; o += 2) {
            if (!linePush && !(dirty & (1u << (o / 4)))) continue;
            bus.push_back({victim + o, (pd[o / 4] >> (o % 4 ? 0 : 16)) & 0xFFFF, 2, true});
            if (victim + o == cpu.failWrite) break;
        }
        if (fault < 0 && kind >= 5) {
            if (kind == 7) {
                bus.push_back({0x8000, pd[0] >> 24, 1, false});
                bus.push_back({0x8000, (pd[0] >> 24) | 0x80, 1, true});
            } else {
                const uint32_t value = store ? payload & mask : pd[0];
                if (bytes < 4) bus.push_back({0x8000, value, bytes, store});
                else { bus.push_back({0x8000, value >> 16, 2, store}); bus.push_back({0x8002, value & 0xFFFF, 2, store}); }
            }
        }
        cpu.recording = true; cpu.execute(); cpu.recording = false;
        caseContext("  push fault model=%u map=%u super=%u kind=%u size=%u mode=%u dirty=%x fault=%d\n",
                    unsigned(model), mapping, super, kind, size, mode, dirty, fault);
        check(cpu.transfers == bus && cpu.busErrors == (fault < 0 ? 0u : 1u), "push fault: exact read/push prefix, longword versus full-line width");
        check(!cpu.pomCache040Data().lookup(victim) &&
              (!replacement || cpu.pomCache040Data().lookup(0x8000)),
              "push fault: displaced victim absent; completed replacement line retained");
        auto peek = [&](uint32_t a) {
            if (const auto* l = cpu.pomCache040Data().lookup(a)) return l->data[a & 15];
            return cpu.mem[a];
        };
        auto word = [&](uint32_t a) { return uint16_t(peek(a) << 8 | peek(a + 1)); };
        auto lng = [&](uint32_t a) { return uint32_t(word(a)) << 16 | word(a + 2); };
        if (fault >= 0) {
            const uint32_t sp = cpu.getSP();
            const uint16_t ssw = uint16_t((linePush ? 0x60 : 0) | (kind == 7 ? 0x200 : 0));
            const uint16_t wb3 = uint16_t(store ? 0x80 | (super ? 5 : 1) | (size == 0 ? 0x20 : size == 1 ? 0x40 : 0) : 0);
            bool frame = cpu.getPC0() == 0x3000 && !cpu.isHalted() && sp == 0x70000 - 60 &&
                         word(sp + 6) == 0x7008 && word(sp + 12) == ssw && word(sp + 18) == 0 &&
                         lng(sp + 20) == first && lng(sp + 40) == first &&
                         word(sp + 14) == wb3 && word(sp + 16) == 0 && lng(sp + 2) == (store ? 0x1002 : 0x1000);
            if (store) frame &= lng(sp + 24) == address && lng(sp + 28) == (payload & mask);
            for (unsigned lw = 0; lw < 4; ++lw) frame &= lng(sp + 44 + 4 * lw) == pd[lw];
            check(frame, "push fault: physical FA/WB1A, TM=0, full PD and independent logical WB3");
            if (cpu.getPC0() == 0x3000) {
                cpu.failWrite = ~0u;
                // Complete the actual stacked PD first, then any WB3 through
                // architectural memory so a retained replacement stays coherent.
                for (unsigned lw = 0; lw < 4; ++lw) cpu.completeWrite(victim + 4 * lw, 4, lng(sp + 44 + 4 * lw));
                if (store) check(cpu.pomJitWriteData(address, bytes, lng(sp + 28)), "push fault: handler completes logical WB3");
                cpu.transfers.clear(); cpu.recording = true;
                cpu.execute(); // RTE
                cpu.execute(); // retry CPUSH/read/TAS, or next NOP after pending store
                cpu.recording = false;
                check((store || replacement || kind < 3) ? cpu.transfers.empty() : cpu.transfers.size() == 2,
                      "push fault: RTE never repeats a completed push or replacement fill");
            }
        }
        const uint32_t next = fault >= 0 && store ? 0x1004 : 0x1002;
        const uint32_t expectedD = kind == 3 ? 0x20212223 : kind == 5 ? pd[0] : payload;
        const uint8_t expectedCcr = kind < 3 ? 0x1F : kind == 7 ? uint8_t(0x10 | ((pd[0] & 0x80000000) ? 8 : 0) | ((pd[0] & 0xFF000000) ? 0 : 4)) :
                                    kind == 5 ? uint8_t(0x10 | ((pd[0] & 0x80000000) ? 8 : 0)) : 0x10;
        check(cpu.getPC0() == next && !cpu.isHalted() && cpu.getD(0) == expectedD && cpu.getCCR() == expectedCcr &&
              cpu.getA(0) == (kind < 3 ? victim : address + (store && mode == 1 ? bytes : 0)),
              "push fault: software completion/RTE preserves data, flags and a single EA update");
        bool data = true;
        for (unsigned lw = 0; lw < 4; ++lw) {
            uint32_t expected = pd[lw];
            if (!replacement && lw == 0 && store) expected = updated(pd[0]);
            if (kind == 7 && lw == 0) expected |= 0x80000000;
            data &= lng(victim + 4 * lw) == expected;
        }
        if (store) data &= lng(0x8000) == updated(replacement ? 0x20212223 : pd[0]);
        check(data, "push fault: pushed image, pending store and clean neighbours survive");
        ++cases;
    }
    std::printf("push faults: %u executed sequences\n", cases);
}


void testDescriptorFaults() {
    // [B] MC68040UM-1993 §3.5, Figure 3-10 and §§8.4.6.2/8.4.6.4:
    // descriptor errors create an R=0 entry and fault the logical operand
    // with ATC, not the physical descriptor cycle. Prior U writes follow
    // the next descriptor read. PTEST reports B without an exception.
    unsigned cases = 0;
    for (auto model : {moira::Model::M68LC040, moira::Model::M68040})
    for (bool page8k : {false, true})
    for (unsigned cache = 0; cache < 3; ++cache)
    for (bool super : {false, true})
    for (unsigned access = 0; access < 7; ++access) // data R/W, code, PTEST R/W data/code
    for (unsigned size = 0; size < (access < 2 ? 3u : 1u); ++size)
    for (bool later : {false, true})
    for (bool indirect : {false, true})
    for (unsigned kind = 0; kind < 9; ++kind) // root/ptr/page/indirect R, root/ptr U, leaf U/M, success
    for (unsigned half = 0; half < 2; ++half)
    for (bool warmTarget : {false, true}) {
        if (warmTarget && !(kind == 7 && access == 1)) continue;
        const bool probe = access >= 3, code = access == 2;
        const bool write = access == 1 || access == 4 || access == 6;
        const bool data = access != 2 && access < 5;
        const bool ordinaryStore = access == 1;
        const unsigned bytes = access < 2 ? 1u << size : code ? 2 : 4;
        if ((later && (code || (!probe && bytes == 1))) || (kind == 3 && !indirect) ||
            (kind == 7 && !write) || (kind == 8 && half)) continue;
        CoherenceCpu cpu(model);
        const uint32_t la = 0x10000000, pageBytes = page8k ? 8192 : 4096;
        const unsigned gap = bytes == 2 ? 1 : 2;
        const uint32_t address = later ? (probe ? la + pageBytes + 0x100 : la + pageBytes - gap) : la + 0x100;
        const uint32_t target = later ? 1 : 0, targetLogical = later ? la + pageBytes : address;
        const uint32_t targetPa = target ? 0xA000 : 0x8000;
        const uint32_t leaf = (indirect ? 0x4800 : 0x4400) + 4 * target;
        const uint32_t cm = cache == 2 ? (ordinaryStore ? 0x20 : 0) : 0x40;
        const uint32_t attributes = cm | (kind == 7 ? 9 : 1);
        const uint32_t payload = 0xA1B2C3D4, mask = bytes == 4 ? ~0u : (1u << (8 * bytes)) - 1;
        const uint16_t reads[] = {0x1018, 0x3018, 0x2018}, writes[] = {0x10C0, 0x30C0, 0x20C0};
        cpu.put32(0, 0x70000); cpu.put32(4, 0x1000); cpu.put32(8, 0x3000);
        cpu.put16(0x1000, probe ? (write ? 0xF548 : 0xF568) : ordinaryStore ? writes[size] : reads[size]);
        cpu.put16(0x1002, 0x4E71); cpu.put16(0x1004, 0x4E71);
        cpu.put16(0x1010, 0x2211); cpu.put16(0x1012, 0x4E71); // MOVE.L (A1),D1, physical control
        cpu.put16(0x1020, 0xF518); cpu.put16(0x1022, 0x4E71);
        cpu.put16(0x3000, 0xF518); cpu.put16(0x3002, 0x4E73); cpu.put16(0x3004, 0x4E71);
        for (unsigned i = 0; i < 16; ++i) {
            cpu.mem[0x8100 + i] = uint8_t(0x80 + i);
            cpu.mem[0x8000 + pageBytes - 16 + i] = uint8_t(0x80 + i);
            cpu.mem[0xA000 + i] = uint8_t(0x90 + i);
            cpu.mem[0xA100 + i] = uint8_t(0x90 + i);
        }
        if (!data) { cpu.put16(0x8100, 0x4E71); cpu.put16(0x8102, 0x4E71); }
        cpu.reset(); cpu.setPomCache040(cache != 0); cpu.setCACR(cache ? 0x80008000 : 0);
        cpu.setDTT0(0x0000C040); cpu.setITT0(0x0000C040);
        cpu.setURP040(0x4000); cpu.setSRP040(0x4000); cpu.setTC040(page8k ? 0xC000 : 0x8000);
        auto map = [&](bool warm) {
            cpu.put32(0x4020, warm ? 0x420A : 0x4202); cpu.put32(0x4200, warm ? 0x440A : 0x4402);
            for (unsigned page = 0; page < 2; ++page) {
                const uint32_t pa = page ? 0xA000 : 0x8000;
                const uint32_t status = !warm && page == target ? attributes : 0x19 | cm;
                cpu.put32(0x4400 + 4 * page, indirect ? 0x4802 + 4 * page : pa | status);
                if (indirect) cpu.put32(0x4800 + 4 * page, pa | status);
            }
        };
        map(true); cpu.setISP(0x70000); cpu.setUSP(0x60000); cpu.setSR(super ? 0x201F : 0x1F);
        if (later) {
            uint32_t ignored = 0; uint16_t word = 0;
            check(data ? cpu.pomJitReadData(la + pageBytes - gap, 1, ignored) : cpu.pomJitReadProg(la + 0x100, word),
                  "descriptor fault: warm only the first page/domain");
        }
        map(false);
        if (warmTarget) {
            uint32_t ignored = 0;
            check(cpu.pomJitReadData(targetLogical, 1, ignored), "descriptor fault: seed a resident unmodified target ATC entry");
            map(false); // force the write hit to re-walk for M, with U history still pending
        }
        cpu.setSR(uint16_t(((super || probe) ? 0x2000 : 0) | 0x1F));
        cpu.setA(0, address); cpu.setD(0, payload); cpu.setDFC((super ? 4 : 0) | (data ? 1 : 2));
        cpu.setMMUSR040(0x12345678);
        if (code) { cpu.setPC(address); cpu.setPC0(address); }
        const uint32_t initialSp = cpu.getSP();
        const uint32_t locations[] = {0x4020, 0x4200, 0x4400 + 4 * target, 0x4800 + 4 * target,
                                      0x4020, 0x4200, leaf, leaf};
        if (kind < 4) cpu.failRead = locations[kind] + 2 * half;
        else if (kind < 8) { cpu.failWrite = locations[kind] + 2 * half; cpu.failBytes = 2; }
        std::vector<CoherenceCpu::Transfer> expected;
        std::vector<uint8_t> table(cpu.mem.begin() + 0x4000, cpu.mem.begin() + 0x5000);
        bool stopped = false;
        auto cycle = [&](uint32_t a, uint32_t value, bool store) {
            if (stopped) return;
            for (unsigned h = 0; h < 2; ++h) {
                const uint32_t at = a + 2 * h, v = (value >> (h ? 0 : 16)) & 0xFFFF;
                expected.push_back({at, v, 2, store});
                if (at == (store ? cpu.failWrite : cpu.failRead)) { stopped = true; break; }
                if (store) { table[at - 0x4000] = uint8_t(v >> 8); table[at + 1 - 0x4000] = uint8_t(v); }
            }
        };
        cycle(0x4020, 0x4202, false);
        cycle(0x4200, 0x4402, false);
        cycle(0x4020, 0x420A, true);
        cycle(0x4400 + 4 * target, indirect ? 0x4802 + 4 * target : targetPa | attributes, false);
        cycle(0x4200, 0x440A, true);
        if (indirect) cycle(leaf, targetPa | attributes, false);
        cycle(leaf, targetPa | cm | (write ? 0x19 : 9), true);
        cpu.recordTables = cpu.recording = true; cpu.execute(); cpu.recording = false;
        caseContext("  descriptor fault model=%u 8k=%u cache=%u super=%u access=%u bytes=%u later=%u indirect=%u kind=%u half=%u warmM=%u\n",
                    unsigned(model), page8k, cache, super, access, bytes, later, indirect, kind, half, warmTarget);
        std::vector<CoherenceCpu::Transfer> actual;
        for (auto t : cpu.transfers) if (t.address >= 0x4000 && t.address < 0x5000) actual.push_back(t);
        check(actual == expected && cpu.busErrors == unsigned(kind != 8), "descriptor fault: read next descriptor before prior U write, exact abort prefix");
        check(std::vector<uint8_t>(cpu.mem.begin() + 0x4000, cpu.mem.begin() + 0x5000) == table,
              "descriptor fault: only completed U/M halfwords change table history");
        auto peek = [&](uint32_t a) {
            if (const auto* line = cpu.pomCache040Data().lookup(a)) return line->data[a & 15];
            return cpu.mem[a];
        };
        auto word = [&](uint32_t a) { return uint16_t(peek(a) << 8 | peek(a + 1)); };
        auto lng = [&](uint32_t a) { return uint32_t(word(a)) << 16 | word(a + 2); };
        if (probe) {
            check(cpu.getPC0() == 0x1002 && cpu.getSP() == initialSp && !cpu.isHalted() &&
                  cpu.getMMUSR040() == (kind == 8 ? targetPa | cm | (write ? 0x11 : 1) : 0x800) &&
                  cpu.getCCR() == 0x1F && cpu.getA(0) == address,
                  "descriptor fault: PTEST reports only B and creates no exception");
        } else if (kind != 8) {
            const uint32_t sp = cpu.getSP();
            const unsigned sz = code || bytes == 2 ? 0x40 : bytes == 1 ? 0x20 : 0;
            const uint16_t attr = uint16_t(sz | (super ? 4 : 0) | (data ? 1 : 2));
            check(cpu.getPC0() == 0x3000 && sp == 0x70000 - 60 && !cpu.isHalted() &&
                  word(sp + 6) == 0x7008 && word(sp + 12) == (attr | (write ? 0 : 0x100) | 0x400 | (later ? 0x800 : 0)) &&
                  lng(sp + 20) == address && lng(sp + 2) == (code ? address : ordinaryStore ? 0x1002 : 0x1000) &&
                  word(sp + 14) == (ordinaryStore ? attr | 0x80 : 0) && word(sp + 16) == 0 && word(sp + 18) == 0 &&
                  (!ordinaryStore || (lng(sp + 24) == address && lng(sp + 28) == (payload & mask))) &&
                  cpu.getMMUSR040() == 0x12345678,
                  "descriptor fault: original logical operand/code frame, ATC/MA, size/FC and WB3");
            check(cpu.getD(0) == payload && cpu.getA(0) == address + (ordinaryStore ? bytes : 0) &&
                  cpu.getCCR() == (ordinaryStore ? 0x18 : 0x1F), "descriptor fault: operand restart versus final-store commit");
        }
        if (kind != 8) {
            bool noTarget = true;
            for (auto t : cpu.transfers) if (t.address >= targetPa + (later && !probe ? 0 : 0x100) &&
                                            t.address < targetPa + (later && !probe ? 16 : 0x110)) noTarget = false;
            check(noTarget, "descriptor fault: aborted walk cannot touch the target operand/code");
            const uint32_t savedSp = cpu.getSP(), savedPc = cpu.getPC0();
            const uint16_t savedSr = cpu.getSR();
            cpu.failRead = cpu.failWrite = ~0u; map(false);
            cpu.setSR(super ? 0x201F : 0x1F); cpu.transfers.clear(); cpu.recording = true;
            uint32_t ignored = 0; uint16_t instruction = 0;
            const bool resident = data ? cpu.pomJitReadData(targetLogical, 1, ignored) : cpu.pomJitReadProg(targetLogical, instruction);
            cpu.recording = false; cpu.setSR(savedSr);
            check(!resident && cpu.transfers.empty() && cpu.busErrors == 1,
                  "descriptor fault: repaired descriptor stays negatively cached until PFLUSH");
            // A TTR-served /BERR before another walk proves the table-search
            // context was cleared on the exceptional exit. Discard only this
            // diagnostic's nested frame; the original frame still drives RTE.
            const uint32_t savedA1 = cpu.getA(1), savedD1 = cpu.getD(1);
            cpu.setA(1, 0xB000); cpu.failRead = 0xB000; cpu.setPC(0x1010); cpu.setPC0(0x1010);
            cpu.recording = true; cpu.execute(); cpu.recording = false;
            const uint32_t sp = cpu.getSP();
            check(cpu.getPC0() == 0x3000 && !cpu.isHalted() && sp == savedSp - 60 &&
                  word(sp + 12) == 0x105 && lng(sp + 20) == 0xB000 && cpu.busErrors == 2,
                  "descriptor fault: following physical /BERR is not a leaked table-search fault");
            cpu.failRead = ~0u; cpu.setSP(savedSp); cpu.setSR(savedSr); cpu.setPC(savedPc); cpu.setPC0(savedPc);
            cpu.setA(1, savedA1); cpu.setD(1, savedD1);
            if (probe) {
                cpu.setPC(0x1020); cpu.setPC0(0x1020); cpu.execute();
                cpu.setPC(0x1000); cpu.setPC0(0x1000); cpu.execute();
                check(cpu.getMMUSR040() == (targetPa | cm | (write ? 0x11 : 1)) && cpu.getSP() == initialSp &&
                      cpu.getPC0() == 0x1002 && !cpu.isHalted(), "descriptor fault: PFLUSH/PTEST repair succeeds");
            } else {
                const uint32_t wb3 = lng(savedSp + 28);
                cpu.execute(); // handler PFLUSHA
                if (ordinaryStore) check(cpu.pomJitWriteData(address, bytes, wb3), "descriptor fault: software completes pending logical store");
                cpu.transfers.clear(); cpu.recording = true;
                cpu.execute(); cpu.execute(); // RTE, then retry read/code or next NOP
                cpu.recording = false;
                if (ordinaryStore) check(cpu.transfers.empty(), "descriptor fault: RTE cannot repeat completed store");
            }
        }
        auto physical = [&](unsigned i) {
            return later && !probe ? (i < gap ? 0x8000 + pageBytes - gap + i : 0xA000 + i - gap) : targetPa + 0x100 + i;
        };
        if (!probe && !code) {
            uint32_t input = 0;
            for (unsigned i = 0; i < bytes; ++i) {
                const uint8_t original = later ? uint8_t(i < gap ? 0x90 - gap + i : 0x90 + i - gap) : uint8_t(0x80 + i);
                input = input << 8 | original;
                check(peek(physical(i)) == (ordinaryStore ? uint8_t(payload >> (8 * (bytes - i - 1))) : original),
                      "descriptor fault: operand bytes survive repair/completion");
            }
            check(cpu.getPC0() == (kind != 8 && ordinaryStore ? 0x1004 : 0x1002) && !cpu.isHalted() &&
                  cpu.getSP() == initialSp && cpu.getCCR() == 0x18 && cpu.getA(0) == address + bytes &&
                  cpu.getD(0) == (ordinaryStore ? payload : (payload & ~mask) | input),
                  "descriptor fault: result, flags and a single postincrement after RTE");
        } else if (code) check(cpu.getPC0() == address + 2 && cpu.getSP() == initialSp && !cpu.isHalted() &&
                               cpu.getCCR() == 0x1F, "descriptor fault: repaired instruction fetch resumes NOP");
        ++cases;
    }
    std::printf("descriptor faults: %u executed sequences\n", cases);
}


void testPageSplitFaults() {
    // [B] MC68040UM-1993 §§8.4.6.2/8.4.6.4: MA marks a second-page
    // ATC fault only; physical /BERR still retains the initial logical FA.
    // The WB3/last-store and restart policy is the existing fork model.
    unsigned cases = 0;
    for (auto model : {moira::Model::M68EC040, moira::Model::M68LC040, moira::Model::M68040})
    for (unsigned mapping = 0; mapping < (model == moira::Model::M68EC040 ? 1u : 3u); ++mapping)
    for (unsigned cache = 0; cache < 3; ++cache) // off, active NC, active WT reads / CB writes
    for (bool super : {false, true})
    for (bool write : {false, true})
    for (unsigned bytes : {2u, 4u})
    for (unsigned gap = 1; gap <= bytes; ++gap)
    for (unsigned mode = 0; mode < 3; ++mode) {
        const bool paged = mapping != 0, cross = gap < bytes, fill = cache == 2;
        const unsigned pageBytes = mapping == 2 ? 8192 : 4096;
        const uint32_t la = paged ? 0x10000000 : 0x8000;
        const uint32_t address = la + pageBytes - gap;
        const uint32_t second = paged ? 0xA000 : 0x8000 + pageBytes;
        auto physical = [&](unsigned offset) {
            return offset < gap ? 0x8000 + pageBytes - gap + offset : second + offset - gap;
        };
        struct Request { unsigned offset, bytes; };
        std::vector<Request> requests;
        if (cross && (bytes == 2 || (address & 1))) {
            for (unsigned i = 0; i < bytes; ++i) requests.push_back({i, 1});
        } else if (cross) { requests.push_back({0, 2}); requests.push_back({2, 2}); }
        else requests.push_back({0, bytes});
        struct Fault { unsigned kind, page, prefix, busBytes; uint32_t pa; };
        std::vector<Fault> faults = {{0, 0, bytes, 0, ~0u}};
        if (fill) {
            for (unsigned page = 0; page < (cross ? 2u : 1u); ++page)
            for (unsigned half = 0; half < 2; ++half)
                faults.push_back({1, page, page ? gap : 0, 2, (physical(page ? gap : 0) & ~3u) + 2 * half});
        } else {
            for (auto r : requests)
            for (unsigned i = 0; i < r.bytes; i += 2) {
                const unsigned offset = r.offset + i;
                faults.push_back({1, unsigned(offset >= gap), offset, std::min(r.bytes, 2u), physical(offset)});
            }
        }
        if (paged)
            for (unsigned kind = 2; kind < 5; ++kind) {
                if ((kind == 3 && !write) || (kind == 4 && super)) continue;
                for (unsigned page = 0; page < (cross ? 2u : 1u); ++page)
                    faults.push_back({kind, page, page ? gap : 0, 0, ~0u});
            }
        for (auto fault : faults) {
            CoherenceCpu cpu(model);
            const uint32_t payload = 0xA1B2C3D4, mask = bytes == 4 ? ~0u : 0xFFFF;
            const uint16_t op = write ? (bytes == 2 ? 0x3080 : 0x2080) : (bytes == 2 ? 0x3010 : 0x2010);
            cpu.put32(0, 0x70000); cpu.put32(4, 0x1000); cpu.put32(8, 0x3000);
            cpu.put16(0x1000, uint16_t(op + mode * (write ? 0x40 : 8)));
            cpu.put16(0x1002, 0x4E71); cpu.put16(0x1004, 0x4E71);
            cpu.put16(0x3000, model == moira::Model::M68EC040 ? 0x4E71 : 0xF518);
            cpu.put16(0x3002, 0x4E73); cpu.put16(0x3004, 0x4E71);
            for (unsigned o = 0; o < 16; ++o) {
                cpu.mem[0x8000 + pageBytes - 16 + o] = uint8_t(0x80 + o);
                cpu.mem[second + o] = uint8_t(0x90 + o);
            }
            uint32_t input = 0;
            for (unsigned i = 0; i < bytes; ++i) input = input << 8 | cpu.mem[physical(i)];
            cpu.reset(); cpu.setPomCache040(cache != 0); cpu.setCACR(cache ? 0x80008000 : 0);
            const uint32_t cm = fill ? (write ? 0x20 : 0) : 0x40;
            auto map = [&](unsigned page, unsigned kind) {
                const uint32_t pa = page ? second : 0x8000;
                const uint32_t attributes = kind == 2 ? 0 : 0x19 | cm | (kind == 3 ? 4 : kind == 4 ? 0x80 : 0);
                cpu.put32(0x4400 + 4 * page, pa | attributes);
            };
            if (paged) {
                cpu.put32(0x4020, 0x4202); cpu.put32(0x4200, 0x4402);
                map(0, fault.page == 0 ? fault.kind : 0); map(1, fault.page == 1 ? fault.kind : 0);
                cpu.setDTT0(0x0000C040); cpu.setURP040(0x4000); cpu.setSRP040(0x4000);
                cpu.setTC040(mapping == 2 ? 0xC000 : 0x8000);
            } else cpu.setDTT0(0x0000C000 | cm);
            cpu.setITT0(0x0000C040); // instruction lookahead cannot consume a data trap
            cpu.setISP(0x70000); cpu.setUSP(0x60000); cpu.setSR(uint16_t((super ? 0x2000 : 0) | 0x1F));
            cpu.setA(0, address + (mode == 2 ? bytes : 0)); cpu.setD(0, payload);
            if (fault.kind == 1) {
                if (!write || fill) cpu.failRead = fault.pa;
                else { cpu.failWrite = fault.pa; cpu.failBytes = fault.busBytes; }
            }
            std::vector<CoherenceCpu::Transfer> bus;
            std::vector<uint32_t> resident;
            bool stopped = false;
            for (auto r : requests) {
                if (stopped) break;
                const unsigned page = r.offset >= gap;
                if (fault.kind >= 2 && page == fault.page) break;
                const uint32_t pa = physical(r.offset);
                if (fill) {
                    const uint32_t base = pa & ~15u;
                    bool hit = false; for (auto known : resident) hit |= known == base;
                    if (hit) continue;
                    for (unsigned beat = 0; beat < 4 && !stopped; ++beat) {
                        const uint32_t a = base + (((pa & 12) + 4 * beat) & 15);
                        for (unsigned half = 0; half < 2; ++half) {
                            const uint32_t word = a + 2 * half;
                            bus.push_back({word, uint32_t(cpu.mem[word]) << 8 | cpu.mem[word + 1], 2, false});
                            if (word == cpu.failRead) { stopped = true; break; }
                        }
                    }
                    if (!stopped) resident.push_back(base);
                } else {
                    for (unsigned i = 0; i < r.bytes; i += 2) {
                        const unsigned n = std::min(r.bytes, 2u), offset = r.offset + i;
                        const uint32_t a = pa + i;
                        uint32_t value = 0;
                        for (unsigned k = 0; k < n; ++k)
                            value = value << 8 | (write ? uint8_t(payload >> (8 * (bytes - offset - k - 1))) : cpu.mem[a + k]);
                        bus.push_back({a, value, n, write});
                        if (fault.kind == 1 && a == fault.pa) { stopped = true; break; }
                    }
                }
            }
            cpu.recording = true; cpu.execute(); cpu.recording = false;
            caseContext("  page split model=%u map=%u cache=%u super=%u write=%u bytes=%u gap=%u mode=%u kind=%u page=%u pa=%x\n",
                        unsigned(model), mapping, cache, super, write, bytes, gap, mode, fault.kind, fault.page, fault.pa);
            check(cpu.transfers == bus && cpu.busErrors == unsigned(fault.kind == 1), "page split: exact transferred prefix and trap origin");
            auto peek = [&](uint32_t a) {
                if (const auto* line = cpu.pomCache040Data().lookup(a)) return line->data[a & 15];
                return cpu.mem[a];
            };
            auto word = [&](uint32_t a) { return uint16_t(peek(a) << 8 | peek(a + 1)); };
            auto lng = [&](uint32_t a) { return uint32_t(word(a)) << 16 | word(a + 2); };
            if (fault.kind) {
                const uint32_t sp = cpu.getSP();
                const uint16_t attr = uint16_t((bytes == 2 ? 0x40 : 0) | (super ? 5 : 1));
                const bool atc = fault.kind >= 2;
                const bool readFault = !write || (!atc && fill);
                const uint16_t ssw = uint16_t(attr | (readFault ? 0x100 : 0) | (atc ? 0x400 : 0) | (atc && fault.page ? 0x800 : 0));
                check(cpu.getPC0() == 0x3000 && !cpu.isHalted() && sp == 0x70000 - 60 &&
                      word(sp + 6) == 0x7008 && word(sp + 12) == ssw && lng(sp + 20) == address &&
                      lng(sp + 2) == (write ? 0x1002 : 0x1000) && word(sp + 16) == 0 && word(sp + 18) == 0 &&
                      word(sp + 14) == (write ? attr | 0x80 : 0) &&
                      (!write || (lng(sp + 24) == address && lng(sp + 28) == (payload & mask))),
                      "page split: MA only for second-page ATC, initial logical FA and original size/WB3");
                check(cpu.getD(0) == payload && cpu.getCCR() == (write ? 0x18 : 0x1F) &&
                      cpu.getA(0) == address + (write ? (mode == 1 ? bytes : 0) : (mode == 2 ? bytes : 0)),
                      "page split: fault commits final-store state or restores read state");
                bool prefix = true;
                for (unsigned i = 0; i < bytes; ++i) {
                    const uint8_t original = uint8_t(input >> (8 * (bytes - i - 1)));
                    prefix &= peek(physical(i)) == (write && i < fault.prefix ? uint8_t(payload >> (8 * (bytes - i - 1))) : original);
                }
                check(prefix, "page split: only completed store fragments are visible before repair");
                if (cpu.getPC0() == 0x3000) {
                    cpu.failRead = cpu.failWrite = ~0u;
                    if (paged) { map(0, 0); map(1, 0); }
                    cpu.execute(); // PFLUSHA (NOP on EC040), then complete pending WB3
                    if (write) check(cpu.pomJitWriteData(address, bytes, lng(sp + 28)), "page split: handler completes full pending store");
                    cpu.transfers.clear(); cpu.recording = true;
                    cpu.execute(); cpu.execute(); // RTE, next NOP or retried read
                    cpu.recording = false;
                    if (write) check(cpu.transfers.empty(), "page split: RTE does not repeat a completed store");
                }
            }
            const uint32_t expected = write ? payload : (payload & ~mask) | input;
            check(cpu.getPC0() == (fault.kind && write ? 0x1004 : 0x1002) && !cpu.isHalted() &&
                  cpu.getD(0) == expected && cpu.getCCR() == 0x18 &&
                  cpu.getA(0) == address + (mode == 1 ? bytes : 0),
                  "page split: completion/RTE preserves result, flags and one EA update");
            bool data = true;
            for (unsigned i = 0; i < bytes; ++i)
                data &= peek(physical(i)) == uint8_t((write ? payload : input) >> (8 * (bytes - i - 1)));
            for (unsigned page = 0; page < 2; ++page)
            for (unsigned i = 0; i < 16; ++i) {
                const uint32_t pa = page ? second + i : 0x8000 + pageBytes - 16 + i;
                bool operand = false;
                for (unsigned k = 0; k < bytes; ++k) operand |= pa == physical(k);
                if (!operand) data &= peek(pa) == uint8_t((page ? 0x90 : 0x80) + i);
            }
            check(data, "page split: operand bytes and neighbours survive repair and completion");
            ++cases;
        }
    }
    std::printf("page-split faults: %u executed sequences\n", cases);
}


void testInstructionFillFaults() {
    unsigned cases = 0;
    for (auto model : {moira::Model::M68EC040, moira::Model::M68LC040, moira::Model::M68040})
    for (unsigned mapping = 0; mapping < (model == moira::Model::M68EC040 ? 1u : 3u); ++mapping)
    for (bool super : {false, true})
    for (unsigned off : {0u, 2u, 8u, 10u})
    for (bool demanded : {false, true})
    for (unsigned half : {0u, 2u}) {
        CoherenceCpu cpu(model);
        const bool paged = mapping != 0, page8k = mapping == 2;
        const uint32_t la = paged ? 0x10000000 : 0x8000, pc = la + off;
        cpu.put32(0, 0x70000); cpu.put32(4, 0x1000); cpu.put32(8, 0x3000);
        cpu.put16(0x1000, 0x4E71); cpu.put16(0x1002, 0x4E71);
        cpu.put16(0x3000, model == moira::Model::M68EC040 ? 0x4E71 : 0xF518);
        cpu.put16(0x3002, 0x4E73); cpu.put16(0x3004, 0x4E71);
        for (unsigned i = 0; i < 32; i += 2) cpu.put16(0x8000 + i, 0x4E71);
        cpu.reset(); cpu.setPomCache040(true); cpu.setCACR(0x80008000);
        cpu.setISP(0x70000); cpu.setUSP(0x60000); cpu.setSR(uint16_t((super ? 0x2000 : 0) | 0x1F));
        cpu.setITT0(0x0000C000); cpu.setDTT0(0x0000C040);
        if (paged) {
            cpu.put32(0x4020, 0x4202); cpu.put32(0x4200, 0x4402); cpu.put32(0x4400, 0x8019);
            cpu.setURP040(0x4000); cpu.setSRP040(0x4000); cpu.setTC040(page8k ? 0xC000 : 0x8000);
        }
        cpu.setPC(pc); cpu.setPC0(pc);
        const unsigned faultLw = demanded ? off / 4 : off < 8 ? 2 : 0;
        cpu.failRead = 0x8000 + faultLw * 4 + half;
        std::vector<CoherenceCpu::Transfer> bus;
        for (unsigned word = 0; word < (demanded ? 1u : 2u); ++word) {
            const unsigned first = (off + word * 2) / 4;
            bool aborted = false;
            for (unsigned beat = 0; beat < 4 && !aborted; ++beat) {
                for (unsigned h : {0u, 2u}) {
                    const uint32_t a = 0x8000 + ((first + beat) & 3) * 4 + h;
                    bus.push_back({a, 0x4E71, 2, false});
                    if (a == cpu.failRead) { aborted = true; break; }
                }
            }
        }
        cpu.recording = true; cpu.execute(); cpu.recording = false;
        caseContext("  instruction fill model=%u map=%u super=%u off=%u demanded=%u half=%u\n",
                    unsigned(model), mapping, super, off, demanded, half);
        check(cpu.transfers == bus && cpu.busErrors == (demanded ? 1u : 2u) &&
              !cpu.pomCache040Inst().lookup(0x8000),
              "instruction fill: aborted line invalid, requested words refetch through fresh probes");
        if (demanded) {
            const uint32_t sp = cpu.getSP();
            auto word = [&](uint32_t a) { return uint16_t(cpu.mem[a] << 8 | cpu.mem[a + 1]); };
            auto lng = [&](uint32_t a) { return uint32_t(word(a)) << 16 | word(a + 2); };
            check(cpu.getPC0() == 0x3000 && !cpu.isHalted() && sp == 0x70000 - 60 &&
                  word(sp + 6) == 0x7008 && word(sp + 12) == (0x140 | (super ? 6 : 2)) &&
                  lng(sp + 20) == pc && lng(sp + 2) == pc && word(sp + 14) == 0 && word(sp + 16) == 0,
                  "instruction fill: demanded fetch faults as logical program word, no writeback");
            if (cpu.getPC0() == 0x3000) {
                cpu.failRead = ~0u; cpu.execute(); cpu.execute(); cpu.execute();
            }
        }
        check(cpu.getPC0() == pc + 2 && !cpu.isHalted() && cpu.getCCR() == 0x1F,
              "instruction fill: unused beat ignored or repaired demanded fetch resumes NOP");
        ++cases;
    }
    std::printf("instruction-fill faults: %u executed sequences\n", cases);
}

void testDisabledDataCache() {
    for (auto model : {moira::Model::M68EC040, moira::Model::M68LC040,
                       moira::Model::M68040}) {
        CoherenceCpu cpu(model);
        cpu.put32(0, 0x70000); cpu.put32(4, 0x1000);
        cpu.put16(0x1000, 0x2010); cpu.put16(0x1002, 0x2010); // MOVE.L (A0),D0 twice
        cpu.put16(0x1004, 0x4E71); cpu.put16(0x1006, 0x4E71);
        cpu.put32(0x8000, 0xAABBCCDD);
        cpu.reset(); cpu.setPomCache040(true); cpu.setCACR(0x80008000);
        cpu.setDTT0(0x0000C020); cpu.setA(0, 0x8000);
        check(cpu.pomJitWriteData(0x8000, 4, 0x11223344), "CACR off: seed dirty data");
        cpu.setCACR(0x8000);                      // IE only, DE disabled
        cpu.recording = true; cpu.execute(); cpu.recording = false;
        const std::vector<CoherenceCpu::Transfer> reads = {
            {0x8000, 0xAABB, 2, false}, {0x8002, 0xCCDD, 2, false}
        };
        const auto* line = cpu.pomCache040Data().lookup(0x8000);
        check(cpu.getD(0) == 0xAABBCCDD && cpu.transfers == reads && line && line->dirty == 1,
              "CACR off: ordinary read bypasses and retains dirty line without pushing");
        cpu.setCACR(0x80008000); cpu.transfers.clear();
        cpu.recording = true; cpu.execute(); cpu.recording = false;
        check(cpu.getD(0) == 0x11223344 && cpu.transfers.empty(),
              "CACR on: retained dirty bytes are visible again without a bus read");
    }
}

// [B] MC68040UM-1993 §4.1: the data cache's two-bit replacement counter
// advances for each half-line accessed during reads, each full line accessed
// during copyback writes and each bus transfer of a write-through write; a
// miss with four valid lines replaces the line it points to (MoiraCache040.h,
// POM68K_VENDOR.md row 85). Each step's delta is 1..3, so rot mod 4 pins it.
void testReplacementCounter() {
    std::printf("part 5 — access-counted replacement (MC68040UM-1993 §4.1)\n");
    Cpu c;
    c.setCACR(0x80000000);                    // DE
    c.setDTT0(0x4000C020);                    // $40xxxxxx copyback
    c.setDTT1(0x5000C000);                    // $50xxxxxx write-through
    auto& dc = c.pomCache040Data();
    struct Step { bool write; uint32_t addr; int bytes; unsigned delta; const char* what; };
    static const Step steps[] = {
        { false, 0x40008000, 4, 1, "read long inside one half-line: +1" },
        { false, 0x40008006, 4, 2, "read long across two half-lines: +2" },
        { false, 0x4000800F, 2, 2, "read word across two lines: +2" },
        { true,  0x40008000, 4, 1, "copyback write inside one line: +1" },
        { true,  0x4000800E, 4, 2, "copyback write across two lines: +2" },
        { true,  0x50008000, 4, 1, "write-through aligned long, one transfer: +1" },
        { true,  0x50008001, 4, 3, "write-through long at +1, byte/word/byte: +3" },
        { true,  0x50008003, 2, 2, "write-through word at +3, two bytes: +2" },
        { false, 0x50008000, 4, 1, "write-through page read: +1" },
    };
    for (const auto& k : steps) {
        const unsigned before = dc.rot;
        uint32_t value = 0;
        const bool ok = k.write ? c.pomJitWriteData(k.addr, k.bytes, 0x5A5A5A5A)
                                : c.pomJitReadData(k.addr, k.bytes, value);
        check(ok && dc.rot == ((before + k.delta) & 3), k.what);
    }

    // Victim choice: a full set replaces the way rot points to, then the
    // missing access counts. Ways 0-3 hold $40009000 + n*$400 (set 0).
    Cpu v;
    v.setCACR(0x80000000);
    v.setDTT0(0x4000C020);
    auto& vc = v.pomCache040Data();
    uint32_t value = 0;
    for (uint32_t way = 0; way < 4; way++)
        v.pomJitReadData(0x40009000 + way * 0x400, 4, value);
    vc.rot = 2;
    check(v.pomJitReadData(0x4000A000, 4, value) &&
          vc.lookup(0x4000A000) && !vc.lookup(0x40009800) &&
          vc.lookup(0x40009000) && vc.lookup(0x40009400) && vc.lookup(0x40009C00) &&
          vc.rot == 3,
          "a full set replaces the way rot points to; the miss then counts");
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--descriptor-faults") {
        testDescriptorFaults();
        std::printf(gFails ? "FAILED (%d)\n" : "PASSED\n", gFails);
        return gFails ? 1 : 0;
    }
    std::printf("cache040_test — M1-M3 architectural cache state "
                "(docs/CACHE_040.md)\n");
    testStruct();
    testCpu();
    testPhysicalOperands();
    testDataPath();
    testImplicitNoncacheable();
    testDisabledDataCache();
    testCas2WriteProtection();
    testCasMismatchTerminalWrite();
    testMove16FaultContext();
    testFaultAfterSuccessfulPush();
    testReadFillFaults();
    testWriteAllocateFaults();
    testPushFaults();
    testPageSplitFaults();
    testDescriptorFaults();
    testInstructionFillFaults();
    testReplacementCounter();
    std::printf(gFails ? "FAILED (%d)\n" : "PASSED\n", gFails);
    return gFails ? 1 : 0;
}
