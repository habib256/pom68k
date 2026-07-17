// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
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
//  14+ timing        : FADD.X FPm,FPn consumes its MC68882UM Table 8-3
//                      figure (56), FMOVECR its (32) — not a placeholder
//
// Exit 0 = all pass, 1 = failure.

#include "Moira.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

class TestCpu : public moira::Moira {
public:
    TestCpu() : mem(1 << 24, 0) { setModel(moira::Model::M68030); }
    std::vector<uint8_t> mem;

private:
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

    cpu.setFPUModel(fpuAttached ? moira::FPUModel::M68882 : moira::FPUModel::NONE);
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

    // 9 ── FMOVE.L #$20,FPCR ; FMOVE.L FPCR,D0 (control-register moves)
    run(cpu, { 0xF23C, 0x9000, 0x0000, 0x0020,      // fmove.l #$20,fpcr
               0xF200, 0xB000 },                    // fmove.l fpcr,d0
        2, true);
    CHECK(cpu.getD(0) == 0x20, "fpcr roundtrip: D0 = %08X, want 00000020", cpu.getD(0));
    CHECK(cpu.getFPCR() == 0x20, "fpcr: FPCR = %08X, want 00000020", cpu.getFPCR());

    // 10 ── FRESTORE NULL frame: full FPU reset (fpp.c:2796 fpu_null) —
    // FP0 back to the NaN pattern, FPCR cleared
    run(cpu, { 0xF23C, 0x9000, 0x0000, 0x0020,      // fmove.l #$20,fpcr
               0xF23C, 0x4000, 0x0000, 0x0002,      // fmove.l #2,fp0
               0x227C, 0x0000, 0x4000,              // movea.l #$4000,a1
               0xF351 },                            // frestore (a1)
        4, true);                                   // mem[$4000] = 0 already
    checkFp(cpu, 0, 0x7FFF0000, 0xFFFFFFFF, 0xFFFFFFFF, "frestore null");
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

    // 12 ── FRESTORE 68882 BUSY frame ($1FD4xxxx): skipped, not a format
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

    // 14 ── timing smoke: FADD.X FP1,FP0 costs its MC68882UM Table 8-3
    // figure (FPn-to-FPm total = 56), not the old CYCLES_68020 placeholder
    run(cpu, { 0xF200, 0x0422 }, 0, true);          // fadd.x fp1,fp0
    {
        auto c0 = cpu.getClock();
        cpu.execute();
        CHECK(cpu.getClock() - c0 == 56,
              "fadd.x timing: %lld cycles, want 56 (Table 8-3)", (long long)(cpu.getClock() - c0));
    }

    // 15 ── timing smoke: FMOVECR = 32 (Table 8-3)
    run(cpu, { 0xF200, 0x5C00 }, 0, true);          // fmovecr #0,fp0
    {
        auto c0 = cpu.getClock();
        cpu.execute();
        CHECK(cpu.getClock() - c0 == 32,
              "fmovecr timing: %lld cycles, want 32 (Table 8-3)", (long long)(cpu.getClock() - c0));
    }

    if (failures) { std::printf("[fpu_sanity] FAIL: %d check(s)\n", failures); return 1; }
    std::printf("[fpu_sanity] OK: detached F-line, FMOVECR, FADD, DZ, OPERR, FCMP/FScc, "
                "FMOVEM, packed, FPCR, FRESTORE frames, 68882 timing\n");
    return 0;
}
