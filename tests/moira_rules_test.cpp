// POM68K — bare-Moira regression pins for CPU rules no corpus vector reaches.
//
// The three vector corpora (sst68000 / sst68030 / sst68040) are random, and a
// random 32-bit operand never lands on the one value a rule special-cases.
// This gate holds those cells by hand, on a flat RAM with no Macintosh in
// sight, so the rule travels with the core (TODO.md § Moteur › Socle). Each
// section names the rule, its source, and why no corpus sees it.
//
// 1. DIVS.W of $80000000 (extern/moira/POM68K_VENDOR.md row 38).
//    |-2^31| is not representable as an i32: abs() of it is undefined
//    behaviour, and NeoST saw GCC and clang disagree by 136 cycles on it.
//    The quotient of $80000000 by any 16-bit divisor overflows, so this is
//    the overflow path of every core. None of the 8 065 DIVS vectors of
//    sst68000 has that dividend. The expected flags are the fork's existing
//    overflow rules, not new ones:
//      68000/68010  V=1, C=0, N and Z preserved   [D] SingleStepTests/680x0
//      68020/68030  V=1, C=N=Z=0 (|quotient| does not fit 16 bits)
//                                                 [D] WinUAE setdivsflags
//      68040        V=1, C=0, N and Z preserved   [D] WinUAE setdivsflags
//    and 18 cycles on the 68000 (negative dividend, early overflow exit).
//
// 2. External /BERR on a 68000 (row 42). extBusError() stacks a 68020/68030
//    frame and throws an MmuBusError that processException<C68000> cannot
//    handle: called from a 68000 board it left execute() as a C++ exception.
//    extBusError000(addr, write) is the 68000 entry point. No corpus has a
//    bus-error vector. The frame is NeoST's recipe: status word = IR bits
//    15-5 | R/W | function code, then the access address, IR, SR and PC.
//
// 4. An access fault on the memory operand of DIVU.L/DIVS.L (row 43).
//    Upstream wrapped that read in `catch(...) { return false; }`: on a
//    68020/68030/68040 a page fault or an external /BERR there took no
//    exception at all and left the instruction half executed. Found by a
//    fresh fuzz seed against the WinUAE oracle; no pinned vector has it.
//
// 5. CHK2/CMP2 on the 68040 leave N and V untouched (row 43). They are
//    undefined; the 68020/68030 follow WinUAE's table, the 68040 follows
//    WinUAE's early return. Four fresh-seed vectors, none pinned.
//
// 6. On the 68040 an unimplemented FPU instruction takes vector 11 even
//    when its source is of an unsupported datatype (row 44). The datatype
//    trap, vector 55, belongs to the instructions the chip executes itself.
//    Found by the first differential run of the integrated FPU against the
//    oracle (51 of the first 74 divergences).
//
// 3. The three host hooks back-ported from NeoST (row 42): a handled
//    illegal opcode is consumed like a NOP, an unhandled one still takes
//    vector 4; TAS brackets its read and its write with setBusLock;
//    willProcessFault sees the frame of an address error. All three are
//    no-ops by default, which the corpora prove; this proves the hooks fire.

#include "MoiraCompat.h"
#ifndef MOIRA_ENABLE_JIT
#define MOIRA_ENABLE_JIT 0   // Dirk's core: no JIT seam until the engine is rebuilt
#endif
#include "Mmu030Walk.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr uint32_t kHole = 0x080000;   // a byte the test board answers with /BERR

class TestCpu : public MoiraCompat {
public:
    explicit TestCpu(moira::CPUModel model) : mem(1 << 20, 0), model_(model) {
        setModel(model);
    }
    std::vector<uint8_t> mem;

    // Cell 2: the board asserts /BERR on kHole.
    bool holeArmed = false;
    bool holeWritesOnly = false;        // the hole answers reads, faults writes
    uint32_t holeAt = kHole;
    mutable unsigned holeTrips = 0;
    bool recordMovem = false;
    uint32_t movemBegin = 0, movemBytes = 0;
    mutable std::vector<uint32_t> movemCycles;
    mutable unsigned movemPointerReads = 0;
    // Cell 3: what the hooks saw. 'L'/'U' lock and unlock, 'r'/'w' bus cycles.
    bool handleIllegal = false;
    mutable std::string events;
    mutable int faults = 0;
    mutable moira::u32 faultAddr = 0;
    // Cell "bit field memory accesses": every data callback inside
    // [busBase, busBase + 16), as <kind><offset>: b/w a byte/word read,
    // B/W a byte/word write (a longword is two word callbacks).
    bool traceBus = false;
    uint32_t busBase = 0;
    mutable std::string bus;
    // Cell "MOVES function code after a fault": readFC() at every 16-bit
    // callback, as {address, fc, write}.
    struct FcAccess { moira::u32 addr; moira::u8 fc; bool write; };
    bool logFc = false;
    mutable std::vector<FcAccess> fcLog;

private:
    void traceAccess(char kind, moira::u32 a) const {
        if (!traceBus || a < busBase || a >= busBase + 16) return;
        if (!bus.empty()) bus += ' ';
        bus += kind;
        bus += std::to_string(a - busBase);
    }

    moira::CPUModel model_;

    // The board's /BERR, through the entry point of the model's core.
    void berr(moira::u32 a, bool write) const {
        ++holeTrips;
        auto& self = *const_cast<TestCpu*>(this);
        if (model_ >= moira::CPUModel::M68EC040) self.extBusError040();
        else if (model_ >= moira::CPUModel::M68EC020) self.extBusError();
        else self.extBusError000(a, write);
    }
    bool hole(moira::u32 a) const { return holeArmed && (a & 0xffffff) == holeAt; }
    void observeMovem(uint32_t a, bool write) const {
        if (!recordMovem) return;
        if (!write && ((a & ~3u) == 0x50000 || (a & ~3u) == 0x50010)) ++movemPointerReads;
        if (a >= movemBegin && a < movemBegin + movemBytes)
            movemCycles.push_back(a | (write ? 0x80000000u : 0));
    }

    moira::u8 read8(moira::u32 a) const override {
        pomBusAddr = a;                       // the address a /BERR reports
        traceAccess('b', a);
        if (hole(a) && !holeWritesOnly) berr(a, false);
        events += 'r';
        return mem[a & 0xfffff];
    }
    moira::u16 read16(moira::u32 a) const override {
        pomBusAddr = a;                       // the address a /BERR reports
        if (logFc) fcLog.push_back({ a, readFC(), false });
        observeMovem(a, false);
        traceAccess('w', a);
        if (hole(a) && !holeWritesOnly) berr(a, false);
        return moira::u16((mem[a & 0xfffff] << 8) | mem[(a + 1) & 0xfffff]);
    }
    void write8(moira::u32 a, moira::u8 v) const override {
        pomBusAddr = a;                       // the address a /BERR reports
        traceAccess('B', a);
        if (hole(a)) berr(a, true);
        events += 'w';
        const_cast<TestCpu*>(this)->mem[a & 0xfffff] = v;
    }
    void write16(moira::u32 a, moira::u16 v) const override {
        pomBusAddr = a;                       // the address a /BERR reports
        if (logFc) fcLog.push_back({ a, readFC(), true });
        observeMovem(a, true);
        traceAccess('W', a);
        if (hole(a)) berr(a, true);
        auto& m = const_cast<TestCpu*>(this)->mem;
        m[a & 0xfffff] = moira::u8(v >> 8); m[(a + 1) & 0xfffff] = moira::u8(v);
    }
    // Fork-only hooks: Dirk's interface has no such delegates, so they are
    // never called and the cells that relied on them fail visibly.
    bool illegalOpcodeHook(moira::u16) { return handleIllegal; }
    void pomDidReachWatchpoint(moira::u32 a, moira::Size S, bool write, bool) {
        char line[32];
        std::snprintf(line, sizeof line, "%c%X:%d ", write ? 'w' : 'r', unsigned(a), int(S));
        watch += line;
    }
    void didChangeCACR(moira::u32 value) override { cacrWritten = value; }
public:
    // Cell "watchpoints under the MMU": every access stop, as
    // <r|w><logical address>:<bytes>.
    std::string watch;
    // CACR read-back cell: the raw MOVEC value the board receives.
    moira::u32 cacrWritten = 0;
    // Cell "bus error during a table search": the 68030 PMMU registers,
    // set directly (after stage(), whose reset() clears them).
    void mmu030(moira::u32 tc, moira::u64 crp, moira::u32 tt0, moira::u32 tt1) {
        setTC(tc); setCRP(crp); setTT0(tt0); setTT1(tt1);
    }
    moira::u16 mmusr() const { return getMMUSR(); }
    // A supervisor data read of a word through the MMU, outside an
    // instruction: the translation sets the descriptor history bits as the
    // bus access would.
    bool supervisorDataRead16(moira::u32 a, moira::u32& v) {
        auto phys = mmu.translate(a, 5, false);
        if (!phys) return false;
        v = moira::u32(mem[*phys & 0xfffff]) << 8 | mem[(*phys + 1) & 0xfffff];
        return true;
    }
private:
    void setBusLock(bool locked) { events += locked ? 'L' : 'U'; }
    void willProcessFault(const moira::StackFrame& frame) {
        faults++; faultAddr = frame.addr;
    }
};

int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { \
    std::printf("  FAIL %s:%d: ", __FILE__, __LINE__); \
    std::printf(__VA_ARGS__); std::printf("\n"); failures++; } } while (0)

uint16_t g16(const TestCpu& c, uint32_t a) {
    return uint16_t(c.mem[a & 0xfffff] << 8 | c.mem[(a + 1) & 0xfffff]);
}
uint32_t g32(const TestCpu& c, uint32_t a) {
    return uint32_t(g16(c, a)) << 16 | g16(c, a + 2);
}
void p32(TestCpu& c, uint32_t a, uint32_t v);

void p16(TestCpu& c, uint32_t a, uint16_t v) {
    c.mem[a & 0xfffff] = uint8_t(v >> 8); c.mem[(a + 1) & 0xfffff] = uint8_t(v);
}

void p32(TestCpu& c, uint32_t a, uint32_t v) {
    p16(c, a, uint16_t(v >> 16)); p16(c, a + 2, uint16_t(v));
}

constexpr uint32_t kCode = 0x1000;
constexpr uint16_t kNop = 0x4e71;

// Stage ONE instruction at kCode, supervisor mode, the given CCR. Registers
// are set by the caller AFTER this (reset() clears them).
void stage(TestCpu& c, uint16_t opcode, uint8_t ccr)
{
    p16(c, kCode, opcode);
    p16(c, kCode + 2, kNop);
    p16(c, kCode + 4, kNop);
    c.reset();
    c.setSR(uint16_t(0x2700 | ccr));
    c.setSP(0x8000);
    c.setPC(kCode); c.setPC0(kCode);
    c.setIRD(opcode); c.setIRC(kNop);
}

struct ModelRow {
    moira::CPUModel model;
    const char* name;
    uint8_t ccrFromAllSet;      // CCR after the divide when it entered as $1F
    long cycles;                // -1: not pinned (020+ cycles are advisory)
};

// [B] MC68030UM-1989 Table 8-7; MC68040UM-1993 §8.4.3.
// Format $2 separates the resume PC from the instruction causing the trap.
void group2InstructionAddress()
{
    struct Row {
        const char* name;
        std::vector<uint16_t> words;
        unsigned vector, boundsBytes;
        bool fpu;
    };
    const Row rows[] = {
        {"DIVU.W", {0x80fc, 0}, 5, 0, false},
        {"DIVS.W", {0x81fc, 0}, 5, 0, false},
        {"DIVU.L", {0x4c7c, 0x0000, 0, 0}, 5, 0, false},
        {"DIVS.L", {0x4c7c, 0x0800, 0, 0}, 5, 0, false},
        {"CHK.W", {0x41bc, 5}, 6, 0, false},
        {"CHK.L", {0x413c, 0, 5}, 6, 0, false},
        {"CHK2.B", {0x00d0, 0x0800}, 6, 1, false},
        {"CHK2.W", {0x02d0, 0x0800}, 6, 2, false},
        {"CHK2.L", {0x04d0, 0x0800}, 6, 4, false},
        {"TRAPV", {0x4e76}, 7, 0, false},
        {"TRAPT", {0x50fc}, 7, 0, false},
        {"TRAPT.W", {0x50fa, 0x1234}, 7, 0, false},
        {"TRAPT.L", {0x50fb, 0x1234, 0x5678}, 7, 0, false},
        {"FTRAPT", {0xf27c, 0x000f}, 7, 0, true},
        {"FTRAPT.W", {0xf27a, 0x000f, 0x1234}, 7, 0, true},
        {"FTRAPT.L", {0xf27b, 0x000f, 0x1234, 0x5678}, 7, 0, true},
    };
    unsigned cases = 0;
    for (auto model : {moira::CPUModel::M68EC020, moira::CPUModel::M68020,
                       moira::CPUModel::M68EC030, moira::CPUModel::M68030,
                       moira::CPUModel::M68EC040, moira::CPUModel::M68LC040,
                       moira::CPUModel::M68040}) {
        for (const auto& row : rows) {
            const bool external = model < moira::CPUModel::M68EC040;
            if (row.fpu && !external && model != moira::CPUModel::M68040) continue;
            for (unsigned fpu = 0; fpu < (row.fpu && external ? 2u : 1u); ++fpu) {
                TestCpu c(model);
                stage(c, row.words[0], 2);         // TRAPV taken
                if (row.fpu) c.setFPUModel(external ?
                    (fpu ? MoiraFpu::M68882 : MoiraFpu::M68881) :
                    MoiraFpu::M68040);
                unsigned offset = 0;
                for (auto word : row.words) { p16(c, kCode + offset, word); offset += 2; }
                p16(c, kCode + offset, kNop); p16(c, kCode + offset + 2, kNop);
                c.setIRC(g16(c, kCode + 2));
                c.setD(0, 7); c.setA(0, 0x3000);
                if (row.boundsBytes == 1) { c.mem[0x3000] = 1; c.mem[0x3001] = 5; }
                if (row.boundsBytes == 2) { p16(c, 0x3000, 1); p16(c, 0x3002, 5); }
                if (row.boundsBytes == 4) { p32(c, 0x3000, 1); p32(c, 0x3004, 5); }
                p32(c, row.vector * 4, 0x4000);
                p16(c, 0x4000, 0x4e73); p16(c, 0x4002, kNop); // real RTE
                c.execute();
                const auto sp = c.getSP();
                CHECK(c.getPC0() == 0x4000 && sp == 0x8000 - 12 &&
                      g16(c, sp + 6) == (0x2000 | row.vector * 4) &&
                      g32(c, sp + 2) == kCode + offset && g32(c, sp + 8) == kCode,
                      "group2 model=%d %s fpu=%u: PC=%08X SP=%08X fmt=%04X resume=%08X IA=%08X",
                      int(model), row.name, fpu, c.getPC0(), sp, g16(c, sp + 6),
                      g32(c, sp + 2), g32(c, sp + 8));
                c.execute();
                CHECK(c.getPC0() == kCode + offset && c.getSP() == 0x8000,
                      "group2 model=%d %s: RTE PC=%08X SP=%08X",
                      int(model), row.name, c.getPC0(), c.getSP());
                c.execute();
                CHECK(c.getPC0() == kCode + offset + 2,
                      "group2 model=%d %s: next NOP PC=%08X", int(model), row.name, c.getPC0());
                ++cases;
            }
        }
    }
    std::printf("format-2 instruction address: %u trap/RTE cases\n", cases);
}

void divsOfIntMin()
{
    // X N Z V C. Entering with $1F shows what is preserved; entering with
    // $00 shows what is set.
    static const ModelRow rows[] = {
        { moira::CPUModel::M68000,   "68000",   0x1e, 18 },
        { moira::CPUModel::M68010,   "68010",   0x1e, -1 },
        { moira::CPUModel::M68020,   "68020",   0x12, -1 },
        { moira::CPUModel::M68030,   "68030",   0x12, -1 },
        { moira::CPUModel::M68LC040, "68LC040", 0x1e, -1 },
        { moira::CPUModel::M68040,   "68040",   0x1e, -1 },
    };
    static const uint16_t divisors[] = { 0x0001, 0x7fff, 0x8000, 0xffff };
    constexpr uint16_t kDivsD1D0 = 0x81c1;          // DIVS.W D1,D0

    for (const ModelRow& row : rows) {
        TestCpu c(row.model);
        for (uint16_t divisor : divisors) {
            for (uint8_t entry : { uint8_t(0x1f), uint8_t(0x00) }) {
                stage(c, kDivsD1D0, entry);
                c.setD(0, 0x80000000);
                c.setD(1, 0xabcd0000u | divisor);   // high word is ignored
                const moira::i64 c0 = c.getClock();
                c.execute();
                const long cycles = long(c.getClock() - c0);

                const uint8_t want = entry ? row.ccrFromAllSet : uint8_t(0x02);
                CHECK(c.getD(0) == 0x80000000,
                      "%s DIVS $80000000/$%04X changed D0 to %08X",
                      row.name, divisor, c.getD(0));
                CHECK(c.getCCR() == want,
                      "%s DIVS $80000000/$%04X entry CCR=%02X: CCR=%02X want %02X",
                      row.name, divisor, entry, c.getCCR(), want);
                CHECK(c.getPC0() == kCode + 2,
                      "%s DIVS $80000000/$%04X took an exception: PC0=%08X",
                      row.name, divisor, c.getPC0());
                if (row.cycles >= 0)
                    CHECK(cycles == row.cycles,
                          "%s DIVS $80000000/$%04X: %ld cycles, want %ld",
                          row.name, divisor, cycles, row.cycles);
            }
        }
    }
}

constexpr uint32_t kHandler = 0x2000;

// Run the staged instruction; an exception leaving execute() is the defect.
bool executeContained(TestCpu& c)
{
    try { c.execute(); } catch (...) { return false; }
    return true;
}

void externalBusError000()
{
    // SR is the register as it stands at the fault: TST has set nothing
    // before its read; MOVE has already set Z from D0 = 0 before its write.
    struct Case { uint16_t opcode; bool write; uint16_t code; uint16_t sr; const char* what; };
    static const Case cases[] = {
        { 0x4a10, false, 0x4a15, 0x2700, "TST.B (A0), read" },      // IR&$FFE0 | R | FC5
        { 0x1080, true,  0x1085, 0x2704, "MOVE.B D0,(A0), write" }, // IR&$FFE0 | W | FC5
    };
    for (const Case& k : cases) {
        TestCpu c(moira::CPUModel::M68000);
        p32(c, 2 * 4, kHandler);                    // vector 2: bus error
        p16(c, kHandler, kNop);
        stage(c, k.opcode, 0);
        c.setA(0, kHole);
        c.holeArmed = true;

        const bool contained = executeContained(c);
        CHECK(contained, "%s: the bus error left execute() as a C++ exception",
              k.what);
        if (!contained) continue;
        const uint32_t sp = c.getSP();
        CHECK(c.getPC0() == kHandler, "%s: PC0=%08X, want the vector-2 handler",
              k.what, c.getPC0());
        CHECK(sp == 0x8000 - 14, "%s: SP=%08X, want a 14-byte group-0 frame",
              k.what, sp);
        CHECK(g16(c, sp) == k.code, "%s: status word %04X want %04X",
              k.what, g16(c, sp), k.code);
        CHECK(g32(c, sp + 2) == kHole, "%s: access address %08X",
              k.what, g32(c, sp + 2));
        CHECK(g16(c, sp + 6) == k.opcode, "%s: IR %04X", k.what, g16(c, sp + 6));
        CHECK(g16(c, sp + 8) == k.sr, "%s: SR %04X want %04X",
              k.what, g16(c, sp + 8), k.sr);
        CHECK(g32(c, sp + 10) == kCode + 2, "%s: PC %08X want %08X",
              k.what, g32(c, sp + 10), kCode + 2);
        CHECK(c.faults == 1 && c.faultAddr == kHole,
              "%s: willProcessFault saw %d fault(s) at %08X",
              k.what, c.faults, c.faultAddr);
    }
}

void hostHooks()
{
    constexpr uint16_t kIllegal = 0x4afc;
    {   // unhandled: vector 4, as upstream
        TestCpu c(moira::CPUModel::M68000);
        p32(c, 4 * 4, kHandler);
        p16(c, kHandler, kNop);
        stage(c, kIllegal, 0);
        c.execute();
        CHECK(c.getPC0() == kHandler, "unhandled ILLEGAL: PC0=%08X", c.getPC0());
    }
    {   // handled: consumed like a NOP
        TestCpu c(moira::CPUModel::M68000);
        p32(c, 4 * 4, kHandler);
        stage(c, kIllegal, 0);
        c.handleIllegal = true;
        const moira::i64 c0 = c.getClock();
        c.execute();
        CHECK(c.getPC0() == kCode + 2 && c.getSP() == 0x8000,
              "handled ILLEGAL: PC0=%08X SP=%08X", c.getPC0(), c.getSP());
        CHECK(c.getClock() - c0 == 4, "handled ILLEGAL: %lld cycles, want 4",
              (long long)(c.getClock() - c0));
    }
    {   // TAS (A0): lock, read, write, unlock
        TestCpu c(moira::CPUModel::M68000);
        stage(c, 0x4ad0, 0);
        c.setA(0, 0x3000);
        c.events.clear();
        c.execute();
        CHECK(c.events == "LrwU", "TAS (A0) bus-lock bracket: \"%s\", want LrwU",
              c.events.c_str());
        CHECK(c.mem[0x3000] == 0x80, "TAS (A0) wrote %02X", c.mem[0x3000]);
    }
    {   // TAS on the faulting byte still releases the lock
        TestCpu c(moira::CPUModel::M68000);
        p32(c, 2 * 4, kHandler);
        p16(c, kHandler, kNop);
        stage(c, 0x4ad0, 0);
        c.setA(0, kHole);
        c.holeArmed = true;
        c.events.clear();
        const bool contained = executeContained(c);
        CHECK(contained && c.events == "LU",
              "TAS on a /BERR byte: contained=%d events \"%s\", want LU",
              int(contained), c.events.c_str());
    }
    {   // an address error reaches willProcessFault with its frame
        TestCpu c(moira::CPUModel::M68000);
        p32(c, 3 * 4, kHandler);
        p16(c, kHandler, kNop);
        stage(c, 0x4a50, 0);                        // TST.W (A0)
        c.setA(0, 0x3001);
        c.execute();
        CHECK(c.faults == 1 && c.faultAddr == 0x3001 && c.getPC0() == kHandler,
              "address error: %d fault(s) at %08X, PC0=%08X",
              c.faults, c.faultAddr, c.getPC0());
    }
}

void divlOperandFault()
{
    static const struct { moira::CPUModel model; const char* name; } models[] = {
        { moira::CPUModel::M68020,   "68020" },
        { moira::CPUModel::M68030,   "68030" },
        { moira::CPUModel::M68LC040, "68LC040" },
    };
    for (const auto& m : models) {
        for (uint16_t ext : { uint16_t(0x0000), uint16_t(0x0800) }) {   // DIVU.L, DIVS.L
            TestCpu c(m.model);
            p32(c, 2 * 4, kHandler);                // vector 2: bus error
            p16(c, kHandler, kNop);
            p16(c, kHandler + 2, kNop);
            stage(c, 0x4c50, 0);                    // DIVx.L (A0),D0
            p16(c, kCode + 2, ext);
            c.setIRC(ext);
            c.setA(0, kHole);
            c.setD(0, 0x12345678);
            c.holeArmed = true;

            const bool contained = executeContained(c);
            CHECK(contained, "%s DIV%c.L (A0): the fault left execute()",
                  m.name, ext ? 'S' : 'U');
            if (!contained) continue;
            CHECK(c.getPC0() == kHandler,
                  "%s DIV%c.L (A0) on a /BERR operand: PC0=%08X, want the "
                  "vector-2 handler (the fault was swallowed)",
                  m.name, ext ? 'S' : 'U', c.getPC0());
            CHECK(c.getSP() < 0x8000 && c.getD(0) == 0x12345678,
                  "%s DIV%c.L (A0): SP=%08X D0=%08X, want a stacked frame and "
                  "an untouched quotient register",
                  m.name, ext ? 'S' : 'U', c.getSP(), c.getD(0));
        }
    }
}

void chk2FlagsOn040()
{
    // CMP2.B (A0),D0 with bounds [1, 5], entered with every flag set ($1F).
    // In range: Z = 0, C = 0. On a bound: Z = 1. N and V: cleared by the
    // 68020/68030 table for these operands, left as they were by the 68040.
    struct Row { moira::CPUModel model; const char* name; uint8_t inRange, onBound; };
    static const Row rows[] = {
        { moira::CPUModel::M68020,   "68020",   0x10, 0x14 },
        { moira::CPUModel::M68030,   "68030",   0x10, 0x14 },
        { moira::CPUModel::M68LC040, "68LC040", 0x1a, 0x1e },
        { moira::CPUModel::M68040,   "68040",   0x1a, 0x1e },
    };
    for (const Row& row : rows) {
        for (int onBound = 0; onBound < 2; onBound++) {
            TestCpu c(row.model);
            stage(c, 0x00d0, 0x1f);                 // CMP2.B (A0),Rn
            p16(c, kCode + 2, 0x0000);              // Rn = D0, CMP2
            c.setIRC(0x0000);
            c.mem[0x3000] = 1; c.mem[0x3001] = 5;   // lower, upper
            c.setA(0, 0x3000);
            c.setD(0, onBound ? 1 : 3);
            c.execute();
            const uint8_t want = onBound ? row.onBound : row.inRange;
            CHECK(c.getCCR() == want && c.getPC0() == kCode + 4,
                  "%s CMP2.B value %s: CCR=%02X want %02X, PC0=%08X",
                  row.name, onBound ? "on the lower bound" : "in range",
                  c.getCCR(), want, c.getPC0());
        }
    }
}

void fpuUnimplementedBeforeDatatype040()
{
    // Packed-decimal immediate source (format 3: unsupported on the 68040).
    // FMOD ($25) is unimplemented there -> vector 11. FADD ($22) is
    // implemented -> the datatype trap, vector 55.
    struct Case { uint16_t opmode; int vector; const char* what; };
    static const Case cases[] = {
        { 0x25, 11, "FMOD.P #imm,FP0 (unimplemented)" },
        { 0x22, 55, "FADD.P #imm,FP0 (implemented)" },
    };
    for (const Case& k : cases) {
        TestCpu c(moira::CPUModel::M68040);
        c.setFPUModel(MoiraFpu::M68040);
        p32(c, 11 * 4, kHandler);
        p32(c, 55 * 4, kHandler + 0x40);
        p16(c, kHandler, kNop); p16(c, kHandler + 2, kNop);
        p16(c, kHandler + 0x40, kNop); p16(c, kHandler + 0x42, kNop);
        const uint16_t ext = uint16_t(0x4000 | (3 << 10) | k.opmode);
        stage(c, 0xf23c, 0);                        // F<op>.P #imm,FP0
        p16(c, kCode + 2, ext);
        c.setIRC(ext);
        p32(c, kCode + 4, 0x00010000);              // +1.0E+0001, packed
        p32(c, kCode + 8, 0x10000000);
        p32(c, kCode + 12, 0x00000000);
        p16(c, kCode + 16, kNop);
        c.execute();
        const uint32_t want = kHandler + (k.vector == 55 ? 0x40 : 0);
        CHECK(c.getPC0() == want,
              "68040 %s: PC0=%08X, want vector %d at %08X",
              k.what, c.getPC0(), k.vector, want);
    }
}

void flineShapesOn68LC040()
{
    // What is no instruction at all is refused before the "no FPU" frame:
    // format $0 through vector 11, or vector 4 with the PC past the
    // extension word for opmodes $78-$7F. A real instruction gets format $4.
    struct Case { uint16_t ext; int vector; uint16_t frameWord; int frameBytes;
                  uint32_t pcOffset; const char* what; };
    static const Case cases[] = {
        { 0x0000, 11, 0x402c, 16, 4, "FMOVE FP0,FP0 (an instruction)" },
        { 0x0005, 11, 0x002c,  8, 0, "opmode $05 (no instruction)" },
        { 0x2000, 11, 0x002c,  8, 0, "opclass 001 (no instruction)" },
        { 0x0078,  4, 0x0010,  8, 4, "opmode $78 (illegal vector)" },
    };
    for (const Case& k : cases) {
        TestCpu c(moira::CPUModel::M68LC040);
        p32(c, 4 * 4, kHandler + 0x40);
        p32(c, 11 * 4, kHandler);
        p16(c, kHandler, kNop); p16(c, kHandler + 2, kNop);
        p16(c, kHandler + 0x40, kNop); p16(c, kHandler + 0x42, kNop);
        stage(c, 0xf200, 0);
        p16(c, kCode + 2, k.ext);
        c.setIRC(k.ext);
        p16(c, kCode + 4, kNop);
        const uint32_t sp = c.getSP();
        c.execute();
        const uint32_t want = kHandler + (k.vector == 4 ? 0x40 : 0);
        CHECK(c.getPC0() == want && sp - c.getSP() == uint32_t(k.frameBytes) &&
              g16(c, c.getSP() + 6) == k.frameWord &&
              g32(c, c.getSP() + 2) == kCode + k.pcOffset,
              "68LC040 $F200 %s: PC0=%08X (want %08X), %u frame bytes (want %d), "
              "format word %04X (want %04X), stacked PC %08X (want %08X)",
              k.what, c.getPC0(), want, unsigned(sp - c.getSP()), k.frameBytes,
              g16(c, c.getSP() + 6), k.frameWord,
              g32(c, c.getSP() + 2), kCode + k.pcOffset);
    }
}

void fbccPredicateIsFiveBits()
{
    // FBcc.W with predicate $21: bit 5 is ignored, so it is FBEQ.
    static const struct { moira::CPUModel cpu; MoiraFpu fpu; const char* name; } rigs[] = {
        { moira::CPUModel::M68030, MoiraFpu::M68882, "68030+68882" },
        { moira::CPUModel::M68040, MoiraFpu::M68040, "68040" },
    };
    for (const auto& rig : rigs) {
        for (int z = 0; z < 2; z++) {
            TestCpu c(rig.cpu);
            c.setFPUModel(rig.fpu);
            p32(c, 11 * 4, kHandler);
            p16(c, kHandler, kNop); p16(c, kHandler + 2, kNop);
            stage(c, 0xf2a1, 0);                    // FBcc.W, predicate $21
            p16(c, kCode + 2, 0x0020);              // to kCode + $22
            c.setIRC(0x0020);
            p16(c, kCode + 4, kNop); p16(c, kCode + 6, kNop);
            p16(c, kCode + 0x22, kNop); p16(c, kCode + 0x24, kNop);
            c.setFPSR(z ? 0x04000000 : 0);          // FPSR.Z
            const uint32_t sp = c.getSP();
            c.execute();
            const uint32_t want = z ? kCode + 0x22 : kCode + 4;
            CHECK(c.getSP() == sp && c.getPC0() == want,
                  "%s FBcc predicate $21 with Z=%d: next instruction at %08X "
                  "(want %08X), SP moved by %d",
                  rig.name, z, c.getPC0(), want, int(sp - c.getSP()));
        }
    }
}

void fpuStoreFaultRestarts040()
{
    // FMOVE.L FP0,(A0)+ onto a /BERR byte: the instruction restarts — the
    // frame holds its own address and A0 is back where it was. No FPU
    // instruction ends on a "last write".
    TestCpu c(moira::CPUModel::M68040);
    c.setFPUModel(MoiraFpu::M68040);
    p32(c, 2 * 4, kHandler);
    p16(c, kHandler, kNop); p16(c, kHandler + 2, kNop);
    stage(c, 0xf218, 0);                            // FMOVE.L FP0,(A0)+
    p16(c, kCode + 2, 0x6000);
    c.setIRC(0x6000);
    p16(c, kCode + 4, kNop);
    c.setA(0, kHole);
    c.holeArmed = true;
    const bool contained = executeContained(c);
    CHECK(contained, "68040 FMOVE.L FP0,(A0)+: the fault left execute()");
    if (!contained) return;
    CHECK(c.getPC0() == kHandler && c.getA(0) == kHole &&
          g32(c, c.getSP() + 2) == kCode,
          "68040 FMOVE.L FP0,(A0)+ on a /BERR byte: PC0=%08X (want %08X), "
          "A0=%08X (want %08X), stacked PC %08X (want %08X)",
          c.getPC0(), kHandler, c.getA(0), kHole, g32(c, c.getSP() + 2), kCode);
}

// [D] WinUAE cpuemu_31.c op_0188/op_01c8 @ Hatari e77819f7:
// no gen_set_fault_pc/mmu_restart=false on ANY MOVEP output byte.
// [F] The manual's generic format-$7 PC does not specify this partial
// peripheral transfer. Settled by: a faulting MOVEP on a real 68040.
void movepFaultRestarts040()
{
    unsigned sequences = 0;
    for (auto model : {moira::CPUModel::M68LC040, moira::CPUModel::M68040})
    for (unsigned mmu = 0; mmu < 3; ++mmu)      // off, 4K, 8K
    for (unsigned path = 0; path < 3; ++path)   // bus, data window, cache
    for (bool load : {false, true}) {
        if (load && path != 0) continue;        // read-fault controls
        for (unsigned bytes : {2u, 4u})
        for (unsigned fault = 0; fault <= bytes; ++fault) {
            ++sequences;
            TestCpu c(model);
            const uint16_t opcode = uint16_t((load ? 0x0709 : 0x0789) |
                                             (bytes == 4 ? 0x40 : 0));
            constexpr uint32_t source = 0x89ABCDEF;
            constexpr uint16_t displacement = 0x54F5;
            const uint32_t pageBytes = mmu == 2 ? 8192 : 4096;
            const uint32_t logicalPage = 0x10000000;
            const uint32_t physicalPage = 0x60000;
            const uint32_t bad = mmu ? logicalPage + pageBytes : kHole;
            const uint32_t base = bad - 2 * fault;
            const uint32_t physical = mmu ? physicalPage + pageBytes - 2 * fault : base;
            const bool fails = fault < bytes;
            const uint32_t an = base - displacement;
            p32(c, 8, kHandler);
            p16(c, kHandler, mmu ? 0xF518 : 0x4E73); // PFLUSHA / RTE
            p16(c, kHandler + 2, mmu ? 0x4E73 : kNop);
            p16(c, kHandler + 4, kNop);
            stage(c, opcode, 0x1F);
            p16(c, kCode + 2, displacement); c.setIRC(displacement);
            c.setD(3, source); c.setA(1, an);
            c.setCACR(path == 2 ? 0x80008000 : 0);
            if (mmu) {
                p32(c, 0x4020, 0x4202); p32(c, 0x4200, 0x6002);
                p32(c, 0x6000, physicalPage | 0x19); // U/M/R, WT
                c.setURP040(0x4000); c.setSRP040(0x4000);
                c.setITT0(0x0000C000); c.setDTT0(0x0000C000);
                c.setTC040(mmu == 2 ? 0xC000 : 0x8000);
            } else {
                c.holeArmed = true;
            }
            for (unsigned i = 0; i < bytes; ++i) {
                c.mem[physical + 2 * i] = uint8_t(0x10 + i);
                c.mem[physical + 2 * i + 1] = 0xA5;
            }
#if MOIRA_ENABLE_JIT
            if (path == 1 && fault != 0) {
                // Only the good page is exposed. The faulting page has no
                // window entry and must return to the physical/MMU path.
                uint32_t ignored = 0, pa = base, page = 0, length = 0;
                CHECK(c.pomJitReadData(base, 1, ignored), "MOVEP window warm-up");
                CHECK(c.pomJitProbeData(base, true, true, pa, page, length),
                      "MOVEP window has a permitted resident translation");
                c.pomJitDtlbFillFn = [](void*, moira::u32, int) -> moira::u8* { return nullptr; };
                auto& e = c.pomJitDtlbW.e[(base >> 12) &
                                         (moira::Moira::PomJitDtlb::kEntries - 1)];
                e.tag = c.pomJitDataTag(base, true); e.codeMask = 0;
                e.host = c.mem.data() + (pa & ~0xFFFu);
            }
#endif
            CHECK(executeContained(c), "MOVEP fault escaped execute()");
            CHECK(c.getA(1) == an && (c.getSR() & 31) == 0x1F && !c.isHalted(),
                  "MOVEP changed An/CCR or halted");
            if (fails) {
                const auto sp = c.getSP();
                CHECK(c.getPC0() == kHandler && sp == 0x8000 - 60 &&
                      g16(c, sp + 6) == 0x7008 && g32(c, sp + 2) == kCode,
                      "MOVEP.%c %s model=%u mmu=%u path=%u byte=%u: "
                      "PC=%08X SP=%08X format=%04X stacked PC=%08X (want %08X)",
                      bytes == 2 ? 'W' : 'L', load ? "load" : "store",
                      unsigned(model), mmu, path, fault, c.getPC0(), sp,
                      g16(c, sp + 6), g32(c, sp + 2), kCode);
                CHECK(g32(c, sp + 20) == bad &&
                      g16(c, sp + 12) == ((mmu ? 0x400 : 0) | (load ? 0x125 : 0x25)),
                      "MOVEP fault address/SSW do not describe its failed byte");
                if (!load)
                    CHECK(g32(c, sp + 24) == bad &&
                          g32(c, sp + 28) == ((source >> (8 * (bytes - fault - 1))) & 0xFF),
                          "MOVEP writeback does not contain the failed byte");
                CHECK(c.getD(3) == source && (mmu || c.holeTrips == 1),
                      "MOVEP source/destination changed early or /BERR did not fire once");
#if MOIRA_ENABLE_JIT
                if (path == 1 && fault != 0)
                    CHECK(c.pomJitDataW1.host != nullptr,
                          "MOVEP prefix did not exercise the data window");
#endif
            } else {
                const uint32_t loaded = bytes == 2 ? 0x89AB1011 : 0x10111213;
                CHECK(c.getPC() == kCode + 4 && c.getSP() == 0x8000 &&
                      c.holeTrips == 0 && c.getD(3) == (load ? loaded : source),
                      "MOVEP success control did not complete normally");
            }
            for (unsigned i = 0; i < bytes; ++i) {
                const auto expected = !load && i < fault ?
                    uint8_t(source >> (8 * (bytes - i - 1))) : uint8_t(0x10 + i);
                CHECK(c.mem[physical + 2 * i] == expected &&
                      c.mem[physical + 2 * i + 1] == 0xA5,
                      "MOVEP prefix/gap mismatch byte=%u fault=%u", i, fault);
            }
            if (fails) {
                c.holeArmed = false;
                if (mmu) p32(c, 0x6004, (physicalPage + pageBytes) | 0x19);
                // [B] MC68040UM-1993 §3.5: repairing the descriptor does
                // not discard the failed ATC entry. The guest must flush it.
                if (mmu)
                    CHECK(executeContained(c) && c.getPC0() == kHandler + 2,
                          "MOVEP repair handler did not execute PFLUSHA");
                CHECK(executeContained(c) && c.getPC() == kCode,
                      "MOVEP RTE skipped the unfinished instruction");
                CHECK(executeContained(c) && c.getPC() == kCode + 4,
                      "MOVEP did not complete on retry");
                const uint32_t loaded = bytes == 2 ? 0x89AB1011 : 0x10111213;
                CHECK(c.getD(3) == (load ? loaded : source), "MOVEP retry register result");
                for (unsigned i = 0; i < bytes; ++i)
                    CHECK(c.mem[physical + 2 * i] == (load ? uint8_t(0x10 + i) :
                          uint8_t(source >> (8 * (bytes - i - 1)))),
                          "MOVEP retry left byte %u incomplete", i);
            }
        }
    }
    std::printf("  MOVEP 040 fault/retry: %u sequences\n", sequences);
}

// [B] MC68040UM-1993 §§8.4.6.2/8.4.6.7: CM restores the calculated EA
// for indexed/PC-relative MOVEM and repeats all operand accesses. Other
// modes calculate from the current registers. Memory indirection is not
// repeated, even if the handler changed the pointer or the index/base.
// Excluded on Dirk Hoffmann's core: the 68040 CM continuation of MOVEM
// (MC68040UM-1993 8.4.6.2, 8.4.6.7) has no equivalent in its restart model.
// An upstream gap to raise with him (TODO.md), not a fork artifact.
#ifndef POM68K_CONTINUATION_040
#define POM68K_CONTINUATION_040 0
#endif
#if POM68K_CONTINUATION_040
void movemRestart040()
{
    unsigned sequences = 0;
    for (auto model : {moira::CPUModel::M68EC040, moira::CPUModel::M68LC040, moira::CPUModel::M68040})
    for (unsigned mmu = 0; mmu < 3; ++mmu)
    for (bool super : {false, true})
    for (bool cache : {false, true})
    for (bool load : {false, true})
    for (unsigned mode = 0; mode < 8; ++mode) // AI, DI, IX brief/full, DIPC, IXPC full, PI, PD
    for (unsigned bytes : {2u, 4u})
    for (unsigned fault = 0; fault < 3; ++fault)
    for (unsigned action = 0; action < 3; ++action) { // CM, cleared CM, edited EA
        const bool indexed = mode >= 2 && mode <= 5, indirect = mode == 3 || mode == 5;
        const bool pd = mode == 7;
        if ((mmu && (model == moira::CPUModel::M68EC040 || pd)) ||
            (load ? pd : mode == 4 || mode == 5 || mode == 6) || (!indexed && action == 2)) continue;
        const auto before = failures;
        TestCpu c(model);
        const uint32_t pageBytes = mmu == 2 ? 8192 : 4096;
        const uint32_t offset = pd ? 0 : pageBytes - fault * bytes;
        const uint32_t oldPa = 0x60000 + offset, repairedPa = mmu ? 0x70000 + offset : oldPa;
        const uint32_t altPa = (mmu ? 0x80000 : 0x64000) + offset;
        const uint32_t ea = mmu ? 0x10000000 + offset : oldPa;
        const uint32_t altEa = mmu ? ea + 0x4000 : altPa;
        const uint32_t pc = ea - 0x100;
        const uint16_t eaBits[] = {0x10, 0x28, 0x30, 0x30, 0x3A, 0x3B, 0x18, 0x20};
        const uint16_t opcode = uint16_t((load ? 0x4C80 : 0x4880) | (bytes == 4 ? 0x40 : 0) | eaBits[mode]);
        const uint16_t mask = pd ? 0xC080 : 0x0103; // D0/D1/A0, reversed for predecrement
        const uint16_t sr = uint16_t((super ? 0x2700 : 0x0700) | 0x1F);
        const uint32_t fresh[] = {0x89ABCDEF, 0x12345678, 0xFEDCBA98};
        stage(c, opcode, 0x1F);
        c.setUSP(0x9000); c.setSR(sr);
        p32(c, 8, kHandler);
        p16(c, kHandler, mmu ? 0xF518 : kNop);
        p16(c, kHandler + 2, 0x4E73); p16(c, kHandler + 4, kNop);
        p16(c, pc, opcode); p16(c, pc + 2, mask);
        unsigned length = 4;
        uint32_t a0 = ea;
        if (mode == 1) { p16(c, pc + 4, 0x40); a0 -= 0x40; length = 6; }
        if (mode == 2) { p16(c, pc + 4, 0x0800); a0 -= 0x20; length = 6; }
        if (mode == 3) { p16(c, pc + 4, 0x0151); a0 = 0x50000; length = 6; }
        if (mode == 4) { p16(c, pc + 4, uint16_t(ea - (pc + 4))); length = 6; }
        if (mode == 5) { p16(c, pc + 4, 0x01F1); p32(c, pc + 6, 0x50000); length = 10; }
        if (pd) a0 += 3 * bytes;
        p16(c, pc + length, kNop); p16(c, pc + length + 2, kNop);
        p32(c, 0x50000, ea); p32(c, 0x50010, altEa);
        c.setA(0, a0); c.setD(0, 0x20); c.setD(1, 0x13579BDF);
        c.setPC(pc); c.setPC0(pc); c.setIRD(opcode); c.setIRC(mask);
        c.setCACR(cache ? 0x80008000 : 0);
        c.setITT0(0x1000C040); c.setITT1(0x0000C040); c.setDTT0(0x0000C040);
        if (mmu) {
            p32(c, 0x4020, 0x4202); p32(c, 0x4200, 0x6002);
            p32(c, 0x6000, 0x60059); // U/M/R, NC; next page invalid
            const unsigned altIndex = mmu == 2 ? 8 : 16;
            p32(c, 0x6000 + altIndex, 0x80059);
            p32(c, 0x6004 + altIndex, (0x80000 + pageBytes) | 0x59);
            c.setURP040(0x4000); c.setSRP040(0x4000);
            c.setTC040(mmu == 2 ? 0xC000 : 0x8000);
        } else {
            c.holeAt = pd ? oldPa + (2 - fault) * bytes : oldPa + fault * bytes;
            c.holeArmed = true;
        }
        for (unsigned i = 0; i < 3; ++i) {
            if (bytes == 4) p32(c, oldPa + i * bytes, fresh[i]);
            else p16(c, oldPa + i * bytes, uint16_t(fresh[i]));
        }
        c.recordMovem = true; c.movemBegin = oldPa; c.movemBytes = 3 * bytes;
        CHECK(executeContained(c), "MOVEM: original fault escaped execute");
        const uint32_t sp = c.getSP(), savedEa = pd ? ea + 3 * bytes : ea;
        CHECK(c.getPC0() == kHandler && sp == 0x8000 - 60 && !c.isHalted() &&
              g16(c, sp + 6) == 0x7008 && g32(c, sp + 2) == pc &&
              g32(c, sp + 8) == savedEa && (g16(c, sp + 12) & 0x1000) &&
              !c.mmu040MovemArmed && c.getCCR() == 0x1F,
              "MOVEM: original CM/EA/PC frame and consumed latch");
        CHECK(c.movemCycles.size() == fault * (bytes / 2) + (mmu ? 0 : 1),
              "MOVEM: exact completed transfer prefix and failed bus cycle");
        CHECK(c.movemPointerReads == (indirect ? 2u : 0u), "MOVEM: initial indirect pointer read once");
        c.holeArmed = false;
        if (mmu) {
            p32(c, 0x6000, 0x70059);
            p32(c, 0x6004, (0x70000 + pageBytes) | 0x59);
        }
        for (unsigned i = 0; i < 3; ++i) {
            for (uint32_t dest : {repairedPa, altPa}) {
                if (bytes == 4) p32(c, dest + i * bytes, fresh[i]);
                else p16(c, dest + i * bytes, uint16_t(fresh[i]));
            }
        }
        uint32_t newA0 = altEa;
        if (mode == 1) newA0 -= 0x40;
        if (mode == 2) newA0 -= 0x40;
        if (mode == 3) newA0 = 0x50010;
        if (pd) newA0 += 3 * bytes;
        c.setA(0, newA0); c.setD(0, 0x40); c.setD(1, 0x2468ACE0);
        if (mode == 4) p16(c, pc + 4, uint16_t(altEa - (pc + 4)));
        if (mode == 5) p32(c, pc + 6, 0x50010);
        if (action == 1) p16(c, sp + 12, uint16_t(g16(c, sp + 12) & ~0x1000));
        if (action == 2) p32(c, sp + 8, altEa);
        CHECK(executeContained(c) && c.getPC0() == kHandler + 2, "MOVEM: handler flush/NOP");
        CHECK(executeContained(c) && c.getPC0() == pc && c.getSR() == sr &&
              c.getSP() == (super ? 0x8000u : 0x9000u) &&
              c.mmu040MovemArmed == (action != 1) &&
              (action == 1 || c.mmu040MovemEa == (action == 2 ? altEa : savedEa)),
              "MOVEM: RTE produces the saved CM continuation at an instruction boundary");
        const bool useSaved = indexed && action != 1;
        const uint32_t target = useSaved && action != 2 ? repairedPa : altPa;
        const uint32_t targetEa = useSaved && action != 2 ? ea : altEa;
        c.movemBegin = target; c.movemCycles.clear(); c.movemPointerReads = 0;
        CHECK(executeContained(c) && c.getPC0() == pc + length && !c.isHalted() &&
              !c.mmu040MovemArmed && c.getCCR() == 0x1F,
              "MOVEM: retry completes and consumes the continuation");
        CHECK(c.movemPointerReads == (indirect && !useSaved ? 2u : 0u),
              "MOVEM: continuation must not repeat the indirect pointer read");
        std::vector<uint32_t> expectedCycles;
        for (unsigned n = 0; n < 3; ++n) {
            const auto i = pd ? 2 - n : n;
            for (unsigned half = 0; half < bytes; half += 2)
                expectedCycles.push_back((target + i * bytes + half) | (load ? 0u : 0x80000000u));
        }
        CHECK(c.movemCycles == expectedCycles, "MOVEM: all operand accesses repeat in order, including the prefix");
        if (load) {
            const auto sext = [bytes](uint32_t v) { return bytes == 4 ? v : uint32_t(int32_t(int16_t(v))); };
            CHECK(c.getD(0) == sext(fresh[0]) && c.getD(1) == sext(fresh[1]) &&
                  c.getA(0) == (mode == 6 ? targetEa + 3 * bytes : sext(fresh[2])),
                  "MOVEM: loaded registers and postincrement result");
        } else {
            const uint32_t expected[] = {0x40, 0x2468ACE0, pd ? newA0 - bytes : newA0};
            for (unsigned i = 0; i < 3; ++i)
                CHECK((bytes == 4 ? g32(c, target + i * bytes) : g16(c, target + i * bytes)) ==
                      (bytes == 4 ? expected[i] : uint16_t(expected[i])), "MOVEM: repeated stores use current register values");
            CHECK(c.getA(0) == (pd ? altEa : newA0), "MOVEM: base register commits once");
        }
        if (failures != before)
            std::printf("MOVEM context: model=%u mmu=%u S=%u cache=%u load=%u mode=%u bytes=%u fault=%u action=%u\n",
                        unsigned(model), mmu, super, cache, load, mode, bytes, fault, action);
        ++sequences;
    }
    std::printf("  MOVEM 040 CM restart: %u sequences\n", sequences);
}
#endif

// [B] MC68040UM-1993 §8.4.6.4 — "The FA is a physical address only for
// cache pushes and a logical address for all other cases"; §8.4.6.7: all
// read access errors and a normal write physical bus error stack "the
// logical address of the fault". The board answers the translated access
// with /BERR: the frame must carry the logical address the program used.
// [D] WinUAE (oracle/uae @ Hatari e77819f7) stacks the physical address on
// this path; the manual overrules it.
void hostBusErrorFaultAddressIsLogical040()
{
    constexpr uint32_t kLogical = 0x10000100, kPhysicalPage = 0x00F00000;
    for (auto model : {moira::CPUModel::M68LC040, moira::CPUModel::M68040})
    for (bool write : {false, true}) {
        TestCpu c(model);
        const uint16_t opcode = write ? 0x2080 : 0x2010;    // MOVE.L D0,(A0) / (A0),D0
        p32(c, 2 * 4, kHandler);
        p16(c, kHandler, kNop); p16(c, kHandler + 2, kNop);
        stage(c, opcode, 0);
        c.setA(0, kLogical); c.setD(0, 0x89ABCDEF);
        // 4K pages; logical $10000000 -> physical $00F00000, cache inhibited.
        // Code, vectors, stack and tables pass through the transparent $00xxxxxx.
        p32(c, 0x4020, 0x4202); p32(c, 0x4200, 0x6002);
        p32(c, 0x6000, kPhysicalPage | 0x59);
        c.setURP040(0x4000); c.setSRP040(0x4000);
        c.setITT0(0x0000C040); c.setDTT0(0x0000C040);
        c.setTC040(0x8000);
        c.holeAt = kPhysicalPage + (kLogical & 0xFFF);
        c.holeArmed = true;
        const bool contained = executeContained(c);
        const uint32_t sp = c.getSP();
        const uint16_t format = g16(c, sp + 6), ssw = g16(c, sp + 0x0C);
        const uint32_t fa = g32(c, sp + 0x14), wb3a = g32(c, sp + 0x18);
        const uint16_t wantSsw = write ? 0x0005 : 0x0105;  // TM=5, long, no ATC
        // [B] MC68040UM-1993 §8.4.6.5: WB3 is the integer unit's holding
        // register, which takes the logical address before the data memory
        // unit translates it (Section 2, Figure 2-2), so for the write WB3A
        // is the logical address too. This pins the address domain only, not where
        // the faulted write is placed: Moira puts it in WB3, §8.4.6.7 puts a
        // normal write bus error in WB1 with WB1A equal to FA.
        CHECK(contained && c.holeTrips == 1 && c.getPC0() == kHandler &&
              format == 0x7008 && ssw == wantSsw && fa == kLogical &&
              (!write || wb3a == kLogical),
              "68040 model=%u %s, /BERR at physical %08X: format %04X SSW %04X "
              "(want %04X), fault address %08X (want the logical %08X), WB3A %08X "
              "(want the logical address on the write), %u trip(s)",
              unsigned(model), write ? "MOVE.L D0,(A0)" : "MOVE.L (A0),D0",
              c.holeAt, format, ssw, wantSsw, fa, kLogical, wb3a, c.holeTrips);
    }
}

// [B] MC68040UM-1993 §8.4.6.2 — the SSW's ATC bit "is set for an ATC fault
// due to a nonresident entry (bus error during table search or invalid
// descriptor encountered)"; §8.2.1 — a TEA "during a bus cycle used to
// access the translation tables" makes the internal access fault taken
// "when the aborted access is retried"; §3.5 — the ATC entry that search
// created makes the retried access abort. The board answers the page
// descriptor fetch with /BERR: the frame is an ATC fault on the operand's
// logical address, not a physical bus error on the descriptor.
void tableSearchBusError040()
{
    constexpr uint32_t kLogical = 0x10000100, kPageDescriptor = 0x6000;
    for (auto model : {moira::CPUModel::M68LC040, moira::CPUModel::M68040})
    for (bool write : {false, true}) {
        TestCpu c(model);
        const uint16_t opcode = write ? 0x2080 : 0x2010;    // MOVE.L D0,(A0) / (A0),D0
        p32(c, 2 * 4, kHandler);
        p16(c, kHandler, kNop); p16(c, kHandler + 2, kNop);
        stage(c, opcode, 0);
        c.setA(0, kLogical); c.setD(0, 0x89ABCDEF);
        // 4K pages; the page descriptor of logical $10000000 sits on the hole.
        // Code, vectors, stack and tables pass through the transparent $00xxxxxx.
        p32(c, 0x4020, 0x4202); p32(c, 0x4200, 0x6002);
        p32(c, kPageDescriptor, 0x00F00059);
        c.setURP040(0x4000); c.setSRP040(0x4000);
        c.setITT0(0x0000C040); c.setDTT0(0x0000C040);
        c.setTC040(0x8000);
        c.holeAt = kPageDescriptor;
        c.holeArmed = true;
        const bool contained = executeContained(c);
        const uint32_t sp = c.getSP();
        const uint16_t format = g16(c, sp + 6), ssw = g16(c, sp + 0x0C);
        const uint32_t fa = g32(c, sp + 0x14);
        const uint16_t wantSsw = write ? 0x0405 : 0x0505;  // ATC, TM=5, long
        CHECK(contained && c.holeTrips == 1 && c.getPC0() == kHandler &&
              format == 0x7008 && ssw == wantSsw && fa == kLogical,
              "68040 model=%u %s, /BERR on the page descriptor fetch: format %04X "
              "SSW %04X (want %04X, ATC), fault address %08X (want the logical "
              "%08X), %u trip(s)",
              unsigned(model), write ? "MOVE.L D0,(A0)" : "MOVE.L (A0),D0",
              format, ssw, wantSsw, fa, kLogical, c.holeTrips);
    }
}

// A misaligned access that crosses into the next page reaches that page
// through its own translation. Logical page 0 maps to physical $33000 and
// page 1 to $35000, so the bytes after the boundary are not at phys + n.
// [B] MC68040UM-1993 §8.4.6.2: MA "is set if an ATC fault occurs for
// second-page access that spans two pages in memory", SIZE "corresponds to
// the original access size"; §8.4.6.4: the FA "contains the address of the
// first byte of the transfer, regardless of which of the two or three bus
// transfers for the misaligned access was faulted". MC68030UM-1989 §9
// (p. 9-1): "As the MMU receives each logical address from the CPU core, it
// searches the ATC".
// Not modelled: MC68040UM §4.1 (p. 4-3), "the partial access to the first
// page always happens twice". The core translates both pages before the
// first data cycle and transfers the first part once, or not at all when
// the second page faults.
// [F] For an invalid second page the 68030 frame carries the logical
// address of that page (where a PTEST finds the fault); MC68030UM §8.2 does
// not describe the case. This pins that address and DF only, not the SIZE
// field, which is open (original size, or the remaining 1-3 bytes of the
// faulted cycle as MC68030UM Figure 8-9 may be read).
void misalignedAccessAcrossPages()
{
    constexpr uint32_t kPage0 = 0x33000, kPage1 = 0x35000;
    struct Setup { moira::CPUModel model; const char* name; uint32_t base; };
    const Setup setups[] = {
        { moira::CPUModel::M68030,   "68030",   0x40000000 },
        { moira::CPUModel::M68LC040, "68LC040", 0x10000000 },
        { moira::CPUModel::M68040,   "68040",   0x10000000 },
    };
    const auto map = [&](TestCpu& c, const Setup& s, bool secondValid) {
        const bool m040 = s.model >= moira::CPUModel::M68EC040;
        if (m040) {
            // 4K pages; root $4000, pointer table $4200, page table $6000.
            p32(c, 0x4020, 0x4202); p32(c, 0x4200, 0x6002);
            p32(c, 0x6000, kPage0 | 0x59);
            p32(c, 0x6004, secondValid ? (kPage1 | 0x59) : 0);
            c.setURP040(0x4000); c.setSRP040(0x4000);
            c.setITT0(0x0000C040); c.setDTT0(0x0000C040);
            c.setTC040(0x8000);
        } else {
            // 4K pages, TIA = TIB = 10 bits; TT0/TT1 cover $00xxxxxx.
            constexpr uint32_t kTc = 0x80000000u | 0xCu << 20 | 0xAu << 12 | 0xAu << 8;
            p32(c, 0x30000 + 0x100 * 4, 0x31000 | 2);       // TIA $100 -> TIB table
            p32(c, 0x31000, kPage0 | 1);
            p32(c, 0x31004, secondValid ? (kPage1 | 1) : 0);
            c.mmu030(kTc, uint64_t(0x7FFF0002u) << 32 | 0x30000,
                     0x00008124u, 0x00008114u);
        }
    };
    for (const auto& s : setups)
    for (unsigned offset : {0xFFDu, 0xFFEu, 0xFFFu})
    for (bool longSize : {true, false})
    for (bool write : {false, true}) {
        if (!longSize && offset != 0xFFF) continue;     // a word crosses at $FFF only
        const unsigned bytes = longSize ? 4 : 2, first = 0x1000 - offset;
        const uint32_t logical = s.base + offset;
        // MOVE.L/W (A0),D1 or MOVE.L/W D1,(A0)
        const uint16_t opcode = uint16_t((longSize ? 0x2000 : 0x3000) |
                                         (write ? 0x0081 : 0x0210));
        TestCpu c(s.model);
        p32(c, 2 * 4, kHandler);
        p16(c, kHandler, kNop);
        stage(c, opcode, 0);
        for (unsigned i = 0; i < 0x2000; i++) {
            c.mem[kPage0 + i] = uint8_t(0xA0 + i % 16);  // page 0 and the page after it
            c.mem[kPage1 + i] = uint8_t(0x50 + i % 16);  // page 1
        }
        map(c, s, true);
        c.setA(0, logical); c.setD(1, 0x89ABCDEF);
        const bool contained = executeContained(c);
        uint32_t want = 0, got = 0;
        bool memoryOk = true;
        for (unsigned i = 0; i < bytes; i++) {
            const uint32_t pa = i < first ? kPage0 + offset + i : kPage1 + (i - first);
            const uint8_t data = uint8_t(0x89ABCDEFu >> (8 * (bytes - 1 - i)));
            if (write) memoryOk &= c.mem[pa] == data;
            want = want << 8 | (write ? data : c.mem[pa]);
        }
        // Nothing lands on the page that physically follows page 0.
        for (unsigned i = 0; i < 4; i++) memoryOk &= c.mem[kPage0 + 0x1000 + i] == uint8_t(0xA0 + i);
        got = write ? want : (longSize ? c.getD(1) : c.getD(1) & 0xFFFF);
        if (!write) want = longSize ? want : want & 0xFFFF;
        CHECK(contained && c.getPC0() == kCode + 2 && got == want && memoryOk,
              "%s %s.%c across pages at logical %08X: PC0=%08X, %s %08X (want %08X), "
              "page boundary bytes %s",
              s.name, write ? "MOVE D1,(A0)" : "MOVE (A0),D1", longSize ? 'L' : 'W',
              logical, c.getPC0(), write ? "wrote" : "read", got, want,
              memoryOk ? "ok" : "WRONG");

        // The second page is invalid: an ATC fault on its access
        TestCpu f(s.model);
        p32(f, 2 * 4, kHandler);
        p16(f, kHandler, kNop);
        stage(f, opcode, 0);
        map(f, s, false);
        f.setA(0, logical); f.setD(1, 0x89ABCDEF);
        const bool faultContained = executeContained(f);
        const uint32_t sp = f.getSP();
        const uint16_t format = g16(f, sp + 6);
        if (s.model >= moira::CPUModel::M68EC040) {
            const uint16_t ssw = g16(f, sp + 0x0C);
            const uint32_t fa = g32(f, sp + 0x14);
            const uint16_t wantSsw = uint16_t(0x0C05 | (write ? 0 : 0x0100) |
                                              (longSize ? 0 : 0x0040));  // MA ATC, TM=5
            CHECK(faultContained && f.getPC0() == kHandler && format == 0x7008 &&
                  ssw == wantSsw && fa == logical,
                  "%s %s.%c across into an invalid page at %08X: PC0=%08X format %04X "
                  "SSW %04X (want %04X, MA), fault address %08X (want the first byte %08X)",
                  s.name, write ? "MOVE D1,(A0)" : "MOVE (A0),D1", longSize ? 'L' : 'W',
                  logical, f.getPC0(), format, ssw, wantSsw, fa, logical);
        } else {
            const uint16_t ssw = g16(f, sp + 0x0A);
            const uint32_t fa = g32(f, sp + 0x10);
            CHECK(faultContained && f.getPC0() == kHandler &&
                  (format & 0xF000) >= 0xA000 && (ssw & 0x0100) &&
                  fa == s.base + 0x1000,
                  "%s %s.%c across into an invalid page at %08X: PC0=%08X format %04X "
                  "SSW %04X (want DF), fault address %08X (want the second page %08X)",
                  s.name, write ? "MOVE D1,(A0)" : "MOVE (A0),D1", longSize ? 'L' : 'W',
                  logical, f.getPC0(), format, ssw, fa, s.base + 0x1000);
        }
    }
}

// Siblings of misalignedAccessAcrossPages: what must not change.
// (a) A misaligned long word inside one page reads and writes the same bytes
//     and walks the tables exactly as an aligned access to the same page
//     does: no lookup of the next page (queue patch 19 only translates an
//     access that runs past a 256-byte boundary).
// (b) A crossing into a page that follows the first one physically keeps
//     the old bus path: two word cycles at phys and phys + 2.
// (c) A crossing write into a write-protected page on the 68040 is an ATC
//     fault with MA. [B] MC68040UM-1993 §8.4.6.2: ATC is set for a
//     "privilege violation (write protected or supervisor only)", MA for a
//     "second-page access that spans two pages". The first page is left
//     unwritten: the core translates both pages first, where §4.1 has the
//     processor access the first page (twice); that order is not modelled.
void misalignedAccessUnchangedPaths()
{
    constexpr uint32_t kPage0 = 0x33000;
    struct Setup { moira::CPUModel model; const char* name; uint32_t base, tables; };
    const Setup setups[] = {
        { moira::CPUModel::M68030,   "68030",   0x40000000, 0x31000 },
        { moira::CPUModel::M68LC040, "68LC040", 0x10000000, 0x6000 },
        { moira::CPUModel::M68040,   "68040",   0x10000000, 0x6000 },
    };
    const auto map = [&](TestCpu& c, const Setup& s, uint32_t page1) {
        if (s.model >= moira::CPUModel::M68EC040) {
            p32(c, 0x4020, 0x4202); p32(c, 0x4200, 0x6002);
            p32(c, 0x6000, kPage0 | 0x59);
            p32(c, 0x6004, page1 | 0x59);
            c.setURP040(0x4000); c.setSRP040(0x4000);
            c.setITT0(0x0000C040); c.setDTT0(0x0000C040);
            c.setTC040(0x8000);
        } else {
            constexpr uint32_t kTc = 0x80000000u | 0xCu << 20 | 0xAu << 12 | 0xAu << 8;
            p32(c, 0x30000 + 0x100 * 4, 0x31000 | 2);
            p32(c, 0x31000, kPage0 | 1);
            p32(c, 0x31004, page1 | 1 | (page1 & 4));      // bit 2: WP
            c.mmu030(kTc, uint64_t(0x7FFF0002u) << 32 | 0x30000,
                     0x00008124u, 0x00008114u);
        }
    };
    const auto run = [&](TestCpu& c, const Setup& s, uint32_t page1, uint16_t opcode,
                         uint32_t logical, uint32_t traceAt) {
        p32(c, 2 * 4, kHandler);
        p16(c, kHandler, kNop);
        stage(c, opcode, 0);
        for (unsigned i = 0; i < 0x2000; i++) c.mem[kPage0 + i] = uint8_t(0xA0 + i % 16);
        map(c, s, page1);
        c.setA(0, logical); c.setD(1, 0x89ABCDEF);
        c.traceBus = true; c.busBase = traceAt; c.bus.clear();
        return executeContained(c);
    };
    for (const auto& s : setups)
    for (bool write : {false, true}) {
        const uint16_t opcode = write ? 0x2081 : 0x2210;    // MOVE.L D1,(A0) / (A0),D1
        // (a) in one page: the descriptor traffic of an aligned access
        TestCpu ref(s.model);
        run(ref, s, 0x35000, opcode, s.base + 0x800, s.tables);
        for (unsigned offset : {0x801u, 0x802u, 0x803u}) {
            TestCpu c(s.model);
            const bool contained = run(c, s, 0x35000, opcode, s.base + offset, s.tables);
            uint32_t want = 0;
            for (unsigned i = 0; i < 4; i++)
                want = want << 8 | c.mem[kPage0 + offset + i];
            const uint32_t got = write ? 0x89ABCDEF : c.getD(1);
            CHECK(contained && c.getPC0() == kCode + 2 && got == want &&
                  !ref.bus.empty() && c.bus == ref.bus,
                  "%s MOVE.L %s at page offset %03X: PC0=%08X, %08X (want %08X), "
                  "descriptor cycles \"%s\" (want those of an aligned access, \"%s\")",
                  s.name, write ? "D1,(A0)" : "(A0),D1", offset, c.getPC0(), got,
                  want, c.bus.c_str(), ref.bus.c_str());
        }
        // (b) crossing into the physically next page: the old two-word path
        {
            TestCpu c(s.model);
            const bool contained = run(c, s, kPage0 + 0x1000, opcode, s.base + 0xFFD,
                                       kPage0 + 0xFF0);
            uint32_t want = 0;
            for (unsigned i = 0; i < 4; i++) want = want << 8 | c.mem[kPage0 + 0xFFD + i];
            const uint32_t got = write ? 0x89ABCDEF : c.getD(1);
            const char* wantBus = write ? "W13 W15" : "w13 w15";
            CHECK(contained && c.getPC0() == kCode + 2 && got == want && c.bus == wantBus,
                  "%s MOVE.L %s across into the contiguous page: PC0=%08X, %08X "
                  "(want %08X), bus \"%s\" (want \"%s\")",
                  s.name, write ? "D1,(A0)" : "(A0),D1", c.getPC0(), got, want,
                  c.bus.c_str(), wantBus);
        }
        // (c) a crossing write into a write-protected page (68040)
        if (write && s.model >= moira::CPUModel::M68EC040) {
            TestCpu c(s.model);
            const bool contained = run(c, s, 0x35000 | 0x04, opcode, s.base + 0xFFE,
                                       kPage0 + 0xFF0);
            const uint32_t sp = c.getSP();
            const uint16_t ssw = g16(c, sp + 0x0C);
            const uint32_t fa = g32(c, sp + 0x14);
            CHECK(contained && c.getPC0() == kHandler && g16(c, sp + 6) == 0x7008 &&
                  ssw == 0x0C05 && fa == s.base + 0xFFE &&
                  c.mem[kPage0 + 0xFFE] == 0xAE && c.mem[kPage0 + 0xFFF] == 0xAF,
                  "%s MOVE.L D1,(A0) across into a write-protected page: PC0=%08X "
                  "SSW %04X (want 0C05: MA, ATC, write, long), fault address %08X "
                  "(want %08X), first page %02X%02X (want AEAF, unwritten)",
                  s.name, c.getPC0(), ssw, fa, s.base + 0xFFE,
                  c.mem[kPage0 + 0xFFE], c.mem[kPage0 + 0xFFF]);
        }
    }
}

void ordinaryStoreStillAdvances040()
{
    // [D] WinUAE cpuemu_31.c @ Hatari e77819f7: ordinary MOVE stores
    // call gen_set_fault_pc. The MOVEP exception must not capture them.
    unsigned sequences = 0;
    for (auto model : {moira::CPUModel::M68LC040, moira::CPUModel::M68040})
    for (unsigned mmu = 0; mmu < 3; ++mmu)
    for (bool cache : {false, true})
    for (uint16_t opcode : {0x1343, 0x3343, 0x2343}) { // MOVE.B/W/L D3,d16(A1)
        ++sequences;
        TestCpu c(model);
        p32(c, 8, kHandler);
        p16(c, kHandler, kNop); p16(c, kHandler + 2, kNop);
        stage(c, opcode, 0x1F);
        p16(c, kCode + 2, 0x54F5); c.setIRC(0x54F5);
        c.setD(3, 0x89ABCDEF);
        c.setA(1, (mmu ? 0x10000000u : kHole) - 0x54F5);
        c.setCACR(cache ? 0x80008000 : 0);
        if (mmu) {
            p32(c, 0x4020, 0x4202); p32(c, 0x4200, 0x6002); // invalid page
            c.setURP040(0x4000); c.setSRP040(0x4000);
            c.setITT0(0x0000C000); c.setDTT0(0x0000C000);
            c.setTC040(mmu == 2 ? 0xC000 : 0x8000);
        } else {
            c.holeArmed = true;
        }
        CHECK(executeContained(c) && !c.isHalted() && c.getPC0() == kHandler &&
              g32(c, c.getSP() + 2) == kCode + 4 && (c.getSR() & 31) == 0x18,
              "MOVE %04X final-store control changed, model=%u mmu=%u cache=%u",
              opcode, unsigned(model), mmu, cache);
    }
    std::printf("  ordinary 040 final-store controls: %u sequences\n", sequences);
}

void ttrTableFollowsRegisters()
{
    // mmu040MatchTTR answers from a table derived from the TTRs, and the
    // table has no hook on the registers: it must notice a change by
    // itself, in either register of a pair. Seen from outside through the
    // one thing a data TTR decides with the MMU off — a write-protected
    // match faults the write. One CPU object, so each step meets the table
    // the previous one left.
    struct Step { uint32_t dtt0, dtt1; bool faults; const char* what; };
    static const Step steps[] = {
        { 0x4000c004, 0,          true,  "DTT0 = $40, both privileges, W" },
        { 0x4000c000, 0,          false, "DTT0 = the same without W" },
        { 0,          0x4000a004, true,  "DTT1 = $40, supervisor only, W" },
        { 0,          0x40008004, false, "DTT1 = $40, user only, W" },
        { 0x5000c004, 0x4000a004, true,  "DTT0 misses, DTT1 matches with W" },
        { 0x4000c000, 0x4000a004, false, "DTT0 matches first, without W" },
    };
    // The operand sits at $40003000 (the test board mirrors its megabyte);
    // the stack stays in $00, outside every register above, so the frame
    // of the fault can be written.
    TestCpu c(moira::CPUModel::M68040);
    for (const Step& k : steps) {
        p32(c, 2 * 4, kHandler);
        p16(c, kHandler, kNop); p16(c, kHandler + 2, kNop);
        stage(c, 0x3080, 0);                        // MOVE.W D0,(A0)
        p16(c, 0x3000, 0x1111);
        c.setA(0, 0x40003000);
        c.setD(0, 0x2222);
        c.setDTT0(k.dtt0);
        c.setDTT1(k.dtt1);
        const bool contained = executeContained(c);
        CHECK(contained, "68040 TTR %s: the fault left execute()", k.what);
        if (!contained) continue;
        const bool faulted = c.getPC0() == kHandler;
        const uint16_t want = k.faults ? 0x1111 : 0x2222;
        CHECK(faulted == k.faults && g16(c, 0x3000) == want,
              "68040 TTR %s: %s, memory %04X (want %s, %04X)",
              k.what, faulted ? "faulted" : "wrote", g16(c, 0x3000),
              k.faults ? "a fault" : "a write", want);
    }
}

void clrReadsOnlyOn68000()
{
    // CLR.B (A0): the 68000 reads the byte before clearing it, the 68020
    // and later do not ([B] M68000PRM, CLR: "In the MC68000 and MC68008 a
    // memory location is read before it is cleared"). The rig logs an 'r'
    // or a 'w' per byte cycle; the instruction fetch is a word and logs
    // nothing.
    static const struct { moira::CPUModel model; const char* name; const char* want; } rows[] = {
        { moira::CPUModel::M68000,   "68000",   "rw" },
        { moira::CPUModel::M68020,   "68020",   "w"  },
        { moira::CPUModel::M68030,   "68030",   "w"  },
        { moira::CPUModel::M68LC040, "68LC040", "w"  },
    };
    for (const auto& row : rows) {
        TestCpu c(row.model);
        stage(c, 0x4210, 0);                        // CLR.B (A0)
        c.mem[0x3000] = 0x5a;
        c.setA(0, 0x3000);
        c.events.clear();
        c.execute();
        CHECK(c.events == row.want && c.mem[0x3000] == 0 && (c.getCCR() & 0x0f) == 0x04,
              "%s CLR.B (A0): bus cycles \"%s\" (want \"%s\"), memory %02X, CCR %02X",
              row.name, c.events.c_str(), row.want, c.mem[0x3000], c.getCCR());
    }
}

void bitfieldMemoryAccesses()
{
    // A memory bit field reads only the bytes it spans — 1, 2, 2+1, 4 or
    // 4+1 — finishes every read before its first write, and writes back the
    // same shape. [B] MC68030UM-1989 §11.6.14: one operand cycle below five
    // bytes, two at five. [D] WinUAE get_bitfield/put_bitfield @ Hatari
    // e77819f7 for the byte decomposition (POM68K_VENDOR.md, row 79). The
    // longword-always core read and rewrote bytes outside the field and
    // wrote the first four bytes before reading the fifth.
    constexpr uint32_t kField = 0x3000;
    struct Case {
        uint16_t opcode, ext; const char* what; const char* want;
        uint8_t before[5], after[5]; uint32_t d1;
    };
    static const Case cases[] = {
        { 0xe9d0, 0x1008, "BFEXTU (A0){0:8},D1", "b0",
          {0xa5}, {0xa5}, 0xa5 },
        { 0xe9d0, 0x1108, "BFEXTU (A0){4:8},D1", "w0",
          {0x12, 0x34}, {0x12, 0x34}, 0x23 },
        { 0xe9d0, 0x1110, "BFEXTU (A0){4:16},D1", "w0 b2",
          {0x12, 0x34, 0x56}, {0x12, 0x34, 0x56}, 0x2345 },
        { 0xe9d0, 0x1000, "BFEXTU (A0){0:32},D1", "w0 w2",
          {1, 2, 3, 4}, {1, 2, 3, 4}, 0x01020304 },
        { 0xe9d0, 0x1040, "BFEXTU (A0){1:32},D1", "w0 w2 b4",
          {0x80, 0, 0, 0, 0x80}, {0x80, 0, 0, 0, 0x80}, 0x00000001 },
        { 0xead0, 0x00de, "BFCHG (A0){3:30}", "w0 w2 b4 W0 W2 B4",
          {0}, {0x1f, 0xff, 0xff, 0xff, 0x80}, 0 },
        { 0xefd0, 0x1304, "BFINS D1,(A0){12:4}", "b1 B1",
          {0xff, 0xff, 0xff}, {0xff, 0xfa, 0xff}, 0xa },
    };
    static const struct { moira::CPUModel model; const char* name; } models[] = {
        { moira::CPUModel::M68020, "68020" }, { moira::CPUModel::M68030, "68030" },
        { moira::CPUModel::M68LC040, "68LC040" },
    };
    for (const auto& m : models) {
        for (const Case& k : cases) {
            TestCpu c(m.model);
            stage(c, k.opcode, 0);
            p16(c, kCode + 2, k.ext);
            c.setIRC(k.ext);
            for (unsigned i = 0; i < 8; i++) c.mem[kField + i] = i < 5 ? k.before[i] : 0;
            c.setA(0, kField);
            const bool readsD1 = (k.opcode & 0xffc0) == 0xefc0;
            c.setD(1, readsD1 ? k.d1 : 0xdeadbeef);
            c.traceBus = true; c.busBase = kField;
            c.execute();
            bool memoryOk = true;
            for (unsigned i = 0; i < 5; i++) memoryOk &= c.mem[kField + i] == k.after[i];
            const bool extracts = (k.opcode & 0xffc0) == 0xe9c0;
            CHECK(c.bus == k.want && memoryOk && c.getPC0() == kCode + 4 &&
                  (!extracts || c.getD(1) == k.d1),
                  "%s %s: bus \"%s\" (want \"%s\"), memory %s, D1 %08X, PC0 %08X",
                  m.name, k.what, c.bus.c_str(), k.want,
                  memoryOk ? "ok" : "wrong", c.getD(1), c.getPC0());
        }

        // The fifth byte answers /BERR: nothing may have been written.
        TestCpu c(m.model);
        p32(c, 2 * 4, kHandler);
        p16(c, kHandler, kNop);
        stage(c, 0xead0, 0);                        // BFCHG (A0){4:32}
        p16(c, kCode + 2, 0x0100);
        c.setIRC(0x0100);
        const uint32_t base = kHole - 4;
        for (unsigned i = 0; i < 4; i++) c.mem[base + i] = uint8_t(0x11 * (i + 1));
        c.setA(0, base);
        c.holeArmed = true;
        c.traceBus = true; c.busBase = base;
        const bool contained = executeContained(c);
        bool unchanged = true;
        for (unsigned i = 0; i < 4; i++) unchanged &= c.mem[base + i] == uint8_t(0x11 * (i + 1));
        CHECK(contained && c.holeTrips == 1 && unchanged &&
              c.bus.find_first_of("BW") == std::string::npos,
              "%s BFCHG (A0){4:32} with /BERR on the fifth byte: bus \"%s\", "
              "first four bytes %s, %u trip(s)", m.name, c.bus.c_str(),
              unchanged ? "unchanged" : "WRITTEN", c.holeTrips);
    }
}

void ptestFlagsInvalidAtTheLimit()
{
    // PTESTR #5,(A0),#1 with the level-1 descriptor invalid (DT = 0): the
    // search ends at the limit AND on an invalid descriptor, and MMUSR must
    // say both — N = 1, I. The fuzz generator's tree: TC = E, PS = 15,
    // IS = 8, TIA = 9, one level of short descriptors at $4000; CRP points
    // there with DT = 2 (valid 4-byte). Found by "mmu/tt seed=1006 #1188".
    TestCpu c(moira::CPUModel::M68030);
    p32(c, 11 * 4, kHandler);
    stage(c, 0xf010, 0);                            // PTEST (A0)
    p16(c, kCode + 2, 0x8615);                      // PTESTR, level 1, FC #5
    c.setIRC(0x8615);
    p16(c, kCode + 4, kNop);
    c.setTC(0x80f89000u);
    c.setCRP(0x7fff000200004000ULL);
    p32(c, 0x4000, 0x00000001);                     // page 0 (code, table): identity
    p32(c, 0x4004, 0x00008001);                     // page 1 (stack): identity
    c.setA(0, 0x000a9a5d);                          // page index $15 -> $4054
    p32(c, 0x4054, 0);                              // invalid descriptor
    c.setMMUSR(0);
    c.execute();
    CHECK(c.getPC0() == kCode + 4 && c.getMMUSR() == 0x0401,
          "68030 PTESTR level 1 into DT=0: PC0=%08X, MMUSR=%04X (want $0401)",
          c.getPC0(), c.getMMUSR());
}

void ptestLongDescriptorLimits()
{
    // [D] WinUAE cpummu030.c mmu030_table_search @ Hatari e77819f7:
    // limits are inclusive; a violation stops BEFORE fetching the child.
    // Cover root and table limits, both bound directions, both PTEST R/W,
    // and stopping at level one without consulting the next table's limit.
    for (bool rootLimit : {false, true}) for (bool lower : {false, true})
    for (int delta : {-1, 0, 1}) for (bool read : {false, true})
    for (int depth : {1, 2}) {
        TestCpu c(moira::CPUModel::M68030);
        stage(c, 0xf010, 0);                       // PTEST (A0)
        // [B] MC68030UM-1989 PTEST p. 3-160: FC is %10XXX (immediate),
        // %01DDD, %00000 (SFC) or %00001 (DFC); §8.1.5 makes any other
        // pattern an F-line. %10101 is the immediate #5.
        const uint16_t modes = uint16_t(0x8100 | (depth << 10) |
                                       (read ? 0x0200 : 0) | (1 << 5) | 0x15);
        p16(c, kCode + 2, modes); c.setIRC(modes);  // result descriptor -> A1
        p16(c, kCode + 4, kNop);
        // TC.E=0 keeps code/stack accesses out of the synthetic tables;
        // PTEST still walks them (D5). IS=8, TIA=TIB=6, PS=12.
        c.setTC(0x00c86600u);
        constexpr uint32_t root = 0x4000, leaf = 0x5000;
        const uint32_t boundary = 2;
        const uint32_t index = boundary + delta;
        const uint32_t bounded = (lower ? 0x80000000u : 0) |
                                 (boundary << 16) | 3;
        c.setCRP((uint64_t(rootLimit ? bounded : 0x7fff0003u) << 32) | root);
        const uint32_t rootIndex = rootLimit ? index : 1;
        const uint32_t leafIndex = rootLimit ? 1 : index;
        const uint32_t parent = root + rootIndex * 8;
        const uint32_t child = leaf + leafIndex * 8;
        p32(c, parent, rootLimit ? 0x7fff0003u : bounded);
        p32(c, parent + 4, leaf);
        p32(c, child, 1); p32(c, child + 4, 0x30000);
        c.setA(0, (rootIndex << 18) | (leafIndex << 12));
        const bool violation = (lower ? delta < 0 : delta > 0) &&
                               (rootLimit || depth > 1);
        const uint16_t want = violation ? uint16_t(0x4400 | (rootLimit ? 0 : 1))
                                        : uint16_t(depth);
        const uint32_t address = violation ? (rootLimit ? 0 : parent)
                                           : (depth == 1 ? parent : child);
        c.execute();
        CHECK(c.getMMUSR() == want && c.getA(1) == address &&
              c.getPC0() == kCode + 4 &&
              g32(c, parent) == (rootLimit ? 0x7fff0003u : bounded) &&
              g32(c, child) == 1,
              "68030 PTEST %s %s delta=%d R=%d level=%d: MMUSR=%04X/%04X "
              "descriptor=%08X/%08X", rootLimit ? "root" : "table",
              lower ? "lower" : "upper", delta, read, depth,
              c.getMMUSR(), want, c.getA(1), address);
    }
}

void readonlyWalk030(const TestCpu& c, uint32_t logical, uint8_t fc,
                     uint16_t expectedStatus, uint32_t expectedPage)
{
    uint32_t page = 0, descriptor = 0;
    uint16_t status = 0;
    int count = 0, writes = 0;
    bool ci = false;
    pom68k::mmu030::tableWalk<false>(c.getTC(), c.getCRP(), c.getSRP(),
        logical, fc, false, 0, [&](uint32_t a) { return g32(c, a); },
        [&](uint32_t, uint32_t) { ++writes; }, page, ci, status, descriptor, count);
    CHECK(status == expectedStatus && writes == 0 &&
          ((status & 0xC400) || page == expectedPage),
          "read-only 030 walk: status=%04X/%04X page=%08X/%08X writes=%d",
          status, expectedStatus, page, expectedPage, writes);
}

void ptestEarlyAndIndirect030()
{
    // [D] WinUAE mmu030_table_search @ Hatari e77819f7: early page
    // bounds apply even at the requested level; indirection follows only
    // below it and its DT selects the target descriptor's size.
    for (int stride : {4, 8}) for (bool lower : {false, true})
    for (int delta : {-1, 0, 1}) for (int depth : {1, 2})
    for (int fc : {1, 5}) {
        TestCpu c(moira::CPUModel::M68030);
        stage(c, 0xf010, 0);
        const uint16_t ext = uint16_t(0x8130 | (depth << 10) | fc);
        p16(c, kCode + 2, ext); c.setIRC(ext);
        c.setTC(0x00c86600u);
        c.setCRP((uint64_t(0x7fff0000u | (stride == 8 ? 3 : 2)) << 32) | 0x4000);
        const uint32_t entry = 0x4000 + stride;
        const uint32_t flags = 0x115 | (lower ? 0x80000000u : 0) | (2 << 16);
        p32(c, entry, stride == 8 ? flags : 0x30015);
        if (stride == 8) p32(c, entry + 4, 0x30000);
        c.setA(0, (1 << 18) | (uint32_t(2 + delta) << 12));
        c.execute();
        uint16_t want = uint16_t(0x0a01 | (stride == 8 && fc == 1 ? 0x2000 : 0));
        if (stride == 8 && (lower ? delta < 0 : delta > 0)) want |= 0x4400;
        CHECK(c.getMMUSR() == want && c.getA(1) == entry &&
              c.getPC0() == kCode + 4 && g32(c, entry) == (stride == 8 ? flags : 0x30015),
              "PTEST early stride=%d lower=%d delta=%d depth=%d FC=%d: "
              "MMUSR=%04X/%04X A1=%08X/%08X", stride, lower, delta, depth, fc,
              c.getMMUSR(), want, c.getA(1), entry);
        readonlyWalk030(c, c.getA(0), uint8_t(fc), uint16_t(want & 0xFFF0),
                        0x30000 + (uint32_t(2 + delta) << 12));
    }
    for (int stride : {4, 8}) for (int targetStride : {4, 8})
    for (int kind : {0, 1, 2, 3}) for (int depth : {1, 2, 3, 4})
    for (int fc : {1, 5}) {
        TestCpu c(moira::CPUModel::M68030);
        stage(c, 0xf010, 0);
        const uint16_t ext = uint16_t(0x8330 | (depth << 10) | fc);
        p16(c, kCode + 2, ext); c.setIRC(ext);
        c.setTC(0x00c86600u);
        const uint32_t tableFlags = 0x7fff0000u | (stride == 8 ? 3 : 2);
        c.setCRP((uint64_t(tableFlags) << 32) | 0x4000);
        const uint32_t parent = 0x4000 + stride, leaf = 0x5000 + 2 * stride;
        const uint32_t target = targetStride == 8 ? 0x6008 : 0x6004;
        p32(c, parent, stride == 8 ? tableFlags : 0x5002);
        if (stride == 8) p32(c, parent + 4, 0x5000);
        const uint32_t pointer = targetStride == 8 ? 3 : 2;
        p32(c, leaf, stride == 8 ? pointer : target | pointer);
        if (stride == 8) p32(c, leaf + 4, target);
        const uint32_t flags = uint32_t(kind) | (targetStride == 8 ? 0x114 : 0x14);
        p32(c, target, targetStride == 8 ? flags : 0x30000 | flags);
        if (targetStride == 8) p32(c, target + 4, 0x30000);
        c.setA(0, (1 << 18) | (2 << 12));
        c.execute();
        const uint32_t address = depth == 1 ? parent : depth == 2 ? leaf : target;
        const uint16_t want = depth < 3 ? uint16_t(depth) : kind != 1 ? 0x0403 :
            uint16_t(0x0a03 | (targetStride == 8 && fc == 1 ? 0x2000 : 0));
        CHECK(c.getMMUSR() == want && c.getA(1) == address && c.getPC0() == kCode + 4 &&
              g32(c, leaf) == (stride == 8 ? pointer : target | pointer) &&
              g32(c, target) == (targetStride == 8 ? flags : 0x30000 | flags),
              "PTEST indirect stride=%d target=%d DT=%d depth=%d FC=%d: "
              "MMUSR=%04X/%04X A1=%08X/%08X", stride, targetStride, kind, depth, fc,
              c.getMMUSR(), want, c.getA(1), address);
        readonlyWalk030(c, c.getA(0), uint8_t(fc), kind != 1 ? 0x0400 :
            uint16_t(0x0a00 | (targetStride == 8 && fc == 1 ? 0x2000 : 0)), 0x30000);
    }
}

void ptestDescriptorFaults030()
{
    // [D] WinUAE cpummu030.c @ Hatari e77819f7: a descriptor-read
    // /BERR excludes that descriptor from N and returns the preceding one.
    // A PTEST stopped above the armed hole must not touch it at all.
    for (int stride : {4, 8}) for (int targetStride : {4, 8})
    for (int faultLevel : {1, 2, 3}) for (int depth : {1, 2, 3, 4}) {
        const int faultSize = faultLevel == 3 ? targetStride : stride;
        for (int offset = 0; offset < faultSize; offset += 4) {
            TestCpu c(moira::CPUModel::M68030);
            stage(c, 0xf010, 0);
            const uint16_t ext = uint16_t(0x8335 | (depth << 10));
            p16(c, kCode + 2, ext); c.setIRC(ext);
            c.setTC(0x00c86600u);
            const uint32_t flags = 0x7fff0000u | (stride == 8 ? 3 : 2);
            c.setCRP((uint64_t(flags) << 32) | 0x4000);
            const uint32_t parent = 0x4000 + stride, leaf = 0x5000 + 2 * stride;
            const uint32_t target = targetStride == 8 ? 0x6008 : 0x6004;
            p32(c, parent, stride == 8 ? flags : 0x5002);
            if (stride == 8) p32(c, parent + 4, 0x5000);
            const uint32_t pointer = targetStride == 8 ? 3 : 2;
            p32(c, leaf, stride == 8 ? pointer : target | pointer);
            if (stride == 8) p32(c, leaf + 4, target);
            p32(c, target, targetStride == 8 ? 1 : 0x30001);
            if (targetStride == 8) p32(c, target + 4, 0x30000);
            const uint32_t addresses[] = {0, parent, leaf, target};
            c.holeArmed = true; c.holeAt = addresses[faultLevel] + offset;
            c.setA(0, (1 << 18) | (2 << 12));
            c.execute();
            const bool faults = faultLevel <= depth;
            const int complete = faults ? faultLevel - 1 : depth;
            const uint16_t want = uint16_t((faults ? 0x8400 : 0) | complete);
            CHECK(c.getMMUSR() == want && c.getA(1) == addresses[complete] &&
                  c.getPC0() == kCode + 4 && bool(c.holeTrips) == faults,
                  "PTEST descriptor fault stride=%d target=%d level=%d word=%d depth=%d: "
                  "MMUSR=%04X/%04X A1=%08X/%08X trips=%u", stride, targetStride,
                  faultLevel, offset, depth, c.getMMUSR(), want,
                  c.getA(1), addresses[complete], c.holeTrips);
        }
    }
}

void directRootFclLimits030()
{
    // [F] MC68030UM-1989 §9.7.1 keeps a DT=1 root's limit active with FCL
    // ("regardless of the state of the FCL bit"); its Figure 9-28 skips the
    // check for y = RP with FCL set, as WinUAE and Dirk's core do. The FCL
    // rows are listed in tests/known/moira_rules_test.txt until a real 68030
    // settles it.
    for (bool fcl : {false, true}) for (bool lower : {false, true})
    for (int delta : {-1, 0, 1}) for (int depth : {1, 7})
    for (int fc : {1, 5}) for (bool read : {false, true}) {
        TestCpu c(moira::CPUModel::M68030);
        stage(c, 0xf010, 0);
        const uint16_t ext = uint16_t(0x8130 | (depth << 10) | (read ? 0x200 : 0) | fc);
        p16(c, kCode + 2, ext); c.setIRC(ext);
        c.setTC(0x00c86600u | (fcl ? 0x01000000u : 0));
        const uint32_t flags = (lower ? 0x80000000u : 0) | (2 << 16) | 1;
        c.setCRP((uint64_t(flags) << 32) | 0x30000);
        c.setA(0, (uint32_t(2 + delta) << 18) | 0x100);
        c.execute();
        const uint16_t want = (lower ? delta < 0 : delta > 0) ? 0x4400 : 0;
        CHECK(c.getMMUSR() == want && c.getA(1) == 0 && c.getPC0() == kCode + 4,
              "direct root PTEST FCL=%d lower=%d delta=%d depth=%d FC=%d R=%d: "
              "MMUSR=%04X/%04X A1=%08X", fcl, lower, delta, depth, fc, read,
              c.getMMUSR(), want, c.getA(1));
    }
    for (bool fcl : {false, true}) for (bool lower : {false, true})
    for (int delta : {-1, 0, 1}) for (bool read : {false, true}) {
        TestCpu c(moira::CPUModel::M68030);
        stage(c, 0x0e90, 0);
        p16(c, kCode + 2, read ? 0 : 0x0800);
        c.setTC(0x80c86600u | (fcl ? 0x01000000u : 0));
        const uint32_t flags = (lower ? 0x80000000u : 0) | (2 << 16) | 1;
        c.setCRP((uint64_t(flags) << 32) | 0x30000);
        c.setTT1(0x80008107);
        c.setISP(0x80008000); c.setSP(0x80008000); c.setVBR(0x80000000);
        p32(c, 8, 0x80009000);
        c.setSFC(1); c.setDFC(1);
        const uint32_t logical = (uint32_t(2 + delta) << 18) | 0x100;
        const uint32_t physical = logical + 0x30000;
        c.setA(0, logical); c.setD(0, 0xdeadbeef); p32(c, physical, 0x12345678);
        c.setPC(0x80000000u | kCode); c.setPC0(0x80000000u | kCode);
        c.setIRD(0x0e90); c.setIRC(read ? 0 : 0x0800);
        c.execute();
        const bool fault = lower ? delta < 0 : delta > 0;
        CHECK(c.getPC0() == (fault ? 0x80009000u : 0x80000000u | (kCode + 4)) &&
              g32(c, physical) == (fault || read ? 0x12345678u : 0xdeadbeefu) &&
              (fault || !read || c.getD(0) == 0x12345678),
              "direct root bus FCL=%d lower=%d delta=%d R=%d: PC=%08X RAM=%08X",
              fcl, lower, delta, read, c.getPC0(), g32(c, physical));
    }
}

void directRootWrites030()
{
    // [B] MC68030UM-1989 §9.7.1: DT=1 roots directly map with a
    // constant offset. No memory descriptor exists to acquire an M bit.
    for (int size : {1, 2, 4}) for (bool moves : {false, true})
    for (int fc : {1, 5}) {
        TestCpu c(moira::CPUModel::M68030);
        const uint16_t store = moves ? uint16_t(0x0e10 | (size == 1 ? 0 : size == 2 ? 0x40 : 0x80))
                                    : uint16_t(size == 1 ? 0x1080 : size == 2 ? 0x3080 : 0x2080);
        const uint16_t load = moves ? store : uint16_t(size == 1 ? 0x1010 : size == 2 ? 0x3010 : 0x2010);
        stage(c, store, 0);
        if (moves) p16(c, kCode + 2, 0x0800);
        c.setTC(0x82c86600u);
        c.setCRP((uint64_t(0x7fff0001u) << 32) | 0x20000);
        c.setSRP((uint64_t(0x7fff0001u) << 32) | 0x40000);
        c.setTT1(0x80008107u);
        c.setISP(0x80008000); c.setSP(0x80008000);
        c.setVBR(0x80000000);
        p32(c, 8, 0x80009000);
        const uint32_t physical = 0x10004 + (moves && fc == 1 ? 0x20000 : 0x40000);
        p32(c, 0x10004, 0x55443322);
        c.setA(0, 0x10004); c.setSFC(fc); c.setDFC(fc);
        for (int pass = 0; pass < 3; pass++) {
            const bool write = pass != 0;
            const uint16_t op = write ? store : load;
            p16(c, kCode, op);
            if (moves) p16(c, kCode + 2, write ? 0x0800 : 0);
            c.setA(0, 0x10004);
            const uint32_t value = 0x12345678 + pass;
            p32(c, physical, 0xabcdef01);
            c.setD(0, value);
            c.setPC(kCode | 0x80000000); c.setPC0(kCode | 0x80000000);
            c.setIRD(op); c.setIRC(moves ? (write ? 0x0800 : 0) : kNop);
            c.execute();
            const uint32_t want = !write ? 0xabcdef01 : size == 1 ? ((value & 255) << 24) | 0x00cdef01 :
                                  size == 2 ? (value << 16) | 0xef01 : value;
            CHECK(c.getPC0() == (0x80000000u | (kCode + (moves ? 4 : 2))) &&
                  c.getSP() == 0x80008000 && g32(c, physical) == want &&
                  g32(c, 0x10004) == 0x55443322 &&
                  c.getCRP() == ((uint64_t(0x7fff0001u) << 32) | 0x20000),
                  "direct root size=%d MOVES=%d FC=%d pass=%d: PC=%08X RAM=%08X/%08X",
                  size, moves, fc, pass, c.getPC0(), g32(c, physical), want);
            // Level-zero PTEST examines the ATC, with its raw page-aligned
            // tag (D5); a read leaves M clear, the following writes set it.
            const uint16_t probe = uint16_t(0x8210 | (moves ? fc : 5));
            p16(c, kCode + 8, 0xf010); p16(c, kCode + 10, probe);
            c.setA(0, 0x10000);
            c.setPC(0x80000000u | (kCode + 8)); c.setPC0(0x80000000u | (kCode + 8));
            c.setIRD(0xf010); c.setIRC(probe);
            c.execute();
            CHECK(c.getMMUSR() == (write ? 0x0200 : 0),
                  "direct root ATC M size=%d MOVES=%d FC=%d pass=%d: MMUSR=%04X",
                  size, moves, fc, pass, c.getMMUSR());
        }
    }
}

// [B] M68000PRM-1992 §2, §2.2.11; MC68030UM-1989 §2: a PC-relative operand
// read is a program reference — the 68000 to the 68030 read it with FC 6 in
// supervisor mode (ruling D46). [B] MC68040UM-1993 §8.4.6.7: the 68040
// converts it to a data reference (FC 5).
void pcRelativeOperandSpace()
{
    struct Row { moira::CPUModel model; unsigned fc; const char* name; };
    const Row rows[] = {
        { moira::CPUModel::M68000,   6, "68000" },
        { moira::CPUModel::M68010,   6, "68010" },
        { moira::CPUModel::M68EC020, 6, "68EC020" },
        { moira::CPUModel::M68020,   6, "68020" },
        { moira::CPUModel::M68EC030, 6, "68EC030" },
        { moira::CPUModel::M68030,   6, "68030" },
        { moira::CPUModel::M68LC040, 5, "68LC040" },
    };
    for (const Row& r : rows) {
        TestCpu c(r.model);
        stage(c, 0x303A, 0);                       // MOVE.W (d16,PC),D0
        p16(c, kCode + 2, 0x0010);
        c.setIRC(0x0010);
        const uint32_t ea = kCode + 2 + 0x10;
        p16(c, ea, 0xBEEF);
        c.logFc = true;
        c.execute();
        c.logFc = false;
        bool seen = false;
        for (const auto& a : c.fcLog)
            if ((a.addr & 0xFFFFFF) == ea && !a.write) {
                seen = true;
                CHECK(a.fc == r.fc, "%s: (d16,PC) operand read FC=%u, want %u",
                      r.name, unsigned(a.fc), r.fc);
            }
        CHECK(seen && (c.getD(0) & 0xFFFF) == 0xBEEF,
              "%s: operand read not observed (D0=%08X)", r.name, c.getD(0));
    }
    // MOVEM.W ($10,PC),D0: the transfer and the extra word read after it are
    // in the same space (NeoST e809c24). The 68030 and 68040 make no extra
    // read here (POM68K_VENDOR.md § MOVEM).
    for (const Row& r : rows) {
        TestCpu c(r.model);
        stage(c, 0x4CBA, 0);                       // MOVEM.W (d16,PC),D0
        p16(c, kCode + 2, 0x0001);
        p16(c, kCode + 4, 0x0010);
        c.setIRC(0x0001);
        const uint32_t ea = kCode + 4 + 0x10;
        p16(c, ea, 0xBEEF);
        c.logFc = true;
        c.execute();
        c.logFc = false;
        unsigned reads = 0;
        for (const auto& a : c.fcLog)
            if ((a.addr & 0xFFFFFF) >= ea && (a.addr & 0xFFFFFF) < ea + 4 && !a.write) {
                reads++;
                CHECK(a.fc == r.fc, "%s: MOVEM (d16,PC) read at %06X FC=%u, want %u",
                      r.name, unsigned(a.addr), unsigned(a.fc), r.fc);
            }
        const bool extra = r.model < moira::CPUModel::M68EC030 ||
                           r.model == moira::CPUModel::M68EC030;
        CHECK(reads == (extra ? 2u : 1u) && (c.getD(0) & 0xFFFF) == 0xBEEF,
              "%s: MOVEM (d16,PC) made %u read(s), want %u (D0=%08X)",
              r.name, reads, extra ? 2u : 1u, c.getD(0));
    }
    std::printf("  PC-relative operand space: ok\n");
}

// [B] MC68030UM-1989 §2, §8.2.1: the PC-relative operand read of the 68020
// and 68030 is a program reference that is still an OPERAND cycle (ruling
// D46). (1) Under translation, a transparent window on program space alone
// serves it, with tables that would fault any data-space access. (2) A
// /BERR on it stacks a format-$B frame with DF set, FC 6 and no FB/RB, and
// (3) an RTE of that frame with DF cleared delivers the data input buffer
// without a second bus cycle.
void pcRelativeOperandCycle()
{
    constexpr uint32_t kTarget = kCode + 2 + 0x10;   // MOVE.W ($10,PC),D0
    {   // (1) 68030: TT0 on FC 2/6 only, every table descriptor invalid
        constexpr uint32_t kTc  = 0x80000000u | 0xCu << 20 | 0xAu << 12 | 0xAu << 8;
        constexpr uint32_t kTt0 = 0x00008124u;          // E, RWM, FC 2/6 (program)
        constexpr uint32_t kTable = 0x60000;            // zeros: invalid
        constexpr uint64_t kCrp = uint64_t(0x7FFF0002u) << 32 | kTable;
        TestCpu c(moira::CPUModel::M68030);
        p32(c, 2 * 4, kHandler);
        p16(c, kHandler, kNop);
        stage(c, 0x303A, 0);
        p16(c, kCode + 2, 0x0010);
        c.setIRC(0x0010);
        p16(c, kTarget, 0xBEEF);
        c.mmu030(kTc, kCrp, kTt0, 0);
        const bool ran = executeContained(c);
        CHECK(ran && c.getPC0() == kCode + 4 && (c.getD(0) & 0xFFFF) == 0xBEEF,
              "68030 (d16,PC) under a program-only TT: PC0=%08X D0=%08X "
              "(want %08X, BEEF: no data-space table search)",
              c.getPC0(), c.getD(0), kCode + 4);
    }
    for (moira::CPUModel m : { moira::CPUModel::M68EC020, moira::CPUModel::M68020,
                            moira::CPUModel::M68030 }) {
        TestCpu c(m);
        p32(c, 2 * 4, kHandler);
        p16(c, kHandler, 0x4E73);                       // RTE
        stage(c, 0x303A, 0);
        p16(c, kCode + 2, 0x0010);
        c.setIRC(0x0010);
        p16(c, kTarget, 0xBEEF);
        c.holeAt = kTarget;
        c.holeArmed = true;
        const bool contained = executeContained(c);
        const uint32_t sp = c.getSP();
        const uint16_t ssw = g16(c, sp + 0x0A);
        CHECK(contained && c.getPC0() == kHandler && (g16(c, sp + 6) >> 12) == 0xB &&
              g32(c, sp + 0x10) == kTarget,
              "model %d (d16,PC) /BERR: PC0=%08X format $%X fault address %08X "
              "(want the handler, $B, %08X)", int(m), c.getPC0(),
              g16(c, sp + 6) >> 12, g32(c, sp + 0x10), kTarget);
        CHECK((ssw & 0xF000) == 0 && (ssw & 0x0100) && (ssw & 0x0040) &&
              (ssw & 7) == 6,
              "model %d (d16,PC) /BERR: SSW %04X (want DF, RW, FC 6; no FC/FB/RC/RB)",
              int(m), ssw);
        p16(c, sp + 0x0A, uint16_t(ssw & ~0x0100));     // software completed it
        p32(c, sp + 0x2C, 0x0000CAFE);                  // data input buffer
        const bool rte = executeContained(c);
        const bool rerun = rte && executeContained(c);
        CHECK(rerun && c.getPC0() == kCode + 4 && (c.getD(0) & 0xFFFF) == 0xCAFE &&
              c.holeTrips == 1,
              "model %d (d16,PC) RTE with DF clear: PC0=%08X D0=%08X, %u /BERR "
              "(want %08X, CAFE from the frame, one)", int(m), c.getPC0(),
              c.getD(0), c.holeTrips, kCode + 4);
    }
    std::printf("  PC-relative operand cycle: ok\n");
}

// [B] MC68040UM-1993 App. B.1, B.3 and B.6: the MC68EC040 replaces the MMU
// with two access control units. Its four ACRs sit in the TTR slots
// ($004-$007, IACR0/1 and DACR0/1) with the TTR format; it never searches a
// table, has no 8-Kbyte page, and PTEST/PFLUSH raise no exception. What the
// manual leaves indeterminate (PTEST bus cycles, MOVEC TC read-back, URP/SRP)
// is this core's [F] choice, pinned here so a change is deliberate:
// no bus cycle, MMUSR untouched, TC reads 0. The LC040 runs beside it as
// the control.
void ec040AccessControlUnit()
{
    for (moira::CPUModel model : { moira::CPUModel::M68EC040, moira::CPUModel::M68LC040 }) {
        const bool ec = model == moira::CPUModel::M68EC040;
        const char* name = ec ? "68EC040" : "68LC040";

        // MOVEC D0,TC then MOVEC TC,D1: E/P never stick on the EC040.
        {
            TestCpu c(model);
            stage(c, 0x4E7B, 0);                   // MOVEC D0,TC
            p16(c, kCode + 2, 0x0003);
            p16(c, kCode + 4, 0x4E7A); p16(c, kCode + 6, 0x1003);  // MOVEC TC,D1
            p16(c, kCode + 8, kNop);   p16(c, kCode + 10, kNop);
            c.setIRC(0x0003);
            c.setD(0, 0x00004000);                 // P = 8 KB, E clear
            c.setD(1, 0xFFFFFFFF);
            c.execute(); c.execute();
            const uint32_t want = ec ? 0 : 0x4000;
            CHECK(c.getTC040() == want && c.getD(1) == want,
                  "%s: MOVEC TC wrote %08X, read back %08X, want %08X",
                  name, c.getTC040(), c.getD(1), want);
        }

        // A TC set under another model does not survive a switch to EC040.
        {
            TestCpu c(moira::CPUModel::M68LC040);
            c.setTC040(0x4000);
            c.setModel(model);
            CHECK(c.getTC040() == (ec ? 0u : 0x4000u),
                  "%s: TC after setModel = %08X", name, c.getTC040());
        }

        // PTESTR (A0), DFC = 5: the LC040 walks from SRP; the EC040 makes
        // no bus cycle, leaves MMUSR alone and takes no exception.
        {
            TestCpu c(model);
            stage(c, 0xF568, 0);
            c.setSRP040(0x6000);                   // DFC = 5: supervisor root
            c.setMMUSR040(0x12345678);
            c.setDFC(5);
            c.setA(0, 0);
            c.traceBus = true; c.busBase = 0x6000;
            c.execute();
            CHECK(c.getPC0() == kCode + 2, "%s: PTEST took an exception (PC0=%08X)",
                  name, c.getPC0());
            if (ec) {
                CHECK(c.bus.empty() && c.getMMUSR040() == 0x12345678,
                      "%s: PTEST bus=\"%s\" MMUSR=%08X", name, c.bus.c_str(),
                      c.getMMUSR040());
            } else {
                CHECK(!c.bus.empty(), "%s: PTEST control made no table read", name);
            }
        }

        // PFLUSHA: no exception on either model.
        {
            TestCpu c(model);
            stage(c, 0xF518, 0);
            c.execute();
            CHECK(c.getPC0() == kCode + 2, "%s: PFLUSHA took an exception", name);
        }

        // DACR0 E|S=ignore|W over $01xxxxxx: a write there is an access
        // error, a read is not. App. B.3.2: "causes the abortion of write
        // or read-modify-write accesses to the block".
        {
            TestCpu c(model);
            p32(c, 2 * 4, kHandler);
            p16(c, kHandler, kNop); p16(c, kHandler + 2, kNop);
            stage(c, 0x3080, 0);                   // MOVE.W D0,(A0)
            c.setDTT0(0x0100C004);
            c.setA(0, 0x01009000);
            c.setD(0, 0xBEEF);
            p16(c, 0x9000, 0x1234);
            c.execute();
            CHECK(c.getPC0() == kHandler && g16(c, 0x9000) == 0x1234,
                  "%s: write to a W block: PC0=%08X mem=%04X", name, c.getPC0(),
                  g16(c, 0x9000));
            stage(c, 0x3210, 0);                   // MOVE.W (A0),D1
            c.setDTT0(0x0100C004);
            c.setA(0, 0x01009000);
            c.execute();
            CHECK(c.getPC0() == kCode + 2 && (c.getD(1) & 0xFFFF) == 0x1234,
                  "%s: read from a W block: PC0=%08X D1=%08X", name, c.getPC0(),
                  c.getD(1));
        }

        // The disassembler names the ACRs on the EC040.
        {
            TestCpu c(model);
            p16(c, kCode, 0x4E7B); p16(c, kCode + 2, 0x0006);   // MOVEC D0,DACR0
            char line[128] = {};
            c.disassemble(line, kCode);
            const bool named = std::string_view(line).find(ec ? "DACR0" : "DTT0")
                               != std::string_view::npos;
            CHECK(named, "%s: MOVEC D0,$006 disassembles as \"%s\"", name, line);
        }
    }
    std::printf("  68EC040 access control unit: ok\n");
}

void ec030UsesPlain020Path()
{
    // POM68K_SURFACE.md § 1.2: the current 68EC030 implementation uses
    // the plain-020 bus/prefetch path and refuses the code window. Keep
    // that contract executable without a Macintosh. A programmed TC/CRP
    // maps code/stack but makes the operand's page invalid: the 68030
    // control must fault, whereas the two plain-020 models read RAM.
    // This pins this fork's declared scope; it is not a silicon corpus.
    for (moira::CPUModel model : { moira::CPUModel::M68020, moira::CPUModel::M68EC030,
                                moira::CPUModel::M68030 }) {
        TestCpu c(model);
        const auto checkWindowRefused = [&] {
            if (!MOIRA_ENABLE_JIT) return;     // a JIT code-window check
            if (model != moira::CPUModel::M68EC030) return;
            for (bool super : { false, true }) {
                moira::u32 pa = 0, base = 0, len = 0;
#if MOIRA_ENABLE_JIT
                CHECK(!c.pomJitProbeCode(kCode, super, pa, base, len),
                      "68EC030: code-window probe accepted super=%d TC=%08X",
                      int(super), c.getTC());
#else
                (void)super; (void)pa; (void)base; (void)len;
#endif
            }
        };
        p32(c, 2 * 4, kHandler);
        p16(c, kHandler, kNop); p16(c, kHandler + 2, kNop);
        stage(c, 0x2010, 0);                       // MOVE.L (A0),D0
        c.setSP(0x7000);                           // code/stack in page 0
        c.setA(0, 0x9000);                         // operand in page 1
        c.setD(0, 0xaabbccdd);
        p32(c, 0x9000, 0x12345678);
        checkWindowRefused();                      // TC cleared by reset
        c.setTC(0x80f89000u);                       // 32 KB pages, one level
        c.setCRP(0x7fff000200004000ULL);
        p32(c, 0x4000, 0x00000001);                // page 0: identity
        p32(c, 0x4004, 0);                         // page 1: invalid
        c.setCACR(1);
        CHECK((c.getCACR() & 1) != 0,
              "model %d: cache-enable precondition was masked off", int(model));
        const bool mmu = model == moira::CPUModel::M68030;
        CHECK(c.hasMMU() == mmu, "model %d: hasMMU()", int(model));
        const bool contained = executeContained(c);
        CHECK(contained, "model %d: execute() leaked a fault", int(model));
        if (!contained) continue;
        CHECK(c.getD(0) == (mmu ? 0xaabbccdd : 0x12345678) &&
              c.getPC0() == (mmu ? kHandler : kCode + 2),
              "model %d: D0=%08X PC0=%08X (want %08X/%08X)", int(model),
              c.getD(0), c.getPC0(), mmu ? 0xaabbccdd : 0x12345678,
              mmu ? kHandler : kCode + 2);
        if (mmu)
            CHECK(g32(c, c.getSP() + 0x10) == 0x9000,
                  "68030 control: fault address %08X (want $9000)",
                  g32(c, c.getSP() + 0x10));
        checkWindowRefused();                      // TC programmed, still no window
    }
}

} // namespace

// POM68K_SURFACE.md § 3, setFPUModel: attaching a 6888x to a 68000 or a
// 68010 must not reach the FPU handlers (they are AVAILABILITY(C68020)).
// [B] M68000 Family Programmer's Reference Manual, exception vectors: on
// these processors every $Fxxx word is the Line 1111 emulator, vector 11, with
// the instruction's own address stacked — a 6-byte frame on the 68000, the
// 8-byte format $0 frame (vector offset $02C) on the 68010.
void fpuIgnoredOn68000And68010()
{
    struct Row { moira::CPUModel model; const char* name; int frameBytes; };
    static const Row rows[] = {
        { moira::CPUModel::M68000, "68000", 6 },
        { moira::CPUModel::M68010, "68010", 8 },
    };
    for (const Row& r : rows) {
        for (MoiraFpu fpu : { MoiraFpu::NONE, MoiraFpu::M68881,
                                     MoiraFpu::M68882 }) {
            TestCpu c(r.model);
            c.setFPUModel(fpu);
            p32(c, 11 * 4, kHandler);
            p16(c, kHandler, kNop); p16(c, kHandler + 2, kNop);
            stage(c, 0xf200, 0);                // FMOVE FP0,FP0 on a 6888x
            p16(c, kCode + 2, 0x0000);
            c.setIRC(0x0000);
            const uint32_t sp = c.getSP();
            c.execute();
            const bool fmtOk = r.model == moira::CPUModel::M68000 ||
                               g16(c, c.getSP() + 6) == 0x002c;
            CHECK(c.getPC0() == kHandler && sp - c.getSP() == uint32_t(r.frameBytes) &&
                  g32(c, c.getSP() + 2) == kCode && fmtOk,
                  "%s + FPU model %d: $F200 took PC0=%08X (want %08X, Line-F), "
                  "%u frame bytes (want %d), stacked PC %08X (want %08X)%s",
                  r.name, int(fpu), c.getPC0(), kHandler,
                  unsigned(sp - c.getSP()), r.frameBytes,
                  g32(c, c.getSP() + 2), kCode, fmtOk ? "" : ", format word not $002C");
        }
    }
    // Control: the same staging on a 68020 with a 68881 does reach the FPU —
    // FMOVE FP0,FP0 retires and the next instruction is the NOP after it —
    // so the rows above are not a harness that can never execute F-line.
    TestCpu c(moira::CPUModel::M68020);
    c.setFPUModel(MoiraFpu::M68881);
    p32(c, 11 * 4, kHandler);
    stage(c, 0xf200, 0);
    p16(c, kCode + 2, 0x0000);
    c.setIRC(0x0000);
    c.execute();
    CHECK(c.getPC0() == kCode + 4,
          "control: 68020 + 68881 $F200 0000 left PC0=%08X (want %08X, retired)",
          c.getPC0(), kCode + 4);
}

// POM68K_SURFACE.md § 2 (extBusError) — a host /BERR on a 68030 table-search
// descriptor fetch.
// [B] MC68030UM § 9.5.2 (p. 9-49): a bus error terminates the table search
//     and the MC68030 takes the exception on the retry of the cycle; the
//     ATC entry's B bit (p. 9-11) is set by it, and a level-0 PTEST reports
//     that B (Table 9-3); a PTEST at levels 1-7 reports B (and I) for a bus
//     error met by its own search, without an exception.
// TT0/TT1 map program and data space below $01000000 transparently, so the
// code, the vectors and the stack never walk; $40000000 does, through a
// two-level 4 KB table whose first descriptor sits on the /BERR hole.
void movesFixupSlot030()
{
    // MOVES.L (A4)+,D0 whose read takes /BERR on a 68030: the frame's
    // pending (A4)+ fixup is wb3 status ($34: A4, long, post-increment,
    // valid = $54), wb2 ($36 high byte) stays zero. [D] WinUAE gencpu i_MOVES
    // @ Hatari e77819f7 allocates the memory-to-register fixup as its second
    // slot; the manual leaves these internal words undefined (POM68K_VENDOR.md
    // row 80). Found by the fresh sweep "core/identity seed=9117 #575".
    TestCpu c(moira::CPUModel::M68030);
    p32(c, 2 * 4, kHandler);
    p16(c, kHandler, kNop);
    stage(c, 0x0e9c, 0);                            // MOVES.L (A4)+,D0
    p16(c, kCode + 2, 0x0000);
    c.setIRC(0x0000);
    c.setSFC(1);
    c.setA(4, kHole);
    c.holeArmed = true;
    const bool contained = executeContained(c);
    const uint32_t sp = c.getSP();
    const uint16_t format = g16(c, sp + 6);
    CHECK(contained && c.getPC0() == kHandler && (format >> 12) == 0xb &&
          c.mem[(sp + 0x34) & 0xfffff] == 0x54 && c.mem[(sp + 0x36) & 0xfffff] == 0 &&
          c.getA(4) == kHole + 4,
          "68030 MOVES.L (A4)+,D0 /BERR: PC0=%08X format %04X, wb3 status %02X "
          "(want $54), wb2 status %02X (want $00), A4 %08X (want %08X)",
          c.getPC0(), format, c.mem[(sp + 0x34) & 0xfffff],
          c.mem[(sp + 0x36) & 0xfffff], c.getA(4), kHole + 4);
}

void movemExtensionLog030()
{
    // MOVEM.L D0-D1,d16(A0) whose first store takes /BERR on a 68030 (not the
    // last write, so a format-$B frame): both extension
    // words (mask, d16) are logged accesses, so the $B frame's version word
    // reports idx = idx_done = 2 ($37 = $22). [D] WinUAE cpuemu_34.c
    // op_48e8_34_ff @ Hatari e77819f7 (get_iword_mmu030c_state); the fork
    // stopped logging before the d16 fetch and reported $11 (POM68K_VENDOR.md
    // row 81; "core/identity seed=802 #139", "core/tt seed=803 #635").
    for (uint32_t odd : {0u, 1u}) {
        TestCpu c(moira::CPUModel::M68030);
        p32(c, 2 * 4, kHandler);
        p16(c, kHandler, kNop);
        stage(c, 0x48e8, 0);                        // MOVEM.L D0-D1,d16(A0)
        p16(c, kCode + 2, 0x0003);                  // D0 faults, D1 still due
        p16(c, kCode + 4, 0x0010);
        p16(c, kCode + 6, kNop);
        c.setIRC(0x0003);
        c.setA(0, kHole - 0x10 + odd);
        c.holeAt = kHole + odd;
        c.holeArmed = true;
        const bool contained = executeContained(c);
        const uint32_t sp = c.getSP();
        CHECK(contained && c.getPC0() == kHandler && (g16(c, sp + 6) >> 12) == 0xb &&
              c.mem[(sp + 0x37) & 0xfffff] == 0x22,
              "68030 MOVEM.L D0-D1,16(A0) /BERR at %s address: PC0=%08X format %04X, "
              "idx/idx_done %02X (want $22)", odd ? "an odd" : "an even",
              c.getPC0(), g16(c, sp + 6), c.mem[(sp + 0x37) & 0xfffff]);

        // The handler returns at once (RTE, DF still set): the retry replays
        // the two logged extension words and finishes both stores.
        p16(c, kHandler, 0x4e73);                   // RTE
        c.holeArmed = false;
        c.setD(0, 0x11223344); c.setD(1, 0x55667788);
        c.execute();                                // RTE
        bool retried = executeContained(c);         // MOVEM again
        const uint32_t at = kHole + odd;
        retried &= g32(c, at) == 0x11223344 && g32(c, at + 4) == 0x55667788 &&
                   c.getA(0) == kHole - 0x10 + odd && c.getPC0() == kCode + 6;
        CHECK(retried,
              "68030 MOVEM.L D0-D1,16(A0) retry after RTE at %s address: stored "
              "%08X %08X, A0 %08X, PC0 %08X (want the registers, A0 unchanged, "
              "PC0 %08X)", odd ? "an odd" : "an even", g32(c, at), g32(c, at + 4),
              c.getA(0), c.getPC0(), kCode + 6);
    }
}

void jmpOddTargetPc030()
{
    // JMP to an odd target on a 68030: format-$B address error whose stacked
    // PC is instruction + 2 for the plain modes and the running pc + 2 for the
    // indexed ones, the oracle's rule and the fork's 040 rule ([D] WinUAE
    // get_disp_ea_020_mmu030 @ Hatari e77819f7; [F] silicon, POM68K_VENDOR.md
    // row 82; "random/off seed=807 #681").
    static const struct { uint16_t opcode; uint16_t ext[2]; unsigned words;
                          uint32_t a0; uint32_t want; const char* what; } cases[] = {
        { 0x4ed0, {0, 0},           0, 0x3001,        kCode + 2, "JMP (A0)" },
        { 0x4ee8, {0x0010, 0},      1, 0x3001 - 0x10, kCode + 2, "JMP 16(A0)" },
        { 0x4ef0, {0x1004, 0},      1, 0x3001 - 4,    kCode + 6, "JMP 4(A0,D1.W)" },
        { 0x4ef0, {0x1120, 0x0010}, 2, 0x3001 - 0x10, kCode + 8, "JMP (16.W,A0,D1)" },
    };
    for (const auto& k : cases) {
        TestCpu c(moira::CPUModel::M68030);
        p32(c, 3 * 4, kHandler);
        p16(c, kHandler, kNop);
        stage(c, k.opcode, 0);
        for (unsigned i = 0; i < 2; i++) p16(c, kCode + 2 + 2 * i, i < k.words ? k.ext[i] : kNop);
        p16(c, kCode + 2 + 2 * k.words, kNop);
        c.setIRC(k.words ? k.ext[0] : kNop);
        c.setA(0, k.a0);
        c.setD(1, 0);
        const bool contained = executeContained(c);
        const uint32_t sp = c.getSP();
        CHECK(contained && c.getPC0() == kHandler && g16(c, sp + 6) == 0xb00c &&
              g32(c, sp + 2) == k.want,
              "68030 %s to an odd target: PC0=%08X format %04X, stacked PC %08X "
              "(want %08X)", k.what, c.getPC0(), g16(c, sp + 6), g32(c, sp + 2), k.want);
    }
}

// [B] MC68040UM-1993 §8.2.2: an odd branch offset is an address error,
// taken or not — vector 3, the frame holding the branch's own address and
// the target with A0 cleared. §9.7.1 and Table 8-4: an enabled BSUN is a
// pre-instruction exception, ahead of it. Ruling D28 (the oracle's FPU
// branches raise nothing).
void fbccOddTarget040()
{
    struct Case { uint16_t op, w1, w2; bool bsun; const char* what; };
    const Case cases[] = {
        { 0xF28F, 0x0011, kNop,   false, "FBT.W to an odd target" },
        { 0xF280, 0x0011, kNop,   false, "FBF.W, odd offset, not taken" },
        { 0xF292, 0x0011, kNop,   true,  "FBGT.W on a NaN, BSUN enabled" },
        { 0xF248, 0x0000, 0x0011, false, "FDBF D0 with an odd offset" },
    };
    for (const Case& k : cases) {
        TestCpu c(moira::CPUModel::M68040);
        p32(c, 3 * 4, kHandler);
        p32(c, 48 * 4, kHandler + 0x10);
        p16(c, kHandler, kNop); p16(c, kHandler + 0x10, kNop);
        stage(c, k.op, 0);
        p16(c, kCode + 2, k.w1); p16(c, kCode + 4, k.w2);
        c.setIRC(k.w1);
        c.setD(0, 0x12340005);
        if (k.bsun) {
            c.setFPCR(0x8000);                      // BSUN enabled
            c.setFPSR(0x01000000);                  // FPCC NAN
        }
        const bool fdbcc = k.op == 0xF248;
        const uint32_t target = kCode + (fdbcc ? 4 : 2) + 0x11;
        const bool contained = executeContained(c);
        const uint32_t sp = c.getSP();
        if (k.bsun) {
            CHECK(contained && c.getPC0() == kHandler + 0x10 &&
                  (g16(c, sp + 6) & 0x0FFF) == 48 * 4,
                  "68040 %s: PC0=%08X vector offset %04X (want the BSUN handler, $0C0)",
                  k.what, c.getPC0(), g16(c, sp + 6));
            continue;
        }
        CHECK(contained && c.getPC0() == kHandler && g16(c, sp + 6) == 0x200C &&
              g32(c, sp + 2) == kCode && g32(c, sp + 8) == (target & ~1u),
              "68040 %s: PC0=%08X format/vector %04X stacked PC %08X address %08X "
              "(want the handler at once, $200C, %08X, %08X)", k.what, c.getPC0(),
              g16(c, sp + 6), g32(c, sp + 2), g32(c, sp + 8), kCode, target & ~1u);
        CHECK(c.getD(0) == 0x12340005, "68040 %s: D0=%08X (want it untouched)",
              k.what, c.getD(0));
    }
}

void jsrTargetFetchFault()
{
    // [D] WinUAE @ Hatari e77819f7 — "040 random/identity seed=3945 #676":
    // JSR to a target the bus refuses. On the 68030 and 68040 the JSR
    // retires (return address pushed, PC at the target, no exception); the
    // fault is the NEXT instruction's fetch and stacks the target as its
    // PC, so an RTE restarts the callee. The fork took the fault inside the
    // JSR and the 68040 frame stacked the return address.
    static const struct { uint16_t opcode; uint16_t ext; uint32_t a0; const char* what; } cases[] = {
        { 0x4e90, kNop,   kHole,        "JSR (A0)" },
        { 0x4ea8, 0x0010, kHole - 0x10, "JSR 16(A0)" },
    };
    for (auto model : {moira::CPUModel::M68030, moira::CPUModel::M68LC040, moira::CPUModel::M68040})
    for (const auto& k : cases) {
        TestCpu c(model);
        p32(c, 2 * 4, kHandler);
        p16(c, kHandler, kNop); p16(c, kHandler + 2, kNop);
        stage(c, k.opcode, 0);
        p16(c, kCode + 2, k.ext); c.setIRC(k.ext);
        c.setA(0, k.a0);
        c.holeArmed = true;
        const uint32_t ret = kCode + (k.ext == kNop ? 2 : 4);
        bool contained = executeContained(c);
        CHECK(contained && c.getPC() == kHole && c.getSP() == 0x8000 - 4 &&
              g32(c, 0x8000 - 4) == ret,
              "%s model=%u: PC=%08X SP=%08X pushed=%08X (want the JSR retired "
              "at its target, return %08X pushed, no exception)", k.what,
              unsigned(model), c.getPC(), c.getSP(), g32(c, 0x8000 - 4), ret);
        contained = executeContained(c);
        const uint32_t sp = c.getSP();
        CHECK(contained && c.getPC0() == kHandler && g32(c, sp + 2) == kHole &&
              g32(c, 0x8000 - 4) == ret,
              "%s model=%u, next fetch: PC0=%08X stacked PC %08X (want the "
              "target %08X)", k.what, unsigned(model), c.getPC0(),
              g32(c, sp + 2), kHole);
    }
}

// [B] MC68030UM-1989 §8.1.2, MC68040UM-1993 §8.2.1: a bus error on an
// instruction prefetch is taken only when the word is used. The board's
// /BERR answers kHole, the first word after the staged instruction. An
// instruction that ends on kHole - 2 retires and the next opcode fetch
// faults; one whose extension word is kHole faults inside itself.
// [B] MC68040UM-1993 §3.5: on the 68040 an A-line, illegal or CHK
// instruction in that last word reports the access error instead of its
// own exception; the 68030 takes its own exception. WinUAE @ Hatari e77819f7
// takes the instruction's exception on both (NOTES.md D39), so the
// "pagend" cells of tests/cpu_vectors leave these three out.
void deferredPrefetchFault()
{
    struct Case { uint16_t op; uint16_t ext; uint32_t at; int vector030; int vector040; const char* what; };
    static const Case cases[] = {
        { 0x4e71, kNop,   kHole - 2, 0,  0,  "NOP ending on the last word" },
        { 0x0640, 0x1234, kHole - 4, 0,  0,  "ADDI.W ending on the last word" },
        { 0x0640, 0x1234, kHole - 2, 2,  2,  "ADDI.W whose immediate is on the hole" },
        { 0xa000, kNop,   kHole - 2, 10, 2,  "A-line on the last word" },
        { 0x4afc, kNop,   kHole - 2, 4,  2,  "ILLEGAL on the last word" },
        { 0x4181, kNop,   kHole - 2, 6,  2,  "CHK D1,D0 out of bounds on the last word" },
    };
    for (auto model : {moira::CPUModel::M68030, moira::CPUModel::M68LC040, moira::CPUModel::M68040})
    for (const auto& k : cases) {
        TestCpu c(model);
        for (int v : {2, 4, 6, 10}) p32(c, 4 * uint32_t(v), kHandler + 0x10 * uint32_t(v));
        for (int v : {2, 4, 6, 10}) p16(c, kHandler + 0x10 * uint32_t(v), kNop);
        stage(c, k.op, 0);
        p16(c, k.at, k.op);
        if (k.at == kHole - 4) p16(c, k.at + 2, k.ext);
        c.setPC(k.at); c.setPC0(k.at);
        c.setIRD(k.op); c.setIRC(k.at == kHole - 4 ? k.ext : kNop);
        c.setD(0, 0x8000); c.setD(1, 0x10);
        c.holeArmed = true;
        const bool is040 = model >= moira::CPUModel::M68EC040;
        const int vector = is040 ? k.vector040 : k.vector030;
        const bool contained = executeContained(c);
        if (!vector) {
            CHECK(contained && c.getPC() == kHole && c.getSP() == 0x8000,
                  "%s model=%u: PC=%08X SP=%08X (want it retired at %08X, "
                  "no exception)", k.what, unsigned(model), c.getPC(),
                  c.getSP(), kHole);
            CHECK(executeContained(c) && c.getPC0() == kHandler + 0x20 &&
                  g32(c, c.getSP() + 2) == kHole,
                  "%s model=%u: the next opcode fetch did not fault at %08X",
                  k.what, unsigned(model), kHole);
        } else {
            CHECK(contained && c.getPC0() == kHandler + 0x10 * uint32_t(vector) &&
                  g32(c, c.getSP() + 2) == (vector == 2 ? k.at : g32(c, c.getSP() + 2)) &&
                  c.getD(0) == 0x8000,
                  "%s model=%u: PC0=%08X stacked PC %08X D0=%08X (want vector %d "
                  "at %08X)", k.what, unsigned(model), c.getPC0(),
                  g32(c, c.getSP() + 2), c.getD(0), vector,
                  kHandler + 0x10 * uint32_t(vector));
        }
    }
}

void stop68000()
{
    // [B] M68000UM Rev 8 § 8.11: STOP is 4(0/0).
    // [D] fx68k @ 0602ee46 — back-port of NeoST 993ce5e, 2fc53a8, 9f20046.
    // A 68000 STOP makes no bus access and costs 4 cycles; an interrupt its
    // operand unmasks is taken at STOP+8 (the first iteration tested the old
    // mask); a later interrupt is taken on a 4-cycle boundary counted from
    // the STOP. The 68020 keeps upstream's STOP, prefetch included.
    constexpr uint16_t kStop = 0x4e72;
    auto stop = [&](TestCpu& c, uint16_t sr) {
        p32(c, 27 * 4, kHandler);                       // level-3 autovector
        p16(c, kHandler, kNop); p16(c, kHandler + 2, kNop);
        stage(c, kStop, 0);
        p16(c, kCode + 2, sr); c.setIRC(sr);
    };
    {   // no bus cycle, 4 cycles, PC past the operand
        TestCpu c(moira::CPUModel::M68000);
        stop(c, 0x2700);
        p32(c, 2 * 4, kHandler + 0x40);                 // a prefetch would land here
        c.holeArmed = true; c.holeAt = kCode + 4;
        const moira::i64 c0 = c.getClock();
        c.execute();
        CHECK(c.holeTrips == 0 && c.isStopped() && c.getClock() - c0 == 4 &&
              c.getPC() == kCode + 4,
              "68000 STOP: %u access(es) past the operand, %lld cycles, PC=%08X "
              "(want none, 4, %08X)", c.holeTrips,
              (long long)(c.getClock() - c0), c.getPC(), kCode + 4);
    }
    {   // an interrupt already pending, unmasked by the operand: STOP+8
        TestCpu c(moira::CPUModel::M68000);
        stop(c, 0x2000);
        c.setIPL(3);
        const moira::i64 c0 = c.getClock();
        c.execute();                                    // STOP
        const moira::i64 afterStop = c.getClock() - c0;
        for (int i = 0; i < 16 && c.isStopped(); i++) c.execute();   // held, then taken
        TestCpu ref(moira::CPUModel::M68000);              // the same exception, no STOP
        p32(ref, 27 * 4, kHandler);
        p16(ref, kHandler, kNop); p16(ref, kHandler + 2, kNop);
        stage(ref, kNop, 0);
        ref.setSR(0x2000);
        ref.setIPL(3);
        const moira::i64 r0 = ref.getClock();
        ref.execute();                                  // NOP, then the interrupt
        ref.execute();
        const moira::i64 plain = ref.getClock() - r0 - 4;   // exception alone
        CHECK(afterStop == 4 && c.getPC0() == kHandler &&
              c.getClock() - c0 == 8 + plain,
              "68000 STOP with a pending interrupt: handler at +%lld, want +%lld "
              "(STOP+8 plus the %lld-cycle exception); PC0=%08X",
              (long long)(c.getClock() - c0), (long long)(8 + plain),
              (long long)plain, c.getPC0());
    }
    {   // the 68020 still prefetches
        TestCpu c(moira::CPUModel::M68020);
        stop(c, 0x2700);
        c.holeArmed = true; c.holeAt = kCode + 4;
        executeContained(c);
        CHECK(c.holeTrips != 0, "68020 STOP lost its prefetch (%u accesses past "
              "the operand)", c.holeTrips);
    }
}

void reset68000()
{
    // [D] fx68k @ 0602ee46 — back-port of NeoST 9f20046: the 68000 reset
    // spends 6 cycles between its two prefetch reads, two more than the
    // 68010 path of this core.
    TestCpu m000(moira::CPUModel::M68000), m010(moira::CPUModel::M68010);
    for (TestCpu* c : { &m000, &m010 }) {
        p32(*c, 0, 0x8000); p32(*c, 4, kCode);
        p16(*c, kCode, kNop); p16(*c, kCode + 2, kNop);
    }
    const moira::i64 a0 = m000.getClock(), b0 = m010.getClock();
    m000.reset(); m010.reset();
    CHECK((m000.getClock() - a0) - (m010.getClock() - b0) == 2 &&
          m000.getPC() == m010.getPC(),
          "68000 reset: %lld cycles against the 68010's %lld (want +2)",
          (long long)(m000.getClock() - a0), (long long)(m010.getClock() - b0));
}

#if MOIRA_ENABLE_JIT
// The 68040 ATCs are private to the core; this is the seam Moira.h befriends.
// Defined inside the namespace: the friend declaration does not make the name
// visible to a qualified definition (GCC refuses `struct moira::PomAtcProbe`).
namespace moira {
struct PomAtcProbe {
    struct Image {
        uint8_t i[sizeof(Moira::mmu040AtcI)], d[sizeof(Moira::mmu040AtcD)];
        uint32_t ai, ad; int8_t li[2], ld[2];
        bool operator==(const Image&) const = default;
    };
    static Image image(const Moira& c) {
        Image m {};
        std::memcpy(m.i, c.mmu040AtcI, sizeof m.i);
        std::memcpy(m.d, c.mmu040AtcD, sizeof m.d);
        m.ai = c.mmu040AtcAccessI; m.ad = c.mmu040AtcAccessD;
        std::memcpy(m.li, c.mmu040AtcLastI, 2); std::memcpy(m.ld, c.mmu040AtcLastD, 2);
        return m;
    }
    static void seed(Moira& c) {
        for (uint32_t n = 0; n < 40; n++) {
            // Resident, unmodified, some global: a write hit re-walks.
            c.mmu040AtcFill(true,  n << 12, (n << 12) | 0x001 | (n & 1 ? 0x400 : 0), true);
            c.mmu040AtcFill(false, n << 13, (n << 13) | 0x011, true);
        }
    }
    static void begin(Moira& c, Moira::PomJitAtcReplay040& r) {
        r.fetches = 2; r.routes = 1;                // I side, then D side
        c.pomJitAtcReplayBegin040(r, 0, true, 1);
    }
    static void mutate(Moira& c, int kind) {
        switch (kind) {
            case 0: c.mmu040AtcLookup(true, 39u << 12, false, true);       // hits
                    c.mmu040AtcLookup(false, 39u << 13, false, true); break;
            case 1: c.mmu040AtcFill(true, 0x00777000, 0x00777011, true); break;
            case 2: c.mmu040AtcFill(false, 0x00555000, 0x00555011, false); break;
            case 3: c.mmu040AtcLookup(true, 39u << 12, true, true); break;  // last-hit re-walk
            case 4: c.mmu040AtcLookup(true, 20u << 12, true, true); break;  // set-scan re-walk
            case 5: c.mmu040AtcFlushAll(); break;
            case 6: c.mmu040AtcFlushNonGlobal(); break;
            case 7: c.mmu040AtcFlushPage(38u << 12, false, true); break;
            default: c.mmu040AtcFill(true, 0x00777000, 0x00777011, true);   // two in a row:
                     c.mmu040AtcFill(true, 0x00778000, 0x00778011, true);   // one copy
                     c.mmu040AtcFill(false, 0x00556000, 0x00556011, true); break;
        }
    }
    static bool armed(const Moira& c) { return c.pomAtcCow040 != nullptr; }
};
} // namespace moira
void atcReplayCopyOnWrite040()
{
    // POM68K (Moira.h § pomAtcCow040): an exact native access opens an ATC
    // transaction that a refused instruction rolls back. Begin saves the
    // access counter and the last-hit indices; the 64 entries of a side are
    // copied only when something is about to change one. Whatever the
    // mutation, Abort must put both ATCs back exactly, and a transaction
    // that retired must leave later mutations alone.
    using P = moira::PomAtcProbe;
    static const uint32_t wantSaved[] = { 0, 8, 4, 8, 8, 12, 12, 12, 12 };
    for (int kind = 0; kind <= 8; kind++) {
        TestCpu c(moira::CPUModel::M68040);
        P::seed(c);
        const P::Image before = P::image(c);
        moira::Moira::PomJitAtcReplay040 r;
        P::begin(c, r);
        CHECK((r.active & 15) == 3, "kind %d: Begin copied entries (active=%X)",
              kind, unsigned(r.active));
        P::mutate(c, kind);
        CHECK((r.active & 12) == wantSaved[kind],
              "kind %d: sides copied %X, want %X", kind, unsigned(r.active & 12),
              unsigned(wantSaved[kind]));
        CHECK(!(P::image(c) == before), "kind %d: the mutation changed nothing", kind);
        c.pomJitAtcReplayAbort040(r);
        CHECK(P::image(c) == before && r.active == 0,
              "kind %d: Abort did not restore both ATCs", kind);

        // Retired: generated code zeroes `active`; nothing is owed after.
        P::begin(c, r);
        r.active = 0;
        std::memset(r.opaque, 0xA5, sizeof r.opaque);
        P::mutate(c, kind);
        bool untouched = true;
        for (uint8_t b : r.opaque) untouched = untouched && b == 0xA5;
        CHECK(untouched && r.active == 0 && (kind == 0 || !P::armed(c)),
              "kind %d: a retired transaction was written to", kind);

        // An owner whose replay object dies (the x64 backend's lives on the
        // stack of run()) releases it; whatever lands in that storage later,
        // even bits that read as "begun, not copied", is never written to.
        P::begin(c, r);
        r.active = 0;
        c.pomJitAtcReplayRelease040(r);
        r.active = 3;
        std::memset(r.opaque, 0xA5, sizeof r.opaque);
        P::mutate(c, kind);
        untouched = true;
        for (uint8_t b : r.opaque) untouched = untouched && b == 0xA5;
        CHECK(untouched && r.active == 3 && !P::armed(c),
              "kind %d: a released replay object was written to", kind);
        r.active = 0;
    }
}
#endif

void fullEaLongDisplacementLog030()
{
    // [D] WinUAE cpummu030.c get_disp_ea_020_mmu030 @ Hatari e77819f7 —
    // "random/identity seed=6107 #1726". MOVE.W ([bd.L,A2],od.W),(A1) whose
    // pointer fetch takes /BERR on a 68030: the format-$B access log holds
    // the extension word, the LONG base displacement as one entry, and the
    // outer displacement; three done, the fourth in flight. The fork logged
    // the base displacement as two words.
    TestCpu c(moira::CPUModel::M68030);
    p32(c, 2 * 4, kHandler);
    p16(c, kHandler, kNop);
    stage(c, 0x32b2, 0);
    p16(c, kCode + 2, 0x0172);              // full format, IS, bd.L, ([..],od.W)
    p32(c, kCode + 4, kHole);
    p16(c, kCode + 8, 0x0010);
    p16(c, kCode + 10, kNop);
    c.setIRC(0x0172);
    c.setA(2, 0); c.setA(1, 0x3000);
    c.holeArmed = true;
    const bool contained = executeContained(c);
    const uint32_t sp = c.getSP();
    CHECK(contained && c.getPC0() == kHandler && g16(c, sp + 6) == 0xb008 &&
          g32(c, sp + 0x58) == 0x0172 && g32(c, sp + 0x54) == kHole &&
          g32(c, sp + 0x50) == 0x0010 && c.mem[sp + 0x37] == 0x34,
          "68030 full-format EA log: format %04X, log %08X %08X %08X, "
          "idx byte %02X (want 00000172 %08X 00000010, 34)",
          g16(c, sp + 6), g32(c, sp + 0x58), g32(c, sp + 0x54),
          g32(c, sp + 0x50), c.mem[sp + 0x37], kHole);
}

void fsccWriteBuffer030()
{
    // FScc.B (A0) with a true predicate whose store takes /BERR on a 68030 +
    // 68882: the bus-fault frame's data output buffer ($18) holds the pending
    // byte. [D] WinUAE fpp.c fpuop_scc @ Hatari e77819f7 passes
    // it unsigned: $000000FF, where the integer core's signed operands give
    // $FFFFFFFF (POM68K_VENDOR.md row 83; "fpu/identity seed=811 #1003",
    // "fpuraw/identity seed=722 #1890").
    TestCpu c(moira::CPUModel::M68030);
    c.setFPUModel(MoiraFpu::M68882);
    p32(c, 2 * 4, kHandler);
    p16(c, kHandler, kNop);
    stage(c, 0xf250, 0);                            // FScc.B (A0)
    p16(c, kCode + 2, 0x000f);                      // predicate T
    c.setIRC(0x000f);
    c.setA(0, kHole);
    c.holeArmed = true;
    const bool contained = executeContained(c);
    const uint32_t sp = c.getSP();
    const uint16_t format = g16(c, sp + 6);
    const uint32_t buffer = g32(c, sp + 0x18);       // data output buffer, $A and $B
    CHECK(contained && c.getPC0() == kHandler && (format & 0xfff) == 8 &&
          buffer == 0x000000ff,
          "68030 FScc.B (A0) /BERR: PC0=%08X format %04X, pending byte in the "
          "frame %08X (want $000000FF)", c.getPC0(), format, buffer);
}

void aluWriteBuffer030()
{
    // A byte ADD/SUB to memory whose store takes /BERR on a 68030 stacks the
    // unmasked 32-bit result as its pending write ($18, format $A: the store
    // is the last write). [D] WinUAE gencpu flag_add/flag_sub @ Hatari
    // e77819f7: `uae_u32 newv = (uae_u8)dst ± (uae_u8)src`. The fork stacked
    // the byte sign-extended (POM68K_VENDOR.md row 84; the eleven vectors that
    // were listed in tests/sst68030_rmw.known).
    static const struct { uint16_t opcode; uint8_t memory; uint32_t d1; uint32_t want;
                          const char* what; } cases[] = {
        { 0x9310, 0x10, 0xc5, 0xffffff4b, "SUB.B D1,(A0) $10-$C5" },
        { 0xd310, 0x7f, 0x01, 0x00000080, "ADD.B D1,(A0) $7F+$01" },
        { 0xd310, 0xff, 0xff, 0x000001fe, "ADD.B D1,(A0) $FF+$FF" },
    };
    for (const auto& k : cases) {
        TestCpu c(moira::CPUModel::M68030);
        p32(c, 2 * 4, kHandler);
        p16(c, kHandler, kNop);
        stage(c, k.opcode, 0);
        c.mem[kHole] = k.memory;
        c.setA(0, kHole);
        c.setD(1, k.d1);
        c.holeArmed = true;
        c.holeWritesOnly = true;
        const bool contained = executeContained(c);
        const uint32_t sp = c.getSP();
        CHECK(contained && c.getPC0() == kHandler && (g16(c, sp + 6) & 0xfff) == 8 &&
              g32(c, sp + 0x18) == k.want,
              "68030 %s /BERR on the store: PC0=%08X format %04X, pending write %08X "
              "(want %08X)", k.what, c.getPC0(), g16(c, sp + 6), g32(c, sp + 0x18), k.want);
    }
}

// MOVE <memory>,(xxx).L: on the 68000 order the destination's second word
// is fetched after the write, so this handler calls writeM itself instead of
// writeOp, and it marked the store as the last write for the 68040 only. A
// 68030 /BERR there stacked a long $B frame at the instruction's own PC; the
// short $A frame with the next instruction's PC is what every other MOVE
// destination stacks. [D] WinUAE gencpu.c gen_set_fault_pc @ Hatari
// e77819f7 ("random/identity seed=72008 #106", fresh-seed sweep 2026-11-14).
void moveAbsLongLastWrite030()
{
    TestCpu c(moira::CPUModel::M68030);
    p32(c, 2 * 4, kHandler);
    p16(c, kHandler, kNop);
    stage(c, 0x33d1, 0);                            // MOVE.W (A1),(xxx).L
    p32(c, kCode + 2, kHole);
    p16(c, kCode + 6, kNop);
    c.setIRC(uint16_t(kHole >> 16));
    p16(c, 0x3000, 0x8001);                         // a negative source word
    c.setA(1, 0x3000);
    c.holeArmed = true;
    c.holeWritesOnly = true;
    const bool contained = executeContained(c);
    const uint32_t sp = c.getSP();
    const uint16_t format = g16(c, sp + 6);
    const uint32_t pc = g32(c, sp + 2);
    const uint16_t sr = g16(c, sp);
    CHECK(contained && c.getPC0() == kHandler && format == 0xa008 &&
          pc == kCode + 6 && (sr & 0x0f) == 0x08,
          "68030 MOVE.W (A1),(xxx).L /BERR on the store: format %04X (want A008), "
          "stacked PC %08X (want %08X), CCR %X (want N)", format, pc, kCode + 6,
          sr & 0x1f);
}

// The CACR clear bits are command strobes that read back as zero.
// [B] MC68030UM-1989 §6.3.1 — CI, CEI (§6.3.1.8/.9), CD and CED
// (§6.3.1.3/.4) are "always read as a zero"; WinUAE's 68030 read mask is
// $3313 and upstream Moira adopted it in f7844ac. The fork kept CD/CED
// readable ($3F13) until POM68K_VENDOR.md row 86. On the 68020 only E and F
// remain. The raw value must still reach the board, which acts on the
// strobes (didChangeCACR). No corpus vector writes a clear bit and reads
// CACR back.
void cacrStrobesReadZero()
{
    static const struct { moira::CPUModel model; const char* name; uint32_t want; } rows[] = {
        { moira::CPUModel::M68020,   "68020",   0x00000003 },
        { moira::CPUModel::M68EC030, "68EC030", 0x00003313 },
        { moira::CPUModel::M68030,   "68030",   0x00003313 },
    };
    for (const auto& r : rows) {
        TestCpu c(r.model);
        stage(c, 0x4e7b, 0);                   // MOVEC D0,CACR
        p16(c, kCode + 2, 0x0002);
        p16(c, kCode + 4, 0x4e7a);             // MOVEC CACR,D1
        p16(c, kCode + 6, 0x1002);
        p16(c, kCode + 8, kNop);
        c.setIRC(0x0002);
        c.setD(0, 0xffffffff);
        c.setD(1, 0x5a5a5a5a);
        const bool contained = executeContained(c) && executeContained(c);
        CHECK(contained && c.getPC0() == kCode + 8 && c.getD(1) == r.want &&
                  c.cacrWritten == 0xffffffff,
              "%s MOVEC CACR read-back: D1=%08X (want %08X), board saw %08X, PC0=%08X",
              r.name, c.getD(1), r.want, c.cacrWritten, c.getPC0());
    }
}

void tableSearchBusError030()
{
    constexpr uint32_t kTc  = 0x80000000u | 0xCu << 20 | 0xAu << 12 | 0xAu << 8;
    constexpr uint32_t kTt0 = 0x00008124u;          // E, RWM, FC 2/6 (program)
    constexpr uint32_t kTt1 = 0x00008114u;          // E, RWM, FC 1/5 (data)
    constexpr uint32_t kVa  = 0x40000000u;          // TIA index $100
    constexpr uint32_t kTable = kHole - 0x100 * 4;  // descriptor $100 on the hole
    constexpr uint64_t kCrp = uint64_t(0x7FFF0002u) << 32 | kTable;   // short table
    const uint16_t ptestR0 = 0x8215, ptestR7 = 0x9e15;   // PTESTR #5,(A0),#n

    {   // 1-2: the access faults on its logical address; the B entry stays
        TestCpu c(moira::CPUModel::M68030);
        p32(c, 2 * 4, kHandler);
        p16(c, kHandler, 0xf010); p16(c, kHandler + 2, ptestR0);
        p16(c, kHandler + 4, kNop);
        stage(c, 0x2010, 0);                        // MOVE.L (A0),D0
        c.setA(0, kVa);
        c.mmu030(kTc, kCrp, kTt0, kTt1);
        c.holeArmed = true;
        c.execute();
        const uint32_t sp = c.getSP();
        CHECK(c.getPC0() == kHandler && g32(c, sp + 0x10) == kVa,
              "68030 /BERR on a descriptor fetch: PC0=%08X (want the bus error "
              "handler %08X), fault address %08X (want the logical %08X)",
              c.getPC0(), kHandler, g32(c, sp + 0x10), kVa);
        c.execute();                                // PTESTR #5,(A0),#0
        CHECK(c.mmusr() == 0x8400,
              "68030 PTEST level 0 after the fault: MMUSR %04X (want $8400, the "
              "ATC entry's B with I)", c.mmusr());
    }
    {   // 3: PTEST's own search meets the /BERR — a report, not an exception
        TestCpu c(moira::CPUModel::M68030);
        p32(c, 2 * 4, kHandler);
        stage(c, 0xf010, 0);
        p16(c, kCode + 2, ptestR7);
        c.setIRC(ptestR7);
        p16(c, kCode + 4, kNop);
        c.setA(0, kVa);
        c.mmu030(kTc, kCrp, kTt0, kTt1);
        c.holeArmed = true;
        c.execute();
        CHECK(c.getPC0() == kCode + 4 && c.mmusr() == 0x8400,
              "68030 PTEST level 7 into a /BERR: PC0=%08X (want %08X, no "
              "exception), MMUSR %04X (want $8400: B, I, N = 0)",
              c.getPC0(), kCode + 4, c.mmusr());
    }
}

// POM68K (Moira.h, fpu.fp): a core that has not been reset yet is serialized
// too — the Portable's is held by its Power Manager — so its FP registers
// must not be whatever the allocator left. Built over scribbled storage.
void unresetFpZero()
{
    alignas(TestCpu) static unsigned char raw[sizeof(TestCpu)];
    std::memset(raw, 0xA5, sizeof raw);
    TestCpu* c = new (raw) TestCpu(moira::CPUModel::M68000);
    for (int n = 0; n < 8; n++) {
        moira::u32 w[3]; c->getFP(n, w);
        CHECK(w[0] == 0 && w[1] == 0 && w[2] == 0,
              "FP%d of a never-reset core is %08X %08X %08X", n, w[0], w[1], w[2]);
    }
    c->~TestCpu();
}

void irqDelayAfterMaskLowering()
{
    // [F] POM68K_VENDOR.md row 13: from the 68020 up, an interrupt unmasked
    // by an SR write is recognised two instructions later (`irqDelay = 2`,
    // POM68K_SURFACE.md § 5); the PRM reads as one, and no capture settles
    // it. The depth was held by `lcii_boot_etalon` alone. This pins what
    // the core does, so that a change of depth is a decision: where the
    // interrupt lands after MOVE to SR, ANDI to SR and RTE, that a write
    // which leaves the level masked arms nothing, that an interrupt
    // arriving with the mask already low is not delayed, and that reset
    // drops an armed delay. The 68000 and 68010 have no delay.
    constexpr uint32_t kAfterRte = kCode + 0x100;
    enum Form { MoveToSr, AndiToSr, Rte, StillMasked, AlreadyLow, ResetBetween };
    const auto landing = [&](moira::CPUModel m, Form form, bool& taken) {
        TestCpu c(m);
        p32(c, 27 * 4, kHandler);                       // level-3 autovector
        for (int i = 0; i < 8; i++) p16(c, kHandler + 2 * i, kNop);
        const uint16_t op = form == AndiToSr ? 0x027c : form == Rte ? 0x4e73
                          : form == AlreadyLow ? kNop : 0x46fc;
        const uint16_t imm = form == AndiToSr ? 0xf8ff
                           : form == StillMasked ? 0x2400 : 0x2000;
        stage(c, op, 0);
        for (int i = 1; i < 10; i++) p16(c, kCode + 2 * i, kNop);
        for (int i = 0; i < 10; i++) p16(c, kAfterRte + 2 * i, kNop);
        if (form == Rte) {
            p16(c, 0x7ff8, 0x2000); p32(c, 0x7ffa, kAfterRte); p16(c, 0x7ffe, 0);
            c.setSP(0x7ff8);
        } else if (form == AlreadyLow) {
            c.setSR(0x2000);                            // arms the delay itself:
            for (int i = 0; i < 3; i++) c.execute();    // let it run out
        } else {
            p16(c, kCode + 2, imm); c.setIRC(imm);
        }
        if (form == ResetBetween) {                     // arm the delay, then reset
            c.execute();
            c.reset();                                  // mask 7: only level 7 passes
            p32(c, 31 * 4, kHandler);
            c.setSP(0x8000);
            c.setPC(kCode + 4); c.setPC0(kCode + 4);
            c.setIRD(kNop); c.setIRC(kNop);
        }
        c.setIPL(form == ResetBetween ? 7 : 3);
        int n = 0;
        for (; n < 12 && c.getPC0() != kHandler; n++) c.execute();
        taken = c.getPC0() == kHandler;
        return taken ? g32(c, c.getSP() + 2) : 0;       // the stacked return PC
    };
    struct Row { moira::CPUModel model; const char* name; uint32_t delayed; };
    for (const Row& row : { Row{ moira::CPUModel::M68000, "68000", 0 },
                            Row{ moira::CPUModel::M68010, "68010", 0 },
                            Row{ moira::CPUModel::M68020, "68020", 4 },
                            Row{ moira::CPUModel::M68030, "68030", 4 },
                            Row{ moira::CPUModel::M68040, "68040", 4 } }) {
        bool taken = false;
        uint32_t ret = landing(row.model, MoveToSr, taken);
        CHECK(taken && ret == kCode + 4 + row.delayed,
              "%s MOVE to SR: interrupt returns to %08X, want %08X",
              row.name, ret, kCode + 4 + row.delayed);
        ret = landing(row.model, AndiToSr, taken);
        CHECK(taken && ret == kCode + 4 + row.delayed,
              "%s ANDI to SR: interrupt returns to %08X, want %08X",
              row.name, ret, kCode + 4 + row.delayed);
        ret = landing(row.model, Rte, taken);
        CHECK(taken && ret == kAfterRte + row.delayed,
              "%s RTE: interrupt returns to %08X, want %08X",
              row.name, ret, kAfterRte + row.delayed);
        landing(row.model, StillMasked, taken);
        CHECK(!taken, "%s MOVE to SR leaving level 3 masked took the interrupt",
              row.name);
        ret = landing(row.model, AlreadyLow, taken);
        // A level raised between two instructions is seen one boundary
        // later on every model (the pin is sampled before the boundary):
        // that instruction is the pin's, not irqDelay's.
        CHECK(taken && ret == kCode + 8,
              "%s interrupt with the mask already low returns to %08X, want "
              "%08X", row.name, ret, kCode + 8);
        ret = landing(row.model, ResetBetween, taken);
        CHECK(taken && ret == kCode + 6,
              "%s reset kept an armed delay: level 7 returns to %08X, want %08X",
              row.name, ret, kCode + 6);
    }
}

void level7TransitionSensitive()
{
    // [B] MC68020UM-1992 §6.1.9 Fig. 6-3, MC68030UM-1989 §8.1.9,
    // MC68040UM-1993 §8.2.9 Fig. 8-3: level 7 is recognised on each
    // transition of the request from a lower level to 7, and by level
    // comparison when the mask drops below 7 — never again merely because
    // it is still held under mask 7 (Moira.h, nmiLatch). The 68000 and
    // 68010 keep upstream Moira's level-sensitive reading, pinned here so
    // that changing them is a decision.
    // `moveSr`: the handler's first instruction lowers the mask to 5.
    const auto entries = [](moira::CPUModel m, bool retransition, bool moveSr) {
        TestCpu c(m);
        p32(c, 31 * 4, kHandler);                       // level-7 autovector
        for (int i = 0; i < 64; i++) p16(c, kHandler + 2 * i, kNop);
        if (moveSr) { p16(c, kHandler, 0x46fc); p16(c, kHandler + 2, 0x2500); }
        stage(c, kNop, 0);
        for (int i = 0; i < 64; i++) p16(c, kCode + 2 * i, kNop);
        c.setSR(0x2500);
        for (int i = 0; i < 4; i++) c.execute();        // run out any delay
        int n = 0;
        c.setIPL(7);
        for (int i = 0; i < 24; i++) { c.execute(); if (c.getPC0() == kHandler) n++; }
        if (retransition) {
            c.setIPL(3);
            for (int i = 0; i < 4; i++) c.execute();
            c.setIPL(7);
            for (int i = 0; i < 24; i++) { c.execute(); if (c.getPC0() == kHandler) n++; }
        }
        return n;
    };
    for (moira::CPUModel m : { moira::CPUModel::M68020, moira::CPUModel::M68030,
                            moira::CPUModel::M68040 }) {
        const int held = entries(m, false, false);
        CHECK(held == 1, "model %d: a held level 7 entered the handler %d times, want 1",
              int(m), held);
        const int twice = entries(m, true, false);
        CHECK(twice == 2, "model %d: 7 -> 3 -> 7 entered the handler %d times, want 2",
              int(m), twice);
        const int compared = entries(m, false, true);
        CHECK(compared >= 2, "model %d: a mask lowered below a held level 7 "
              "entered the handler %d times, want a second entry", int(m), compared);
    }
    for (moira::CPUModel m : { moira::CPUModel::M68000, moira::CPUModel::M68010 }) {
        const int held = entries(m, false, false);
        CHECK(held > 1, "model %d: the 68000/68010 level-sensitive level 7 "
              "changed (%d entries)", int(m), held);
    }
}

void interruptSequenceBilled()
{
    // [B] MC68020UM-1992 §8.2.17: Interrupt (I-Stack) 26 (2/0/4), (M-Stack)
    // 41 (2/0/8), the column the 020 core's TRAP #n (20) uses. An execute()
    // that takes the interrupt runs no instruction, so the clock moves by
    // the sequence alone. Until 2026-11-19 it moved by 0.
    for (moira::CPUModel m : { moira::CPUModel::M68020, moira::CPUModel::M68030,
                            moira::CPUModel::M68040 }) {
        for (bool master : { false, true }) {
            TestCpu c(m);
            p32(c, 27 * 4, kHandler);                   // level-3 autovector
            for (int i = 0; i < 8; i++) p16(c, kHandler + 2 * i, kNop);
            stage(c, kNop, 0);
            for (int i = 0; i < 16; i++) p16(c, kCode + 2 * i, kNop);
            c.setSR(master ? 0x3000 : 0x2000);          // S, M, mask 0
            c.setSP(0x8000);
            for (int i = 0; i < 4; i++) c.execute();    // run out any delay
            c.setIPL(3);
            c.execute();                                // samples the pin
            const auto before = c.getClock();
            int n = 0;
            while (c.getPC0() != kHandler && n++ < 4) {
                const auto t = c.getClock();
                c.execute();
                if (c.getPC0() == kHandler) {
                    const long long cost = (long long)(c.getClock() - t);
                    const long long want = master ? 41 : 26;
                    CHECK(cost == want, "model %d %s-stack interrupt costs %lld cycles, want %lld",
                          int(m), master ? "M" : "I", cost, want);
                }
            }
            CHECK(c.getPC0() == kHandler && c.getClock() > before,
                  "model %d: the interrupt was not taken", int(m));
        }
    }
}

void masterStackInterrupt()
{
    // POM68K_VENDOR.md row 145. An interrupt taken with SR.M set stacks its
    // format-$0 frame on the master stack, clears M, and stacks a format-$1
    // throwaway frame on the interrupt stack; the handler runs on the
    // interrupt stack, and its RTE pops the throwaway, whose SR puts M back,
    // then the master frame.
    // [B] MC68020UM-1992 §6.1.9 / MC68030UM-1989 §8.1.9 — "If the M bit of
    //     the status register is set, the processor clears the M bit and
    //     creates a throwaway exception stack frame on top of the interrupt
    //     stack [...] The copy of the status register saved on the throwaway
    //     frame is exactly the same as that placed on the master stack
    //     except that the S bit is set in the version placed on the
    //     interrupt stack. [...] The resulting status register (after
    //     exception processing) has the S bit set and the M bit cleared."
    // Until 2026-11-20 both frames went on the master stack and M stayed
    // set. Taken from supervisor ($3000) and from user mode with M set
    // ($1000), where the two SR copies differ by S.
    constexpr uint32_t kIsp = 0x6000, kMsp = 0x8000, kUsp = 0x4000;
    for (moira::CPUModel m : { moira::CPUModel::M68020, moira::CPUModel::M68030,
                            moira::CPUModel::M68040 }) {
        for (uint16_t sr : { uint16_t(0x3000), uint16_t(0x1000) }) {
            TestCpu c(m);
            p32(c, 27 * 4, kHandler);                   // level-3 autovector
            p16(c, kHandler, 0x4e73);                   // RTE
            p16(c, kHandler + 2, kNop);
            stage(c, kNop, 0);
            for (int i = 1; i < 16; i++) p16(c, kCode + 2 * i, kNop);
            c.setSR(sr);                                // mask 0
            c.setISP(kIsp); c.setMSP(kMsp); c.setUSP(kUsp);
            for (int i = 0; i < 3; i++) c.execute();    // past the SR-write delay
            c.setIPL(3);
            for (int i = 0; i < 6 && c.getPC0() != kHandler; i++) c.execute();
            const uint32_t mf = kMsp - 8, tf = kIsp - 8;
            const uint32_t ret = g32(c, mf + 2);
            CHECK(c.getPC0() == kHandler && c.getSR() == 0x2300 && c.getSP() == tf &&
                  c.getISP() == tf && c.getMSP() == mf && c.getUSP() == kUsp,
                  "model %d SR=%04X: interrupt with M set: PC0=%08X SR=%04X A7=%08X "
                  "ISP=%08X MSP=%08X USP=%08X (want %08X, 2300, A7=ISP=%08X, "
                  "MSP=%08X, USP=%08X)", int(m), sr, c.getPC0(), c.getSR(), c.getSP(),
                  c.getISP(), c.getMSP(), c.getUSP(), kHandler, tf, mf, kUsp);
            CHECK(g16(c, mf) == sr && ret > kCode && ret < kCode + 32 &&
                  g16(c, mf + 6) == 27 * 4 &&
                  g16(c, tf) == (sr | 0x2000) && g32(c, tf + 2) == ret &&
                  g16(c, tf + 6) == (0x1000 | 27 * 4),
                  "model %d SR=%04X: master frame SR=%04X PC=%08X fmt/vec=%04X, "
                  "interrupt-stack frame SR=%04X PC=%08X fmt/vec=%04X (want %04X, "
                  "a PC in the NOPs, 006C; %04X, the same PC, 106C)", int(m), sr,
                  g16(c, mf), ret, g16(c, mf + 6), g16(c, tf), g32(c, tf + 2),
                  g16(c, tf + 6), sr, sr | 0x2000);
            c.setIPL(0);
            const bool contained = executeContained(c); // RTE: $1 then $0
            CHECK(contained && c.getPC0() == ret && c.getSR() == sr &&
                  c.getISP() == kIsp && c.getMSP() == kMsp && c.getUSP() == kUsp,
                  "model %d SR=%04X: RTE through the throwaway frame: PC0=%08X SR=%04X "
                  "ISP=%08X MSP=%08X USP=%08X (want %08X, %04X, %08X, %08X, %08X)",
                  int(m), sr, c.getPC0(), c.getSR(), c.getISP(), c.getMSP(),
                  c.getUSP(), ret, sr, kIsp, kMsp, kUsp);
        }
    }
}

void movesReleasesAlternateFc()
{
    // POM68K_VENDOR.md row 37: MOVES puts SFC or DFC on the function-code
    // pins for its one access; if that access takes a bus error, the pins
    // must be back on the supervisor's own code before the handler's first
    // fetch, or the handler runs in the faulting access's address space.
    // `v8_ramsize` holds it on a V8 board for the 68020 and the 68030, in
    // the read direction; this holds both directions on the 68040 too.
    for (moira::CPUModel m : { moira::CPUModel::M68020, moira::CPUModel::M68030,
                            moira::CPUModel::M68040 }) {
        for (bool write : { false, true }) {
            TestCpu c(m);
            p32(c, 2 * 4, kHandler);                    // vector 2: bus error
            p16(c, kHandler, kNop); p16(c, kHandler + 2, kNop);
            stage(c, 0x0e90, 0);                        // MOVES.L (A0),D1 / D1,(A0)
            const uint16_t ext = write ? 0x1800 : 0x1000;
            p16(c, kCode + 2, ext); c.setIRC(ext);
            c.setSFC(1); c.setDFC(1);                   // user data
            c.setA(0, kHole);
            c.holeArmed = true;
            const bool contained = executeContained(c);
            for (int i = 0; i < 4 && c.getPC0() != kHandler; i++) c.execute();
            CHECK(contained && c.holeTrips != 0 && c.getPC0() == kHandler,
                  "model %d MOVES %s: no bus error taken (trips %u, PC0=%08X)",
                  int(m), write ? "write" : "read", c.holeTrips, c.getPC0());
            CHECK((c.readFC() & 4) != 0,
                  "model %d faulting MOVES %s left FC=%u on the pins for its "
                  "handler", int(m), write ? "write" : "read", c.readFC());
        }
    }
}

void movesFcAfterFault010()
{
    // Inventory row 151 (upstream ce28239, reproduced 2026-11-20): MOVES
    // <ea>,Rn set fcSource for its read and cleared it on the normal path
    // only, so after an address error on that read every later access --
    // the whole stacking, the vector fetch, the handler -- reported SFC.
    // [B] MC68020UM-1992 §2.2: SFC/DFC give the function code of the MOVES
    // operand access alone. Both directions, an address error (odd A0) and
    // a host /BERR (extBusError000), on the 68010, the one core where the
    // two exceptions reach execMoves. The faulted access itself keeps SFC
    // or DFC (it is user data, FC 1, here); everything after it must be
    // supervisor data (5) or supervisor program (6).
    struct Case { bool write; bool berr; const char* name; };
    for (Case k : { Case{ false, false, "read, address error" },
                    Case{ true,  false, "write, address error" },
                    Case{ false, true,  "read, /BERR" },
                    Case{ true,  true,  "write, /BERR" } }) {
        TestCpu c(moira::CPUModel::M68010);
        p32(c, 2 * 4, kHandler);                    // vector 2: bus error
        p32(c, 3 * 4, kHandler);                    // vector 3: address error
        p16(c, kHandler, kNop);
        p16(c, kHandler + 2, 0x3239);               // MOVE.W $00003000,D1
        p32(c, kHandler + 4, 0x3000);
        p16(c, kHandler + 8, kNop);
        p16(c, kHandler + 10, kNop);
        stage(c, 0x0e50, 0);                        // MOVES.W (A0),D0 / D0,(A0)
        const uint16_t ext = k.write ? 0x0800 : 0x0000;
        p16(c, kCode + 2, ext); c.setIRC(ext);
        c.setSFC(1); c.setDFC(1);                   // user data
        c.setA(0, k.berr ? kHole : 0x4001);
        c.holeArmed = k.berr;
        c.logFc = true;
        (void)executeContained(c);
        for (int i = 0; i < 4 && c.getPC0() != kHandler + 8; i++) c.execute();
        CHECK(c.getPC0() == kHandler + 8, "68010 MOVES %s: handler not reached "
              "(PC0=%08X)", k.name, c.getPC0());
        // Everything after the faulted access: skip up to and including it.
        size_t first = 0;
        for (size_t i = 0; i < c.fcLog.size(); i++) {
            const auto& e = c.fcLog[i];
            if ((e.addr & 0xffffff) == (k.berr ? kHole : 0x4000u) ||
                (e.addr & 0xffffff) == 0x4001u) first = i + 1;
        }
        int stacked = 0, bad = 0, dataRead = -1;
        for (size_t i = first; i < c.fcLog.size(); i++) {
            const auto& e = c.fcLog[i];
            if (e.write) stacked++;
            if (!e.write && e.addr == 0x3000) dataRead = e.fc;
            if ((e.fc & 4) == 0) {
                if (bad++ == 0)
                    std::printf("    first wrong FC: %c%06X FC=%u\n",
                                e.write ? 'W' : 'R', unsigned(e.addr), e.fc);
            }
        }
        CHECK(stacked > 0 && bad == 0, "68010 MOVES %s: %d of the accesses after "
              "the fault carry a user FC (%d stacking writes)", k.name, bad, stacked);
        CHECK(dataRead == 5, "68010 MOVES %s: the handler's data read has FC %d, "
              "not 5", k.name, dataRead);
        CHECK(c.readFC() & 4, "68010 MOVES %s: readFC() is %u after the handler",
              k.name, c.readFC());
    }
}

void divslOverflowKeepsRegisters()
{
    // Inventory row 153 (upstream ce28239, reproduced 2026-11-20): upstream's
    // divlsMoira asserted on both overflow forms below, and its 32/32 form
    // overwrote the dividend. The fork had already dropped the asserts and
    // kept the registers (rows 43 and 55); this pins it on every model of
    // the 020 core, in a build with asserts or without.
    // [B] M68000PRM-1992 DIVS: on overflow V is set and the destination
    // operands are unaffected.
    for (moira::CPUModel m : { moira::CPUModel::M68020, moira::CPUModel::M68030,
                            moira::CPUModel::M68040 }) {
        for (bool wide : { false, true }) {
            TestCpu c(m);
            const uint16_t ext = wide ? 0x0c01 : 0x0800;     // D1:D0 / D0
            stage(c, 0x4c42, 0);                             // DIVS.L D2,...
            p16(c, kCode + 2, ext); c.setIRC(ext);
            c.setD(0, wide ? 0 : 0x80000000u);
            c.setD(1, wide ? 0x80000000u : 0x12345678u);
            c.setD(2, 0xffffffffu);
            const bool contained = executeContained(c);
            CHECK(contained && (c.getSR() & 2) &&
                  c.getD(0) == (wide ? 0u : 0x80000000u) &&
                  c.getD(1) == (wide ? 0x80000000u : 0x12345678u),
                  "model %d DIVS.L %s: V=%d D0=%08X D1=%08X", int(m),
                  wide ? "64/32" : "32/32", int((c.getSR() & 2) != 0),
                  c.getD(0), c.getD(1));
        }
    }
}

void watchpointsUnderMmu()
{
    // POM68K_VENDOR.md row 20: on the 68030 and 68040 data accesses go
    // through mmuRead/mmuWrite and mmu040Read/mmu040Write, which bypass
    // the readM/writeM that host upstream's watchpoint check; each of the
    // four carries its own, on the LOGICAL address. The debugger is their
    // only consumer and `debug_session_test` runs a 68000, so none of the
    // four had a gate.
    constexpr uint32_t kData = 0x40000;
    for (moira::CPUModel m : { moira::CPUModel::M68030, moira::CPUModel::M68040 }) {
        for (bool write : { false, true }) {
            TestCpu c(m);
            stage(c, write ? 0x2080 : 0x2010, 0);       // MOVE.L D0,(A0) / (A0),D0
            c.setA(0, kData);
            c.debugger.watchpoints.setAt(kData);
            c.execute();
            const std::string want = std::string(write ? "w" : "r") + "40000:4 ";
            CHECK(c.watch == want, "model %d %s watchpoint: saw \"%s\", want \"%s\"",
                  int(m), write ? "write" : "read", c.watch.c_str(), want.c_str());
        }
    }
    {   // 68030, translation on: the stop is on the logical address
        constexpr uint32_t kTc  = 0x80000000u | 0xCu << 20 | 0xAu << 12 | 0xAu << 8;
        constexpr uint32_t kTt0 = 0x00008124u;          // program space transparent
        constexpr uint32_t kTable = 0x60000, kLogical = 0x00123010, kPhysical = 0x50010;
        constexpr uint64_t kCrp = uint64_t(0x7FFF0002u) << 32 | kTable;
        for (uint32_t armed : { kLogical, kPhysical }) {
            TestCpu c(moira::CPUModel::M68030);
            p32(c, kTable, (kTable + 0x1000) | 2);      // TIA 0: a short table
            p32(c, kTable + 0x1000 + 0x123 * 4, (kPhysical & ~0xFFFu) | 1);   // TIB $123: the page
            p32(c, kPhysical, 0xCAFE1234);
            stage(c, 0x2010, 0);                        // MOVE.L (A0),D0
            c.setA(0, kLogical);
            c.mmu030(kTc, kCrp, kTt0, 0);
            c.debugger.watchpoints.setAt(armed);
            c.execute();
            CHECK(c.getD(0) == 0xCAFE1234, "68030 translated read returned %08X "
                  "(want the physical page's CAFE1234)", c.getD(0));
            const std::string want = armed == kLogical ? "r123010:4 " : "";
            CHECK(c.watch == want, "68030 watchpoint armed at %08X under "
                  "translation: saw \"%s\", want \"%s\"", armed, c.watch.c_str(),
                  want.c_str());
        }
    }
}

void externalBusError020()
{
    // POM68K_VENDOR.md row 21, held until now by `lc_boot_etalon`: a device
    // /BERR on the plain 68020 and 68EC020, which have no mode-5 loop.
    // (1) the handler's first instruction runs — the prefetch queue is
    // refilled, the faulted opcode is not replayed at the handler's PC;
    // (2) the format-$B frame carries the address of the sub-access that
    // faulted, the second word of a longword included; (3) CCR and the
    // address registers are those of the program, not a stale mode-5 save.
    for (moira::CPUModel m : { moira::CPUModel::M68020, moira::CPUModel::M68EC020 }) {
        struct Case { uint16_t opcode; uint32_t a0; const char* what; };
        for (const Case& k : { Case{ 0x2010, kHole,     "MOVE.L (A0),D0" },
                               Case{ 0x2080, kHole,     "MOVE.L D0,(A0)" },
                               Case{ 0x2080, kHole - 2, "MOVE.L D0,(A0), second word" } }) {
            TestCpu c(m);
            p32(c, 2 * 4, kHandler);
            p16(c, kHandler, 0x7655);                   // MOVEQ #$55,D3
            p16(c, kHandler + 2, kNop); p16(c, kHandler + 4, kNop);
            stage(c, k.opcode, 0x15);                   // X, Z and C set
            c.setA(0, k.a0); c.setA(1, 0x00123456);
            c.setD(0, 0x80000001);                      // a store would set N, clear Z
            c.holeArmed = true;
            const bool contained = executeContained(c);
            const uint32_t sp = c.getSP();
            CHECK(contained && c.getPC0() == kHandler && c.holeTrips == 1,
                  "model %d %s: PC0=%08X after %u /BERR (want the handler, one)",
                  int(m), k.what, c.getPC0(), c.holeTrips);
            CHECK((g16(c, sp + 6) >> 12) == 0xB && g32(c, sp + 0x10) == kHole,
                  "model %d %s: frame format $%X, fault address %08X (want $B, %08X)",
                  int(m), k.what, g16(c, sp + 6) >> 12, g32(c, sp + 0x10), kHole);
            CHECK(c.getA(0) == k.a0 && c.getA(1) == 0x00123456,
                  "model %d %s: A0=%08X A1=%08X after the fault (want %08X, 00123456)",
                  int(m), k.what, c.getA(0), c.getA(1), k.a0);
            const bool ran = executeContained(c);       // the handler's MOVEQ
            CHECK(ran && c.getD(3) == 0x55 && c.holeTrips == 1,
                  "model %d %s: the handler's first instruction did not run "
                  "(D3=%08X, %u /BERR)", int(m), k.what, c.getD(3), c.holeTrips);
            CHECK(g16(c, sp) == 0x2715,
                  "model %d %s: stacked SR %04X (want 2715, the program's CCR)",
                  int(m), k.what, g16(c, sp));
        }
    }
}

void oddStackInterruptFrame()
{
    // POM68K_VENDOR.md row 15: from the 68010 up an interrupt taken while
    // SSP is odd stacks its format-0 frame at the true addresses (masking
    // A0 is the 68000's bus, and `sst68000` holds that half), with the
    // vector offset scaled once; the RTE that follows reads the same bytes
    // back. QuickDraw's three-byte stack temporaries make an odd SP
    // routine on the LC II: the masked frame was one byte low and its RTE
    // took a format error. Held until now by `lcii_launch_etalon`.
    // The 68010 is not here: it shares the writer but its own interrupt
    // entry does not survive an odd SSP, as a 16-bit bus would not.
    constexpr uint32_t kOddSp = 0x8001, kFormatError = kHandler + 0x80;
    for (moira::CPUModel m : { moira::CPUModel::M68020, moira::CPUModel::M68030,
                            moira::CPUModel::M68040 }) {
        TestCpu c(m);
        p32(c, 27 * 4, kHandler);                       // level-3 autovector
        p32(c, 14 * 4, kFormatError);
        p16(c, kHandler, 0x4e73);                       // RTE
        p16(c, kHandler + 2, kNop);
        p16(c, kFormatError, kNop); p16(c, kFormatError + 2, kNop);
        stage(c, kNop, 0);
        for (int i = 1; i < 16; i++) p16(c, kCode + 2 * i, kNop);
        c.setSR(0x2005);                                // mask 0, Z and C
        for (int i = 0; i < 3; i++) c.execute();        // past the SR-write delay
        c.setSP(kOddSp);
        c.setIPL(3);
        for (int i = 0; i < 6 && c.getPC0() != kHandler; i++) c.execute();
        const uint32_t sp = c.getSP();
        const uint32_t ret = g32(c, sp + 2);
        CHECK(c.getPC0() == kHandler && sp == kOddSp - 8,
              "model %d: interrupt on an odd SSP: PC0=%08X SP=%08X (want %08X, %08X)",
              int(m), c.getPC0(), sp, kHandler, kOddSp - 8);
        CHECK(g16(c, sp) == 0x2005 && ret > kCode && ret < kCode + 32 && !(ret & 1) &&
              g16(c, sp + 6) == 27 * 4,
              "model %d: frame at %08X is SR=%04X PC=%08X format/vector=%04X "
              "(want 2005, an even PC in the NOPs, 006C)",
              int(m), sp, g16(c, sp), ret, g16(c, sp + 6));
        c.setIPL(0);
        const bool contained = executeContained(c);     // RTE
        CHECK(contained && c.getPC0() == ret && c.getSP() == kOddSp &&
              (c.getSR() & 0xFF1F) == 0x2005,
              "model %d: RTE from the odd frame: PC0=%08X SP=%08X SR=%04X "
              "(want %08X, %08X, 2005; %08X is the format error handler)",
              int(m), c.getPC0(), c.getSP(), c.getSR(), ret, kOddSp, kFormatError);
    }
}

void prefetchPipeAndIplPoll030()
{
    // POM68K_VENDOR.md row 11, held until now by `lcii_boot_etalon`.
    //
    // (1) [C] LC II ROM $A416AA: `pmove (A3),tc; nop; bne; jmp (A5)` — the
    // ROM turns translation on and expects the three words behind the
    // PMOVE to be the ones already in the pipe, since the new map sends
    // their logical addresses elsewhere.
    // [F] The core carries four words, not three; nothing here measures a
    // 68030's pipe. Settled by: a capture of the fetches after a PMOVE.
    // The cell maps the code page onto a page holding other instructions
    // and counts how many of the old ones still run.
    {
        constexpr uint32_t kTc = 0x80000000u | 0xCu << 20 | 0xAu << 12 | 0xAu << 8;
        constexpr uint32_t kTable = 0x60000, kShadow = 0x70000, kTcAt = 0x50000;
        constexpr uint64_t kCrp = uint64_t(0x7FFF0002u) << 32 | kTable;
        TestCpu c(moira::CPUModel::M68030);
        p32(c, kTable, (kTable + 0x1000) | 2);          // TIA 0: a short table
        p32(c, kTable + 0x1000 + 0 * 4, 0x0000 | 1);    // page 0 on itself
        p32(c, kTable + 0x1000 + 1 * 4, kShadow | 1);   // the code page elsewhere
        p32(c, kTcAt, kTc);
        stage(c, 0xf010, 0);                            // PMOVE (A0),TC
        p16(c, kCode + 2, 0x4000); c.setIRC(0x4000);
        for (int i = 1; i <= 7; i++) {                  // MOVEQ #i,Di / #$50+i,Di
            p16(c, kCode + 2 + 2 * i, uint16_t(0x7000 | i << 9 | i));
            p16(c, kShadow + (kCode & 0xFFF) + 2 + 2 * i, uint16_t(0x7050 | i << 9 | i));
        }
        c.setA(0, kTcAt);
        c.mmu030(0, kCrp, 0, 0);
        for (int i = 0; i < 8; i++) c.execute();
        bool ok = true;
        for (int i = 1; i <= 7; i++)
            ok = ok && c.getD(i) == uint32_t(i <= 4 ? i : 0x50 + i);
        CHECK(ok, "68030 pipe across PMOVE to TC: D1-D7 = %X %X %X %X %X %X %X "
              "(want 1 2 3 4 from the old map, then 55 56 57 from the new)",
              c.getD(1), c.getD(2), c.getD(3), c.getD(4), c.getD(5), c.getD(6),
              c.getD(7));
    }
    // (2) [C] LC II ROM $A00820, TimeDBRA: a `dbra D0,*` with no data
    // access must still sample the interrupt lines at every boundary. One
    // more iteration runs (the pin's own latency), not the rest of the loop.
    for (moira::CPUModel m : { moira::CPUModel::M68020, moira::CPUModel::M68030,
                            moira::CPUModel::M68040 }) {
        TestCpu c(m);
        p32(c, 27 * 4, kHandler);
        p16(c, kHandler, kNop); p16(c, kHandler + 2, kNop);
        stage(c, 0x51c8, 0);                            // DBRA D0,*
        p16(c, kCode + 2, 0xfffe); c.setIRC(0xfffe);
        c.setD(0, 1000);
        c.setSR(0x2000);
        for (int i = 0; i < 5; i++) c.execute();        // past the SR-write delay
        const uint32_t before = c.getD(0);
        c.setIPL(3);
        for (int i = 0; i < 20 && c.getPC0() != kHandler; i++) c.execute();
        CHECK(c.getPC0() == kHandler && before - c.getD(0) == 1,
              "model %d: interrupt inside a DBRA loop: PC0=%08X after %u more "
              "iteration(s) (want the handler after 1)", int(m), c.getPC0(),
              before - c.getD(0));
    }
}

void cas2IsReadModifyWrite030()
{
    // [D] WinUAE @ Hatari e77819f7, "random/tt seed=5109 #261" of the
    // 2026-11-13 fresh-seed sweep: on the 68030 the operand reads of CAS2
    // are the read half of a locked read-modify-write, like CAS's and
    // TAS's — the page is probed for writing and marked modified even
    // when the compare fails and nothing is written. The fork had CAS and
    // TAS; CAS2 left both descriptors with U alone. The prefetch that ends
    // the instruction is not part of the cycle: the code page stays clean.
    constexpr uint32_t kTc = 0x80000000u | 0xCu << 20 | 0xAu << 12 | 0xAu << 8;
    constexpr uint32_t kTable = 0x60000, kPages = kTable + 0x1000;
    constexpr uint64_t kCrp = uint64_t(0x7FFF0002u) << 32 | kTable;
    constexpr uint32_t kFirst = 0x40010, kSecond = 0x41010;
    struct Case { uint16_t opcode; uint16_t ext[3]; unsigned words; bool second;
                  const char* what; };
    for (const Case& k : { Case{ 0x0cfc, {0x8080, 0x90c1, kNop}, 2, true,  "CAS2.W, compare failing" },
                           Case{ 0x0cd0, {0x0080, kNop, kNop},   1, false, "CAS.W, compare failing" },
                           Case{ 0x4ad0, {kNop, kNop, kNop},     0, false, "TAS" } }) {
        TestCpu c(moira::CPUModel::M68030);
        p32(c, kTable, kPages | 2);                     // TIA 0: a short table
        for (uint32_t page : { 0u, 1u, 0x40u, 0x41u })  // each on itself
            p32(c, kPages + page * 4, page << 12 | 1);
        p16(c, kFirst, 0x1234); p16(c, kSecond, 0x5678);
        stage(c, k.opcode, 0);
        for (unsigned i = 0; i < 3; i++) p16(c, kCode + 2 + 2 * i, k.ext[i]);
        c.setIRC(k.ext[0]);
        c.setA(0, kFirst); c.setA(1, kSecond);
        c.setD(0, 0); c.setD(1, 0);                     // neither compare matches
        c.mmu030(kTc, kCrp, 0, 0);
        const bool contained = executeContained(c);
        const auto modified = [&](uint32_t page) {
            return (g32(c, kPages + page * 4) & 0x10) != 0;
        };
        CHECK(contained && c.faults == 0 && c.getPC0() == kCode + 2 + 2 * k.words,
              "68030 %s under translation: PC0=%08X, %d fault(s)", k.what,
              c.getPC0(), c.faults);
        CHECK(modified(0x40) && modified(0x41) == k.second && !modified(1),
              "68030 %s: M on the operand pages %d/%d (want 1/%d), on the code "
              "page %d (want 0)", k.what, modified(0x40), modified(0x41),
              k.second, modified(1));
    }
}

void lockEndsWithTheInstruction040()
{
    // The 68040's locked read-modify-write (TAS, CAS, CAS2) translates its
    // reads as writes, so the operand page is marked modified
    // (MC68040UM-1993 §7.4.5 for the locked sequence). The lock ends with
    // the instruction. A read that follows — here the data door generated
    // code uses, which runs no instruction-start bookkeeping — must leave
    // its page with U alone. Before 2026-11-14 the flag outlived the
    // instruction and the x86-64 translator set M on every page a block read
    // after TAS ("040 random/ttvar/8k seed=2026111470 #927").
    constexpr uint32_t kRoot = 0x4000, kPtr = 0x4200, kPages = 0x6000;
    constexpr uint32_t kFirst = 0x20010, kSecond = 0x21010, kRead = 0x30000;
    struct Case { uint16_t opcode; uint16_t ext[3]; unsigned words; const char* what; };
    for (const Case& k : { Case{ 0x4ad0, {kNop, kNop, kNop},     0, "TAS (A0)" },
                           Case{ 0x0cd0, {0x0080, kNop, kNop},   1, "CAS.W, compare failing" },
                           Case{ 0x0cfc, {0x8080, 0x90c1, kNop}, 2, "CAS2.W, compare failing" } }) {
        TestCpu c(moira::CPUModel::M68040);
        p32(c, kRoot, kPtr | 2);
        p32(c, kPtr, kPages | 2);
        for (uint32_t page = 0; page < 64; page++)       // identity, 256 KB
            p32(c, kPages + page * 4, page << 12 | 1);
        p16(c, kFirst, 0x1234); p16(c, kSecond, 0x5678); p16(c, kRead, 0x9ABC);
        stage(c, k.opcode, 0);
        for (unsigned i = 0; i < 3; i++) p16(c, kCode + 2 + 2 * i, k.ext[i]);
        c.setIRC(k.ext[0]);
        c.setA(0, kFirst); c.setA(1, kSecond);
        c.setD(0, 0); c.setD(1, 0);                      // neither compare matches
        c.setURP040(kRoot); c.setSRP040(kRoot);
        c.setTC040(0x8000);
        const bool contained = executeContained(c);
        const auto desc = [&](uint32_t addr) { return g32(c, kPages + (addr >> 12) * 4); };
        CHECK(contained && c.faults == 0 && c.getPC0() == kCode + 2 + 2 * k.words,
              "68040 %s under translation: PC0=%08X, %d fault(s)", k.what,
              c.getPC0(), c.faults);
        CHECK((desc(kFirst) & 0x10) != 0,
              "68040 %s: the operand page is not modified (%08X)", k.what, desc(kFirst));
        uint32_t value = 0;
        const bool read = c.supervisorDataRead16(kRead, value);
        CHECK(read && value == 0x9ABC && (desc(kRead) & 0x18) == 0x08,
              "68040 %s, then a data read: read=%d value=%04X descriptor %08X "
              "(want U without M)", k.what, read, value, desc(kRead));
    }
}

void indirectPointerNotLocked040()
{
    // The pointer of a memory-indirect EA is an ordinary read, fetched
    // before the locked operand read of CAS or TAS begins. The fork armed
    // the lock before the whole readOp, so an ATC fault on the pointer was
    // reported as a locked write (SSW $06xx) where the oracle has a plain
    // read ($05xx). [D] WinUAE cpuemu_31.c op_0af0 @ Hatari e77819f7
    // ("040 random/identity seed=75010 #758", fresh-seed sweep 2026-11-14).
    constexpr uint32_t kRoot = 0x4000, kPtr = 0x4200, kPages = 0x6000;
    constexpr uint32_t kBad = 0x3f000;              // pointer on an invalid page
    struct Case { uint16_t opcode; uint16_t ext[4]; const char* what; };
    for (const Case& k : { Case{ 0x0af0, {0x0040, 0x01f1, 0x0003, 0xf000}, "CAS.B D0,D1,([bd.L])" },
                           Case{ 0x4af0, {0x01f1, 0x0003, 0xf000, kNop},   "TAS ([bd.L])" } }) {
        TestCpu c(moira::CPUModel::M68040);
        p32(c, kRoot, kPtr | 2);
        p32(c, kPtr, kPages | 2);
        for (uint32_t page = 0; page < 64; page++)       // identity, 256 KB
            p32(c, kPages + page * 4, page << 12 | 1);
        p32(c, kPages + (kBad >> 12) * 4, 0);            // ... but the pointer's page
        p32(c, 2 * 4, kHandler);
        p16(c, kHandler, kNop);
        stage(c, k.opcode, 0);
        for (unsigned i = 0; i < 4; i++) p16(c, kCode + 2 + 2 * i, k.ext[i]);
        c.setIRC(k.ext[0]);
        c.setURP040(kRoot); c.setSRP040(kRoot);
        c.setTC040(0x8000);
        const bool contained = executeContained(c);
        const uint32_t sp = c.getSP();
        const uint16_t format = g16(c, sp + 6), ssw = g16(c, sp + 0x0c);
        const uint32_t fa = g32(c, sp + 0x14);
        CHECK(contained && c.getPC0() == kHandler && format == 0x7008 &&
              fa == kBad && (ssw & 0x0700) == 0x0500,
              "68040 %s, pointer fetch on an invalid page: format %04X, fault "
              "address %08X (want %08X), SSW %04X (want ATC|RW, no LK)", k.what,
              format, fa, kBad, ssw);
    }
}

void bkptMakesNoDataAccess()
{
    // BKPT on the 68020 up takes the illegal-instruction exception without
    // a data access: its breakpoint acknowledge is a CPU-space cycle. The
    // fork kept upstream's stand-in, a data read at PC through the MMU, so
    // a 68040 whose code arrives through a transparent ITT window walked the
    // tables of the BKPT's page — U set there, or an access fault when that
    // page is invalid in the tables. [D] WinUAE (op_illg) @ Hatari e77819f7;
    // "040 random/ttvar seed=77012 #456", fresh-seed sweep 2026-11-14.
    constexpr uint32_t kRoot = 0x4000, kPtr = 0x4200, kPages = 0x6000;
    TestCpu c(moira::CPUModel::M68040);
    p32(c, kRoot, kPtr | 2);
    p32(c, kPtr, kPages | 2);
    for (uint32_t page = 0; page < 64; page++)
        p32(c, kPages + page * 4, page << 12 | 1);
    p32(c, kPages + (kCode >> 12) * 4, 0);          // code page invalid in the tables
    p32(c, 4 * 4, kHandler);
    p16(c, kHandler, kNop);
    stage(c, 0x4848, 0);                            // BKPT #0
    c.setURP040(kRoot); c.setSRP040(kRoot);
    c.setITT0(0x00ffc000);                          // every instruction fetch transparent
    c.setTC040(0x8000);
    const bool contained = executeContained(c);
    const uint32_t sp = c.getSP();
    CHECK(contained && c.getPC0() == kHandler && g16(c, sp + 6) == 0x0010 &&
          g32(c, sp + 2) == kCode,
          "68040 BKPT on a page reached through ITT0 only: PC0=%08X (want %08X), "
          "format %04X (want 0010), stacked PC %08X (want %08X)", c.getPC0(),
          kHandler, g16(c, sp + 6), g32(c, sp + 2), kCode);
}

void bitfieldStoreRestarts040()
{
    // A 68040 store that is not the instruction's marked last write
    // restarts it: the frame holds the instruction's own PC and the flags
    // it started with. BFCHG/BFCLR/BFSET/BFINS store through
    // x_put_bitfield with mmu_restart still true, as do JSR's push, MOVEM
    // to memory and MOVEP; the fork took a bit-field store fault as a last
    // write (next PC, field flags kept). [D] WinUAE cpuemu_31.c op_eed0,
    // op_4e90 and m68k_run_mmu040 @ Hatari e77819f7 ("040 random/ttvar
    // seed=79012 #1850", fresh-seed sweep 2026-11-14).
    constexpr uint32_t kRoot = 0x4000, kPtr = 0x4200, kPages = 0x6000;
    constexpr uint32_t kRo = 0x21000;               // a write-protected page
    struct Case { uint16_t opcode, ext; bool user; const char* what; };
    for (const Case& k : { Case{ 0xeed0, 0x0008, false, "BFSET (A0){0:8}" },
                           Case{ 0x4e90, kNop,   true,  "JSR (A0) from user mode, push faulting" } }) {
        TestCpu c(moira::CPUModel::M68040);
        p32(c, kRoot, kPtr | 2);
        p32(c, kPtr, kPages | 2);
        for (uint32_t page = 0; page < 64; page++)
            p32(c, kPages + page * 4, page << 12 | 1);
        p32(c, kPages + (kRo >> 12) * 4, kRo | 4 | 1);  // W: write-protected
        p32(c, 2 * 4, kHandler);
        p16(c, kHandler, kNop);
        stage(c, k.opcode, 0x1b);                   // X N V C set
        p16(c, kCode + 2, k.ext); c.setIRC(k.ext);
        c.setURP040(kRoot); c.setSRP040(kRoot);
        c.setTC040(0x8000);
        if (k.user) {
            c.setA(0, 0x3000);
            c.setSR(0x001b);
            c.setUSP(kRo + 0x10); c.setSP(kRo + 0x10);
        } else {
            c.setA(0, kRo);
        }
        const bool contained = executeContained(c);
        const uint32_t sp = c.getSP();
        const uint16_t sr = g16(c, sp), format = g16(c, sp + 6);
        const uint32_t pc = g32(c, sp + 2);
        CHECK(contained && c.getPC0() == kHandler && format == 0x7008 && pc == kCode &&
              (sr & 0x1f) == 0x1b && (!k.user || c.getUSP() == kRo + 0x10),
              "68040 %s: format %04X (want 7008), stacked PC %08X (want %08X), "
              "CCR %02X (want 1B), USP %08X", k.what, format, pc, kCode, sr & 0x1f,
              c.getUSP());
    }
}

void srWriteRefillIsAFetch()
{
    // ANDI/ORI/EORI to SR refill the pipe after the SR write with an
    // instruction fetch, as MOVE to SR does. Upstream's stand-in was a
    // data-space read, translated with the new SR's function code: after
    // EORI #$2000,SR to user mode, a 68040 whose code is covered by an ITT
    // and whose DTT matches supervisor accesses only walked the tables for
    // the next instruction's page. [D] WinUAE @ Hatari e77819f7 (no data
    // access); "040 random/tt seed=79011 #1749", fresh-seed sweep 2026-11-14.
    constexpr uint32_t kRoot = 0x4000, kPtr = 0x4200, kPages = 0x6000;
    TestCpu c(moira::CPUModel::M68040);
    p32(c, kRoot, kPtr | 2);
    p32(c, kPtr, kPages | 2);
    for (uint32_t page = 0; page < 64; page++)
        p32(c, kPages + page * 4, page << 12 | 1);
    stage(c, 0x0a7c, 0);                            // EORI #$2000,SR
    p16(c, kCode + 2, 0x2000); c.setIRC(0x2000);
    c.setURP040(kRoot); c.setSRP040(kRoot);
    c.setITT0(0x00ffc000);                          // all instruction fetches
    c.setDTT0(0x00ffa000);                          // supervisor data only
    c.setTC040(0x8000);
    const bool contained = executeContained(c);
    CHECK(contained && c.getPC0() == kCode + 4 && (c.getSR() & 0x2000) == 0 &&
          (g32(c, kRoot) & 8) == 0 && (g32(c, kPages + (kCode >> 12) * 4) & 8) == 0,
          "68040 EORI #$2000,SR into user mode: PC0=%08X SR=%04X, root %08X and code "
          "page %08X descriptors (want U clear: no table walk)", c.getPC0(), c.getSR(),
          g32(c, kRoot), g32(c, kPages + (kCode >> 12) * 4));
}

void rteRerunTakesNoTrace040()
{
    // An RTE that resumes a 68040 format-$7 frame reruns the faulted
    // instruction with no trace in between, even when an SR it loaded on
    // the way (here a format-$1 throwaway frame with T0 set) armed the
    // one-shot trace. [D] WinUAE newcpu.c do_specialties (SPCFLAG_MMURESTART)
    // @ Hatari e77819f7; "040 core/off seed=81001 #1204", fresh-seed sweep
    // 2026-11-14.
    TestCpu c(moira::CPUModel::M68040);
    p32(c, 9 * 4, kHandler);                        // trace
    p16(c, kHandler, kNop);
    p16(c, 0x3000, kNop);
    stage(c, 0x4e73, 0);                            // RTE
    constexpr uint32_t sp = 0x1800;
    c.setSP(sp);
    p16(c, sp + 0, 0x6000); p32(c, sp + 2, 0x0000dead); p16(c, sp + 6, 0x1000);   // throwaway, T0|S
    p16(c, sp + 8, 0x2000); p32(c, sp + 10, 0x3000);    p16(c, sp + 14, 0x7008);  // access error
    for (uint32_t i = 16; i < 8 + 60; i += 2) p16(c, sp + i, 0);
    const bool contained = executeContained(c);
    CHECK(contained && c.getPC0() == 0x3000 && c.getSP() == sp + 8 + 60 &&
          c.getSR() == 0x2000,
          "68040 RTE through a T0 throwaway frame into a format-$7 frame: "
          "PC0=%08X (want 00003000) SP=%08X (want %08X) SR=%04X (want 2000), "
          "no trace before the rerun", c.getPC0(), c.getSP(), sp + 8 + 60, c.getSR());
    // The trace is deferred, not dropped: the rerun instruction (the NOP at
    // $3000) runs, then vector 9 is taken with the next PC stacked.
    // "loop core/off seed=61 #128 trace=t1", 2026-11-20.
    bool later = contained && executeContained(c);
    const uint32_t afterNop = c.getPC0();
    later = later && executeContained(c);
    const uint32_t tsp = c.getSP();
    CHECK(later && afterNop == 0x3002 && c.getPC0() == kHandler && tsp == sp + 8 + 60 - 12 &&
          g16(c, tsp + 6) == 0x2024 && g32(c, tsp + 2) == 0x3002,
          "68040 RTE into a format-$7 frame: the deferred trace follows the rerun; "
          "PC0 after the rerun %08X (want 00003002), then %08X (want %08X), SP=%08X "
          "(want %08X) format/vector=%04X stacked PC=%08X", afterNop, c.getPC0(), kHandler,
          tsp, sp + 8 + 60 - 12, g16(c, tsp + 6), g32(c, tsp + 2));
}

void traceFollowsTheTracedInstruction()
{
    // An instruction that begins with T1 set takes vector 9 when it
    // completes, before the next one starts, and from the 68020 up the
    // frame is the six-word format $2 whose instruction address is the
    // traced instruction's.
    // [B] MC68020UM-1992 §6.1.7 and Table 6-5; MC68030UM-1989 §8.1.7 and
    //     Table 8-7; MC68040UM-1993 §8.2.6.
    // [D] WinUAE @ Hatari e77819f7 — "trace=t1" cells of tests/cpu_vectors.
    // Until 2026-11-19 the 68020 and 68030 stacked a format $0 frame, and
    // the 68040 ran one more instruction before the exception. The 68000
    // and 68010 keep their four-word frames.
    struct Row { moira::CPUModel model; const char* name; bool sixWord; };
    static const Row rows[] = {
        { moira::CPUModel::M68000, "68000", false }, { moira::CPUModel::M68010, "68010", false },
        { moira::CPUModel::M68020, "68020", true },  { moira::CPUModel::M68030, "68030", true },
        { moira::CPUModel::M68040, "68040", true },
    };
    for (const Row& r : rows) {
        TestCpu c(r.model);
        p32(c, 9 * 4, kHandler);
        p16(c, kHandler, kNop);
        stage(c, 0x7255, 0);                        // MOVEQ #$55,D1
        p16(c, kCode + 2, 0x7466);                  // MOVEQ #$66,D2
        c.setIRC(0x7466);
        c.setD(1, 0); c.setD(2, 0);
        c.setSR(0xa700);
        constexpr uint32_t sp0 = 0x8000;
        bool contained = executeContained(c);       // the traced MOVEQ
        contained = contained && executeContained(c);   // its trace exception
        const uint32_t sp = c.getSP();
        const uint32_t size = r.sixWord ? 12 : (r.model == moira::CPUModel::M68000 ? 6 : 8);
        const bool frame = r.model == moira::CPUModel::M68000
            ? true
            : g16(c, sp + 6) == (r.sixWord ? 0x2024 : 0x0024);
        CHECK(contained && c.getPC0() == kHandler && c.getD(1) == 0x55 && c.getD(2) == 0 &&
              sp == sp0 - size && g16(c, sp) == 0xa700 && g32(c, sp + 2) == kCode + 2 &&
              frame && (!r.sixWord || g32(c, sp + 8) == kCode) && c.getSR() == 0x2700,
              "%s trace: PC0=%08X (want %08X) D1=%X D2=%X (want 55, 0: one instruction) "
              "SP=%08X (want %08X) stacked PC=%08X (want %08X) format/vector=%04X "
              "address=%08X (want %08X on a six-word frame)",
              r.name, c.getPC0(), kHandler, c.getD(1), c.getD(2), sp, sp0 - size,
              g32(c, sp + 2), kCode + 2, g16(c, sp + 6), g32(c, sp + 8), kCode);
    }
}

void traceOnChangeOfFlow()
{
    // T1 = 0, T0 = 1: only an instruction that changes the flow takes the
    // trace exception; the frame is the same format $2, its address the
    // instruction's.
    // [B] MC68020UM-1992 §6.1.7; MC68030UM-1989 §8.1.7; MC68040UM-1993
    //     §8.2.6 (which adds NOP, among others, on the 68040).
    // [D] WinUAE @ Hatari e77819f7 — "trace=t0" cells of tests/cpu_vectors
    //     for the 68030 and 68040. An instruction trap is traced after its
    //     own exception on the 68020/68030 and not on the 68040.
    // Until 2026-11-20 T0 was stored and had no effect.
    struct Row { moira::CPUModel model; const char* name; bool is040; };
    static const Row rows[] = {
        { moira::CPUModel::M68020, "68020", false }, { moira::CPUModel::M68030, "68030", false },
        { moira::CPUModel::M68040, "68040", true },
    };
    constexpr uint32_t sp0 = 0x8000, kTrapHandler = kHandler + 0x40;
    for (const Row& r : rows) {
        struct Case { uint16_t opcode; const char* what; int traced; uint32_t next; };
        // traced: 1 always, 0 never, 2 on the 68040 only
        static const Case cases[] = {
            { 0x6002, "BRA.S", 1, kCode + 4 },
            { 0x7255, "MOVEQ", 0, kCode + 2 },
            { kNop,   "NOP",   2, kCode + 2 },
        };
        for (const Case& k : cases) {
            TestCpu c(r.model);
            p32(c, 9 * 4, kHandler);
            p16(c, kHandler, kNop);
            stage(c, k.opcode, 0);
            c.setSR(0x6700);
            bool contained = executeContained(c);
            contained = contained && executeContained(c);   // the trace, or the next NOP
            const bool want = k.traced == 1 || (k.traced == 2 && r.is040);
            const uint32_t sp = c.getSP();
            const bool traced = c.getPC0() == kHandler && sp == sp0 - 12 &&
                                g16(c, sp + 6) == 0x2024 && g32(c, sp + 2) == k.next &&
                                g32(c, sp + 8) == kCode && c.getSR() == 0x2700;
            const bool plain = c.getPC0() == k.next + 2 && sp == sp0 && c.getSR() == 0x6700;
            CHECK(contained && (want ? traced : plain),
                  "%s T0, %s: want %s; PC0=%08X SP=%08X SR=%04X format/vector=%04X "
                  "stacked PC=%08X address=%08X",
                  r.name, k.what, want ? "a trace exception" : "no trace", c.getPC0(), sp,
                  c.getSR(), g16(c, sp + 6), g32(c, sp + 2), g32(c, sp + 8));
        }
        {   // TRAP #0: its own exception first, then the trace below the 68040
            TestCpu c(r.model);
            p32(c, 9 * 4, kHandler);
            p32(c, 32 * 4, kTrapHandler);
            p16(c, kHandler, kNop);
            p16(c, kTrapHandler, kNop);
            stage(c, 0x4e40, 0);
            c.setSR(0x6700);
            bool contained = executeContained(c);
            contained = contained && executeContained(c);
            const uint32_t sp = c.getSP();
            const bool both = c.getPC0() == kHandler && sp == sp0 - 8 - 12 &&
                              g16(c, sp + 6) == 0x2024 && g32(c, sp + 2) == kTrapHandler &&
                              g32(c, sp + 8) == kCode && g16(c, sp + 12) == 0x6700 &&
                              g16(c, sp + 12 + 6) == 0x0080;
            const bool trapOnly = c.getPC0() == kTrapHandler + 2 && sp == sp0 - 8 &&
                                  g16(c, sp) == 0x6700 && g16(c, sp + 6) == 0x0080;
            CHECK(contained && (r.is040 ? trapOnly : both),
                  "%s T0, TRAP #0: want %s; PC0=%08X SP=%08X (entry %08X) top "
                  "format/vector=%04X", r.name,
                  r.is040 ? "the trap alone" : "the trap, then the trace", c.getPC0(), sp, sp0,
                  g16(c, sp + 6));
        }
    }
}

void mullSameRegister()
{
    // MULS.L / MULU.L with a 64-bit product whose two halves name the same
    // register: the 68020 and 68030 leave the high half there, the 68040 the
    // low half. [D] WinUAE newcpu_common.c m68k_mull @ Hatari e77819f7
    // ("68040 is different"); class-balanced cells of 2026-11-20.
    struct Row { moira::CPUModel model; const char* name; bool low; };
    static const Row rows[] = {
        { moira::CPUModel::M68020, "68020", false }, { moira::CPUModel::M68030, "68030", false },
        { moira::CPUModel::M68040, "68040", true },
    };
    for (const Row& r : rows) {
        for (const bool isSigned : { false, true }) {
            TestCpu c(r.model);
            stage(c, 0x4c03, 0);                    // MUL?.L D3,D0:D0
            const uint16_t ext = uint16_t(0x0400 | (isSigned ? 0x0800 : 0));
            p16(c, kCode + 2, ext); c.setIRC(ext);
            p16(c, kCode + 4, kNop);
            c.setD(3, 0x88af2b9c); c.setD(0, 0x0ad4a9c2);
            const bool contained = executeContained(c);
            const uint32_t want = isSigned ? (r.low ? 0x6db90838 : 0xfaf3b9a5)
                                           : (r.low ? 0x6db90838 : 0x05c86367);
            CHECK(contained && c.getD(0) == want,
                  "%s %s D3,D0:D0: D0=%08X (want %08X, the %s half)", r.name,
                  isSigned ? "MULS.L" : "MULU.L", c.getD(0), want, r.low ? "low" : "high");
        }
    }
}

void cas2SameCompareRegister()
{
    // CAS2 with Dc1 = Dc2 and a failing comparison: the register receives
    // memory operand 1 on the 68020 and 68030 ([B] M68000PRM-1992 CAS2),
    // operand 2 on the 68040 ([D] WinUAE gencpu.c i_CAS2 @ Hatari e77819f7,
    // "68040: register update order swapped"; ruling D44).
    struct Row { moira::CPUModel model; const char* name; uint32_t want; };
    static const Row rows[] = {
        { moira::CPUModel::M68020, "68020", 0x11111234 }, { moira::CPUModel::M68030, "68030", 0x11111234 },
        { moira::CPUModel::M68040, "68040", 0x11115678 },
    };
    for (const Row& r : rows) {
        TestCpu c(r.model);
        stage(c, 0x0cfc, 0);                        // CAS2.W D0:D0,D1:D2,(A0):(A1)
        p16(c, kCode + 2, 0x8040); p16(c, kCode + 4, 0x9080);
        p16(c, kCode + 6, kNop);
        c.setIRC(0x8040);
        p16(c, 0x30000, 0x1234); p16(c, 0x30010, 0x5678);
        c.setA(0, 0x30000); c.setA(1, 0x30010);
        c.setD(0, 0x1111ffff); c.setD(1, 0); c.setD(2, 0);
        const bool contained = executeContained(c);
        CHECK(contained && c.getD(0) == r.want && g16(c, 0x30000) == 0x1234,
              "%s CAS2.W D0:D0 with a failing comparison: D0=%08X (want %08X)", r.name,
              c.getD(0), r.want);
    }
}

void packUnpkFaultRestoresAn()
{
    // [D] WinUAE gencpu.c i_PACK/i_UNPK @ Hatari e77819f7 (cpuemu_31/32
    // op_8148/op_8188), "040 random/identity seed=5205 #483" of the
    // 2026-11-13 fresh-seed sweep. PACK -(A3),-(A2) reads its two source
    // bytes as ONE word at A3-2; a fault there restarts the instruction
    // with A3 back at its entry value and SIZE=word in the SSW. The fork
    // decremented A3 by hand without arming its fixup, and read two bytes.
    // UNPK -(A3),-(A2) writes its pair as ONE word at A2-2, the
    // instruction's last write: the frame holds the next PC and A2 stays
    // decremented, A3 too (its read completed).
    for (auto model : {moira::CPUModel::M68030, moira::CPUModel::M68040}) {
        const bool is040 = model == moira::CPUModel::M68040;
        {   // PACK: the source word read takes /BERR, then RTE re-runs it
            TestCpu c(model);
            p32(c, 2 * 4, kHandler);
            p16(c, kHandler, 0x4e73);               // RTE
            stage(c, 0x854b, 0);                    // PACK -(A3),-(A2),#$0000
            p16(c, kCode + 2, 0x0000); c.setIRC(0x0000);
            p16(c, kHole, 0x0304);                  // packs to $34
            c.setA(3, kHole + 2); c.setA(2, 0x30000);
            c.holeArmed = true;
            const bool contained = executeContained(c);
            const uint32_t sp = c.getSP();
            const uint16_t ssw = is040 ? g16(c, sp + 12) : g16(c, sp + 10);
            const bool wordSize = is040 ? (ssw & 0x60) == 0x40 : (ssw & 0x30) == 0x20;
            // In the handler the 68040 shows A3 at its entry value. The
            // 68030 shows it one more long below: the oracle's frame carries
            // the fixup encoding (gencpu gives PACK no size, so long) and
            // mmu030fixupmod applies it until the RTE takes it back
            // (cpummu030.c @ Hatari e77819f7). Only the round trip is
            // architectural.
            const uint32_t a3Handler = is040 ? kHole + 2 : kHole - 2;
            CHECK(contained && c.getPC0() == kHandler && c.getA(3) == a3Handler &&
                  c.getA(2) == 0x30000 && g32(c, sp + 2) == kCode && wordSize,
                  "%s PACK -(A3),-(A2) source fault: PC0=%08X A3=%08X (want %08X) "
                  "A2=%08X stacked PC %08X (want %08X) SSW %04X (want word size)",
                  is040 ? "68040" : "68030", c.getPC0(), c.getA(3), a3Handler,
                  c.getA(2), g32(c, sp + 2), kCode, ssw);
            c.holeArmed = false;
            for (int i = 0; i < 3 && c.getPC0() != kCode + 4; i++)
                if (!executeContained(c)) break;
            CHECK(c.getPC0() == kCode + 4 && c.getA(3) == kHole &&
                  c.getA(2) == 0x2ffff && c.mem[0x2ffff] == 0x34,
                  "%s PACK after RTE: PC0=%08X (want %08X) A3=%08X (want %08X) "
                  "A2=%08X (want 0002FFFF) byte %02X (want 34)",
                  is040 ? "68040" : "68030", c.getPC0(), kCode + 4, c.getA(3),
                  kHole, c.getA(2), c.mem[0x2ffff]);
        }
        {   // UNPK: the destination word write takes /BERR
            TestCpu c(model);
            p32(c, 2 * 4, kHandler);
            p16(c, kHandler, kNop); p16(c, kHandler + 2, kNop);
            stage(c, 0x858b, 0);                    // UNPK -(A3),-(A2),#$0000
            p16(c, kCode + 2, 0x0000); c.setIRC(0x0000);
            c.setA(3, 0x30001); c.setA(2, kHole + 2);
            c.holeArmed = true; c.holeWritesOnly = true;
            c.traceBus = true; c.busBase = kHole;
            const bool contained = executeContained(c);
            const uint32_t sp = c.getSP();
            CHECK(contained && c.getPC0() == kHandler && c.getA(2) == kHole &&
                  c.getA(3) == 0x30000 && c.holeTrips == 1 && c.bus == "W0" &&
                  g32(c, sp + 2) == kCode + 4,
                  "%s UNPK -(A3),-(A2) destination fault: PC0=%08X A2=%08X (want %08X) "
                  "A3=%08X (want 00030000), %u /BERR(s) (want 1), bus \"%s\" (want "
                  "\"W0\"), stacked PC %08X (want %08X, last write)",
                  is040 ? "68040" : "68030", c.getPC0(), c.getA(2), kHole,
                  c.getA(3), c.holeTrips, c.bus.c_str(), g32(c, sp + 2), kCode + 4);
        }
    }
    // The 68030's short frame stacks the pending store zero-extended: gencpu
    // computes both from a uae_u16 ("random/identity seed=76008 #1852" of
    // the 2026-11-14 sweep: $0000009A, the fork had $FFFFFF9A).
    struct Store { uint16_t opcode, adj; uint8_t hi, lo; uint32_t a3, a2, want; const char* what; };
    for (const Store& k : { Store{ 0x854b, 0x0000, 0x09, 0x0a, 0x30002, kHole + 1, 0x0000009a,
                                   "PACK -(A3),-(A2) byte $9A" },
                            Store{ 0x858b, 0x7700, 0x98, 0x00, 0x30001, kHole + 2, 0x00008008,
                                   "UNPK -(A3),-(A2),#$7700 word $8008" } }) {
        TestCpu c(moira::CPUModel::M68030);
        p32(c, 2 * 4, kHandler);
        p16(c, kHandler, kNop);
        stage(c, k.opcode, 0);
        p16(c, kCode + 2, k.adj); c.setIRC(k.adj);
        c.mem[0x30000] = k.hi; c.mem[0x30001] = k.lo;
        c.setA(3, k.a3); c.setA(2, k.a2);
        c.holeArmed = true; c.holeWritesOnly = true;
        const bool contained = executeContained(c);
        const uint32_t sp = c.getSP();
        CHECK(contained && c.getPC0() == kHandler && g16(c, sp + 6) == 0xa008 &&
              g32(c, sp + 0x18) == k.want,
              "68030 %s, store fault: format %04X (want A008), data output "
              "buffer %08X (want %08X)", k.what, g16(c, sp + 6), g32(c, sp + 0x18), k.want);
    }
}

void cas2ExtensionLog030()
{
    // [D] WinUAE gencpu.c i_CAS2 @ Hatari e77819f7 — "op=CAS2_W #4168" of the
    // addressing-mode cells under the MMU (2026-11-20). CAS2 fetches its
    // two extension words as one long: a fault on the first operand read
    // stacks a format-$B access log of ONE entry, the second in flight.
    // The fork logged two words.
    TestCpu c(moira::CPUModel::M68030);
    p32(c, 2 * 4, kHandler);
    p16(c, kHandler, kNop);
    stage(c, 0x0cfc, 0);                    // CAS2.W D0:D1,D2:D3,(A0):(A1)
    p16(c, kCode + 2, 0x8080); p16(c, kCode + 4, 0x90c1);
    p16(c, kCode + 6, kNop);
    c.setIRC(0x8080);
    c.setA(0, kHole); c.setA(1, 0x30000);
    c.holeArmed = true;
    const bool contained = executeContained(c);
    const uint32_t sp = c.getSP();
    CHECK(contained && c.getPC0() == kHandler && g16(c, sp + 6) == 0xb008 &&
          g32(c, sp + 0x58) == 0x808090c1 && c.mem[sp + 0x37] == 0x12,
          "68030 CAS2 first-operand fault: format %04X, log %08X (want 808090C1), "
          "idx byte %02X (want 12)", g16(c, sp + 6), g32(c, sp + 0x58), c.mem[sp + 0x37]);
}

void moveFromSrStoreIsLastWrite()
{
    // [D] WinUAE gencpu.c i_MVSR2 @ Hatari e77819f7; addressing-mode cells
    // under the MMU of 2026-11-20 ("op=MOVE_from_SR.ind #3821", 68030;
    // "op=MOVE_from_SR.pi #3839", 68040). MOVE SR,<ea> and MOVE CCR,<ea>
    // end on their store as every MOVE does: (An)+ has stepped when it
    // faults, the 68030 stacks the short frame $A with the next PC. The
    // fork called writeM itself, without the last-write mark: format $B
    // on the 68030, An not stepped on either model.
    for (auto model : {moira::CPUModel::M68030, moira::CPUModel::M68040}) {
        const bool is040 = model == moira::CPUModel::M68040;
        for (const uint16_t opcode : { uint16_t(0x40da), uint16_t(0x42da) }) {   // MOVE SR|CCR,(A2)+
            TestCpu c(model);
            p32(c, 2 * 4, kHandler);
            p16(c, kHandler, kNop); p16(c, kHandler + 2, kNop);
            stage(c, opcode, 0);
            p16(c, kCode + 2, kNop); c.setIRC(kNop);
            c.setA(2, kHole);
            c.holeArmed = true; c.holeWritesOnly = true;
            const bool contained = executeContained(c);
            const uint32_t sp = c.getSP();
            const uint16_t format = g16(c, sp + 6);
            CHECK(contained && c.getPC0() == kHandler && c.holeTrips == 1 &&
                  c.getA(2) == kHole + 2 && format == (is040 ? 0x7008 : 0xa008) &&
                  (is040 || g32(c, sp + 2) == kCode + 2),
                  "%s MOVE %s,(A2)+ store fault: PC0=%08X A2=%08X (want %08X), "
                  "%u /BERR(s) (want 1), format %04X (want %04X), stacked PC %08X",
                  is040 ? "68040" : "68030", opcode == 0x40da ? "SR" : "CCR", c.getPC0(),
                  c.getA(2), kHole + 2, c.holeTrips, format, is040 ? 0x7008 : 0xa008,
                  g32(c, sp + 2));
        }
    }
}

void stackStoreFaults()
{
    // [D] WinUAE gencpu.c i_LINK, i_PEA, i_JSR, i_RTD, i_NEG, i_NEGX @ Hatari
    // e77819f7 — class cells with the user stack on faulting pages
    // (opcells.py --mmu identity --user --stack data, 2026-11-20). The fork
    // stacked format $B for LINK and PEA and left An and A7 as they entered,
    // left A7 decremented under a faulting JSR, logged no displacement for
    // RTD and BSR, stacked NEG's pending byte sign-extended and ASL's
    // without the bit shifted out; a 68040 BSR stacked the next PC.
    for (auto model : {moira::CPUModel::M68030, moira::CPUModel::M68040}) {
        const bool is040 = model == moira::CPUModel::M68040;
        const char* name = is040 ? "68040" : "68030";
        auto user = [](TestCpu& c, uint16_t opcode, uint16_t ext, uint32_t usp) {
            p32(c, 2 * 4, kHandler);
            p16(c, kHandler, kNop); p16(c, kHandler + 2, kNop);
            stage(c, opcode, 0);
            p16(c, kCode + 2, ext); c.setIRC(ext);
            c.setSR(0x0000);
            c.setUSP(usp); c.setSP(usp);
            c.holeArmed = true;
        };
        {   // LINK A6,#-8: the push is the last write
            TestCpu c(model);
            user(c, 0x4e56, 0xfff8, kHole + 4);
            c.setA(6, 0x12345678);
            c.holeWritesOnly = true;
            const bool contained = executeContained(c);
            const uint32_t sp = c.getSP();
            CHECK(contained && c.getPC0() == kHandler && c.holeTrips == 1 &&
                  g16(c, sp + 6) == (is040 ? 0x7008 : 0xa008) && g32(c, sp + 2) == kCode + 4 &&
                  c.getA(6) == kHole && c.getUSP() == kHole - 8 &&
                  (is040 || g32(c, sp + 0x18) == 0x12345678),
                  "%s LINK A6,#-8 push fault: format %04X, stacked PC %08X (want %08X), "
                  "A6=%08X (want %08X), USP=%08X (want %08X), %u /BERR(s)", name,
                  g16(c, sp + 6), g32(c, sp + 2), kCode + 4, c.getA(6), kHole, c.getUSP(),
                  kHole - 8, c.holeTrips);
        }
        {   // BSR.W: restarts with A7 intact; the 68030 has logged the displacement
            TestCpu c(model);
            user(c, 0x6100, 0x0040, kHole + 4);
            c.holeWritesOnly = true;
            const bool contained = executeContained(c);
            const uint32_t sp = c.getSP();
            CHECK(contained && c.getPC0() == kHandler && c.holeTrips == 1 &&
                  g16(c, sp + 6) == (is040 ? 0x7008 : 0xb008) && g32(c, sp + 2) == kCode &&
                  c.getUSP() == kHole + 4 &&
                  (is040 || (g32(c, sp + 0x58) == 0x0040 && c.mem[sp + 0x37] == 0x12)),
                  "%s BSR.W push fault: format %04X, stacked PC %08X (want %08X), "
                  "USP=%08X (want %08X), 68030 log %08X idx byte %02X (want 00000040, 12)",
                  name, g16(c, sp + 6), g32(c, sp + 2), kCode, c.getUSP(), kHole + 4,
                  g32(c, sp + 0x58), c.mem[sp + 0x37]);
        }
        {   // UNLK A6 with A6 on the hole: restarts, A7 intact
            TestCpu c(model);
            user(c, 0x4e5e, kNop, 0x00030000);
            c.setA(6, kHole);
            const bool contained = executeContained(c);
            const uint32_t sp = c.getSP();
            CHECK(contained && c.getPC0() == kHandler && c.holeTrips == 1 &&
                  g32(c, sp + 2) == kCode && c.getUSP() == 0x00030000 && c.getA(6) == kHole,
                  "%s UNLK A6 pop fault: stacked PC %08X (want %08X), USP=%08X (want "
                  "00030000), A6=%08X (want %08X)", name, g32(c, sp + 2), kCode,
                  c.getUSP(), c.getA(6), kHole);
        }
        if (is040) continue;
        {   // UNPK -(A3),-(A2): the source fixup is encoded as a long
            TestCpu c(model);
            p32(c, 2 * 4, kHandler);
            p16(c, kHandler, kNop);
            stage(c, 0x858b, 0);                    // UNPK -(A3),-(A2),#$0000
            p16(c, kCode + 2, 0x0000); c.setIRC(0x0000);
            c.setA(3, kHole + 1); c.setA(2, 0x30002);
            c.holeArmed = true;
            const bool contained = executeContained(c);
            const uint32_t sp = c.getSP();
            CHECK(contained && c.getPC0() == kHandler && g16(c, sp + 6) == 0xb008 &&
                  c.mem[sp + 0x36] == 0x73 && c.getA(3) == kHole - 3,
                  "68030 UNPK -(A3),-(A2) source fault: format %04X (want B008), fixup "
                  "byte %02X (want 73), A3=%08X in the handler (want %08X)",
                  g16(c, sp + 6), c.mem[sp + 0x36], c.getA(3), kHole - 3);
        }
        {   // ASL (A0): the pending word keeps the bit shifted out
            TestCpu c(model);
            p32(c, 2 * 4, kHandler);
            p16(c, kHandler, kNop);
            stage(c, 0xe1d0, 0);
            p16(c, kHole, 0xc001);
            c.setA(0, kHole);
            c.holeArmed = true; c.holeWritesOnly = true;
            const bool contained = executeContained(c);
            const uint32_t sp = c.getSP();
            CHECK(contained && c.getPC0() == kHandler && g16(c, sp + 6) == 0xa008 &&
                  g32(c, sp + 0x18) == 0x00018002,
                  "68030 ASL (A0) of $C001, store fault: format %04X (want A008), data "
                  "output buffer %08X (want 00018002)", g16(c, sp + 6), g32(c, sp + 0x18));
        }
        {   // PEA (A0): last write, A7 stepped
            TestCpu c(model);
            user(c, 0x4850, kNop, kHole + 4);
            c.setA(0, 0x00031234);
            c.holeWritesOnly = true;
            const bool contained = executeContained(c);
            const uint32_t sp = c.getSP();
            CHECK(contained && c.getPC0() == kHandler && g16(c, sp + 6) == 0xa008 &&
                  g32(c, sp + 2) == kCode + 2 && c.getUSP() == kHole &&
                  g32(c, sp + 0x18) == 0x00031234,
                  "68030 PEA (A0) push fault: format %04X (want A008), stacked PC %08X "
                  "(want %08X), USP=%08X (want %08X), pending %08X", g16(c, sp + 6),
                  g32(c, sp + 2), kCode + 2, c.getUSP(), kHole, g32(c, sp + 0x18));
        }
        {   // JSR (A0): restarts, A7 intact in the handler
            TestCpu c(model);
            user(c, 0x4e90, kNop, kHole + 4);
            c.setA(0, 0x3000);
            c.holeWritesOnly = true;
            const bool contained = executeContained(c);
            const uint32_t sp = c.getSP();
            CHECK(contained && c.getPC0() == kHandler && g16(c, sp + 6) == 0xb008 &&
                  g32(c, sp + 2) == kCode && c.getUSP() == kHole + 4,
                  "68030 JSR (A0) push fault: format %04X (want B008), stacked PC %08X "
                  "(want %08X), USP=%08X (want %08X)", g16(c, sp + 6), g32(c, sp + 2),
                  kCode, c.getUSP(), kHole + 4);
        }
        {   // RTD #8: the displacement is logged before the pop
            TestCpu c(model);
            user(c, 0x4e74, 0x0008, kHole);
            const bool contained = executeContained(c);
            const uint32_t sp = c.getSP();
            CHECK(contained && c.getPC0() == kHandler && g16(c, sp + 6) == 0xb008 &&
                  g32(c, sp + 0x58) == 0x0008 && c.mem[sp + 0x37] == 0x12 &&
                  c.getUSP() == kHole,
                  "68030 RTD #8 pop fault: format %04X (want B008), log %08X (want "
                  "00000008), idx byte %02X (want 12), USP=%08X", g16(c, sp + 6),
                  g32(c, sp + 0x58), c.mem[sp + 0x37], c.getUSP());
        }
        struct Ext { uint16_t opcode; uint8_t dst, src, ccr; uint32_t want; const char* what; };
        for (const Ext& k : { Ext{ 0x9109, 0xa1, 0x7d, 0x10, 0xffffff23, "SUBX.B -(A1),-(A0): $A1 - $7D - X" },
                              Ext{ 0xd109, 0x7f, 0x01, 0x00, 0x00000080, "ADDX.B -(A1),-(A0): $7F + $01" } }) {
            TestCpu c(model);
            p32(c, 2 * 4, kHandler);
            p16(c, kHandler, kNop);
            stage(c, k.opcode, k.ccr);
            c.mem[kHole] = k.dst; c.mem[0x30000] = k.src;
            c.setA(0, kHole + 1); c.setA(1, 0x30001);
            c.holeArmed = true; c.holeWritesOnly = true;
            const bool contained = executeContained(c);
            const uint32_t sp = c.getSP();
            CHECK(contained && c.getPC0() == kHandler && g16(c, sp + 6) == 0xa008 &&
                  g32(c, sp + 0x18) == k.want,
                  "68030 %s, store fault: format %04X (want A008), data output buffer "
                  "%08X (want %08X)", k.what, g16(c, sp + 6), g32(c, sp + 0x18), k.want);
        }
        struct Neg { uint16_t opcode; uint8_t operand; uint32_t want; const char* what; };
        for (const Neg& k : { Neg{ 0x4410, 0x81, 0xffffff7f, "NEG.B (A0) of $81" },
                              Neg{ 0x4010, 0x80, 0x00000080, "NEGX.B (A0) of $80, X clear" } }) {
            TestCpu c(model);
            p32(c, 2 * 4, kHandler);
            p16(c, kHandler, kNop);
            stage(c, k.opcode, 0);
            c.mem[kHole] = k.operand;
            c.setA(0, kHole);
            c.holeArmed = true; c.holeWritesOnly = true;
            const bool contained = executeContained(c);
            const uint32_t sp = c.getSP();
            CHECK(contained && c.getPC0() == kHandler && g16(c, sp + 6) == 0xa008 &&
                  g32(c, sp + 0x18) == k.want,
                  "68030 %s, store fault: format %04X (want A008), data output buffer "
                  "%08X (want %08X)", k.what, g16(c, sp + 6), g32(c, sp + 0x18), k.want);
        }
    }
}

void stackFaultRoundTrip()
{
    // What the frames of stackStoreFaults are for: a handler that lets the
    // access through and returns leaves the state of a run that never
    // faulted. The 68040 LINK is the exception by design — its push is a
    // pending write-back the handler must perform itself
    // ([B] MC68040UM-1993 §8.4.6), so its memory cell is not compared.
    struct Case { uint16_t opcode, ext; uint32_t usp, a6; bool readFault, storeIsWriteBack;
                  const char* what; };
    static const Case cases[] = {
        { 0x4e56, 0xfff8, kHole + 4, 0x12345678, false, true,  "LINK A6,#-8" },
        { 0x4850, kNop,   kHole + 4, 0x12345678, false, false, "PEA (A0)" },
        { 0x4e90, kNop,   kHole + 4, 0x12345678, false, false, "JSR (A0)" },
        { 0x6100, 0x0040, kHole + 4, 0x12345678, false, false, "BSR.W" },
        { 0x4e5e, kNop,   0x00030000, kHole,     true,  false, "UNLK A6" },
        { 0x4e74, 0x0008, kHole,     0x12345678, true,  false, "RTD #8" },
        { 0x4e77, kNop,   kHole - 2, 0x12345678, true,  false, "RTR, the return address faulting" },
    };
    for (auto model : {moira::CPUModel::M68030, moira::CPUModel::M68040}) {
        const bool is040 = model == moira::CPUModel::M68040;
        for (const Case& k : cases) {
            uint32_t pc[2] {}, usp[2] {}, a6[2] {}, cell[2] {};
            unsigned trips = 0;
            for (int faulting = 0; faulting < 2; faulting++) {
                TestCpu c(model);
                p32(c, 2 * 4, kHandler);
                p16(c, kHandler, 0x4e73);           // RTE
                p16(c, 0x3000, kNop); p16(c, 0x3002, kNop);
                p16(c, kCode + 0x42, kNop); p16(c, kCode + 0x44, kNop);
                p32(c, kHole, 0x00003000);          // what UNLK and RTD pop
                stage(c, k.opcode, 0);
                p16(c, kCode + 2, k.ext); c.setIRC(k.ext);
                c.setA(0, 0x3000); c.setA(6, k.a6);
                c.setSR(0x0000);
                c.setUSP(k.usp); c.setSP(k.usp);
                c.holeArmed = faulting != 0; c.holeWritesOnly = !k.readFault;
                bool contained = executeContained(c);
                if (faulting) {
                    trips = c.holeTrips;
                    c.holeArmed = false;
                    for (int i = 0; i < 3 && contained && (c.getSR() & 0x2000); i++)
                        contained = executeContained(c);
                    // A restarted instruction is back at its own address.
                    if (contained && c.getPC0() == kCode) contained = executeContained(c);
                }
                pc[faulting] = contained ? c.getPC0() : 0xffffffff;
                usp[faulting] = c.getUSP(); a6[faulting] = c.getA(6);
                cell[faulting] = g32(c, kHole);
            }
            const bool cellOk = cell[0] == cell[1] || (is040 && k.storeIsWriteBack);
            CHECK(trips == 1 && pc[0] == pc[1] && usp[0] == usp[1] && a6[0] == a6[1] && cellOk,
                  "%s %s through a fault and an RTE: %u /BERR(s) (want 1), PC0 %08X (want "
                  "%08X), USP %08X (want %08X), A6 %08X (want %08X), cell %08X (want %08X)",
                  is040 ? "68040" : "68030", k.what, trips, pc[1], pc[0], usp[1], usp[0],
                  a6[1], a6[0], cell[1], cell[0]);
        }
    }
}

void refillPrefetchFaultIsDeferred()
{
    // [B] MC68030UM-1989 §8.1.2 — a bus error on a prefetch waits until the
    //     word is needed.
    // [D] WinUAE @ Hatari e77819f7 — no access after an SR or CCR write:
    //     directed vectors of 2026-11-20, the instruction in the last four
    //     bytes of a page and the next page invalid (40 of 40 per form
    //     differed). The fork read the second refill word for real and
    //     took the bus error inside the instruction.
    struct Case { uint16_t opcode; int words; uint16_t ext; const char* what; };
    static const Case cases[] = {
        { 0x46c0, 1, 0,      "MOVE D0,SR" },   { 0x44c0, 1, 0,      "MOVE D0,CCR" },
        { 0x027c, 2, 0xffff, "ANDI #$FFFF,SR" }, { 0x007c, 2, 0x0000, "ORI #0,SR" },
        { 0x0a7c, 2, 0x0000, "EORI #0,SR" },
    };
    for (auto model : {moira::CPUModel::M68030, moira::CPUModel::M68040}) {
        for (const Case& k : cases) {
            TestCpu c(model);
            p32(c, 2 * 4, kHandler);
            p16(c, kHandler, kNop);
            stage(c, k.opcode, 0);
            if (k.words == 2) { p16(c, kCode + 2, k.ext); c.setIRC(k.ext); }
            c.setD(0, 0x2715);
            const uint32_t next = kCode + 2 * uint32_t(k.words);
            c.holeArmed = true; c.holeAt = next + 2;    // the word after the next opcode
            const bool contained = executeContained(c);
            CHECK(contained && c.getPC0() == next && c.faults == 0,
                  "%s %s with the second refill word on a /BERR hole: PC0=%08X (want %08X), "
                  "%d fault(s) taken (want 0)", model == moira::CPUModel::M68040 ? "68040" : "68030",
                  k.what, c.getPC0(), next, c.faults);
        }
    }
}

int main(int argc, char** argv)
{
#if POM68K_CONTINUATION_040
    if (argc == 2 && std::string_view(argv[1]) == "--movem-restart") {
        movemRestart040();
        std::printf("[moira_rules_test] MOVEM: %d failure(s)\n", failures);
        return failures ? 1 : 0;
    }
    movemRestart040();
#else
    (void)argc; (void)argv;
#endif
    divsOfIntMin();
    externalBusError000();
    hostHooks();
    group2InstructionAddress();
    divlOperandFault();
    chk2FlagsOn040();
    fpuUnimplementedBeforeDatatype040();
    flineShapesOn68LC040();
    fbccPredicateIsFiveBits();
    fpuStoreFaultRestarts040();
    movepFaultRestarts040();
    hostBusErrorFaultAddressIsLogical040();
    tableSearchBusError040();
    misalignedAccessAcrossPages();
    misalignedAccessUnchangedPaths();
    ordinaryStoreStillAdvances040();
    ttrTableFollowsRegisters();
    clrReadsOnlyOn68000();
    bitfieldMemoryAccesses();
    ptestFlagsInvalidAtTheLimit();
    ptestLongDescriptorLimits();
    ptestEarlyAndIndirect030();
    ptestDescriptorFaults030();
    directRootFclLimits030();
    directRootWrites030();
    fpuIgnoredOn68000And68010();
    tableSearchBusError030();
    movesFixupSlot030();
    movemExtensionLog030();
    jmpOddTargetPc030();
    fsccWriteBuffer030();
    aluWriteBuffer030();
    moveAbsLongLastWrite030();
    ec030UsesPlain020Path();
    ec040AccessControlUnit();
    pcRelativeOperandSpace();
    pcRelativeOperandCycle();
    cacrStrobesReadZero();
    fbccOddTarget040();
    jsrTargetFetchFault();
    deferredPrefetchFault();
    fullEaLongDisplacementLog030();
#if MOIRA_ENABLE_JIT
    atcReplayCopyOnWrite040();
#endif
    stop68000();
    reset68000();
    unresetFpZero();
    irqDelayAfterMaskLowering();
    level7TransitionSensitive();
    interruptSequenceBilled();
    masterStackInterrupt();
    movesReleasesAlternateFc();
    movesFcAfterFault010();
    divslOverflowKeepsRegisters();
    watchpointsUnderMmu();
    externalBusError020();
    oddStackInterruptFrame();
    prefetchPipeAndIplPoll030();
    cas2IsReadModifyWrite030();
    lockEndsWithTheInstruction040();
    indirectPointerNotLocked040();
    bkptMakesNoDataAccess();
    bitfieldStoreRestarts040();
    srWriteRefillIsAFetch();
    rteRerunTakesNoTrace040();
    traceFollowsTheTracedInstruction();
    traceOnChangeOfFlow();
    mullSameRegister();
    cas2SameCompareRegister();
    packUnpkFaultRestoresAn();
    moveFromSrStoreIsLastWrite();
    cas2ExtensionLog030();
    stackStoreFaults();
    stackFaultRoundTrip();
    refillPrefetchFaultIsDeferred();

    if (failures) {
        std::printf("[moira_rules_test] FAIL: %d check(s)\n", failures);
        return 1;
    }
    std::printf("[moira_rules_test] OK: DIVS.W of $80000000 on six models, "
                "68000 external /BERR, host hooks, DIVx.L operand fault, "
                "68040 CHK2/CMP2 flags, 68040 FPU trap priority, 68LC040 F-line "
                "shapes, five-bit FBcc predicate, 68040 FPU store fault, 68040 logical fault "
                "address of a /BERR, 68040 TTR "
                "table, CLR without its read from the 68020 up, bit field memory accesses, PTEST at its level "
                "limit, Line-F on a 68000/68010 with a 6888x attached, 68030 /BERR "
                "during a table search, 68030 MOVES fixup slot, 68030 MOVEM extension log, 68030 JMP odd-target PC, 68030 FScc write buffer, 68030 ADD/SUB write buffer, 68030 MOVE to (xxx).L last write, 68EC030 plain-020 path with a 68030 "
                "MMU/cache control, the 68EC040 access control unit, the PC-relative operand space and cycle, the 68040 FBcc to an odd target, CACR strobes read as zero, JSR target fetch fault on the 68030/68040, deferred prefetch faults and the 68040 page-end exception rule, 68030 long-displacement access log, 68040 ATC replay copy-on-write, 68000 STOP without a bus cycle, 68000 reset timing, FP registers before the first reset, IRQ recognition after an SR write, faulting MOVES function code, 68010 MOVES function code after an address error or /BERR, DIVS.L overflow keeping its registers, watchpoints under the MMU, 68020 external /BERR, interrupt frame on an odd SSP, 68030 pipe across PMOVE to TC, IPL sampled in a DBRA loop, 68030 CAS2 as a read-modify-write, the 68040 lock ending with TAS/CAS/CAS2, a memory-indirect pointer read outside the lock, BKPT without a data access, 68040 bit-field and JSR store faults restart, the SR-write refill as a fetch, no trace before a format-$7 rerun, PACK/UNPK -(An) faults on the 68030/68040, level 7 on a transition from the 68020 up, the billed 020-core interrupt sequence, the throwaway frame of an interrupt taken with M set, the trace exception after the traced instruction, trace on change of flow, MULx.L into one register, CAS2 with one compare register, MOVE from SR/CCR ending on its store, the 68030 CAS2 extension long, LINK/UNLK/PEA/JSR/BSR/RTD stack faults, the UNPK source fixup and the pending store of NEG, NEGX, ADDX, SUBX and ASL, the deferred refill fault after an SR or CCR write\n");
    return 0;
}
