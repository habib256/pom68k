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

#include "Moira.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr uint32_t kHole = 0x080000;   // a byte the test board answers with /BERR

class TestCpu : public moira::Moira {
public:
    explicit TestCpu(moira::Model model) : mem(1 << 20, 0), model_(model) {
        setModel(model);
    }
    std::vector<uint8_t> mem;

    // Cell 2: the board asserts /BERR on kHole.
    bool holeArmed = false;
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

private:
    void traceAccess(char kind, moira::u32 a) const {
        if (!traceBus || a < busBase || a >= busBase + 16) return;
        if (!bus.empty()) bus += ' ';
        bus += kind;
        bus += std::to_string(a - busBase);
    }

    moira::Model model_;

    // The board's /BERR, through the entry point of the model's core.
    void berr(moira::u32 a, bool write) const {
        ++holeTrips;
        auto& self = *const_cast<TestCpu*>(this);
        if (model_ >= moira::Model::M68EC040) self.extBusError040();
        else if (model_ >= moira::Model::M68EC020) self.extBusError();
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
        traceAccess('b', a);
        if (hole(a)) berr(a, false);
        events += 'r';
        return mem[a & 0xfffff];
    }
    moira::u16 read16(moira::u32 a) const override {
        observeMovem(a, false);
        traceAccess('w', a);
        if (hole(a)) berr(a, false);
        return moira::u16((mem[a & 0xfffff] << 8) | mem[(a + 1) & 0xfffff]);
    }
    void write8(moira::u32 a, moira::u8 v) const override {
        traceAccess('B', a);
        if (hole(a)) berr(a, true);
        events += 'w';
        const_cast<TestCpu*>(this)->mem[a & 0xfffff] = v;
    }
    void write16(moira::u32 a, moira::u16 v) const override {
        observeMovem(a, true);
        traceAccess('W', a);
        if (hole(a)) berr(a, true);
        auto& m = const_cast<TestCpu*>(this)->mem;
        m[a & 0xfffff] = moira::u8(v >> 8); m[(a + 1) & 0xfffff] = moira::u8(v);
    }
    bool illegalOpcodeHook(moira::u16) override { return handleIllegal; }
public:
    // Cell "bus error during a table search": the 68030 PMMU registers,
    // set directly (after stage(), whose reset() clears them).
    void mmu030(moira::u32 tc, moira::u64 crp, moira::u32 tt0, moira::u32 tt1) {
        reg.tc = tc; reg.crp = crp; reg.tt0 = tt0; reg.tt1 = tt1;
    }
    moira::u16 mmusr() const { return moira::u16(reg.mmusr); }
    void armIcacheOverlay() {
        pomIcache.armed = true;
        pomIcache.missPenalty = 17;
        setCACR(1);
    }
    moira::i64 overlayFetches() const { return pomIcache.fetches; }
private:
    void setBusLock(bool locked) override { events += locked ? 'L' : 'U'; }
    void willProcessFault(const moira::StackFrame& frame) override {
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
    moira::Model model;
    const char* name;
    uint8_t ccrFromAllSet;      // CCR after the divide when it entered as $1F
    long cycles;                // -1: not pinned (020+ cycles are advisory)
};

void divsOfIntMin()
{
    // X N Z V C. Entering with $1F shows what is preserved; entering with
    // $00 shows what is set.
    static const ModelRow rows[] = {
        { moira::Model::M68000,   "68000",   0x1e, 18 },
        { moira::Model::M68010,   "68010",   0x1e, -1 },
        { moira::Model::M68020,   "68020",   0x12, -1 },
        { moira::Model::M68030,   "68030",   0x12, -1 },
        { moira::Model::M68LC040, "68LC040", 0x1e, -1 },
        { moira::Model::M68040,   "68040",   0x1e, -1 },
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
        TestCpu c(moira::Model::M68000);
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
        TestCpu c(moira::Model::M68000);
        p32(c, 4 * 4, kHandler);
        p16(c, kHandler, kNop);
        stage(c, kIllegal, 0);
        c.execute();
        CHECK(c.getPC0() == kHandler, "unhandled ILLEGAL: PC0=%08X", c.getPC0());
    }
    {   // handled: consumed like a NOP
        TestCpu c(moira::Model::M68000);
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
        TestCpu c(moira::Model::M68000);
        stage(c, 0x4ad0, 0);
        c.setA(0, 0x3000);
        c.events.clear();
        c.execute();
        CHECK(c.events == "LrwU", "TAS (A0) bus-lock bracket: \"%s\", want LrwU",
              c.events.c_str());
        CHECK(c.mem[0x3000] == 0x80, "TAS (A0) wrote %02X", c.mem[0x3000]);
    }
    {   // TAS on the faulting byte still releases the lock
        TestCpu c(moira::Model::M68000);
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
        TestCpu c(moira::Model::M68000);
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
    static const struct { moira::Model model; const char* name; } models[] = {
        { moira::Model::M68020,   "68020" },
        { moira::Model::M68030,   "68030" },
        { moira::Model::M68LC040, "68LC040" },
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
    struct Row { moira::Model model; const char* name; uint8_t inRange, onBound; };
    static const Row rows[] = {
        { moira::Model::M68020,   "68020",   0x10, 0x14 },
        { moira::Model::M68030,   "68030",   0x10, 0x14 },
        { moira::Model::M68LC040, "68LC040", 0x1a, 0x1e },
        { moira::Model::M68040,   "68040",   0x1a, 0x1e },
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
        TestCpu c(moira::Model::M68040);
        c.setFPUModel(moira::FPUModel::M68040);
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
        TestCpu c(moira::Model::M68LC040);
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
    static const struct { moira::Model cpu; moira::FPUModel fpu; const char* name; } rigs[] = {
        { moira::Model::M68030, moira::FPUModel::M68882, "68030+68882" },
        { moira::Model::M68040, moira::FPUModel::M68040, "68040" },
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
    TestCpu c(moira::Model::M68040);
    c.setFPUModel(moira::FPUModel::M68040);
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
    for (auto model : {moira::Model::M68LC040, moira::Model::M68040})
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
            c.setPomCache040(path == 2);
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
                if (path == 1 && fault != 0)
                    CHECK(c.pomJitDataW1.host != nullptr,
                          "MOVEP prefix did not exercise the data window");
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
void movemRestart040()
{
    unsigned sequences = 0;
    for (auto model : {moira::Model::M68EC040, moira::Model::M68LC040, moira::Model::M68040})
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
        if ((mmu && (model == moira::Model::M68EC040 || pd)) ||
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
        c.setPomCache040(cache); c.setCACR(cache ? 0x80008000 : 0);
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

void ordinaryStoreStillAdvances040()
{
    // [D] WinUAE cpuemu_31.c @ Hatari e77819f7: ordinary MOVE stores
    // call gen_set_fault_pc. The MOVEP exception must not capture them.
    unsigned sequences = 0;
    for (auto model : {moira::Model::M68LC040, moira::Model::M68040})
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
        c.setPomCache040(cache); c.setCACR(cache ? 0x80008000 : 0);
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
    TestCpu c(moira::Model::M68040);
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
    static const struct { moira::Model model; const char* name; const char* want; } rows[] = {
        { moira::Model::M68000,   "68000",   "rw" },
        { moira::Model::M68020,   "68020",   "w"  },
        { moira::Model::M68030,   "68030",   "w"  },
        { moira::Model::M68LC040, "68LC040", "w"  },
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
    static const struct { moira::Model model; const char* name; } models[] = {
        { moira::Model::M68020, "68020" }, { moira::Model::M68030, "68030" },
        { moira::Model::M68LC040, "68LC040" },
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
    TestCpu c(moira::Model::M68030);
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

void ec030UsesPlain020Path()
{
    // POM68K_SURFACE.md § 1.2: the current 68EC030 implementation uses
    // the plain-020 bus/prefetch path and refuses the code window. Keep
    // that contract executable without a Macintosh. A programmed TC/CRP
    // maps code/stack but makes the operand's page invalid: the 68030
    // control must fault, whereas the two plain-020 models read RAM.
    // This pins this fork's declared scope; it is not a silicon corpus.
    for (moira::Model model : { moira::Model::M68020, moira::Model::M68EC030,
                                moira::Model::M68030 }) {
        TestCpu c(model);
        const auto checkWindowRefused = [&] {
            if (model != moira::Model::M68EC030) return;
            for (bool super : { false, true }) {
                moira::u32 pa = 0, base = 0, len = 0;
                CHECK(!c.pomJitProbeCode(kCode, super, pa, base, len),
                      "68EC030: code-window probe accepted super=%d TC=%08X",
                      int(super), c.getTC());
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
        c.armIcacheOverlay();
        CHECK((c.getCACR() & 1) != 0,
              "model %d: cache-enable precondition was masked off", int(model));
        const bool mmu = model == moira::Model::M68030;
        CHECK(c.hasMMU() == mmu, "model %d: hasMMU()", int(model));
        const bool contained = executeContained(c);
        CHECK(contained, "model %d: execute() leaked a fault", int(model));
        if (!contained) continue;
        CHECK(c.getD(0) == (mmu ? 0xaabbccdd : 0x12345678) &&
              c.getPC0() == (mmu ? kHandler : kCode + 2),
              "model %d: D0=%08X PC0=%08X (want %08X/%08X)", int(model),
              c.getD(0), c.getPC0(), mmu ? 0xaabbccdd : 0x12345678,
              mmu ? kHandler : kCode + 2);
        CHECK((c.overlayFetches() > 0) == mmu,
              "model %d: overlay counted %lld fetches", int(model),
              static_cast<long long>(c.overlayFetches()));
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
    struct Row { moira::Model model; const char* name; int frameBytes; };
    static const Row rows[] = {
        { moira::Model::M68000, "68000", 6 },
        { moira::Model::M68010, "68010", 8 },
    };
    for (const Row& r : rows) {
        for (moira::FPUModel fpu : { moira::FPUModel::NONE, moira::FPUModel::M68881,
                                     moira::FPUModel::M68882 }) {
            TestCpu c(r.model);
            c.setFPUModel(fpu);
            p32(c, 11 * 4, kHandler);
            p16(c, kHandler, kNop); p16(c, kHandler + 2, kNop);
            stage(c, 0xf200, 0);                // FMOVE FP0,FP0 on a 6888x
            p16(c, kCode + 2, 0x0000);
            c.setIRC(0x0000);
            const uint32_t sp = c.getSP();
            c.execute();
            const bool fmtOk = r.model == moira::Model::M68000 ||
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
    TestCpu c(moira::Model::M68020);
    c.setFPUModel(moira::FPUModel::M68881);
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
        TestCpu c(moira::Model::M68030);
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
        TestCpu c(moira::Model::M68030);
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

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--movem-restart") {
        movemRestart040();
        std::printf("[moira_rules_test] MOVEM: %d failure(s)\n", failures);
        return failures ? 1 : 0;
    }
    movemRestart040();
    divsOfIntMin();
    externalBusError000();
    hostHooks();
    divlOperandFault();
    chk2FlagsOn040();
    fpuUnimplementedBeforeDatatype040();
    flineShapesOn68LC040();
    fbccPredicateIsFiveBits();
    fpuStoreFaultRestarts040();
    movepFaultRestarts040();
    ordinaryStoreStillAdvances040();
    ttrTableFollowsRegisters();
    clrReadsOnlyOn68000();
    bitfieldMemoryAccesses();
    ptestFlagsInvalidAtTheLimit();
    fpuIgnoredOn68000And68010();
    tableSearchBusError030();
    ec030UsesPlain020Path();

    if (failures) {
        std::printf("[moira_rules_test] FAIL: %d check(s)\n", failures);
        return 1;
    }
    std::printf("[moira_rules_test] OK: DIVS.W of $80000000 on six models, "
                "68000 external /BERR, host hooks, DIVx.L operand fault, "
                "68040 CHK2/CMP2 flags, 68040 FPU trap priority, 68LC040 F-line "
                "shapes, five-bit FBcc predicate, 68040 FPU store fault, 68040 TTR "
                "table, CLR without its read from the 68020 up, bit field memory accesses, PTEST at its level "
                "limit, Line-F on a 68000/68010 with a 6888x attached, 68030 /BERR "
                "during a table search, 68EC030 plain-020 path with a 68030 "
                "MMU/cache control\n");
    return 0;
}
