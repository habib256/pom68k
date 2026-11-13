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
#include <cstring>
#include <new>
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
        if (hole(a) && !holeWritesOnly) berr(a, false);
        events += 'r';
        return mem[a & 0xfffff];
    }
    moira::u16 read16(moira::u32 a) const override {
        observeMovem(a, false);
        traceAccess('w', a);
        if (hole(a) && !holeWritesOnly) berr(a, false);
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
    void pomDidReachWatchpoint(moira::u32 a, moira::Size S, bool write, bool) override {
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
void movesFixupSlot030()
{
    // MOVES.L (A4)+,D0 whose read takes /BERR on a 68030: the frame's
    // pending (A4)+ fixup is wb3 status ($34: A4, long, post-increment,
    // valid = $54), wb2 ($36 high byte) stays zero. [D] WinUAE gencpu i_MOVES
    // @ Hatari e77819f7 allocates the memory-to-register fixup as its second
    // slot; the manual leaves these internal words undefined (POM68K_VENDOR.md
    // row 80). Found by the fresh sweep "core/identity seed=9117 #575".
    TestCpu c(moira::Model::M68030);
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
        TestCpu c(moira::Model::M68030);
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
        TestCpu c(moira::Model::M68030);
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
    for (auto model : {moira::Model::M68030, moira::Model::M68LC040, moira::Model::M68040})
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
        TestCpu c(moira::Model::M68000);
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
        TestCpu c(moira::Model::M68000);
        stop(c, 0x2000);
        c.setIPL(3);
        const moira::i64 c0 = c.getClock();
        c.execute();                                    // STOP
        const moira::i64 afterStop = c.getClock() - c0;
        for (int i = 0; i < 16 && c.isStopped(); i++) c.execute();   // held, then taken
        TestCpu ref(moira::Model::M68000);              // the same exception, no STOP
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
        TestCpu c(moira::Model::M68020);
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
    TestCpu m000(moira::Model::M68000), m010(moira::Model::M68010);
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
        TestCpu c(moira::Model::M68040);
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
    TestCpu c(moira::Model::M68030);
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
    TestCpu c(moira::Model::M68030);
    c.setFPUModel(moira::FPUModel::M68882);
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
        TestCpu c(moira::Model::M68030);
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
    static const struct { moira::Model model; const char* name; uint32_t want; } rows[] = {
        { moira::Model::M68020,   "68020",   0x00000003 },
        { moira::Model::M68EC030, "68EC030", 0x00003313 },
        { moira::Model::M68030,   "68030",   0x00003313 },
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

// POM68K (Moira.h, fpu.fp): a core that has not been reset yet is serialized
// too — the Portable's is held by its Power Manager — so its FP registers
// must not be whatever the allocator left. Built over scribbled storage.
void unresetFpZero()
{
    alignas(TestCpu) static unsigned char raw[sizeof(TestCpu)];
    std::memset(raw, 0xA5, sizeof raw);
    TestCpu* c = new (raw) TestCpu(moira::Model::M68000);
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
    const auto landing = [&](moira::Model m, Form form, bool& taken) {
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
    struct Row { moira::Model model; const char* name; uint32_t delayed; };
    for (const Row& row : { Row{ moira::Model::M68000, "68000", 0 },
                            Row{ moira::Model::M68010, "68010", 0 },
                            Row{ moira::Model::M68020, "68020", 4 },
                            Row{ moira::Model::M68030, "68030", 4 },
                            Row{ moira::Model::M68040, "68040", 4 } }) {
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

void movesReleasesAlternateFc()
{
    // POM68K_VENDOR.md row 37: MOVES puts SFC or DFC on the function-code
    // pins for its one access; if that access takes a bus error, the pins
    // must be back on the supervisor's own code before the handler's first
    // fetch, or the handler runs in the faulting access's address space.
    // `v8_ramsize` holds it on a V8 board for the 68020 and the 68030, in
    // the read direction; this holds both directions on the 68040 too.
    for (moira::Model m : { moira::Model::M68020, moira::Model::M68030,
                            moira::Model::M68040 }) {
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

void watchpointsUnderMmu()
{
    // POM68K_VENDOR.md row 20: on the 68030 and 68040 data accesses go
    // through mmuRead/mmuWrite and mmu040Read/mmu040Write, which bypass
    // the readM/writeM that host upstream's watchpoint check; each of the
    // four carries its own, on the LOGICAL address. The debugger is their
    // only consumer and `debug_session_test` runs a 68000, so none of the
    // four had a gate.
    constexpr uint32_t kData = 0x40000;
    for (moira::Model m : { moira::Model::M68030, moira::Model::M68040 }) {
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
            TestCpu c(moira::Model::M68030);
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
    for (moira::Model m : { moira::Model::M68020, moira::Model::M68EC020 }) {
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
    for (moira::Model m : { moira::Model::M68020, moira::Model::M68030,
                            moira::Model::M68040 }) {
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
        TestCpu c(moira::Model::M68030);
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
    for (moira::Model m : { moira::Model::M68020, moira::Model::M68030,
                            moira::Model::M68040 }) {
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
    movesFixupSlot030();
    movemExtensionLog030();
    jmpOddTargetPc030();
    fsccWriteBuffer030();
    aluWriteBuffer030();
    ec030UsesPlain020Path();
    cacrStrobesReadZero();
    jsrTargetFetchFault();
    fullEaLongDisplacementLog030();
#if MOIRA_ENABLE_JIT
    atcReplayCopyOnWrite040();
#endif
    stop68000();
    reset68000();
    unresetFpZero();
    irqDelayAfterMaskLowering();
    movesReleasesAlternateFc();
    watchpointsUnderMmu();
    externalBusError020();
    oddStackInterruptFrame();
    prefetchPipeAndIplPoll030();

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
                "during a table search, 68030 MOVES fixup slot, 68030 MOVEM extension log, 68030 JMP odd-target PC, 68030 FScc write buffer, 68030 ADD/SUB write buffer, 68EC030 plain-020 path with a 68030 "
                "MMU/cache control, CACR strobes read as zero, JSR target fetch fault on the 68030/68040, 68030 long-displacement access log, 68040 ATC replay copy-on-write, 68000 STOP without a bus cycle, 68000 reset timing, FP registers before the first reset, IRQ recognition after an SR write, faulting MOVES function code, watchpoints under the MMU, 68020 external /BERR, interrupt frame on an odd SSP, 68030 pipe across PMOVE to TC, IPL sampled in a DBRA loop\n");
    return 0;
}
