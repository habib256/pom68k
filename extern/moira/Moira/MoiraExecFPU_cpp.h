// -----------------------------------------------------------------------------
// This file is part of Moira - A Motorola 68k emulator
//
// Copyright (C) Dirk W. Hoffmann. www.dirkwhoffmann.de
// Published under the terms of the MIT License
// -----------------------------------------------------------------------------

bool
Moira::isValidExtFPU(Instr I, Mode M, u16 op, u32 ext) const
{
    auto cod  = xxx_____________ (ext);
    auto mode = ___xx___________ (ext);
    auto fmt  = ___xxx__________ (ext);
    auto lst  = ___xxx__________ (ext);
    auto cmd  = _________xxxxxxx (ext);

    switch (I) {

        case Instr::FDBcc:
        case Instr::FScc:
        case Instr::FTRAPcc:

            return (ext & 0xFFE0) == 0;

        case Instr::FMOVECR:

            return (op & 0x3F) == 0;

        case Instr::FMOVE:

            switch (cod) {

                case 0b010:

                    if (M == Mode::IP) break;
                    return true;

                case 0b000:

                    if (cmd == 0 && cod == 0 && (op & 0x3F)) break;
                    return true;

                case 0b011:

                    if (fmt != 0b011 && fmt != 0b111 && (ext & 0x7F)) break;

                    if (M == Mode::DN) {
                        if (fmt == 0b010 || fmt == 0b011 || fmt == 0b101 || fmt == 0b111) break;
                    }
                    if (M == Mode::AN) {
                        if (fmt == 0b011 || fmt == 0b111) break;
                    }
                    if (M == Mode::DIPC || M == Mode::IXPC || M == Mode::IM || M == Mode::IP) {
                        break;
                    } else {
                        if (fmt == 0b111 && (ext & 0xF)) break;
                    }

                    return true;
            }

        case Instr::FMOVEM:

            switch (cod) {

                case 0b101:
                {

                    if (ext & 0x3FF) break;

                    if (M == Mode::DN || M == Mode::AN) {
                        if (lst != 0b000 && lst != 0b001 && lst != 0b010 && lst != 0b100) break;
                    }
                    if (M == Mode::DIPC || M == Mode::IXPC || M == Mode::IM || M == Mode::IP) {
                        break;
                    }
                    return true;
                }
                case 0b100:

                    if (ext & 0x3FF) break;
                    if (M == Mode::IP) break;
                    return true;

                case 0b110:
                case 0b111:

                    if (ext & 0x0700) break;
                    if (mode == 3 && (ext & 0x8F)) break;

                    if (M == Mode::DN || M == Mode::AN) {
                        break;
                    }
                    if (M == Mode::DIPC || M == Mode::IXPC || M == Mode::IM || M == Mode::IP) {
                        break;
                    }
                    if (M == Mode::AI) {
                        if (mode == 0 || mode == 1) break;
                    }
                    if (M == Mode::PI) {
                        if (mode == 0 || mode == 1 || cod == 0b111) break;
                    }
                    if (M == Mode::PD) {
                        if (cod == 0b110) break;
                        if (cod == 0b111 && (mode == 1) && (ext & 0x8F)) break;
                        if (cod == 0b111 && (mode == 2 || mode == 3)) break;
                    }
                    if (M == Mode::DI || M == Mode::IX || M == Mode::AW || M == Mode::AL) {
                        if (mode == 0 || mode == 1) break;
                    }
                    return true;
            }
            return false;

        default:
            fatalError;
    }
}


// [B] MC68881UM-1989 §2 and §7.5.4.3: programmer model and interrupt protocol.
// [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
//
// POM68K O5/Q9 — MC6888x + integrated MC68040 FPU execution
//
// The 68882 programmer's model (MC68881UM-1989) executed on
// top of extern/softfloat — the same 80-bit softfloat family the primary
// oracle (WinUAE) runs, numerical agreement still requires the differential
// corpus: a shared arithmetic library does not prove instruction semantics.
// The instruction semantics below are ported
// from WinUAE fpp.c / fpp_softfloat.c (oracle vendor tree, hatari e77819f7)
// with file:line citations; the manual is cited where it is the source.
//
// Reachability: nothing in this file executes unless an FPU is attached
// (setFPUModel). With fpuModel == NONE the jump table holds the stock
// Line-F handlers, keeping the FPU-less SST030 corpus byte-identical.
//
// External-coprocessor interruption boundary:
//   * 020/030 pre-instruction traps use format $0 and post-instruction
//     traps architecturally use format $2 (retained D23a uses $3). Long
//     6888x operations publish null/come-again
//     checkpoints; an eligible IRQ stacks format $9 and leaves a resumable
//     coprocessor command.
//   * FSAVE serializes that command as a $1F/$B4 or $1F/$D4 BUSY frame;
//     FRESTORE resumes frames emitted by this model and accepts opaque
//     hardware BUSY frames for compatibility (POM68K_VENDOR.md § FPU).
// The integrated 040 path implements its native opcode subset, FPSP
// unimplemented/datatype traps, revision-$41 FSAVE frames, and BUSY resume.

// [B] MC68881UM-1989 §2.3: FPSR fields.
// [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
// FPSR bit layout (MC68881UM-1989 § 2.3; WinUAE fpp.h:10-17, fpp.c:295-309)
static constexpr u32 FPSR_CC_N      = 0x08000000;
static constexpr u32 FPSR_CC_Z      = 0x04000000;
static constexpr u32 FPSR_CC_I      = 0x02000000;
static constexpr u32 FPSR_CC_NAN    = 0x01000000;

static constexpr u32 FPSR_QUOT_SIGN = 0x00800000;
static constexpr u32 FPSR_QUOT_LSB  = 0x007F0000;

static constexpr u32 FPSR_BSUN      = 0x00008000;
static constexpr u32 FPSR_SNAN      = 0x00004000;
static constexpr u32 FPSR_OPERR     = 0x00002000;
static constexpr u32 FPSR_OVFL      = 0x00001000;
static constexpr u32 FPSR_UNFL      = 0x00000800;
static constexpr u32 FPSR_DZ        = 0x00000400;
static constexpr u32 FPSR_INEX2     = 0x00000200;
static constexpr u32 FPSR_INEX1     = 0x00000100;

static constexpr u32 FPSR_AE_IOP    = 0x00000080;
static constexpr u32 FPSR_AE_OVFL   = 0x00000040;
static constexpr u32 FPSR_AE_UNFL   = 0x00000020;
static constexpr u32 FPSR_AE_DZ     = 0x00000010;
static constexpr u32 FPSR_AE_INEX   = 0x00000008;

// FpuExtended (MoiraTypes.h) mirrors softfloat's floatx80 bit-for-bit
static inline floatx80 fpuToFx80(const FpuExtended &v)
{
    floatx80 r; r.high = v.high; r.low = v.low; return r;
}
static inline FpuExtended fpuFromFx80(floatx80 v)
{
    return FpuExtended { v.high, v.low };
}

// [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
// Operand sizes per format field, in bytes; the second row is the A7
// variant (SP stays word-aligned for .B) — WinUAE fpp.c:1532-1533
static constexpr int fpuSzTab1[8] = { 4, 4, 12, 12, 2, 8, 1, 0 };
static constexpr int fpuSzTab2[8] = { 4, 4, 12, 12, 2, 8, 2, 0 };

// Format conversions (fpp_softfloat.c:201-261). to_exten is a raw bit
// copy (denormals/unnormals preserved); from_exten canonicalizes through
// floatx80_to_floatx80 like the real chip's output stage.
static inline void fpuFromIntV(FpuExtended &v, i32 val)
{
    v = fpuFromFx80(int32_to_floatx80(val));
}
void Moira::fpuToSingleV(FpuExtended &v, u32 w)
{
    v = fpuFromFx80(float32_to_floatx80_allowunnormal(w, fpuFloatStatus.get()));
}
u32 Moira::fpuFromSingleV(const FpuExtended &v)
{
    return floatx80_to_float32(fpuToFx80(v), fpuFloatStatus.get());
}
void Moira::fpuToDoubleV(FpuExtended &v, u32 w1, u32 w2)
{
    v = fpuFromFx80(float64_to_floatx80_allowunnormal((u64(w1) << 32) | w2, fpuFloatStatus.get()));
}
void Moira::fpuFromDoubleV(const FpuExtended &v, u32 *w1, u32 *w2)
{
    float64 f = floatx80_to_float64(fpuToFx80(v), fpuFloatStatus.get());
    *w1 = u32(f >> 32);
    *w2 = u32(f);
}
static inline void fpuToExtendedV(FpuExtended &v, u32 w1, u32 w2, u32 w3)
{
    v.high = u16(w1 >> 16);
    v.low = (u64(w2) << 32) | w3;
}
void Moira::fpuFromExtendedV(const FpuExtended &v, u32 *w1, u32 *w2, u32 *w3)
{
    floatx80 f = floatx80_to_floatx80(fpuToFx80(v), fpuFloatStatus.get());
    *w1 = u32(f.high) << 16;
    *w2 = u32(f.low >> 32);
    *w3 = u32(f.low);
}
i64 Moira::fpuToIntV(const FpuExtended &v, int size)
{
    switch (size) {
        case 0:  return floatx80_to_int8(fpuToFx80(v), fpuFloatStatus.get());
        case 1:  return floatx80_to_int16(fpuToFx80(v), fpuFloatStatus.get());
        case 2:  return floatx80_to_int32(fpuToFx80(v), fpuFloatStatus.get());
        default: return 0;
    }
}

// [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
// Packed decimal (P) input — WinUAE fp_to_pack (fpp_softfloat.c:629-687)
void Moira::fpuToPackedV(FpuExtended &v, const u32 wrd[3])
{
    if (((wrd[0] >> 16) & 0x7fff) == 0x7fff) {
        // Infinity has extended exponent and all-0 packed fraction;
        // NaNs are copied bit by bit
        fpuToExtendedV(v, wrd[0], wrd[1], wrd[2]);
        return;
    }
    if (!(wrd[0] & 0xf) && !wrd[1] && !wrd[2]) {
        // Exponent is ignored when the mantissa is zero
        fpuToExtendedV(v, wrd[0] & 0x80000000, wrd[1], wrd[2]);
        return;
    }

    u32 packExp  = (wrd[0] >> 16) & 0xFFF;
    u32 packInt  = wrd[0] & 0xF;
    u64 packFrac = (u64(wrd[1]) << 32) | wrd[2];
    u32 packSe   = (wrd[0] >> 30) & 1;
    u32 packSm   = (wrd[0] >> 31) & 1;

    i32 exp = 0;
    for (int i = 0; i < 3; i++) { exp *= 10; exp += (packExp >> (8 - i * 4)) & 0xF; }
    if (packSe) exp = -exp;
    exp -= 16;
    if (exp < 0) { exp = -exp; packSe = 1; }

    i64 mant = packInt;
    for (int i = 0; i < 16; i++) { mant *= 10; mant += (packFrac >> (60 - i * 4)) & 0xF; }

    floatx80 f;
    f.high = u16((exp & 0x3FFF) | (packSe ? 0x4000 : 0) | (packSm ? 0x8000 : 0));
    f.low = u64(mant);

    v = fpuFromFx80(floatdecimal_to_floatx80(f, fpuFloatStatus.get()));
}

// [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
// Packed decimal (P) output with k-factor — WinUAE fp_from_pack
// (fpp_softfloat.c:690-751)
void Moira::fpuFromPackedV(const FpuExtended &v, u32 wrd[3], int kfactor)
{
    i32 kf = kfactor;
    floatx80 f = floatx80_to_floatdecimal(fpuToFx80(v), &kf, fpuFloatStatus.get());

    if ((f.high & 0x7FFF) == 0x7FFF) {

        wrd[0] = u32(f.high) << 16;
        wrd[1] = u32(f.low >> 32);
        wrd[2] = u32(f.low);

    } else {

        u32 exponent = f.high & 0x3FFF;
        u64 significand = f.low;

        u32 packInt = 0;
        u64 packFrac = 0;
        i32 len = kf;               // softfloat saved len into kfactor
        while (len > 0) {
            len--;
            u64 digit = significand % 10;
            significand /= 10;
            if (len == 0) packInt = u32(digit);
            else packFrac |= digit << (64 - len * 4);
        }

        u32 packExp = 0, packExp4 = 0;
        len = 4;
        while (len > 0) {
            len--;
            u64 digit = exponent % 10;
            exponent /= 10;
            if (len == 0) packExp4 = u32(digit);
            else packExp |= u32(digit) << (12 - len * 4);
        }

        wrd[0] = (packExp << 16) | (packExp4 << 12) | packInt
               | ((f.high & 0x4000) ? 0x40000000 : 0)
               | ((f.high & 0x8000) ? 0x80000000 : 0);
        wrd[1] = u32(packFrac >> 32);
        wrd[2] = u32(packFrac);
    }
}

// [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
// The 6888x predicate table (WinUAE fpp.c:2078-2096, condition_table_6888x,
// cputester-verified against real silicon). Indexed by
// [(N Z I NAN) * 32 + predicate]; fpuCondEval applies the sixteen 040
// differences after this base lookup.
static const bool fpuCondTable6888x[512] = {
    0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
    0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1,
    0, 1, 0, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1,
    0, 1, 0, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
    0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1,
    0, 1, 0, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1,
    0, 1, 0, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1
};

// [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
// FMOVECR constant ROM (WinUAE fpp.c:169-192, exact 6888x bit patterns;
// the `inexact` flag sets INEX2 and rndoff nudges the low long per
// rounding mode — the real ROM stores more precision than 64 mantissa bits)
struct FpuCrEntry { u32 val[3]; u8 inexact; i8 rndoff[4]; };

static const FpuCrEntry fpuCrTable[22] = {
    { {0x40000000, 0xc90fdaa2, 0x2168c235}, 1, {0,-1,-1, 0} }, //  0 = pi
    { {0x3ffd0000, 0x9a209a84, 0xfbcff798}, 1, {0, 0, 0, 1} }, //  1 = log10(2)
    { {0x40000000, 0xadf85458, 0xa2bb4a9a}, 1, {0, 0, 0, 1} }, //  2 = e
    { {0x3fff0000, 0xb8aa3b29, 0x5c17f0bc}, 1, {0,-1,-1, 0} }, //  3 = log2(e)
    { {0x3ffd0000, 0xde5bd8a9, 0x37287195}, 0, {0, 0, 0, 0} }, //  4 = log10(e)
    { {0x00000000, 0x00000000, 0x00000000}, 0, {0, 0, 0, 0} }, //  5 = 0.0
    { {0x3ffe0000, 0xb17217f7, 0xd1cf79ac}, 1, {0,-1,-1, 0} }, //  6 = ln(2)
    { {0x40000000, 0x935d8ddd, 0xaaa8ac17}, 1, {0,-1,-1, 0} }, //  7 = ln(10)
    { {0x3fff0000, 0x80000000, 0x00000000}, 0, {0, 0, 0, 0} }, //  8 = 1e0
    { {0x40020000, 0xa0000000, 0x00000000}, 0, {0, 0, 0, 0} }, //  9 = 1e1
    { {0x40050000, 0xc8000000, 0x00000000}, 0, {0, 0, 0, 0} }, // 10 = 1e2
    { {0x400c0000, 0x9c400000, 0x00000000}, 0, {0, 0, 0, 0} }, // 11 = 1e4
    { {0x40190000, 0xbebc2000, 0x00000000}, 0, {0, 0, 0, 0} }, // 12 = 1e8
    { {0x40340000, 0x8e1bc9bf, 0x04000000}, 0, {0, 0, 0, 0} }, // 13 = 1e16
    { {0x40690000, 0x9dc5ada8, 0x2b70b59e}, 1, {0,-1,-1, 0} }, // 14 = 1e32
    { {0x40d30000, 0xc2781f49, 0xffcfa6d5}, 1, {0, 0, 0, 1} }, // 15 = 1e64
    { {0x41a80000, 0x93ba47c9, 0x80e98ce0}, 1, {0,-1,-1, 0} }, // 16 = 1e128
    { {0x43510000, 0xaa7eebfb, 0x9df9de8e}, 1, {0,-1,-1, 0} }, // 17 = 1e256
    { {0x46a30000, 0xe319a0ae, 0xa60e91c7}, 1, {0,-1,-1, 0} }, // 18 = 1e512
    { {0x4d480000, 0xc9767586, 0x81750c17}, 1, {0, 0, 0, 1} }, // 19 = 1e1024
    { {0x5a920000, 0x9e8b3b5d, 0xc53d5de5}, 1, {0,-1,-1, 0} }, // 20 = 1e2048
    { {0x75250000, 0xc4605202, 0x8a20979b}, 1, {0,-1,-1, 0} }  // 21 = 1e4094
};

// [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
// Undefined constant ROM offsets — 68881 and 68882 hold identical garbage
// (WinUAE fpp.c:224-236, cputester-dumped)
static const u32 fpuCrUndef[11][3] = {
    { 0x40000000, 0x00000000, 0x00000000 },
    { 0x40010000, 0xfe000682, 0x00000000 },
    { 0x40010000, 0xffc00503, 0x80000000 },
    { 0x20000000, 0x7fffffff, 0x00000000 },
    { 0x00000000, 0xffffffff, 0xffffffff },
    { 0x3c000000, 0xffffffff, 0xfffff800 },
    { 0x3f800000, 0xffffff00, 0x00000000 },
    { 0x00010000, 0xf65d8d9c, 0x00000000 },
    { 0x7fff0000, 0x001e0000, 0x00000000 },
    { 0x43ff0000, 0x000e0000, 0x00000000 },
    { 0x407f0000, 0x00060000, 0x00000000 }
};

// [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
// Dyadic opmode predicate (WinUAE fpp.c:381-384)
static inline bool fpuIsDyadic(u16 ext)
{
    return (ext & 0x30) == 0x20 || (ext & 0x7f) == 0x38;
}

// [B] MC68040UM-1993 Table E-1: native 040 instructions.
// Native MC68040 forced-precision arithmetic opmodes (MC68040UM-1993 Table E-1).
// The 68881/68882 reject the whole $40-$7F range; the 040 implements only
// these sixteen encodings.  Keeping the predicate in one place is important:
// an unknown command in this range is an F-line instruction, not a silent
// arithmetic no-op.
static inline bool fpuIsNative040(u16 ext)
{
    switch (ext & 0x7f) {
        case 0x40: case 0x41: case 0x44: case 0x45: // FSMOVE/FSSQRT/FDMOVE/FDSQRT
        case 0x58: case 0x5a: case 0x5c: case 0x5e: // FSABS/FSNEG/FDABS/FDNEG
        case 0x60: case 0x62: case 0x63:             // FSDIV/FSADD/FSMUL
        case 0x64: case 0x66: case 0x67:             // FDDIV/FDADD/FDMUL
        case 0x68: case 0x6c:                         // FSSUB/FDSUB
            return true;
        default:
            return false;
    }
}

// Base opmodes executed directly by the integrated MC68040 FPU.  Valid
// arithmetic commands not in this list trap to the FPSP as unimplemented;
// reserved holes are filtered separately and take an ordinary Line-F.
static inline bool fpuIsImplemented040(u16 ext)
{
    switch (ext & 0x7f) {
        case 0x00: case 0x04: case 0x18: case 0x1a:
        case 0x20: case 0x22: case 0x23: case 0x24:
        case 0x27: case 0x28: case 0x38: case 0x3a:
            return true;
        default:
            return fpuIsNative040(ext);
    }
}

static inline bool fpuIsNonexistent040(u16 ext)
{
    switch (ext & 0x7f) {
        case 0x05: case 0x07: case 0x0b: case 0x13: case 0x17: case 0x1b:
        case 0x29: case 0x2a: case 0x2b: case 0x2c: case 0x2d: case 0x2e:
        case 0x2f: case 0x39: case 0x3b: case 0x3c: case 0x3d: case 0x3e:
        case 0x3f: case 0x42: case 0x43: case 0x46: case 0x47: case 0x48:
        case 0x49: case 0x4a: case 0x4b: case 0x4c: case 0x4d: case 0x4e:
        case 0x4f: case 0x50: case 0x51: case 0x52: case 0x53: case 0x54:
        case 0x55: case 0x56: case 0x57: case 0x59: case 0x5b: case 0x5d:
        case 0x5f: case 0x61: case 0x65: case 0x69: case 0x6a: case 0x6b:
        case 0x6d: case 0x6e: case 0x6f: case 0x70: case 0x71: case 0x72:
        case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
            return true;
        default:
            return false;
    }
}

static inline int fpuForcedPrecision040(u16 ext)
{
    switch (ext & 0x7f) {
        case 0x40: case 0x41: case 0x58: case 0x5a:
        case 0x60: case 0x62: case 0x63: case 0x68:
            return 32;
        case 0x44: case 0x45: case 0x5c: case 0x5e:
        case 0x64: case 0x66: case 0x67: case 0x6c:
            return 64;
        default:
            return 0;
    }
}

// [B] MC68881UM-1989 §8.5.1: timing tables under their stated assumptions.
//
// MC68882 instruction timing — MC68881UM-1989 Section 8, all figures
// in FPCP clock cycles under the manual's assumptions (68020/030 host,
// same clock, no wait states). One table/section for every FPU cost:
//
//   * fpuCk882Op[]      — Table 8-3 "MC68882 Overall Execution Times",
//                         FPn-to-FPm "Total" column, indexed by opmode.
//   * fpuCk882Fmt[]     — the per-format spread of the same table
//                         (memory/Dn/#imm source adds this on top).
//   * fpuCk882MoveOut[] — Table 8-3 "FMOVE to memory" row.
//   * FPU_CK_* consts   — Table 8-6 (control moves/FMOVEM, cache case,
//                         "+2 clocks if MC68882" footnote applied),
//                         Table 8-7 (conditionals, cache case) and
//                         Table 8-8 (FSAVE/FRESTORE, MC68882, cache case).
//
// The EA-calculation cycles ride on Moira's integer mechanism unchanged:
// computeEA<C68020> accumulates the per-mode penalty into `cp` and
// CYCLES_68020(c) syncs (c) + cp (MoiraMacros.h:25, MoiraDataflow_cpp.h:262)
// — identical to how the integer exec functions charge EAs; the 68000/68010
// paths are untouched (CYCLES_68020 compiles out for them).
//
// Phase-2 cycles are advisory (SST030 `length` is not compared) but
// emuCycles orders events, so the old CYCLES_68020(6/20) placeholders and
// their gross underestimates (a 570-cycle FTWOTOX billed as 20) are gone.

// [B] MC68881UM-1989 Table 8-3: FPn-to-FPm total times.
// Table 8-3, FPn-to-FPm totals by command-word opmode. Alias opmodes
// ($05, $07, ... — see fpuArithmetic) charge their base operation;
// $40-$7F are outside this external-6888x timing table; the 040 has
// its own admitted subset. Alias costs follow the base op as an estimate.
static constexpr i16 fpuCk882Op[64] = {
     21,  58, 690,  58, 110, 110, 574, 574, // FMOVE FINT FSINH FINTRZ FSQRT×2 FLOGNP1×2
    548, 664, 406, 406, 584, 696, 394, 476, // FETOXM1 FTANH FATAN×2 FASIN FATANH FSIN FTAN
    500, 570, 570, 570, 528, 584, 584, 584, // FETOX FTWOTOX FTENTOX×2 FLOGN FLOG10 FLOG2×2
     38, 610,  38,  38, 628, 394,  48,  34, // FABS FCOSH FNEG×2 FACOS FCOS FGETEXP FGETMAN
    108,  75,  56,  76,  74, 105,  46,  64, // FDIV FMOD FADD FMUL FSGLDIV FREM FSCALE FSGLMUL
     56,  56,  56,  56,  56,  56,  56,  56, // FSUB (+aliases $29-$2F)
    454, 454, 454, 454, 454, 454, 454, 454, // FSINCOS
     38,  38,  36,  36,  38,  38,  36,  36  // FCMP FTST (+aliases)
};

// [B] MC68881UM-1989 Table 8-3: format-dependent totals; approximation below.
// Table 8-3 column spread over the FPn-to-FPm column, by format field:
// L +38, S +13, X +25, P +855, W +38, D +19, B +38. The integer formats
// show +30 on the monadic rows and +38 on the dyadic ones; the dyadic
// figure is charged uniformly (advisory, ±8 cycles).
// [F] Uniform dyadic surcharge on monadic formats is an approximation.
// Settled by: per-op/per-format timing with calibrated FPCP/MPU clocks.
static constexpr i16 fpuCk882Fmt[8] = { 38, 13, 25, 855, 38, 19, 38, 855 };

// [B] MC68881UM-1989 Table 8-3: memory destination times.
// Table 8-3 "FMOVE to memory" row, by format field: integer 110, S 38,
// X 50, P 2006 (static k; dynamic k +14 per footnote *****), D 44.
static constexpr i16 fpuCk882MoveOut[8] = { 110, 38, 50, 2006, 110, 44, 110, 2020 };

// [B] MC68881UM-1989 Table 8-3: FMOVECR.
// Table 8-3: FMOVECR total (constant-ROM source)
static constexpr int FPU_CK_FMOVECR = 32;

// [B] MC68881UM-1989 Table 8-7: conditional instruction times.
// Table 8-7 (cache case): FBcc taken/not (FNOP = FBF not-taken), FDBcc
// (cc-true / false-taken / false-not-taken), FScc by destination,
// FTRAPcc taken/not per operand size (none/W/L)
static constexpr int FPU_CK_FBCC_T      = 20;
static constexpr int FPU_CK_FBCC_N      = 18;
static constexpr int FPU_CK_FDBCC_TRUE  = 20;
static constexpr int FPU_CK_FDBCC_TAKEN = 20;
static constexpr int FPU_CK_FDBCC_EXP   = 24;
static constexpr int FPU_CK_FSCC_DN     = 18;
static constexpr int FPU_CK_FSCC_PIPD   = 22;
static constexpr int FPU_CK_FSCC_MEM    = 20;
static constexpr int FPU_CK_FTRAP_T[3]  = { 39, 41, 43 };
static constexpr int FPU_CK_FTRAP_N[3]  = { 18, 20, 22 };

// [B] MC68881UM-1989 Table 8-6: control-register/FMOVEM times.
// Table 8-6 (cache case + the "+2 clocks if MC68882" footnote): single
// control-register moves and per-register FMOVEM increments
static constexpr int FPU_CK_CR_TO_RN    = 33;   // FMOVE FPcr,Rn
static constexpr int FPU_CK_CR_FROM_RN  = 30;   // FMOVE Rn,FPcr
static constexpr int FPU_CK_CR_MEM      = 29;   // FMOVEM FPcr_list,<ea> 29+6n
static constexpr int FPU_CK_CR_IMM      = 27;   // FMOVEM #data,FPcr_list 27+6n
static constexpr int FPU_CK_CR_PER_REG  = 6;
static constexpr int FPU_CK_FDR_TO_MEM  = 39;   // FMOVEM FPdr_list,<ea> 39+25n
static constexpr int FPU_CK_FDR_OUT_REG = 25;
static constexpr int FPU_CK_FDR_FROM_MEM= 37;   // FMOVEM <ea>,FPdr_list 37+31n
static constexpr int FPU_CK_FDR_IN_REG  = 31;
static constexpr int FPU_CK_FDR_DYN     = 14;   // dynamic (Dn) list surcharge

// [B] MC68881UM-1989 Table 8-8: state-frame instruction times.
// Table 8-8 (MC68882/MC68881 rows, cache case): FSAVE/FRESTORE by frame.
// BUSY uses the manual's frame costs for both save/restore staging; an
// invalid frame charges the NULL figure before the format error (documented
// estimate — the manual has no such row).
// [F] Invalid-frame and resumable private BUSY staging costs are estimates.
// Settled by: FSAVE/FRESTORE protocol timing on a real 68881/68882.
static constexpr int FPU_CK_FSAVE_NULL  = 16;
static constexpr int FPU_CK_FSAVE_IDLE2 = 100;  // 68882 $38 IDLE
static constexpr int FPU_CK_FSAVE_IDLE1 = 52;   // 68881 $18 IDLE
static constexpr int FPU_CK_FREST_NULL  = 21;
static constexpr int FPU_CK_FREST_IDLE2 = 105;  // 68882 $38 IDLE
static constexpr int FPU_CK_FREST_IDLE1 = 57;   // 68881 $18 IDLE
static constexpr int FPU_CK_FREST_BUSY2 = 339;  // 68882 $D4 BUSY
static constexpr int FPU_CK_FREST_BUSY1 = 291;  // 68881 $B4 BUSY

// [B] MC68881UM-1989 §6.1.9: exception priority.
// [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
// Exception vector from FPSR exception byte, priority order BSUN > SNAN >
// OPERR > OVFL > UNFL > DZ > INEX (MC68881UM-1989 § 6.1.9; WinUAE fpp.c:441-452)
static u16 fpuVectorFor(u32 exception)
{
    static const u16 vtable[8] = { 49, 49, 50, 51, 53, 52, 54, 48 };
    exception >>= 8;
    for (int i = 7; i >= 0; i--) {
        if (exception & (1u << i)) return vtable[i];
    }
    return 0;
}


//
// FPU state maintenance
//

void
Moira::fpuResetState()
{
    // [B] MC68881UM-1989 §6.4.2.1: NULL restore resets the programmer model.
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // MC68881UM-1989 § 6.4.2.1 (also FRESTORE of a NULL frame, WinUAE
    // fpu_null, fpp.c:1452): control registers cleared, data registers
    // set to the nonsignaling NaN $7FFF FFFFFFFF FFFFFFFF
    fpu.fpcr = 0;
    fpu.fpsr = 0;
    fpu.fpiar = 0;
    for (auto &r : fpu.fp) r = FpuExtended { 0x7fff, 0xffffffffffffffffULL };

    fpu.state = 0;
    fpu.expState = 0;
    fpu.expPend = 0;
    fpu.fsaveCcr = 0;
    fpu.fsaveEo[0] = fpu.fsaveEo[1] = fpu.fsaveEo[2] = 0;
    fpu.ea = fpu.fpiarCu = 0;
    fpu.busy = false;
    fpu.busyUntil = 0;
    fpu.busyOpcode = fpu.busyExt = 0;
    fpu.busyIa = 0;
    fpu.busyFpsr = 0;
    for (auto &r : fpu.busyFp) r = FpuExtended { 0, 0 };
    fpu.cmdreg1b = fpu.cmdreg3b = 0;
    fpu.stag = fpu.dtag = fpu.wbtm66 = fpu.grs = fpu.wbte15 = 0;
    fpu.e1 = fpu.e3 = fpu.t = false;
    for (int i = 0; i < 3; i++) fpu.fpt[i] = fpu.et[i] = fpu.wbt[i] = 0;
}

void
Moira::fpuCompleteBusy(bool wait)
{
    if (!fpu.busy) return;
    if (wait && clock < fpu.busyUntil)
        sync(int(fpu.busyUntil - clock));
    if (clock < fpu.busyUntil) return;

    for (int i = 0; i < 8; i++) fpu.fp[i] = fpu.busyFp[i];
    fpu.fpsr = fpu.busyFpsr;
    fpu.busy = false;
    fpu.state = 1;
}

template <Core C> void
Moira::fpuRunBusy(int cycles, u16 opcode, u16 ext)
{
    // [B] MC68881UM-1989 §7.4.2.1: IA in null response primitives.
    // MC68881/2 protocol null/come-again primitives set IA, allowing the
    // MPU to sample interrupts at short checkpoints while it waits. The
    // integrated 040 never enters this external-coprocessor path.
    fpu.busy = true;
    fpu.busyOpcode = opcode;
    fpu.busyExt = ext;
    fpu.busyUntil = clock + cycles;

    int left = cycles;
    while (left > 0) {
        const int quantum = std::min(left, 8);
        sync(quantum);
        left -= quantum;
        fpu.busyUntil = clock + left;
        pollIpl();
        if (left && (reg.ipl > reg.sr.ipl || reg.ipl == 7))
            throw FpuMidInstruction(reg.ipl);
    }
    fpuCompleteBusy(false);
}

void
Moira::fpuSetMode()
{
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // FPCR mode byte -> softfloat (WinUAE fp_set_mode,
    // fpp_softfloat.c:53-84). Note the 6888x quirk: an invalid precision
    // field ($C0) selects double, not extended.
    set_float_detect_tininess(float_tininess_before_rounding, fpuFloatStatus.get());

    switch (fpu.fpcr & 0xC0) {

        case 0x40: set_floatx80_rounding_precision(32, fpuFloatStatus.get()); break;
        case 0x00: set_floatx80_rounding_precision(80, fpuFloatStatus.get()); break;
        default:   set_floatx80_rounding_precision(64, fpuFloatStatus.get()); break;
    }
    switch (fpu.fpcr & 0x30) {

        case 0x00: set_float_rounding_mode(float_round_nearest_even, fpuFloatStatus.get()); break;
        case 0x10: set_float_rounding_mode(float_round_to_zero, fpuFloatStatus.get()); break;
        case 0x20: set_float_rounding_mode(float_round_down, fpuFloatStatus.get()); break;
        case 0x30: set_float_rounding_mode(float_round_up, fpuFloatStatus.get()); break;
    }

    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // The integrated 040 and external 6888x use different NaN/infinity
    // compatibility flags (WinUAE fp_init_softfloat, fpp_softfloat.c).
    set_special_flags(fpuModel == FPUModel::M68040
        ? cmp_signed_nan : addsub_swap_inf, fpuFloatStatus.get());
}

void
Moira::fpuClearStatus()
{
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // Clear the FPSR exception status byte only (WinUAE fpsr_clear_status,
    // fpp.c:626-633) plus the softfloat flag accumulator
    fpu.fpsr &= 0x0fff00f8;
    fpuFloatStatus->float_exception_flags = 0;
}

u32
Moira::fpuMakeStatus()
{
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // Softfloat flags -> FPSR exception byte + accrued byte (WinUAE
    // fpsr_make_status/fp_get_status/updateaccrued, fpp.c:635-670).
    // Returns the enabled exceptions that abort the register update
    // (SNAN/OPERR/DZ — softfloat build has support_exceptions).
    auto f = fpuFloatStatus->float_exception_flags;

    if (f & float_flag_signaling) fpu.fpsr |= FPSR_SNAN;
    if (f & float_flag_invalid)   fpu.fpsr |= FPSR_OPERR;
    if (f & float_flag_divbyzero) fpu.fpsr |= FPSR_DZ;
    if (f & float_flag_overflow)  fpu.fpsr |= FPSR_OVFL;
    if (f & float_flag_underflow) fpu.fpsr |= FPSR_UNFL;
    if (f & float_flag_inexact)   fpu.fpsr |= FPSR_INEX2;
    if (f & float_flag_decimal)   fpu.fpsr |= FPSR_INEX1;

    if (fpu.fpsr & FPSR_OVFL) fpu.fpsr |= FPSR_AE_OVFL;
    if (fpu.fpsr & (FPSR_OVFL | FPSR_INEX2 | FPSR_INEX1)) fpu.fpsr |= FPSR_AE_INEX;

    u32 exception = fpu.fpsr & fpu.fpcr & (FPSR_SNAN | FPSR_OPERR | FPSR_DZ);

    // [B] MC68040UM-1993 §9.7.4.2 and §9.7.5.2: nonmaskable range exceptions.
    // POM68K (ruling D24): on the 68040 an overflow or an underflow always
    // goes to the FPSP, enabled or not, and the destination register keeps
    // its value (MC68040UM-1993 § 9.7.4.2: "the register is not affected";
    // [D] WinUAE fpp.c fpsr_make_status @ Hatari e77819f7). Invisible while
    // every such exception was delivered before the store.
    if (fpuModel == FPUModel::M68040)
        exception |= fpu.fpsr & (FPSR_OVFL | FPSR_UNFL);

    if (fpu.fpsr & (FPSR_BSUN | FPSR_SNAN | FPSR_OPERR)) fpu.fpsr |= FPSR_AE_IOP;
    if ((fpu.fpsr & FPSR_UNFL) && (fpu.fpsr & FPSR_INEX2)) fpu.fpsr |= FPSR_AE_UNFL;
    if (fpu.fpsr & FPSR_DZ) fpu.fpsr |= FPSR_AE_DZ;

    return exception;
}

void
Moira::fpuSetResultFlags(const FpuExtended &r)
{
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // Condition code byte from a result (WinUAE fpsr_set_result_always +
    // fpsr_set_result, fpp.c:602-625)
    floatx80 fx = fpuToFx80(r);

    fpu.fpsr &= 0x00fffff8;
    if (floatx80_is_negative(fx)) fpu.fpsr |= FPSR_CC_N;

    if (floatx80_is_any_nan(fx))        fpu.fpsr |= FPSR_CC_NAN;
    else if (floatx80_is_zero(fx))      fpu.fpsr |= FPSR_CC_Z;
    else if (floatx80_is_infinity(fx))  fpu.fpsr |= FPSR_CC_I;
}

void
Moira::fpuSetQuotient(u64 quot, u8 sign)
{
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // FMOD/FREM quotient byte (WinUAE fpsr_set_quotient, fpp.c:688); note
    // it clears the cc byte too, cc is re-set from the result afterwards
    fpu.fpsr &= 0x0f00fff8;
    fpu.fpsr |= u32(quot << 16) & FPSR_QUOT_LSB;
    fpu.fpsr |= sign ? FPSR_QUOT_SIGN : 0;
}

void
Moira::fpuGetQuotient(u64 *quot, u8 *sign)
{
    *quot = (fpu.fpsr & FPSR_QUOT_LSB) >> 16;
    *sign = (fpu.fpsr & FPSR_QUOT_SIGN) ? 1 : 0;
}

void
Moira::fpuNormalize(FpuExtended &v)
{
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // The 6888x supports denormal/unnormal operands by normalizing them
    // on input (WinUAE normalize_or_fault_if_no_denormal_support 6888x
    // branch, fpp.c:1464-1481)
    floatx80 fx = fpuToFx80(v);
    if (floatx80_is_unnormal(fx) || floatx80_is_denormal(fx)) {
        if (fpuModel == FPUModel::M68040 && !floatx80_is_zero(fx)) return;
        v = fpuFromFx80(floatx80_normalize(fx));
    }
}

static bool
fpuNeedsDatatype040(const FpuExtended &v)
{
    const floatx80 f = fpuToFx80(v);
    return !floatx80_is_zero(f) &&
        (floatx80_is_unnormal(f) || floatx80_is_denormal(f));
}

static inline bool fpuSingleDenormal040(u32 v)
{
    return (v & 0x7f800000u) == 0 && (v & 0x007fffffu) != 0;
}

static inline bool fpuDoubleDenormal040(u32 hi, u32 lo)
{
    return (hi & 0x7ff00000u) == 0 && ((hi & 0x000fffffu) != 0 || lo != 0);
}

static u8
fpuTag040(const FpuExtended &v, int size)
{
    const floatx80 f = fpuToFx80(v);
    if (floatx80_is_zero(f)) return 1;
    if (floatx80_is_unnormal(f) || floatx80_is_denormal(f))
        return (size == 1 || size == 5) ? 5 : 4;
    if (floatx80_is_any_nan(f)) return 3;
    if (floatx80_is_infinity(f)) return 2;
    return 0;
}

bool
Moira::fpuCheckArithException(
    const FpuExtended &src, u16 opcode, u16 ext, u32 ea, u32 nonmaskable)
{
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // WinUAE fpsr_check_arithmetic_exception, 6888x branch (fpp.c:454-517):
    // latch the highest-priority enabled exception vector; the trap itself
    // fires pre-instruction at the NEXT FPU instruction (coprocessor
    // protocol). Also captures the 68882 FSAVE IDLE-frame data.
    u32 exception = fpu.fpsr & fpu.fpcr & 0xff00;
    if (fpuModel == FPUModel::M68040)
        exception |= fpu.fpsr & (FPSR_OVFL | FPSR_UNFL | nonmaskable);
    if (!exception) return false;

    fpu.expPend = fpuVectorFor(exception);
    fpu.ea = ea;                                        // [D] @ Hatari e77819f7: fpp.c:485 fp_ea
    fpu.fpiar = reg.pc0;                                // [D] @ Hatari e77819f7: fpp.c:487-489

    u32 opclass = ext >> 13 & 7;

    if (fpuModel == FPUModel::M68040) {
        // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
        // Integrated-FPU exception state. The frame fields mirror WinUAE's
        // revision-$41 UNIMP/BUSY capture; who delivers the exception is
        // decided by the return value at the end of this branch.
        fpu.expState = 1;
        fpu.fpiarCu = fpu.fpiar;
        fpu.cmdreg1b = ext;
        fpu.cmdreg3b = 0;
        fpu.stag = fpu.dtag = fpu.wbtm66 = 0;
        fpu.grs = fpu.expPend == 54 ? 7 : 1;
        fpu.wbte15 = fpu.expPend == 54 ? 1 : 0;
        fpu.e1 = true; fpu.e3 = false; fpu.t = opclass == 3;
        for (int i = 0; i < 3; i++) fpu.fpt[i] = fpu.et[i] = fpu.wbt[i] = 0;

        // POM68K: what the handler's FSAVE will show. The FPSP finishes the
        // operation from these fields — WBTEMP is the intermediate result it
        // rounds or denormalizes — so they are the instruction's real
        // output on a trapped overflow or underflow, not bookkeeping.
        // [D] WinUAE fpp.c fpsr_check_arithmetic_exception, "fsave data for
        //     68040" @ Hatari e77819f7, field for field; softfloat keeps
        //     the intermediate values (getFloatInternal*).
        // Until 2026-11-07 WBTEMP, the guard bits and the rounded
        // FPTEMP/ETEMP of a move were left at zero.
        // Gate: sst68040 (fpuh cells: FSAVE as the handler's first word).
        const auto put = [](u32 *dst, const FpuExtended &v) {
            dst[0] = u32(v.high) << 16;
            dst[1] = u32(v.low >> 32);
            dst[2] = u32(v.low);
        };
        const int size = int((ext >> 10) & 7);
        const int dreg = (ext >> 7) & 7;
        const bool trapped =                    // INEX, UNFL, OVFL
            fpu.expPend == 49 || fpu.expPend == 51 || fpu.expPend == 53;

        if (opclass == 3) {

            // Move out: E1 and T; the source with its signaling bit quieted.
            fpu.wbte15 = (fpu.expPend == 51 || fpu.expPend == 54) ? 1 : 0;
            FpuExtended quiet = src;
            if ((quiet.high & 0x7fff) == 0x7fff && (quiet.low << 1) != 0)
                quiet.low |= 0x4000000000000000ULL;
            put(fpu.et, quiet);
            fpu.stag = fpuTag040(src, -1);

        } else if (trapped && (((ext & 0x30) == 0x20) || ((ext & 0x3f) == 0x04))) {

            // FADD/FSUB/FMUL/FDIV/FSQRT: a BUSY frame, E3, with the
            // intermediate result in WBTEMP.
            fpu.expState = 2;
            fpu.e1 = false; fpu.e3 = true;
            fpu.cmdreg3b = u16((ext & 0x03c3) | ((ext & 0x0038) >> 1) |
                               ((ext & 0x0004) << 3));
            put(fpu.wbt, fpuFromFx80(fpu.expPend == 51
                                     ? getFloatInternalUnrounded(fpuFloatStatus.get())
                                     : getFloatInternalRoundedSome(fpuFloatStatus.get())));
            fpu.grs = u8(getFloatInternalGRS(fpuFloatStatus.get()));
            fpu.wbte15 = fpu.expPend == 51 ? 1 : 0;
            put(fpu.et, src);
            fpu.stag = fpuTag040(src, opclass == 0 ? -1 : size);
            if (fpuIsDyadic(ext)) {
                put(fpu.fpt, fpu.fp[dreg]);
                fpu.dtag = fpuTag040(fpu.fp[dreg], -1);
            }

        } else if (trapped) {

            // FMOVE to a register, FABS, FNEG: the rounded value in FPTEMP
            // and, undocumented, the fully rounded one in ETEMP.
            put(fpu.fpt, fpuFromFx80(getFloatInternalFloatx80(fpuFloatStatus.get())));
            fpu.grs = u8(getFloatInternalGRS(fpuFloatStatus.get()));
            put(fpu.et, fpuFromFx80(getFloatInternalRoundedAll(fpuFloatStatus.get())));
            fpu.stag = fpuTag040(src, opclass == 0 ? -1 : size);

        } else {

            // SNAN, OPERR, DZ: the operands as they were.
            put(fpu.et, src);
            fpu.stag = fpuTag040(src, opclass == 0 ? -1 : size);
            if (fpuIsDyadic(ext)) {
                put(fpu.fpt, fpu.fp[dreg]);
                fpu.dtag = fpuTag040(fpu.fp[dreg], -1);
            }
        }

        // [B] MC68040UM-1993 §9.7.2.1, §9.7.3.1 and §9.7.6: posted exceptions.
        // POM68K (ruling D24): an exception the user ENABLED is posted — the
        // instruction completes and the next floating-point instruction
        // takes it as a pre-instruction exception (fpuCheckPending, format
        // $0). Only an exception that is pending without being enabled —
        // the 68040's nonmaskable OVFL/UNFL, or SNAN/OPERR on a move to an
        // integer — is taken at once, post-instruction, by the caller.
        // MC68040UM-1993 § 9.7.2.1 b: "the processor posts an exception and
        //     another floating-point instruction is eventually encountered;
        //     a pre-instruction exception is reported at that time";
        //     § 9.7.3.1 b, § 9.7.6 likewise; § 9.7.3.2 for the immediate
        //     integer case.
        // [D] WinUAE fpp.c fpsr_check_arithmetic_exception @ Hatari e77819f7:
        //     nonmaskable = the latched vector is not the one the enabled
        //     set alone would give.
        // Until 2026-11-07 every exception was delivered at once here.
        // Gate: sst68040 (fpu, fpuraw and the two-instruction fpu2 cells),
        //       fpu040_test.
        return fpu.expPend != fpuVectorFor(fpu.fpsr & fpu.fpcr & 0xff00);
    }

    fpu.fsaveEo[0] = fpu.fsaveEo[1] = fpu.fsaveEo[2] = 0;
    if (opclass == 3) {                                 // [D] @ Hatari e77819f7: fpp.c:500-504
        fpu.fsaveCcr = (u32(ext) << 16) | ext;
    } else {
        fpu.fsaveCcr = (u32(u16(opcode | 0x0080)) << 16) | ext;
    }

    switch (fpu.expPend) {

        case 54: case 52: case 50:                      // SNAN, OPERR, DZ
        {
            fpu.fsaveEo[0] = u32(src.high) << 16;
            fpu.fsaveEo[1] = u32(src.low >> 32);
            fpu.fsaveEo[2] = u32(src.low);
            if (fpu.expPend == 52 && opclass == 3) {    // OPERR on FMOVE out
                fpu.fsaveEo[0] &= 0x4fff0000;
                fpu.fsaveEo[1] = fpu.fsaveEo[2] = 0;
            }
            break;
        }
        case 53:                                        // OVFL
        {
            FpuExtended eo = fpuFromFx80(getFloatInternalOverflow(fpuFloatStatus.get()));
            fpu.fsaveEo[0] = u32(eo.high) << 16;
            fpu.fsaveEo[1] = u32(eo.low >> 32);
            fpu.fsaveEo[2] = u32(eo.low);
            break;
        }
        case 51:                                        // UNFL
        {
            FpuExtended eo = fpuFromFx80(getFloatInternalUnderflow(fpuFloatStatus.get()));
            fpu.fsaveEo[0] = u32(eo.high) << 16;
            fpu.fsaveEo[1] = u32(eo.low >> 32);
            fpu.fsaveEo[2] = u32(eo.low);
            break;
        }
        default:                                        // INEX1/INEX2: none
            break;
    }

    return false;   // maskable on the 6888x — never aborts the update here
}

template <Core C> void
Moira::execFpuException(u16 vector, bool pre)
{
    // [B] MC68030UM-1989 §8.4: external-coprocessor frame; retained oracle exception below.
    // [D] WinUAE @ Hatari e77819f7 — retained ruling D23a.
    // FPU exceptions follow Exception_build_stack_frame_common
    // (newcpu_common.c:1616, nr 48-55): pre-instruction = format $0;
    // post-instruction = format $3 with the operand EA, including on 020/030.
    // The MC68030UM-1989 §8.4 instead specifies the external-coprocessor
    // post-instruction frame as format $2 with an instruction address. The
    // pinned corpus retains the oracle exception (oracle/README.md, D23a).
    // [F] No hardware frame settles this retained divergence. Settled by:
    // a real 68030 + 6888x post-instruction frame and handler RTE sequence.
    u16 status = getSR();

    setSupervisorMode(true);
    clearTraceFlags();
    flags &= ~State::TRACE_EXC;

    if (pre) {

        writeStackFrame0000<C>(status, reg.pc0, vector);

    } else {

        push<C, Long>(fpu.ea);                          // effective address
        push<C, Word>(u16(0x3000 | vector << 2));       // format $3 | offset
        push<C, Long>(reg.pc);                          // next instruction
        push<C, Word>(status);
    }

    jumpToVector<C>(vector);
}

void
Moira::fpuArmFixup(int n)
{
    // Plain (An)± fixup for FPU operands on the 68030 (WinUAE fpp.c
    // get_fp_value/put_fp_value2 modes 3/4, fpp.c:1598-1620, and FScc
    // -(An), fpp.c:2299-2304): the ORIGINAL register value is restored by
    // a non-lastwrite bus fault (newcpu.c m68k_run_mmu030 CATCH ->
    // cpu_restore_fixup), while the frame's wb2/wb3 status byte stays 0
    // (mmu030fixupreg needs the 0x300 flag bits fpp.c never sets). Bit 7
    // marks the fixup as plain; bit 6 = valid (slot occupancy).
    //
    // POM68K: the 68040 arms it too. [D] WinUAE fpp.c get_fp_value /
    // put_fp_value @ Hatari e77819f7 set mmufixup[0] for modes 3 and 4 on
    // every model, and m68k_run_mmu040's CATCH restores it: a faulting FPU
    // operand access leaves An as it was (execMmu040BusError, which never
    // treats an FPU instruction as ending on a "last write").
    if (cpuModel != Model::M68030 && cpuModel < Model::M68EC040) return;

    int slot = mmuFixupReg[0] ? 1 : 0;
    mmuFixupReg[slot] = u8(n | 0x40 | 0x80);
    mmuFixupVal[slot] = reg.a[n];
}

template <Core C> void
Moira::execFpuIllegalOpmode040()
{
    // POM68K: arithmetic opmodes $78-$7F on the 68040 family take the
    // ILLEGAL vector, not Line-F, with the stacked PC past the extension
    // word.
    // [D] WinUAE fpp.c fault_if_nonexisting_opmode @ Hatari e77819f7
    //     ("Unexpected, isn't it?!" -> Exception(4) with m68k_getpc()).
    //     Vector and PC come from the same reading; until 2026-11-07 the
    //     vector was the oracle's and the PC the generic illegal-opcode
    //     one, a pair nobody had observed.
    // [F] Not in MC68040UM-1993, which sends every F-line hole to vector 11.
    //     Settled by: the frame of $F200 $0078 on a real 68040.
    // Gate: sst68040 (fline and fpuraw cells).
    u16 status = getSR();

    setSupervisorMode(true);
    clearTraceFlags();
    flags &= ~State::TRACE_EXC;

    writeStackFrame0000<C>(status, reg.pc, 4);
    jumpToVector<C, AE_SET_CB3>(4);
}

template <Core C> void
Moira::execFRestoreFormatError()
{
    // [F] Retired Musashi comparison: local patched revision unavailable.
    // Settled by: a replay with a pinned replacement oracle.
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // FRESTORE of an invalid frame: format error, vector 14 (WinUAE
    // fpp.c:2792-2806 -> Exception(14) -> format $0 frame with
    // PC = m68k_getpc(), i.e. past ALL consumed words — NOT Moira's
    // generic reg.pc - 2 convention. Solo-corpus arbitrated 2026-07-15,
    // ruling D21 (WinUAE wins; Musashi too weak on this class).
    u16 status = getSR();

    setSupervisorMode(true);
    clearTraceFlags();
    flags &= ~State::TRACE_EXC;

    writeStackFrame0000<C>(status, reg.pc, 14);
    jumpToVector<C, AE_SET_CB3>(14);
}

template <Core C> void
Moira::execFpuDisabled040(u32 ea)
{
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // POM68K Q4 — F2xx on a FPU-less 040 (68LC040/68EC040): vector 11
    // with the format $4 "FP disabled" frame (WinUAE fpu_op_illg
    // fpp.c:1085-1094: fp_unimp_ins -> Exception(11);
    // Exception_build_stack_frame_common nr==11 && fp_unimp_ins &&
    // 68040 && fpu_model==0 -> frame 0x4 {SR, currpc, $402C, fp_ea,
    // instruction_pc}). The caller consumed the shape's extension words
    // (reg.pc mirrors WinUAE's m68k_getpc()) and passes fp_ea.
    // (A Mac-only escape hatch to stack format $0 instead, for the PACK 4
    // glue, was removed on 2026-11-08: no caller had ever set it —
    // POM68K_VENDOR.md § Q8.)

    u16 status = getSR();

    setSupervisorMode(true);
    clearTraceFlags();
    flags &= ~State::TRACE_EXC;
    trace040Pending = false;
    SYNC(4);

    push<C, Long>(reg.pc0);                 // PC of the faulted instruction
    push<C, Long>(ea);                      // effective address (fp_ea)
    push<C, Word>(0x4000 | 11 << 2);        // format $4 | vector offset $2C
    push<C, Long>(reg.pc);                  // PC past the consumed words
    push<C, Word>(status);

    jumpToVector<C>(11);
}

template <Core C> void
Moira::execFpuUnimplemented040(
    u16 opcode, u16 ext, const FpuExtended &src, int dst, int size, u32 ea)
{
    // [B] MC68040UM-1993 §9.6.1 and §9.8: unimplemented-operation state.
    // MC68040 unimplemented arithmetic instruction.  The format-$2
    // exception frame points at the operand EA, while a following FSAVE
    // exposes the revision-$41 UNIMP state used by Motorola FPSP.
    if ((ext & 0x7f) == 0x04) ext |= 1;       // FSQRT command encoding 4 -> 5

    fpu.state = 1;
    fpu.expState = 1;
    fpu.expPend = 0;
    fpu.fpiar = reg.pc0;
    fpu.fpiarCu = reg.pc0;
    fpu.ea = ea;
    fpu.cmdreg3b = u16((ext & 0x03c3) | ((ext & 0x0038) >> 1) |
                       ((ext & 0x0004) << 3));
    fpu.cmdreg1b = ext;
    fpu.stag = fpuTag040(src, size);
    fpu.dtag = 0;
    fpu.wbtm66 = fpu.grs = fpu.wbte15 = 0;
    fpu.e1 = fpu.e3 = fpu.t = false;
    fpu.et[0] = u32(src.high) << 16;
    fpu.et[1] = u32(src.low >> 32);
    fpu.et[2] = u32(src.low);
    for (int i = 0; i < 3; i++) fpu.fpt[i] = fpu.wbt[i] = 0;
    if (dst >= 0) {
        const FpuExtended &d = fpu.fp[dst & 7];
        fpu.fpt[0] = u32(d.high) << 16;
        fpu.fpt[1] = u32(d.low >> 32);
        fpu.fpt[2] = u32(d.low);
        fpu.dtag = fpuTag040(d, -1);
    }

    u16 status = getSR();
    setSupervisorMode(true);
    clearTraceFlags();
    flags &= ~State::TRACE_EXC;
    trace040Pending = false;

    push<C, Long>(ea);                              // format-$2 operand EA
    push<C, Word>(u16(0x2000 | 11 << 2));
    push<C, Long>(reg.pc);                          // resume after FP instruction
    push<C, Word>(status);
    jumpToVector<C>(11);
}

template <Core C> void
Moira::execFpuDatatype040(
    u16 opcode, u16 ext, const FpuExtended &src, int dst, int size, u32 ea,
    const u32 *packed, bool encodedDenormal)
{
    (void)opcode;
    fpu.state = 1;
    fpu.expState = 2;
    fpu.expPend = 55;
    fpu.fpiar = reg.pc0;
    fpu.fpiarCu = reg.pc0;
    fpu.ea = ea;
    // CMDREG1B shows FSQRT as opmode $05, not $04 ([D] WinUAE fpp.c
    // fp_unimp_datatype "FSQRT 4->5" @ Hatari e77819f7).
    fpu.cmdreg1b = (ext & 0x7f) == 0x04 ? u16(ext | 1) : ext;
    fpu.cmdreg3b = u16((ext & 0x03c3) | ((ext & 0x0038) >> 1) |
                       ((ext & 0x0004) << 3));
    // A move out tags its source as what it is, an extended register,
    // whatever the destination format (same function: get_ftag(src, -1)).
    fpu.stag = fpuTag040(src, ((ext >> 13) & 7) == 3 ? -1 : size);
    fpu.dtag = 0;
    fpu.wbtm66 = fpu.grs = fpu.wbte15 = 0;
    fpu.e1 = packed != nullptr;
    fpu.e3 = false;
    fpu.t = ((ext >> 13) & 7) == 3;
    fpu.et[0] = u32(src.high) << 16;
    fpu.et[1] = u32(src.low >> 32);
    fpu.et[2] = u32(src.low);
    for (int i = 0; i < 3; i++) fpu.fpt[i] = fpu.wbt[i] = 0;

    const u32 opclass = (ext >> 13) & 7;
    if (packed && opclass != 3) {
        // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
        // The 040 BUSY frame carries an incoming packed operand in this
        // deliberately odd split form (WinUAE fp_unimp_datatype).
        fpu.fpt[2] = packed[0];
        fpu.fpt[1] = packed[1];
        fpu.et[1] = packed[1];
        fpu.et[2] = packed[2];
        fpu.stag = 7;
    } else if (opclass == 3) {
        // FMOVE-to-packed records the source in both ET and FPT.
        fpu.fpt[0] = fpu.et[0];
        fpu.fpt[1] = fpu.et[1];
        fpu.fpt[2] = fpu.et[2];
        fpu.dtag = fpu.stag;
    } else if (dst >= 0) {
        const FpuExtended &d = fpu.fp[dst & 7];
        fpu.fpt[0] = u32(d.high) << 16;
        fpu.fpt[1] = u32(d.low >> 32);
        fpu.fpt[2] = u32(d.low);
        fpu.dtag = fpuTag040(d, -1);
    }
    if (encodedDenormal) {
        fpu.stag = 5;
        fpu.et[0] = size == 1 ? 0x3f800000u : 0x3c000000u;
    }
    execFpuException<C>(55, false);
}

template <Core C> bool
Moira::fpuCheckPending()
{
    if ((fpuModel == FPUModel::M68881 || fpuModel == FPUModel::M68882) &&
        fpu.busy)
        fpuCompleteBusy(true);

    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // WinUAE fp_exception_pending (fpp.c:386-417): a latched arithmetic
    // exception fires pre-instruction at the next FPU instruction. The
    // 68882 keeps the vector armed after taking it (fpp.c:386-388); the
    // 68881 clears it.
    if (!fpu.expPend) return false;

    u16 vector = u16(fpu.expPend);
    if (fpuModel != FPUModel::M68882) fpu.expPend = 0;
    execFpuException<C>(vector, true);
    return true;
}

template <Core C> int
Moira::fpuCondEval(u16 cond)
{
    // [B] MC68881UM-1989 §4.4 and §4.8.6: predicates and BSUN.
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // WinUAE fpp_cond (fpp.c:2099-2121); predicates use 5 bits, bit 4
    // separates the IEEE non-aware predicates that set BSUN on NaN
    // (MC68881UM-1989 § 4.4)
    if ((cond & 0x10) && (fpu.fpsr & FPSR_CC_NAN)) {

        // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
        // fpsr_set_bsun (fpp.c:672-686)
        fpu.fpsr |= FPSR_BSUN | FPSR_AE_IOP;

        if (fpu.fpcr & FPSR_BSUN) {

            fpu.expPend = 48;
            if (fpuModel != FPUModel::M68882) fpu.expPend = 0;
            execFpuException<C>(48, true);
            return -2;
        }
    }

    int control = (fpu.fpsr >> 24) & 15;
    const int index = control * 32 + (cond & 0x1f);
    bool result = fpuCondTable6888x[index];

    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // The 040/060 predicate table differs from the 6888x table at these
    // sixteen NAN/non-aware entries (WinUAE condition_table_040_060).
    if (fpuModel == FPUModel::M68040) {
        switch (index) {
            case 167: case 174: case 183: case 190:
            case 231: case 238: case 247: case 254:
            case 423: case 430: case 439: case 446:
            case 487: case 494: case 503: case 510:
                result = false;
                break;
        }
    }
    return result ? 1 : 0;
}

bool
Moira::fpuGetConstant(FpuExtended &v, int cr)
{
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // FMOVECR constant ROM (WinUAE fpu_get_constant, fpp.c:783-941).
    // mode/prec from FPCR drive both the inexact-rounding nudge of the
    // defined constants and the bizarre behaviour of undefined offsets.
    u32 f[3] = { 0, 0, 0 };
    int entry = 0;
    int mode = (fpu.fpcr >> 4) & 3;
    int prec = (fpu.fpcr >> 6) & 3;

    switch (cr) {

        case 0x00: entry = 0; break;    // pi
        case 0x0b: entry = 1; break;    // log10(2)
        case 0x0c: entry = 2; break;    // e
        case 0x0d: entry = 3; break;    // log2(e)
        case 0x0e: entry = 4; break;    // log10(e)
        case 0x0f: entry = 5; break;    // 0.0
        case 0x30: entry = 6; break;    // ln(2)
        case 0x31: entry = 7; break;    // ln(10)
        case 0x32: entry = 8; break;    // 1e0
        case 0x33: entry = 9; break;    // 1e1
        case 0x34: entry = 10; break;   // 1e2
        case 0x35: entry = 11; break;   // 1e4
        case 0x36: entry = 12; break;   // 1e8
        case 0x37: entry = 13; break;   // 1e16
        case 0x38: entry = 14; break;   // 1e32
        case 0x39: entry = 15; break;   // 1e64
        case 0x3a: entry = 16; break;   // 1e128
        case 0x3b: entry = 17; break;   // 1e256
        case 0x3c: entry = 18; break;   // 1e512
        case 0x3d: entry = 19; break;   // 1e1024
        case 0x3e: entry = 20; break;   // 1e2048
        case 0x3f: entry = 21; break;   // 1e4096

        default:                        // [D] @ Hatari e77819f7: undefined offsets (fpp.c:850-908)
        {
            bool checkF1Adjust = false;
            int f1Adjust = 0;
            u32 sr = 0;

            if (cr > 10) cr = 0;        // most undefined fields hold entry 0
            f[0] = fpuCrUndef[cr][0];
            f[1] = fpuCrUndef[cr][1];
            f[2] = fpuCrUndef[cr][2];

            // Rounding mode and precision work very strangely here
            switch (cr) {

                case 1:
                    checkF1Adjust = true;
                    break;
                case 2:
                    if (prec == 1 && mode == 3) f1Adjust = -1;
                    break;
                case 3:
                    if (prec == 1 && (mode == 0 || mode == 3)) sr |= FPSR_CC_I;
                    else sr |= FPSR_CC_NAN;
                    break;
                case 7:
                    sr |= FPSR_CC_NAN;
                    checkF1Adjust = true;
                    break;
            }
            if (checkF1Adjust) {
                if (prec == 1) {
                    if (mode == 0) f1Adjust = -1;
                    else if (mode == 1 || mode == 2) f1Adjust = 1;
                }
            }

            fpuToExtendedV(v, f[0], f[1], f[2]);
            if (prec == 1) v = fpuFromFx80(floatx80_round32(fpuToFx80(v), fpuFloatStatus.get()));
            if (prec >= 2) v = fpuFromFx80(floatx80_round64(fpuToFx80(v), fpuFloatStatus.get()));

            if (f1Adjust) {
                f[0] = u32(v.high) << 16;
                f[1] = u32(v.low >> 32) + u32(f1Adjust) * 0x80;
                f[2] = u32(v.low);
                fpuToExtendedV(v, f[0], f[1], f[2]);
            }

            fpuSetResultFlags(v);
            fpu.fpsr |= sr;
            return false;
        }
    }

    f[0] = fpuCrTable[entry].val[0];
    f[1] = fpuCrTable[entry].val[1];
    f[2] = fpuCrTable[entry].val[2];

    // Inexact constants set INEX2 and round; the LSB never wraps
    if (fpuCrTable[entry].inexact) {
        fpu.fpsr |= FPSR_INEX2;
        f[2] += u32(i32(fpuCrTable[entry].rndoff[mode]));
    }

    fpuToExtendedV(v, f[0], f[1], f[2]);

    if (prec == 1) v = fpuFromFx80(floatx80_round32(fpuToFx80(v), fpuFloatStatus.get()));
    if (prec >= 2) v = fpuFromFx80(floatx80_round64(fpuToFx80(v), fpuFloatStatus.get()));

    fpuSetResultFlags(v);
    return true;
}

bool
Moira::fpuArithmetic(const FpuExtended &srcM, FpuExtended &dstM, u16 ext)
{
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // Opmode dispatch, WinUAE fp_arithmetic (fpp.c:2944-3166). The alias
    // opmodes (0x05, 0x07, 0x0b, ... — accepted by the pinned oracle) map
    // onto their base operation. The native MC68040 subset in
    // $40-$7F reaches this function only after execFGeneric has checked the
    // CPU model and the exact whitelist above.
    floatx80 src = fpuToFx80(srcM);
    floatx80 dst = fpuToFx80(dstM);
    u64 quot = 0;
    uint64_t q = 0;     // softfloat's uint64_t may differ from moira::u64
    u8 s = 0;

    // [B] MC68040UM-1993 §9.4.2 and Table E-1: precision/range control.
    // FSMOVE/FDMOVE and the S/D arithmetic variants override FPCR.PREC.
    // Set the precision before the operation, rather than rounding an
    // extended result afterwards: that avoids a double-rounding error and
    // lets SoftFloat raise INEX/OVFL/UNFL at the architecturally selected
    // boundary (MC68040UM-1993 §9.4.2).
    if (int precision = fpuForcedPrecision040(ext))
        set_floatx80_rounding_precision(precision, fpuFloatStatus.get());

    switch (ext & 0x7f) {

        case 0x00: dst = floatx80_move(src, fpuFloatStatus.get()); break;            // FMOVE
        case 0x01: dst = floatx80_round_to_int(src, fpuFloatStatus.get()); break;    // FINT
        case 0x02: dst = floatx80_sinh(src, fpuFloatStatus.get()); break;            // FSINH
        case 0x03: dst = floatx80_round_to_int_toward_zero(src, fpuFloatStatus.get()); break; // FINTRZ
        case 0x04:                                                          // FSQRT
        case 0x05: dst = floatx80_sqrt(src, fpuFloatStatus.get()); break;
        case 0x06:                                                          // FLOGNP1
        case 0x07: dst = floatx80_lognp1(src, fpuFloatStatus.get()); break;
        case 0x08: dst = floatx80_etoxm1(src, fpuFloatStatus.get()); break;          // FETOXM1
        case 0x09: dst = floatx80_tanh(src, fpuFloatStatus.get()); break;            // FTANH
        case 0x0a:                                                          // FATAN
        case 0x0b: dst = floatx80_atan(src, fpuFloatStatus.get()); break;
        case 0x0c: dst = floatx80_asin(src, fpuFloatStatus.get()); break;            // FASIN
        case 0x0d: dst = floatx80_atanh(src, fpuFloatStatus.get()); break;           // FATANH
        case 0x0e: dst = floatx80_sin(src, fpuFloatStatus.get()); break;             // FSIN
        case 0x0f: dst = floatx80_tan(src, fpuFloatStatus.get()); break;             // FTAN
        case 0x10: dst = floatx80_etox(src, fpuFloatStatus.get()); break;            // FETOX
        case 0x11: dst = floatx80_twotox(src, fpuFloatStatus.get()); break;          // FTWOTOX
        case 0x12:                                                          // FTENTOX
        case 0x13: dst = floatx80_tentox(src, fpuFloatStatus.get()); break;
        case 0x14: dst = floatx80_logn(src, fpuFloatStatus.get()); break;            // FLOGN
        case 0x15: dst = floatx80_log10(src, fpuFloatStatus.get()); break;           // FLOG10
        case 0x16:                                                          // FLOG2
        case 0x17: dst = floatx80_log2(src, fpuFloatStatus.get()); break;
        case 0x18: dst = floatx80_abs(src, fpuFloatStatus.get()); break;             // FABS
        case 0x19: dst = floatx80_cosh(src, fpuFloatStatus.get()); break;            // FCOSH
        case 0x1a:                                                          // FNEG
        case 0x1b: dst = floatx80_neg(src, fpuFloatStatus.get()); break;
        case 0x1c: dst = floatx80_acos(src, fpuFloatStatus.get()); break;            // FACOS
        case 0x1d: dst = floatx80_cos(src, fpuFloatStatus.get()); break;             // FCOS
        case 0x1e: dst = floatx80_getexp(src, fpuFloatStatus.get()); break;          // FGETEXP
        case 0x1f: dst = floatx80_getman(src, fpuFloatStatus.get()); break;          // FGETMAN
        case 0x20: dst = floatx80_div(dst, src, fpuFloatStatus.get()); break;        // FDIV
        case 0x21:                                                          // FMOD
            fpuGetQuotient(&quot, &s);
            q = quot;
            dst = floatx80_mod(dst, src, &q, &s, fpuFloatStatus.get());
            fpuSetQuotient(q, s);
            break;
        case 0x22: dst = floatx80_add(dst, src, fpuFloatStatus.get()); break;        // FADD
        case 0x23: dst = floatx80_mul(dst, src, fpuFloatStatus.get()); break;        // FMUL
        case 0x24: dst = floatx80_sgldiv(dst, src, fpuFloatStatus.get()); break;     // FSGLDIV
        case 0x25:                                                          // FREM
            fpuGetQuotient(&quot, &s);
            q = quot;
            dst = floatx80_rem(dst, src, &q, &s, fpuFloatStatus.get());
            fpuSetQuotient(q, s);
            break;
        case 0x26: dst = floatx80_scale(dst, src, fpuFloatStatus.get()); break;      // FSCALE
        case 0x27: dst = floatx80_sglmul(dst, src, fpuFloatStatus.get()); break;     // FSGLMUL
        case 0x28:                                                          // FSUB
        case 0x29: case 0x2a: case 0x2b: case 0x2c: case 0x2d: case 0x2e: case 0x2f:
            dst = floatx80_sub(dst, src, fpuFloatStatus.get());
            break;
        case 0x30: case 0x31: case 0x32: case 0x33:                         // FSINCOS
        case 0x34: case 0x35: case 0x36: case 0x37:
        {
            // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
            // Cosine lands in FPc directly, sine is the stored result
            // (WinUAE fp_sincos, fpp_softfloat.c:527-530); when FPc == FPs
            // the sine (stored last) wins
            floatx80 c;
            dst = floatx80_sincos(src, &c, fpuFloatStatus.get());
            fpu.fp[ext & 7] = fpuFromFx80(c);
            break;
        }
        case 0x38: case 0x39: case 0x3c: case 0x3d:                         // FCMP
        {
            dst = floatx80_cmp(dst, src, fpuFloatStatus.get());
            fpuMakeStatus();
            fpuSetResultFlags(fpuFromFx80(dst));
            return false;
        }
        case 0x3a: case 0x3b: case 0x3e: case 0x3f:                         // FTST
        {
            dst = floatx80_tst(src, fpuFloatStatus.get());
            fpuMakeStatus();
            fpuSetResultFlags(fpuFromFx80(dst));
            return false;
        }

        case 0x40: dst = floatx80_move(src, fpuFloatStatus.get()); break;            // FSMOVE
        case 0x41: dst = floatx80_sqrt(src, fpuFloatStatus.get()); break;            // FSSQRT
        case 0x44: dst = floatx80_move(src, fpuFloatStatus.get()); break;            // FDMOVE
        case 0x45: dst = floatx80_sqrt(src, fpuFloatStatus.get()); break;            // FDSQRT
        case 0x58: dst = floatx80_abs(src, fpuFloatStatus.get()); break;             // FSABS
        case 0x5a: dst = floatx80_neg(src, fpuFloatStatus.get()); break;             // FSNEG
        case 0x5c: dst = floatx80_abs(src, fpuFloatStatus.get()); break;             // FDABS
        case 0x5e: dst = floatx80_neg(src, fpuFloatStatus.get()); break;             // FDNEG
        case 0x60: dst = floatx80_div(dst, src, fpuFloatStatus.get()); break;        // FSDIV
        case 0x62: dst = floatx80_add(dst, src, fpuFloatStatus.get()); break;        // FSADD
        case 0x63: dst = floatx80_mul(dst, src, fpuFloatStatus.get()); break;        // FSMUL
        case 0x64: dst = floatx80_div(dst, src, fpuFloatStatus.get()); break;        // FDDIV
        case 0x66: dst = floatx80_add(dst, src, fpuFloatStatus.get()); break;        // FDADD
        case 0x67: dst = floatx80_mul(dst, src, fpuFloatStatus.get()); break;        // FDMUL
        case 0x68: dst = floatx80_sub(dst, src, fpuFloatStatus.get()); break;        // FSSUB
        case 0x6c: dst = floatx80_sub(dst, src, fpuFloatStatus.get()); break;        // FDSUB
        default:
            return false;   // unreachable — filtered in execFGeneric
    }

    fpuSetResultFlags(fpuFromFx80(dst));

    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // Enabled SNAN/OPERR/DZ abort the register update (fpp.c:3161-3165)
    if (fpuMakeStatus()) return false;

    dstM = fpuFromFx80(dst);
    return true;
}


//
// Operand transfer
//

template <Core C, Mode M> int
Moira::fpuGetSource(u16 opcode, u16 ext, FpuExtended &src, u32 &ea)
{
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // WinUAE get_fp_value (fpp.c:1527-1758); the FPx-to-FPx case is
    // handled by the caller. Returns 0 on invalid encodings (Line-F).
    const int n = _____________xxx(opcode);
    const int size = ___xxx__________(ext);

    // POM68K: on the 68040 an UNIMPLEMENTED instruction takes vector 11
    // whatever its source looks like; the unsupported-datatype trap
    // (vector 55: packed, denormal, unnormal) belongs to the instructions
    // the chip executes itself. The caller raises vector 11 right after
    // this fetch, so the datatype trap must not pre-empt it here.
    // [D] WinUAE @ Hatari e77819f7, get_fp_value: fault_if_unimplemented_680x0
    // precedes every fp_unimp_datatype; first seen on fuzz vector
    // "040 fpu/identity seed=902 #299" (FMOD with a packed source).
    const bool datatype040 =
        fpuModel == FPUModel::M68040 && fpuIsImplemented040(ext);

    if constexpr (M == Mode::DN) {

        switch (size) {

            case 0: fpuFromIntV(src, i32(readD(n))); return 1;              // L
            case 1:
            {
                const u32 raw = readD(n);
                fpuToSingleV(src, raw); fpuNormalize(src);
                if (datatype040 && fpuSingleDenormal040(raw)) {
                    execFpuDatatype040<C>(opcode, ext, src, (ext >> 7) & 7,
                                          size, 0, nullptr, true);
                    return -1;
                }
                return 1;
            }
            case 4: fpuFromIntV(src, i16(readD(n))); return 1;              // W
            case 6: fpuFromIntV(src, i8(readD(n))); return 1;               // B

            default:
                // X, P, D from Dn: F-line (fpp.c:1572-1585) — except that
                // the 68040 traps an unimplemented instruction first, for
                // every size but packed ([D] WinUAE get_fp_value case 0
                // @ Hatari e77819f7; gate: sst68040 fpuraw cells).
                if (fpuModel == FPUModel::M68040 && size != 3 &&
                    !fpuIsImplemented040(ext)) {
                    execFpuUnimplemented040<C>(opcode, ext, src,
                                               (ext >> 7) & 7, size, 0);
                    return -1;
                }
                return 0;
        }

    } else if constexpr (M == Mode::AN || M == Mode::IP) {

        // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
        // fpp.c:1588-1593 / get_fp_ad default; same 68040 priority as Dn.
        if (fpuModel == FPUModel::M68040 && !fpuIsImplemented040(ext)) {
            execFpuUnimplemented040<C>(opcode, ext, src, (ext >> 7) & 7,
                                       size, 0);
            return -1;
        }
        return 0;

    } else if constexpr (M == Mode::IM) {

        // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
        // Immediate extension words are consumed before conversion
        // (fpp.c:1649-1676); B/W immediates occupy one word, value in
        // the low byte/word
        u32 e0, e1, e2;

        switch (size) {

            case 0: fpuFromIntV(src, i32(readI<C, Long>())); return 1;
            case 1:
            {
                const u32 raw = readI<C, Long>();
                fpuToSingleV(src, raw); fpuNormalize(src);
                if (datatype040 && fpuSingleDenormal040(raw)) {
                    execFpuDatatype040<C>(opcode, ext, src, (ext >> 7) & 7,
                                          size, 0, nullptr, true);
                    return -1;
                }
                return 1;
            }
            case 2:
                e0 = readI<C, Long>(); e1 = readI<C, Long>(); e2 = readI<C, Long>();
                fpuToExtendedV(src, e0, e1, e2); fpuNormalize(src); return 1;
            case 3:
            {
                u32 wrd[3];
                wrd[0] = readI<C, Long>(); wrd[1] = readI<C, Long>(); wrd[2] = readI<C, Long>();
                if (datatype040) {
                    const FpuExtended zero { 0, 0 };
                    execFpuDatatype040<C>(opcode, ext, zero, (ext >> 7) & 7,
                                          size, 0, wrd);
                    return -1;
                }
                fpuToPackedV(src, wrd); fpuNormalize(src); return 1;
            }
            case 4: fpuFromIntV(src, i16(readI<C, Word>())); return 1;
            case 5:
                e0 = readI<C, Long>(); e1 = readI<C, Long>();
                fpuToDoubleV(src, e0, e1); fpuNormalize(src);
                if (datatype040 && fpuDoubleDenormal040(e0, e1)) {
                    execFpuDatatype040<C>(opcode, ext, src, (ext >> 7) & 7,
                                          size, 0, nullptr, true);
                    return -1;
                }
                return 1;
            case 6: fpuFromIntV(src, i8(readI<C, Word>())); return 1;

            default:
                return 0;
        }

    } else {

        // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
        // Memory operands. (An)+/-(An) update the register before the
        // access like WinUAE (fpp.c:1598-1620) — the adjustment survives
        // a fault, consistent with Moira's 68000 findings.
        if (size == 7) return 0;

        const int bytes = (n == 7) ? fpuSzTab2[size] : fpuSzTab1[size];
        u32 ad;

        if constexpr (M == Mode::AI) {
            ad = readA(n);
        } else if constexpr (M == Mode::PI) {
            fpuArmFixup(n);                     // [D] @ Hatari e77819f7: fpp.c:1600-1602
            ad = readA(n);
            writeA(n, ad + bytes);
        } else if constexpr (M == Mode::PD) {
            fpuArmFixup(n);                     // [D] @ Hatari e77819f7: fpp.c:1600-1602
            ad = readA(n) - bytes;
            writeA(n, ad);
        } else {
            ad = computeEA<C, M, Long>(n);
        }
        ea = ad;    // [D] @ Hatari e77819f7: WinUAE *adp (fpp.c:1687) — feeds fp_ea on a latch

        switch (size) {

            case 0: fpuFromIntV(src, i32(readM<C, M, Long>(ad))); return 1;
            case 1:
            {
                const u32 raw = readM<C, M, Long>(ad);
                fpuToSingleV(src, raw); fpuNormalize(src);
                if (datatype040 && fpuSingleDenormal040(raw)) {
                    execFpuDatatype040<C>(opcode, ext, src, (ext >> 7) & 7,
                                          size, ea, nullptr, true);
                    return -1;
                }
                return 1;
            }
            case 2:
            {
                u32 w1 = readM<C, M, Long>(ad);
                u32 w2 = readM<C, M, Long>(ad + 4);
                u32 w3 = readM<C, M, Long>(ad + 8);
                fpuToExtendedV(src, w1, w2, w3); fpuNormalize(src); return 1;
            }
            case 3:
            {
                u32 wrd[3];
                wrd[0] = readM<C, M, Long>(ad);
                wrd[1] = readM<C, M, Long>(ad + 4);
                wrd[2] = readM<C, M, Long>(ad + 8);
                if (datatype040) {
                    const FpuExtended zero { 0, 0 };
                    execFpuDatatype040<C>(opcode, ext, zero, (ext >> 7) & 7,
                                          size, ea, wrd);
                    return -1;
                }
                fpuToPackedV(src, wrd); fpuNormalize(src); return 1;
            }
            case 4: fpuFromIntV(src, i16(readM<C, M, Word>(ad))); return 1;
            case 5:
            {
                u32 w1 = readM<C, M, Long>(ad);
                u32 w2 = readM<C, M, Long>(ad + 4);
                fpuToDoubleV(src, w1, w2); fpuNormalize(src);
                if (datatype040 && fpuDoubleDenormal040(w1, w2)) {
                    execFpuDatatype040<C>(opcode, ext, src, (ext >> 7) & 7,
                                          size, ea, nullptr, true);
                    return -1;
                }
                return 1;
            }
            case 6: fpuFromIntV(src, i8(readM<C, M, Byte>(ad))); return 1;

            default:
                return 0;
        }
    }
}

template <Core C, Mode M> int
Moira::fpuPutDest(u16 opcode, u16 ext, FpuExtended value, u32 &ea)
{
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // WinUAE put_fp_value2 (fpp.c:1760-1975). Returns 0 on invalid
    // encodings (Line-F).
    const int n = _____________xxx(opcode);
    const int size = ___xxx__________(ext);

    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // Signed 7-bit k-factor, static (ext) or dynamic (Dn) — fpp.c:1825-1828
    auto kFactor = [&]() {
        int k = (size == 7) ? int(readD((ext >> 4) & 7)) : ext;
        k &= 127;
        if (k & 64) k |= ~63;
        return k;
    };

    if constexpr (M == Mode::DN) {

        // Packed output to Dn is an invalid EA (ordinary Line-F), not a
        // software-datatype operation. Memory packed output is handled below.
        if (fpuModel == FPUModel::M68040 && (size == 3 || size == 7))
            return 0;
        if (fpuModel == FPUModel::M68040 &&
            (size == 0 || size == 1 || size == 4 || size == 6) &&
            fpuNeedsDatatype040(value)) {
            execFpuDatatype040<C>(opcode, ext, value, -1, size, 0);
            return -1;
        }

        switch (size) {

            case 6: // B
                fpuNormalize(value);
                writeD<Byte>(n, u32(fpuToIntV(value, 0)));
                return 1;
            case 4: // W
                fpuNormalize(value);
                writeD<Word>(n, u32(fpuToIntV(value, 1)));
                return 1;
            case 0: // L
                fpuNormalize(value);
                writeD(n, u32(fpuToIntV(value, 2)));
                return 1;
            case 1: // S
                fpuNormalize(value);
                writeD(n, fpuFromSingleV(value));
                return 1;
            case 3: case 7: // P
            {
                // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
                // The k-factor conversion runs — and updates FPSR — even
                // though the Dn destination then takes an F-line
                // (fpp.c:1819-1834 "checked even if EA is illegal").
                // WinUAE folds the raw host flags only (fpp_get_status,
                // no accrued-byte propagation).
                u32 wrd[3];
                int k = kFactor();
                fpuNormalize(value);
                fpuFromPackedV(value, wrd, k);

                auto f = fpuFloatStatus->float_exception_flags;
                if (f & float_flag_signaling) fpu.fpsr |= FPSR_SNAN;
                if (f & float_flag_invalid)   fpu.fpsr |= FPSR_OPERR;
                if (f & float_flag_divbyzero) fpu.fpsr |= FPSR_DZ;
                if (f & float_flag_overflow)  fpu.fpsr |= FPSR_OVFL;
                if (f & float_flag_underflow) fpu.fpsr |= FPSR_UNFL;
                if (f & float_flag_inexact)   fpu.fpsr |= FPSR_INEX2;
                if (f & float_flag_decimal)   fpu.fpsr |= FPSR_INEX1;
                return 0;
            }
            default:        // X, D into Dn: F-line
                return 0;
        }

    } else if constexpr (M == Mode::AN || M == Mode::DIPC || M == Mode::IXPC ||
                         M == Mode::IM || M == Mode::IP) {

        return 0;           // [D] @ Hatari e77819f7: fpp.c:1839, 1887-1889

    } else {

        const int szIdx = (size == 7) ? 3 : size;   // dynamic P sizes as P
        const int bytes = (n == 7) ? fpuSzTab2[szIdx] : fpuSzTab1[szIdx];
        u32 ad;

        if constexpr (M == Mode::AI) {
            ad = readA(n);
        } else if constexpr (M == Mode::PI) {
            fpuArmFixup(n);                     // [D] @ Hatari e77819f7: fpp.c:1847-1849
            ad = readA(n);
            writeA(n, ad + bytes);
        } else if constexpr (M == Mode::PD) {
            fpuArmFixup(n);                     // [D] @ Hatari e77819f7: fpp.c:1847-1849
            ad = readA(n) - bytes;
            writeA(n, ad);
        } else {
            ad = computeEA<C, M, Long>(n);
        }
        ea = ad;    // [D] @ Hatari e77819f7: WinUAE *adp (fpp.c:1893) — feeds fp_ea on a latch

        if (fpuModel == FPUModel::M68040 &&
            (size == 3 || size == 7 || fpuNeedsDatatype040(value))) {
            const u32 packedMarker[3] {};
            execFpuDatatype040<C>(opcode, ext, value, -1, size, ea,
                                  (size == 3 || size == 7) ? packedMarker : nullptr);
            return -1;
        }

        switch (size) {

            case 0: // L
                fpuNormalize(value);
                writeM<C, M, Long>(ad, u32(fpuToIntV(value, 2)));
                return 1;
            case 1: // S
                fpuNormalize(value);
                writeM<C, M, Long>(ad, fpuFromSingleV(value));
                return 1;
            case 2: // X
            {
                fpuNormalize(value);
                u32 w1, w2, w3;
                fpuFromExtendedV(value, &w1, &w2, &w3);
                writeM<C, M, Long>(ad, w1);
                writeM<C, M, Long>(ad + 4, w2);
                writeM<C, M, Long>(ad + 8, w3);
                return 1;
            }
            case 3: case 7: // P (static / dynamic k-factor)
            {
                u32 wrd[3];
                int k = kFactor();
                fpuNormalize(value);
                fpuFromPackedV(value, wrd, k);
                writeM<C, M, Long>(ad, wrd[0]);
                writeM<C, M, Long>(ad + 4, wrd[1]);
                writeM<C, M, Long>(ad + 8, wrd[2]);
                return 1;
            }
            case 4: // W
                fpuNormalize(value);
                writeM<C, M, Word>(ad, u32(fpuToIntV(value, 1)) & 0xffff);
                return 1;
            case 5: // D
            {
                fpuNormalize(value);
                u32 w1, w2;
                fpuFromDoubleV(value, &w1, &w2);
                writeM<C, M, Long>(ad, w1);
                writeM<C, M, Long>(ad + 4, w2);
                return 1;
            }
            case 6: // B
                fpuNormalize(value);
                writeM<C, M, Byte>(ad, u32(fpuToIntV(value, 0)) & 0xff);
                return 1;

            default:
                return 0;
        }
    }
}


//
// Instruction handlers
//

template <Core C, Instr I, Mode M, Size S> void
Moira::execFBcc(u16 opcode)
{
    AVAILABILITY(Core::C68020)

    // [B] MC68881UM-1989 §4.6 (FBcc): conditional branch.
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // FBcc (MC68881UM-1989 § 4.6; WinUAE fpuop_bcc, fpp.c:2342-2372). FNOP is
    // FBF.W with zero displacement and needs no special handling: the
    // predicate is never true and the fall-through consumes the word.
    // POM68K Q4: FPU-less 040 (68LC040/68EC040) — the shape's words are
    // consumed, then vector 11 / format $4 (see execFpuDisabled040)
    if (cpuModel >= Model::M68EC040 && !hasFPU()) {
        (void)readI<C, S>();                // the displacement
        execFpuDisabled040<C>(0);           // fpuop_bcc: fault ea = 0
        FINALIZE
        return;
    }

    if (fpuCheckPending<C>()) return;

    // POM68K: the predicate is five bits wide; bit 5 of the opcode's
    // six-bit field is ignored, as fpuCondEval already does for FScc,
    // FDBcc and FTRAPcc.
    // [D] WinUAE fpp.c fpp_cond @ Hatari e77819f7 opens on
    //     `condition &= 0x1f` for every FPU model. Until 2026-11-07 this
    //     handler took a Line-F instead, from a reading of the dead
    //     `#if 0` block of that same function — no source ever said so.
    // [F] No manual at hand states what a 68882 or a 68040 does with bit
    //     5. Settled by: FBcc $F2A1 (predicate $21) on real hardware.
    // Gate: sst68040 (fpuraw cells); fuzz seeds 808, 809 on the 68030.

    fpu.state = 1;                          // [D] @ Hatari e77819f7: maybe_idle_state (fpp.c:2195)

    int cc = fpuCondEval<C>(opcode & 0x3f);
    if (cc < 0) { FINALIZE return; }        // enabled BSUN trap taken

    if (cc) {

        u32 oldpc = reg.pc;                 // address of the displacement
        u32 disp = queue.irc;

        if constexpr (S == Long) {

            readExt<C>();
            disp = disp << 16 | queue.irc;
        }

        u32 newpc = U32_ADD(oldpc, SEXT<S>(disp));

        // POM68K O4 slice 4 pattern: 68030 odd instruction-flow target —
        // vector 3 with a format $B frame (see execBcc)
        if constexpr (C == Core::C68020) {
            if (cpuModel == Model::M68030 && (newpc & 1)) {
                mmuLogReset();
                if constexpr (S == Word) mmuLogExtWord(u16(disp));
                if constexpr (S == Long) mmuLogExtWord(disp);
                execAddressError030<C>(newpc, reg.pc0);
                FINALIZE
                return;
            }
        }

        reg.pc = newpc;
        fullPrefetch<C, POLL>();

    } else {

        // Fall through to the next instruction
        readExt<C>();
        if constexpr (S == Long) readExt<C>();
        prefetch<C, POLL>();
    }

    CYCLES_68020(cc ? FPU_CK_FBCC_T : FPU_CK_FBCC_N)    // [B] MC68881UM-1989 Table 8-7.

    FINALIZE
}

template <Core C, Instr I, Mode M, Size S> void
Moira::execFDbcc(u16 opcode)
{
    AVAILABILITY(Core::C68020)

    // [B] MC68881UM-1989 §4.6 (FDBcc): decrement/branch.
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // FDBcc (MC68881UM-1989 § 4.6 (FDBcc); WinUAE fpuop_dbcc, fpp.c:2210-2250)
    // POM68K Q4: FPU-less 040 (68LC040/68EC040) — the shape's words are
    // consumed, then vector 11 / format $4 (see execFpuDisabled040)
    if (cpuModel >= Model::M68EC040 && !hasFPU()) {
        u16 cw = u16(readI<C, Word>());     // condition word
        u16 dw = u16(readI<C, Word>());     // displacement word
        // fpuop_dbcc quirk: fault ea = (extra << 16) | (disp & $FFFF)
        execFpuDisabled040<C>(u32(cw) << 16 | dw);
        FINALIZE
        return;
    }

    if (fpuCheckPending<C>()) return;

    u16 ext = u16(readI<C, Word>());        // condition word

    fpu.state = 1;

    int cc = fpuCondEval<C>(ext & 0x3f);
    if (cc < 0) { FINALIZE return; }

    int ck = FPU_CK_FDBCC_TRUE;             // [B] MC68881UM-1989 Table 8-7: true predicate.

    if (!cc) {

        int dn = _____________xxx(opcode);
        u32 newpc = U32_ADD(reg.pc, (i16)queue.irc);

        bool takeBranch = readD<Word>(dn) != 0;
        ck = takeBranch ? FPU_CK_FDBCC_TAKEN : FPU_CK_FDBCC_EXP;

        // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
        // Decrement before the branch decision (fpp.c:2241-2243)
        writeD<Word>(dn, U32_SUB(readD<Word>(dn), 1));

        if (takeBranch) {

            // POM68K O4 slice 4 pattern (see execDbcc): odd target after
            // the counter update, stacked PC = the odd target
            if constexpr (C == Core::C68020) {
                if (cpuModel == Model::M68030 && (newpc & 1)) {
                    mmuLogReset();
                    mmuLogExtWord(u16(queue.irc));
                    execAddressError030<C>(newpc, newpc);
                    FINALIZE
                    return;
                }
            }

            reg.pc = newpc;
            fullPrefetch<C, POLL>();

        } else {

            reg.pc += 2;
            fullPrefetch<C, POLL>();
        }

    } else {

        reg.pc += 2;
        fullPrefetch<C, POLL>();
    }

    CYCLES_68020(ck)

    FINALIZE
}

template <Core C, Instr I, Mode M, Size S> void
Moira::execFScc(u16 opcode)
{
    AVAILABILITY(Core::C68020)

    // [B] MC68881UM-1989 §4.6 (FScc): set condition.
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // FScc (MC68881UM-1989 § 4.6 (FScc); WinUAE fpuop_scc, fpp.c:2252-2311). EA
    // extension words are consumed before the predicate is evaluated;
    // the (An)+/-(An) adjustment happens after (fpp.c:2273-2308).
    // POM68K Q4: FPU-less 040 (68LC040/68EC040) — the shape's words are
    // consumed, then vector 11 / format $4 (see execFpuDisabled040)
    if (cpuModel >= Model::M68EC040 && !hasFPU()) {
        (void)readI<C, Word>();             // condition word
        u32 ea40 = 0;                       // get_fp_ad conventions
        int n40 = _____________xxx(opcode);
        if constexpr (M == Mode::AI || M == Mode::PI) ea40 = readA(n40);
        // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
        // fpuop_scc pre-adjusts -(An) by the byte size (fpp.c:2290-2293)
        if constexpr (M == Mode::PD) ea40 = readA(n40) - (n40 == 7 ? 2 : 1);
        if constexpr (M == Mode::DI || M == Mode::IX ||
                      M == Mode::AW || M == Mode::AL) {
            ea40 = computeEA<C, M, Byte>(n40);
        }
        execFpuDisabled040<C>(ea40);
        FINALIZE
        return;
    }

    if (fpuCheckPending<C>()) return;

    u16 ext = u16(readI<C, Word>());
    int n = _____________xxx(opcode);

    u32 ea = 0;
    if constexpr (M == Mode::DI || M == Mode::IX || M == Mode::AW || M == Mode::AL) {
        ea = computeEA<C, M, Byte>(n);
    } else if constexpr (M == Mode::AI || M == Mode::PI) {
        ea = readA(n);
    } else if constexpr (M == Mode::PD) {
        ea = readA(n) - (n == 7 ? 2 : 1);
    }

    fpu.state = 1;

    int cc = fpuCondEval<C>(ext & 0x3f);
    if (cc < 0) { FINALIZE return; }

    if constexpr (M == Mode::DN) {

        writeD<Byte>(n, cc ? 0xff : 0x00);

    } else {

        // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
        // -(An) arms a plain fixup before the register update (WinUAE
        // fpuop_scc, fpp.c:2299-2304; (An)+ updates after the write and
        // arms nothing)
        if constexpr (M == Mode::PD) { fpuArmFixup(n); writeA(n, ea); }
        // [D] WinUAE fpp.c fpuop_scc @ Hatari e77819f7: the byte goes out as
        // an unsigned int, so a faulting 68030 store stacks wb3_data $000000FF
        // where the integer core's signed operands stack $FFFFFFFF
        // ("fpu/identity seed=811 #1003"; POM68K_VENDOR.md row 83).
        writeM<C, M, Byte, MMU_RAWBUF>(ea, cc ? 0xff : 0x00);
        if constexpr (M == Mode::PI) writeA(n, ea + (n == 7 ? 2 : 1));
    }

    prefetch<C, POLL>();

    // [B] MC68881UM-1989 Table 8-7: destination class.
    // Table 8-7 by destination class ((An)± row includes the update)
    CYCLES_68020(M == Mode::DN ? FPU_CK_FSCC_DN :
                 (M == Mode::PI || M == Mode::PD) ? FPU_CK_FSCC_PIPD
                                                  : FPU_CK_FSCC_MEM)

    FINALIZE
}

template <Core C, Instr I, Mode M, Size S> void
Moira::execFTrapcc(u16 opcode)
{
    AVAILABILITY(Core::C68020)

    // [B] MC68881UM-1989 §4.6 (FTRAPcc): conditional trap.
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // FTRAPcc (MC68881UM-1989 § 4.6 (FTRAPcc); WinUAE fpuop_trapcc, fpp.c:2313-2340).
    // Takes the TRAPcc exception (vector 7, format $2 frame with the
    // next-instruction PC — same as the integer TRAPcc family).
    // POM68K Q4: FPU-less 040 (68LC040/68EC040) — the shape's words are
    // consumed, then vector 11 / format $4 (see execFpuDisabled040)
    if (cpuModel >= Model::M68EC040 && !hasFPU()) {
        (void)readI<C, Word>();             // condition word
        if ((opcode & 0b111) == 0b010) (void)readI<C, Word>();
        if ((opcode & 0b111) == 0b011) (void)readI<C, Long>();
        execFpuDisabled040<C>(0);           // fpuop_trapcc: fault ea = 0
        FINALIZE
        return;
    }

    if (fpuCheckPending<C>()) return;

    u16 ext = u16(readI<C, Word>());

    int opd = 0;                            // 0 = none, 1 = .W, 2 = .L
    switch (opcode & 0b111) {

        case 0b010: (void)readI<C, Word>(); opd = 1; break;
        case 0b011: (void)readI<C, Long>(); opd = 2; break;
    }

    fpu.state = 1;

    int cc = fpuCondEval<C>(ext & 0x3f);
    if (cc < 0) { FINALIZE return; }

    if (cc) {

        execException<C>(M68kException::TRAPV);
        CYCLES_68020(FPU_CK_FTRAP_T[opd])   // [B] MC68881UM-1989 Table 8-7: taken trap.
        FINALIZE
        return;
    }

    prefetch<C, POLL>();

    CYCLES_68020(FPU_CK_FTRAP_N[opd])       // [B] MC68881UM-1989 Table 8-7: untaken trap.

    FINALIZE
}

template <Core C, Instr I, Mode M, Size S> void
Moira::execFNop(u16 opcode)
{
    // FNOP is encoded as FBF.W with zero displacement and dispatched
    // through execFBcc (the jump table never points here; kept for
    // completeness)
    execFBcc<C, Instr::FBcc, M, Word>(opcode);
}

template <Core C, Instr I, Mode M, Size S> void
Moira::execFSave(u16 opcode)
{
    AVAILABILITY(Core::C68020)

    // [B] MC68881UM-1989 §6.4.2: frame sizes; private/omitted payload below.
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // FSAVE (MC68881UM-1989 § 6.4.2; WinUAE fpuop_save, fpp.c:2374-2589, 6888x
    // branch). Supervisor only. Writes a 4-byte NULL frame when the FPU
    // never left the reset state, else the 68882 $3C-byte IDLE frame
    // (version $1F, length $38). WinUAE's 68030-MMU build skips writing
    // the eight 68882-internal longs "to save MMU state space"
    // (fpp.c:2541-2543) — the primary oracle runs that path, so it is
    // replicated when cpuModel == M68030.
    //
    // If an interrupt stopped a long 6888x operation at a protocol
    // checkpoint, serialize its private completion image as the documented
    // $B4/$D4 BUSY frame. The opaque payload starts with a POM2 marker so
    // FRESTORE can resume it deterministically; real hardware frames remain
    // accepted as opaque compatibility input.
    SUPERVISOR_MODE_ONLY

    int n = _____________xxx(opcode);
    u32 ad;

    if constexpr (M == Mode::AI || M == Mode::PD) {
        ad = readA(n);
    } else {
        ad = computeEA<C, M, Long>(n);
    }

    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // POM68K Q4: FPU-less 040 — privilege check and EA computation first
    // (WinUAE fpuop_save: get_fp_ad before fault_if_no_fpu), then the
    // format $4 frame with the computed address
    if (cpuModel >= Model::M68EC040 && !hasFPU()) {
        execFpuDisabled040<C>(ad);
        FINALIZE
        return;
    }

    if (fpuModel == FPUModel::M68040) {
        // Revision-$41 integrated-FPU frames. IDLE/NULL is four bytes;
        // exceptional states are the $34-byte UNIMP and $64-byte BUSY
        // layouts consumed by Motorola FPSP.
        int frameSize = fpu.expState == 2 ? 0x64 : (fpu.expState == 1 ? 0x34 : 4);
        const u32 frameId = fpu.expState == 2 ? 0x41600000u :
                            fpu.expState == 1 ? 0x41300000u :
                            fpu.state == 0 ? 0u : 0x41000000u;
        if constexpr (M == Mode::PD) ad -= frameSize;
        const u32 adp = ad;

        writeM<C, M, Long>(ad, frameId); ad += 4;
        if (fpu.expState == 2) {
            writeM<C, M, Long>(ad, 0); ad += 4;
            writeM<C, M, Long>(ad, 0); ad += 4;          // CU_SAVEPC
            writeM<C, M, Long>(ad, 0); ad += 4;
            writeM<C, M, Long>(ad, 0); ad += 4;
            writeM<C, M, Long>(ad, 0); ad += 4;
            for (int i = 0; i < 3; i++) {
                writeM<C, M, Long>(ad, fpu.wbt[i]); ad += 4;
            }
            writeM<C, M, Long>(ad, 0); ad += 4;
            writeM<C, M, Long>(ad, fpu.fpiarCu); ad += 4;
            writeM<C, M, Long>(ad, 0); ad += 4;
            writeM<C, M, Long>(ad, 0); ad += 4;
        }
        if (fpu.expState != 0) {
            writeM<C, M, Long>(ad, u32(fpu.cmdreg3b) << 16); ad += 4;
            writeM<C, M, Long>(ad, 0); ad += 4;
            writeM<C, M, Long>(ad, u32(fpu.stag) << 29 |
                u32(fpu.wbtm66) << 26 | u32(fpu.grs) << 23); ad += 4;
            writeM<C, M, Long>(ad, u32(fpu.cmdreg1b) << 16); ad += 4;
            writeM<C, M, Long>(ad, u32(fpu.dtag) << 29 |
                u32(fpu.wbte15) << 20); ad += 4;
            writeM<C, M, Long>(ad, u32(fpu.e1) << 26 |
                u32(fpu.e3) << 25 | u32(fpu.t) << 20); ad += 4;
            for (int i = 0; i < 3; i++) {
                writeM<C, M, Long>(ad, fpu.fpt[i]); ad += 4;
            }
            for (int i = 0; i < 3; i++) {
                writeM<C, M, Long>(ad, fpu.et[i]); ad += 4;
            }
        }

        if constexpr (M == Mode::PD) writeA(n, adp);
        fpu.expState = 0;
        fpu.expPend = 0;
        prefetch<C, POLL>();
        CYCLES_68020(frameSize == 4 ? FPU_CK_FSAVE_NULL : FPU_CK_FSAVE_IDLE2)
        FINALIZE
        return;
    }

    fpuCompleteBusy(false);

    const int fpuVersion = 0x1f;                        // [D] @ Hatari e77819f7: fpp.c:1438-1441
    const bool busy = fpu.busy;
    int frameSize = busy ? (fpuModel == FPUModel::M68882 ? 0xd8 : 0xb8)
                         : (fpuModel == FPUModel::M68882 ? 0x3c : 0x1c);
    u32 frameId = busy
        ? (u32(fpuVersion) << 24) | u32(frameSize - 4) << 16
        : fpu.state == 0
            ? u32(frameSize - 4) << 16
            : (u32(fpuVersion) << 24) | (u32(frameSize - 4) << 16);

    u32 biuFlags = 0x540effff;                          // [D] @ Hatari e77819f7: fpp.c:2519
    biuFlags |= fpu.expState ? 0x20000000 : 0x08000000;

    fpu.expPend = 0;                                    // [D] @ Hatari e77819f7: fpp.c:2523
    if (!busy && fpu.state == 0) frameSize = 4;         // NULL frame

    if constexpr (M == Mode::PD) ad -= frameSize;
    u32 adp = ad;

    writeM<C, M, Long>(ad, frameId);
    ad += 4;

    if (busy) {
        // The chip's internal bytes are deliberately opaque to software.
        // Keep a deterministic POM2 image so our FRESTORE can resume the
        // suspended SoftFloat operation while preserving the architectural
        // $1F/$D4 (or $B4) frame size and transfer order.
        const u32 remain = fpu.busyUntil > clock
            ? u32(fpu.busyUntil - clock) : 0;
        for (int off = 4; off < frameSize; off += 4) {
            u32 v = 0;
            if (off == 4) v = 0x504f4d32;               // "POM2"
            else if (off == 8) v = remain;
            else if (off == 12) v = u32(fpu.busyOpcode) << 16 | fpu.busyExt;
            else if (off == 16) v = fpu.busyFpsr;
            else if (off >= 24 && off < 24 + 8 * 12) {
                const int r = (off - 24) / 12;
                const int w = ((off - 24) % 12) / 4;
                if (w == 0) v = u32(fpu.busyFp[r].high) << 16;
                else if (w == 1) v = u32(fpu.busyFp[r].low >> 32);
                else v = u32(fpu.busyFp[r].low);
            }
            writeM<C, M, Long>(ad, v); ad += 4;
        }
    } else if (fpu.state != 0) {   // IDLE frame

        writeM<C, M, Long>(ad, fpu.fsaveCcr);           // command/condition
        ad += 4;

        if (fpuModel == FPUModel::M68882) {
            if (cpuModel == Model::M68030) {
                ad += 8 * 4;                            // internal regs untouched
            } else {
                for (int i = 0; i < 8; i++) {           // [D] @ Hatari e77819f7: zeroed (fpp.c:2565-2570)
                    writeM<C, M, Long>(ad, 0);
                    ad += 4;
                }
            }
        }

        writeM<C, M, Long>(ad, fpu.fsaveEo[0]); ad += 4;    // exceptional operand
        writeM<C, M, Long>(ad, fpu.fsaveEo[1]); ad += 4;
        writeM<C, M, Long>(ad, fpu.fsaveEo[2]); ad += 4;
        writeM<C, M, Long>(ad, 0x00000000); ad += 4;        // operand register
        writeM<C, M, Long>(ad, biuFlags); ad += 4;          // BIU flags
    }

    if constexpr (M == Mode::PD) writeA(n, adp);

    fpu.expState = 0;
    fpu.expPend = 0;
    fpu.busy = false;                              // FSAVE leaves FPCP idle

    prefetch<C, POLL>();

    // [B] MC68881UM-1989 Table 8-8: FSAVE.
    // Table 8-8 (FSAVE row) by emitted frame
    CYCLES_68020(frameSize == 4 ? FPU_CK_FSAVE_NULL :
                 frameSize == 0xd8 ? FPU_CK_FREST_BUSY2 :
                 frameSize == 0xb8 ? FPU_CK_FREST_BUSY1 :
                 frameSize == 0x3c ? FPU_CK_FSAVE_IDLE2 : FPU_CK_FSAVE_IDLE1)

    FINALIZE
}

template <Core C, Instr I, Mode M, Size S> void
Moira::execFRestore(u16 opcode)
{
    AVAILABILITY(Core::C68020)

    // [B] MC68881UM-1989 §6.4.2: frame formats.
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // FRESTORE (MC68881UM-1989 § 6.4.2; WinUAE fpuop_restore, fpp.c:2593-2812).
    // Supervisor only. Frame acceptance mirrors the 6888x branch
    // (fpp.c:2755-2807) exactly, oracle-verified:
    //
    //   version $00           -> NULL: FPU reset (fpu_null, fpp.c:2796)
    //   version $1F, size $18 -> 68881 IDLE: reload micro-state
    //   version $1F, size $38 -> 68882 IDLE: reload micro-state
    //   version $1F, size $B4 -> 68881 BUSY: resume POM2 frame; accept opaque
    //   version $1F, size $D4 -> 68882 BUSY: resume POM2 frame; accept opaque
    //   version $1F, other    -> format error (vector 14, fpp.c:2791-2794)
    //   version $41           -> WinUAE's 68040 retry hack, see below
    //   anything else         -> format error (vector 14, fpp.c:2804-2806)
    //
    // Note the version byte of BOTH 68881 and 68882 frames is $1F
    // (get_fpu_version, fpp.c:1432-1449) — MC68881UM-1989 § 6.4.2 defines the version field.
    SUPERVISOR_MODE_ONLY

    int n = _____________xxx(opcode);
    u32 ad;

    if constexpr (M == Mode::AI || M == Mode::PI) {
        ad = readA(n);
    } else {
        ad = computeEA<C, M, Long>(n);
    }

    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // POM68K Q4: FPU-less 040 — same convention as FSAVE (WinUAE
    // fpuop_restore: get_fp_ad before fault_if_no_fpu)
    if (cpuModel >= Model::M68EC040 && !hasFPU()) {
        execFpuDisabled040<C>(ad);
        FINALIZE
        return;
    }

    const u32 adOrig = ad;
    const u32 d = readM<C, M, Long>(ad);
    const u32 frameVersion = d >> 24;

    ad = adOrig + 4;

    int ck = FPU_CK_FREST_NULL;             // [B] MC68881UM-1989 Table 8-8: FRESTORE.

    if (frameVersion == 0x1f && fpuModel != FPUModel::M68040) { // 6888x frame

        u32 frameSize = (d >> 16) & 0xff;
        fpu.state = 1;

        if (frameSize == 0x18 || frameSize == 0x38) {   // 68881/68882 IDLE

            fpu.fsaveCcr = readM<C, M, Long>(ad);
            ad += 4;
            ad += frameSize - 24;                       // internal registers
            fpu.fsaveEo[0] = readM<C, M, Long>(ad); ad += 4;
            fpu.fsaveEo[1] = readM<C, M, Long>(ad); ad += 4;
            fpu.fsaveEo[2] = readM<C, M, Long>(ad); ad += 4;
            ad += 4;                                    // operand register
            u32 biuFlags = readM<C, M, Long>(ad); ad += 4;

            if ((biuFlags & 0x08000000) == 0) {         // [D] @ Hatari e77819f7: fpp.c:2781-2787
                fpu.expState = 2;
                fpu.expPend = fpuVectorFor(fpu.fpsr & fpu.fpcr & 0xff00);
            } else {
                fpu.expState = 0;
                fpu.expPend = 0;
            }
            ck = frameSize == 0x38 ? FPU_CK_FREST_IDLE2 : FPU_CK_FREST_IDLE1;

        } else if (frameSize == 0xB4 || frameSize == 0xD4) {    // BUSY

            const u32 magic = readM<C, M, Long>(adOrig + 4);
            if (magic == 0x504f4d32) {
                const u32 remain = readM<C, M, Long>(adOrig + 8);
                const u32 command = readM<C, M, Long>(adOrig + 12);
                fpu.busyOpcode = u16(command >> 16);
                fpu.busyExt = u16(command);
                fpu.busyFpsr = readM<C, M, Long>(adOrig + 16);
                for (int r = 0; r < 8; r++) {
                    const u32 p = adOrig + 24 + u32(12 * r);
                    const u32 w0 = readM<C, M, Long>(p);
                    const u32 w1 = readM<C, M, Long>(p + 4);
                    const u32 w2 = readM<C, M, Long>(p + 8);
                    fpu.busyFp[r] = FpuExtended {
                        u16(w0 >> 16), u64(w1) << 32 | w2 };
                }
                fpu.busy = true;
                // The restore transfer itself precedes resumed execution.
                const int restoreCk = frameSize == 0xD4
                    ? FPU_CK_FREST_BUSY2 : FPU_CK_FREST_BUSY1;
                fpu.busyUntil = clock + restoreCk + remain;
            } else {
                // An opaque hardware frame remains accepted. Its exact
                // microcode state cannot be interpreted, matching the old
                // compatibility behavior, but it is never rejected.
                fpu.busy = false;
            }
            ad += frameSize;                    // [D] @ Hatari e77819f7: skipped (fpp.c:2788-2790)
            ck = frameSize == 0xD4 ? FPU_CK_FREST_BUSY2 : FPU_CK_FREST_BUSY1;

        } else {

            execFRestoreFormatError<C>();
            CYCLES_68020(FPU_CK_FREST_NULL)
            FINALIZE
            return;
        }

    } else if (frameVersion == 0x00) {      // NULL frame — FPU reset

        fpuResetState();

    } else if (frameVersion == 0x41 && fpuModel == FPUModel::M68040) {

        // [B] MC68881UM-1989 §6.2.8 and §6.4.2: invalid saved-frame version.
        // The 68040's own frame (get_fpu_version(68040) == $41, fpp.c).
        //
        // POM68K, ruling D23b (settled 2026-11-07): a 68882 given this frame
        // takes the format error below. Until then it accepted it, after
        // WinUAE's "horrible hack to support on the fly 6888x <> 68040 FPU
        // switching" (fpp.c:2799-2802) — which exists only in the oracle's
        // PERMISSIVE setting (fpu_no_unimplemented == false), the one the
        // four pinned sst68030 vectors holding the acceptance were generated
        // in (fpu_tt #166, fpu_off #28/#83, fpu_identity #75, now listed in
        // tests/sst68030.known). The strict oracle takes the format error.
        // MC68881UM-1989 § 6.4.2: no such frame version exists for the
        //     coprocessor — a format error.
        // [D] WinUAE fpp.c fpuop_restore @ Hatari e77819f7, strict setting.
        // Gate: sst68030_fpuraw and sst68030_fpu2 (their D23b blocks are
        // gone), sst68030_solo (regenerated strict), the LC 475/575/580 and
        // Quadra 605 etalons (68LC040 + software 68882: a system that
        // believes it has a 68040 FPU restores only frames it saved).
        u32 frameSize = (d >> 16) & 0xff;

        if (frameSize == 0x00) {                        // 68040 IDLE

            fpu.state = 1;                              // [D] @ Hatari e77819f7: fpp.c:2732-2734
            fpu.expState = 0;                           // expPend untouched

        } else if (frameSize == 0x30 || frameSize == 0x28) {    // UNIMP

            ad += frameSize;                            // [D] @ Hatari e77819f7: fpp.c:2727-2730

        } else if (frameSize == 0x60) {                 // 68040 BUSY

            // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
            // Resume a planted BUSY frame when CU_SAVEPC is $FE, matching
            // WinUAE fpp.c:2668-2725. The FPT/ET operands are raw extended
            // values and CMDREG1B selects the destination and operation.
            const u32 cuSavePc = readM<C, M, Long>(adOrig + 8) >> 24;
            const bool et15 = (readM<C, M, Long>(adOrig + 60) & 0x10000000u) != 0;
            const u16 cmd = u16(readM<C, M, Long>(adOrig + 64) >> 16);
            const bool fpte15 = (readM<C, M, Long>(adOrig + 68) & 0x10000000u) != 0;
            const u32 fpt0 = readM<C, M, Long>(adOrig + 76);
            const u32 fpt1 = readM<C, M, Long>(adOrig + 80);
            const u32 fpt2 = readM<C, M, Long>(adOrig + 84);
            const u32 et0 = readM<C, M, Long>(adOrig + 88);
            const u32 et1 = readM<C, M, Long>(adOrig + 92);
            const u32 et2 = readM<C, M, Long>(adOrig + 96);
            ad = adOrig + 4 + frameSize;

            const u32 opclass = (cmd >> 13) & 7;
            if (cuSavePc == 0xfe && (opclass == 0 || opclass == 2)) {
                FpuExtended dst { u16(fpt0 >> 16), u64(fpt1) << 32 | fpt2 };
                FpuExtended src { u16(et0 >> 16), u64(et1) << 32 | et2 };
                if (fpte15)
                    dst = fpuFromFx80(floatx80_denormalize(fpuToFx80(dst), true));
                if (et15)
                    src = fpuFromFx80(floatx80_denormalize(fpuToFx80(src), true));
                fpuSetMode();
                fpuClearStatus();
                const bool store = fpuArithmetic(src, dst, cmd);
                if (store) fpu.fp[(cmd >> 7) & 7] = dst;
                // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
                // Whatever the resumed operation raises stays posted: the
                // oracle ignores the verdict here (fpp.c fpuop_restore).
                (void)fpuCheckArithException(src, opcode, cmd, fpu.ea);
            }
            fpu.state = 1;
            fpu.expState = 0;
            fpu.expPend = 0;

        } else {

            execFRestoreFormatError<C>();               // [D] @ Hatari e77819f7: fpp.c:2735-2739
            CYCLES_68020(FPU_CK_FREST_NULL)
            FINALIZE
            return;
        }

    } else {

        execFRestoreFormatError<C>();
        CYCLES_68020(FPU_CK_FREST_NULL)
        FINALIZE
        return;
    }

    if constexpr (M == Mode::PI) writeA(n, ad);

    prefetch<C, POLL>();

    CYCLES_68020(ck)

    FINALIZE
}

template <Core C, Instr I, Mode M, Size S> void
Moira::execFGen(u16 opcode)
{
    AVAILABILITY(Core::C68020)

    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // Router for the general F-line window (WinUAE fpuop_arithmetic,
    // fpp.c:3168-3617). The extension word is peeked here and consumed by
    // the sub-handlers.
    u16 ext = queue.irc;

    // POM68K: on the 68040 family the EA fields $3D-$3F are not FPU
    // opcodes at all — a plain Line-F ahead of every FPU consideration
    // (pending exception, status byte, "no FPU" frame).
    // [D] WinUAE table68k @ Hatari e77819f7 lists no such EA for FPP, so
    //     the opcode never reaches fpp.c. The 68020/68030 keep their
    //     historic path: there the coprocessor, not the CPU, decides
    //     whether the EA is ever evaluated.
    // Gate: sst68040 (fline and fpuraw cells).
    if constexpr (M == Mode::IP) {
        if (cpuModel >= Model::M68EC040) {
            execLineF<C, I, M, S>(opcode);
            return;
        }
    }

    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // An as EA is only decodable for FPx-to-FPx and control-register
    // moves. FMOVEM (opclasses 110/111) refuses it here; an An source or
    // destination (opclasses 010/011) is refused by the operand helpers,
    // AFTER the pending-exception check and the status-byte clear the
    // oracle performs first (fpp.c fpuop_arithmetic2: fpsr_clear_status
    // precedes get_fp_value / put_fp_value), and on the 68040 after the
    // unimplemented-instruction trap. On a FPU-less 040 the no-FPU hook
    // below decides first: get_fp_value's An case reaches
    // fault_if_unimplemented_680x0 -> format $4 (fresh-seed arbitrated
    // 2026-07-18), not Line-F.
    if constexpr (M == Mode::AN) {
        if ((ext & 0xC000) == 0xC000 &&
            !(cpuModel >= Model::M68EC040 && !hasFPU())) {
            execLineF<C, I, M, S>(opcode);
            return;
        }
    }

    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // POM68K Q4: FPU-less 040 — mirror WinUAE fpuop_arithmetic2 with
    // fpu_model = 0 per opclass (fpp.c:3168-3600): the extension word is
    // always consumed; the EA (and its extension words) are computed
    // exactly where WinUAE computes them before fault_if_no_fpu, and Dn/
    // An/#imm FMOVEM shapes stay Line-F (get_fp_ad failure -> fpu_noinst).
    if (cpuModel >= Model::M68EC040 && !hasFPU()) {

        u16 x = u16(readI<C, Word>());
        int opclass = (x >> 13) & 7;
        int n = _____________xxx(opcode);
        int size = (x >> 10) & 7;

        // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
        // get_fp_value/put_fp_value operand sizes (fpp.c sz1/sz2)
        static constexpr int sz1[8] = { 4, 4, 12, 12, 2, 8, 1, 0 };

        // POM68K: what is not an instruction at all is refused BEFORE the
        // "no FPU" frame is considered — a plain format $0 Line-F, or the
        // illegal-instruction vector for opmodes $78-$7F.
        // [D] WinUAE fpp.c fpuop_arithmetic2 @ Hatari e77819f7: opclass 001
        //     falls to fpu_noinst; fault_if_nonexisting_opmode runs ahead
        //     of get_fp_value (hence of fault_if_no_fpu) and is not
        //     conditioned on an FPU being present; put_fp_value2 returns
        //     "no instruction" for An, for X/P/D to Dn and for PC-relative
        //     or immediate destinations before it tests the FPU.
        // [F] No manual states the order on a 68LC040. Settled by: the
        //     stacked frame of $F200 $0005 on a real 68LC040.
        // Gate: sst68040 (fline cells).
        if (opclass == 1) {
            execLineF<C, I, M, S>(opcode);
            return;
        }
        if ((opclass == 0 || opclass == 2) && (x & 0xFC00) != 0x5C00) {
            if ((x & 0x7f) >= 0x78) {
                execFpuIllegalOpmode040<C>();
                return;
            }
            if (fpuIsNonexistent040(x)) {
                execLineF<C, I, M, S>(opcode);
                return;
            }
            // A packed source in Dn is no instruction either (get_fp_value
            // case 0: `case 3: ... return 0` ahead of fault_if_no_fpu).
            if constexpr (M == Mode::DN) {
                if (opclass == 2 && size == 3) {
                    execLineF<C, I, M, S>(opcode);
                    return;
                }
            }
        }
        if (opclass == 3) {
            bool noInstruction = false;
            if constexpr (M == Mode::DN) {
                noInstruction = size == 2 || size == 3 || size == 5 || size == 7;
            } else if constexpr (M == Mode::AN || M == Mode::DIPC ||
                                 M == Mode::IXPC || M == Mode::IM) {
                noInstruction = true;
            }
            if (noInstruction) {
                execLineF<C, I, M, S>(opcode);
                return;
            }
        }

        if (opclass == 6 || opclass == 7) {

            // FMOVEM FPn<>mem — get_fp_ad fails on Dn/An/#imm -> Line-F
            if constexpr (M == Mode::DN || M == Mode::AN || M == Mode::IM) {
                execLineF<C, I, M, S>(opcode);
                return;
            } else {
                u32 ea = 0;
                if constexpr (M == Mode::AI || M == Mode::PI || M == Mode::PD) {
                    ea = readA(n);
                } else {
                    ea = computeEA<C, M, Long>(n);
                }
                execFpuDisabled040<C>(ea);
                return;
            }

        } else if (opclass == 4 || opclass == 5) {

            // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
            // FMOVE(M) control registers: Dn/An forms fault with ea = 0;
            // the #imm form consumes one long per selected register first
            // (fpp.c:3288-3296); memory forms compute the EA
            if constexpr (M == Mode::IM) {
                // An immediate destination is no instruction; with no
                // register selected the source is one long, for FPIAR
                // ([D] WinUAE fpp.c "No control register bits set: FPIAR"
                // and "immediate as destination" @ Hatari e77819f7).
                if (x & 0x2000) {
                    execLineF<C, I, M, S>(opcode);
                    return;
                }
                const u16 sel = (x & 0x1C00) ? u16(x & 0x1C00) : u16(0x0400);
                if (sel & 0x1000) (void)readI<C, Long>();
                if (sel & 0x0800) (void)readI<C, Long>();
                if (sel & 0x0400) (void)readI<C, Long>();
                execFpuDisabled040<C>(0);
                return;
            } else if constexpr (M == Mode::DN || M == Mode::AN) {
                execFpuDisabled040<C>(0);
                return;
            } else {
                u32 ea = 0;
                if constexpr (M == Mode::AI || M == Mode::PI || M == Mode::PD) {
                    ea = readA(n);
                } else {
                    ea = computeEA<C, M, Long>(n);
                }
                execFpuDisabled040<C>(ea);
                return;
            }

        } else {

            // opclass 0-3: register/memory operands through get_fp_value /
            // put_fp_value — FPx-to-FPx, FMOVECR and Dn/#imm fault with
            // ea = 0 (immediate words consumed); the memory modes compute
            // the operand address with the (An)± conventions (register
            // restored by the fault, so only the EA value shows)
            if (!(x & 0x4000) || (x & 0xFC00) == 0x5C00) {
                execFpuDisabled040<C>(0);
                return;
            }
            if constexpr (M == Mode::DN || M == Mode::AN) {
                execFpuDisabled040<C>(0);
                return;
            } else if constexpr (M == Mode::IM) {
                switch (size) {
                    case 0: case 1: (void)readI<C, Long>(); break;
                    case 2: case 3: (void)readI<C, Long>();
                                    (void)readI<C, Long>();
                                    (void)readI<C, Long>(); break;
                    case 5: (void)readI<C, Long>();
                            (void)readI<C, Long>(); break;
                    default: (void)readI<C, Word>(); break;
                }
                execFpuDisabled040<C>(0);
                return;
            } else if constexpr (M == Mode::AI || M == Mode::PI) {
                execFpuDisabled040<C>(readA(n));
                return;
            } else if constexpr (M == Mode::PD) {
                int dec = (n == 7 && sz1[size] == 1) ? 2 : sz1[size];
                execFpuDisabled040<C>(readA(n) - dec);
                return;
            } else {
                execFpuDisabled040<C>(computeEA<C, M, Long>(n));
                return;
            }
        }
    }

    if (fpuCheckPending<C>()) return;       // [D] @ Hatari e77819f7: pre-instruction (fpp.c:3185)

    fpu.state = 1;                          // [D] @ Hatari e77819f7: fpp.c:3607

    switch (ext >> 13 & 7) {

        case 0b000:
        case 0b010:

            if ((ext & 0xFC00) == 0x5C00) {
                execFMovecr<C, Instr::FMOVECR, M, S>(opcode);
            } else {
                execFGeneric<C, I, M, S>(opcode);
            }
            return;

        case 0b011:

            execFMove<C, Instr::FMOVE, M, S>(opcode);
            return;

        case 0b100:
        case 0b101:
        case 0b110:
        case 0b111:

            execFMovem<C, Instr::FMOVEM, M, S>(opcode);
            return;

        default:    // [D] @ Hatari e77819f7: opclass 001: undefined — F-line (fpp.c:3599-3602)

            execLineF<C, I, M, S>(opcode);
            return;
    }
}

template <Core C, Instr I, Mode M, Size S> void
Moira::execFMovecr(u16 opcode)
{
    AVAILABILITY(Core::C68020)

    // [B] MC68881UM-1989 §4.7.1.3 (FMOVECR): command encoding.
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // FMOVECR (MC68881UM-1989 § 4.7.1.3; WinUAE fpp.c:3527-3541). ROM offsets
    // $40-$7F take an F-line on 6888x.
    u16 ext = u16(readI<C, Word>());
    int dst = ______xxx_______(ext);

    if (fpuModel == FPUModel::M68040) {
        const FpuExtended src { 0, 0 };
        execFpuUnimplemented040<C>(opcode, ext, src, dst, -1, 0);
        FINALIZE
        return;
    }

    if (ext & 0x40) {

        execLineF<C, I, M, S>(opcode);
        return;
    }

    fpuSetMode();
    fpuClearStatus();

    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // maybe_set_fpiar (fpp.c:764-771): FPIAR updates only when an
    // exception other than BSUN is enabled — a WinUAE (= oracle) economy
    // replicated on purpose
    if (fpuModel == FPUModel::M68040 || (fpu.fpcr & 0x7f00)) fpu.fpiar = reg.pc0;

    FpuExtended v;
    fpuGetConstant(v, ext & 0x7f);
    fpu.fp[dst] = v;

    fpuMakeStatus();
    fpuCheckArithException(v, opcode, ext, 0);

    prefetch<C, POLL>();

    CYCLES_68020(FPU_CK_FMOVECR)    // [B] MC68881UM-1989 Table 8-3.

    FINALIZE
}

template <Core C, Instr I, Mode M, Size S> void
Moira::execFGeneric(u16 opcode)
{
    AVAILABILITY(Core::C68020)

    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // Arithmetic, opclasses 000 (FPx source) and 010 (EA source) — WinUAE
    // fpp.c:3523-3598
    u16 ext = u16(readI<C, Word>());
    int dstReg = ______xxx_______(ext);

    // Non-existing opmodes fault before fetching an operand. The external
    // 6888x rejects all of $40-$7F; the integrated 040 has a sparse map and
    // reserves $78-$7F for the illegal-instruction vector.
    if (fpuModel == FPUModel::M68040) {
        if ((ext & 0x7f) >= 0x78) {
            execFpuIllegalOpmode040<C>();
            return;
        }
        if (fpuIsNonexistent040(ext)) {
            execLineF<C, I, M, S>(opcode);
            return;
        }
    } else if ((ext & 0x7f) >= 0x40) {
        execLineF<C, I, M, S>(opcode);
        return;
    }

    FpuExtended oldFp[8];
    for (int i = 0; i < 8; i++) oldFp[i] = fpu.fp[i];
    const u32 oldFpsr = fpu.fpsr;

    fpuSetMode();
    fpuClearStatus();

    // POM68K: the 68040 latches FPIAR before the operand is fetched, so a
    // faulting source read already shows it. [D] WinUAE fpp.c "68040 and
    // 68060 always set FPIAR" @ Hatari e77819f7, ahead of get_fp_value.
    if (fpuModel == FPUModel::M68040) fpu.fpiar = reg.pc0;

    FpuExtended src;
    u32 ea = 0;

    if (!(ext & 0x4000)) {

        // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
        // FPx-to-FPx (fpp.c:1541-1548)
        src = fpu.fp[___xxx__________(ext)];
        fpuNormalize(src);

    } else {

        const int sourceResult = fpuGetSource<C, M>(opcode, ext, src, ea);
        if (sourceResult == 0) {
            execLineF<C, I, M, S>(opcode);
            return;
        }
        if (sourceResult < 0) {
            FINALIZE
            return;
        }
    }

    if (fpuModel == FPUModel::M68040 || (fpu.fpcr & 0x7f00))
        fpu.fpiar = reg.pc0;

    FpuExtended dst = fpu.fp[dstReg];
    if (fpuIsDyadic(ext)) fpuNormalize(dst);        // [D] @ Hatari e77819f7: fpp.c:3569-3570

    if (fpuModel == FPUModel::M68040 && !fpuIsImplemented040(ext)) {
        const int size = (ext & 0x4000) ? int((ext >> 10) & 7) : -1;
        execFpuUnimplemented040<C>(opcode, ext, src, dstReg, size, ea);
        FINALIZE
        return;
    }

    if (fpuModel == FPUModel::M68040) {
        const int size = (ext & 0x4000) ? int((ext >> 10) & 7) : -1;
        if (size == 3 || size == 7 || fpuNeedsDatatype040(src) ||
            (fpuIsDyadic(ext) && fpuNeedsDatatype040(dst))) {
            execFpuDatatype040<C>(opcode, ext, src, dstReg, size, ea);
            FINALIZE
            return;
        }
    }

    bool store = fpuArithmetic(src, dst, ext);

    const bool immediate = fpuCheckArithException(src, opcode, ext, ea);
    if (immediate && fpuModel == FPUModel::M68040) {
        execFpuException<C>(u16(fpu.expPend), false);
        FINALIZE
        return;
    }
    if (store) fpu.fp[dstReg] = dst;

    if (fpuModel == FPUModel::M68881 || fpuModel == FPUModel::M68882) {
        // Keep the completed image private while the MPU waits on the
        // coprocessor protocol. An interrupt at an IA checkpoint exposes a
        // format-$9 continuation and FSAVE can then suspend this BUSY image.
        for (int i = 0; i < 8; i++) fpu.busyFp[i] = fpu.fp[i];
        fpu.busyFpsr = fpu.fpsr;
        for (int i = 0; i < 8; i++) fpu.fp[i] = oldFp[i];
        fpu.fpsr = oldFpsr;

        fpu.busyIa = reg.pc0;
        prefetch<C, POLL>();
        const int cycles = fpuCk882Op[ext & 0x3f] +
            ((ext & 0x4000) ? fpuCk882Fmt[___xxx__________(ext)] : 0) + cp;
        fpuRunBusy<C>(cycles, opcode, ext);
        FINALIZE
        return;
    }

    prefetch<C, POLL>();

    // [B] MC68881UM-1989 Table 8-3: base costs; format approximation above.
    // Table 8-3: opmode base (FPn-to-FPm) + per-format spread for
    // memory/Dn/#imm sources; EA cycles arrive via cp (see table header)
    CYCLES_68020(fpuCk882Op[ext & 0x3f] +
                 ((ext & 0x4000) ? fpuCk882Fmt[___xxx__________(ext)] : 0))

    FINALIZE
}

template <Core C, Instr I, Mode M, Size S> void
Moira::execFMove(u16 opcode)
{
    AVAILABILITY(Core::C68020)

    // [B] MC68881UM-1989 §7.5.1.3: register-to-external dialog.
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // FMOVE FPx -> EA, opclass 011 (MC68881UM-1989 § 7.5.1.3; WinUAE
    // fpp.c:3190-3205). Condition codes are NOT set by a move out; the
    // exception byte is (OPERR on integer overflow, INEX on rounding).
    u16 ext = u16(readI<C, Word>());

    fpuSetMode();
    fpuClearStatus();

    FpuExtended value = fpu.fp[______xxx_______(ext)];
    u32 ea = 0;

    const int putResult = fpuPutDest<C, M>(opcode, ext, value, ea);
    if (putResult <= 0) {

        if (putResult == 0) execLineF<C, I, M, S>(opcode);
        return;
    }

    fpuMakeStatus();
    if (fpuModel == FPUModel::M68040 || (fpu.fpcr & 0x7f00))
        fpu.fpiar = reg.pc0;

    const int dstSize = (ext >> 10) & 7;
    const u32 integerNonmaskable = fpuModel == FPUModel::M68040 &&
        (dstSize == 0 || dstSize == 4 || dstSize == 6)
        ? (FPSR_SNAN | FPSR_OPERR) : 0;
    if (fpuCheckArithException(value, opcode, ext, ea, integerNonmaskable)) {
        /* 040: immediate; 6888x: maskable pending exception. */
    }
    if (fpu.expPend) {

        // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
        // Mid/post-instruction exception (WinUAE fp_exception_pending(false),
        // fpp.c:3204); the 68882 keeps the vector pending
        u16 vector = u16(fpu.expPend);
        if (fpuModel != FPUModel::M68882) fpu.expPend = 0;
        execFpuException<C>(vector, false);
        FINALIZE
        return;
    }

    prefetch<C, POLL>();

    CYCLES_68020(fpuCk882MoveOut[___xxx__________(ext)])   // [B] MC68881UM-1989 Table 8-3.

    FINALIZE
}

template <Core C, Instr I, Mode M, Size S> void
Moira::execFMovem(u16 opcode)
{
    AVAILABILITY(Core::C68020)

    // [B] MC68881UM-1989 §7.5.1.4 and §7.5.1.5: FMOVEM dialogs.
    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
    // FMOVEM — control registers (opclasses 100/101, MC68881UM-1989 § 7.5.1.4)
    // and FP data registers (opclasses 110/111, § 7.5.1.5). WinUAE
    // fpp.c:3207-3521. Neither form touches the condition codes.
    u16 ext = u16(readI<C, Word>());
    int n = _____________xxx(opcode);
    int cod = xxx_____________(ext);
    int ck = 0;                     // [B] MC68881UM-1989 Table 8-6.

    if (cod == 0b100 || cod == 0b101) {

        //
        // Control register block (FPCR/FPSR/FPIAR any combination)
        //

        const u16 bits = ext & 0x1c00;
        const bool toEa = (ext & 0x2000) != 0;
        const int crn = ((bits >> 12) & 1) + ((bits >> 11) & 1) + ((bits >> 10) & 1);

        ck = FPU_CK_CR_MEM + FPU_CK_CR_PER_REG * (crn ? crn : 1);
        if constexpr (M == Mode::DN || M == Mode::AN)
            ck = toEa ? FPU_CK_CR_TO_RN : FPU_CK_CR_FROM_RN;
        if constexpr (M == Mode::IM)
            ck = FPU_CK_CR_IMM + FPU_CK_CR_PER_REG * (crn ? crn : 1);

        if constexpr (M == Mode::DN) {

            // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
            // Only a single control register transfers with Dn
            // (fpp.c:3215-3229); no bits = FPIAR
            if (bits && bits != 0x1000 && bits != 0x0800 && bits != 0x0400) {
                execLineF<C, I, M, S>(opcode);
                return;
            }
            if (toEa) {
                if (ext & 0x1000) writeD(n, getFPCR());
                if (ext & 0x0800) writeD(n, getFPSR());
                if ((ext & 0x0400) || !bits) writeD(n, fpu.fpiar);
            } else {
                if (ext & 0x1000) setFPCR(readD(n));
                if (ext & 0x0800) setFPSR(readD(n));
                if ((ext & 0x0400) || !bits) fpu.fpiar = readD(n);
            }

        } else if constexpr (M == Mode::AN) {

            // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
            // Only FPIAR transfers with An (fpp.c:3247-3273)
            if (bits && bits != 0x0400) {
                execLineF<C, I, M, S>(opcode);
                return;
            }
            if (toEa) writeA(n, fpu.fpiar);
            else fpu.fpiar = readA(n);

        } else if constexpr (M == Mode::IM) {

            // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
            // #imm as source only; multiple registers allowed on 6888x
            // (fpp.c:3274-3308)
            if (toEa) {
                execLineF<C, I, M, S>(opcode);
                return;
            }
            u16 sel = bits ? bits : 0x0400;
            u32 e0 = 0, e1 = 0, e2 = 0;
            if (sel & 0x1000) e0 = readI<C, Long>();
            if (sel & 0x0800) e1 = readI<C, Long>();
            if (sel & 0x0400) e2 = readI<C, Long>();
            if (sel & 0x1000) setFPCR(e0);
            if (sel & 0x0800) setFPSR(e1);
            if (sel & 0x0400) fpu.fpiar = e2;

        } else if constexpr (M == Mode::IP) {

            execLineF<C, I, M, S>(opcode);
            return;

        } else if constexpr (M == Mode::DIPC || M == Mode::IXPC) {

            // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
            // PC-relative: source only (fpp.c:3320-3324)
            if (toEa) {
                execLineF<C, I, M, S>(opcode);
                return;
            }
            u16 sel = bits ? bits : 0x0400;
            u32 ad = computeEA<C, M, Long>(n);
            if (sel & 0x1000) { setFPCR(readM<C, M, Long>(ad)); ad += 4; }
            if (sel & 0x0800) { setFPSR(readM<C, M, Long>(ad)); ad += 4; }
            if (sel & 0x0400) { fpu.fpiar = readM<C, M, Long>(ad); ad += 4; }

        } else {

            // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
            // Memory EAs (fpp.c:3309-3409). -(An)/(An)+ update after the
            // transfer; -(An) writes/reads ascending from ea - 4*count.
            u16 sel = bits ? bits : 0x0400;
            int count = ((sel >> 12) & 1) + ((sel >> 11) & 1) + ((sel >> 10) & 1);
            int incr = (M == Mode::PD) ? 4 * count : 0;

            u32 ad;
            if constexpr (M == Mode::AI || M == Mode::PI || M == Mode::PD) {
                ad = readA(n);
            } else {
                ad = computeEA<C, M, Long>(n);
            }
            ad -= incr;

            if (toEa) {
                if (sel & 0x1000) { writeM<C, M, Long>(ad, getFPCR()); ad += 4; }
                if (sel & 0x0800) { writeM<C, M, Long>(ad, getFPSR()); ad += 4; }
                if (sel & 0x0400) { writeM<C, M, Long>(ad, fpu.fpiar); ad += 4; }
            } else {
                if (sel & 0x1000) { setFPCR(readM<C, M, Long>(ad)); ad += 4; }
                if (sel & 0x0800) { setFPSR(readM<C, M, Long>(ad)); ad += 4; }
                if (sel & 0x0400) { fpu.fpiar = readM<C, M, Long>(ad); ad += 4; }
            }

            if constexpr (M == Mode::PI) writeA(n, ad);
            if constexpr (M == Mode::PD) writeA(n, ad - incr);
        }

    } else {

        //
        // FP data register block (12 bytes per register, raw bits)
        //

        const bool toEa = cod == 0b111;
        const int mode = ___xx___________(ext);

        // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
        // EA restrictions (fpp.c:3419-3442): registers/immediates never
        // decode; FP->mem excludes (An)+ and PC-relative; mem->FP
        // excludes -(An)
        if constexpr (M == Mode::DN || M == Mode::AN || M == Mode::IM || M == Mode::IP) {
            execLineF<C, I, M, S>(opcode);
            return;
        }

        // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
        // WinUAE computes the EA (get_fp_ad, consuming extension words)
        // before the direction checks
        u32 ad;
        if constexpr (M == Mode::AI || M == Mode::PI || M == Mode::PD) {
            ad = readA(n);
        } else {
            ad = computeEA<C, M, Long>(n);
        }

        if constexpr (M == Mode::DIPC || M == Mode::IXPC) {
            if (toEa) { execLineF<C, I, M, S>(opcode); return; }
        }
        if constexpr (M == Mode::PI) {
            if (toEa) { execLineF<C, I, M, S>(opcode); return; }
        }
        if constexpr (M == Mode::PD) {
            if (!toEa) { execLineF<C, I, M, S>(opcode); return; }
        }

        u32 list = (mode & 1) ? readD((ext >> 4) & 7) & 0xff : ext & 0xff;

        // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
        // 6888x and 040 use different list and word orders. On the 040,
        // memory->FP static/dynamic predecrement-format lists count FP0 up;
        // FP->memory may also write each 96-bit register low-mantissa first
        // (WinUAE fpp.c:3461-3490).
        const int incr = (M == Mode::PD) ? -1 : 1;
        int regdir = (mode == 0 || mode == 1) ? -1 : 1;
        bool reverseWords = false;
        if (fpuModel == FPUModel::M68040) {
            regdir = 1;
            if ((mode == 0 || mode == 1) && (M == Mode::PD || toEa))
                regdir = -1;
            if (list && toEa && incr > 0 && regdir < 0)
                reverseWords = true;
            if (list && toEa && incr < 0 && regdir > 0)
                reverseWords = true;
        }

        // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
        // POM68K O5 (seed-17 solo convergence): 68030 restart bookkeeping,
        // WinUAE fmovem2mem/fmovem2fpp mmu030 paths (fpp.c:2819-2850,
        // 2875-2910) — MOVEM1 in state[1] (plus FMOVEM for memory->FP), a
        // completed-long counter in state[0], and memory->FP parks the
        // first two longs of the register in flight in fmovem_store
        // (stacked in the padding slots of a $B fault frame's access
        // log). The transfers themselves are UNLOGGED — WinUAE uses the
        // non-state x_put_long/x_get_long there — with the pending write
        // value placed in the data buffer manually, so a fault stacks
        // idx == idx_done (only the ext-word consumption is in the log);
        // same pattern as Moira's integer MOVEM (O4 slice 3).
        bool mmu030 = false;
        bool logSave = false;
        if constexpr (C == Core::C68020) {
            if (cpuModel == Model::M68030) [[unlikely]] {
                mmu030 = true;
                mmuState[1] |= toEa ? u16(0x4000) : u16(0x4000 | 0x2000);
                logSave = mmuLogging;
                mmuLogging = false;
            }
        }

        // [B] MC68881UM-1989 Table 8-6: register-list costs.
        // Table 8-6 FPdr rows (n = registers moved; dynamic-list rows
        // add a Dn-read surcharge)
        int nr = 0;
        for (u32 l = list; l & 0xff; l <<= 1) nr += (l >> 7) & 1;
        ck = (toEa ? FPU_CK_FDR_TO_MEM + FPU_CK_FDR_OUT_REG * nr
                   : FPU_CK_FDR_FROM_MEM + FPU_CK_FDR_IN_REG * nr)
           + ((mode & 1) ? FPU_CK_FDR_DYN : 0);

        for (int r = 0; r < 8; r++) {

            int fpn = regdir < 0 ? 7 - r : r;

            if (list & 0x80) {

                if (incr < 0) ad -= 12;

                if (toEa) {
                    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
                    // Raw bits, no conversion (WinUAE fmovem2mem uses
                    // from_exten_fmovem, fpp.c:2859); data buffer set
                    // before each put (fpp.c:2838)
                    u32 w1 = u32(fpu.fp[fpn].high) << 16;
                    u32 w2 = u32(fpu.fp[fpn].low >> 32);
                    u32 w3 = u32(fpu.fp[fpn].low);
                    const u32 first = reverseWords ? w3 : w1;
                    const u32 third = reverseWords ? w1 : w3;
                    if (mmu030) mmuDataBuffer = first;
                    writeM<C, M, Long>(ad, first);
                    if (mmu030) { mmuState[0]++; mmuDataBuffer = w2; }
                    writeM<C, M, Long>(ad + 4, w2);
                    if (mmu030) { mmuState[0]++; mmuDataBuffer = third; }
                    writeM<C, M, Long>(ad + 8, third);
                    if (mmu030) mmuState[0]++;
                } else {
                    // [D] WinUAE @ Hatari e77819f7 — oracle reading, cited below.
                    // Save the first two longs in case the 2nd or 3rd
                    // read faults (WinUAE fpp.c:2905-2907)
                    u32 w1 = readM<C, M, Long>(ad);
                    if (mmu030) { mmuFmovemStore[0] = w1; mmuState[0]++; }
                    u32 w2 = readM<C, M, Long>(ad + 4);
                    if (mmu030) { mmuFmovemStore[1] = w2; mmuState[0]++; }
                    u32 w3 = readM<C, M, Long>(ad + 8);
                    if (mmu030) mmuState[0]++;
                    fpuToExtendedV(fpu.fp[fpn], w1, w2, w3);
                }

                if (incr > 0) ad += 12;
            }
            list <<= 1;
        }

        if (mmu030) mmuLogging = logSave;

        if constexpr (M == Mode::PI || M == Mode::PD) writeA(n, ad);
    }

    prefetch<C, POLL>();

    CYCLES_68020(ck)

    FINALIZE
}
