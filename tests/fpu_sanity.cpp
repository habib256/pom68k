// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — MIT (see LICENSE-MIT)
//
// O5 gate `fpu_sanity` — hand-computed 68882 smoke cases against Moira
// (Model::M68030 + FPUModel::M68882 on a flat 16 MB bus). This is NOT the
// differential gate (tests/sst68030 replays the oracle corpus); it pins a
// handful of values computed by hand from the MC68881/882UM so a gross
// regression in the softfloat wiring is caught without a corpus:
//
//   1. detached FPU  : F-line (vector 11) — legacy behaviour preserved
//   2. FMOVECR pi    : exact ROM pattern + INEX2/AE_INEX
//   3. 2 + 2         : FMOVE.L #imm + FADD.L #imm = 4.0 exactly
//   4. 1 / 0         : +inf, DZ + AE_DZ, cc I
//   5. sqrt(-1)      : NaN, OPERR + AE_IOP, cc NAN
//   6. FCMP + FSLT   : 1 vs 2 -> N; FSLT sets Dn.b = $FF
//   7. FMOVEM.X      : raw 96-bit image of FP0 lands in memory
//  10+ FRESTORE      : frame acceptance (NULL / 68882 IDLE $38 / 68882
//                      BUSY $D4 skip / garbage -> vector 14), WinUAE
//                      fpuop_restore-mirrored (fpp.c:2755-2807)
//  14  FMOVE FPn,<ea> : an enabled exception is a mid-instruction one
//                      (format $9), taken after the store; RTE resumes at
//                      the scanPC (MC68881UM §6.1, §7.4.2.6)
//      FMOVE SNaN to B/W/L : SNAN, not OPERR (MC68881UM Table 6-2)
//  15+ timing        : FADD.X FPm,FPn consumes its MC68882UM Table 8-3
//                      figure (56), FMOVECR its (32) — not a placeholder
//
// Exit 0 = all pass, 1 = failure.

#include "MoiraCompat.h"
// Cells for what Dirk Hoffmann's core does not model yet (TODO.md):
//   POM68K_FPU_TIMING    FPU instruction timing (MC68882UM Table 8-3)
//   POM68K_FPU_MIDINSTR  a 68882 instruction interrupted mid-way (format $9)
#define POM68K_FPU_TIMING 0
#define POM68K_FPU_MIDINSTR 0

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

class TestCpu : public MoiraCompat {
public:
    TestCpu() : mem(1 << 24, 0) { setModel(moira::CPUModel::M68030); }
    std::vector<uint8_t> mem;
    void armIrqAfter(int cycles, int level) {
        irqClock = getClock() + cycles; irqLevel = level; irqArmed = true;
    }

private:
    moira::i64 irqClock = 0;
    int irqLevel = 0;
    bool irqArmed = false;
    void sync(int cycles) override {
        clock += cycles;
        if (irqArmed && clock >= irqClock) {
            irqArmed = false;
            setIPL(moira::u8(irqLevel));
        }
    }
    moira::u8  read8(moira::u32 a) const override { return mem[a & 0xFFFFFF]; }
    moira::u16 read16(moira::u32 a) const override {
        return moira::u16((mem[a & 0xFFFFFF] << 8) | mem[(a + 1) & 0xFFFFFF]);
    }
    void write8(moira::u32 a, moira::u8 v) const override {
        const_cast<TestCpu*>(this)->mem[a & 0xFFFFFF] = v;
    }
    void write16(moira::u32 a, moira::u16 v) const override {
        write8(a, moira::u8(v >> 8)); write8(a + 1, moira::u8(v));
    }
};

int failures = 0;

#define CHECK(cond, ...) do { \
    if (!(cond)) { \
        std::printf("  FAIL %s:%d: ", __FILE__, __LINE__); \
        std::printf(__VA_ARGS__); std::printf("\n"); \
        failures++; \
    } } while (0)

// Loads a program at $1000, points vector 11 (F-line) at $3000, and runs
// `steps` instructions with A7 = $8000 (supervisor). `pokes` seeds extra
// memory words (addr/value pairs) before execution.
void run(TestCpu& cpu, const std::vector<uint16_t>& words, int steps, bool fpuAttached,
         const std::vector<std::pair<uint32_t, uint16_t>>& pokes = {}) {
    std::fill(cpu.mem.begin(), cpu.mem.end(), 0);
    // Vector 11 (Line-F) -> $3000; vector 7 (TRAPcc) -> $3100;
    // vector 14 (format error) -> $3200
    cpu.mem[0x2E] = 0x30;
    cpu.mem[0x1E] = 0x31;
    cpu.mem[0x3A] = 0x32;
    uint32_t a = 0x1000;
    for (uint16_t w : words) { cpu.mem[a] = uint8_t(w >> 8); cpu.mem[a + 1] = uint8_t(w); a += 2; }
    for (auto& [pa, pv] : pokes) { cpu.mem[pa] = uint8_t(pv >> 8); cpu.mem[pa + 1] = uint8_t(pv); }

    cpu.setFPUModel(fpuAttached ? MoiraFpu::M68882 : MoiraFpu::NONE);
    cpu.reset();
    cpu.setSR(0x2700);
    cpu.setSP(0x8000);
    cpu.setPC(0x1000);
    cpu.setPC0(0x1000);
    cpu.setIRD(words.empty() ? 0 : words[0]);
    cpu.setIRC(words.size() > 1 ? words[1] : 0);

    for (int i = 0; i < steps; i++) cpu.execute();
}

void checkFp(TestCpu& cpu, int n, uint32_t w0, uint32_t w1, uint32_t w2, const char* what) {
    uint32_t w[3];
    cpu.getFP(n, w);
    CHECK(w[0] == w0 && w[1] == w1 && w[2] == w2,
          "%s: FP%d = %08X %08X %08X, want %08X %08X %08X",
          what, n, w[0], w[1], w[2], w0, w1, w2);
}


// A zero conversion returns before SoftFloat writes its decimal digit count.
// Exercise all signed seven-bit k-factors, including positive values >17.
void packedZeros() {
    int transfers = 0;
    for (auto model : {MoiraFpu::M68881, MoiraFpu::M68882})
    for (bool negative : {false, true}) {
        std::vector<uint16_t> program;
        for (bool dynamic : {false, true}) for (unsigned k = 0; k < 128; ++k) {
            program.push_back(0xF210);               // FMOVE.P FP0,(A0)
            program.push_back(uint16_t(dynamic ? 0x7C10 : 0x6C00 | k));
        }
        TestCpu cpu;
        run(cpu, program, 0, true);
        cpu.setFPUModel(model);
        const uint32_t zero[3] = {negative ? 0x80000000u : 0, 0, 0};
        cpu.setFP(0, zero);
        uint8_t want[12] = {};
        want[0] = negative ? 0x80 : 0;
        uint32_t next = 0x1000;
        for (bool dynamic : {false, true}) for (unsigned k = 0; k < 128; ++k) {
            std::memset(cpu.mem.data() + 0x4000, 0xA5, 12);
            cpu.setA(0, 0x4000);
            cpu.setD(1, 0xDEADBE80u | k);             // only low seven bits count
            cpu.setFPSR(0);
            cpu.execute();
            next += 4;
            CHECK(std::memcmp(cpu.mem.data() + 0x4000, want, 12) == 0 &&
                  cpu.getPC0() == next,
                  "packed zero FPU=%d sign=%d dynamic=%d k=%u PC=%08X FPSR=%08X",
                  int(model), negative, dynamic, k, cpu.getPC0(), cpu.getFPSR());
            // [B] MC68881UM-1989 Table 6-2: OPERR when the k-factor exceeds
            // +17, a zero source included (ruling D40). k is signed 7-bit.
            const bool operr = k < 64 && k > 17;
            CHECK(bool(cpu.getFPSR() & 0x2000) == operr &&
                  bool(cpu.getFPSR() & 0x80) == operr,
                  "packed zero FPU=%d sign=%d dynamic=%d k=%u: FPSR=%08X, OPERR/IOP %s",
                  int(model), negative, dynamic, k, cpu.getFPSR(), operr ? "expected" : "unexpected");
            ++transfers;
        }
    }
    std::printf("packed signed zero: %d static/dynamic k-factor transfers\n", transfers);
}

// The frame of a move-out exception, from the stack pointer up.
// [B] MC68030UM-1989 §10.4.19, Figure 10-43 (manual p. 10-42, PDF p. 542):
// +$00 status register, +$02 SCAN PC, +$06 1001|vector, +$08 PROGRAM
// COUNTER, +$0C internal register, +$0E operation word, +$10 effective
// address; "The program counter value saved in this stack frame is the
// operation word address of the coprocessor instruction". MC68881UM-1989
// Figure 7-16 (PDF p. 283) has the same order, and MC68030UM-1989 Table 8-7
// (PDF p. 432) names +$02 the program counter, "next word to be fetched",
// and +$08 the instruction address.
struct MidFrame {
    uint16_t sr, format, op;
    uint32_t scanPC, pc, ea;
};

MidFrame readMidFrame(const TestCpu& cpu, uint32_t sp) {
    auto m16 = [&](uint32_t a) { return uint16_t(cpu.mem[a] << 8 | cpu.mem[a + 1]); };
    auto m32 = [&](uint32_t a) { return uint32_t(m16(a)) << 16 | m16(a + 2); };
    return {m16(sp), m16(sp + 6), m16(sp + 0x0e), m32(sp + 2), m32(sp + 8), m32(sp + 0x10)};
}

// A 68881 OPERR on FMOVE.L FP0,(A0) of +infinity, through the handler and
// its RTE. The handler records D0 before the ADDQ that follows the FMOVE has
// run, and the FNOP after it finds nothing pending.
// [B] MC68881UM-1989 §6.1.3 (PDF p. 232): with the trap enabled the operand
// "is written as if the trap were disabled" — the largest positive integer
// for +infinity — "and then a take exception primitive is returned", a
// mid-instruction exception. §7.4.2.6, Figure 7-16 (PDF pp. 282-283): the
// frame. §6.1.13 (PDF pp. 246-247): FPIAR points to the FMOVE; the MC68881
// clears the exception on the acknowledge, so a handler may be a bare RTE;
// on RTE the MPU reads the response CIR, which is null (CA=0, PF=1)
// because the MC68881 has finished the instruction: "the main processor
// continues ... with the execution of the next instruction".
// [B] MC68030UM-1989 §8.1.13 (PDF pp. 426-427): RTE of a format $9 frame
// restores the stacked values and reads the response CIR; §10.4.1 (PDF
// p. 525): "When an instruction is completed, the processor transfers the
// value in the scanPC to the program counter". The scan PC is the word at
// +$02, so a handler that rewrites it chooses where execution resumes.
// The MC68882 keeps EXC PEND after the acknowledge (MC68881UM-1989
// §6.4.2.2, §5.2.2): a handler without FSAVE/BSET/FRESTORE is outside its
// contract, so these sequences run on the MC68881 only.
// The internal register word at +$0C is not compared.
// [F] Unknown: its contents. Settled by: a frame captured on a 68030 + 68881.
void moveOutMidInstruction() {
    for (auto model : {moira::CPUModel::M68020, moira::CPUModel::M68030})
    for (bool redirect : {false, true}) {
        TestCpu cpu;
        cpu.setModel(model);
        // The redirecting handler stores $100A (the NOP after the FNOP) at +$02
        std::vector<std::pair<uint32_t, uint16_t>> handler =
            {{0x00d0, 0x0000}, {0x00d2, 0x3300},     // vector 52 (OPERR) -> $3300
             {0x3300, 0x23C0}, {0x3302, 0x0000},     // move.l d0,$5000
             {0x3304, 0x5000}};
        if (redirect) {
            handler.insert(handler.end(),
                {{0x3306, 0x2F7C}, {0x3308, 0x0000},  // move.l #$100a,(2,sp)
                 {0x330A, 0x100A}, {0x330C, 0x0002},
                 {0x330E, 0x4E73}});                 // rte
        } else {
            handler.push_back({0x3306, 0x4E73});     // rte
        }
        run(cpu, {0xF210, 0x6000,                    // fmove.l fp0,(a0)
                  0x5280,                            // addq.l  #1,d0
                  0xF280, 0x0000,                    // fnop
                  0x4E71},                           // nop
            0, true, handler);
        cpu.setFPUModel(MoiraFpu::M68881);
        cpu.setA(0, 0x4000);
        cpu.setD(0, 0);
        const uint32_t inf[3] = {0x7fff0000, 0, 0};
        cpu.setFP(0, inf);
        cpu.setFPCR(0x2000);                         // OPERR enabled
        cpu.setFPSR(0);
        auto m16 = [&](uint32_t a) { return uint16_t(cpu.mem[a] << 8 | cpu.mem[a + 1]); };
        auto m32 = [&](uint32_t a) { return uint32_t(m16(a)) << 16 | m16(a + 2); };

        cpu.execute();                               // FMOVE: store, then the trap
        const uint32_t sp = cpu.getSP();
        const MidFrame f = readMidFrame(cpu, sp);
        if (!redirect) {
            std::printf("68881 move-out model=%d: SP=%08X frame", int(model), sp);
            for (uint32_t i = 0; i < 20; i += 2) std::printf(" %04X", m16(sp + i));
            std::printf("\n");
        }
        CHECK(m32(0x4000) == 0x7fffffff,
              "mid-instruction model=%d: ($4000) = %08X, want 7FFFFFFF stored before the trap",
              int(model), m32(0x4000));
        CHECK(cpu.getPC0() == 0x3300 && sp == 0x7fec,
              "mid-instruction model=%d: PC0/SP = %08X/%08X, want 00003300/00007FEC",
              int(model), cpu.getPC0(), sp);
        CHECK(f.sr == 0x2700 && f.scanPC == 0x1004 && f.format == 0x90d0 &&
              f.pc == 0x1000 && f.op == 0xF210 && f.ea == 0x4000,
              "mid-instruction model=%d: SR %04X scanPC(+2) %08X format %04X PC(+8) %08X "
              "op %04X EA %08X, want 2700 00001004 90D0 00001000 F210 00004000",
              int(model), f.sr, f.scanPC, f.format, f.pc, f.op, f.ea);
        CHECK(cpu.getFPIAR() == 0x1000 && cpu.getFPSR() == 0x00002080,
              "mid-instruction model=%d: FPIAR/FPSR = %08X/%08X, want 00001000/00002080",
              int(model), cpu.getFPIAR(), cpu.getFPSR());

        cpu.execute();                               // MOVE.L D0,$5000
        if (redirect) cpu.execute();                 // MOVE.L #$100A,(2,SP)
        cpu.execute();                               // RTE
        CHECK(m32(0x5000) == 0,
              "mid-instruction model=%d: handler saw D0 = %08X, want 0 (trap before the ADDQ)",
              int(model), m32(0x5000));
        if (redirect) {
            CHECK(cpu.getPC0() == 0x100A && cpu.getSP() == 0x8000,
                  "mid-instruction RTE model=%d: rewritten scan PC: PC0/SP = %08X/%08X, "
                  "want 0000100A/00008000", int(model), cpu.getPC0(), cpu.getSP());
            continue;
        }
        CHECK(cpu.getPC0() == 0x1004 && cpu.getSP() == 0x8000 && cpu.getSR() == 0x2700,
              "mid-instruction RTE model=%d: PC0/SP/SR = %08X/%08X/%04X, want 00001004/00008000/2700 "
              "(the scan PC, not the FMOVE again)",
              int(model), cpu.getPC0(), cpu.getSP(), cpu.getSR());
        cpu.execute();                               // ADDQ.L #1,D0
        cpu.execute();                               // FNOP: nothing pending
        CHECK(cpu.getD(0) == 1 && cpu.getPC0() == 0x100A && cpu.getSP() == 0x8000,
              "mid-instruction model=%d: after FNOP D0/PC0/SP = %08X/%08X/%08X, "
              "want 00000001/0000100A/00008000 (no second trap)",
              int(model), cpu.getD(0), cpu.getPC0(), cpu.getSP());
    }
}

// FMOVE of a signaling NaN to a byte, word or long word on a 6888x: SNAN, not
// OPERR. The destination receives the most significant bits of the
// significand with the SNAN bit set; with OPERR enabled and SNAN disabled
// nothing traps; with SNAN enabled the move takes a mid-instruction SNAN
// exception (vector 54) after the store.
// [B] MC68881UM-1989 Table 6-2 (PDF p. 232): FMOVE to B, W or L raises OPERR
// on "Integer Overflow/Underflow, Source is Non-Signaling NAN, or Source is
// ±infinity". §6.1.2 (PDF p. 231): "If the destination data format is B, W,
// or L, then the 8, 16, or 32 most significant bits of the SNAN significand,
// with the SNAN bit set, are written to the destination"; with the trap
// enabled the result is written the same way "and then a mid-instruction
// exception is signaled". Both bits set IOP in the accrued byte (§6.1).
// The 68040 keeps its own rule (queue patch 7: SNAN and OPERR both set and
// nonmaskable) and is not covered here.
// [F] Unknown: whether a 68040 sets OPERR for an SNAN source. MC68040UM-1993
// Table 9-11 (PDF p. 273) lists only a "nonsignaling NAN", §9.7.3.2 (PDF
// p. 274) any "NAN". Settled by: the FPSR after FMOVE.L FP0,D0 of an SNAN
// on a real 68040, through the FPSP.
void moveOutSignalingNaN() {
    for (auto model : {moira::CPUModel::M68020, moira::CPUModel::M68030})
    for (auto fpuModel : {MoiraFpu::M68881, MoiraFpu::M68882})
    for (uint32_t fpcr : {0x0000u, 0x2000u, 0x4000u})
    for (int dest : {0, 1, 2, 3}) {
        // fmove.l fp0,d0 / fmove.w fp0,d0 / fmove.b fp0,d0 / fmove.l fp0,(a0)
        static const uint16_t ops[] = {0xF200, 0xF200, 0xF200, 0xF210};
        static const uint16_t exts[] = {0x6000, 0x7000, 0x7800, 0x6000};
        static const uint32_t want[] = {0x6abcdef0, 0xdead6abc, 0xdeadbe6a, 0x6abcdef0};
        TestCpu cpu;
        cpu.setModel(model);
        run(cpu, {ops[dest], exts[dest], 0x4E71}, 0, true,
            {{0x00d0, 0x0000}, {0x00d2, 0x3300},     // vector 52 (OPERR) -> $3300
             {0x00d8, 0x0000}, {0x00da, 0x3400},     // vector 54 (SNAN)  -> $3400
             {0x3300, 0x4E71}, {0x3400, 0x4E71}});
        cpu.setFPUModel(fpuModel);
        cpu.setA(0, 0x4000);
        cpu.setD(0, 0xdeadbeef);
        const uint32_t snan[3] = {0x7fff0000, 0x2abcdef0, 0x12345678};
        cpu.setFP(0, snan);
        cpu.setFPCR(fpcr);
        cpu.setFPSR(0);
        auto m16 = [&](uint32_t a) { return uint16_t(cpu.mem[a] << 8 | cpu.mem[a + 1]); };
        auto m32 = [&](uint32_t a) { return uint32_t(m16(a)) << 16 | m16(a + 2); };

        cpu.execute();
        const uint32_t stored = dest == 3 ? m32(0x4000) : cpu.getD(0);
        const bool trap = fpcr == 0x4000;
        const uint32_t sp = cpu.getSP();
        CHECK(stored == want[dest] && cpu.getFPSR() == 0x00004080 &&
              cpu.getPC0() == (trap ? 0x3400u : 0x1004u) && sp == (trap ? 0x7fecu : 0x8000u) &&
              (!trap || m16(sp + 6) == 0x90d8),
              "SNAN move-out model=%d FPU=%d FPCR=%04X dest=%d: stored=%08X FPSR=%08X "
              "PC0/SP=%08X/%08X format=%04X, want %08X 00004080 %s",
              int(model), int(fpuModel), fpcr, dest, stored, cpu.getFPSR(),
              cpu.getPC0(), sp, trap ? m16(sp + 6) : 0, want[dest],
              trap ? "00003400/00007FEC 90D8 (SNAN, format $9)" : "00001004/00008000 (no trap)");
    }
}

}  // namespace

int main() {
    TestCpu cpu;

    // 1 ── detached FPU: F200 takes the F-line trap (vector 11)
    run(cpu, { 0xF200, 0x4000 }, 1, false);
    CHECK(cpu.getPC0() == 0x3000, "detached: PC0 = %08X, want 00003000 (F-line handler)", cpu.getPC0());
    CHECK(cpu.getSP() == 0x8000 - 8, "detached: SP = %08X, want %08X (format $0 frame)", cpu.getSP(), 0x8000 - 8);

    // 2 ── FMOVECR #$00,FP0 = pi (MC68881UM § 4.6.1.3; ROM pattern from
    // WinUAE fpp.c:170, RN => no rounding offset). INEX2 + accrued INEX.
    run(cpu, { 0xF200, 0x5C00 }, 1, true);
    checkFp(cpu, 0, 0x40000000, 0xC90FDAA2, 0x2168C235, "fmovecr pi");
    CHECK(cpu.getFPSR() == 0x00000208, "fmovecr pi: FPSR = %08X, want 00000208 (INEX2|AE_INEX)", cpu.getFPSR());

    // 3 ── FMOVE.L #2,FP0 ; FADD.L #2,FP0 => 4.0 exactly (no flags)
    run(cpu, { 0xF23C, 0x4000, 0x0000, 0x0002,      // fmove.l #2,fp0
               0xF23C, 0x4022, 0x0000, 0x0002 },    // fadd.l  #2,fp0
        2, true);
    checkFp(cpu, 0, 0x40010000, 0x80000000, 0x00000000, "2+2");
    CHECK(cpu.getFPSR() == 0, "2+2: FPSR = %08X, want 0", cpu.getFPSR());

    // 4 ── FMOVE.L #1,FP0 ; FDIV.L #0,FP0 => +inf, DZ (MC68881UM § 5.5)
    run(cpu, { 0xF23C, 0x4000, 0x0000, 0x0001,      // fmove.l #1,fp0
               0xF23C, 0x4020, 0x0000, 0x0000 },    // fdiv.l  #0,fp0
        2, true);
    checkFp(cpu, 0, 0x7FFF0000, 0x00000000, 0x00000000, "1/0");
    CHECK(cpu.getFPSR() == 0x02000410, "1/0: FPSR = %08X, want 02000410 (I cc, DZ|AE_DZ)", cpu.getFPSR());

    // 5 ── FMOVE.L #-1,FP0 ; FSQRT.X FP0,FP0 => NaN, OPERR (§ 5.2)
    run(cpu, { 0xF23C, 0x4000, 0xFFFF, 0xFFFF,      // fmove.l #-1,fp0
               0xF200, 0x0004 },                    // fsqrt.x fp0,fp0
        2, true);
    {
        uint32_t w[3]; cpu.getFP(0, w);
        CHECK((w[0] & 0x7FFF0000) == 0x7FFF0000 && (w[1] | w[2]) != 0,
              "sqrt(-1): FP0 = %08X %08X %08X, want a NaN", w[0], w[1], w[2]);
    }
    CHECK(cpu.getFPSR() == 0x01002080, "sqrt(-1): FPSR = %08X, want 01002080 (NAN cc, OPERR|AE_IOP)", cpu.getFPSR());

    // 6 ── FCMP orderings: 1 cmp 2 -> N set; FSLT D0 -> $FF; FSGT D1 -> $00
    run(cpu, { 0xF23C, 0x4000, 0x0000, 0x0001,      // fmove.l #1,fp0
               0xF23C, 0x4080, 0x0000, 0x0002,      // fmove.l #2,fp1
               0xF200, 0x0438,                      // fcmp.x  fp1,fp0
               0xF240, 0x0014,                      // fslt    d0
               0xF241, 0x0012 },                    // fsgt    d1
        5, true);
    CHECK((cpu.getFPSR() & 0x0F000000) == 0x08000000,
          "fcmp 1,2: FPSR cc = %08X, want N only", cpu.getFPSR() & 0x0F000000);
    CHECK((cpu.getD(0) & 0xFF) == 0xFF, "fslt: D0.b = %02X, want FF", cpu.getD(0) & 0xFF);
    CHECK((cpu.getD(1) & 0xFF) == 0x00, "fsgt: D1.b = %02X, want 00", cpu.getD(1) & 0xFF);

    // 7 ── FMOVEM.X FP0,(A1): raw 96-bit image (2.0) at $4000
    run(cpu, { 0xF23C, 0x4000, 0x0000, 0x0002,      // fmove.l #2,fp0
               0x227C, 0x0000, 0x4000,              // movea.l #$4000,a1
               0xF211, 0xF080 },                    // fmovem.x fp0,(a1)
        3, true);
    {
        static const uint8_t want[12] = { 0x40, 0x00, 0x00, 0x00, 0x80, 0, 0, 0, 0, 0, 0, 0 };
        CHECK(std::memcmp(&cpu.mem[0x4000], want, 12) == 0,
              "fmovem.x: mem[4000] = %02X%02X %02X%02X%02X%02X, want 4000 8000..00",
              cpu.mem[0x4000], cpu.mem[0x4001], cpu.mem[0x4004], cpu.mem[0x4005],
              cpu.mem[0x4006], cpu.mem[0x4007]);
    }

    // 8 ── FMOVE.P FP0,(A1){#3}: 1.0 packs to +1.00E+0 => $00000001 0 0
    // (MC68881UM § 3.6.4 packed format, hand-packed)
    run(cpu, { 0xF23C, 0x4000, 0x0000, 0x0001,      // fmove.l #1,fp0
               0x227C, 0x0000, 0x4000,              // movea.l #$4000,a1
               0xF211, 0x6C03 },                    // fmove.p fp0,(a1){#3}
        3, true);
    {
        static const uint8_t want[12] = { 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0 };
        CHECK(std::memcmp(&cpu.mem[0x4000], want, 12) == 0,
              "fmove.p 1.0{#3}: mem[4000] = %02X%02X%02X%02X..., want 00000001 0 0",
              cpu.mem[0x4000], cpu.mem[0x4001], cpu.mem[0x4002], cpu.mem[0x4003]);
    }

    packedZeros();

    // 9 ── FMOVE.L #$20,FPCR ; FMOVE.L FPCR,D0 (control-register moves)
    run(cpu, { 0xF23C, 0x9000, 0x0000, 0x0020,      // fmove.l #$20,fpcr
               0xF200, 0xB000 },                    // fmove.l fpcr,d0
        2, true);
    CHECK(cpu.getD(0) == 0x20, "fpcr roundtrip: D0 = %08X, want 00000020", cpu.getD(0));
    CHECK(cpu.getFPCR() == 0x20, "fpcr: FPCR = %08X, want 00000020", cpu.getFPCR());

    // 10 ── FRESTORE NULL frame: full FPU reset. [B] MC68881UM-1989 §6.4.2:
    // "a restore of the null state performs the reset function with all
    // floating-point data registers loaded with nonsignaling NANs". The
    // manual names no mantissa: WinUAE's all-ones pattern (fpp.c:2796) and
    // Dirk Hoffmann's default NaN both qualify (ruling 2026-11-24).
    run(cpu, { 0xF23C, 0x9000, 0x0000, 0x0020,      // fmove.l #$20,fpcr
               0xF23C, 0x4000, 0x0000, 0x0002,      // fmove.l #2,fp0
               0x227C, 0x0000, 0x4000,              // movea.l #$4000,a1
               0xF351 },                            // frestore (a1)
        4, true);                                   // mem[$4000] = 0 already
    {
        uint32_t w[3]; cpu.getFP(0, w);
        const bool qnan = (w[0] >> 16 & 0x7FFF) == 0x7FFF && (w[1] & 0x40000000u);
        CHECK(qnan, "frestore null: FP0 = %08X %08X %08X, want a nonsignaling NaN",
              w[0], w[1], w[2]);
    }
    CHECK(cpu.getFPCR() == 0, "frestore null: FPCR = %08X, want 0", cpu.getFPCR());

    // 11 ── FRESTORE 68882 IDLE frame ($1F38xxxx) via (A1)+: accepted, no
    // exception, A1 advances over the whole $3C-byte frame. BIU bit 27
    // set = no pending exception re-armed (fpp.c:2781-2787).
    run(cpu, { 0x227C, 0x0000, 0x4000,              // movea.l #$4000,a1
               0xF359 },                            // frestore (a1)+
        2, true,
        { { 0x4000, 0x1F38 }, { 0x4002, 0x0000 },   // version $1F size $38
          { 0x4038, 0x0800 }, { 0x403A, 0x0000 } }); // BIU flags, bit 27 set
    CHECK(cpu.getA(1) == 0x403C, "frestore idle882: A1 = %08X, want 0000403C", cpu.getA(1));
    CHECK(cpu.getPC0() == 0x1008, "frestore idle882: PC0 = %08X, want 00001008 (no trap)", cpu.getPC0());

    // 12 ── FRESTORE an opaque 68882 BUSY frame: accepted for compatibility
    // error (fpp.c:2788-2790); A1 advances 4 + $D4
    run(cpu, { 0x227C, 0x0000, 0x4000,              // movea.l #$4000,a1
               0xF359 },                            // frestore (a1)+
        2, true,
        { { 0x4000, 0x1FD4 }, { 0x4002, 0x0000 } });
    CHECK(cpu.getA(1) == 0x40D8, "frestore busy882: A1 = %08X, want 000040D8", cpu.getA(1));
    CHECK(cpu.getPC0() == 0x1008, "frestore busy882: PC0 = %08X, want 00001008 (no trap)", cpu.getPC0());

    // 13 ── FRESTORE garbage version: format error, vector 14
    // (fpp.c:2804-2806, D21 PC convention)
    run(cpu, { 0x227C, 0x0000, 0x4000,              // movea.l #$4000,a1
               0xF351 },                            // frestore (a1)
        2, true,
        { { 0x4000, 0x1234 }, { 0x4002, 0x5678 } });
    CHECK(cpu.getPC0() == 0x3200, "frestore garbage: PC0 = %08X, want 00003200 (vector 14)", cpu.getPC0());

    // 14 — an enabled exception of FMOVE FPn,<ea> is a mid-instruction
    // exception, taken right after the store, on every 6888x/MPU pairing,
    // to memory and to a data register.
    // [B] MC68881UM-1989 §6.1 (PDF p. 226): "a take mid-instruction
    // exception primitive is issued after the operand is stored in memory";
    // (PDF p. 228): "FPCP instruction exceptions that arise from the move
    // floating-point data register to external destination instructions are
    // reported to the MPU as mid-instruction exceptions"; §6.1.3 (PDF p. 232): the destination "is written as if
    // the trap were disabled, and then a take exception primitive is
    // returned". §7.4.2.6, Figure 7-16 (PDF pp. 282-283): ten-word frame,
    // ScanPC = the next instruction, PC = the F-line operation word, the
    // operation word image, EA = the destination address. [B] MC68030UM-1989
    // §10.4.19, Figure 10-43 (manual p. 10-42, PDF p. 542): +$02 scan PC,
    // +$08 program counter (see readMidFrame); the EA field is undefined when
    // no effective address was evaluated (a data register destination).
    // This reverses the 2026-11-24 ruling, which read §6.1 "neither the
    // MC68881 nor the MC68882 reports the post-instruction exception" as a
    // deferral to the next FPCP instruction: the move-out exception is not a
    // post-instruction one, it is the mid-instruction one of the same page
    // (PDF p. 226).
    // [D] WinUAE @ Hatari e77819f7 traps at once but stacks format $3.
    for (auto model : {moira::CPUModel::M68EC020, moira::CPUModel::M68020,
                       moira::CPUModel::M68EC030, moira::CPUModel::M68030})
    for (auto fpuModel : {MoiraFpu::M68881, MoiraFpu::M68882})
    // Destinations: d0, (a0), (4,a0), -(a0), (a0)+. The EA field holds the
    // address the operand went to; a predecrement has already updated A0.
    for (int dest : {0, 1, 2, 3, 4}) {
        cpu.setModel(model);
        static const uint16_t ops[] = {0xF200, 0xF210, 0xF228, 0xF220, 0xF218};
        static const uint32_t eas[] = {0, 0x4000, 0x4004, 0x3FFC, 0x4000};
        static const uint32_t a0s[] = {0x4000, 0x4000, 0x4000, 0x3FFC, 0x4004};
        const bool memory = dest != 0;
        const uint16_t op = ops[dest];
        const uint32_t scanPC = dest == 2 ? 0x1006 : 0x1004;
        const uint32_t ea = eas[dest];
        run(cpu, {op, 0x6000, 0x0004, 0x4E71},     // the displacement is a NOP otherwise
            0, true, {{0x00d0, 0x0000}, {0x00d2, 0x3300}, {0x3300, 0x4E73}});
        cpu.setFPUModel(fpuModel);
        cpu.setA(0, 0x4000);
        cpu.setD(0, 0);
        const uint32_t inf[3] = {0x7fff0000, 0, 0};
        cpu.setFP(0, inf);
        cpu.setFPCR(0x2000);                      // integer-conversion OPERR
        cpu.execute();
        const uint32_t sp = cpu.getSP();
        auto m16 = [&](uint32_t a) { return uint16_t(cpu.mem[a] << 8 | cpu.mem[a + 1]); };
        auto m32 = [&](uint32_t a) { return uint32_t(m16(a)) << 16 | m16(a + 2); };
        const MidFrame f = readMidFrame(cpu, sp);
        const uint32_t stored = memory ? m32(ea) : cpu.getD(0);
        CHECK(cpu.getPC0() == 0x3300 && sp == 0x7fec && f.format == 0x90d0 &&
              f.scanPC == scanPC && f.pc == 0x1000 && f.op == op &&
              (!memory || f.ea == ea) && stored == 0x7fffffff && cpu.getA(0) == a0s[dest] &&
              cpu.getFPSR() == 0x00002080 && cpu.getFPIAR() == 0x1000,
              "FP mid-instruction model=%d FPU=%d dest=%d: PC0/SP=%08X/%08X "
              "format=%04X scanPC(+2)=%08X PC(+8)=%08X op=%04X EA=%08X stored=%08X "
              "A0=%08X FPSR/FPIAR=%08X/%08X",
              int(model), int(fpuModel), dest, cpu.getPC0(), sp, f.format,
              f.scanPC, f.pc, f.op, f.ea, stored, cpu.getA(0),
              cpu.getFPSR(), cpu.getFPIAR());
        // A bare RTE handler is within the MC68881's contract only (see
        // moveOutMidInstruction); the MC68882's is left unasserted.
        if (fpuModel != MoiraFpu::M68881) continue;
        cpu.execute();                            // RTE
        CHECK(cpu.getPC0() == scanPC && cpu.getSP() == 0x8000 && cpu.getA(0) == a0s[dest],
              "FP mid-instruction RTE model=%d FPU=%d dest=%d: PC0/SP/A0=%08X/%08X/%08X, "
              "want %08X/00008000/%08X",
              int(model), int(fpuModel), dest, cpu.getPC0(), cpu.getSP(), cpu.getA(0),
              scanPC, a0s[dest]);
    }
    cpu.setModel(moira::CPUModel::M68030);
    moveOutMidInstruction();
    moveOutSignalingNaN();

#if POM68K_FPU_TIMING
    // 15 ── timing smoke: FADD.X FP1,FP0 costs its MC68882UM Table 8-3
    // figure (FPn-to-FPm total = 56), not the old CYCLES_68020 placeholder
    run(cpu, { 0xF200, 0x0422 }, 0, true);          // fadd.x fp1,fp0
    {
        auto c0 = cpu.getClock();
        cpu.execute();
        CHECK(cpu.getClock() - c0 == 56,
              "fadd.x timing: %lld cycles, want 56 (Table 8-3)", (long long)(cpu.getClock() - c0));
    }

    // 16 ── timing smoke: FMOVECR = 32 (Table 8-3)
    run(cpu, { 0xF200, 0x5C00 }, 0, true);          // fmovecr #0,fp0
    {
        auto c0 = cpu.getClock();
        cpu.execute();
        CHECK(cpu.getClock() - c0 == 32,
              "fmovecr timing: %lld cycles, want 32 (Table 8-3)", (long long)(cpu.getClock() - c0));
    }

#endif
#if POM68K_FPU_MIDINSTR
    // 17 ── an IRQ accepted from a 68882 IA checkpoint uses the CPU's
    // format-$9 continuation frame. The handler's FSAVE sees a $D4 BUSY
    // image; FRESTORE + RTE resumes the suspended square root and commits 2.
    run(cpu, { 0xF23C, 0x4000, 0x0000, 0x0004,      // fmove.l #4,fp0
               0xF200, 0x0004 },                    // fsqrt.x fp0,fp0
        1, true,
        { { 0x006C, 0x0000 }, { 0x006E, 0x3400 },   // level-3 autovector
          { 0x3400, 0xF321 },                       // fsave -(a1)
          { 0x3402, 0xF359 },                       // frestore (a1)+
          { 0x3404, 0x4E73 } });                    // rte
    cpu.setSR(0x2000);                              // supervisor, IRQs enabled
    cpu.setA(1, 0x5000);
    cpu.armIrqAfter(16, 3);
    cpu.execute();                                  // interrupted FSQRT
    {
        const uint32_t sp = cpu.getSP();
        auto m16 = [&](uint32_t a) { return uint16_t(cpu.mem[a] << 8 | cpu.mem[a + 1]); };
        auto m32 = [&](uint32_t a) { return uint32_t(m16(a)) << 16 | m16(a + 2); };
        CHECK(cpu.getPC0() == 0x3400 && sp == 0x7fec,
              "68882 mid-IRQ PC0/SP=%08X/%08X", cpu.getPC0(), sp);
        CHECK(m16(sp + 6) == 0x906c && m32(sp + 8) == 0x1008,
              "format-9 word/IA=%04X/%08X", m16(sp + 6), m32(sp + 8));
    }
    cpu.execute();                                  // FSAVE BUSY
    CHECK(cpu.getA(1) == 0x4f28,
          "fsave busy882: A1=%08X, want 00004F28", cpu.getA(1));
    CHECK(cpu.mem[0x4f28] == 0x1f && cpu.mem[0x4f29] == 0xd4 &&
          cpu.mem[0x4f2c] == 'M' && cpu.mem[0x4f2d] == 'O' &&
          cpu.mem[0x4f2e] == 'I' && cpu.mem[0x4f2f] == 'R',
          "fsave busy882 header/magic not $1FD4/MOIR");
    cpu.execute();                                  // FRESTORE BUSY
    cpu.execute();                                  // RTE + completion
    CHECK(cpu.getPC0() == 0x100c && cpu.getA(1) == 0x5000,
          "format-9 resume PC0/A1=%08X/%08X", cpu.getPC0(), cpu.getA(1));
    checkFp(cpu, 0, 0x40000000, 0x80000000, 0x00000000,
            "resumed sqrt(4)");

#endif

    if (failures) { std::printf("[fpu_sanity] FAIL: %d check(s)\n", failures); return 1; }
    std::printf("[fpu_sanity] OK: detached F-line, FMOVECR, FADD, DZ, OPERR, FCMP/FScc, "
                "FMOVEM, packed, FPCR, FRESTORE/exception frames, move-out format $9, 68882 timing/BUSY IRQ\n");
    return 0;
}
