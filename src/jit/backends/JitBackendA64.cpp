// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── AArch64 code generator ───────────────────────────────────────────────
// Native 68040 ALU, MOVE, effective-address, control-flow and MOVEM paths,
// with the same exact per-instruction fallback contract as the x86-64 JIT.

#include "JitBackendA64.h"

#include "../JitCodeBuffer.h"
#include "../JitConfig.h"
#include "Moira.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace jit {
namespace {

using Layout = moira::Moira::PomJitLayout;

extern "C" void pom68kA64Sync(moira::Moira* cpu, uint32_t cycles) noexcept {
    cpu->pomJitSync(int(cycles));
}
extern "C" int pom68kA64Step(moira::Moira* cpu) noexcept {
    if (!cpu->pomJitCovers(cpu->getPC()) || !cpu->pomJitIdle()) return -1;
    return cpu->pomJitExecOne() ? 1 : 0;
}
extern "C" int pom68kA64Read(moira::Moira* cpu, uint32_t addr,
                              uint32_t bytes, uint32_t* out) noexcept {
    return cpu->pomJitReadData(addr, int(bytes), *out) ? 1 : 0;
}
extern "C" int pom68kA64Write(moira::Moira* cpu, uint32_t addr,
                               uint32_t bytes, uint32_t value) noexcept {
    return cpu->pomJitWriteData(addr, int(bytes), value) ? 1 : 0;
}

struct Frame {
    int64_t clockTarget;
    uint32_t instrs;
    uint32_t exit;
    void* dtlbSelf;
    uint8_t* (*dtlbFill)(void*, uint32_t, int);
    const uint8_t* guardHit;
    uint32_t scratch;
    uint32_t saveA;
    uint32_t saveV;
    uint32_t slowInstrs;
    const void* periphClock;
    void* linkTable;
    uint32_t value;
    uint32_t pad;
    uint64_t* slowStaticHisto;
    uint64_t* slowRuntimeHisto;
    int64_t savedClock;
    WriteObserver observeWrite;
    void* observeWriteSelf;
    uint64_t observedHostPointer;
};
static_assert(offsetof(Frame, linkTable) == 64);
static_assert(offsetof(Frame, slowStaticHisto) == 80);
static_assert(offsetof(Frame, slowRuntimeHisto) == 88);
static_assert(offsetof(Frame, savedClock) == 96);
static_assert(offsetof(Frame, observeWrite) == 104);
static_assert(offsetof(Frame, observeWriteSelf) == 112);
static_assert(offsetof(Frame, observedHostPointer) == 120);

// Minimal fixed-width assembler. Every memory operand is an unsigned scaled
// immediate off x0 (Moira*) or x1 (Frame*); layout() validates the offsets.
class Asm {
public:
    enum Cond : uint32_t { EQ = 0, NE = 1, CS = 2, CC = 3, HI = 8,
                           GE = 10, LT = 11 };
    struct Fix { size_t at; int label; bool conditional; uint32_t cond; };
    struct Mark { size_t code, labels, fixes; };

    Mark mark() const { return {code_.size(), labels_.size(), fixes_.size()}; }
    void rewind(Mark m) {
        code_.resize(m.code); labels_.resize(m.labels); fixes_.resize(m.fixes);
    }

    int label() { labels_.push_back(size_t(-1)); return int(labels_.size() - 1); }
    void bind(int l) { labels_[size_t(l)] = code_.size(); }
    void emit(uint32_t w) { code_.push_back(w); }

    void bCond(Cond c, int l) {
        fixes_.push_back({code_.size(), l, true, uint32_t(c)});
        emit(0x54000000u | uint32_t(c));
    }
    void b(int l) {
        fixes_.push_back({code_.size(), l, false, 0});
        emit(0x14000000u);
    }

    void movW(unsigned rd, uint32_t v) {
        emit(0x52800000u | ((v & 0xFFFFu) << 5) | rd);
        if (v >> 16) emit(0x72A00000u | ((v >> 16) << 5) | rd);
    }
    void movX(unsigned rd, uint64_t v) {
        emit(0xD2800000u | (uint32_t(v & 0xFFFFu) << 5) | rd);
        for (unsigned hw = 1; hw < 4; hw++) {
            const uint32_t part = uint32_t((v >> (16 * hw)) & 0xFFFFu);
            if (part)
                emit(0xF2800000u | (hw << 21) | (part << 5) | rd);
        }
    }
    void address(unsigned rd, unsigned rn, uint32_t off) {
        if (off < 4096) {
            emit(0x91000000u | (off << 10) | (rn << 5) | rd); // ADD imm
        } else {
            movX(rd, off);
            emit(0x8B000000u | (rd << 16) | (rn << 5) | rd);  // ADD reg
        }
    }
    void ldrX(unsigned rt, unsigned rn, uint32_t off) {
        if ((off & 7) || off / 8 >= 4096) {
            address(15, rn, off); emit(0xF9400000u | (15 << 5) | rt); return;
        }
        emit(0xF9400000u | ((off / 8) << 10) | (rn << 5) | rt);
    }
    void ldrW(unsigned rt, unsigned rn, uint32_t off) {
        if ((off & 3) || off / 4 >= 4096) {
            address(15, rn, off); emit(0xB9400000u | (15 << 5) | rt); return;
        }
        emit(0xB9400000u | ((off / 4) << 10) | (rn << 5) | rt);
    }
    void ldrH(unsigned rt, unsigned rn, uint32_t off) {
        if ((off & 1) || off / 2 >= 4096) {
            address(15, rn, off); emit(0x79400000u | (15 << 5) | rt); return;
        }
        emit(0x79400000u | ((off / 2) << 10) | (rn << 5) | rt);
    }
    void ldrB(unsigned rt, unsigned rn, uint32_t off) {
        if (off >= 4096) {
            address(15, rn, off); emit(0x39400000u | (15 << 5) | rt); return;
        }
        emit(0x39400000u | (off << 10) | (rn << 5) | rt);
    }
    void strW(unsigned rt, unsigned rn, uint32_t off) {
        if ((off & 3) || off / 4 >= 4096) {
            address(15, rn, off); emit(0xB9000000u | (15 << 5) | rt); return;
        }
        emit(0xB9000000u | ((off / 4) << 10) | (rn << 5) | rt);
    }
    void strX(unsigned rt, unsigned rn, uint32_t off) {
        if ((off & 7) || off / 8 >= 4096) {
            address(15, rn, off); emit(0xF9000000u | (15 << 5) | rt); return;
        }
        emit(0xF9000000u | ((off / 8) << 10) | (rn << 5) | rt);
    }
    void strH(unsigned rt, unsigned rn, uint32_t off) {
        if ((off & 1) || off / 2 >= 4096) {
            address(15, rn, off); emit(0x79000000u | (15 << 5) | rt); return;
        }
        emit(0x79000000u | ((off / 2) << 10) | (rn << 5) | rt);
    }
    void strB(unsigned rt, unsigned rn, uint32_t off) {
        if (off >= 4096) {
            address(15, rn, off); emit(0x39000000u | (15 << 5) | rt); return;
        }
        emit(0x39000000u | (off << 10) | (rn << 5) | rt);
    }
    void cmpX(unsigned rn, unsigned rm) {
        emit(0xEB00001Fu | (rm << 16) | (rn << 5));
    }
    void cmpW(unsigned rn, unsigned rm) {
        emit(0x6B00001Fu | (rm << 16) | (rn << 5));
    }
    void cmpWZero(unsigned rn) { emit(0x7100001Fu | (rn << 5)); }
    void addW(unsigned rd, unsigned rn, unsigned rm) {
        emit(0x0B000000u | (rm << 16) | (rn << 5) | rd);
    }
    void subW(unsigned rd, unsigned rn, unsigned rm) {
        emit(0x4B000000u | (rm << 16) | (rn << 5) | rd);
    }
    void subX(unsigned rd, unsigned rn, unsigned rm) {
        emit(0xCB000000u | (rm << 16) | (rn << 5) | rd);
    }
    void addImmW(unsigned rd, unsigned rn, unsigned imm) {
        emit(0x11000000u | ((imm & 0xFFFu) << 10) | (rn << 5) | rd);
    }
    void addImmX(unsigned rd, unsigned rn, unsigned imm) {
        emit(0x91000000u | ((imm & 0xFFFu) << 10) | (rn << 5) | rd);
    }
    void subImmX(unsigned rd, unsigned rn, unsigned imm) {
        emit(0xD1000000u | ((imm & 0xFFFu) << 10) | (rn << 5) | rd);
    }
    void subImmW(unsigned rd, unsigned rn, unsigned imm) {
        emit(0x51000000u | ((imm & 0xFFFu) << 10) | (rn << 5) | rd);
    }
    void addX(unsigned rd, unsigned rn, unsigned rm) {
        emit(0x8B000000u | (rm << 16) | (rn << 5) | rd);
    }
    void movRegW(unsigned rd, unsigned rn) {
        emit(0x2A0003E0u | (rn << 16) | rd);
    }
    void movRegX(unsigned rd, unsigned rn) {
        emit(0xAA0003E0u | (rn << 16) | rd);
    }
    void andW(unsigned rd, unsigned rn, unsigned rm) {
        emit(0x0A000000u | (rm << 16) | (rn << 5) | rd);
    }
    void orrW(unsigned rd, unsigned rn, unsigned rm) {
        emit(0x2A000000u | (rm << 16) | (rn << 5) | rd);
    }
    void eorW(unsigned rd, unsigned rn, unsigned rm) {
        emit(0x4A000000u | (rm << 16) | (rn << 5) | rd);
    }
    void mvnW(unsigned rd, unsigned rm) {
        emit(0x2A2003E0u | (rm << 16) | rd);
    }
    void lsrW(unsigned rd, unsigned rn, unsigned shift) {
        emit(0x53000000u | (shift << 16) | (31u << 10) | (rn << 5) | rd);
    }
    void lslW(unsigned rd, unsigned rn, unsigned shift) {
        const unsigned immr = (32 - shift) & 31;
        const unsigned imms = 31 - shift;
        emit(0x53000000u | (immr << 16) | (imms << 10) | (rn << 5) | rd);
    }
    void asrW(unsigned rd, unsigned rn, unsigned shift) {
        emit(0x13000000u | (shift << 16) | (31u << 10) | (rn << 5) | rd);
    }
    void lsrX(unsigned rd, unsigned rn, unsigned shift) {
        emit(0xD3400000u | (shift << 16) | (63u << 10) | (rn << 5) | rd);
    }
    void lslX(unsigned rd, unsigned rn, unsigned shift) {
        const unsigned immr = (64 - shift) & 63;
        const unsigned imms = 63 - shift;
        emit(0xD3400000u | (immr << 16) | (imms << 10) | (rn << 5) | rd);
    }
    void lslVarW(unsigned rd, unsigned rn, unsigned rm) {
        emit(0x1AC02000u | (rm << 16) | (rn << 5) | rd);
    }
    void sxtB(unsigned rd, unsigned rn) {
        emit(0x13001C00u | (rn << 5) | rd);
    }
    void sxtH(unsigned rd, unsigned rn) {
        emit(0x13003C00u | (rn << 5) | rd);
    }
    void rorW(unsigned rd, unsigned rn, unsigned shift) {
        emit(0x13800000u | (rn << 16) | (shift << 10) | (rn << 5) | rd);
    }
    void rev16W(unsigned rd, unsigned rn) {
        emit(0x5AC00400u | (rn << 5) | rd);
    }
    void rev32W(unsigned rd, unsigned rn) {
        emit(0x5AC00800u | (rn << 5) | rd);
    }
    void clzW(unsigned rd, unsigned rn) {
        emit(0x5AC01000u | (rn << 5) | rd);
    }
    void cbnzW(unsigned rt, int l) {
        fixes_.push_back({code_.size(), l, true, 16});
        emit(0x35000000u | rt);
    }
    void cbzW(unsigned rt, int l) {
        fixes_.push_back({code_.size(), l, true, 17});
        emit(0x34000000u | rt);
    }
    void cbzX(unsigned rt, int l) {
        fixes_.push_back({code_.size(), l, true, 18});
        emit(0xB4000000u | rt);
    }
    void blr(unsigned rn) { emit(0xD63F0000u | (rn << 5)); }
    void br(unsigned rn) { emit(0xD61F0000u | (rn << 5)); }

    bool finish() {
        for (const Fix& f : fixes_) {
            if (f.label < 0 || size_t(f.label) >= labels_.size() ||
                labels_[size_t(f.label)] == size_t(-1)) return false;
            const int64_t d = int64_t(labels_[size_t(f.label)]) - int64_t(f.at);
            if (f.conditional) {
                if (d < -(1 << 18) || d >= (1 << 18)) return false;
                const uint32_t imm = uint32_t(d) & 0x7FFFFu;
                if (f.cond == 16) code_[f.at] = 0x35000000u | (imm << 5) | 9u;
                else if (f.cond == 17) code_[f.at] = 0x34000000u | (imm << 5) | 9u;
                else if (f.cond == 18) code_[f.at] = 0xB4000000u | (imm << 5) | 14u;
                else code_[f.at] = 0x54000000u | (imm << 5) | f.cond;
            } else {
                if (d < -(1 << 25) || d >= (1 << 25)) return false;
                code_[f.at] = 0x14000000u | (uint32_t(d) & 0x03FFFFFFu);
            }
        }
        return true;
    }

    const uint8_t* bytes() const {
        return reinterpret_cast<const uint8_t*>(code_.data());
    }
    size_t byteSize() const { return code_.size() * sizeof(uint32_t); }

private:
    std::vector<uint32_t> code_;
    std::vector<size_t> labels_;
    std::vector<Fix> fixes_;
};

struct IcacheShadow {
    uint32_t tag[16] {};
    uint8_t valid[16] {};
    bool seen[16] {};
};

// Compact emitted 68030 i-cache model. Fetches and optimistic hits are
// aggregated once per instruction; each actual miss corrects the hit count,
// updates its compile-time-known line and adds the runtime miss penalty.
void chargeIcache(Asm& a, const Layout& L, const BlockIr& ir,
                  const Instr& in, IcacheShadow& shadow,
                  uint32_t exactFetchWords = 0) {
    const uint32_t words = exactFetchWords ? exactFetchWords
                                           : uint32_t(in.words) + 1;
    const uint32_t sup = ir.super ? 0x80000000u : 0u;
    a.ldrX(9, 0, L.icFetches); a.addImmX(9, 9, words); a.strX(9, 0, L.icFetches);

    const int disabled = a.label();
    a.ldrB(9, 0, L.cacr); a.movW(10, 1); a.andW(9, 9, 10); a.cbzW(9, disabled);
    a.ldrX(9, 0, L.icHits); a.addImmX(9, 9, words); a.strX(9, 0, L.icHits);

    for (uint32_t w = 0; w < words; w++) {
        const uint32_t addr = in.pc + w * 2;
        const int line = int((addr >> 4) & 15);
        const uint32_t tag = (addr >> 8) | sup;
        const uint8_t bit = uint8_t(1u << ((addr >> 2) & 3));
        if (shadow.seen[line] && shadow.tag[line] == tag &&
            (shadow.valid[line] & bit)) continue;

        const int miss = a.label(), done = a.label(), sameTag = a.label();
        a.ldrW(9, 0, L.icTag + uint32_t(line) * 4); a.movW(10, tag);
        a.cmpW(9, 10); a.bCond(Asm::NE, miss);
        a.ldrB(9, 0, L.icValid + uint32_t(line)); a.movW(10, bit);
        a.andW(9, 9, 10); a.cbnzW(9, done);

        a.bind(miss);
        a.ldrW(9, 0, L.icTag + uint32_t(line) * 4); a.movW(10, tag);
        a.cmpW(9, 10); a.bCond(Asm::EQ, sameTag);
        a.strW(10, 0, L.icTag + uint32_t(line) * 4);
        a.movW(9, 0); a.strB(9, 0, L.icValid + uint32_t(line));
        a.bind(sameTag);
        a.ldrB(9, 0, L.icValid + uint32_t(line)); a.movW(10, bit);
        a.orrW(9, 9, 10); a.strB(9, 0, L.icValid + uint32_t(line));
        a.ldrX(9, 0, L.icHits); a.subImmX(9, 9, 1); a.strX(9, 0, L.icHits);
        a.ldrX(9, 0, L.icMisses); a.addImmX(9, 9, 1); a.strX(9, 0, L.icMisses);
        a.ldrW(9, 0, L.icPenalty); a.addX(21, 21, 9);
        a.bind(done);

        if (!shadow.seen[line] || shadow.tag[line] != tag) {
            shadow.tag[line] = tag; shadow.valid[line] = 0; shadow.seen[line] = true;
        }
        shadow.valid[line] |= bit;
    }
    a.bind(disabled);
}

void unchargeIcache(Asm& a, const Layout& L, const Instr& in) {
    const uint32_t words = uint32_t(in.words) + 1;
    a.ldrX(9, 0, L.icFetches); a.subImmX(9, 9, words); a.strX(9, 0, L.icFetches);
    const int disabled = a.label();
    a.ldrB(9, 0, L.cacr); a.movW(10, 1); a.andW(9, 9, 10); a.cbzW(9, disabled);
    a.ldrX(9, 0, L.icHits); a.subImmX(9, 9, words); a.strX(9, 0, L.icHits);
    a.bind(disabled);
}

enum class AluOp { Or, And, Eor, Add, Sub, Cmp };

uint32_t regOff(const Layout& L, bool address, unsigned n) {
    return (address ? L.a : L.d) + n * 4;
}

void loadSized(Asm& a, const Layout& L, unsigned rd, bool address,
               unsigned n, int bits) {
    const uint32_t o = regOff(L, address, n);
    if (bits == 8) a.ldrB(rd, 0, o);
    else if (bits == 16) a.ldrH(rd, 0, o);
    else a.ldrW(rd, 0, o);
}

void storeSized(Asm& a, const Layout& L, unsigned rs, bool address,
                unsigned n, int bits) {
    const uint32_t o = regOff(L, address, n);
    if (bits == 8) a.strB(rs, 0, o);
    else if (bits == 16) a.strH(rs, 0, o);
    else a.strW(rs, 0, o);
}

void maskResult(Asm& a, unsigned r, int bits) {
    if (bits == 32) return;
    a.movW(12, bits == 8 ? 0xFFu : 0xFFFFu);
    a.andW(r, r, 12);
}

void emitNz(Asm& a, const Layout& L, unsigned result, int bits) {
    // The result is already masked to its guest width.
    a.lsrW(12, result, unsigned(bits - 1));
    a.strB(12, 0, L.srN);
    a.movW(12, 0);
    a.strB(12, 0, L.srZ);
    const int nonzero = a.label();
    a.cmpWZero(result);
    a.bCond(Asm::NE, nonzero);
    a.movW(12, 1);
    a.strB(12, 0, L.srZ);
    a.bind(nonzero);
}

void emitLogicFlags(Asm& a, const Layout& L, unsigned result, int bits) {
    emitNz(a, L, result, bits);
    a.movW(12, 0);
    a.strB(12, 0, L.srV);
    a.strB(12, 0, L.srC);
}

// a=w9, b=w10, result=w11, all zero-extended/masked to `bits`.
void emitAddSubFlags(Asm& a, const Layout& L, int bits, bool sub, bool setX) {
    emitNz(a, L, 11, bits);

    // V: add ~(a^b)&(a^r), sub (a^b)&(a^r), sign bit selected last.
    a.eorW(12, 9, 10);
    if (!sub) a.mvnW(12, 12);
    a.eorW(13, 9, 11);
    a.andW(12, 12, 13);
    a.lsrW(12, 12, unsigned(bits - 1));
    a.strB(12, 0, L.srV);

    if (!sub) {
        a.addX(12, 9, 10);
        a.lsrX(12, 12, unsigned(bits));
        a.strB(12, 0, L.srC);
        if (setX) a.strB(12, 0, L.srX);
    } else {
        // AArch64 C means no borrow; 68k C/X mean borrow.
        a.movW(12, 0);
        a.strB(12, 0, L.srC);
        if (setX) a.strB(12, 0, L.srX);
        const int noBorrow = a.label();
        const int done = a.label();
        a.cmpW(9, 10);
        a.bCond(Asm::CS, noBorrow);
        a.movW(12, 1);
        a.strB(12, 0, L.srC);
        if (setX) a.strB(12, 0, L.srX);
        a.b(done);
        a.bind(noBorrow);
        a.bind(done);
    }
}

bool aluDirectionA64(uint16_t op, int& direction) {
    const int hi = (op >> 12) & 0xF;
    const int opmode = (op >> 6) & 7;
    const int mode = (op >> 3) & 7;
    if (opmode == 3 || opmode == 7) {
        direction = (hi == 0x9 || hi == 0xB || hi == 0xD) ? 0 : -1;
        return direction >= 0;
    }
    if (opmode <= 2) { direction = 0; return true; }
    direction = (mode <= 1) ? ((hi == 0xB && mode == 0) ? 1 : -1) : 1;
    return direction >= 0;
}

enum EaIndex { E_DN, E_AN, E_AI, E_PI, E_PD, E_DI, E_IX,
               E_AW, E_AL, E_DIPC, E_IXPC, E_IM, E_COUNT };

int eaIndexA64(int mode, int reg) {
    if (mode < 7) return mode;
    switch (reg) {
        case 0: return E_AW;
        case 1: return E_AL;
        case 2: return E_DIPC;
        case 3: return E_IXPC;
        case 4: return E_IM;
        default: return -1;
    }
}

const int8_t kEaReadA64[E_COUNT][3] = {
    {2,2,2}, {2,2,2}, {6,6,6}, {6,6,6}, {7,7,7}, {7,7,7},
    {9,9,9}, {6,6,6}, {6,6,6}, {7,7,7}, {9,9,9}, {4,4,6}
};
const int8_t kMoveDstA64[E_COUNT] = {0,0,2,2,3,3,-1,3,4,-1,-1,-1};

struct Ea {
    int idx = -1, reg = 0, ext = 0;
    int32_t value = 0;
    uint32_t base = 0;
    int ixReg = 0, ixShift = 0;
    bool ixLong = false;
    bool memory = false;
};

bool decodeEa(const BlockIr& ir, const Instr& in, int mode, int reg,
              int bits, int extAt, Ea& ea) {
    ea.idx = eaIndexA64(mode, reg); ea.reg = reg;
    if (ea.idx < 0) return false;
    ea.memory = ea.idx >= E_AI && ea.idx != E_IM;
    const uint32_t extPc = in.pc + 2 + uint32_t(extAt) * 2;
    switch (ea.idx) {
        case E_DN: case E_AN: case E_AI: case E_PI: case E_PD:
            return true;
        case E_DI: ea.value = int16_t(ir.word(extPc)); ea.ext = 1; return true;
        case E_IX: case E_IXPC: {
            const uint16_t x = ir.word(extPc);
            if (x & 0x0100) return false;            // full 68020 format later
            ea.value = int8_t(x & 0xFF);
            ea.base = extPc;
            ea.ixReg = ((x >> 12) & 7) + ((x & 0x8000) ? 8 : 0);
            ea.ixLong = (x & 0x0800) != 0;
            ea.ixShift = (x >> 9) & 3;
            ea.ext = 1;
            return true;
        }
        case E_AW: ea.value = int16_t(ir.word(extPc)); ea.ext = 1; return true;
        case E_AL:
            ea.value = int32_t(uint32_t(ir.word(extPc)) << 16 | ir.word(extPc + 2));
            ea.ext = 2; return true;
        case E_DIPC:
            ea.value = int32_t(extPc + uint32_t(int16_t(ir.word(extPc))));
            ea.ext = 1; return true;
        case E_IM:
            if (bits == 32) {
                ea.value = int32_t(uint32_t(ir.word(extPc)) << 16 | ir.word(extPc + 2));
                ea.ext = 2;
            } else {
                ea.value = bits == 8 ? int8_t(ir.word(extPc) & 0xFF)
                                     : int16_t(ir.word(extPc));
                ea.ext = 1;
            }
            return true;
        default: return false;
    }
}

bool canEmitReg(uint16_t op) {
    if (op == 0x4E71 || ((op & 0xF100) == 0x7000)) return true;
    const int mode = (op >> 3) & 7;
    switch (op & 0xF000) {
        case 0x0000: { // immediate ALU to Dn
            if ((op & 0xF100) == 0x0100 || (op & 0xFF00) == 0x0800) {
                const int ei = eaIndexA64(mode, op & 7);
                const int action = (op >> 6) & 3;
                return mode != 1 && ei >= 0 && ei != E_AN && ei != E_IM &&
                       (action == 0 || ei != E_DIPC);
            }
            const int kind = (op >> 9) & 7, sz = (op >> 6) & 3;
            const int ei = eaIndexA64(mode, op & 7);
            return ei >= 0 && ei != E_AN && ei != E_IM && ei != E_DIPC && sz <= 2 &&
                   (kind == 0 || kind == 1 || kind == 2 || kind == 3 ||
                    kind == 5 || kind == 6);
        }
        case 0x1000: case 0x2000: case 0x3000: {
            const int dm = (op >> 6) & 7, dr = (op >> 9) & 7;
            const int si = eaIndexA64(mode, op & 7), di = eaIndexA64(dm, dr);
            if (si < 0 || di < 0 || di == E_IM || di == E_DIPC) return false;
            if ((op & 0xF000) == 0x1000 && (mode == 1 || dm == 1)) return false;
            return true;
        }
        case 0x4000:
            if ((op & 0xFFF8) == 0x4E50 || (op & 0xFFF8) == 0x4E58) return true;
            if (op == 0x4E75) return true;
            if ((op & 0xFF80) == 0x4E80) {
                const int ei = eaIndexA64(mode, op & 7);
                return ei == E_AI || ei == E_DI || ei == E_AW ||
                       ei == E_AL || ei == E_DIPC;
            }
            if ((op & 0xF1C0) == 0x41C0) {
                const int ei = eaIndexA64(mode, op & 7);
                return ei == E_AI || ei == E_DI || ei == E_AW ||
                       ei == E_AL || ei == E_DIPC;
            }
            if ((op & 0xFB80) == 0x4880 && mode >= 2) return true;
            if ((op & 0xFFB8) == 0x4880 || (op & 0xFFF8) == 0x4840) return true;
            return eaIndexA64(mode, op & 7) >= 0 &&
                   ((op & 0xFF00) == 0x4A00 || (op & 0xFF00) == 0x4200 ||
                    (op & 0xFF00) == 0x4400 || (op & 0xFF00) == 0x4600) &&
                   ((op >> 6) & 3) <= 2;
        case 0x5000:
            if ((op & 0xF0F8) == 0x50C8) return true;
            return eaIndexA64(mode, op & 7) >= 0 &&
                   eaIndexA64(mode, op & 7) != E_IM &&
                   eaIndexA64(mode, op & 7) != E_DIPC &&
                   (op & 0x00C0) != 0x00C0;
        case 0x6000:
            return true;
        case 0x8000: case 0x9000: case 0xB000: case 0xC000: case 0xD000: {
            int direction = -1;
            return eaIndexA64(mode, op & 7) >= 0 && aluDirectionA64(op, direction);
        }
        case 0xE000: {
            if ((op & 0xF8C0) == 0xE8C0) return mode == 0; // register bitfield
            const int sz = (op >> 6) & 3, type = (op >> 3) & 3;
            return sz <= 2 && !(op & 0x20) && type != 2; // immediate, no ROX yet
        }
        default: return false;
    }
}

// Narrow 68030 restartable-write family. These MOVE forms have one data
// access and no source side effect.
// A direct DTLB hit is guaranteed-faultless by dataSpan(); every refusal is
// attempted through Moira's exact access thunk and replayed untouched if it
// faults. Brief indexed and -(An) destinations are included only because the
// injected-fault and successful-access gates prove their observable state.
// Full 68020 indexed extensions remain rejected by decodeEa().
bool restartWrite030(uint16_t op) {
    const uint16_t line = op & 0xF000;
    if (line != 0x1000 && line != 0x2000 && line != 0x3000) return false;
    const int sm = (op >> 3) & 7;
    const int dm = (op >> 6) & 7;
    const int sr = op & 7;
    const int dr = (op >> 9) & 7;
    const int si = eaIndexA64(sm, sr);
    const int di = eaIndexA64(dm, dr);
    const bool sourceValue = si == E_DN || si == E_AN || si == E_IM;
    const bool provedDestination = di == E_AI || di == E_PI || di == E_PD ||
                                   di == E_DI || di == E_IX || di == E_AW ||
                                   di == E_AL;
    return sourceValue && provedDestination &&
           !(line == 0x1000 && si == E_AN);
}

int bitsForSizeIndex(int sz) { return sz == 0 ? 8 : sz == 1 ? 16 : 32; }

bool emitAluResult(Asm& a, const Layout& L, AluOp kind, int bits,
                   bool store, bool addressDst, unsigned dst, bool setX) {
    switch (kind) {
        case AluOp::Or:  a.orrW(11, 9, 10); break;
        case AluOp::And: a.andW(11, 9, 10); break;
        case AluOp::Eor: a.eorW(11, 9, 10); break;
        case AluOp::Add: a.addW(11, 9, 10); break;
        case AluOp::Sub: case AluOp::Cmp: a.subW(11, 9, 10); break;
    }
    maskResult(a, 11, bits);
    if (store) storeSized(a, L, 11, addressDst, dst, bits);
    if (kind == AluOp::Add)
        emitAddSubFlags(a, L, bits, false, setX);
    else if (kind == AluOp::Sub || kind == AluOp::Cmp)
        emitAddSubFlags(a, L, bits, true, setX);
    else
        emitLogicFlags(a, L, 11, bits);
    return true;
}

uint32_t immediateValue(const BlockIr& ir, const Instr& in, int bits) {
    if (bits == 32)
        return uint32_t(ir.word(in.pc + 2)) << 16 | ir.word(in.pc + 4);
    return bits == 8 ? uint32_t(ir.word(in.pc + 2) & 0xFF)
                     : uint32_t(ir.word(in.pc + 2));
}

void addrOf(Asm& a, const Layout& L, const Ea& ea, int bits) {
    const unsigned step = ea.reg == 7 && bits == 8 ? 2u : unsigned(bits / 8);
    switch (ea.idx) {
        case E_AI: case E_PI:
            a.ldrW(9, 0, L.a + unsigned(ea.reg) * 4); break;
        case E_PD:
            a.ldrW(9, 0, L.a + unsigned(ea.reg) * 4);
            a.subImmW(9, 9, step); break;
        case E_DI:
            a.ldrW(9, 0, L.a + unsigned(ea.reg) * 4);
            a.movW(10, uint32_t(ea.value)); a.addW(9, 9, 10); break;
        case E_IX: case E_IXPC:
            if (ea.idx == E_IX) a.ldrW(9, 0, L.a + unsigned(ea.reg) * 4);
            else a.movW(9, ea.base);
            if (ea.ixReg < 8) a.ldrW(10, 0, L.d + unsigned(ea.ixReg) * 4);
            else a.ldrW(10, 0, L.a + unsigned(ea.ixReg - 8) * 4);
            if (!ea.ixLong) a.sxtH(10, 10);
            if (ea.ixShift) a.lslW(10, 10, unsigned(ea.ixShift));
            a.addW(9, 9, 10);
            if (ea.value) {
                a.movW(10, uint32_t(ea.value)); a.addW(9, 9, 10);
            }
            break;
        case E_AW: case E_AL: case E_DIPC:
            a.movW(9, uint32_t(ea.value)); break;
        default: break;
    }
}

void commitEa(Asm& a, const Layout& L, const Ea& ea, int bits) {
    const unsigned step = ea.reg == 7 && bits == 8 ? 2u : unsigned(bits / 8);
    if (ea.idx != E_PI && ea.idx != E_PD) return;
    // w10 commonly still holds the source operand (ADDA/SUBA/CMPA and
    // register-destination ALU).  Clobbering it here made (An)+/-(An)
    // consume the updated address as data.  w13 is dead at every commit
    // point; bit operations that use it have already stored their result.
    a.ldrW(13, 0, L.a + unsigned(ea.reg) * 4);
    if (ea.idx == E_PI) a.addImmW(13, 13, step);
    else a.subImmW(13, 13, step);
    a.strW(13, 0, L.a + unsigned(ea.reg) * 4);
}

// The 68030 updates (An)+ before the data access. Generated code still has
// to keep its bailout boundary pristine, so the update happens only after a
// DTLB translation/probe has succeeded (or immediately before an exact
// access thunk). A real fault is replayed by Moira, which then constructs the
// format-A/B restart state itself.
void commitEaBeforeAccess(Asm& a, const Layout& L, const Ea& ea, int bits) {
    // Predecrement precedes the access on every core. Postincrement reads
    // differ by model; the mode-5 030 performs it before the access.
    if (ea.idx == E_PD || (L.is030 && ea.idx == E_PI))
        commitEa(a, L, ea, bits);
}

void commitEaAfterAccess(Asm& a, const Layout& L, const Ea& ea, int bits) {
    if (ea.idx == E_PD || (L.is030 && ea.idx == E_PI)) return;
    commitEa(a, L, ea, bits);
}

void rollbackEaBeforeAccess(Asm& a, const Layout& L, const Ea& ea, int bits) {
    if (ea.idx != E_PD && (!L.is030 || ea.idx != E_PI)) return;
    const unsigned step = ea.reg == 7 && bits == 8 ? 2u : unsigned(bits / 8);
    a.ldrW(13, 0, L.a + unsigned(ea.reg) * 4);
    if (ea.idx == E_PI) a.subImmW(13, 13, step);
    else a.addImmW(13, 13, step);
    a.strW(13, 0, L.a + unsigned(ea.reg) * 4);
}

void loadGuest(Asm& a, int bits, unsigned rd);

// Refuse a write whose first or last byte overlaps a 256-byte slice holding
// translated code. `entry` is a PomJitDtlbEntry*, `pageOff` is addr&4095.
// A JIT access is at most one MOVEM burst (64 bytes), hence at most two
// slices need testing.
void guardCodeSlices(Asm& a, unsigned entry, unsigned pageOff, int bytes,
                     int miss) {
    const int clear = a.label();
    a.ldrW(12, entry, 4);                // PomJitDtlbEntry::codeMask
    a.cbzW(12, clear);
    for (int end = 0; end < (bytes > 1 ? 2 : 1); end++) {
        if (end) a.addImmW(10, pageOff, unsigned(bytes - 1));
        else a.movRegW(10, pageOff);
        a.lsrW(10, 10, moira::Moira::PomJitDtlb::kSliceShift);
        a.movW(11, 1);
        a.lslVarW(11, 11, 10);
        a.andW(10, 12, 11);
        a.cbnzW(10, miss);
    }
    a.bind(clear);
}

// Translate guest address w9. On success x14 is the host byte pointer; a
// refused mapping or cross-page access transfers to the untouched slow path.
void memProbe(Asm& a, const Layout& L, bool super, int bytes, bool write,
              int miss) {
    const int fill = a.label(), have = a.label(), done = a.label();
    a.lsrW(10, 9, 12);                 // logical page
    a.movRegW(11, 10);                 // tag
    if (super) {
        a.movW(12, 0x80000000u); a.orrW(11, 11, 12);
    }
    a.movW(12, moira::Moira::PomJitDtlb::kEntries - 1);
    a.andW(10, 10, 12);
    a.lslX(10, 10, 4);                 // entry index * 16
    a.address(14, 0, write ? L.dtlbW : L.dtlbR);
    a.addX(14, 14, 10);
    a.ldrW(12, 14, 0);
    a.cmpW(11, 12);
    a.bCond(Asm::NE, fill);

    a.bind(have);
    a.movW(12, 4095); a.andW(13, 9, 12);
    if (bytes > 1) {
        a.movW(12, unsigned(4096 - bytes));
        a.cmpW(13, 12); a.bCond(Asm::HI, miss);
    }
    // Read codeMask before x14 stops being the entry pointer and becomes
    // the host pointer. This is the arm64 half of the 256-byte write guard.
    if (write) guardCodeSlices(a, 14, 13, bytes, miss);
    a.ldrX(14, 14, 8);
    a.cbzX(14, miss);
    a.addX(14, 14, 13);
    a.b(done);

    a.bind(fill);
    // Preserve the guest address and fetch every call operand before x1 is
    // repurposed from Frame* to the thunk's integer argument.
    a.strW(9, 1, 40);
    a.ldrX(16, 1, 24);
    a.ldrX(0, 1, 16);
    a.movRegW(1, 9);
    a.movW(2, write ? 1u : 0u);
    a.blr(16);
    a.movRegX(14, 0);
    a.emit(0xA94207E0u);                // ldp x0,x1,[sp,#32]
    a.ldrW(9, 1, 40);
    a.cbzX(14, miss);
    a.movW(12, 4095); a.andW(13, 9, 12);
    if (bytes > 1) {
        a.movW(12, unsigned(4096 - bytes));
        a.cmpW(13, 12); a.bCond(Asm::HI, miss);
    }
    if (write) {
        // The fill thunk returns only the host pointer. Recompute the entry
        // address so its freshly installed codeMask is checked as well.
        a.lsrW(10, 9, 12);
        a.movW(12, moira::Moira::PomJitDtlb::kEntries - 1);
        a.andW(10, 10, 12);
        a.lslX(10, 10, 4);
        a.address(15, 0, L.dtlbW);
        a.addX(15, 15, 10);
        guardCodeSlices(a, 15, 13, bytes, miss);
    }
    a.addX(14, 14, 13);
    a.bind(done);
}

// Read one guest operand. When it is the instruction's only guest access,
// a DTLB refusal (normally MMIO) calls the exact Moira access thunk and
// rejoins native code. A fault still reaches the untouched instruction
// fallback. Multi-access/RMW instructions retain the conservative path so
// replay can never duplicate a device side effect.
void memLoadGuest(Asm& a, const Layout& L, bool super, int bits, unsigned rd,
                  int slow, bool soleAccess, const Ea* ea = nullptr) {
    soleAccess = soleAccess && accessThunkMode() >= 1;
    if (!soleAccess) {
        memProbe(a, L, super, bits / 8, false, slow);
        if (ea) commitEaBeforeAccess(a, L, *ea, bits);
        loadGuest(a, bits, rd);
        return;
    }

    const int miss = a.label(), done = a.label();
    memProbe(a, L, super, bits / 8, false, miss);
    if (ea) commitEaBeforeAccess(a, L, *ea, bits);
    loadGuest(a, bits, rd);
    a.b(done);

    a.bind(miss);
    a.strX(21, 1, 96);                 // failed thunk must leave no cycle debt
    a.strX(21, 0, L.clock);             // a device read may advance time
    a.address(3, 1, 72);                // Frame::value
    a.movRegW(1, 9);                    // guest address
    a.movW(2, unsigned(bits / 8));
    if (ea) commitEaBeforeAccess(a, L, *ea, bits);
    a.movX(16, uint64_t(uintptr_t(&pom68kA64Read)));
    a.blr(16);
    a.movRegW(14, 0);
    a.emit(0xA94207E0u);                // ldp x0,x1,[sp,#32]
    a.ldrX(21, 0, L.clock);
    a.cmpWZero(14);
    const int ok = a.label();
    a.bCond(Asm::NE, ok);
    if (ea) rollbackEaBeforeAccess(a, L, *ea, bits);
    a.ldrX(21, 1, 96); a.strX(21, 0, L.clock);
    a.b(slow);                          // fault: replay untouched instruction
    a.bind(ok);
    a.ldrW(rd, 1, 72);                  // thunk result is host-ordered
    a.bind(done);
}

void loadGuest(Asm& a, int bits, unsigned rd) {
    if (bits == 8) a.ldrB(rd, 14, 0);
    else if (bits == 16) { a.ldrH(rd, 14, 0); a.rev16W(rd, rd); }
    else { a.ldrW(rd, 14, 0); a.rev32W(rd, rd); }
}

void storeGuest(Asm& a, int bits, unsigned rs) {
    if (bits == 8) a.strB(rs, 14, 0);
    else if (bits == 16) {
        a.rev16W(12, rs); a.strH(12, 14, 0);
    } else {
        a.rev32W(12, rs); a.strW(12, 14, 0);
    }
}

// Optional lockstep journal for direct stores, which otherwise bypass the
// machine memory-map callback entirely. w9 is the pre-update guest address;
// Frame::value holds the source. The callback sees the architectural An
// update because callers place it immediately before this hook.
void observeDirectWrite(Asm& a, int bits, uint32_t instructionPc) {
    const int done = a.label();
    a.ldrX(16, 1, 104);
    a.cbzX(16, done);
    a.strW(9, 1, 36);
    a.strX(14, 1, 120);
    a.ldrX(15, 1, 112);
    a.ldrW(2, 1, 36);
    a.ldrW(4, 1, 72);
    a.movRegX(14, 0);
    a.movRegX(0, 15);
    a.movRegX(1, 14);
    a.movW(3, unsigned(bits / 8));
    a.movW(5, instructionPc);
    a.movW(6, 1);
    a.blr(16);
    a.emit(0xA94207E0u);                // ldp x0,x1,[sp,#32]
    a.ldrW(9, 1, 36);
    a.ldrW(11, 1, 72);
    a.ldrX(14, 1, 120);
    a.bind(done);
}

// Store one sole-access operand. Plain RAM keeps the fast direct DTLB path.
// A refused mapping (MMIO, /BERR hole, protected translation) is attempted
// through the exact model-specific Moira write. If that faults, no native
// architectural state has been committed: clock and an early 030 (An)+
// update are restored before the untouched instruction is replayed, which
// is where the format-A/B frame is constructed.
void memStoreGuest(Asm& a, const Layout& L, bool super, int bits, unsigned rs,
                   int slow, bool soleAccess, const Ea* ea = nullptr) {
    soleAccess = L.is030 && soleAccess && accessThunkMode() >= 1;
    if (!soleAccess) {
        memProbe(a, L, super, bits / 8, true, slow);
        if (ea) commitEaBeforeAccess(a, L, *ea, bits);
        a.ldrW(rs, 1, 72);
        storeGuest(a, bits, rs);
        return;
    }

    const int miss = a.label(), done = a.label(), ok = a.label();
    memProbe(a, L, super, bits / 8, true, miss);
    if (ea) commitEaBeforeAccess(a, L, *ea, bits);
    a.ldrW(rs, 1, 72);
    storeGuest(a, bits, rs);
    a.b(done);

    a.bind(miss);
    a.strX(21, 1, 96);
    a.strX(21, 0, L.clock);
    a.ldrW(3, 1, 72);
    a.movRegW(1, 9);
    a.movW(2, unsigned(bits / 8));
    if (ea) commitEaBeforeAccess(a, L, *ea, bits);
    a.movX(16, uint64_t(uintptr_t(&pom68kA64Write)));
    a.blr(16);
    a.movRegW(14, 0);
    a.emit(0xA94207E0u);                // ldp x0,x1,[sp,#32]
    a.ldrX(21, 0, L.clock);
    a.cmpWZero(14);
    a.bCond(Asm::NE, ok);
    if (ea) rollbackEaBeforeAccess(a, L, *ea, bits);
    a.ldrX(21, 1, 96); a.strX(21, 0, L.clock);
    a.b(slow);
    a.bind(ok);
    a.ldrW(rs, 1, 72);
    a.bind(done);
}

void loadGuestOff(Asm& a, int bits, unsigned rd, uint32_t off) {
    if (bits == 16) { a.ldrH(rd, 14, off); a.rev16W(rd, rd); }
    else { a.ldrW(rd, 14, off); a.rev32W(rd, rd); }
}

void storeGuestOff(Asm& a, int bits, unsigned rs, uint32_t off) {
    if (bits == 16) { a.rev16W(12, rs); a.strH(12, 14, off); }
    else { a.rev32W(12, rs); a.strW(12, 14, off); }
}

bool emitRegInstr(Asm& a, const Layout& L, const BlockIr& ir, const Instr& in,
                  int slow = -1) {
    const uint16_t op = in.opcode;
    if (op == 0x4E71) return in.cycles == 2;

    if ((op & 0xF100) == 0x7000) {                  // MOVEQ
        if (in.cycles != 2) return false;
        const int32_t v = int8_t(op & 0xFF);
        const unsigned dn = (op >> 9) & 7;
        a.movW(11, uint32_t(v));
        a.strW(11, 0, L.d + dn * 4);
        emitLogicFlags(a, L, 11, 32);
        return true;
    }

    const uint16_t line = op & 0xF000;
    if (line == 0x1000 || line == 0x2000 || line == 0x3000) { // MOVE/MOVEA
        const int bits = line == 0x1000 ? 8 : line == 0x3000 ? 16 : 32;
        const int sm = (op >> 3) & 7, sr = op & 7;
        const int dm = (op >> 6) & 7, dr = (op >> 9) & 7;
        Ea src, dst;
        if (!decodeEa(ir, in, sm, sr, bits, 0, src) ||
            !decodeEa(ir, in, dm, dr, bits, src.ext, dst)) return false;
        if (bits == 8 && (src.idx == E_AN || dst.idx == E_AN)) return false;
        if ((src.idx == E_PI || src.idx == E_PD) && src.reg == dst.reg &&
            (dst.idx == E_AN || dst.idx == E_AI || dst.idx == E_PI ||
             dst.idx == E_PD || dst.idx == E_DI)) return false;
        const int sz = bits == 8 ? 0 : bits == 16 ? 1 : 2;
        // A postincrement source whose sole access uses the pre-update /
        // rollback helper is safe to validate against the split base cost.
        // Restartable writes consume it only for restartWrite030's injected-
        // fault-proved family. Brief indexed destination calculation costs
        // five base cycles; keep that admission local so memory-source MOVE
        // forms do not ride on this sole-write proof.
        const bool restartWrite = L.is030 && restartWrite030(op);
        const int dstCycles = restartWrite && dst.idx == E_IX
            ? 5 : kMoveDstA64[dst.idx];
        const int cycles = kEaReadA64[src.idx][sz] + dstCycles;
        const bool splitSafe030 = L.is030 &&
            ((src.idx == E_PI && !dst.memory) || restartWrite);
        const unsigned tracedCycles = splitSafe030 ? in.baseCycles : in.cycles;
        if (cycles < 0 || tracedCycles != unsigned(cycles) ||
            in.words != unsigned(1 + src.ext + dst.ext)) return false;

        if (src.memory && dst.memory) {
            if (slow < 0) return false;
            // A memory-to-memory MOVE has two independently refusable DTLB
            // probes. The old sequential path committed a 030 source (An)+
            // after the first hit; if the destination probe then missed,
            // untouched replay incremented An a second time. Probe both
            // mappings first and only then expose either EA side effect.
            addrOf(a, L, src, bits);
            memProbe(a, L, ir.super, bits / 8, false, slow);
            a.strX(14, 1, 120);            // source host pointer
            addrOf(a, L, dst, bits);
            memProbe(a, L, ir.super, bits / 8, true, slow);
            a.movRegX(15, 14);             // destination host pointer

            commitEaBeforeAccess(a, L, src, bits);
            a.ldrX(14, 1, 120);
            loadGuest(a, bits, 11);
            commitEaAfterAccess(a, L, src, bits);

            commitEaBeforeAccess(a, L, dst, bits);
            a.movRegX(14, 15);
            storeGuest(a, bits, 11);
            commitEaAfterAccess(a, L, dst, bits);
            emitLogicFlags(a, L, 11, bits);
            return true;
        }

        if (src.memory) {
            if (slow < 0) return false;
            addrOf(a, L, src, bits);
            memLoadGuest(a, L, ir.super, bits, 11, slow, !dst.memory, &src);
            if (!dst.memory) commitEaAfterAccess(a, L, src, bits);
        } else if (src.idx == E_IM) {
            a.movW(11, uint32_t(src.value));
            // Brief immediates are decoded through int8_t/int16_t so MOVEA.W
            // can sign-extend them.  Plain MOVE.B/W must materialise flags
            // from the guest-width value: leaving 0xFFFF as 0xFFFFFFFF made
            // emitNz store 0xFF into the C++ bool backing N.
            maskResult(a, 11, bits);
        } else {
            loadSized(a, L, 11, src.idx == E_AN, unsigned(src.reg), bits);
        }

        if (dst.memory) {
            if (slow < 0) return false;
            a.strW(11, 1, 72);           // survive a possible DTLB fill
            // On a 68030 MOVE publishes N/Z/V/C before entering writeOp's
            // LASTWRITE access. Exact MMIO callbacks therefore observe the
            // final CCR already, just like a fault frame does. Keep w11 live
            // for the store and suppress the ordinary tail update below.
            if (restartWrite && dst.idx != E_PI)
                emitLogicFlags(a, L, 11, bits);
            addrOf(a, L, dst, bits);
            if (restartWrite && dst.idx != E_PI) {
                // At Moira's last-write point reg.pc has consumed every
                // extension, while pc0 still names this instruction. An
                // exact MMIO thunk can expose both before the ordinary block
                // epilogue runs, so materialise the running boundary now.
                const uint32_t nextPc = in.pc + uint32_t(in.words) * 2;
                a.movW(12, nextPc); a.strW(12, 0, L.pc);
                a.movW(12, in.pc); a.strW(12, 0, L.pc0);
                const uint16_t heldIrd = in.terminalQueueValid
                    ? in.terminalIrd : in.opcode;
                const uint16_t heldIrc = in.terminalQueueValid
                    ? in.terminalIrc : ir.word(nextPc);
                a.movW(12, heldIrd); a.strH(12, 0, L.ird);
                a.movW(12, heldIrc); a.strH(12, 0, L.irc);
            }
            if (restartWrite && dst.idx == E_PI) {
                // PI is native only for a direct, faultless RAM mapping.
                // Probe before publishing CCR, PC/queue or the An update so
                // MMIO and /BERR reach Moira with a pristine entry boundary.
                memProbe(a, L, ir.super, bits / 8, true, slow);
                commitEaBeforeAccess(a, L, dst, bits);
                a.ldrW(11, 1, 72);
                emitLogicFlags(a, L, 11, bits);
                observeDirectWrite(a, bits, in.pc);
                storeGuest(a, bits, 11);
            } else {
                memStoreGuest(a, L, ir.super, bits, 11, slow,
                              restartWrite && !src.memory, &dst);
            }
            if (src.memory) commitEaAfterAccess(a, L, src, bits);
            commitEaAfterAccess(a, L, dst, bits);
        } else {
            if (dst.idx == E_AN && bits == 16) a.sxtH(11, 11);
            storeSized(a, L, 11, dst.idx == E_AN, unsigned(dst.reg),
                       dst.idx == E_AN ? 32 : bits);
        }
        if (dst.idx != E_AN && !(dst.memory && restartWrite))
            emitLogicFlags(a, L, 11, bits);
        return true;
    }

    if (line == 0x0000) {                          // immediate ALU -> Dn
        const int mode = (op >> 3) & 7;
        const bool dynamicBit = (op & 0xF100) == 0x0100;
        const bool staticBit = (op & 0xFF00) == 0x0800;
        if (dynamicBit || staticBit) {               // BTST/BCHG/BCLR/BSET
            if (mode == 1) return false;             // MOVEP overlap / An
            const bool toReg = mode == 0;
            const int bits = toReg ? 32 : 8;
            const int action = (op >> 6) & 3;        // 0 test,1 xor,2 clear,3 set
            const int extUsed = staticBit ? 1 : 0;
            Ea dst;
            if (!decodeEa(ir, in, mode, op & 7, bits, extUsed, dst) ||
                dst.idx == E_AN || dst.idx == E_IM ||
                (action != 0 && dst.idx == E_DIPC) ||
                in.words != unsigned(1 + extUsed + dst.ext)) return false;
            const int sz = toReg ? 2 : 0;
            const int base = kEaReadA64[dst.idx][sz];
            const int cycles = toReg ? 4 : base + 2;
            if (base < 0 || in.cycles != unsigned(cycles)) return false;
            if (dst.memory) {
                if (slow < 0) return false;
                addrOf(a, L, dst, bits);
                if (action == 0)
                    memLoadGuest(a, L, ir.super, bits, 11, slow, true, &dst);
                else {
                    memProbe(a, L, ir.super, bits / 8, true, slow);
                    commitEaBeforeAccess(a, L, dst, bits);
                    loadGuest(a, bits, 11);
                }
            } else {
                loadSized(a, L, 11, false, unsigned(dst.reg), 32);
            }
            if (dynamicBit) {
                a.ldrW(10, 0, L.d + ((op >> 9) & 7) * 4);
                a.movW(12, toReg ? 31 : 7); a.andW(10, 10, 12);
                a.movW(12, 1); a.lslVarW(12, 12, 10);
            } else {
                const uint32_t bit = ir.word(in.pc + 2) & 0xFF;
                a.movW(12, 1u << (bit & (toReg ? 31u : 7u)));
            }
            a.movRegW(13, 11);                    // original operand
            a.andW(11, 11, 12);
            a.movW(12, 1); a.strB(12, 0, L.srZ);
            // cbz's compact fixup is deliberately tied to w9.
            const int zero = a.label(), done = a.label();
            a.movRegW(9, 11); a.cbzW(9, zero);
            a.movW(12, 0); a.strB(12, 0, L.srZ); a.b(done);
            a.bind(zero); a.bind(done);
            // Rebuild the mask after the Z test (w12 is scratch there).
            if (action != 0) {
                a.movRegW(11, 13);
                if (dynamicBit) {
                    a.ldrW(10, 0, L.d + ((op >> 9) & 7) * 4);
                    a.movW(12, toReg ? 31 : 7); a.andW(10, 10, 12);
                    a.movW(12, 1); a.lslVarW(12, 12, 10);
                } else {
                    const uint32_t bit = ir.word(in.pc + 2) & 0xFF;
                    a.movW(12, 1u << (bit & (toReg ? 31u : 7u)));
                }
                if (action == 1) a.eorW(11, 11, 12);
                else if (action == 2) { a.mvnW(12, 12); a.andW(11, 11, 12); }
                else a.orrW(11, 11, 12);
                if (dst.memory) storeGuest(a, bits, 11);
                else a.strW(11, 0, L.d + unsigned(dst.reg) * 4);
            }
            if (dst.memory) commitEaAfterAccess(a, L, dst, bits);
            return true;
        }
        const int kindCode = (op >> 9) & 7, sz = (op >> 6) & 3;
        if (sz > 2) return false;
        AluOp kind;
        switch (kindCode) {
            case 0: kind = AluOp::Or; break;
            case 1: kind = AluOp::And; break;
            case 2: kind = AluOp::Sub; break;
            case 3: kind = AluOp::Add; break;
            case 5: kind = AluOp::Eor; break;
            case 6: kind = AluOp::Cmp; break;
            default: return false;
        }
        const int bits = bitsForSizeIndex(sz);
        const int immExt = bits == 32 ? 2 : 1;
        Ea dst;
        if (!decodeEa(ir, in, (op >> 3) & 7, op & 7, bits, immExt, dst) ||
            dst.idx == E_AN || dst.idx == E_IM || dst.idx == E_DIPC ||
            in.words != unsigned(1 + immExt + dst.ext)) return false;
        const int base = kEaReadA64[dst.idx][sz];
        const int cycles = kind == AluOp::Cmp ? base
                           : (dst.idx == E_DN ? 2 : base + 2);
        if (base < 0 || in.cycles != unsigned(cycles)) return false;
        if (dst.memory) {
            if (slow < 0) return false;
            addrOf(a, L, dst, bits);
            if (kind == AluOp::Cmp)
                memLoadGuest(a, L, ir.super, bits, 9, slow, true, &dst);
            else {
                memProbe(a, L, ir.super, bits / 8, true, slow);
                commitEaBeforeAccess(a, L, dst, bits);
                loadGuest(a, bits, 9);
            }
        } else {
            loadSized(a, L, 9, false, unsigned(dst.reg), bits);
        }
        a.movW(10, immediateValue(ir, in, bits));
        emitAluResult(a, L, kind, bits,
                      !dst.memory && kind != AluOp::Cmp,
                      false, unsigned(dst.reg), kind != AluOp::Cmp);
        if (dst.memory) {
            if (kind != AluOp::Cmp) storeGuest(a, bits, 11);
            commitEaAfterAccess(a, L, dst, bits);
        }
        return true;
    }

    if (line == 0x5000) {                          // ADDQ/SUBQ register
        const int sz = (op >> 6) & 3, mode = (op >> 3) & 7;
        if (sz > 2) return false;
        int imm = (op >> 9) & 7; if (!imm) imm = 8;
        const bool sub = (op & 0x0100) != 0;
        const int bits = mode == 1 ? 32 : bitsForSizeIndex(sz);
        Ea dst;
        if (!decodeEa(ir, in, mode, op & 7, bits, 0, dst) ||
            dst.idx == E_IM || dst.idx == E_DIPC ||
            in.words != unsigned(1 + dst.ext)) return false;
        const int base = kEaReadA64[dst.idx][bits == 8 ? 0 : bits == 16 ? 1 : 2];
        const int cycles = dst.idx <= E_AN ? 2 : base + 2;
        // The 030 tracer used to expose only the total clock delta. A cache
        // miss therefore made this register-only form look more expensive
        // than its architectural two cycles and forced a fallback. This is
        // the first deliberately narrow consumer of the split timing: An
        // has no data access, update ordering, or restartable write hidden
        // behind that lower cost.
        const unsigned tracedCycles = L.is030 && dst.idx <= E_AN
            ? in.baseCycles : in.cycles;
        if (base < 0 || tracedCycles != unsigned(cycles)) return false;
        if (dst.memory) {
            if (slow < 0) return false;
            addrOf(a, L, dst, bits);
            memProbe(a, L, ir.super, bits / 8, true, slow);
            commitEaBeforeAccess(a, L, dst, bits);
            loadGuest(a, bits, 9);
        } else {
            loadSized(a, L, 9, dst.idx == E_AN, unsigned(dst.reg), bits);
        }
        a.movW(10, unsigned(imm));
        if (dst.idx == E_AN) {
            if (sub) a.subW(11, 9, 10); else a.addW(11, 9, 10);
            a.strW(11, 0, L.a + unsigned(dst.reg) * 4);
            return true;                            // address form: no flags
        }
        emitAluResult(a, L, sub ? AluOp::Sub : AluOp::Add,
                      bits, !dst.memory, false, unsigned(dst.reg), true);
        if (dst.memory) {
            storeGuest(a, bits, 11); commitEaAfterAccess(a, L, dst, bits);
        }
        return true;
    }

    if (line == 0x8000 || line == 0x9000 || line == 0xB000 ||
        line == 0xC000 || line == 0xD000) {
        int direction = -1;
        if (!aluDirectionA64(op, direction)) return false;
        const int hi = (op >> 12) & 0xF, opmode = (op >> 6) & 7;
        const int mode = (op >> 3) & 7;

        if (opmode == 3 || opmode == 7) {            // ADDA/SUBA/CMPA
            if (hi != 0x9 && hi != 0xB && hi != 0xD) return false;
            const int srcBits = opmode == 3 ? 16 : 32;
            const int sz = srcBits == 16 ? 1 : 2;
            Ea src;
            if (!decodeEa(ir, in, mode, op & 7, srcBits, 0, src) ||
                in.words != unsigned(1 + src.ext) ||
                in.cycles != unsigned(kEaReadA64[src.idx][sz])) return false;
            if (src.memory) {
                if (slow < 0) return false;
                addrOf(a, L, src, srcBits);
                memLoadGuest(a, L, ir.super, srcBits, 10, slow, true, &src);
                commitEaAfterAccess(a, L, src, srcBits);
            } else if (src.idx == E_IM) a.movW(10, uint32_t(src.value));
            else loadSized(a, L, 10, src.idx == E_AN, unsigned(src.reg), srcBits);
            if (srcBits == 16) a.sxtH(10, 10);
            a.ldrW(9, 0, L.a + ((op >> 9) & 7) * 4);
            const AluOp kind = hi == 0x9 ? AluOp::Sub
                               : hi == 0xB ? AluOp::Cmp : AluOp::Add;
            if (kind == AluOp::Add) a.addW(11, 9, 10);
            else a.subW(11, 9, 10);
            if (kind != AluOp::Cmp)
                a.strW(11, 0, L.a + ((op >> 9) & 7) * 4);
            else
                emitAddSubFlags(a, L, 32, true, false);
            return true;
        }

        const int bits = bitsForSizeIndex(direction == 0 ? opmode : opmode - 4);
        if (bits == 8 && mode == 1) return false;
        AluOp kind;
        switch (hi) {
            case 0x8: kind = AluOp::Or; break;
            case 0x9: kind = AluOp::Sub; break;
            case 0xB: kind = direction == 0 ? AluOp::Cmp : AluOp::Eor; break;
            case 0xC: kind = AluOp::And; break;
            case 0xD: kind = AluOp::Add; break;
            default: return false;
        }
        const unsigned dn = (op >> 9) & 7;
        if (direction == 0) {
            const int sz = bits == 8 ? 0 : bits == 16 ? 1 : 2;
            Ea src;
            if (!decodeEa(ir, in, mode, op & 7, bits, 0, src) ||
                in.words != unsigned(1 + src.ext) ||
                in.cycles != unsigned(kEaReadA64[src.idx][sz])) return false;
            if (src.memory) {
                if (slow < 0) return false;
                addrOf(a, L, src, bits);
                memLoadGuest(a, L, ir.super, bits, 10, slow, true, &src);
                commitEaAfterAccess(a, L, src, bits);
            } else if (src.idx == E_IM) a.movW(10, uint32_t(src.value));
            else loadSized(a, L, 10, src.idx == E_AN, unsigned(src.reg), bits);
            loadSized(a, L, 9, false, dn, bits);       // destination
            return emitAluResult(a, L, kind, bits, kind != AluOp::Cmp,
                                 false, dn, kind != AluOp::Cmp);
        }
        if (mode == 0) {                                // EOR Dn,Dm
            if (kind != AluOp::Eor || in.words != 1 || in.cycles != 2)
                return false;
            loadSized(a, L, 9, false, op & 7, bits);
            loadSized(a, L, 10, false, dn, bits);
            return emitAluResult(a, L, kind, bits, true, false, op & 7, false);
        }
        // Register-to-memory ALU: one writable translation serves the RMW,
        // so the fallback is still entered before any guest-visible change.
        const int sz = bits == 8 ? 0 : bits == 16 ? 1 : 2;
        Ea dst;
        if (!decodeEa(ir, in, mode, op & 7, bits, 0, dst) || !dst.memory ||
            dst.idx == E_DIPC || in.words != unsigned(1 + dst.ext)) return false;
        const int cycles = kEaReadA64[dst.idx][sz] + 2;
        if (in.cycles != unsigned(cycles) || slow < 0) return false;
        addrOf(a, L, dst, bits);
        memProbe(a, L, ir.super, bits / 8, true, slow);
        commitEaBeforeAccess(a, L, dst, bits);
        loadGuest(a, bits, 9);
        loadSized(a, L, 10, false, dn, bits);
        emitAluResult(a, L, kind, bits, false, false, 0,
                      kind == AluOp::Add || kind == AluOp::Sub);
        storeGuest(a, bits, 11); commitEaAfterAccess(a, L, dst, bits);
        return true;
    }

    if (line == 0x4000) {
        const int mode = (op >> 3) & 7, sz = (op >> 6) & 3;
        if ((op & 0xFFF8) == 0x4E50) {              // LINK.W An,#d16
            if (in.cycles != 5 || in.words != 2 || slow < 0) return false;
            const unsigned an = op & 7;
            const int32_t disp = int16_t(ir.word(in.pc + 2));
            a.ldrW(9, 0, L.a + 7 * 4); a.subImmW(9, 9, 4);
            a.strW(9, 1, 44);                       // new frame/address
            if (an == 7) a.movRegW(11, 9);
            else a.ldrW(11, 0, L.a + an * 4);
            a.strW(11, 1, 72);
            memProbe(a, L, ir.super, 4, true, slow);
            a.ldrW(11, 1, 72); storeGuest(a, 32, 11);
            a.ldrW(9, 1, 44); a.strW(9, 0, L.a + an * 4);
            a.movW(10, uint32_t(disp)); a.addW(9, 9, 10);
            a.strW(9, 0, L.a + 7 * 4);
            return true;
        }
        if ((op & 0xFFF8) == 0x4E58) {              // UNLK An
            if (in.cycles != 6 || in.words != 1 || slow < 0) return false;
            const unsigned an = op & 7;
            a.ldrW(9, 0, L.a + an * 4); a.strW(9, 1, 44);
            memProbe(a, L, ir.super, 4, false, slow);
            loadGuest(a, 32, 11);
            a.strW(11, 0, L.a + an * 4);
            if (an != 7) {
                a.ldrW(9, 1, 44); a.addImmW(9, 9, 4);
                a.strW(9, 0, L.a + 7 * 4);
            }
            return true;
        }
        if ((op & 0xF1C0) == 0x41C0) {              // LEA <ea>,An
            Ea src;
            if (!decodeEa(ir, in, mode, op & 7, 32, 0, src) || !src.memory ||
                src.idx == E_PI || src.idx == E_PD ||
                in.words != unsigned(1 + src.ext)) return false;
            static const int8_t cost[E_COUNT] =
                {-1,-1,6,-1,-1,7,-1,6,6,7,-1,-1};
            if (cost[src.idx] < 0 || in.cycles != unsigned(cost[src.idx])) return false;
            addrOf(a, L, src, 32);
            a.strW(9, 0, L.a + ((op >> 9) & 7) * 4);
            return true;
        }
        if ((op & 0xFB80) == 0x4880 && mode >= 2) { // MOVEM
            const bool toRegs = (op & 0x0400) != 0;
            const int bits = (op & 0x0040) ? 32 : 16, bytes = bits / 8;
            const uint16_t mask = ir.word(in.pc + 2);
            if (!mask || slow < 0) return false;
            Ea ea;
            if (!decodeEa(ir, in, mode, op & 7, bits, 1, ea) || !ea.memory ||
                in.words != unsigned(2 + ea.ext)) return false;
            if ((toRegs && ea.idx == E_PD) ||
                (!toRegs && (ea.idx == E_PI || ea.idx == E_DIPC))) return false;
            int n = 0; for (int b = 0; b < 16; b++) n += (mask >> b) & 1;
            static const int8_t toRegBase[E_COUNT] =
                {-1,-1,12,8,-1,13,-1,12,12,9,-1,-1};
            static const int8_t toMemBase[E_COUNT] =
                {-1,-1,8,-1,4,9,-1,8,8,-1,-1,-1};
            const int baseCost = toRegs ? toRegBase[ea.idx] : toMemBase[ea.idx];
            if (baseCost < 0 || in.cycles != unsigned(baseCost + 4 * n)) return false;
            a.ldrB(9, 0, L.movemArmed); a.cbnzW(9, slow);

            auto loadR = [&](int b, unsigned rd) {
                a.ldrW(rd, 0, b < 8 ? L.d + unsigned(b) * 4
                                     : L.a + unsigned(b - 8) * 4);
            };
            auto storeR = [&](int b, unsigned rs) {
                a.strW(rs, 0, b < 8 ? L.d + unsigned(b) * 4
                                     : L.a + unsigned(b - 8) * 4);
            };

            if (ea.idx == E_PD) {
                a.ldrW(9, 0, L.a + unsigned(ea.reg) * 4);
                a.movW(10, unsigned(n * bytes)); a.subW(9, 9, 10);
                a.strW(9, 1, 44);
                memProbe(a, L, ir.super, n * bytes, true, slow);
                a.ldrW(10, 0, L.a + unsigned(ea.reg) * 4);
                int j = 0;
                for (int b = 0; b < 16; b++) {
                    if (!(mask & (0x8000u >> b))) continue;
                    if (b == 8 + ea.reg) a.subImmW(11, 10, unsigned(bytes));
                    else loadR(b, 11);
                    storeGuestOff(a, bits, 11, unsigned(j++ * bytes));
                }
                a.ldrW(9, 1, 44); a.strW(9, 0, L.a + unsigned(ea.reg) * 4);
                return true;
            }

            addrOf(a, L, ea, bits); a.strW(9, 1, 44);
            memProbe(a, L, ir.super, n * bytes, !toRegs, slow);
            int j = 0;
            for (int b = 0; b < 16; b++) {
                if (!(mask & (1u << b))) continue;
                if (toRegs) {
                    loadGuestOff(a, bits, 11, unsigned(j * bytes));
                    if (bits == 16) a.sxtH(11, 11);
                    storeR(b, 11);
                } else {
                    loadR(b, 11);
                    storeGuestOff(a, bits, 11, unsigned(j * bytes));
                }
                j++;
            }
            if (ea.idx == E_PI) {
                a.ldrW(9, 1, 44); a.addImmW(9, 9, unsigned(n * bytes));
                a.strW(9, 0, L.a + unsigned(ea.reg) * 4);
            }
            return true;
        }
        if ((op & 0xFFB8) == 0x4880) {              // EXT
            if (in.cycles != 4 || in.words != 1) return false;
            const unsigned dn = op & 7;
            if (op & 0x0040) {
                a.ldrH(11, 0, L.d + dn * 4); a.sxtH(11, 11);
                a.strW(11, 0, L.d + dn * 4); emitLogicFlags(a, L, 11, 32);
            } else {
                a.ldrB(11, 0, L.d + dn * 4); a.sxtB(11, 11);
                a.strH(11, 0, L.d + dn * 4); maskResult(a, 11, 16);
                emitLogicFlags(a, L, 11, 16);
            }
            return true;
        }
        if ((op & 0xFFF8) == 0x4840) {              // SWAP
            if (in.cycles != 4 || in.words != 1) return false;
            a.ldrW(11, 0, L.d + (op & 7) * 4); a.rorW(11, 11, 16);
            a.strW(11, 0, L.d + (op & 7) * 4); emitLogicFlags(a, L, 11, 32);
            return true;
        }
        if (sz > 2) return false;
        const int bits = bitsForSizeIndex(sz);
        const uint16_t family = op & 0xFF00;
        Ea ea;
        if (!decodeEa(ir, in, mode, op & 7, bits, 0, ea) ||
            in.words != unsigned(1 + ea.ext) || ea.idx == E_IM ||
            (bits == 8 && ea.idx == E_AN)) return false;
        const bool tst = family == 0x4A00;
        if (!tst && ea.idx == E_AN) return false;
        const int base = kEaReadA64[ea.idx][sz];
        const int cycles = tst ? base : (ea.idx <= E_AN ? 2 : base + 2);
        // TST (An) is a read-only operation with no effective-address side
        // effect. Its measured base component still includes any data-bus
        // stall, so an I/O trace remains conservatively rejected; only an
        // unrelated instruction-cache miss is removed from validation.
        const unsigned tracedCycles = L.is030 && tst &&
                                      (ea.idx == E_AI || ea.idx == E_PI)
            ? in.baseCycles : in.cycles;
        if (base < 0 || tracedCycles != unsigned(cycles)) return false;
        if (ea.memory) {
            if (slow < 0) return false;
            addrOf(a, L, ea, bits);
            if (tst)
                memLoadGuest(a, L, ir.super, bits, 9, slow, true, &ea);
            else {
                memProbe(a, L, ir.super, bits / 8, true, slow);
                commitEaBeforeAccess(a, L, ea, bits);
                loadGuest(a, bits, 9);
            }
        } else {
            loadSized(a, L, 9, ea.idx == E_AN, unsigned(ea.reg), bits);
            maskResult(a, 9, bits);
        }
        switch (family) {
            case 0x4A00: emitLogicFlags(a, L, 9, bits); break;
            case 0x4200: a.movW(11, 0); emitLogicFlags(a, L, 11, bits); break;
            case 0x4600:
                a.mvnW(11, 9); maskResult(a, 11, bits);
                emitLogicFlags(a, L, 11, bits); break;
            case 0x4400:
                a.movRegW(10, 9); a.movW(9, 0); a.subW(11, 9, 10);
                maskResult(a, 11, bits);
                emitAddSubFlags(a, L, bits, true, true); break;
            default: return false;
        }
        if (!tst) {
            if (ea.memory) storeGuest(a, bits, 11);
            else storeSized(a, L, 11, false, unsigned(ea.reg), bits);
        }
        if (ea.memory) commitEaAfterAccess(a, L, ea, bits);
        return true;
    }

    if (line == 0xE000 && (op & 0xF8C0) == 0xE8C0) { // register bitfield
        if (((op >> 3) & 7) != 0 || in.words != 2) return false;
        const uint16_t ext = ir.word(in.pc + 2);
        if ((ext & 0x0820) != 0) return false;       // register offset/width later
        const int kind = (op >> 8) & 7;
        const unsigned dst = op & 7, out = (ext >> 12) & 7;
        const unsigned offset = (ext >> 6) & 31;
        unsigned width = ext & 31; if (!width) width = 32;
        static const uint8_t cycles[8] = {6,8,12,8,12,18,12,10};
        if (in.cycles != cycles[kind]) return false;
        a.ldrW(9, 0, L.d + dst * 4);                // original
        if (offset) a.rorW(10, 9, 32 - offset);     // rotl(original,offset)
        else a.movRegW(10, 9);
        if (width < 32) a.lsrW(11, 10, 32 - width);
        else a.movRegW(11, 10);                     // extracted, right-justified

        // Common flags describe the original field. BFINS overrides them.
        emitNz(a, L, 11, 32);
        a.lsrW(12, 11, width - 1); a.strB(12, 0, L.srN);
        a.movW(12, 0); a.strB(12, 0, L.srV); a.strB(12, 0, L.srC);

        if (kind == 0) return true;                 // BFTST
        if (kind == 1 || kind == 3) {               // BFEXTU / BFEXTS
            if (kind == 3 && width < 32) {
                a.lslW(11, 11, 32 - width); a.asrW(11, 11, 32 - width);
            }
            a.strW(11, 0, L.d + out * 4);
            return true;
        }
        if (kind == 5) {                            // BFFFO
            a.clzW(12, 11);
            if (width < 32) a.subImmW(12, 12, 32 - width);
            if (offset) a.addImmW(12, 12, offset);
            a.strW(12, 0, L.d + out * 4);
            return true;
        }
        const uint32_t top = width == 32 ? 0xFFFFFFFFu
                                         : (0xFFFFFFFFu << (32 - width));
        const uint32_t mask = std::rotr(top, offset);
        a.movW(12, mask);
        if (kind == 2) a.eorW(9, 9, 12);            // BFCHG
        else if (kind == 4) { a.mvnW(12, 12); a.andW(9, 9, 12); } // BFCLR
        else if (kind == 6) a.orrW(9, 9, 12);       // BFSET
        else {                                      // BFINS
            a.ldrW(11, 0, L.d + out * 4);
            if (width < 32) {
                a.movW(13, uint32_t((uint64_t(1) << width) - 1));
                a.andW(11, 11, 13);
            }
            emitNz(a, L, 11, 32);
            a.lsrW(13, 11, width - 1); a.strB(13, 0, L.srN);
            a.movW(13, 0); a.strB(13, 0, L.srV); a.strB(13, 0, L.srC);
            if (width < 32) a.lslW(11, 11, 32 - width);
            if (offset) a.rorW(11, 11, offset);
            // emitNz uses w12 as scratch, so the field mask materialised
            // above no longer survives to the actual insertion.
            a.movW(12, mask);
            a.mvnW(12, 12); a.andW(9, 9, 12); a.orrW(9, 9, 11);
        }
        a.strW(9, 0, L.d + dst * 4);
        return true;
    }

    if (line == 0xE000) {                          // immediate register shifts
        const int sz = (op >> 6) & 3, type = (op >> 3) & 3;
        if (sz > 2 || (op & 0x20) || type == 2 || in.words != 1) return false;
        const int bits = bitsForSizeIndex(sz);
        int count = (op >> 9) & 7; if (!count) count = 8;
        const bool left = (op & 0x0100) != 0;
        const int expected = type == 1 ? 4 : type == 3 ? 8 : left ? 8 : 6;
        if (in.cycles != unsigned(expected)) return false;
        const unsigned dn = op & 7;
        loadSized(a, L, 11, false, dn, bits); maskResult(a, 11, bits);
        a.movW(13, 0);                              // accumulated ASL overflow
        for (int k = 0; k < count; k++) {
            if (left) {
                a.lsrW(10, 11, unsigned(bits - 1)); // outgoing bit
                a.lslW(12, 11, 1);
                if (bits != 32) { a.movW(9, bits == 8 ? 0xFFu : 0xFFFFu); a.andW(12, 12, 9); }
                if (type == 3) a.orrW(12, 12, 10);  // ROL
                if (type == 0) {                    // ASL sign changed?
                    a.lsrW(9, 12, unsigned(bits - 1));
                    a.eorW(9, 9, 10); a.orrW(13, 13, 9);
                }
            } else {
                a.movW(9, 1); a.andW(10, 11, 9);   // outgoing bit
                if (type == 0) {
                    if (bits == 8) a.sxtB(12, 11);
                    else if (bits == 16) a.sxtH(12, 11);
                    else a.movRegW(12, 11);
                    a.asrW(12, 12, 1);
                    if (bits != 32) { a.movW(9, bits == 8 ? 0xFFu : 0xFFFFu); a.andW(12, 12, 9); }
                } else {
                    a.lsrW(12, 11, 1);
                    if (type == 3) {
                        a.lslW(9, 10, unsigned(bits - 1)); a.orrW(12, 12, 9);
                    }
                }
            }
            a.movRegW(11, 12);
        }
        storeSized(a, L, 11, false, dn, bits);
        emitNz(a, L, 11, bits);
        a.strB(10, 0, L.srC);
        if (type != 3) a.strB(10, 0, L.srX);
        if (type == 0 && left) a.strB(13, 0, L.srV);
        else { a.movW(12, 0); a.strB(12, 0, L.srV); }
        return true;
    }
    return false;
}

void commitBoundary(Asm& a, const Layout& L, uint32_t pc) {
    a.movW(9, pc); a.strW(9, 0, L.pc); a.strW(9, 0, L.pc0);
}

void commitQueue(Asm& a, const Layout& L, uint16_t ird, uint16_t irc) {
    a.movW(9, ird); a.strH(9, 0, L.ird);
    a.movW(9, irc); a.strH(9, 0, L.irc);
}

void chargeAndRetire(Asm& a, const Layout& L, unsigned cycles,
                     bool paced, int batch, uint32_t,
                     uint32_t, bool) {
    if (paced) {
        // Cpu040::sync's batching decision, inlined. The guest clock stays
        // canonical in Moira memory, so cold calls and linked blocks need no
        // callee-saved clock register protocol.
        const int done = a.label();
        if (cycles) a.addImmX(21, 21, cycles);
        a.ldrX(10, 22, 0);
        if (batch < 0) {
            a.cmpX(21, 10);              // absolute next-event deadline
        } else {
            a.subX(11, 21, 10);
            a.movX(12, unsigned(batch));
            a.cmpX(11, 12);
        }
        a.bCond(Asm::LT, done);
        a.strX(21, 0, L.clock);
        a.movW(1, 0);                    // cycles already charged
        a.movX(16, uint64_t(uintptr_t(&pom68kA64Sync)));
        a.blr(16);
        a.emit(0xA94207E0u);             // ldp x0,x1,[sp,#32]
        a.ldrX(21, 0, L.clock);
        a.bind(done);
    } else {
        // x21 is the canonical clock while generated code runs; the helper
        // reads it from the object and writes the charged value back there.
        a.strX(21, 0, L.clock);
        a.movW(1, cycles);
        a.movX(16, uint64_t(uintptr_t(&pom68kA64Sync)));
        a.blr(16);
        a.emit(0xA94207E0u);             // ldp x0,x1,[sp,#32]
        a.ldrX(21, 0, L.clock);
    }
    a.ldrW(9, 1, 8);                    // Frame::instrs
    a.addImmW(9, 9, 1);
    a.strW(9, 1, 8);
}

// Emits a jump to `taken` iff the materialised 68k CCR satisfies cc.
void branchIfCond(Asm& a, const Layout& L, int cc, int taken) {
    switch (cc & 15) {
        case 0: a.b(taken); return;                         // T
        case 1: return;                                    // F
        case 2: {                                          // HI = !C && !Z
            const int fail = a.label();
            a.ldrB(9, 0, L.srC); a.cbnzW(9, fail);
            a.ldrB(9, 0, L.srZ); a.cbzW(9, taken);
            a.bind(fail); return;
        }
        case 3:                                            // LS = C || Z
            a.ldrB(9, 0, L.srC); a.cbnzW(9, taken);
            a.ldrB(9, 0, L.srZ); a.cbnzW(9, taken); return;
        case 4: a.ldrB(9, 0, L.srC); a.cbzW(9, taken); return;
        case 5: a.ldrB(9, 0, L.srC); a.cbnzW(9, taken); return;
        case 6: a.ldrB(9, 0, L.srZ); a.cbzW(9, taken); return;
        case 7: a.ldrB(9, 0, L.srZ); a.cbnzW(9, taken); return;
        case 8: a.ldrB(9, 0, L.srV); a.cbzW(9, taken); return;
        case 9: a.ldrB(9, 0, L.srV); a.cbnzW(9, taken); return;
        case 10: a.ldrB(9, 0, L.srN); a.cbzW(9, taken); return;
        case 11: a.ldrB(9, 0, L.srN); a.cbnzW(9, taken); return;
        case 12:                                          // GE = N == V
            a.ldrB(9, 0, L.srN); a.ldrB(10, 0, L.srV);
            a.eorW(9, 9, 10); a.cbzW(9, taken); return;
        case 13:                                          // LT = N != V
            a.ldrB(9, 0, L.srN); a.ldrB(10, 0, L.srV);
            a.eorW(9, 9, 10); a.cbnzW(9, taken); return;
        case 14: {                                        // GT = !Z && N == V
            const int fail = a.label();
            a.ldrB(9, 0, L.srZ); a.cbnzW(9, fail);
            a.ldrB(9, 0, L.srN); a.ldrB(10, 0, L.srV);
            a.eorW(9, 9, 10); a.cbzW(9, taken);
            a.bind(fail); return;
        }
        case 15:                                          // LE = Z || N != V
            a.ldrB(9, 0, L.srZ); a.cbnzW(9, taken);
            a.ldrB(9, 0, L.srN); a.ldrB(10, 0, L.srV);
            a.eorW(9, 9, 10); a.cbnzW(9, taken); return;
    }
}

int findTarget(const BlockIr& ir, uint32_t pc) {
    for (size_t i = 0; i < ir.instrs.size(); i++)
        if (ir.instrs[i].pc == pc) return int(i);
    return -1;
}

void leaveTo(Asm& a, const BlockIr& ir, uint32_t pc, uint32_t linkMask,
             int epilogue) {
    if (linkMask) {
        const uint32_t off = ((pc >> 1) & linkMask) * 16;
        const int miss = a.label();
        a.ldrX(14, 1, 64);               // Frame::linkTable
        a.address(15, 14, off);
        a.ldrW(9, 15, 0);
        a.movW(10, pc | uint32_t(ir.super));
        a.cmpW(9, 10); a.bCond(Asm::NE, miss);
        a.ldrX(16, 15, 8); a.br(16);
        a.bind(miss);
    }
    a.movW(9, uint32_t(Exit::BlockEnd)); a.strW(9, 1, 12);
    a.b(epilogue);
}

void leaveToDynamic(Asm& a, const Layout& L, const BlockIr& ir,
                    uint32_t linkMask, int epilogue) {
    if (linkMask) {
        const int miss = a.label();
        a.ldrW(11, 0, L.pc);
        a.lsrW(10, 11, 1);
        a.movW(12, linkMask); a.andW(10, 10, 12);
        a.lslX(10, 10, 4);
        a.ldrX(14, 1, 64);
        a.addX(15, 14, 10);
        a.ldrW(9, 15, 0);
        if (ir.super) { a.movW(12, 1); a.orrW(11, 11, 12); }
        a.cmpW(9, 11); a.bCond(Asm::NE, miss);
        a.ldrX(16, 15, 8); a.br(16);
        a.bind(miss);
    }
    a.movW(9, uint32_t(Exit::BlockEnd)); a.strW(9, 1, 12);
    a.b(epilogue);
}

bool emitBranchInstr(Asm& a, const Layout& L, const BlockIr& ir,
                     const Instr& in, const std::vector<int>& entries,
                     int epilogue, int slow, bool paced, int batch,
                     uint32_t linkMask) {
    const uint16_t op = in.opcode;
    const uint32_t nextPc = in.pc + uint32_t(in.words) * 2;
    const uint16_t entryLookahead = ir.word(in.pc + 2);
    // Mode-5 has no tail refill. Instructions that consume all but their
    // last extension with readExt(), then SKIP_LAST_RD/fullPrefetch(), hold
    // the last encoded word. A one-word transfer still holds its entry
    // lookahead. The traced path below must confirm every formula before a
    // 68030 branch emitter is admitted.
    const uint16_t lastHeld = in.words > 1
        ? ir.word(nextPc - 2) : entryLookahead;
    const auto tracedQueueIs = [&](uint16_t irc) {
        return !L.is030 || !in.terminalQueueValid ||
               (in.terminalIrd == op && in.terminalIrc == irc);
    };

    if (op == 0x4E75) {                              // RTS
        if (in.words != 1 || in.cycles != 10) return false;
        a.ldrW(9, 0, L.a + 7 * 4);
        memLoadGuest(a, L, ir.super, 32, 11, slow, true);
        a.movW(12, 1); a.andW(9, 11, 12); a.cbnzW(9, slow);
        a.ldrW(9, 0, L.a + 7 * 4); a.addImmW(9, 9, 4);
        a.strW(9, 0, L.a + 7 * 4);
        a.strW(11, 0, L.pc); a.strW(11, 0, L.pc0);
        if (!tracedQueueIs(entryLookahead)) return false;
        commitQueue(a, L, op, entryLookahead);
        chargeAndRetire(a, L, 10, paced, batch, in.pc,
                        uint32_t(in.words) + 1, ir.super);
        leaveToDynamic(a, L, ir, linkMask, epilogue);
        return true;
    }

    if ((op & 0xF000) == 0x6000 && ((op >> 8) & 15) == 1) { // BSR
        if (in.words > 3 || in.cycles != 7) return false;
        const int32_t disp = in.words == 1 ? int8_t(op & 0xFF)
                           : in.words == 2 ? int16_t(ir.word(in.pc + 2))
                           : int32_t(uint32_t(ir.word(in.pc + 2)) << 16 |
                                     ir.word(in.pc + 4));
        const uint32_t target = uint32_t(in.pc + 2 + uint32_t(disp));
        if (target & 1) return false;
        a.ldrW(9, 0, L.a + 7 * 4); a.subImmW(9, 9, 4);
        a.strW(9, 1, 44);
        memProbe(a, L, ir.super, 4, true, slow);
        a.movW(11, nextPc); storeGuest(a, 32, 11);
        a.ldrW(9, 1, 44); a.strW(9, 0, L.a + 7 * 4);
        if (!tracedQueueIs(lastHeld)) return false;
        commitBoundary(a, L, target); commitQueue(a, L, op, lastHeld);
        chargeAndRetire(a, L, 7, paced, batch, in.pc,
                        uint32_t(in.words) + 1, ir.super);
        leaveTo(a, ir, target, linkMask, epilogue);
        return true;
    }

    if ((op & 0xFF80) == 0x4E80) {                  // JSR / JMP
        const bool jsr = (op & 0x0040) == 0;
        Ea ea;
        if (!decodeEa(ir, in, (op >> 3) & 7, op & 7, 32, 0, ea) ||
            !ea.memory || ea.idx == E_PI || ea.idx == E_PD ||
            in.words != unsigned(1 + ea.ext)) return false;
        static const int8_t cost[E_COUNT] =
            {-1,-1,4,-1,-1,5,-1,4,4,5,-1,-1};
        if (cost[ea.idx] < 0 || in.cycles != unsigned(cost[ea.idx])) return false;
        addrOf(a, L, ea, 32);
        a.strW(9, 1, 72);                           // target
        a.movW(12, 1); a.andW(9, 9, 12); a.cbnzW(9, slow);
        if (jsr) {
            a.ldrW(9, 0, L.a + 7 * 4); a.subImmW(9, 9, 4);
            a.strW(9, 1, 44);
            memProbe(a, L, ir.super, 4, true, slow);
            a.movW(11, nextPc); storeGuest(a, 32, 11);
            a.ldrW(9, 1, 44); a.strW(9, 0, L.a + 7 * 4);
        }
        a.ldrW(11, 1, 72);
        a.strW(11, 0, L.pc); a.strW(11, 0, L.pc0);
        if (!tracedQueueIs(lastHeld)) return false;
        commitQueue(a, L, op, lastHeld);
        chargeAndRetire(a, L, unsigned(cost[ea.idx]), paced, batch, in.pc,
                        uint32_t(in.words) + 1, ir.super);
        const bool constant = ea.idx == E_AW || ea.idx == E_AL || ea.idx == E_DIPC;
        if (constant) leaveTo(a, ir, uint32_t(ea.value), linkMask, epilogue);
        else leaveToDynamic(a, L, ir, linkMask, epilogue);
        return true;
    }

    if ((op & 0xF000) == 0x6000) {                   // BRA/Bcc (not BSR)
        const int cc = (op >> 8) & 15;
        if (cc == 1 || in.words > 3) return false;
        const int32_t disp = in.words == 1 ? int8_t(op & 0xFF)
                           : in.words == 2 ? int16_t(ir.word(in.pc + 2))
                           : int32_t(uint32_t(ir.word(in.pc + 2)) << 16 |
                                     ir.word(in.pc + 4));
        const uint32_t target = uint32_t(in.pc + 2 + uint32_t(disp));
        const uint32_t fall = in.pc + uint32_t(in.words) * 2;
        if (target & 1) return false;
        const int takenCycles = cc == 0 ? 10 : 6;
        const int fallCycles = in.words == 1 ? 4 : 6;
        if (in.cycles != takenCycles && in.cycles != fallCycles) return false;

        const uint16_t takenIrc = lastHeld;
        const uint16_t fallIrc = ir.word(fall);
        if (L.is030 && in.terminalQueueValid) {
            const bool targetOnly = in.observedNextPc == target && target != fall;
            const bool fallOnly = in.observedNextPc == fall && target != fall;
            if (in.terminalIrd != op ||
                (targetOnly && in.terminalIrc != takenIrc) ||
                (fallOnly && in.terminalIrc != fallIrc) ||
                (!targetOnly && !fallOnly && in.terminalIrc != takenIrc &&
                 in.terminalIrc != fallIrc)) return false;
        }

        const int taken = a.label();
        branchIfCond(a, L, cc, taken);
        // Not taken.
        commitBoundary(a, L, fall);
        commitQueue(a, L, op, fallIrc);
        chargeAndRetire(a, L, unsigned(fallCycles), paced, batch, in.pc,
                        uint32_t(in.words) + 1, ir.super);
        leaveTo(a, ir, fall, linkMask, epilogue);

        a.bind(taken);
        commitBoundary(a, L, target);
        commitQueue(a, L, op, takenIrc);
        chargeAndRetire(a, L, unsigned(takenCycles), paced, batch, in.pc,
                        uint32_t(in.words) + 1, ir.super);
        const int ti = findTarget(ir, target);
        if (ti >= 0) a.b(entries[size_t(ti)]);
        else leaveTo(a, ir, target, linkMask, epilogue);
        return true;
    }

    if ((op & 0xF0F8) == 0x50C8) {                  // DBcc
        if (in.words != 2 || (in.cycles != 6 && in.cycles != 10)) return false;
        const int cc = (op >> 8) & 15, dn = op & 7;
        const int32_t disp = int16_t(ir.word(in.pc + 2));
        const uint32_t target = uint32_t(in.pc + 2 + uint32_t(disp));
        const uint32_t fall = in.pc + 4;
        if (target & 1) return false;
        if (!tracedQueueIs(entryLookahead)) return false;
        const int condTrue = a.label(), expired = a.label();
        if (cc != 1) branchIfCond(a, L, cc, condTrue);
        a.ldrH(9, 0, L.d + unsigned(dn) * 4);        // pre-decrement value
        a.subImmW(10, 9, 1);
        a.strH(10, 0, L.d + unsigned(dn) * 4);
        a.cbzW(9, expired);

        commitBoundary(a, L, target);
        commitQueue(a, L, op, ir.word(in.pc + 2));
        chargeAndRetire(a, L, 6, paced, batch, in.pc,
                        uint32_t(in.words) + 1, ir.super);
        { const int ti = findTarget(ir, target);
          if (ti >= 0) a.b(entries[size_t(ti)]);
          else leaveTo(a, ir, target, linkMask, epilogue); }

        a.bind(expired);
        commitBoundary(a, L, fall);
        commitQueue(a, L, op, ir.word(in.pc + 2));
        chargeAndRetire(a, L, 10, paced, batch, in.pc,
                        uint32_t(in.words) + 1, ir.super);
        leaveTo(a, ir, fall, linkMask, epilogue);

        a.bind(condTrue);
        commitBoundary(a, L, fall);
        commitQueue(a, L, op, ir.word(in.pc + 2));
        chargeAndRetire(a, L, 6, paced, batch, in.pc,
                        uint32_t(in.words) + 1, ir.super);
        leaveTo(a, ir, fall, linkMask, epilogue);
        return true;
    }
    return false;
}

class A64Compiled : public Compiled {
public:
    uint8_t* entry = nullptr;
    uint8_t* linked = nullptr;
};

class A64Backend final : public Backend {
public:
    Backend* clone() const override { return new A64Backend; }
    const char* name() const override { return "aarch64"; }
    const char* description() const override {
        return "native AArch64 code generation";
    }
    bool usable() const override { return CodeBuffer::supported(); }
    BackendCaps caps() const override {
        BackendCaps c;
        c.nativeCode = true;
        c.aluReg = c.aluMem = c.moves = c.branches = c.addrModes = true;
        c.dtlbCodeMask = true;
        c.maxBlockInstrs = 64;
        // The 68030 emitter is implemented and lockstep-clean, but remains
        // behind POM68K_JIT_UNSAFE_BACKEND until its fixed-budget throughput
        // beats the threaded backend (docs/JIT_BRINGUP.md Phase A2/B).
        c.guestFamilies = kGuest68040;
        return c;
    }
    bool canEmit(uint16_t op) const override {
        return canEmitReg(op);
    }
    Compiled* compile(const BlockIr& ir, const Context& ctx) override;
    void* linkEntry(Compiled* c) const override {
        return static_cast<A64Compiled*>(c)->linked;
    }
    RunResult run(Compiled* c, Context& ctx) override;
    void release(Compiled* c) override { delete c; }
    void flushAll() override {
        if (buf_.valid()) { buf_.makeWritable(); buf_.reset(); }
    }

private:
    CodeBuffer buf_;
    Layout layout_{};
    bool haveLayout_ = false;
    int diagLeft_ = 40;
};

Compiled* A64Backend::compile(const BlockIr& ir, const Context& ctx) {
    const auto reject = [this](const char* why) -> Compiled* {
        if (verbose() && diagLeft_-- > 0)
            std::fprintf(stderr, "[jit/a64] refused: %s\n", why);
        return nullptr;
    };
    if (!ctx.cpu || ir.instrs.empty() || ir.code.empty() ||
        !ctx.cpu->pomJitSimpleIpl()) return reject("empty/context/IPL mode");
    if (verbose()) {
        std::fprintf(stderr, "[jit/a64] native block $%08X (%zu):",
                     ir.entryPc, ir.instrs.size());
        for (const Instr& in : ir.instrs)
            std::fprintf(stderr, " %04X/%u=%u+%u+%u", in.opcode, in.cycles,
                         in.baseCycles, in.icacheCycles,
                         in.postExceptionCycles);
        std::fprintf(stderr, "\n");
    }
    if (!haveLayout_) { layout_ = ctx.cpu->pomJitLayout(); haveLayout_ = true; }
    const Layout& L = layout_;
    const bool paced = ctx.periphClock && ctx.periphBatch != 0 &&
                       detail::envBool("POM68K_JIT_A64_PACING", true);
    const int batch = paced ? ctx.periphBatch : 0;
    const bool restartWrite = L.is030 &&
        std::any_of(ir.instrs.begin(), ir.instrs.end(),
                    [](const Instr& in) { return restartWrite030(in.opcode); });
    // The 030 oracle proved that a block containing a native write is an
    // architectural chain boundary in both directions. Returning to Engine
    // services every deferred condition before another compiled block can
    // observe it; the fault-frame gate proves replay, not transparent links.
    const uint32_t linkMask = ctx.linkTable && !restartWrite ? ctx.linkMask : 0;

    Asm a;
    const int epilogue = a.label();
    const int exitLost = a.label();
    const int exitFault = a.label();
    // x21 keeps the guest clock and x22 the peripheral-clock pointer across
    // linked blocks and helper calls. x0/x1 remain the CPU/Frame ABI pair.
    a.emit(0xA9BC7BFDu);                 // stp x29,x30,[sp,#-64]!
    a.emit(0x910003FDu);                 // mov x29,sp
    a.emit(0xA9015BF5u);                 // stp x21,x22,[sp,#16]
    a.emit(0xA90207E0u);                 // stp x0,x1,[sp,#32]
    a.ldrX(21, 0, L.clock);
    if (paced) a.ldrX(22, 1, 56);        // Frame::periphClock
    // TAS/CAS are unsafe block terminators, hence no linked native chain can
    // set the 030 locked-RMW latch. Clear the value inherited from the last
    // interpreted instruction, matching mmuExecuteStart's contract.
    if (L.is030) { a.movW(9, 0); a.strB(9, 0, L.mmuRmw); }
    const size_t linkEntryOffset = a.byteSize();
    struct ExitLabels { int budget, flags; };
    std::vector<ExitLabels> exits;
    std::vector<int> entries;
    std::vector<int> slowStatic;
    std::vector<int> slowRuntime;
    std::vector<int> slowBody;
    exits.reserve(ir.instrs.size());
    entries.reserve(ir.instrs.size());
    slowStatic.reserve(ir.instrs.size());
    slowRuntime.reserve(ir.instrs.size());
    slowBody.reserve(ir.instrs.size());
    for (size_t i = 0; i < ir.instrs.size(); i++) {
        entries.push_back(a.label());
        slowStatic.push_back(a.label());
        slowRuntime.push_back(a.label());
        slowBody.push_back(a.label());
    }

    size_t nativeCount = 0;
    const bool icache = L.icLive && icacheEmitEnabled();
    IcacheShadow icacheShadow;

    for (size_t i = 0; i < ir.instrs.size(); i++) {
        a.bind(entries[i]);
        exits.push_back({a.label(), a.label()});
        a.ldrX(10, 1, 0);                // Frame::clockTarget
        a.cmpX(21, 10);
        a.bCond(Asm::GE, exits.back().budget);
        a.ldrW(9, 0, L.flags);
        a.cbnzW(9, exits.back().flags);

        const Instr& in = ir.instrs[i];
        // A word/long Bcc fetches a different number of words on its taken
        // and fall-through paths on the 68030. DBcc is not such a Bcc: it
        // always consumes its displacement extension before choosing among
        // condition-true, counter-expired and taken paths, so the ordinary
        // words+1 charge is exact. This distinction covers 56C9, which the
        // LC II census measured at 95.68% of all block fallbacks.
        const bool dbcc = (in.opcode & 0xF0F8) == 0x50C8;
        if (icache && in.kind == Kind::Branch && in.words > 1 && !dbcc) {
            a.b(slowStatic[i]);
            continue;
        }
        const Asm::Mark mark = a.mark();
        // DBcc reads its displacement from the already fetched queue word;
        // unlike readExt-based instructions it does not fetch a lookahead.
        if (icache) chargeIcache(a, L, ir, in, icacheShadow, dbcc ? 2u : 0u);
        // mmu040InstrStart's POLL_IPL sample.
        a.ldrB(9, 0, L.iplPin);
        a.strB(9, 0, L.regIpl);

        bool native = false;
        if (in.kind == Kind::Branch) {
            native = emitBranchInstr(a, L, ir, in, entries, epilogue,
                                     slowRuntime[i],
                                     paced, batch, linkMask);
        } else if (canEmit(in.opcode)) {
            native = emitRegInstr(a, L, ir, in, slowRuntime[i]);
        }
        if (!native) {
            // An emitter is allowed to discover a length/cycle mismatch
            // after writing a few instructions. Erase that partial attempt
            // and hand the untouched guest instruction to Moira.
            a.rewind(mark);
            a.b(slowStatic[i]);
            continue;
        }
        nativeCount++;
        if (in.kind == Kind::Branch) continue;

        const uint16_t ird = in.terminalQueueValid
            ? in.terminalIrd : in.opcode;
        const uint16_t irc = in.terminalQueueValid
            ? in.terminalIrc
            : ir.word(in.pc + uint32_t(in.words) * 2);
        // Halfword stores avoid assuming that the queue itself is 4-byte
        // aligned. (x86 tolerates its combined unaligned 32-bit store;
        // AArch64's scaled STR encoding does not encode that offset.)
        a.movW(9, ird); a.strH(9, 0, L.ird);
        a.movW(9, irc); a.strH(9, 0, L.irc);

        // On the 030, chargeIcache() has already reproduced the instruction
        // fetch component. Native instructions therefore owe only the base
        // component recorded by the tracer. Existing instructions traced on
        // cache hits have baseCycles == cycles, so this changes behaviour
        // only for forms explicitly admitted using split timing above.
        const uint16_t nativeCycles = L.is030 ? in.baseCycles : in.cycles;
        chargeAndRetire(a, L, nativeCycles, paced, batch, in.pc,
                        uint32_t(in.words) + 1, ir.super);

    }

    if (ir.instrs.back().kind != Kind::Branch) {
        const uint32_t endPc = ir.instrs.back().pc +
                               uint32_t(ir.instrs.back().words) * 2;
        commitBoundary(a, L, endPc);
        leaveTo(a, ir, endPc, linkMask, epilogue);
    } else {
        // An unsupported terminating branch reaches its own slow stub;
        // supported branches already emitted an explicit transfer.
        a.b(epilogue);
    }

    // Cold, exact per-instruction fallback. This is also the safety net for
    // memory mappings that cannot be represented by the inline data TLB.
    for (size_t i = 0; i < ir.instrs.size(); i++) {
        const auto emitHisto = [&](uint32_t frameOff) {
            a.ldrX(14, 1, frameOff);
            a.address(15, 14, uint32_t(ir.instrs[i].opcode) * 8);
            a.ldrX(9, 15, 0);
            a.addImmX(9, 9, 1);
            a.strX(9, 15, 0);
        };
        a.bind(slowStatic[i]);
        if (ctx.slowStaticHisto) emitHisto(80);
        a.b(slowBody[i]);
        a.bind(slowRuntime[i]);
        if (ctx.slowRuntimeHisto) emitHisto(88);
        if (icache) unchargeIcache(a, L, ir.instrs[i]);
        a.b(slowBody[i]);
        a.bind(slowBody[i]);
        commitBoundary(a, L, ir.instrs[i].pc);
        a.strX(21, 0, L.clock);
        a.movX(16, uint64_t(uintptr_t(&pom68kA64Step)));
        a.blr(16);
        a.movRegW(14, 0);                 // preserve helper result
        a.emit(0xA94207E0u);              // ldp x0,x1,[sp,#32]
        a.ldrX(21, 0, L.clock);
        a.cmpWZero(14);
        a.bCond(Asm::LT, exitLost);
        a.bCond(Asm::EQ, exitFault);
        a.ldrW(9, 1, 8);                  // retired
        a.addImmW(9, 9, 1);
        a.strW(9, 1, 8);
        a.ldrW(9, 1, 52);                 // slowInstrs
        a.addImmW(9, 9, 1);
        a.strW(9, 1, 52);
        a.ldrX(14, 1, 32);                // guardHit
        a.ldrB(9, 14, 0);
        a.cbnzW(9, exitLost);
        if (ir.instrs[i].kind == Kind::Branch ||
            ir.instrs[i].kind == Kind::Unsafe || i + 1 == ir.instrs.size()) {
            a.movW(9, uint32_t(Exit::BlockEnd)); a.strW(9, 1, 12);
            a.b(epilogue);
        } else {
            a.b(entries[i + 1]);
        }
    }

    a.bind(exitLost);
    a.movW(9, uint32_t(Exit::WindowLost)); a.strW(9, 1, 12);
    a.b(epilogue);
    a.bind(exitFault);
    a.ldrW(9, 1, 8);
    a.addImmW(9, 9, 1);
    a.strW(9, 1, 8);
    a.movW(9, uint32_t(Exit::Fault)); a.strW(9, 1, 12);
    a.b(epilogue);

    for (size_t i = 0; i < exits.size(); i++) {
        for (const auto [label, why] : {
                 std::pair{exits[i].budget, Exit::ClockBudget},
                 std::pair{exits[i].flags, Exit::CpuFlags}}) {
            a.bind(label);
            a.movW(9, ir.instrs[i].pc);
            a.strW(9, 0, L.pc); a.strW(9, 0, L.pc0);
            a.movW(9, uint32_t(why)); a.strW(9, 1, 12);
            a.b(epilogue);
        }
    }
    a.bind(epilogue);
    a.strX(21, 0, L.clock);
    a.emit(0xA9415BF5u);                 // ldp x21,x22,[sp,#16]
    a.emit(0xA8C47BFDu);                 // ldp x29,x30,[sp],#64
    a.emit(0xD65F03C0u);                 // ret
    if (!a.finish()) return reject("branch fixup");

    // Match the x86-64 policy: a block dominated by interpreter calls is
    // slower than the threaded window and should not consume code cache.
    if (nativeCount * 2 < ir.instrs.size()) return reject("native coverage");

    if (!buf_.valid() && !buf_.reserve(128u * 1024 * 1024)) return reject("code reserve");
    if (!buf_.makeWritable()) return reject("W^X -> writable");
    // Keep an indirect entry away from the final 16 bytes of an Apple
    // 16-KiB page. arm64 fetches a wider instruction window at a branch
    // target; a prologue straddling that boundary faults under MAP_JIT on
    // current macOS even though vm_region reports both pages RWX.
    uint8_t* dst = buf_.alloc(a.byteSize(), 64);
    if (!dst) {
        // makeWritable() toggles MAP_JIT protection for the whole thread.
        // Existing blocks must remain executable even when this allocation
        // cannot be satisfied.
        buf_.makeExecutable();
        return reject("code buffer full");
    }
    std::memcpy(dst, a.bytes(), a.byteSize());
    if (!buf_.makeExecutable()) return reject("W^X -> executable");
    auto* c = new A64Compiled;
    c->entry = dst;
    c->linked = restartWrite ? nullptr : dst + linkEntryOffset;
    return c;
}

RunResult A64Backend::run(Compiled* c, Context& ctx) {
    static const uint8_t noGuard = 0;
    Frame f{};
    f.clockTarget = ctx.clockTarget;
    f.exit = uint32_t(Exit::BlockEnd);
    f.dtlbSelf = ctx.dtlbSelf;
    f.dtlbFill = ctx.dtlbFill;
    f.guardHit = ctx.guard ? reinterpret_cast<const uint8_t*>(&ctx.guard->hit)
                           : &noGuard;
    f.periphClock = ctx.periphClock;
    f.linkTable = ctx.linkTable;
    f.slowStaticHisto = ctx.slowStaticHisto;
    f.slowRuntimeHisto = ctx.slowRuntimeHisto;
    f.observeWrite = ctx.observeWrite;
    f.observeWriteSelf = ctx.observeWriteSelf;
    using Fn = void (*)(moira::Moira*, Frame*);
    reinterpret_cast<Fn>(static_cast<A64Compiled*>(c)->entry)(ctx.cpu, &f);
    return RunResult{f.instrs, f.slowInstrs, Exit(f.exit)};
}

}  // namespace

Backend* a64Backend() {
    static A64Backend backend;
    return &backend;
}

}  // namespace jit
