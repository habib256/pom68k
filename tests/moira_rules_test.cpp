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
// 3. The three host hooks back-ported from NeoST (row 42): a handled
//    illegal opcode is consumed like a NOP, an unhandled one still takes
//    vector 4; TAS brackets its read and its write with setBusLock;
//    willProcessFault sees the frame of an address error. All three are
//    no-ops by default, which the corpora prove; this proves the hooks fire.

#include "Moira.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kHole = 0x080000;   // a byte the test board answers with /BERR

class TestCpu : public moira::Moira {
public:
    explicit TestCpu(moira::Model model) : mem(1 << 20, 0) { setModel(model); }
    std::vector<uint8_t> mem;

    // Cell 2: the board asserts /BERR on kHole.
    bool holeArmed = false;
    // Cell 3: what the hooks saw. 'L'/'U' lock and unlock, 'r'/'w' bus cycles.
    bool handleIllegal = false;
    mutable std::string events;
    mutable int faults = 0;
    mutable moira::u32 faultAddr = 0;

private:
    moira::u8 read8(moira::u32 a) const override {
        if (holeArmed && (a & 0xffffff) == kHole)
            const_cast<TestCpu*>(this)->extBusError000(a, false);
        events += 'r';
        return mem[a & 0xfffff];
    }
    moira::u16 read16(moira::u32 a) const override {
        return moira::u16((mem[a & 0xfffff] << 8) | mem[(a + 1) & 0xfffff]);
    }
    void write8(moira::u32 a, moira::u8 v) const override {
        if (holeArmed && (a & 0xffffff) == kHole)
            const_cast<TestCpu*>(this)->extBusError000(a, true);
        events += 'w';
        const_cast<TestCpu*>(this)->mem[a & 0xfffff] = v;
    }
    void write16(moira::u32 a, moira::u16 v) const override {
        auto& m = const_cast<TestCpu*>(this)->mem;
        m[a & 0xfffff] = moira::u8(v >> 8); m[(a + 1) & 0xfffff] = moira::u8(v);
    }
    bool illegalOpcodeHook(moira::u16) override { return handleIllegal; }
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

} // namespace

int main()
{
    divsOfIntMin();
    externalBusError000();
    hostHooks();

    if (failures) {
        std::printf("[moira_rules_test] FAIL: %d check(s)\n", failures);
        return 1;
    }
    std::printf("[moira_rules_test] OK: DIVS.W of $80000000 on six models, "
                "68000 external /BERR, host hooks\n");
    return 0;
}
