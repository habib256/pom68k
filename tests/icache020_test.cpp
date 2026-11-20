// POM68K — 68020 on-chip instruction cache gate (upstream Moira's MoiraCache).
// VERHILLE Arnaud — Copyright (C) 2026 — MIT (see LICENSE-MIT)
//
// POM68K's configuration now has the cache too (inventory row 147); this
// gate links `moira_upstream_config` (upstream's settings, cache on), so the
// cache is held under the configuration upstream ships, and runs small hand-assembled programs on a flat bus whose address decides
// the port width (dsack). Each check states where its expectation comes from:
//
//   [B] MC68020UM-1992 (tests/core_manuals.lock, which holds the reviewed
//       locators): § 4.1 (PDF p. 45), § 4.2 and § 4.3.1 (p. 47), § 4.3.2
//       (p. 48), § 5.2.1 (p. 53), § 5.2.5 (p. 70).
//   [D] upstream Moira ce28239 — the program traces (every bus access with
//       its clock) of this file's programs were identical between upstream
//       ce28239 and the fork rebased onto it on 2026-11-20
//       (icache_compare, run on both cores), before the fork routed the
//       expired DBcc's read through the cache (inventory row 152), which
//       removes upstream's R16 at the DBcc's pc+2 and nothing else.
//   [F] cycle totals: the fork's corrected model (inventory row 147), whose
//       anchors are the manual's loops below, not a measurement.
//       Settled by: a 68020 bus capture of these programs.
//
// The gate fails if the cache is compiled out or ignored: a hit must make
// no bus cycle, which a cache-less core (every fetch on the bus) cannot do.

#include "Moira.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <type_traits>
#include <vector>

using namespace moira;

namespace {

constexpr u32 kMemSize = 1 << 20;
constexpr u32 kEnd = 0x0F00;           // BRA.S * : the run stops here
constexpr u32 kStack = 0x3F000;

struct Access {
    bool write; int size; u32 addr; u32 value; i64 clock;
    bool operator==(const Access &) const = default;
};
struct Step {
    u32 pc; i64 clock; u32 d1; std::vector<Access> bus;
    bool operator==(const Step &) const = default;
};

// Port width by address (dsack):
//   $00000-$3FFFF 32 bits, $40000-$7FFFF 16 bits, $80000-$BFFFF 8 bits.
class TestCpu : public Moira {
public:
    std::vector<u8> mem = std::vector<u8>(kMemSize, 0);
    mutable std::vector<Access> bus;

    u8 get8(u32 a) const { return mem[a & (kMemSize - 1)]; }
    void put8(u32 a, u8 v) { mem[a & (kMemSize - 1)] = v; }
    u16 get16(u32 a) const { return u16(get8(a) << 8 | get8(a + 1)); }
    void put16(u32 a, u16 v) { put8(a, u8(v >> 8)); put8(a + 1, u8(v)); }
    void put32(u32 a, u32 v) { put16(a, u16(v >> 16)); put16(a + 2, u16(v)); }

    // Drops the lines and the latch behind the core's back (not through
    // CACR.C, which the gate tests): the save-state control below.
    void dropCache() {
        for (auto &line : iCache.cache) line.valid = false;
        iCache.latch.addr = 0xffffffff;
    }
    // The cycle budget's balance (Moira::syncCp): below zero it is a debt.
    int balance() const { return budget.accumulated; }

private:
    void note(bool w, int s, u32 a, u32 v) const { bus.push_back({w, s, a, v, getClock()}); }
    u8 read8(u32 a) const override { u8 v = get8(a); note(false, 1, a, v); return v; }
    u16 read16(u32 a) const override { u16 v = get16(a); note(false, 2, a, v); return v; }
    u32 read32(u32 a) const override {
        u32 v = u32(get16(a)) << 16 | get16(a + 2); note(false, 4, a, v); return v;
    }
    u16 read16OnReset(u32 a) const override { return get16(a); }
    u16 read16Dasm(u32 a) const override { return get16(a); }
    void write8(u32 a, u8 v) const override {
        note(true, 1, a, v); const_cast<TestCpu *>(this)->put8(a, v);
    }
    void write16(u32 a, u16 v) const override {
        note(true, 2, a, v); const_cast<TestCpu *>(this)->put16(a, v);
    }
    void write32(u32 a, u32 v) const override {
        note(true, 4, a, v); const_cast<TestCpu *>(this)->put32(a, v);
    }
    u8 dsack(u32 a) const override {
        switch ((a & (kMemSize - 1)) >> 18) {
            case 0:  return DSACK_32;
            case 2:  return DSACK_8;
            default: return DSACK_16;
        }
    }
};

// ── A cursor that emits big-endian words; `mark` records phase starts ───
struct Asm {
    TestCpu &c; u32 pc; std::vector<u32> *marks = nullptr;
    Asm &w(u16 v) { c.put16(pc, v); pc += 2; return *this; }
    Asm &l(u32 v) { w(u16(v >> 16)); return w(u16(v)); }
    Asm &mark() { if (marks) marks->push_back(pc); return *this; }

    Asm &nop()                    { return w(0x4E71); }
    Asm &moveqD0(int8_t n)        { return w(u16(0x7000 | u8(n))); }
    Asm &moveLImmD0(u32 v)        { return w(0x203C).l(v); }
    Asm &setCacr(u32 v)           { return moveLImmD0(v).w(0x4E7B).w(0x0002); }  // MOVEC D0,CACR
    Asm &setCaar(u32 v)           { return moveLImmD0(v).w(0x4E7B).w(0x0802); }  // MOVEC D0,CAAR
    Asm &addqL1D1()               { return w(0x5281); }
    Asm &dbraD0(u32 target)       { w(0x51C8); return w(u16(target - pc)); }
    Asm &jsr(u32 a)               { mark(); return w(0x4EB9).l(a); }
    Asm &rts()                    { return w(0x4E75); }
    Asm &end()                    { mark(); return w(0x4EF9).l(kEnd); }        // JMP kEnd
    // D0 = n-1; loop: ADDQ.L #1,D1 ; NOP ; DBRA D0,loop
    Asm &loop(int n) { moveqD0(int8_t(n - 1)); u32 top = pc; addqL1D1(); nop(); return dbraD0(top); }
};

struct Program {
    const char *name;
    void (*build)(TestCpu &, std::vector<u32> &marks);
    u32 entry;
};

// A loop of 6 passes; its body occupies the lines $xx0C and $xx10.
void loopOn(TestCpu &c, std::vector<u32> &m)   { Asm a{c, 0x01000, &m}; a.setCacr(1).loop(6).end(); }
void loopOff(TestCpu &c, std::vector<u32> &m)  { Asm a{c, 0x01000, &m}; a.setCacr(0).loop(6).end(); }
void loop16(TestCpu &c, std::vector<u32> &m)   { Asm a{c, 0x41000, &m}; a.setCacr(1).loop(6).end(); }
void loop8(TestCpu &c, std::vector<u32> &m)    { Asm a{c, 0x81000, &m}; a.setCacr(1).loop(6).end(); }

// The subroutine used by the phase programs: lines $2000, $2004, $2008.
void sub(TestCpu &c, u32 at) { Asm s{c, at}; s.loop(3).rts(); }

// Phases: fill (E) | E off | E on again.
void enable(TestCpu &c, std::vector<u32> &m)
{
    sub(c, 0x2000);
    Asm a{c, 0x1000, &m};
    a.setCacr(1).jsr(0x2000).setCacr(0).jsr(0x2000).setCacr(1).jsr(0x2000).end();
}
// Phases: fill $2000 (E) | $3000 under E|F | $2000 under E|F.
void freeze(TestCpu &c, std::vector<u32> &m)
{
    sub(c, 0x2000); sub(c, 0x3000);
    Asm a{c, 0x1000, &m};
    a.setCacr(1).jsr(0x2000).setCacr(3).jsr(0x3000).jsr(0x2000).end();
}
// Phases: fill | CE with CAAR=$2004, then call again.
void clearEntry(TestCpu &c, std::vector<u32> &m)
{
    sub(c, 0x2000);
    Asm a{c, 0x1000, &m};
    a.setCacr(1).jsr(0x2000).setCaar(0x2004).setCacr(1 | 4).jsr(0x2000).end();
}
// Phases: fill | C, then call again.
void clearAll(TestCpu &c, std::vector<u32> &m)
{
    sub(c, 0x2000);
    Asm a{c, 0x1000, &m};
    a.setCacr(1).jsr(0x2000).setCacr(1 | 8).jsr(0x2000).end();
}
// Phases: fill in supervisor mode | user call 1 | user call 2 | TRAP #0.
void userTag(TestCpu &c, std::vector<u32> &m)
{
    c.put32(32 * 4, kEnd);                       // TRAP #0 vector
    sub(c, 0x2000);
    Asm a{c, 0x1000, &m};
    a.setCacr(1).jsr(0x2000)
     .w(0x027C).w(0xDFFF)                        // ANDI #$DFFF,SR: user mode
     .w(0x4FF9).l(0x30000)                       // LEA $30000,A7
     .jsr(0x2000).jsr(0x2000).mark().w(0x4E40);  // TRAP #0
}
// The loop body rewrites its own ADDQ.L #1,D1 (at $2002) into ADDQ.L #2,D1.
// Phases: call 1 | call 2 | C, call 3.
void selfModifying(TestCpu &c, std::vector<u32> &m)
{
    Asm s{c, 0x2000};
    s.moveqD0(3).addqL1D1().w(0x33FC).w(0x5481).l(0x2002).dbraD0(0x2002).rts();
    Asm a{c, 0x1000, &m};
    a.setCacr(1).jsr(0x2000).jsr(0x2000).setCacr(1 | 8).jsr(0x2000).end();
}

const Program kPrograms[] = {
    { "loop-on",        loopOn,        0x01000 },
    { "loop-off",       loopOff,       0x01000 },
    { "loop-port16",    loop16,        0x41000 },
    { "loop-port8",     loop8,         0x81000 },
    { "enable",         enable,        0x01000 },
    { "freeze",         freeze,        0x01000 },
    { "clear-entry",    clearEntry,    0x01000 },
    { "clear-all",      clearAll,      0x01000 },
    { "user-tag",       userTag,       0x01000 },
    { "self-modifying", selfModifying, 0x01000 },
};

std::vector<u32> boot(TestCpu &c, Model model, const Program &p)
{
    std::vector<u32> marks;
    c.setModel(model);
    p.build(c, marks);
    c.put16(kEnd, 0x60FE);                       // BRA.S *
    c.put32(0, kStack); c.put32(4, p.entry);
    c.reset();
    for (int i = 0; i < 8; i++) c.setD(i, 0);
    return marks;
}

void run(TestCpu &c, int limit, std::vector<Step> &out)
{
    for (int n = 0; c.getPC() != kEnd && n < limit; n++) {
        const u32 pc = c.getPC();
        c.bus.clear();
        c.execute();
        out.push_back({pc, c.getClock(), c.getD(1), c.bus});
    }
}

struct Trace {
    std::vector<Step> steps;
    std::vector<u32> marks;
    i64 cycles = 0;
    u32 d1 = 0;
    u16 at2002 = 0;                    // the word self-modifying rewrites
    // Steps of phase k: from the k-th mark's instruction to the next one's.
    std::vector<Access> phase(size_t k) const {
        std::vector<Access> r; bool in = false; size_t seen = 0;
        for (auto &s : steps) {
            if (seen < marks.size() && s.pc == marks[seen]) { in = (seen == k); seen++; }
            if (in) r.insert(r.end(), s.bus.begin(), s.bus.end());
        }
        return r;
    }
    std::vector<Access> all() const {
        std::vector<Access> r;
        for (auto &s : steps) r.insert(r.end(), s.bus.begin(), s.bus.end());
        return r;
    }
};

Trace execute(Model model, const Program &p)
{
    auto c = std::make_unique<TestCpu>();
    Trace t;
    t.marks = boot(*c, model, p);
    run(*c, 100000, t.steps);
    t.cycles = c->getClock();
    t.d1 = c->getD(1);
    t.at2002 = c->get16(0x2002);
    return t;
}

// Reads of `size` bytes in [lo, hi) (size 0: any).
int reads(const std::vector<Access> &bus, u32 lo, u32 hi, int size = 0)
{
    int n = 0;
    for (auto &a : bus)
        if (!a.write && a.addr >= lo && a.addr < hi && (size == 0 || a.size == size)) n++;
    return n;
}

int failures = 0;
const char *modelName = "";
#define CHECK(c, ...) do { if (!(c)) { \
    std::printf("  FAIL %s %s:%d: ", modelName, __FILE__, __LINE__); \
    std::printf(__VA_ARGS__); std::printf("\n"); failures++; } } while (0)

const Program &prog(const char *name)
{
    for (auto &p : kPrograms) if (!std::strcmp(p.name, name)) return p;
    std::printf("no program %s\n", name); std::exit(2);
}

void checkModel(Model model)
{
    // ── Hits make no bus cycle; a miss fills a line with one long cycle ──
    // [B] § 4.1: a hit ends the cycle on chip; a miss fetches from memory and
    //     fills the entry, both words, with a long-word-aligned bus cycle.
    // [B] § 5.2.5: on a hit the external cycle aborts and AS is not asserted.
    // The loop body is two lines ($100C, $1010); six passes, two fills.
    // This is also the compiled-out detector: without the cache every pass
    // fetches its lines from the bus (12 reads, as with E clear below).
    const Trace on = execute(model, prog("loop-on"));
    CHECK(reads(on.all(), 0x100C, 0x1014) == 2 &&
          reads(on.all(), 0x100C, 0x1014, 4) == 2,
          "loop-on: %d reads of the loop lines, expected 2 R32 fills "
          "(cache compiled out or ignored?)", reads(on.all(), 0x100C, 0x1014));
    CHECK(on.d1 == 6, "loop-on: D1=%X, expected 6", on.d1);

    // [B] § 4.3.1 E: E clear disables the cache. Every pass fetches both
    // lines: 6 x 2 long reads. [D] the count of 12 (the latch serves the
    // second word of a longword, so one read per line per pass).
    const Trace off = execute(model, prog("loop-off"));
    CHECK(reads(off.all(), 0x100C, 0x1014, 4) == 12,
          "loop-off: %d R32 of the loop lines, expected 12",
          reads(off.all(), 0x100C, 0x1014, 4));

    // ── Fill split by port width ─────────────────────────────────────────
    // [B] § 5.2.1: a long-word read from a 16-bit port takes two cycles, from
    // an 8-bit port four. [B] § 4.1: the fill is a long-word read. After
    // the fills the loop hits: two lines, 2 x 2 or 2 x 4 cycles in all.
    const Trace p16 = execute(model, prog("loop-port16"));
    const auto b16 = p16.all();
    CHECK(reads(b16, 0x4100C, 0x41014) == 4 && reads(b16, 0x4100C, 0x41014, 2) == 4,
          "loop-port16: %d reads of the loop lines, expected 4 R16",
          reads(b16, 0x4100C, 0x41014));
    CHECK(reads(b16, 0x4100C, 0x4100D, 2) == 1 && reads(b16, 0x4100E, 0x4100F, 2) == 1,
          "loop-port16: line $4100C not filled as R16 $4100C + R16 $4100E");
    const Trace p8 = execute(model, prog("loop-port8"));
    const auto b8 = p8.all();
    CHECK(reads(b8, 0x8100C, 0x81014) == 8 && reads(b8, 0x8100C, 0x81014, 1) == 8,
          "loop-port8: %d reads of the loop lines, expected 8 R8",
          reads(b8, 0x8100C, 0x81014));
    for (u32 a = 0x8100C; a < 0x81010; a++)
        CHECK(reads(b8, a, a + 1, 1) == 1, "loop-port8: byte $%05X not read once", a);
    CHECK(p16.d1 == 6 && p8.d1 == 6, "narrow-port loops: D1=%X/%X", p16.d1, p8.d1);

    // Phase counts below are every read of the subroutine's lines, of any
    // size. [B] § 8.2.15: DBcc with the count expired, cache case 10(0/0/0),
    // makes no instruction access when the code is cached. Upstream ce28239
    // read the word after the displacement as an R16 straight from the bus
    // (execDbcc), past the cache, on every pass; the fork routes it through
    // the cache (inventory row 152), so a hit phase reads nothing at all.
    auto fills = [](const Trace &t, size_t k, u32 base) {
        return reads(t.phase(k), base, base + 12);
    };

    // ── E clear bypasses valid lines; set again, they hit ───────────────
    // [B] § 4.3.1 E: "Disabling the instruction cache does not flush the
    // entries. If the cache is reenabled, the previously valid entries
    // remain valid and may be used." [D] 10 R32 with E clear: the three
    // lines on each of the 3 passes, and $2008 again for the RTS.
    const Trace en = execute(model, prog("enable"));
    CHECK(fills(en, 0, 0x2000) == 3, "enable: fill phase %d R32, expected 3", fills(en, 0, 0x2000));
    CHECK(fills(en, 1, 0x2000) == 10, "enable: E-off phase %d R32, expected 10 (valid lines bypassed)",
          fills(en, 1, 0x2000));
    CHECK(fills(en, 2, 0x2000) == 0, "enable: E-on-again phase %d R32, expected 0",
          fills(en, 2, 0x2000));

    // ── F freezes allocation, hits continue ──────────────────────────────
    // [B] § 4.3.1 F: with F set a miss does not replace the entry; § 4.1:
    // the valid bit is set on a miss "unless the F-bit in the CACR is set".
    // $3000 under F misses on each of its 3 passes ([D] 10 R32, as with E
    // clear above);
    // $2000, filled before, still hits.
    const Trace fr = execute(model, prog("freeze"));
    CHECK(fills(fr, 0, 0x2000) == 3, "freeze: fill phase %d R32", fills(fr, 0, 0x2000));
    CHECK(fills(fr, 1, 0x3000) == 10, "freeze: frozen $3000 phase %d R32, expected 10",
          fills(fr, 1, 0x3000));
    CHECK(fills(fr, 2, 0x2000) == 0, "freeze: $2000 under F %d R32, expected 0 (hits)",
          fills(fr, 2, 0x2000));

    // ── CE + CAAR clears one line ────────────────────────────────────────
    // [B] § 4.3.1 CE, § 4.3.2: the CAAR index (A7-A2) names the one entry
    // cleared. CAAR = $2004: only that line is fetched again.
    const Trace ce = execute(model, prog("clear-entry"));
    const auto ce1 = ce.phase(1);
    CHECK(reads(ce1, 0x2000, 0x200C, 4) == 1 && reads(ce1, 0x2004, 0x2005, 4) == 1,
          "clear-entry: second call %d R32, expected only $2004",
          reads(ce1, 0x2000, 0x200C, 4));

    // ── C clears all ─────────────────────────────────────────────────────
    // [B] § 4.3.1 C: all valid bits cleared; every line is fetched again.
    const Trace ca = execute(model, prog("clear-all"));
    CHECK(fills(ca, 1, 0x2000) == 3, "clear-all: second call %d R32, expected 3",
          fills(ca, 1, 0x2000));

    // ── User and supervisor lines do not alias ───────────────────────────
    // [B] § 4.1: the tag is A31-A8 and FC2. Lines filled in supervisor mode
    // miss in user mode; the user fill then hits.
    const Trace ut = execute(model, prog("user-tag"));
    CHECK(fills(ut, 0, 0x2000) == 3 && fills(ut, 1, 0x2000) == 3 && fills(ut, 2, 0x2000) == 0,
          "user-tag: R32 per call %d/%d/%d, expected 3/3/0",
          fills(ut, 0, 0x2000), fills(ut, 1, 0x2000), fills(ut, 2, 0x2000));

    // ── Self-modifying code runs the stale opcode until C ────────────────
    // [B] § 4.1 NOTE: data accesses are not cached; entries are cleared only
    // by C, CE or reset (§ 4.2, § 4.3.1). The rewritten ADDQ (#1 -> #2) is
    // served from the stale line in calls 1 and 2; after C, call 3 fetches it.
    // D1 = 4 + 4 + 8.
    const Trace sm = execute(model, prog("self-modifying"));
    u32 d1AfterCall[3] = {0, 0, 0};
    {
        size_t seen = 0;
        for (auto &s : sm.steps) {
            if (seen < sm.marks.size() && s.pc == sm.marks[seen]) seen++;
            if (seen >= 1 && seen <= 3) d1AfterCall[seen - 1] = s.d1;
        }
    }
    CHECK(d1AfterCall[0] == 4 && d1AfterCall[1] == 8 && sm.d1 == 16,
          "self-modifying: D1 after calls %X/%X/%X, expected 4/8/10",
          d1AfterCall[0], d1AfterCall[1], sm.d1);
    CHECK(sm.at2002 == 0x5481, "self-modifying: memory not patched ($%04X)", sm.at2002);

    // ── Cycle totals ─────────────────────────────────────────────────────
    // The fork's corrected fetch charges (readInstr, inventory row 147): a
    // hit or a latch word adds 0 to the cache-case tables, a bus cycle +2
    // from a 32-bit port and +6 from a narrower one. Upstream ce28239
    // charged 38/58/58/58/128/116/90/82/122/114 here, below the manual's
    // cache case, by discounting every hit 2 more (checkManualLoops holds
    // the [B] anchors). [F] for silicon: no capture of these programs.
    struct { const Trace *t; const char *n; i64 cycles; } totals[] = {
        { &on, "loop-on", 100 }, { &off, "loop-off", 120 },
        { &p16, "loop-port16", 120 }, { &p8, "loop-port8", 120 },
        { &en, "enable", 260 }, { &fr, "freeze", 238 },
        { &ce, "clear-entry", 190 }, { &ca, "clear-all", 172 },
        { &ut, "user-tag", 240 },
        { &sm, "self-modifying", 332 },
    };
    for (auto &x : totals)
        CHECK(x.t->cycles == x.cycles, "%s: %lld cycles, expected %lld", x.n,
              (long long)x.t->cycles, (long long)x.cycles);
}

// ── Manual loops: warm on the cache case, cache off towards the worst ──
// Eight copies of one instruction and DBF D7,top, 200 iterations, from the
// 32-bit port (the conditions of the MC68020UM-1992 §8.2 tables). These are
// the loops of the 2026-11-20 finding on upstream ce28239, where a warm
// loop was charged below the manual's best case (8 x NOP + DBF: 4.5 clocks
// per iteration against best 19) because the tables are the cache case and
// a hit was discounted 2 more (inventory row 147).
// [B] § 8.2.16 NOP 2/2/3, § 8.2.6 MOVE Rn,(An) 3/4/5, § 8.2.15 DBcc count
//     not expired 3/6/9 (best/cache/worst): per iteration
//     8 x NOP + DBF 19/22/33, 8 x MOVE.L D0,(A0) + DBF 27/38/49, DBF 3/6/9.
// Warm, every fetch hits: exactly the cache case. CACR = 0, every longword
// is a bus cycle at +2: 32, 48 and 8, one clock under the worst case
// (33, 49, 9; the manual's figures are per instruction, the fill serves
// two words). [F] for silicon: no capture of these loops.
struct LoopRun { double warm; long long cold; int minBalance; };

LoopRun runLoop(Model model, u16 op, int n, u32 cacr)
{
    auto c = std::make_unique<TestCpu>();
    c->setModel(model);
    Asm a{*c, 0x1000};
    a.moveLImmD0(cacr).w(0x4E7B).w(0x0002);      // MOVEC D0,CACR
    a.w(0x41F9).l(0x20000);                      // LEA $20000,A0
    a.w(0x3E3C).w(199);                          // MOVE.W #199,D7
    a.nop().nop();
    const u32 top = a.pc;
    for (int i = 0; i < n; i++) a.w(op);
    a.w(0x51CF); a.w(u16(top - a.pc));           // DBF D7,top
    a.w(0x60FE);                                 // BRA.S *
    c->put32(0, kStack); c->put32(4, 0x1000);
    c->reset();
    while (c->getPC() != top) c->execute();
    long long start[200]; int it = 0, minBalance = c->balance();
    while (it < 200) {
        if (c->getPC() == top) start[it++] = c->getClock();
        if (it == 200) break;
        c->execute();
        if (c->balance() < minBalance) minBalance = c->balance();
    }
    return { double(start[199] - start[99]) / 100.0, start[1] - start[0], minBalance };
}

void checkManualLoops(Model model)
{
    struct { const char *name; u16 op; int n; int cache; long long off; } loops[] = {
        { "8 x NOP + DBF",          0x4E71, 8, 22, 32 },
        { "8 x MOVE.L D0,(A0) + DBF", 0x2080, 8, 38, 48 },
        { "DBF alone",              0x4E71, 0,  6, 8 },
    };
    for (auto &l : loops) {
        const LoopRun on = runLoop(model, l.op, l.n, 1);
        const LoopRun off = runLoop(model, l.op, l.n, 0);
        CHECK(on.warm == double(l.cache), "%s: warm %.2f clocks per iteration, "
              "manual cache case %d", l.name, on.warm, l.cache);
        CHECK(off.warm == double(l.off), "%s: CACR=0 %.2f clocks per iteration, "
              "expected %lld", l.name, off.warm, l.off);
        // The budget never borrows: no balance below zero over 200 passes.
        CHECK(on.minBalance >= 0 && off.minBalance >= 0, "%s: the cycle budget "
              "went into debt (%d / %d)", l.name, on.minBalance, off.minBalance);
    }
}

// ── Save state: lines, latch and cycle budget travel (MoiraState.h) ─────
struct Writer {
    static constexpr bool loading = false;
    std::vector<u8> &buf;
    template <class... T> void operator()(T &...v) { (put(v), ...); }
    template <class T> void put(T &v) {
        static_assert(std::is_trivially_copyable_v<T>);
        auto p = reinterpret_cast<const u8 *>(&v); buf.insert(buf.end(), p, p + sizeof v);
    }
};
struct Reader {
    static constexpr bool loading = true;
    const std::vector<u8> &buf; size_t pos = 0;
    template <class... T> void operator()(T &...v) { (get(v), ...); }
    template <class T> void get(T &v) {
        static_assert(std::is_trivially_copyable_v<T>);
        std::memcpy(&v, buf.data() + pos, sizeof v); pos += sizeof v;
    }
};

// Runs k steps, saves, restores into a fresh core holding a copy of the
// memory and finishes there. `dropCache` clears the restored lines first.
std::vector<Step> splitRun(Model model, const Program &p, int k, bool dropCache)
{
    auto a = std::make_unique<TestCpu>();
    boot(*a, model, p);
    std::vector<Step> out;
    run(*a, k, out);
    std::vector<u8> state;
    Writer w{state}; a->pomVisitState(w);
    auto b = std::make_unique<TestCpu>();
    b->setModel(model);
    b->mem = a->mem;
    Reader r{state}; b->pomVisitState(r);
    if (r.pos != state.size()) { std::printf("  FAIL save state: %zu of %zu bytes read\n", r.pos, state.size()); failures++; }
    if (dropCache) b->dropCache();
    run(*b, 100000, out);
    return out;
}

void checkSaveState(Model model)
{
    for (auto &p : kPrograms) {
        const Trace whole = execute(model, p);
        const int n = int(whole.steps.size());
        for (int k : { n / 3, n / 2, (2 * n) / 3 })
            CHECK(splitRun(model, p, k, false) == whole.steps,
                  "save state: %s split at %d of %d differs", p.name, k, n);
    }
    // Control: the round trip is not vacuous. Split self-modifying in call 2
    // (stale line cached, memory already patched) and drop the restored
    // lines: the fresh fetch sees ADDQ #2 and D1 ends elsewhere.
    const Program &sm = prog("self-modifying");
    const Trace whole = execute(model, sm);
    size_t k = 0, seen = 0;
    for (; k < whole.steps.size(); k++) {
        if (seen < whole.marks.size() && whole.steps[k].pc == whole.marks[seen]) seen++;
        if (seen == 2 && whole.steps[k].pc == 0x2002) break;   // call 2, first ADDQ
    }
    const auto dropped = splitRun(model, sm, int(k), true);
    CHECK(!dropped.empty() && dropped.back().d1 != whole.d1,
          "save-state control: dropping the restored lines changed nothing (D1=%X)",
          whole.d1);
}

// --dump: every program's trace, for comparing cores (icache_compare).
void dump()
{
    for (Model m : { Model::M68020, Model::M68EC020 }) {
        for (auto &p : kPrograms) {
            const Trace t = execute(m, p);
            std::printf("=== %s %s: %zu instructions, %lld cycles, D1=%X\n",
                        m == Model::M68020 ? "68020" : "68EC020", p.name,
                        t.steps.size(), (long long)t.cycles, t.d1);
            for (auto &s : t.steps) {
                std::printf("%06X -> %lld\n", s.pc, (long long)s.clock);
                for (auto &a : s.bus)
                    std::printf("  %c%d %06X =%08X @%lld\n", a.write ? 'W' : 'R', a.size * 8,
                                a.addr, a.value, (long long)a.clock);
            }
        }
    }
}

} // namespace

int main(int argc, char **argv)
{
    if (argc > 1 && !std::strcmp(argv[1], "--dump")) { dump(); return 0; }
    if (!MOIRA_EMULATE_ICACHE) {
        std::printf("[icache020_test] FAIL: built with MOIRA_EMULATE_ICACHE false; "
                    "link moira_upstream_config\n");
        return 1;
    }
    const std::pair<Model, const char *> models[] = {
        { Model::M68020, "68020" }, { Model::M68EC020, "68EC020" } };
    for (auto [m, name] : models) {
        modelName = name;
        checkModel(m);
        checkManualLoops(m);
        checkSaveState(m);
    }
    if (failures) {
        std::printf("[icache020_test] FAIL: %d check(s)\n", failures);
        return 1;
    }
    std::printf("[icache020_test] OK: 68020/EC020 i-cache hit/miss, port split, "
                "E/F/CE/C, FC2 tag, stale code, cycles, save state\n");
    return 0;
}
