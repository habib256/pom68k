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


//
// POM68K O5 slice 2 — MC68882 FPU EXECUTION (2026-07-15)
//
// The 68882 programmer's model (MC68881/MC68882 User's Manual) executed on
// top of extern/softfloat — the same 80-bit softfloat family the primary
// oracle (WinUAE) runs, so numerical convergence with the differential
// corpus holds by construction. The instruction semantics below are ported
// from WinUAE fpp.c / fpp_softfloat.c (oracle vendor tree, hatari e77819f7)
// with file:line citations; the manual is cited where it is the source.
//
// Reachability: nothing in this file executes unless a 6888x is attached
// (setFPUModel). With fpuModel == NONE the jump table holds the stock
// Line-F handlers, keeping the FPU-less SST030 corpus byte-identical.
//
// Known-incomplete (for the differential loop to attack):
//   * FPU exception *traps* use a best-effort format $0 frame (pre- and
//     mid-instruction) instead of the MC68030UM coprocessor protocol
//     frames ($9/$2); FPSR/accrued bookkeeping itself is complete. Fuzz
//     keeps exception enables mostly zero, so this path is rarely taken.
//   * FSAVE BUSY frames are not generated (FRESTORE skips them), matching
//     WinUAE's 6888x support level — a deliberate oracle-parity decision
//     (POM68K_VENDOR.md § FPU).
//   * FRESTORE of a 68040 BUSY frame ($41/$60, reachable through WinUAE's
//     version-hack, see execFRestore) skips the frame instead of resuming
//     the interrupted op.
//

// FPSR bit layout (MC68881UM § 2.2.2; WinUAE fpp.h:10-17, fpp.c:295-309)
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

// One softfloat context. WinUAE keeps a single static too
// (fpp_softfloat.c:50); every instruction re-arms it from FPCR via
// fpuSetMode(), so sharing across Moira instances is safe.
static float_status moiraFpuFs;

// FpuExtended (MoiraTypes.h) mirrors softfloat's floatx80 bit-for-bit
static inline floatx80 fpuToFx80(const FpuExtended &v)
{
    floatx80 r; r.high = v.high; r.low = v.low; return r;
}
static inline FpuExtended fpuFromFx80(floatx80 v)
{
    return FpuExtended { v.high, v.low };
}

// Operand sizes per format field, in bytes; the second row is the A7
// variant (SP stays word-aligned for .B) — WinUAE fpp.c:1523-1524
static constexpr int fpuSzTab1[8] = { 4, 4, 12, 12, 2, 8, 1, 0 };
static constexpr int fpuSzTab2[8] = { 4, 4, 12, 12, 2, 8, 2, 0 };

// Format conversions (fpp_softfloat.c:201-261). to_exten is a raw bit
// copy (denormals/unnormals preserved); from_exten canonicalizes through
// floatx80_to_floatx80 like the real chip's output stage.
static inline void fpuFromIntV(FpuExtended &v, i32 val)
{
    v = fpuFromFx80(int32_to_floatx80(val));
}
static inline void fpuToSingleV(FpuExtended &v, u32 w)
{
    v = fpuFromFx80(float32_to_floatx80_allowunnormal(w, &moiraFpuFs));
}
static inline u32 fpuFromSingleV(const FpuExtended &v)
{
    return floatx80_to_float32(fpuToFx80(v), &moiraFpuFs);
}
static inline void fpuToDoubleV(FpuExtended &v, u32 w1, u32 w2)
{
    v = fpuFromFx80(float64_to_floatx80_allowunnormal((u64(w1) << 32) | w2, &moiraFpuFs));
}
static inline void fpuFromDoubleV(const FpuExtended &v, u32 *w1, u32 *w2)
{
    float64 f = floatx80_to_float64(fpuToFx80(v), &moiraFpuFs);
    *w1 = u32(f >> 32);
    *w2 = u32(f);
}
static inline void fpuToExtendedV(FpuExtended &v, u32 w1, u32 w2, u32 w3)
{
    v.high = u16(w1 >> 16);
    v.low = (u64(w2) << 32) | w3;
}
static inline void fpuFromExtendedV(const FpuExtended &v, u32 *w1, u32 *w2, u32 *w3)
{
    floatx80 f = floatx80_to_floatx80(fpuToFx80(v), &moiraFpuFs);
    *w1 = u32(f.high) << 16;
    *w2 = u32(f.low >> 32);
    *w3 = u32(f.low);
}
static inline i64 fpuToIntV(const FpuExtended &v, int size)
{
    switch (size) {
        case 0:  return floatx80_to_int8(fpuToFx80(v), &moiraFpuFs);
        case 1:  return floatx80_to_int16(fpuToFx80(v), &moiraFpuFs);
        case 2:  return floatx80_to_int32(fpuToFx80(v), &moiraFpuFs);
        default: return 0;
    }
}

// Packed decimal (P) input — WinUAE fp_to_pack (fpp_softfloat.c:629-687)
static void fpuToPackedV(FpuExtended &v, const u32 wrd[3])
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

    v = fpuFromFx80(floatdecimal_to_floatx80(f, &moiraFpuFs));
}

// Packed decimal (P) output with k-factor — WinUAE fp_from_pack
// (fpp_softfloat.c:690-751)
static void fpuFromPackedV(const FpuExtended &v, u32 wrd[3], int kfactor)
{
    i32 kf = kfactor;
    floatx80 f = floatx80_to_floatdecimal(fpuToFx80(v), &kf, &moiraFpuFs);

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

// The 6888x predicate table (WinUAE fpp.c:2069-2087, condition_table_6888x,
// cputester-verified against real silicon). Indexed by
// [(N Z I NAN) * 32 + predicate]; the 040/060 differ only on a few IEEE
// non-aware rows and are not modelled (LC II PDS FPU is a 68882).
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

// Dyadic opmode predicate (WinUAE fpp.c:372-375)
static inline bool fpuIsDyadic(u16 ext)
{
    return (ext & 0x30) == 0x20 || (ext & 0x7f) == 0x38;
}

//
// MC68882 instruction timing — MC68881/MC68882UM Section 8, all figures
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
//

// Table 8-3, FPn-to-FPm totals by command-word opmode. Alias opmodes
// ($05, $07, ... — see fpuArithmetic) charge their base operation;
// $40-$7F never execute (Line-F).
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

// Table 8-3 column spread over the FPn-to-FPm column, by format field:
// L +38, S +13, X +25, P +855, W +38, D +19, B +38. The integer formats
// show +30 on the monadic rows and +38 on the dyadic ones; the dyadic
// figure is charged uniformly (advisory, ±8 cycles).
static constexpr i16 fpuCk882Fmt[8] = { 38, 13, 25, 855, 38, 19, 38, 855 };

// Table 8-3 "FMOVE to memory" row, by format field: integer 110, S 38,
// X 50, P 2006 (static k; dynamic k +14 per footnote *****), D 44.
static constexpr i16 fpuCk882MoveOut[8] = { 110, 38, 50, 2006, 110, 44, 110, 2020 };

// Table 8-3: FMOVECR total (constant-ROM source)
static constexpr int FPU_CK_FMOVECR = 32;

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

// Table 8-8 (MC68882/MC68881 rows, cache case): FSAVE/FRESTORE by frame.
// The BUSY figures are FRESTORE-only here (FSAVE never emits BUSY, see
// execFSave); an invalid frame charges the NULL figure before the format
// error (documented estimate — the manual has no such row).
static constexpr int FPU_CK_FSAVE_NULL  = 16;
static constexpr int FPU_CK_FSAVE_IDLE2 = 100;  // 68882 $38 IDLE
static constexpr int FPU_CK_FSAVE_IDLE1 = 52;   // 68881 $18 IDLE
static constexpr int FPU_CK_FREST_NULL  = 21;
static constexpr int FPU_CK_FREST_IDLE2 = 105;  // 68882 $38 IDLE
static constexpr int FPU_CK_FREST_IDLE1 = 57;   // 68881 $18 IDLE
static constexpr int FPU_CK_FREST_BUSY2 = 339;  // 68882 $D4 BUSY
static constexpr int FPU_CK_FREST_BUSY1 = 291;  // 68881 $B4 BUSY

// Exception vector from FPSR exception byte, priority order BSUN > SNAN >
// OPERR > OVFL > UNFL > DZ > INEX (MC68881UM § 5.3; WinUAE fpp.c:432-443)
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
    // MC68881/882UM § 6.1 (also FRESTORE of a NULL frame, WinUAE
    // fpu_null, fpp.c:1443): control registers cleared, data registers
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
}

void
Moira::fpuSetMode()
{
    // FPCR mode byte -> softfloat (WinUAE fp_set_mode,
    // fpp_softfloat.c:53-84). Note the 6888x quirk: an invalid precision
    // field ($C0) selects double, not extended.
    set_float_detect_tininess(float_tininess_before_rounding, &moiraFpuFs);

    switch (fpu.fpcr & 0xC0) {

        case 0x40: set_floatx80_rounding_precision(32, &moiraFpuFs); break;
        case 0x00: set_floatx80_rounding_precision(80, &moiraFpuFs); break;
        default:   set_floatx80_rounding_precision(64, &moiraFpuFs); break;
    }
    switch (fpu.fpcr & 0x30) {

        case 0x00: set_float_rounding_mode(float_round_nearest_even, &moiraFpuFs); break;
        case 0x10: set_float_rounding_mode(float_round_to_zero, &moiraFpuFs); break;
        case 0x20: set_float_rounding_mode(float_round_down, &moiraFpuFs); break;
        case 0x30: set_float_rounding_mode(float_round_up, &moiraFpuFs); break;
    }

    // 6888x: FADD/FSUB of opposite infinities yields the second operand
    // (WinUAE fp_init_softfloat, fpp_softfloat.c:753-761)
    set_special_flags(addsub_swap_inf, &moiraFpuFs);
}

void
Moira::fpuClearStatus()
{
    // Clear the FPSR exception status byte only (WinUAE fpsr_clear_status,
    // fpp.c:617-624) plus the softfloat flag accumulator
    fpu.fpsr &= 0x0fff00f8;
    moiraFpuFs.float_exception_flags = 0;
}

u32
Moira::fpuMakeStatus()
{
    // Softfloat flags -> FPSR exception byte + accrued byte (WinUAE
    // fpsr_make_status/fp_get_status/updateaccrued, fpp.c:626-661).
    // Returns the enabled exceptions that abort the register update
    // (SNAN/OPERR/DZ — softfloat build has support_exceptions).
    auto f = moiraFpuFs.float_exception_flags;

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

    if (fpu.fpsr & (FPSR_BSUN | FPSR_SNAN | FPSR_OPERR)) fpu.fpsr |= FPSR_AE_IOP;
    if ((fpu.fpsr & FPSR_UNFL) && (fpu.fpsr & FPSR_INEX2)) fpu.fpsr |= FPSR_AE_UNFL;
    if (fpu.fpsr & FPSR_DZ) fpu.fpsr |= FPSR_AE_DZ;

    return exception;
}

void
Moira::fpuSetResultFlags(const FpuExtended &r)
{
    // Condition code byte from a result (WinUAE fpsr_set_result_always +
    // fpsr_set_result, fpp.c:593-616)
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
    // FMOD/FREM quotient byte (WinUAE fpsr_set_quotient, fpp.c:679); note
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
    // The 6888x supports denormal/unnormal operands by normalizing them
    // on input (WinUAE normalize_or_fault_if_no_denormal_support 6888x
    // branch, fpp.c:1455-1472)
    floatx80 fx = fpuToFx80(v);
    if (floatx80_is_unnormal(fx) || floatx80_is_denormal(fx)) {
        v = fpuFromFx80(floatx80_normalize(fx));
    }
}

bool
Moira::fpuCheckArithException(const FpuExtended &src, u16 opcode, u16 ext, u32 ea)
{
    // WinUAE fpsr_check_arithmetic_exception, 6888x branch (fpp.c:445-508):
    // latch the highest-priority enabled exception vector; the trap itself
    // fires pre-instruction at the NEXT FPU instruction (coprocessor
    // protocol). Also captures the 68882 FSAVE IDLE-frame data.
    u32 exception = fpu.fpsr & fpu.fpcr & 0xff00;
    if (!exception) return false;

    fpu.expPend = fpuVectorFor(exception);
    fpu.ea = ea;                                        // fpp.c:476 fp_ea
    fpu.fpiar = reg.pc0;                                // fpp.c:478-480

    u32 opclass = ext >> 13 & 7;

    fpu.fsaveEo[0] = fpu.fsaveEo[1] = fpu.fsaveEo[2] = 0;
    if (opclass == 3) {                                 // fpp.c:491-495
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
            FpuExtended eo = fpuFromFx80(getFloatInternalOverflow());
            fpu.fsaveEo[0] = u32(eo.high) << 16;
            fpu.fsaveEo[1] = u32(eo.low >> 32);
            fpu.fsaveEo[2] = u32(eo.low);
            break;
        }
        case 51:                                        // UNFL
        {
            FpuExtended eo = fpuFromFx80(getFloatInternalUnderflow());
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
    // FPU exception frames — WinUAE Exception_build_stack_frame_common
    // (newcpu_common.c:1616, nr 48-55): pre-instruction = four-word
    // format $0 with PC = the FP instruction being started (it
    // re-executes after the handler); post-instruction (FMOVE out) =
    // format $3 "floating-point post-instruction" frame — SR, next PC,
    // $3xxx vector word, then the operand's effective address (fp_ea).
    // Solo-corpus arbitrated 2026-07-15 (was a format $0 stub).
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
    // get_fp_value/put_fp_value2 modes 3/4, fpp.c:1589-1611, and FScc
    // -(An), fpp.c:2290-2295): the ORIGINAL register value is restored by
    // a non-lastwrite bus fault (newcpu.c m68k_run_mmu030 CATCH ->
    // cpu_restore_fixup), while the frame's wb2/wb3 status byte stays 0
    // (mmu030fixupreg needs the 0x300 flag bits fpp.c never sets). Bit 7
    // marks the fixup as plain; bit 6 = valid (slot occupancy).
    if (cpuModel != Model::M68030) return;

    int slot = mmuFixupReg[0] ? 1 : 0;
    mmuFixupReg[slot] = u8(n | 0x40 | 0x80);
    mmuFixupVal[slot] = reg.a[n];
}

template <Core C> void
Moira::execFRestoreFormatError()
{
    // FRESTORE of an invalid frame: format error, vector 14 (WinUAE
    // fpp.c:2783-2797 -> Exception(14) -> format $0 frame with
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
    // POM68K Q4 — F2xx on a FPU-less 040 (68LC040/68EC040): vector 11
    // with the format $4 "FP disabled" frame (WinUAE fpu_op_illg
    // fpp.c:1085-1094: fp_unimp_ins -> Exception(11);
    // Exception_build_stack_frame_common nr==11 && fp_unimp_ins &&
    // 68040 && fpu_model==0 -> frame 0x4 {SR, currpc, $402C, fp_ea,
    // instruction_pc}). The caller consumed the shape's extension words
    // (reg.pc mirrors WinUAE's m68k_getpc()) and passes fp_ea.
    //
    // Mac OS PACK 4 F-line glue only accepts format $0 ($002C). Real LC 475
    // loads FPSP for format $4; until that path is selected,
    // fpuDisabledSaneFline (Cpu040 under POM68K_Q605_NOFPU) rewinds to the
    // opcode and stacks classic Line-F so SANE can emulate. sst68040 leaves
    // the knob clear and keeps the architectural format $4 frame.
    if (fpuDisabledSaneFline) {
        (void)ea;
        reg.pc = reg.pc0;
        execException<C>(M68kException::LINEF);
        return;
    }

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

template <Core C> bool
Moira::fpuCheckPending()
{
    // WinUAE fp_exception_pending (fpp.c:377-408): a latched arithmetic
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
    // WinUAE fpp_cond (fpp.c:2090-2112); predicates use 5 bits, bit 4
    // separates the IEEE non-aware predicates that set BSUN on NaN
    // (MC68881UM § 4.6.2.2)
    if ((cond & 0x10) && (fpu.fpsr & FPSR_CC_NAN)) {

        // fpsr_set_bsun (fpp.c:663-677)
        fpu.fpsr |= FPSR_BSUN | FPSR_AE_IOP;

        if (fpu.fpcr & FPSR_BSUN) {

            fpu.expPend = 48;
            if (fpuModel != FPUModel::M68882) fpu.expPend = 0;
            execFpuException<C>(48, true);
            return -2;
        }
    }

    int control = (fpu.fpsr >> 24) & 15;
    return fpuCondTable6888x[control * 32 + (cond & 0x1f)] ? 1 : 0;
}

bool
Moira::fpuGetConstant(FpuExtended &v, int cr)
{
    // FMOVECR constant ROM (WinUAE fpu_get_constant, fpp.c:774-932).
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

        default:                        // undefined offsets (fpp.c:850-908)
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
            if (prec == 1) v = fpuFromFx80(floatx80_round32(fpuToFx80(v), &moiraFpuFs));
            if (prec >= 2) v = fpuFromFx80(floatx80_round64(fpuToFx80(v), &moiraFpuFs));

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

    if (prec == 1) v = fpuFromFx80(floatx80_round32(fpuToFx80(v), &moiraFpuFs));
    if (prec >= 2) v = fpuFromFx80(floatx80_round64(fpuToFx80(v), &moiraFpuFs));

    fpuSetResultFlags(v);
    return true;
}

bool
Moira::fpuArithmetic(const FpuExtended &srcM, FpuExtended &dstM, u16 ext)
{
    // Opmode dispatch, WinUAE fp_arithmetic (fpp.c:2935-3157). The alias
    // opmodes (0x05, 0x07, 0x0b, ... — undocumented but real on 6888x
    // silicon) map onto their base operation. Opmodes >= 0x40 never reach
    // here (F-line, see execFGeneric).
    floatx80 src = fpuToFx80(srcM);
    floatx80 dst = fpuToFx80(dstM);
    u64 quot = 0;
    uint64_t q = 0;     // softfloat's uint64_t may differ from moira::u64
    u8 s = 0;

    switch (ext & 0x7f) {

        case 0x00: dst = floatx80_move(src, &moiraFpuFs); break;            // FMOVE
        case 0x01: dst = floatx80_round_to_int(src, &moiraFpuFs); break;    // FINT
        case 0x02: dst = floatx80_sinh(src, &moiraFpuFs); break;            // FSINH
        case 0x03: dst = floatx80_round_to_int_toward_zero(src, &moiraFpuFs); break; // FINTRZ
        case 0x04:                                                          // FSQRT
        case 0x05: dst = floatx80_sqrt(src, &moiraFpuFs); break;
        case 0x06:                                                          // FLOGNP1
        case 0x07: dst = floatx80_lognp1(src, &moiraFpuFs); break;
        case 0x08: dst = floatx80_etoxm1(src, &moiraFpuFs); break;          // FETOXM1
        case 0x09: dst = floatx80_tanh(src, &moiraFpuFs); break;            // FTANH
        case 0x0a:                                                          // FATAN
        case 0x0b: dst = floatx80_atan(src, &moiraFpuFs); break;
        case 0x0c: dst = floatx80_asin(src, &moiraFpuFs); break;            // FASIN
        case 0x0d: dst = floatx80_atanh(src, &moiraFpuFs); break;           // FATANH
        case 0x0e: dst = floatx80_sin(src, &moiraFpuFs); break;             // FSIN
        case 0x0f: dst = floatx80_tan(src, &moiraFpuFs); break;             // FTAN
        case 0x10: dst = floatx80_etox(src, &moiraFpuFs); break;            // FETOX
        case 0x11: dst = floatx80_twotox(src, &moiraFpuFs); break;          // FTWOTOX
        case 0x12:                                                          // FTENTOX
        case 0x13: dst = floatx80_tentox(src, &moiraFpuFs); break;
        case 0x14: dst = floatx80_logn(src, &moiraFpuFs); break;            // FLOGN
        case 0x15: dst = floatx80_log10(src, &moiraFpuFs); break;           // FLOG10
        case 0x16:                                                          // FLOG2
        case 0x17: dst = floatx80_log2(src, &moiraFpuFs); break;
        case 0x18: dst = floatx80_abs(src, &moiraFpuFs); break;             // FABS
        case 0x19: dst = floatx80_cosh(src, &moiraFpuFs); break;            // FCOSH
        case 0x1a:                                                          // FNEG
        case 0x1b: dst = floatx80_neg(src, &moiraFpuFs); break;
        case 0x1c: dst = floatx80_acos(src, &moiraFpuFs); break;            // FACOS
        case 0x1d: dst = floatx80_cos(src, &moiraFpuFs); break;             // FCOS
        case 0x1e: dst = floatx80_getexp(src, &moiraFpuFs); break;          // FGETEXP
        case 0x1f: dst = floatx80_getman(src, &moiraFpuFs); break;          // FGETMAN
        case 0x20: dst = floatx80_div(dst, src, &moiraFpuFs); break;        // FDIV
        case 0x21:                                                          // FMOD
            fpuGetQuotient(&quot, &s);
            q = quot;
            dst = floatx80_mod(dst, src, &q, &s, &moiraFpuFs);
            fpuSetQuotient(q, s);
            break;
        case 0x22: dst = floatx80_add(dst, src, &moiraFpuFs); break;        // FADD
        case 0x23: dst = floatx80_mul(dst, src, &moiraFpuFs); break;        // FMUL
        case 0x24: dst = floatx80_sgldiv(dst, src, &moiraFpuFs); break;     // FSGLDIV
        case 0x25:                                                          // FREM
            fpuGetQuotient(&quot, &s);
            q = quot;
            dst = floatx80_rem(dst, src, &q, &s, &moiraFpuFs);
            fpuSetQuotient(q, s);
            break;
        case 0x26: dst = floatx80_scale(dst, src, &moiraFpuFs); break;      // FSCALE
        case 0x27: dst = floatx80_sglmul(dst, src, &moiraFpuFs); break;     // FSGLMUL
        case 0x28:                                                          // FSUB
        case 0x29: case 0x2a: case 0x2b: case 0x2c: case 0x2d: case 0x2e: case 0x2f:
            dst = floatx80_sub(dst, src, &moiraFpuFs);
            break;
        case 0x30: case 0x31: case 0x32: case 0x33:                         // FSINCOS
        case 0x34: case 0x35: case 0x36: case 0x37:
        {
            // Cosine lands in FPc directly, sine is the stored result
            // (WinUAE fp_sincos, fpp_softfloat.c:527-530); when FPc == FPs
            // the sine (stored last) wins
            floatx80 c;
            dst = floatx80_sincos(src, &c, &moiraFpuFs);
            fpu.fp[ext & 7] = fpuFromFx80(c);
            break;
        }
        case 0x38: case 0x39: case 0x3c: case 0x3d:                         // FCMP
        {
            dst = floatx80_cmp(dst, src, &moiraFpuFs);
            fpuMakeStatus();
            fpuSetResultFlags(fpuFromFx80(dst));
            return false;
        }
        case 0x3a: case 0x3b: case 0x3e: case 0x3f:                         // FTST
        {
            dst = floatx80_tst(src, &moiraFpuFs);
            fpuMakeStatus();
            fpuSetResultFlags(fpuFromFx80(dst));
            return false;
        }
        default:
            return false;   // unreachable — filtered in execFGeneric
    }

    fpuSetResultFlags(fpuFromFx80(dst));

    // Enabled SNAN/OPERR/DZ abort the register update (fpp.c:3152-3156)
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
    // WinUAE get_fp_value (fpp.c:1518-1749); the FPx-to-FPx case is
    // handled by the caller. Returns 0 on invalid encodings (Line-F).
    const int n = _____________xxx(opcode);
    const int size = ___xxx__________(ext);

    if constexpr (M == Mode::DN) {

        switch (size) {

            case 0: fpuFromIntV(src, i32(readD(n))); return 1;              // L
            case 1: fpuToSingleV(src, readD(n)); fpuNormalize(src); return 1; // S
            case 4: fpuFromIntV(src, i16(readD(n))); return 1;              // W
            case 6: fpuFromIntV(src, i8(readD(n))); return 1;               // B

            default:
                return 0;   // X, P, D from Dn: F-line (fpp.c:1563-1576)
        }

    } else if constexpr (M == Mode::AN || M == Mode::IP) {

        return 0;           // fpp.c:1579-1584 / get_fp_ad default

    } else if constexpr (M == Mode::IM) {

        // Immediate extension words are consumed before conversion
        // (fpp.c:1640-1667); B/W immediates occupy one word, value in
        // the low byte/word
        u32 e0, e1, e2;

        switch (size) {

            case 0: fpuFromIntV(src, i32(readI<C, Long>())); return 1;
            case 1: fpuToSingleV(src, readI<C, Long>()); fpuNormalize(src); return 1;
            case 2:
                e0 = readI<C, Long>(); e1 = readI<C, Long>(); e2 = readI<C, Long>();
                fpuToExtendedV(src, e0, e1, e2); fpuNormalize(src); return 1;
            case 3:
            {
                u32 wrd[3];
                wrd[0] = readI<C, Long>(); wrd[1] = readI<C, Long>(); wrd[2] = readI<C, Long>();
                fpuToPackedV(src, wrd); fpuNormalize(src); return 1;
            }
            case 4: fpuFromIntV(src, i16(readI<C, Word>())); return 1;
            case 5:
                e0 = readI<C, Long>(); e1 = readI<C, Long>();
                fpuToDoubleV(src, e0, e1); fpuNormalize(src); return 1;
            case 6: fpuFromIntV(src, i8(readI<C, Word>())); return 1;

            default:
                return 0;
        }

    } else {

        // Memory operands. (An)+/-(An) update the register before the
        // access like WinUAE (fpp.c:1589-1611) — the adjustment survives
        // a fault, consistent with Moira's 68000 findings.
        if (size == 7) return 0;

        const int bytes = (n == 7) ? fpuSzTab2[size] : fpuSzTab1[size];
        u32 ad;

        if constexpr (M == Mode::AI) {
            ad = readA(n);
        } else if constexpr (M == Mode::PI) {
            fpuArmFixup(n);                     // fpp.c:1591-1593
            ad = readA(n);
            writeA(n, ad + bytes);
        } else if constexpr (M == Mode::PD) {
            fpuArmFixup(n);                     // fpp.c:1600-1602
            ad = readA(n) - bytes;
            writeA(n, ad);
        } else {
            ad = computeEA<C, M, Long>(n);
        }
        ea = ad;    // WinUAE *adp (fpp.c:1678) — feeds fp_ea on a latch

        switch (size) {

            case 0: fpuFromIntV(src, i32(readM<C, M, Long>(ad))); return 1;
            case 1: fpuToSingleV(src, readM<C, M, Long>(ad)); fpuNormalize(src); return 1;
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
                fpuToPackedV(src, wrd); fpuNormalize(src); return 1;
            }
            case 4: fpuFromIntV(src, i16(readM<C, M, Word>(ad))); return 1;
            case 5:
            {
                u32 w1 = readM<C, M, Long>(ad);
                u32 w2 = readM<C, M, Long>(ad + 4);
                fpuToDoubleV(src, w1, w2); fpuNormalize(src); return 1;
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
    // WinUAE put_fp_value2 (fpp.c:1751-1966). Returns 0 on invalid
    // encodings (Line-F).
    const int n = _____________xxx(opcode);
    const int size = ___xxx__________(ext);

    // Signed 7-bit k-factor, static (ext) or dynamic (Dn) — fpp.c:1816-1819
    auto kFactor = [&]() {
        int k = (size == 7) ? int(readD((ext >> 4) & 7)) : ext;
        k &= 127;
        if (k & 64) k |= ~63;
        return k;
    };

    if constexpr (M == Mode::DN) {

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
                // The k-factor conversion runs — and updates FPSR — even
                // though the Dn destination then takes an F-line
                // (fpp.c:1810-1825 "checked even if EA is illegal").
                // WinUAE folds the raw host flags only (fpp_get_status,
                // no accrued-byte propagation).
                u32 wrd[3];
                int k = kFactor();
                fpuNormalize(value);
                fpuFromPackedV(value, wrd, k);

                auto f = moiraFpuFs.float_exception_flags;
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

        return 0;           // fpp.c:1830, 1878-1880

    } else {

        const int szIdx = (size == 7) ? 3 : size;   // dynamic P sizes as P
        const int bytes = (n == 7) ? fpuSzTab2[szIdx] : fpuSzTab1[szIdx];
        u32 ad;

        if constexpr (M == Mode::AI) {
            ad = readA(n);
        } else if constexpr (M == Mode::PI) {
            fpuArmFixup(n);                     // fpp.c:1838-1840
            ad = readA(n);
            writeA(n, ad + bytes);
        } else if constexpr (M == Mode::PD) {
            fpuArmFixup(n);                     // fpp.c:1847-1849
            ad = readA(n) - bytes;
            writeA(n, ad);
        } else {
            ad = computeEA<C, M, Long>(n);
        }
        ea = ad;    // WinUAE *adp (fpp.c:1884) — feeds fp_ea on a latch

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

    // FBcc (MC68881UM § 4.6; WinUAE fpuop_bcc, fpp.c:2333-2363). FNOP is
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

    // POM68K Q4: pseudo-conditions ($20-$3F, registered on the 040 family
    // only) are Line-F when an FPU is present (WinUAE fpp_cond -> -1 ->
    // fpu_op_illg -> op_illg)
    if ((opcode & 0x3f) >= 0x20) { execLineF<C, I, M, S>(opcode); FINALIZE return; }

    fpu.state = 1;                          // maybe_idle_state (fpp.c:2186)

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

    CYCLES_68020(cc ? FPU_CK_FBCC_T : FPU_CK_FBCC_N)    // Table 8-7

    FINALIZE
}

template <Core C, Instr I, Mode M, Size S> void
Moira::execFDbcc(u16 opcode)
{
    AVAILABILITY(Core::C68020)

    // FDBcc (MC68881UM § 4.7; WinUAE fpuop_dbcc, fpp.c:2201-2241)
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

    int ck = FPU_CK_FDBCC_TRUE;             // Table 8-7: cc true, no branch

    if (!cc) {

        int dn = _____________xxx(opcode);
        u32 newpc = U32_ADD(reg.pc, (i16)queue.irc);

        bool takeBranch = readD<Word>(dn) != 0;
        ck = takeBranch ? FPU_CK_FDBCC_TAKEN : FPU_CK_FDBCC_EXP;

        // Decrement before the branch decision (fpp.c:2232-2234)
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

    // FScc (MC68881UM § 4.8; WinUAE fpuop_scc, fpp.c:2243-2302). EA
    // extension words are consumed before the predicate is evaluated;
    // the (An)+/-(An) adjustment happens after (fpp.c:2264-2299).
    // POM68K Q4: FPU-less 040 (68LC040/68EC040) — the shape's words are
    // consumed, then vector 11 / format $4 (see execFpuDisabled040)
    if (cpuModel >= Model::M68EC040 && !hasFPU()) {
        (void)readI<C, Word>();             // condition word
        u32 ea40 = 0;                       // get_fp_ad conventions
        int n40 = _____________xxx(opcode);
        if constexpr (M == Mode::AI || M == Mode::PI) ea40 = readA(n40);
        // fpuop_scc pre-adjusts -(An) by the byte size (fpp.c:2281-2284)
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

        // -(An) arms a plain fixup before the register update (WinUAE
        // fpuop_scc, fpp.c:2290-2295; (An)+ updates after the write and
        // arms nothing)
        if constexpr (M == Mode::PD) { fpuArmFixup(n); writeA(n, ea); }
        writeM<C, M, Byte>(ea, cc ? 0xff : 0x00);
        if constexpr (M == Mode::PI) writeA(n, ea + (n == 7 ? 2 : 1));
    }

    prefetch<C, POLL>();

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

    // FTRAPcc (MC68881UM § 4.9; WinUAE fpuop_trapcc, fpp.c:2304-2331).
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
        CYCLES_68020(FPU_CK_FTRAP_T[opd])   // Table 8-7, trap taken
        FINALIZE
        return;
    }

    prefetch<C, POLL>();

    CYCLES_68020(FPU_CK_FTRAP_N[opd])       // Table 8-7, trap not taken

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

    // FSAVE (MC68882UM § 6; WinUAE fpuop_save, fpp.c:2365-2580, 6888x
    // branch). Supervisor only. Writes a 4-byte NULL frame when the FPU
    // never left the reset state, else the 68882 $3C-byte IDLE frame
    // (version $1F, length $38). WinUAE's 68030-MMU build skips writing
    // the eight 68882-internal longs "to save MMU state space"
    // (fpp.c:2532-2534) — the primary oracle runs that path, so it is
    // replicated when cpuModel == M68030.
    //
    // BUSY frames ($B4/$D4) are deliberately NOT generated: WinUAE's own
    // 6888x support never emits them (fpuop_save has no busy path — the
    // mid-instruction save window doesn't exist in its coprocessor
    // model), and the oracle is the convergence target. FRESTORE accepts
    // them (skip, like WinUAE fpp.c:2788-2790). Decision logged in
    // POM68K_VENDOR.md § FPU.
    SUPERVISOR_MODE_ONLY

    int n = _____________xxx(opcode);
    u32 ad;

    if constexpr (M == Mode::AI || M == Mode::PD) {
        ad = readA(n);
    } else {
        ad = computeEA<C, M, Long>(n);
    }

    // POM68K Q4: FPU-less 040 — privilege check and EA computation first
    // (WinUAE fpuop_save: get_fp_ad before fault_if_no_fpu), then the
    // format $4 frame with the computed address
    if (cpuModel >= Model::M68EC040 && !hasFPU()) {
        execFpuDisabled040<C>(ad);
        FINALIZE
        return;
    }

    const int fpuVersion = 0x1f;                        // fpp.c:1429-1432
    int frameSize = fpuModel == FPUModel::M68882 ? 0x3c : 0x1c;
    u32 frameId = fpu.state == 0
        ? u32(frameSize - 4) << 16
        : (u32(fpuVersion) << 24) | (u32(frameSize - 4) << 16);

    u32 biuFlags = 0x540effff;                          // fpp.c:2510
    biuFlags |= fpu.expState ? 0x20000000 : 0x08000000;

    fpu.expPend = 0;                                    // fpp.c:2514
    if (fpu.state == 0) frameSize = 4;                  // NULL frame

    if constexpr (M == Mode::PD) ad -= frameSize;
    u32 adp = ad;

    writeM<C, M, Long>(ad, frameId);
    ad += 4;

    if (fpu.state != 0) {   // IDLE frame

        writeM<C, M, Long>(ad, fpu.fsaveCcr);           // command/condition
        ad += 4;

        if (fpuModel == FPUModel::M68882) {
            if (cpuModel == Model::M68030) {
                ad += 8 * 4;                            // internal regs untouched
            } else {
                for (int i = 0; i < 8; i++) {           // zeroed (fpp.c:2556-2561)
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

    prefetch<C, POLL>();

    // Table 8-8 (FSAVE row) by emitted frame
    CYCLES_68020(frameSize == 4 ? FPU_CK_FSAVE_NULL :
                 frameSize == 0x3c ? FPU_CK_FSAVE_IDLE2 : FPU_CK_FSAVE_IDLE1)

    FINALIZE
}

template <Core C, Instr I, Mode M, Size S> void
Moira::execFRestore(u16 opcode)
{
    AVAILABILITY(Core::C68020)

    // FRESTORE (MC68882UM § 6; WinUAE fpuop_restore, fpp.c:2593-2812).
    // Supervisor only. Frame acceptance mirrors the 6888x branch
    // (fpp.c:2755-2807) exactly, oracle-verified:
    //
    //   version $00           -> NULL: FPU reset (fpu_null, fpp.c:2796)
    //   version $1F, size $18 -> 68881 IDLE: reload micro-state
    //   version $1F, size $38 -> 68882 IDLE: reload micro-state
    //   version $1F, size $B4 -> 68881 BUSY: skipped (fpp.c:2788-2790)
    //   version $1F, size $D4 -> 68882 BUSY: skipped
    //   version $1F, other    -> format error (vector 14, fpp.c:2791-2794)
    //   version $41           -> WinUAE's 68040 retry hack, see below
    //   anything else         -> format error (vector 14, fpp.c:2804-2806)
    //
    // Note the version byte of BOTH 68881 and 68882 frames is $1F
    // (get_fpu_version, fpp.c:1432-1449) — MC68882UM § 6 agrees.
    SUPERVISOR_MODE_ONLY

    int n = _____________xxx(opcode);
    u32 ad;

    if constexpr (M == Mode::AI || M == Mode::PI) {
        ad = readA(n);
    } else {
        ad = computeEA<C, M, Long>(n);
    }

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

    int ck = FPU_CK_FREST_NULL;             // Table 8-8 (FRESTORE row)

    if (frameVersion == 0x1f) {             // 6888x frame

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

            if ((biuFlags & 0x08000000) == 0) {         // fpp.c:2781-2787
                fpu.expState = 2;
                fpu.expPend = fpuVectorFor(fpu.fpsr & fpu.fpcr & 0xff00);
            } else {
                fpu.expState = 0;
                fpu.expPend = 0;
            }
            ck = frameSize == 0x38 ? FPU_CK_FREST_IDLE2 : FPU_CK_FREST_IDLE1;

        } else if (frameSize == 0xB4 || frameSize == 0xD4) {    // BUSY

            ad += frameSize;                    // skipped (fpp.c:2788-2790)
            ck = frameSize == 0xD4 ? FPU_CK_FREST_BUSY2 : FPU_CK_FREST_BUSY1;

        } else {

            execFRestoreFormatError<C>();
            CYCLES_68020(FPU_CK_FREST_NULL)
            FINALIZE
            return;
        }

    } else if (frameVersion == 0x00) {      // NULL frame — FPU reset

        fpuResetState();

    } else if (frameVersion == 0x41) {

        // WinUAE's "horrible hack" (fpp.c:2799-2802): with
        // fpu_no_unimplemented == false — the oracle's config,
        // oracle/uae/glue.c:84 — a 68040-version frame retries down the
        // 68040 branch (fpp.c:2663-2753). Only $41 matches there:
        // get_fpu_version(68040) == $41 since fpu_revision == 0
        // (fpp.c:1442-1446); a $40 frame mismatches and falls through to
        // the format error below. Oracle-verified 2026-07-15 (solo).
        u32 frameSize = (d >> 16) & 0xff;

        if (frameSize == 0x00) {                        // 68040 IDLE

            fpu.state = 1;                              // fpp.c:2732-2734
            fpu.expState = 0;                           // expPend untouched

        } else if (frameSize == 0x30 || frameSize == 0x28) {    // UNIMP

            ad += frameSize;                            // fpp.c:2727-2730

        } else if (frameSize == 0x60) {                 // 68040 BUSY

            // WinUAE reloads the frame and, when CU_SAVEPC == $FE and
            // the command is opclass 0/2, re-executes the interrupted
            // arithmetic op (fpp.c:2668-2725). The resume is not
            // modelled (TODO — no planted fuzz frame produces it); the
            // frame is skipped with WinUAE's final address (ad_orig +
            // 4 + $60, the sum of its field offsets).
            ad += frameSize;

        } else {

            execFRestoreFormatError<C>();               // fpp.c:2735-2739
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

    // Router for the general F-line window (WinUAE fpuop_arithmetic,
    // fpp.c:3159-3608). The extension word is peeked here and consumed by
    // the sub-handlers.
    u16 ext = queue.irc;

    // An as EA is only decodable for FPx-to-FPx and control-register
    // moves (mirrors dasmFGen; WinUAE returns "no instruction" for the
    // other opclasses). On a FPU-less 040 the no-FPU hook below decides
    // first: get_fp_value's An case reaches fault_if_unimplemented_680x0
    // -> format $4 (fresh-seed arbitrated 2026-07-18), not Line-F.
    if constexpr (M == Mode::AN) {
        if ((ext & 0x4000) &&
            !(cpuModel >= Model::M68EC040 && !hasFPU())) {
            execLineF<C, I, M, S>(opcode);
            return;
        }
    }

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

        // get_fp_value/put_fp_value operand sizes (fpp.c sz1/sz2)
        static constexpr int sz1[8] = { 4, 4, 12, 12, 2, 8, 1, 0 };

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

            // FMOVE(M) control registers: Dn/An forms fault with ea = 0;
            // the #imm form consumes one long per selected register first
            // (fpp.c:3288-3296); memory forms compute the EA
            if constexpr (M == Mode::IM) {
                if (x & 0x1000) (void)readI<C, Long>();
                if (x & 0x0800) (void)readI<C, Long>();
                if (x & 0x0400) (void)readI<C, Long>();
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

    if (fpuCheckPending<C>()) return;       // pre-instruction (fpp.c:3176)

    fpu.state = 1;                          // fpp.c:3598

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

        default:    // opclass 001: undefined — F-line (fpp.c:3590-3593)

            execLineF<C, I, M, S>(opcode);
            return;
    }
}

template <Core C, Instr I, Mode M, Size S> void
Moira::execFMovecr(u16 opcode)
{
    AVAILABILITY(Core::C68020)

    // FMOVECR (MC68881UM § 4.6.1.3; WinUAE fpp.c:3518-3532). ROM offsets
    // $40-$7F take an F-line on 6888x.
    u16 ext = u16(readI<C, Word>());
    int dst = ______xxx_______(ext);

    if (ext & 0x40) {

        execLineF<C, I, M, S>(opcode);
        return;
    }

    fpuSetMode();
    fpuClearStatus();

    // maybe_set_fpiar (fpp.c:755-762): FPIAR updates only when an
    // exception other than BSUN is enabled — a WinUAE (= oracle) economy
    // replicated on purpose
    if (fpu.fpcr & 0x7f00) fpu.fpiar = reg.pc0;

    FpuExtended v;
    fpuGetConstant(v, ext & 0x7f);
    fpu.fp[dst] = v;

    fpuMakeStatus();
    fpuCheckArithException(v, opcode, ext, 0);

    prefetch<C, POLL>();

    CYCLES_68020(FPU_CK_FMOVECR)    // Table 8-3

    FINALIZE
}

template <Core C, Instr I, Mode M, Size S> void
Moira::execFGeneric(u16 opcode)
{
    AVAILABILITY(Core::C68020)

    // Arithmetic, opclasses 000 (FPx source) and 010 (EA source) — WinUAE
    // fpp.c:3514-3589
    u16 ext = u16(readI<C, Word>());
    int dstReg = ______xxx_______(ext);

    // 6888x: opmodes $40-$7F do not exist (they are the 68040 S/D-rounded
    // variants) — F-line (WinUAE fault_if_nonexisting_opmode,
    // fpp.c:1130-1136, accurate mode). Checked before the operand fetch.
    if ((ext & 0x7f) >= 0x40) {

        execLineF<C, I, M, S>(opcode);
        return;
    }

    fpuSetMode();
    fpuClearStatus();

    FpuExtended src;
    u32 ea = 0;

    if (!(ext & 0x4000)) {

        // FPx-to-FPx (fpp.c:1532-1539)
        src = fpu.fp[___xxx__________(ext)];
        fpuNormalize(src);

    } else {

        if (fpuGetSource<C, M>(opcode, ext, src, ea) == 0) {

            execLineF<C, I, M, S>(opcode);
            return;
        }
    }

    if (fpu.fpcr & 0x7f00) fpu.fpiar = reg.pc0;     // maybe_set_fpiar

    FpuExtended dst = fpu.fp[dstReg];
    if (fpuIsDyadic(ext)) fpuNormalize(dst);        // fpp.c:3560-3561

    bool store = fpuArithmetic(src, dst, ext);

    fpuCheckArithException(src, opcode, ext, ea);   // latch, trap at next FP op
    if (store) fpu.fp[dstReg] = dst;

    prefetch<C, POLL>();

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

    // FMOVE FPx -> EA, opclass 011 (MC68881UM § 4.5.3; WinUAE
    // fpp.c:3181-3196). Condition codes are NOT set by a move out; the
    // exception byte is (OPERR on integer overflow, INEX on rounding).
    u16 ext = u16(readI<C, Word>());

    fpuSetMode();
    fpuClearStatus();

    FpuExtended value = fpu.fp[______xxx_______(ext)];
    u32 ea = 0;

    if (fpuPutDest<C, M>(opcode, ext, value, ea) == 0) {

        execLineF<C, I, M, S>(opcode);
        return;
    }

    fpuMakeStatus();
    if (fpu.fpcr & 0x7f00) fpu.fpiar = reg.pc0;     // maybe_set_fpiar

    if (fpuCheckArithException(value, opcode, ext, ea)) { /* 6888x: maskable */ }
    if (fpu.expPend) {

        // Mid/post-instruction exception (WinUAE fp_exception_pending(false),
        // fpp.c:3195); the 68882 keeps the vector pending
        u16 vector = u16(fpu.expPend);
        if (fpuModel != FPUModel::M68882) fpu.expPend = 0;
        execFpuException<C>(vector, false);
        FINALIZE
        return;
    }

    prefetch<C, POLL>();

    CYCLES_68020(fpuCk882MoveOut[___xxx__________(ext)])   // Table 8-3

    FINALIZE
}

template <Core C, Instr I, Mode M, Size S> void
Moira::execFMovem(u16 opcode)
{
    AVAILABILITY(Core::C68020)

    // FMOVEM — control registers (opclasses 100/101, MC68881UM § 4.5.4)
    // and FP data registers (opclasses 110/111, § 4.5.5). WinUAE
    // fpp.c:3198-3512. Neither form touches the condition codes.
    u16 ext = u16(readI<C, Word>());
    int n = _____________xxx(opcode);
    int cod = xxx_____________(ext);
    int ck = 0;                     // Table 8-6 cost, set per branch below

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

            // Only a single control register transfers with Dn
            // (fpp.c:3206-3220); no bits = FPIAR
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

            // Only FPIAR transfers with An (fpp.c:3238-3264)
            if (bits && bits != 0x0400) {
                execLineF<C, I, M, S>(opcode);
                return;
            }
            if (toEa) writeA(n, fpu.fpiar);
            else fpu.fpiar = readA(n);

        } else if constexpr (M == Mode::IM) {

            // #imm as source only; multiple registers allowed on 6888x
            // (fpp.c:3265-3299)
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

            // PC-relative: source only (fpp.c:3311-3315)
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

            // Memory EAs (fpp.c:3300-3400). -(An)/(An)+ update after the
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

        // EA restrictions (fpp.c:3410-3433): registers/immediates never
        // decode; FP->mem excludes (An)+ and PC-relative; mem->FP
        // excludes -(An)
        if constexpr (M == Mode::DN || M == Mode::AN || M == Mode::IM || M == Mode::IP) {
            execLineF<C, I, M, S>(opcode);
            return;
        }

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

        // 6888x: -(An) transfers descending; static/dynamic predecrement
        // list format reverses the register order (fpp.c:3450-3461)
        const int incr = (M == Mode::PD) ? -1 : 1;
        const int regdir = (mode == 0 || mode == 1) ? -1 : 1;

        // POM68K O5 (seed-17 solo convergence): 68030 restart bookkeeping,
        // WinUAE fmovem2mem/fmovem2fpp mmu030 paths (fpp.c:2810-2841,
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
                    // Raw bits, no conversion (WinUAE fmovem2mem uses
                    // from_exten_fmovem, fpp.c:2850); data buffer set
                    // before each put (fpp.c:2829)
                    u32 w1 = u32(fpu.fp[fpn].high) << 16;
                    u32 w2 = u32(fpu.fp[fpn].low >> 32);
                    u32 w3 = u32(fpu.fp[fpn].low);
                    if (mmu030) mmuDataBuffer = w1;
                    writeM<C, M, Long>(ad, w1);
                    if (mmu030) { mmuState[0]++; mmuDataBuffer = w2; }
                    writeM<C, M, Long>(ad + 4, w2);
                    if (mmu030) { mmuState[0]++; mmuDataBuffer = w3; }
                    writeM<C, M, Long>(ad + 8, w3);
                    if (mmu030) mmuState[0]++;
                } else {
                    // Save the first two longs in case the 2nd or 3rd
                    // read faults (WinUAE fpp.c:2896-2898)
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
