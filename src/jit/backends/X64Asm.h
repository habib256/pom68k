// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── x86-64 instruction encoder ──
// Knows nothing about the 68k and nothing about POM68K: it turns method
// calls into bytes. The 68k-to-x86 translation lives one file up, in
// JitBackendX64.cpp, so that the part which is merely tedious (REX/ModRM/
// SIB, displacement widths, branch fixups) is separable from the part which
// has to be *right* about guest semantics.
//
// Only the forms the backend actually emits are implemented. An encoder
// that covers the whole ISA would be mostly untested code; every method
// here is reached by JitBackendX64 and therefore by jit_backend_test.
//
// Operand-size handling is the one thing to keep in mind while reading:
// the 68k's byte/word/long operations map onto x86's 8/16/32-bit forms
// EXACTLY, including the "upper bits of the destination register are left
// alone" rule, because the guest register file lives in memory and an
// 8- or 16-bit store touches only those bytes. That is why the backend
// never allocates guest registers into host registers.

#pragma once
#include <cstdint>
#include <cstddef>

namespace jit::x64 {

enum Reg : uint8_t {
    RAX = 0, RCX, RDX, RBX, RSP, RBP, RSI, RDI,
    R8, R9, R10, R11, R12, R13, R14, R15
};

// Operand width in bytes. B/W/L are the three 68k sizes; Q is host-only
// (pointers and the 64-bit cycle clock).
enum class Sz : uint8_t { B = 1, W = 2, L = 4, Q = 8 };

// [base + disp]. The backend only ever addresses the guest register file
// and a handful of engine fields, all of which hang off one base pointer.
struct Mem {
    Reg     base;
    int32_t disp;
};
inline Mem mem(Reg base, int32_t disp) { return Mem{ base, disp }; }

// x86 condition codes, by their encoding.
enum class Cc : uint8_t {
    O = 0x0, NO = 0x1, B = 0x2, AE = 0x3, E = 0x4, NE = 0x5, BE = 0x6, A = 0x7,
    S = 0x8, NS = 0x9, P = 0xA, NP = 0xB, L = 0xC, GE = 0xD, LE = 0xE, G = 0xF
};
inline Cc invert(Cc c) { return Cc(uint8_t(c) ^ 1); }

// A branch target. Labels are owned by the Asm (fresh() below), not by the
// caller: a fixup holds a pointer to one, and a Label living in a helper's
// stack frame would dangle by the time finish() patched it.
struct Label {
    int32_t bound = -1;                 // offset once placed, -1 while open
};

class Asm {
public:
    Asm(uint8_t* buf, size_t cap) : p_(buf), base_(buf), cap_(cap) {}

    size_t   size() const { return size_t(p_ - base_); }
    uint8_t* cursor() const { return p_; }
    uint8_t* base() const { return base_; }
    bool     overflowed() const { return overflow_; }

    // ── raw ──────────────────────────────────────────────────────────────
    void db(uint8_t v) {
        if (size() >= cap_) { overflow_ = true; return; }
        *p_++ = v;
    }
    void dw(uint16_t v) { db(uint8_t(v)); db(uint8_t(v >> 8)); }
    void dd(uint32_t v) { dw(uint16_t(v)); dw(uint16_t(v >> 16)); }
    void dq(uint64_t v) { dd(uint32_t(v)); dd(uint32_t(v >> 32)); }

    // ── labels ───────────────────────────────────────────────────────────
    void bind(Label& l) { l.bound = int32_t(size()); }

    // A label with the Asm's lifetime. Returns nullptr when the pool is
    // exhausted, which the caller must treat as "give up on this block".
    Label* fresh() {
        if (nLabels_ >= kMaxLabels) { overflow_ = true; return &labels_[0]; }
        labels_[nLabels_] = Label{};
        return &labels_[nLabels_++];
    }

    // Emits a rel32 placeholder for `l` and records it for patching. Every
    // open reference must be resolved by finish(); an unresolved one is a
    // code-generator bug, not a runtime condition, so it is asserted by the
    // backend rather than handled.
    void rel32(Label& l) { addFixup(l, false); dd(0); }
    // …and the 8-bit form, four bytes shorter per branch. Only for hops the
    // caller KNOWS are short — inside one emitted instruction. If the
    // distance turns out not to fit, finish() fails and the block is
    // refused, which costs coverage rather than correctness.
    void rel8(Label& l) { addFixup(l, true); db(0); }

    // Byte offset and fixup count, for rewind(). An instruction family that
    // gives up half-way has to leave no trace: not the bytes it emitted, and
    // not the branch fixups pointing into them.
    struct Mark { size_t bytes; int fixups; };
    Mark mark() const { return Mark{ size(), nFixups_ }; }
    void rewind(const Mark& m) {
        p_ = base_ + m.bytes;
        nFixups_ = m.fixups;
    }

    // Patches every recorded reference. Safe to call once, at the end.
    bool finish() {
        for (int i = 0; i < nFixups_; i++) {
            const Fixup& f = fixups_[i];
            if (f.label->bound < 0) return false;
            uint8_t* at = base_ + f.at;
            if (f.shrt) {
                const int32_t delta = f.label->bound - (f.at + 1);
                if (delta < -128 || delta > 127) return false;
                at[0] = uint8_t(delta);
                continue;
            }
            const int32_t delta = f.label->bound - (f.at + 4);
            at[0] = uint8_t(delta); at[1] = uint8_t(delta >> 8);
            at[2] = uint8_t(delta >> 16); at[3] = uint8_t(delta >> 24);
        }
        return !overflow_;
    }

    // ── encoding primitives ──────────────────────────────────────────────
    // `rmIsReg` says the r/m field holds a REGISTER rather than a memory
    // operand, and it is not a detail: without REX, byte encodings address
    // AH/CH/DH/BH for r/m values 4-7, and with REX they address SPL/BPL/
    // SIL/DIL. A byte operation on SIL or DIL that omits the prefix
    // therefore silently operates on the high half of RCX or RBX instead —
    // which is exactly the shape of bug that only surfaces a hundred
    // million instructions into a boot.
    void rex(Sz sz, int reg, int rmBase, bool rmIsReg = false) {
        uint8_t r = 0x40;
        if (sz == Sz::Q) r |= 0x08;
        if (reg & 8) r |= 0x04;
        if (rmBase & 8) r |= 0x01;
        const bool byteHigh = sz == Sz::B &&
                              ((reg >= 4 && reg < 8) ||
                               (rmIsReg && rmBase >= 4 && rmBase < 8));
        if (r != 0x40 || byteHigh) db(r);
    }
    void opsize(Sz sz) { if (sz == Sz::W) db(0x66); }

    // ModRM + SIB + displacement for [base + disp].
    void modrm(int reg, const Mem& m) {
        const int b = m.base & 7;
        const bool needSib = (b == 4);              // RSP / R12
        uint8_t mod;
        if (m.disp == 0 && b != 5) mod = 0;         // RBP/R13 always need a disp
        else if (m.disp >= -128 && m.disp <= 127) mod = 1;
        else mod = 2;
        db(uint8_t((mod << 6) | ((reg & 7) << 3) | (needSib ? 4 : b)));
        if (needSib) db(uint8_t(0x24));             // scale 0, no index, base
        if (mod == 1) db(uint8_t(m.disp));
        else if (mod == 2) dd(uint32_t(m.disp));
    }
    void modrm(int reg, Reg rm) { db(uint8_t(0xC0 | ((reg & 7) << 3) | (rm & 7))); }

    // ── moves ────────────────────────────────────────────────────────────
    void movRR(Sz sz, Reg dst, Reg src) {           // dst = src
        opsize(sz); rex(sz, src, dst, true);
        db(sz == Sz::B ? 0x88 : 0x89);
        modrm(src, dst);
    }
    void movRM(Sz sz, Reg dst, const Mem& m) {      // dst = [m]
        opsize(sz); rex(sz, dst, m.base);
        db(sz == Sz::B ? 0x8A : 0x8B);
        modrm(dst, m);
    }
    void movMR(Sz sz, const Mem& m, Reg src) {      // [m] = src
        opsize(sz); rex(sz, src, m.base);
        db(sz == Sz::B ? 0x88 : 0x89);
        modrm(src, m);
    }
    void movMI(Sz sz, const Mem& m, int32_t imm) {  // [m] = imm
        opsize(sz); rex(sz, 0, m.base);
        db(sz == Sz::B ? 0xC6 : 0xC7);
        modrm(0, m);
        if (sz == Sz::B) db(uint8_t(imm));
        else if (sz == Sz::W) dw(uint16_t(imm));
        else dd(uint32_t(imm));                     // Q: sign-extended imm32
    }
    void movRI(Reg dst, uint32_t imm) {             // dst = imm (32-bit, zeroes top)
        rex(Sz::L, 0, dst);
        db(uint8_t(0xB8 + (dst & 7)));
        dd(imm);
    }
    void movRI64(Reg dst, uint64_t imm) {
        rex(Sz::Q, 0, dst);
        db(uint8_t(0xB8 + (dst & 7)));
        dq(imm);
    }
    void movzx(Sz from, Reg dst, const Mem& m) {    // dst = zero-extend [m]
        rex(Sz::L, dst, m.base);
        db(0x0F); db(from == Sz::B ? 0xB6 : 0xB7);
        modrm(dst, m);
    }
    void movsx(Sz from, Reg dst, const Mem& m) {    // dst = sign-extend [m]
        rex(Sz::L, dst, m.base);
        db(0x0F); db(from == Sz::B ? 0xBE : 0xBF);
        modrm(dst, m);
    }
    void movzxRR(Sz from, Reg dst, Reg src) {
        rex(from == Sz::B ? Sz::B : Sz::L, dst, src, true);
        db(0x0F); db(from == Sz::B ? 0xB6 : 0xB7);
        modrm(dst, src);
    }
    void movsxRR(Sz from, Reg dst, Reg src) {
        rex(from == Sz::B ? Sz::B : Sz::L, dst, src, true);
        db(0x0F); db(from == Sz::B ? 0xBE : 0xBF);
        modrm(dst, src);
    }
    // Bit Scan Reverse: dst = index of the highest set bit of src.
    // UNDEFINED when src is zero — every caller branches around that case
    // (BFFFO's zero-field arm). Baseline x86-64; no LZCNT/ABM assumed.
    void bsrRR(Sz sz, Reg dst, Reg src) {
        opsize(sz); rex(sz, dst, src, true);
        db(0x0F); db(0xBD);
        modrm(dst, src);
    }
    // 32 -> 64 sign extension (movsxd), for guest addresses used as offsets.
    void movsxd(Reg dst, Reg src) {
        rex(Sz::Q, dst, src); db(0x63); modrm(dst, src);
    }
    void leaRM(Reg dst, const Mem& m) {
        rex(Sz::Q, dst, m.base); db(0x8D); modrm(dst, m);
    }
    // [base + index*scale + disp] — used for the data-window host pointer.
    void leaIdx(Reg dst, Reg base, Reg index, int scale, int32_t disp) {
        uint8_t r = 0x48;
        if (dst & 8) r |= 0x04;
        if (index & 8) r |= 0x02;
        if (base & 8) r |= 0x01;
        db(r); db(0x8D);
        const int s = scale == 8 ? 3 : scale == 4 ? 2 : scale == 2 ? 1 : 0;
        uint8_t mod = (disp == 0 && (base & 7) != 5) ? 0
                    : (disp >= -128 && disp <= 127) ? 1 : 2;
        db(uint8_t((mod << 6) | ((dst & 7) << 3) | 4));
        db(uint8_t((s << 6) | ((index & 7) << 3) | (base & 7)));
        if (mod == 1) db(uint8_t(disp));
        else if (mod == 2) dd(uint32_t(disp));
    }

    // ── ALU ──────────────────────────────────────────────────────────────
    // op codes below are the "/r, r to r/m" base opcodes.
    enum class Op : uint8_t { ADD = 0, OR = 1, ADC = 2, SBB = 3,
                              AND = 4, SUB = 5, XOR = 6, CMP = 7 };

    void aluMR(Op op, Sz sz, const Mem& m, Reg src) {          // [m] op= src
        opsize(sz); rex(sz, src, m.base);
        db(uint8_t(uint8_t(op) * 8 + (sz == Sz::B ? 0x00 : 0x01)));
        modrm(src, m);
    }
    void aluRM(Op op, Sz sz, Reg dst, const Mem& m) {          // dst op= [m]
        opsize(sz); rex(sz, dst, m.base);
        db(uint8_t(uint8_t(op) * 8 + (sz == Sz::B ? 0x02 : 0x03)));
        modrm(dst, m);
    }
    void aluRR(Op op, Sz sz, Reg dst, Reg src) {               // dst op= src
        opsize(sz); rex(sz, src, dst, true);
        db(uint8_t(uint8_t(op) * 8 + (sz == Sz::B ? 0x00 : 0x01)));
        modrm(src, dst);
    }
    void aluMI(Op op, Sz sz, const Mem& m, int32_t imm) {      // [m] op= imm
        const bool imm8 = sz != Sz::B && imm >= -128 && imm <= 127;
        opsize(sz); rex(sz, 0, m.base);
        db(sz == Sz::B ? 0x80 : imm8 ? 0x83 : 0x81);
        modrm(int(op), m);
        if (sz == Sz::B || imm8) db(uint8_t(imm));
        else if (sz == Sz::W) dw(uint16_t(imm));
        else dd(uint32_t(imm));
    }
    void aluRI(Op op, Sz sz, Reg dst, int32_t imm) {
        const bool imm8 = sz != Sz::B && imm >= -128 && imm <= 127;
        opsize(sz); rex(sz, 0, dst, true);
        db(sz == Sz::B ? 0x80 : imm8 ? 0x83 : 0x81);
        modrm(int(op), dst);
        if (sz == Sz::B || imm8) db(uint8_t(imm));
        else if (sz == Sz::W) dw(uint16_t(imm));
        else dd(uint32_t(imm));
    }

    void testMI(Sz sz, const Mem& m, int32_t imm) {
        opsize(sz); rex(sz, 0, m.base);
        db(sz == Sz::B ? 0xF6 : 0xF7);
        modrm(0, m);
        if (sz == Sz::B) db(uint8_t(imm));
        else if (sz == Sz::W) dw(uint16_t(imm));
        else dd(uint32_t(imm));
    }
    void testRI(Sz sz, Reg r, int32_t imm) {
        opsize(sz); rex(sz, 0, r, true);
        db(sz == Sz::B ? 0xF6 : 0xF7);
        modrm(0, r);
        if (sz == Sz::B) db(uint8_t(imm));
        else if (sz == Sz::W) dw(uint16_t(imm));
        else dd(uint32_t(imm));
    }
    void testRR(Sz sz, Reg a, Reg b) {
        opsize(sz); rex(sz, b, a, true);
        db(sz == Sz::B ? 0x84 : 0x85);
        modrm(b, a);
    }

    // ── unary ────────────────────────────────────────────────────────────
    void negM(Sz sz, const Mem& m) { unary(sz, m, 3); }
    void notM(Sz sz, const Mem& m) { unary(sz, m, 2); }
    void negR(Sz sz, Reg r) { unaryR(sz, r, 3); }
    void notR(Sz sz, Reg r) { unaryR(sz, r, 2); }
    void cdq() { db(0x99); }                         // EDX:EAX = signext(EAX)
    void cqo() { db(0x48); db(0x99); }               // RDX:RAX = signext(RAX)
    void divR(bool sign, Reg divisor) {             // EDX:EAX / r32
        rex(Sz::L, 0, divisor, true);
        db(0xF7); modrm(sign ? 7 : 6, divisor);
    }
    void divR64(bool sign, Reg divisor) {           // RDX:RAX / r64
        rex(Sz::Q, 0, divisor, true);
        db(0xF7); modrm(sign ? 7 : 6, divisor);
    }
    void incM(Sz sz, const Mem& m) {
        opsize(sz); rex(sz, 0, m.base); db(sz == Sz::B ? 0xFE : 0xFF); modrm(0, m);
    }

    // ── shifts ───────────────────────────────────────────────────────────
    // ext: 4 = SHL, 5 = SHR, 7 = SAR, 0 = ROL, 1 = ROR
    void shiftRI(Sz sz, Reg r, int ext, uint8_t count) {
        opsize(sz); rex(sz, 0, r, true);
        if (count == 1) { db(sz == Sz::B ? 0xD0 : 0xD1); modrm(ext, r); return; }
        db(sz == Sz::B ? 0xC0 : 0xC1); modrm(ext, r); db(count);
    }
    void shiftRCl(Sz sz, Reg r, int ext) {
        opsize(sz); rex(sz, 0, r, true);
        db(sz == Sz::B ? 0xD2 : 0xD3); modrm(ext, r);
    }

    void bswap(Reg r) { rex(Sz::L, 0, r); db(0x0F); db(uint8_t(0xC8 + (r & 7))); }
    void rolR16(Reg r, uint8_t n) { shiftRI(Sz::W, r, 0, n); }

    // ── flags ────────────────────────────────────────────────────────────
    void setccM(Cc cc, const Mem& m) {
        rex(Sz::B, 0, m.base);
        db(0x0F); db(uint8_t(0x90 + uint8_t(cc)));
        modrm(0, m);
    }
    void setccR(Cc cc, Reg r) {
        rex(Sz::B, 0, r, true);
        db(0x0F); db(uint8_t(0x90 + uint8_t(cc)));
        modrm(0, r);
    }
    void cmovccRM(Cc cc, Sz sz, Reg dst, const Mem& m) {
        opsize(sz); rex(sz, dst, m.base);
        db(0x0F); db(uint8_t(0x40 + uint8_t(cc)));
        modrm(dst, m);
    }

    // ── control flow ─────────────────────────────────────────────────────
    void jmp(Label& l)      { db(0xE9); rel32(l); }
    void jcc(Cc cc, Label& l) { db(0x0F); db(uint8_t(0x80 + uint8_t(cc))); rel32(l); }
    void jmpShort(Label& l)      { db(0xEB); rel8(l); }
    void jccShort(Cc cc, Label& l) { db(uint8_t(0x70 + uint8_t(cc))); rel8(l); }
    void callR(Reg r)       { rex(Sz::L, 0, r); db(0xFF); modrm(2, r); }
    void callM(const Mem& m) { rex(Sz::L, 0, m.base); db(0xFF); modrm(2, m); }
    // Indirect jump through memory — how a linked block exit reaches the
    // next block's entry without the engine in between.
    void jmpM(const Mem& m)  { rex(Sz::L, 0, m.base); db(0xFF); modrm(4, m); }
    void ret()              { db(0xC3); }

    void push(Reg r) { if (r & 8) db(0x41); db(uint8_t(0x50 + (r & 7))); }
    void pop(Reg r)  { if (r & 8) db(0x41); db(uint8_t(0x58 + (r & 7))); }

private:
    void unary(Sz sz, const Mem& m, int ext) {
        opsize(sz); rex(sz, 0, m.base); db(sz == Sz::B ? 0xF6 : 0xF7); modrm(ext, m);
    }
    void unaryR(Sz sz, Reg r, int ext) {
        opsize(sz); rex(sz, 0, r, true); db(sz == Sz::B ? 0xF6 : 0xF7); modrm(ext, r);
    }

    struct Fixup { int32_t at; Label* label; bool shrt; };
    void addFixup(Label& l, bool shrt) {
        if (nFixups_ >= kMaxFixups) { overflow_ = true; return; }
        fixups_[nFixups_].at = int32_t(size());
        fixups_[nFixups_].label = &l;
        fixups_[nFixups_].shrt = shrt;
        nFixups_++;
    }
    static constexpr int kMaxFixups = 4096;
    static constexpr int kMaxLabels = 2048;

    uint8_t* p_;
    uint8_t* base_;
    size_t   cap_;
    bool     overflow_ = false;
    Fixup    fixups_[kMaxFixups];
    int      nFixups_ = 0;
    Label    labels_[kMaxLabels];
    int      nLabels_ = 0;
};

}  // namespace jit::x64
