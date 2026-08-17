// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Integrated MC68040 FPU gate. Pins the sparse native opcode map, forced
// precision, FPSP exception/FSAVE state, datatype payloads, FMOVEM ordering
// and BUSY-frame resume against the external-6888x model boundary.

#include "Moira.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

class TestCpu : public moira::Moira {
public:
    TestCpu() : mem(1 << 20, 0) {}
    std::vector<uint8_t> mem;

private:
    moira::u8 read8(moira::u32 a) const override { return mem[a & 0xfffff]; }
    moira::u16 read16(moira::u32 a) const override {
        return moira::u16((mem[a & 0xfffff] << 8) | mem[(a + 1) & 0xfffff]);
    }
    void write8(moira::u32 a, moira::u8 v) const override {
        const_cast<TestCpu*>(this)->mem[a & 0xfffff] = v;
    }
    void write16(moira::u32 a, moira::u16 v) const override {
        write8(a, moira::u8(v >> 8));
        write8(a + 1, moira::u8(v));
    }
};

int failures = 0;

#define CHECK(cond, ...) do { \
    if (!(cond)) { \
        std::printf("  FAIL %s:%d: ", __FILE__, __LINE__); \
        std::printf(__VA_ARGS__); std::printf("\n"); \
        failures++; \
    } \
} while (0)

void prepare(TestCpu& cpu, moira::Model model, uint16_t ext)
{
    std::fill(cpu.mem.begin(), cpu.mem.end(), 0);
    cpu.mem[0x2c] = 0x00;                    // vector 11 -> $00003000
    cpu.mem[0x2d] = 0x00;
    cpu.mem[0x2e] = 0x30;
    cpu.mem[0x2f] = 0x00;
    cpu.mem[0x1000] = 0xf2;
    cpu.mem[0x1001] = 0x00;
    cpu.mem[0x1002] = uint8_t(ext >> 8);
    cpu.mem[0x1003] = uint8_t(ext);

    cpu.setModel(model);
    cpu.setFPUModel(model == moira::Model::M68040
        ? moira::FPUModel::M68040 : moira::FPUModel::M68882);
    cpu.reset();
    cpu.setSR(0x2700);
    cpu.setSP(0x8000);
    cpu.setPC(0x1000);
    cpu.setPC0(0x1000);
    cpu.setIRD(0xf200);
    cpu.setIRC(ext);
}

void setFp(TestCpu& cpu, int n, uint32_t se, uint64_t mant)
{
    uint32_t w[3] = { se, uint32_t(mant >> 32), uint32_t(mant) };
    cpu.setFP(n, w);
}

std::array<uint32_t, 3> getFp(TestCpu& cpu, int n)
{
    std::array<uint32_t, 3> w{};
    cpu.getFP(n, w.data());
    return w;
}

uint16_t get16(const TestCpu& cpu, uint32_t a)
{
    return uint16_t(cpu.mem[a] << 8 | cpu.mem[a + 1]);
}

uint32_t get32(const TestCpu& cpu, uint32_t a)
{
    return uint32_t(cpu.mem[a] << 24 | cpu.mem[a + 1] << 16 |
                    cpu.mem[a + 2] << 8 | cpu.mem[a + 3]);
}

void put16(TestCpu& cpu, uint32_t a, uint16_t v)
{
    cpu.mem[a] = uint8_t(v >> 8); cpu.mem[a + 1] = uint8_t(v);
}

void put32(TestCpu& cpu, uint32_t a, uint32_t v)
{
    put16(cpu, a, uint16_t(v >> 16)); put16(cpu, a + 2, uint16_t(v));
}

void expectOne(TestCpu& cpu, int n, const char* what)
{
    const auto w = getFp(cpu, n);
    CHECK(w[0] == 0x3fff0000 && w[1] == 0x80000000 && w[2] == 0,
          "%s: FP%d=%08X %08X %08X, want 3FFF0000 80000000 00000000",
          what, n, w[0], w[1], w[2]);
}

} // namespace

int main()
{
    TestCpu cpu;

    // The same command word is non-existing on a 68030 + external 68882.
    prepare(cpu, moira::Model::M68030, 0x0040);          // FSMOVE FP0,FP0
    cpu.execute();
    CHECK(cpu.getPC0() == 0x3000,
          "68882 command $40: PC0=%08X, want F-line handler $3000", cpu.getPC0());
    CHECK(cpu.getSP() == 0x7ff8,
          "68882 command $40: SP=%08X, want format-$0 frame at $7FF8", cpu.getSP());

    // The 040 exposes all sixteen FPCR bits; a 68882 hardwires bits 3-0 low.
    prepare(cpu, moira::Model::M68040, 0x0000);
    cpu.setFPCR(0x000f);
    CHECK(cpu.getFPCR() == 0x000f, "68040 FPCR low nibble=%08X", cpu.getFPCR());
    prepare(cpu, moira::Model::M68030, 0x0000);
    cpu.setFPCR(0x000f);
    CHECK(cpu.getFPCR() == 0, "68882 FPCR low nibble=%08X", cpu.getFPCR());

    // Predicate index 167 is one of the silicon differences between the
    // integrated 040 table and the external 6888x table.
    prepare(cpu, moira::Model::M68040, 0x0007);
    put16(cpu, 0x1000, 0xf240);                         // fscc d0
    cpu.setIRD(0xf240); cpu.setIRC(0x0007);
    cpu.setFPSR(0x05000000);                           // Z|NAN
    cpu.setD(0, 0xffffffff);
    cpu.execute();
    CHECK((cpu.getD(0) & 0xff) == 0,
          "040 predicate[167] D0=%08X", cpu.getD(0));
    prepare(cpu, moira::Model::M68030, 0x0007);
    put16(cpu, 0x1000, 0xf240);
    cpu.setIRD(0xf240); cpu.setIRC(0x0007);
    cpu.setFPSR(0x05000000);
    cpu.setD(0, 0);
    cpu.execute();
    CHECK((cpu.getD(0) & 0xff) == 0xff,
          "68882 predicate[167] D0=%08X", cpu.getD(0));

    // A hole in the 040's sparse $40-$7F map remains an ordinary F-line.
    prepare(cpu, moira::Model::M68040, 0x0042);
    cpu.execute();
    CHECK(cpu.getPC0() == 0x3000,
          "68040 reserved command $42: PC0=%08X, want F-line handler $3000", cpu.getPC0());

    // FINT exists on a 68882 but is delegated to FPSP by the 68040.  The
    // trap is format $2 and FSAVE exposes a revision-$41/$30 UNIMP frame.
    prepare(cpu, moira::Model::M68040, 0x0001);          // fint.x fp0,fp0
    setFp(cpu, 0, 0x3fff0000, 0x8000000000000000ULL);
    put16(cpu, 0x3000, 0xf310);                         // fsave (a0)
    cpu.setA(0, 0x4000);
    cpu.execute();
    CHECK(cpu.getPC0() == 0x3000 && cpu.getSP() == 0x7ff4,
          "FINT unimplemented PC0/SP=%08X/%08X", cpu.getPC0(), cpu.getSP());
    CHECK(get32(cpu, 0x7ff6) == 0x1004 && get16(cpu, 0x7ffa) == 0x202c &&
          get32(cpu, 0x7ffc) == 0,
          "FINT format-$2 frame PC/fmt/EA=%08X/%04X/%08X",
          get32(cpu, 0x7ff6), get16(cpu, 0x7ffa), get32(cpu, 0x7ffc));
    cpu.execute();
    CHECK(get32(cpu, 0x4000) == 0x41300000,
          "040 UNIMP FSAVE id=%08X", get32(cpu, 0x4000));
    CHECK(get32(cpu, 0x4004) == 0x00010000 && get32(cpu, 0x4010) == 0x00010000,
          "040 UNIMP command words=%08X/%08X", get32(cpu, 0x4004), get32(cpu, 0x4010));
    CHECK(get32(cpu, 0x4028) == 0x3fff0000 && get32(cpu, 0x402c) == 0x80000000,
          "040 UNIMP ET=%08X/%08X", get32(cpu, 0x4028), get32(cpu, 0x402c));

    // FMOVECR is likewise a software-emulated operation on the 040.
    prepare(cpu, moira::Model::M68040, 0x5c00);
    cpu.execute();
    CHECK(cpu.getPC0() == 0x3000 && get16(cpu, cpu.getSP() + 6) == 0x202c,
          "040 FMOVECR did not take format-$2 unimplemented trap");

    // FSMOVE overrides FPCR extended precision.  1 + 2^-24 is exactly
    // halfway between adjacent single values and RN-even produces 1.0.
    prepare(cpu, moira::Model::M68040, 0x00c0);          // fsmove.x fp0,fp1
    setFp(cpu, 0, 0x3fff0000, 0x8000008000000000ULL);
    cpu.execute();
    expectOne(cpu, 1, "fsmove forced-single");
    CHECK((cpu.getFPSR() & 0x00000208) == 0x00000208,
          "fsmove: FPSR=%08X, want INEX2|AE_INEX", cpu.getFPSR());

    // FDMOVE similarly forces a 53-bit boundary.  1 + 2^-53 is the RN-even
    // halfway case and must not retain the extra extended-precision bit.
    prepare(cpu, moira::Model::M68040, 0x00c4);          // fdmove.x fp0,fp1
    setFp(cpu, 0, 0x3fff0000, 0x8000000000000400ULL);
    cpu.execute();
    expectOne(cpu, 1, "fdmove forced-double");
    CHECK((cpu.getFPSR() & 0x00000208) == 0x00000208,
          "fdmove: FPSR=%08X, want INEX2|AE_INEX", cpu.getFPSR());

    // A nonexceptional 040 operation leaves a four-byte IDLE frame, not a
    // 68882 $3C-byte frame.
    prepare(cpu, moira::Model::M68040, 0x0000);          // fmove.x fp0,fp0
    setFp(cpu, 0, 0x3fff0000, 0x8000000000000000ULL);
    put16(cpu, 0x1004, 0xf310);                         // fsave (a0)
    cpu.setA(0, 0x5000);
    cpu.execute();
    cpu.execute();
    CHECK(get32(cpu, 0x5000) == 0x41000000,
          "040 IDLE FSAVE id=%08X", get32(cpu, 0x5000));

    // Enabled arithmetic exceptions are immediate on the integrated 040,
    // use a format-$3 post-instruction frame, and suppress the FP result.
    prepare(cpu, moira::Model::M68040, 0x0420);          // fdiv.x fp1,fp0
    setFp(cpu, 0, 0x3fff0000, 0x8000000000000000ULL);  // 1
    setFp(cpu, 1, 0x00000000, 0x0000000000000000ULL);  // 0
    cpu.setFPCR(0x0400);                                // enable DZ
    cpu.mem[0x00c8] = 0x00; cpu.mem[0x00c9] = 0x00;    // vector 50 -> $3400
    cpu.mem[0x00ca] = 0x34; cpu.mem[0x00cb] = 0x00;
    put16(cpu, 0x3400, 0xf310);                         // fsave (a0)
    cpu.setA(0, 0x6000);
    cpu.execute();
    CHECK(cpu.getPC0() == 0x3400 && cpu.getSP() == 0x7ff4,
          "040 DZ exception PC0/SP=%08X/%08X", cpu.getPC0(), cpu.getSP());
    CHECK(get16(cpu, 0x7ffa) == 0x30c8 && get32(cpu, 0x7ffc) == 0,
          "040 DZ format-$3 frame=%04X/%08X",
          get16(cpu, 0x7ffa), get32(cpu, 0x7ffc));
    expectOne(cpu, 0, "040 DZ suppresses destination");
    cpu.execute();
    CHECK(get32(cpu, 0x6000) == 0x41300000,
          "040 DZ FSAVE id=%08X", get32(cpu, 0x6000));

    // Denormal/unnormal operands are vector 55 software datatypes on the
    // 040 and leave a BUSY ($60) frame for FPSP.
    prepare(cpu, moira::Model::M68040, 0x0080);          // fmove.x fp0,fp1
    setFp(cpu, 0, 0x00000000, 0x4000000000000000ULL);  // ext denormal
    setFp(cpu, 1, 0x40010000, 0x8000000000000000ULL);  // 4.0
    cpu.mem[0x00dc] = 0x00; cpu.mem[0x00dd] = 0x00;    // vector 55 -> $3500
    cpu.mem[0x00de] = 0x35; cpu.mem[0x00df] = 0x00;
    put16(cpu, 0x3500, 0xf310);                         // fsave (a0)
    cpu.setA(0, 0x7000);
    cpu.execute();
    CHECK(cpu.getPC0() == 0x3500 && get16(cpu, cpu.getSP() + 6) == 0x30dc,
          "040 datatype exception PC0/frame=%08X/%04X",
          cpu.getPC0(), get16(cpu, cpu.getSP() + 6));
    {
        const auto fp1 = getFp(cpu, 1);
        CHECK(fp1[0] == 0x40010000 && fp1[1] == 0x80000000 && fp1[2] == 0,
              "040 datatype trap changed FP1=%08X %08X %08X", fp1[0], fp1[1], fp1[2]);
    }
    cpu.execute();
    CHECK(get32(cpu, 0x7000) == 0x41600000,
          "040 datatype FSAVE id=%08X", get32(cpu, 0x7000));

    // Packed input is a software datatype too, but FPSP needs the original
    // 96 bits in the 040's peculiar FPT/ET split rather than a converted
    // approximation.
    prepare(cpu, moira::Model::M68040, 0x4c00);          // fmove.p (a1),fp0
    put16(cpu, 0x1000, 0xf211); cpu.setIRD(0xf211);
    cpu.setA(1, 0x4200); cpu.setA(0, 0x7200);
    put32(cpu, 0x4200, 0x01234567);
    put32(cpu, 0x4204, 0x89abcdef);
    put32(cpu, 0x4208, 0x76543210);
    cpu.mem[0x00dc] = 0x00; cpu.mem[0x00dd] = 0x00;
    cpu.mem[0x00de] = 0x36; cpu.mem[0x00df] = 0x00;
    put16(cpu, 0x3600, 0xf310);                         // fsave (a0)
    cpu.execute();
    CHECK(cpu.getPC0() == 0x3600, "040 packed input did not take vector 55");
    cpu.execute();
    CHECK(get32(cpu, 0x7200) == 0x41600000 &&
          get32(cpu, 0x723c) == 0xe0000000 &&
          get32(cpu, 0x7250) == 0x89abcdef &&
          get32(cpu, 0x7254) == 0x01234567 &&
          get32(cpu, 0x725c) == 0x89abcdef &&
          get32(cpu, 0x7260) == 0x76543210,
          "040 packed BUSY payload malformed");

    // Packed output to memory records the FP source in both ET/FPT and sets
    // E1/T; the same format aimed at Dn remains an invalid-EA Line-F.
    prepare(cpu, moira::Model::M68040, 0x6c00);          // fmove.p fp0,(a1)
    put16(cpu, 0x1000, 0xf211); cpu.setIRD(0xf211);
    setFp(cpu, 0, 0x40000000, 0x8000000000000000ULL);
    cpu.setA(1, 0x4400); cpu.setA(0, 0x7400);
    cpu.mem[0x00dc] = 0x00; cpu.mem[0x00dd] = 0x00;
    cpu.mem[0x00de] = 0x39; cpu.mem[0x00df] = 0x00;
    put16(cpu, 0x3900, 0xf310);
    cpu.execute(); cpu.execute();
    CHECK(get32(cpu, 0x7448) == 0x04100000 &&
          get32(cpu, 0x744c) == get32(cpu, 0x7458) &&
          get32(cpu, 0x7450) == get32(cpu, 0x745c) &&
          get32(cpu, 0x7454) == get32(cpu, 0x7460),
          "040 packed-output E1/T or FPT/ET payload malformed");
    prepare(cpu, moira::Model::M68040, 0x6c00);          // packed -> D0 invalid
    cpu.execute();
    CHECK(cpu.getPC0() == 0x3000 && cpu.getSP() == 0x7ff8,
          "040 packed-to-Dn should take ordinary Line-F");

    // A denormal encoded as single precision must remain distinguishable
    // after conversion to the core's extended representation (STAG=5 and
    // the 040's synthetic single exponent in ET).
    prepare(cpu, moira::Model::M68040, 0x4400);          // fmove.s (a1),fp0
    put16(cpu, 0x1000, 0xf211); cpu.setIRD(0xf211);
    cpu.setA(1, 0x4300); cpu.setA(0, 0x7300);
    put32(cpu, 0x4300, 0x00000001);                     // min single subnormal
    cpu.mem[0x00dc] = 0x00; cpu.mem[0x00dd] = 0x00;
    cpu.mem[0x00de] = 0x37; cpu.mem[0x00df] = 0x00;
    put16(cpu, 0x3700, 0xf310);
    cpu.execute(); cpu.execute();
    CHECK(get32(cpu, 0x733c) == 0xa0000000 &&
          get32(cpu, 0x7358) == 0x3f800000,
          "040 single-denormal frame STAG/ET=%08X/%08X",
          get32(cpu, 0x733c), get32(cpu, 0x7358));

    // Integer conversions make SNAN/OPERR nonmaskable on the 040 even with
    // FPCR exception enables clear.
    prepare(cpu, moira::Model::M68040, 0x6000);          // fmove.l fp0,d0
    setFp(cpu, 0, 0x7fff0000, 0);                       // infinity
    cpu.mem[0x00d0] = 0x00; cpu.mem[0x00d1] = 0x00;
    cpu.mem[0x00d2] = 0x38; cpu.mem[0x00d3] = 0x00;    // vector 52
    cpu.execute();
    CHECK(cpu.getPC0() == 0x3800 && get16(cpu, cpu.getSP() + 6) == 0x30d0,
          "040 integer OPERR was not an immediate nonmaskable exception");

    // The integrated 040 has its own FMOVEM register-list and 96-bit word
    // order, distinct from the external 6888x convention.
    prepare(cpu, moira::Model::M68040, 0xc080);          // fmovem.x (a1),fp0
    put16(cpu, 0x1000, 0xf211); cpu.setIRD(0xf211);
    cpu.setA(1, 0x4400);
    put32(cpu, 0x4400, 0x40000000); put32(cpu, 0x4404, 0x80000000);
    put32(cpu, 0x4408, 0);
    cpu.execute();
    {
        const auto fp0 = getFp(cpu, 0);
        CHECK(fp0[0] == 0x40000000 && fp0[1] == 0x80000000 && fp0[2] == 0,
              "040 FMOVEM input list selected wrong register");
    }
    prepare(cpu, moira::Model::M68040, 0xe080);          // fmovem.x fp7,(a1)
    put16(cpu, 0x1000, 0xf211); cpu.setIRD(0xf211);
    cpu.setA(1, 0x4500);
    setFp(cpu, 7, 0x40000000, 0x8000000000000000ULL);
    cpu.execute();
    CHECK(get32(cpu, 0x4500) == 0 && get32(cpu, 0x4504) == 0x80000000 &&
          get32(cpu, 0x4508) == 0x40000000,
          "040 FMOVEM output did not reverse low/high/exponent words");

    // FRESTORE resumes a BUSY frame whose CU_SAVEPC requests continuation.
    prepare(cpu, moira::Model::M68040, 0x0000);
    put16(cpu, 0x1000, 0xf358);                         // frestore (a0)+
    cpu.setIRD(0xf358); cpu.setIRC(0);
    cpu.setA(0, 0x4000);
    put32(cpu, 0x4000, 0x41600000);                    // $41/$60 BUSY
    put32(cpu, 0x4008, 0xfe000000);                    // CU_SAVEPC = $FE
    put32(cpu, 0x4040, 0x04220000);                    // fadd.x fp1,fp0
    put32(cpu, 0x404c, 0x3fff0000);                    // FPT = 1.0
    put32(cpu, 0x4050, 0x80000000);
    put32(cpu, 0x4054, 0x00000000);
    put32(cpu, 0x4058, 0x40000000);                    // ET = 2.0
    put32(cpu, 0x405c, 0x80000000);
    put32(cpu, 0x4060, 0x00000000);
    cpu.execute();
    {
        const auto fp0 = getFp(cpu, 0);
        CHECK(fp0[0] == 0x40000000 && fp0[1] == 0xc0000000 && fp0[2] == 0,
              "040 BUSY resume FP0=%08X %08X %08X", fp0[0], fp0[1], fp0[2]);
    }
    CHECK(cpu.getA(0) == 0x4064, "040 BUSY FRESTORE A0=%08X", cpu.getA(0));

    // Execute every native sparse-map command.  FP0 starts at 1.0, FP1 at
    // 4.0; each operation must retire and replace FP0 rather than falling
    // through as Line-F or a silent no-op.
    static constexpr uint8_t native[] = {
        0x40, 0x41, 0x44, 0x45, 0x58, 0x5a, 0x5c, 0x5e,
        0x60, 0x62, 0x63, 0x64, 0x66, 0x67, 0x68, 0x6c
    };
    for (uint8_t opmode : native) {
        const uint16_t ext = uint16_t(0x0400 | opmode); // FP1 source, FP0 dest
        prepare(cpu, moira::Model::M68040, ext);
        setFp(cpu, 0, 0x3fff0000, 0x8000000000000000ULL); // 1.0
        setFp(cpu, 1, 0x40010000, 0x8000000000000000ULL); // 4.0
        const auto before = getFp(cpu, 0);
        cpu.execute();
        const auto after = getFp(cpu, 0);
        CHECK(cpu.getPC0() != 0x3000,
              "68040 command $%02X unexpectedly took F-line", unsigned(opmode));
        CHECK(after != before,
              "68040 command $%02X left FP0 unchanged", unsigned(opmode));
    }

    // Arithmetic forced precision, not just the MOVE encodings: 1 + 2^-24.
    prepare(cpu, moira::Model::M68040, 0x0462);          // fsadd.x fp1,fp0
    setFp(cpu, 0, 0x3fff0000, 0x8000000000000000ULL);
    setFp(cpu, 1, 0x3fe70000, 0x8000000000000000ULL);
    cpu.execute();
    expectOne(cpu, 0, "fsadd forced-single");

    // And the corresponding double boundary: 1 + 2^-53.
    prepare(cpu, moira::Model::M68040, 0x0466);          // fdadd.x fp1,fp0
    setFp(cpu, 0, 0x3fff0000, 0x8000000000000000ULL);
    setFp(cpu, 1, 0x3fca0000, 0x8000000000000000ULL);
    cpu.execute();
    expectOne(cpu, 0, "fdadd forced-double");

    if (failures) {
        std::printf("[fpu040_test] FAIL: %d check(s)\n", failures);
        return 1;
    }
    std::printf("[fpu040_test] OK: integrated model + UNIMP/IDLE frames + native opmodes\n");
    return 0;
}
