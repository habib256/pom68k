// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── 68k cycle-cost model, shared by every native code generator ──────────
// Everything in this header is a property of the GUEST — Moira's 68020
// timing columns, the CMPA surcharge, the full-format extension prices.
// None of it depends on the host ISA, which is why it must not live in a
// `backends/` file: each backend that re-transcribed a column by hand
// created a twin to be maintained in the other one, and the standing "port
// the a64 030 deltas to x86-64" item was the bill (TODO.md § 10, finding 2).
// Backends keep only emission; they READ this model.
//
// The tables are cross-checked at admission time against the tracer's own
// per-instruction cycles (`traced030` / `Instr::cycles`), so a wrong cell
// here never mis-times generated code — it makes both backends REFUSE the
// form outright. Coverage, not correctness (the 2026-08-09 `(xxx).W` cell
// and the 2026-08-12 a64 twin are the precedents).

#pragma once

#include "JitIr.h"

#include <cstdint>

namespace jit {

// ── EA cost index ────────────────────────────────────────────────────────
// One row per 68k addressing mode: 0 Dn, 1 An, 2 (An), 3 (An)+, 4 -(An),
// 5 d16(An), 6 brief indexed, then the mode-7 sub-forms 7.0 abs.w,
// 7.1 abs.l, 7.2 d16(PC), 7.3 brief indexed PC, 7.4 immediate.
enum EaCostIndex : int { EA_DN = 0, EA_AN, EA_AI, EA_PI, EA_PD, EA_DI, EA_IX,
                         EA_AW, EA_AL, EA_DIPC, EA_IXPC, EA_IM,
                         EA_MODE_COUNT };

// Cost-table row for (mode, reg), or -1 when no backend supports the form
// (mode-7 regs 5+ are reserved encodings).
inline int eaCostIndex(int mode, int reg) {
    if (mode < 7) return mode;
    switch (reg) {
        case 0: return EA_AW;
        case 1: return EA_AL;
        case 2: return EA_DIPC;
        case 3: return EA_IXPC;
        case 4: return EA_IM;
        default: return -1;
    }
}

// "Read an operand through <ea>" — execMove0 / execAndEaRg / execTst /
// execCmpiRg all agree on these on the 68020 (Moira MoiraExec_cpp.h).
// The EA_AW cell reads 2, not 3: on the 020 an absolute-short operand costs
// what a register-indirect one does, its extension word being already in
// the prefetch queue. That single off-by-one was 47.4 % of all x86-64 block
// fallbacks on the idle Finder before 2026-08-09, and the a64 twin paid the
// same toll silently until 2026-08-12 — the cell is written ONCE now.
inline constexpr int8_t kEaRead[EA_MODE_COUNT][3] = {   // [mode][0=B 1=W 2=L]
    { 2, 2, 2 },   // Dn
    { 2, 2, 2 },   // An
    { 6, 6, 6 },   // (An)
    { 6, 6, 6 },   // (An)+
    { 7, 7, 7 },   // -(An)
    { 7, 7, 7 },   // d16(An)
    { 9, 9, 9 },   // d8(An,Xn) — brief extension
    { 6, 6, 6 },   // abs.w
    { 6, 6, 6 },   // abs.l
    { 7, 7, 7 },   // d16(PC)
    { 9, 9, 9 },   // d8(PC,Xn) — brief extension
    { 4, 4, 6 },   // #imm
};

// "Read-modify-write <ea>" — execAndRgEa / execAddqEa / execAndiEa /
// execClr / execNegEa / the BTST family all agree: the read cost plus two,
// and a plain 2 when the destination is a register.
inline int eaRmwCost(int m, int szIdx) {
    const int r = kEaRead[m][szIdx];
    if (r < 0) return -1;
    return (m == EA_DN || m == EA_AN) ? 2 : r + 2;
}

// MOVE's destination surcharge (execMove0/2/3/4/5/7/8, 68020 column). The
// `(xxx).W` cell is 2 for the same prefetch-queue reason as kEaRead's.
inline constexpr int8_t kMoveDst[EA_MODE_COUNT] =
    { 0, 0, 2, 2, 3, 3, -1, 2, 4, -1, -1, -1 };

// CMPA's surcharge over ADDA/SUBA. execAdda's 020 column IS kEaRead; CMPA
// is kEaRead + 2 in every mode, byte, word and long alike — it holds a
// SYNC(2) that the ADDA path only takes for a word or a register source
// (MoiraExec_cpp.h:2129 vs :421-423). Charging both alike refused every
// single CMPA: 1.04 M idle-Finder fallbacks on x64 (2026-08-09), about one
// million short-budget fallbacks when a64 repeated the omission.
inline constexpr int kCmpaExtraCycles = 2;

// ── per-operation 68020 columns ──────────────────────────────────────────
// Each names its Moira exec column. A -1 is "no such encoding" (Dn/An rows
// of address-only operations), never a backend's admission policy — an
// unimplemented lowering refuses in the backend, not by blanking a cell
// here. kJsrJmp carries the TRUE indexed cells (7); both generators now
// consume them for JSR, including the full-format address penalty where
// applicable, instead of transcribing a narrower backend-local table.
inline constexpr int8_t kScc[EA_MODE_COUNT] =        // execSccEa, byte row
    { -1, -1, 10, 10, 11, 11, 13, 10, 10, -1, -1, -1 };
inline constexpr int8_t kLea[EA_MODE_COUNT] =        // execLea
    { -1, -1, 6, -1, -1, 7, 9, 6, 6, 7, 9, -1 };
inline constexpr int8_t kPea[EA_MODE_COUNT] =        // execPea
    { -1, -1, 9, -1, -1, 10, 12, 9, 9, 10, 12, -1 };
inline constexpr int8_t kJsrJmp[EA_MODE_COUNT] =     // execJsr / execJmp
    { -1, -1, 4, -1, -1, 5, 7, 4, 4, 5, 7, -1 };
// execMovem{EaRg,RgEa}: base[mode] + 4 per transferred register.
inline constexpr int8_t kMovemToRegs[EA_MODE_COUNT] =
    { -1, -1, 12, 8, -1, 13, -1, 12, 12, 9, -1, -1 };
inline constexpr int8_t kMovemToMem[EA_MODE_COUNT] =
    { -1, -1, 8, -1, 4, 9, -1, 8, 8, -1, -1, -1 };
// execMulu/execMuls, 68020/68030 word-to-long column. Unlike the 68000 and
// 68010 implementations, these cores do not add an operand-dependent
// cyclesMul term; signed and unsigned forms share the same fixed column.
inline constexpr int8_t kMulWord[EA_MODE_COUNT] =
    { 27, -1, 31, 31, 32, 32, 34, 31, 31, 32, 34, 29 };
// execMull, 68020/68030 column. The mandatory extension selects signedness
// and a 32- or 64-bit result, but every successful form shares this EA cost.
inline constexpr int8_t kMulLong[EA_MODE_COUNT] =
    { 43, -1, 47, 47, 48, 48, 50, 47, 47, 48, 50, 47 };
// execDivu/execDivs, 68020 word columns. Address-register direct is not a
// legal source; all other cells are guest timing facts, independent of the
// transactional proof a backend requires for a particular memory mapping.
inline constexpr int8_t kDivuWord[EA_MODE_COUNT] =
    { 44, -1, 48, 48, 49, 49, 51, 48, 48, 49, 51, 46 };
inline constexpr int8_t kDivsWord[EA_MODE_COUNT] =
    { 56, -1, 60, 60, 61, 61, 63, 60, 60, 61, 63, 58 };
// execDivl, 68020 column. Signedness and 32-/64-bit dividend selection live
// in the mandatory extension word but share this successful-retirement cost.
inline constexpr int8_t kDivLong[EA_MODE_COUNT] =
    { 84, -1, 88, 88, 89, 89, 91, 88, 88, 89, 91, 88 };

// Register-count shifts have data-dependent timing, so both native backends
// specialize a block for the traced count and guard Dn&63 before any effect.
// Rogue justified eight for the whole family. Speedometer 4 then identified
// three equally hot *logical* shifts at count sixteen (E8A8/E4AC/E2AD); keep
// that measured extension narrow rather than silently widening AS/RO as well.
inline constexpr unsigned kMaxSpecializedShiftCount = 8;
inline constexpr unsigned kMaxSpecializedLogicalShiftCount = 16;

// ── full-format (68020) extension prices ─────────────────────────────────

// Address-FORMATION penalty of a full-format extension, on top of the
// brief-form price a per-instruction column charges for EA_IX/EA_IXPC.
// This is what LEA/PEA-class users add: the base displacement costs by its
// width, and memory indirection adds the outer fetch.
inline int fullIndexPenalty(uint8_t baseDisplacementWords,
                            IndexIndirect indirect,
                            uint8_t outerDisplacementWords) {
    const int base = baseDisplacementWords == 1 ? 2
                   : baseDisplacementWords == 2 ? 6 : 0;
    if (indirect == IndexIndirect::None) return base;
    return base + (outerDisplacementWords ? 7 : 5);
}

// Operand-READ surcharge of a full-format DIRECT source beyond the brief
// `(d8,An,Xn)` price in kEaRead[EA_IX] — the D1F0 fix (2026-08-27/28).
// One proven sub-form, the one the SimCity census named: direct, base
// suppressed, index live, two-word base displacement — the array addressing
// a compiler emits, `ADDA.L (bd.L,ZAn,Xn),An`. The 030 traces 15 where the
// brief table says 9; the 6 was measured against that trace, not derived.
// Every other full form returns -1 — REFUSED rather than guessed at, and
// the backends' timing gates still compare against the trace, so a wrong
// constant here refuses the instruction instead of mischarging it.
inline int fullFormatReadExtra(IndexIndirect indirect,
                               bool baseSuppressed,
                               bool indexSuppressed,
                               uint8_t baseDisplacementWords) {
    if (indirect != IndexIndirect::None || !baseSuppressed ||
        indexSuppressed || baseDisplacementWords != 2)
        return -1;
    return 6;
}

}  // namespace jit
