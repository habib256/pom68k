// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── AArch64 code generator ───────────────────────────────────────────────
// Native 68040/68030 ALU, MOVE, effective-address and control-flow paths,
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

MemoryProofOptions proofOptions(const Layout& L) {
    MemoryProofOptions o;
    const int thunks = accessThunkMode();
    o.exactReads = thunks >= 1;
    // This backend's exact write replay is the proved 68030 LASTWRITE
    // subset. 040 stores use the native copyback-line protocol instead.
    o.exactWrites = L.is030 && thunks >= 2;
    o.cacheReads = L.cache040Live && cache040LineReadsEnabled();
    o.cacheWrites = L.cache040Live && cache040LineWritesEnabled();
    o.cachePairs = L.cache040Live && cache040LinePairsEnabled();
    o.restartableWriteRequired = L.is030;
    return o;
}

static_assert(sizeof(moira::Moira::PomJitCache040Entry) == 32);
static_assert(offsetof(moira::Moira::PomJitCache040Entry, physicalTag) == 4);
static_assert(offsetof(moira::Moira::PomJitCache040Entry, generation) == 8);
static_assert(offsetof(moira::Moira::PomJitCache040Entry, line) == 16);
static_assert(offsetof(moira::Cache040::Line, tag) == 0);
static_assert(offsetof(moira::Cache040::Line, valid) == 4);
static_assert(offsetof(moira::Cache040::Line, dirty) == 5);
static_assert(offsetof(moira::Cache040::Line, data) == 6);

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
    uint64_t* slowRuntimeReasonHisto;
    const uint32_t* dtlbFillReason;
    uint32_t runtimeReason;
    uint32_t pad2;
    RuntimeAddressObserver runtimeAddressObserver;
    void* runtimeAddressSelf;
    uint32_t runtimeAddress;
    uint32_t runtimeCodeMask;
    uint32_t runtimeBytes;
    uint32_t runtimeWrite;
};
static_assert(offsetof(Frame, linkTable) == 64);
static_assert(offsetof(Frame, slowStaticHisto) == 80);
static_assert(offsetof(Frame, slowRuntimeHisto) == 88);
static_assert(offsetof(Frame, savedClock) == 96);
static_assert(offsetof(Frame, observeWrite) == 104);
static_assert(offsetof(Frame, observeWriteSelf) == 112);
static_assert(offsetof(Frame, observedHostPointer) == 120);
static_assert(offsetof(Frame, slowRuntimeReasonHisto) == 128);
static_assert(offsetof(Frame, dtlbFillReason) == 136);
static_assert(offsetof(Frame, runtimeReason) == 144);
static_assert(offsetof(Frame, runtimeAddressObserver) == 152);
static_assert(offsetof(Frame, runtimeAddressSelf) == 160);
static_assert(offsetof(Frame, runtimeAddress) == 168);
static_assert(offsetof(Frame, runtimeCodeMask) == 172);
static_assert(offsetof(Frame, runtimeBytes) == 176);
static_assert(offsetof(Frame, runtimeWrite) == 180);

// Set once at the start of every compile. Code generation is synchronous;
// thread_local keeps independent machine threads from sharing this option.
thread_local bool gRuntimeReasonHisto = false;
// Opcode-local release of the historical conservative store guard. B592 is
// the sole promoted default: its zero-mask path is lockstep/SMC/long-oracle
// proved. Zero keeps the byte-for-byte legacy path; other opcodes remain an
// explicit diagnostic selection until they clear the same evidence bar.
thread_local uint16_t gExactStoreGuardOpcode = 0xB592;
thread_local uint16_t gCurrentOpcode = 0;

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
                         : in.fetchWords   ? uint32_t(in.fetchWords)
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

// One path-specific fetch, used by the fall-through of a conditional Bcc.W.
// It deliberately does not advance the compile-time shadow: the taken path
// does not perform this fetch, so no later fold may rely on the update.
void chargeIcacheExtraWord(Asm& a, const Layout& L, const BlockIr& ir,
                           uint32_t addr) {
    const uint32_t sup = ir.super ? 0x80000000u : 0u;
    const int line = int((addr >> 4) & 15);
    const uint32_t tag = (addr >> 8) | sup;
    const uint8_t bit = uint8_t(1u << ((addr >> 2) & 3));

    a.ldrX(9, 0, L.icFetches); a.addImmX(9, 9, 1); a.strX(9, 0, L.icFetches);
    const int disabled = a.label();
    a.ldrB(9, 0, L.cacr); a.movW(10, 1); a.andW(9, 9, 10); a.cbzW(9, disabled);
    a.ldrX(9, 0, L.icHits); a.addImmX(9, 9, 1); a.strX(9, 0, L.icHits);

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
    a.bind(disabled);
}

// A statically refused instruction is executed by Moira and therefore makes
// the same cache-line transitions as the emitted charge would. Advance only
// the compiler shadow so later folded hits remain justified; emit no guest
// state change here, because the fallback may still be declined at a budget
// boundary before the instruction actually runs.
void shadowIcache(const BlockIr& ir, const Instr& in, IcacheShadow& shadow,
                  uint32_t exactFetchWords = 0) {
    const uint32_t words = exactFetchWords ? exactFetchWords
                         : in.fetchWords   ? uint32_t(in.fetchWords)
                                           : uint32_t(in.words) + 1;
    const uint32_t sup = ir.super ? 0x80000000u : 0u;
    for (uint32_t w = 0; w < words; w++) {
        const uint32_t addr = in.pc + w * 2;
        const int line = int((addr >> 4) & 15);
        const uint32_t tag = (addr >> 8) | sup;
        const uint8_t bit = uint8_t(1u << ((addr >> 2) & 3));
        if (!shadow.seen[line] || shadow.tag[line] != tag) {
            shadow.tag[line] = tag;
            shadow.valid[line] = 0;
            shadow.seen[line] = true;
        }
        shadow.valid[line] |= bit;
    }
}

// Exact data thunks can advance peripheral time from inside the operand
// access. The interpreter has already paid any instruction-cache misses by
// then, whereas charge-on-success must normally defer their publication
// until the access can no longer bail out. Native execution is therefore
// ordering-safe only when earlier fetches in this block prove that every
// word of this instruction is already an i-cache hit. Starting the shadow
// empty makes the proof independent of cache state at block entry.
bool shadowProvesIcacheHits(const BlockIr& ir, const Instr& in,
                            const IcacheShadow& shadow,
                            uint32_t exactFetchWords = 0) {
    const uint32_t words = exactFetchWords ? exactFetchWords
                         : in.fetchWords   ? uint32_t(in.fetchWords)
                                           : uint32_t(in.words) + 1;
    const uint32_t sup = ir.super ? 0x80000000u : 0u;
    for (uint32_t w = 0; w < words; w++) {
        const uint32_t addr = in.pc + w * 2;
        const int line = int((addr >> 4) & 15);
        const uint32_t tag = (addr >> 8) | sup;
        const uint8_t bit = uint8_t(1u << ((addr >> 2) & 3));
        if (!shadow.seen[line] || shadow.tag[line] != tag ||
            !(shadow.valid[line] & bit)) return false;
    }
    return true;
}

// Runtime half of the same proof. On a cache miss the untouched instruction
// goes through Moira, which applies the miss penalty before its exact data
// access. On a hit the post-success charge below has no cycle component, so
// it may safely update only the accounting after the native body. Words
// already guaranteed by the block shadow need no redundant test.
void guardIcacheHits(Asm& a, const Layout& L, const BlockIr& ir,
                     const Instr& in, const IcacheShadow& shadow, int slow,
                     uint32_t exactFetchWords = 0) {
    const uint32_t words = exactFetchWords ? exactFetchWords
                         : in.fetchWords   ? uint32_t(in.fetchWords)
                                           : uint32_t(in.words) + 1;
    const uint32_t sup = ir.super ? 0x80000000u : 0u;
    const int disabled = a.label();
    a.ldrB(9, 0, L.cacr); a.movW(10, 1); a.andW(9, 9, 10);
    a.cbzW(9, disabled);
    for (uint32_t w = 0; w < words; w++) {
        const uint32_t addr = in.pc + w * 2;
        const int line = int((addr >> 4) & 15);
        const uint32_t tag = (addr >> 8) | sup;
        const uint8_t bit = uint8_t(1u << ((addr >> 2) & 3));
        if (shadow.seen[line] && shadow.tag[line] == tag &&
            (shadow.valid[line] & bit)) continue;
        a.ldrW(9, 0, L.icTag + uint32_t(line) * 4); a.movW(10, tag);
        a.cmpW(9, 10); a.bCond(Asm::NE, slow);
        a.ldrB(9, 0, L.icValid + uint32_t(line)); a.movW(10, bit);
        a.andW(9, 9, 10); a.cbzW(9, slow);
    }
    a.bind(disabled);
}

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
// E_AW (index 7) reads 2, not 3 — the same correction the x86-64 twin took on
// 2026-08-09 after a fallback census (JitBackendX64.cpp:208-221): on the 020 an
// absolute-short destination costs what a register-indirect one does, its
// extension word being already in the prefetch queue. The cost table is
// cross-checked against the tracer's own Instr::cycles, so a wrong cell does
// not mis-time anything — it makes the backend REFUSE the form outright. On
// x86-64 that single cell was 47.4 % of all block fallbacks; this backend has
// been paying the same toll silently because the census was never run here.
// Coverage, not correctness. (2026-08-12)
const int8_t kMoveDstA64[E_COUNT] = {0,0,2,2,3,3,-1,2,4,-1,-1,-1};

struct Ea {
    int idx = -1, reg = 0, ext = 0;
    int32_t value = 0;
    uint32_t base = 0;
    int ixReg = 0, ixShift = 0;
    bool ixLong = false;
    bool memory = false;
};

bool decodeEa(const Instr& in, int mode, int reg,
              int bits, int extAt, Ea& ea) {
    ea.idx = eaIndexA64(mode, reg); ea.reg = reg;
    if (ea.idx < 0) return false;
    const uint8_t size = bits == 8 ? 0 : bits == 16 ? 1 : 2;
    const DecodedEffectiveAddress* decoded = findEffectiveAddress(
        in, uint8_t(mode), uint8_t(reg), size, uint8_t(extAt));
    // Full 68020 index plans are decoded by the common IR, but this backend
    // has not proved a lowering for them yet. Reject the plan as a capability
    // decision; never reinterpret its first word as the brief format.
    if (!decoded || !decoded->valid || decoded->fullFormat) return false;
    ea.memory = decoded->memory();
    ea.value = decoded->value;
    ea.base = decoded->extensionAddress;
    ea.ixReg = decoded->indexRegister;
    ea.ixLong = decoded->indexLong;
    ea.ixShift = decoded->indexShift;
    ea.ext = decoded->extensionWords;
    return true;
}

bool canEmitReg(uint16_t op) {
    const InstructionSemantics sem = describeInstruction(op);
    const int mode = sem.eaMode;
    const int ei = eaIndexA64(mode, sem.eaReg);
    const auto controlEa = [](int index) {
        return index == E_AI || index == E_DI || index == E_AW ||
               index == E_AL || index == E_DIPC;
    };
    switch (sem.operation) {
        case SemanticOp::Nop:
        case SemanticOp::MoveQuick:
        case SemanticOp::MoveSrToReg:
        case SemanticOp::Link:
        case SemanticOp::Unlink:
        case SemanticOp::ReturnSubroutine:
        case SemanticOp::DecrementBranch:
        case SemanticOp::Branch:
        case SemanticOp::BranchSubroutine:
        case SemanticOp::Extend:
        case SemanticOp::Swap:
            return true;
        case SemanticOp::Bit:
            return mode != 1 && ei >= 0 && ei != E_AN && ei != E_IM &&
                   (sem.action == 0 || ei != E_DIPC);
        case SemanticOp::ImmediateAlu:
            return ei >= 0 && ei != E_AN && ei != E_IM && ei != E_DIPC;
        case SemanticOp::Move: {
            const int dm = sem.destinationMode, dr = sem.destinationReg;
            const int si = ei, di = eaIndexA64(dm, dr);
            if (si < 0 || di < 0 || di == E_IM || di == E_DIPC) return false;
            if (sem.sizeIndex == 0 && (mode == 1 || dm == 1)) return false;
            return true;
        }
        case SemanticOp::JumpSubroutine:
        case SemanticOp::Jump:
        case SemanticOp::Lea:
            return controlEa(ei);
        case SemanticOp::Pea:
            return controlEa(ei) || ei == E_IX || ei == E_IXPC;
        case SemanticOp::Movem:
            return true;
        case SemanticOp::SetCondition:
            return ei >= 0 && ei != E_AN && ei != E_DIPC && ei != E_IM;
        case SemanticOp::Test:
        case SemanticOp::Clear:
        case SemanticOp::Negate:
        case SemanticOp::Complement:
        case SemanticOp::AluRegToEa:
            return ei >= 0 && ei != E_IM;
        case SemanticOp::AddSubQuick:
            return ei >= 0 && ei != E_IM && ei != E_DIPC;
        case SemanticOp::AluEaToReg:
        case SemanticOp::AddressAlu:
            return ei >= 0; // immediate is a legal source EA
        case SemanticOp::Bitfield:
            return mode == 0;
        case SemanticOp::ShiftRegister:
            return !sem.dynamic && sem.action != 2; // no ROX yet
        default: return false;
    }
}

int bitsForSizeIndex(int sz) { return sz == 0 ? 8 : sz == 1 ? 16 : 32; }

// The 68030 timing trace includes the instruction-cache miss penalty in the
// total. Generated code reproduces that penalty separately, so opcode cost
// tables must be checked against the base component. A post-exception trace
// did not retire cleanly and is never a valid native-code admission proof.
unsigned traced030(const Layout& L, const Instr& in) {
    if (!L.is030) return in.cycles;
    if (in.postExceptionCycles != 0) return 0xFFFFFFFFu;
    return in.baseCycles;
}

void branchIfCond(Asm& a, const Layout& L, int cc, int taken);

bool emitAluResult(Asm& a, const Layout& L, AluOperation kind, int bits,
                   bool store, bool addressDst, unsigned dst, bool setX) {
    switch (kind) {
        case AluOperation::Or:  a.orrW(11, 9, 10); break;
        case AluOperation::And: a.andW(11, 9, 10); break;
        case AluOperation::Eor: a.eorW(11, 9, 10); break;
        case AluOperation::Add: a.addW(11, 9, 10); break;
        case AluOperation::Sub: case AluOperation::Cmp: a.subW(11, 9, 10); break;
        default: return false;
    }
    maskResult(a, 11, bits);
    if (store) storeSized(a, L, 11, addressDst, dst, bits);
    if (kind == AluOperation::Add)
        emitAddSubFlags(a, L, bits, false, setX);
    else if (kind == AluOperation::Sub || kind == AluOperation::Cmp)
        emitAddSubFlags(a, L, bits, true, setX);
    else
        emitLogicFlags(a, L, 11, bits);
    return true;
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
void commitEaBeforeAccess(Asm& a, const Layout& L, const Ea& ea, int bits,
                          const MemoryAccessPlan& access) {
    if (access.eaCommit == EaCommit::BeforeAccess)
        commitEa(a, L, ea, bits);
}

void commitEaAfterAccess(Asm& a, const Layout& L, const Ea& ea, int bits,
                         const MemoryAccessPlan& access) {
    if (access.eaCommit == EaCommit::AfterAccess)
        commitEa(a, L, ea, bits);
}

void rollbackEaBeforeAccess(Asm& a, const Layout& L, const Ea& ea, int bits,
                            const MemoryAccessPlan& access) {
    if (access.eaCommit != EaCommit::BeforeAccess) return;
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
    const bool exact = gExactStoreGuardOpcode != 0 &&
                       gCurrentOpcode == gExactStoreGuardOpcode;
    a.ldrW(12, entry, 4);                // PomJitDtlbEntry::codeMask
    if (exact) {
        a.cmpWZero(12);
        a.bCond(Asm::EQ, clear);
    } else {
        a.cbzW(12, clear);
    }
    for (int end = 0; end < (bytes > 1 ? 2 : 1); end++) {
        if (end) a.addImmW(10, pageOff, unsigned(bytes - 1));
        else a.movRegW(10, pageOff);
        a.lsrW(10, 10, moira::Moira::PomJitDtlb::kSliceShift);
        a.movW(11, 1);
        a.lslVarW(11, 11, 10);
        a.andW(10, 12, 11);
        if (gRuntimeReasonHisto) {
            const int next = a.label();
            if (exact) {
                a.cmpWZero(10);
                a.bCond(Asm::EQ, next);
            } else {
                a.cbzW(10, next);
            }
            a.strW(12, 1, 172);         // Frame::runtimeCodeMask
            a.b(miss);
            a.bind(next);
        } else {
            if (exact) {
                a.cmpWZero(10);
                a.bCond(Asm::NE, miss);
            } else {
                a.cbnzW(10, miss);
            }
        }
    }
    a.bind(clear);
}

void markRuntimeReason(Asm& a, RuntimeFallbackReason reason) {
    if (!gRuntimeReasonHisto) return;
    a.movW(12, uint32_t(reason));
    a.strW(12, 1, 144);                 // Frame::runtimeReason
}

void markRuntimeAccess(Asm& a, int bytes, bool write, bool clearMask) {
    if (!gRuntimeReasonHisto) return;
    a.strW(9, 1, 168);                  // Frame::runtimeAddress
    if (clearMask) {
        a.movW(12, 0);
        a.strW(12, 1, 172);             // Frame::runtimeCodeMask
    }
    a.movW(12, unsigned(bytes));
    a.strW(12, 1, 176);                 // Frame::runtimeBytes
    a.movW(12, write ? 1u : 0u);
    a.strW(12, 1, 180);                 // Frame::runtimeWrite
}

void clearRuntimeAccess(Asm& a) {
    if (!gRuntimeReasonHisto) return;
    a.movW(12, 0);
    a.strW(12, 1, 176);                 // zero bytes = no address detail
}

void observeRuntimeAddress(Asm& a, uint16_t opcode) {
    if (!gRuntimeReasonHisto) return;
    const int record = a.label(), maybeOther = a.label(), done = a.label();
    a.ldrW(9, 1, 144);                  // Frame::runtimeReason
    a.movW(10, uint32_t(RuntimeCodeMask));
    a.cmpW(9, 10);
    a.bCond(Asm::EQ, record);
    a.movW(10, uint32_t(RuntimeCrossPage));
    a.cmpW(9, 10);
    a.bCond(Asm::NE, maybeOther);
    a.b(record);
    a.bind(maybeOther);
    a.movW(10, uint32_t(RuntimeOther));
    a.cmpW(9, 10);
    a.bCond(Asm::NE, done);
    a.ldrW(10, 1, 176);
    a.cmpWZero(10);
    a.bCond(Asm::EQ, done);
    a.bind(record);
    a.ldrX(16, 1, 152);                 // Frame::runtimeAddressObserver
    a.ldrX(0, 1, 160);                  // observer self
    a.movRegW(1, 9);                    // reason
    a.movW(2, opcode);
    // x1 has just become the first integer argument. Recover Frame from the
    // prologue save before loading the remaining arguments.
    a.ldrX(14, 31, 40);                 // saved Frame* at [sp,#40]
    a.ldrW(3, 14, 168);
    a.ldrW(4, 14, 176);
    a.ldrW(5, 14, 180);
    a.ldrW(6, 14, 172);
    a.blr(16);
    a.emit(0xA94207E0u);                // ldp x0,x1,[sp,#32]
    a.bind(done);
}

// Translate guest address w9. On success x14 is the host byte pointer; a
// refused mapping or cross-page access transfers to the untouched slow path.
void memProbe(Asm& a, const Layout& L, bool super, int bytes, bool write,
              int miss, bool cacheRead = false, int cacheWriteHit = -1,
              bool cacheOnly = false, bool countCacheHit = true) {
    const int fill = a.label(), have = a.label(), done = a.label();
    const int crossMiss = gRuntimeReasonHisto ? a.label() : miss;
    const int maskMiss = gRuntimeReasonHisto ? a.label() : miss;
    const int nonPlainMiss = gRuntimeReasonHisto ? a.label() : miss;
    const int fillMiss = gRuntimeReasonHisto ? a.label() : miss;

    const bool cacheWrite = write && cacheWriteHit >= 0;
    if (((cacheRead && !write && cache040LineReadsEnabled()) || cacheWrite) &&
        L.cache040Live) {
        const int cacheMiss = a.label();

        // A line-crossing operand needs two independently translated cache
        // lines. Keep that rare case on the exact path.
        a.movW(12, 15); a.andW(13, 9, 12);
        if (bytes > 1) {
            a.movW(12, unsigned(16 - bytes));
            a.cmpW(13, 12); a.bCond(Asm::HI, cacheMiss);
        }

        // Direct-mapped logical-line lookup (32-byte entries).
        a.lsrW(10, 9, 4);
        a.movRegW(11, 10);
        if (super) {
            a.movW(12, 0x80000000u); a.orrW(11, 11, 12);
        }
        a.movW(12, moira::Moira::PomJitCache040Table::kEntries - 1);
        a.andW(10, 10, 12);
        a.lslX(10, 10, 5);
        a.address(14, 0, cacheWrite ? L.cache040W : L.cache040R);
        a.addX(14, 14, 10);
        a.ldrW(12, 14, 0);
        a.cmpW(11, 12); a.bCond(Asm::NE, cacheMiss);

        // ATC-derived state cannot survive an ATC eviction/map change.
        a.ldrW(12, 14, 8);
        a.ldrW(13, 0, L.cache040Gen);
        a.cmpW(12, 13); a.bCond(Asm::NE, cacheMiss);

        // The cache way is stable, its contents are not: validate both the
        // valid bit and physical tag before exposing its big-endian bytes.
        a.ldrW(13, 14, 4);
        a.ldrX(14, 14, 16);
        a.ldrB(12, 14, offsetof(moira::Cache040::Line, valid));
        a.cbzW(12, cacheMiss);
        a.ldrW(12, 14, offsetof(moira::Cache040::Line, tag));
        a.cmpW(12, 13); a.bCond(Asm::NE, cacheMiss);

        // Cache040::hits is diagnostic state, but keep it exact: this path
        // is emitted only for an instruction's sole guest access, so no
        // later speculative probe can force a replay after the increment.
        if (countCacheHit) {
            a.ldrX(12, 0, L.cache040Hits);
            a.addImmX(12, 12, 1);
            a.strX(12, 0, L.cache040Hits);
            if (cache040LineReadStatsEnabled()) {
                const uint32_t counter = cacheWrite ? L.cache040NativeWriteHits
                                                    : L.cache040NativeReadHits;
                a.ldrX(12, 0, counter);
                a.addImmX(12, 12, 1);
                a.strX(12, 0, counter);
            }
        }
        a.movW(12, 15); a.andW(13, 9, 12);
        a.addImmX(14, 14, offsetof(moira::Cache040::Line, data));
        a.addX(14, 14, 13);
        a.b(cacheWrite ? cacheWriteHit : done);
        a.bind(cacheMiss);
        if (cacheOnly) a.b(miss);
    }

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
        a.cmpW(13, 12); a.bCond(Asm::HI, crossMiss);
    }
    // Read codeMask before x14 stops being the entry pointer and becomes
    // the host pointer. This is the arm64 half of the 256-byte write guard.
    if (write) guardCodeSlices(a, 14, 13, bytes, maskMiss);
    a.ldrX(14, 14, 8);
    a.cbzX(14, nonPlainMiss);
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
    a.cbzX(14, fillMiss);
    a.movW(12, 4095); a.andW(13, 9, 12);
    if (bytes > 1) {
        a.movW(12, unsigned(4096 - bytes));
        a.cmpW(13, 12); a.bCond(Asm::HI, crossMiss);
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
        guardCodeSlices(a, 15, 13, bytes, maskMiss);
    }
    a.addX(14, 14, 13);

    if (gRuntimeReasonHisto) {
        // The successful fill path arrives here by fall-through. Skip the
        // diagnostic miss stubs; otherwise every success is mislabeled as
        // the first reason even though the totals still appear to add up.
        a.b(done);
        a.bind(crossMiss);
        markRuntimeAccess(a, bytes, write, true);
        markRuntimeReason(a, RuntimeCrossPage);
        a.b(miss);
        a.bind(maskMiss);
        // The historical CBZ/CBNZ fixup tests w9 regardless of the register
        // requested by guardCodeSlices. Preserve that conservative fallback
        // until its newly exposed native stores are conformant, but classify
        // it honestly: w10 is the actual (mask & accessed-slice) result.
        {
            const int falseConflict = a.label();
            a.cmpWZero(10);
            a.bCond(Asm::EQ, falseConflict);
            a.strW(12, 1, 172);
            markRuntimeAccess(a, bytes, write, false);
            markRuntimeReason(a, RuntimeCodeMask);
            a.b(miss);
            a.bind(falseConflict);
            markRuntimeAccess(a, bytes, write, true);
            markRuntimeReason(a, RuntimeOther);
            a.b(miss);
        }
        a.bind(nonPlainMiss);
        markRuntimeReason(a, RuntimeNonPlain);
        a.b(miss);
        a.bind(fillMiss);
        // fillDtlb records why a null was returned. The pointer is present
        // exactly when this diagnostic code was emitted.
        a.ldrX(15, 1, 136);             // Frame::dtlbFillReason
        a.ldrW(12, 15, 0);
        a.strW(12, 1, 144);
        a.b(miss);
    }
    a.bind(done);
}

// Read one guest operand. When it is the instruction's only guest access,
// a DTLB refusal (normally MMIO) calls the exact Moira access thunk and
// rejoins native code. A fault still reaches the untouched instruction
// fallback. Multi-access/RMW instructions retain the conservative path so
// replay can never duplicate a device side effect.
void memLoadGuest(Asm& a, const Layout& L, bool super, int bits, unsigned rd,
                  int slow, const MemoryAccessPlan& access,
                  const Ea* ea = nullptr) {
    if (!access.valid() || access.direction != MemoryDirection::Read ||
        access.bytes != unsigned(bits / 8)) { a.b(slow); return; }
    const bool cacheRead = access.cache;
    const bool exactAccess = access.exactThunk;
    if (!exactAccess) {
        memProbe(a, L, super, bits / 8, false, slow, cacheRead);
        if (ea) commitEaBeforeAccess(a, L, *ea, bits, access);
        loadGuest(a, bits, rd);
        return;
    }

    const int miss = a.label(), done = a.label();
    if (!access.exactRequired) {
        memProbe(a, L, super, bits / 8, false, miss, cacheRead);
        if (ea) commitEaBeforeAccess(a, L, *ea, bits, access);
        loadGuest(a, bits, rd);
        a.b(done);
    }

    a.bind(miss);
    a.strX(21, 1, 96);                 // failed thunk must leave no cycle debt
    a.strX(21, 0, L.clock);             // a device read may advance time
    a.address(3, 1, 72);                // Frame::value
    a.movRegW(1, 9);                    // guest address
    a.movW(2, unsigned(bits / 8));
    if (ea) commitEaBeforeAccess(a, L, *ea, bits, access);
    a.movX(16, uint64_t(uintptr_t(&pom68kA64Read)));
    a.blr(16);
    a.movRegW(14, 0);
    a.emit(0xA94207E0u);                // ldp x0,x1,[sp,#32]
    a.ldrX(21, 0, L.clock);
    a.cmpWZero(14);
    const int ok = a.label();
    a.bCond(Asm::NE, ok);
    if (ea) rollbackEaBeforeAccess(a, L, *ea, bits, access);
    a.ldrX(21, 1, 96); a.strX(21, 0, L.clock);
    a.b(slow);                          // fault: replay untouched instruction
    a.bind(ok);
    markRuntimeReason(a, RuntimeOther); // handled MMIO was not the fallback
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

// Publish the authoritative bytes written through x14 to the line's four
// dirty-longword bits. The cache probe has already proved that this is a
// resident copyback line and that the whole operand fits inside it.
void markCache040Dirty(Asm& a, int bytes) {
    a.movW(12, 15); a.andW(13, 9, 12); // byte offset in the line
    a.subX(15, 14, 13);
    a.subImmX(15, 15, offsetof(moira::Cache040::Line, data));

    a.lsrW(13, 13, 2);
    a.movW(12, 1); a.lslVarW(12, 12, 13);
    if (bytes > 1) {
        a.addImmW(13, 9, unsigned(bytes - 1));
        a.lsrW(13, 13, 2);
        a.movW(10, 3); a.andW(13, 13, 10);
        a.movW(10, 1); a.lslVarW(10, 10, 13);
        a.orrW(12, 12, 10);
    }
    a.ldrB(13, 15, offsetof(moira::Cache040::Line, dirty));
    a.orrW(12, 12, 13);
    a.strB(12, 15, offsetof(moira::Cache040::Line, dirty));
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
                   int slow, const MemoryAccessPlan& access,
                   const Ea* ea = nullptr) {
    if (!access.valid() || access.direction != MemoryDirection::Write ||
        access.bytes != unsigned(bits / 8)) { a.b(slow); return; }
    const bool cacheExactWrite = !L.is030 && access.single() && L.cache040Live;
    const bool cacheWrite = access.cache;
    const bool exactAccess = L.is030 && access.exactThunk;
    if (!exactAccess) {
        // This is the attribution-control half of the J4 write gate. Once
        // the architectural D-cache is live, disabling its native line hit
        // must restore the exact cache-aware instruction path; an ordinary
        // DTLB pointer names backing RAM and would bypass a dirty copyback
        // line. The enabled path below may still consult the DTLB after a W
        // miss, but cache-active fills are tagged-null refusals and therefore
        // reach the same untouched replay.
        if (cacheExactWrite && !cacheWrite) {
            a.b(slow);
            return;
        }
        const int cacheHit = cacheWrite ? a.label() : -1;
        const int done = cacheWrite ? a.label() : -1;
        memProbe(a, L, super, bits / 8, true, slow, false, cacheHit);
        if (ea) commitEaBeforeAccess(a, L, *ea, bits, access);
        a.ldrW(rs, 1, 72);
        storeGuest(a, bits, rs);
        if (cacheWrite) {
            a.b(done);
            a.bind(cacheHit);
            if (ea) commitEaBeforeAccess(a, L, *ea, bits, access);
            a.ldrW(rs, 1, 72);
            storeGuest(a, bits, rs);
            markCache040Dirty(a, bits / 8);
            a.bind(done);
        }
        return;
    }

    const int miss = a.label(), done = a.label(), ok = a.label();
    memProbe(a, L, super, bits / 8, true, miss);
    if (ea) commitEaBeforeAccess(a, L, *ea, bits, access);
    a.ldrW(rs, 1, 72);
    storeGuest(a, bits, rs);
    a.b(done);

    a.bind(miss);
    a.strX(21, 1, 96);
    a.strX(21, 0, L.clock);
    a.ldrW(3, 1, 72);
    a.movRegW(1, 9);
    a.movW(2, unsigned(bits / 8));
    if (ea) commitEaBeforeAccess(a, L, *ea, bits, access);
    a.movX(16, uint64_t(uintptr_t(&pom68kA64Write)));
    a.blr(16);
    a.movRegW(14, 0);
    a.emit(0xA94207E0u);                // ldp x0,x1,[sp,#32]
    a.ldrX(21, 0, L.clock);
    a.cmpWZero(14);
    a.bCond(Asm::NE, ok);
    if (ea) rollbackEaBeforeAccess(a, L, *ea, bits, access);
    a.ldrX(21, 1, 96); a.strX(21, 0, L.clock);
    a.b(slow);
    a.bind(ok);
    markRuntimeReason(a, RuntimeOther); // handled MMIO was not the fallback
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
    const InstructionSemantics& sem = in.semantics;
    if (!sem.valid()) return false;
    auto memory = instructionMemoryPlan(in.memory, proofOptions(L));
    if (sem.operation == SemanticOp::Nop) return traced030(L, in) == 2;

    if (sem.operation == SemanticOp::MoveQuick) {
        if (traced030(L, in) != 2) return false;
        const int32_t v = int8_t(op & 0xFF);
        const unsigned dn = sem.registerIndex;
        a.movW(11, uint32_t(v));
        a.strW(11, 0, L.d + dn * 4);
        emitLogicFlags(a, L, 11, 32);
        return true;
    }

    if (sem.operation == SemanticOp::MoveSrToReg) {
        if (in.words != 1 || in.baseCycles != 8 || in.postExceptionCycles != 0)
            return false;
        a.movW(11, 0);
        const auto addSrBit = [&](uint32_t off, unsigned bit) {
            a.ldrB(9, 0, off);
            if (bit) a.lslW(9, 9, bit);
            a.orrW(11, 11, 9);
        };
        addSrBit(L.srT1, 15); addSrBit(L.srT0, 14);
        addSrBit(L.srS, 13);  addSrBit(L.srM, 12);
        a.ldrB(9, 0, L.srIpl); a.lslW(9, 9, 8); a.orrW(11, 11, 9);
        addSrBit(L.srX, 4); addSrBit(L.srN, 3); addSrBit(L.srZ, 2);
        addSrBit(L.srV, 1); addSrBit(L.srC, 0);
        a.strH(11, 0, L.d + sem.eaReg * 4);
        return true;
    }

    if (sem.operation == SemanticOp::Move) {         // MOVE/MOVEA
        const int bits = bitsForSizeIndex(sem.sizeIndex);
        const int sm = sem.eaMode, sr = sem.eaReg;
        const int dm = sem.destinationMode, dr = sem.destinationReg;
        Ea src, dst;
        if (!decodeEa(in, sm, sr, bits, 0, src) ||
            !decodeEa(in, dm, dr, bits, src.ext, dst)) return false;
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
        MemoryAccessPlan srcAccess, dstAccess;
        if (src.memory) {
            srcAccess = memory.access(MemoryDirection::Read,
                                      MemoryOperand::Source,
                                      uint8_t(bits / 8), uint8_t(sm),
                                      uint8_t(sr));
            if (!srcAccess.valid()) return false;
        }
        if (dst.memory) {
            dstAccess = memory.access(MemoryDirection::Write,
                                      MemoryOperand::Destination,
                                      uint8_t(bits / 8), uint8_t(dm),
                                      uint8_t(dr));
            if (!dstAccess.valid()) return false;
        }
        if (!memory.complete()) return false;
        const bool restartWrite = L.is030 &&
                                  memory.proof.restartableLastWrite();
        const int dstCycles = restartWrite && dst.idx == E_IX
            ? 5 : kMoveDstA64[dst.idx];
        const int cycles = kEaReadA64[src.idx][sz] + dstCycles;
        // The emitted i-cache model owns miss penalties. Keep the historical
        // total-cost admission only for the restartable-write family: its
        // coarse-budget reproducer is still parked on x64, so widening that
        // particular proof by symmetry would be unsound.
        const unsigned tracedCycles = restartWrite
            ? unsigned(in.cycles) : traced030(L, in);
        if (cycles < 0 || tracedCycles != unsigned(cycles) ||
            in.words != unsigned(1 + src.ext + dst.ext)) return false;

        if (src.memory && dst.memory) {
            if (slow < 0) return false;
            // The two dominant cache-on MOVE fallbacks are the paired
            // longword shuttles abs.W -> -(A7) and (A7)+ -> abs.W. Probe
            // BOTH resident lines before reading either one: a miss can
            // then replay a pristine instruction, while two hits make the
            // source read and dirty destination write indivisible here.
            const bool cachePair = memory.proof.atomicCachePair();
            if (cachePair) {
                if (!srcAccess.cache || !dstAccess.cache) return false;
                addrOf(a, L, src, bits);
                memProbe(a, L, ir.super, 4, false, slow, true, -1,
                         true, false);
                a.strX(14, 1, 120);                  // source line bytes
                addrOf(a, L, dst, bits);
                const int writeHit = a.label();
                memProbe(a, L, ir.super, 4, true, slow, false, writeHit,
                         true, false);
                a.b(slow);                           // cache-only success branches
                a.bind(writeHit);

                // Publish both diagnostic hits only after both proofs. If
                // the W probe misses, exact replay owns the source hit and
                // these counters must not be double-incremented.
                a.ldrX(12, 0, L.cache040Hits);
                a.addImmX(12, 12, 2); a.strX(12, 0, L.cache040Hits);
                if (cache040LineReadStatsEnabled()) {
                    a.ldrX(12, 0, L.cache040NativeReadHits);
                    a.addImmX(12, 12, 1);
                    a.strX(12, 0, L.cache040NativeReadHits);
                    a.ldrX(12, 0, L.cache040NativeWriteHits);
                    a.addImmX(12, 12, 1);
                    a.strX(12, 0, L.cache040NativeWriteHits);
                }

                // Large Moira layout offsets use x15 as the assembler's
                // address scratch. Keep neither host pointer live across an
                // EA commit: source and destination get separate Frame
                // slots and are reloaded only at the actual access.
                a.strX(14, 1, 96);                   // destination line bytes
                commitEaBeforeAccess(a, L, src, bits, srcAccess);
                a.ldrX(14, 1, 120); loadGuest(a, bits, 11);
                commitEaAfterAccess(a, L, src, bits, srcAccess);
                commitEaBeforeAccess(a, L, dst, bits, dstAccess);
                a.ldrX(14, 1, 96); storeGuest(a, bits, 11);
                markCache040Dirty(a, 4);
                commitEaAfterAccess(a, L, dst, bits, dstAccess);
                emitLogicFlags(a, L, 11, bits);
                return true;
            }
            if (memory.proof.protocol != MemoryProofProtocol::PreflightAll ||
                !srcAccess.preflight || !dstAccess.preflight ||
                in.memory.order != MemoryOrder::SourceThenDestination)
                return false;
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
            a.strX(14, 1, 96);             // destination host pointer

            commitEaBeforeAccess(a, L, src, bits, srcAccess);
            a.ldrX(14, 1, 120);
            loadGuest(a, bits, 11);
            commitEaAfterAccess(a, L, src, bits, srcAccess);

            commitEaBeforeAccess(a, L, dst, bits, dstAccess);
            a.ldrX(14, 1, 96);
            storeGuest(a, bits, 11);
            commitEaAfterAccess(a, L, dst, bits, dstAccess);
            emitLogicFlags(a, L, 11, bits);
            return true;
        }

        if (src.memory) {
            if (slow < 0) return false;
            addrOf(a, L, src, bits);
            memLoadGuest(a, L, ir.super, bits, 11, slow, srcAccess, &src);
            if (!dst.memory)
                commitEaAfterAccess(a, L, src, bits, srcAccess);
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
                    ? in.terminalIrc : ir.prefetchWord(nextPc);
                a.movW(12, heldIrd); a.strH(12, 0, L.ird);
                a.movW(12, heldIrc); a.strH(12, 0, L.irc);
            }
            if (restartWrite && dst.idx == E_PI) {
                // PI is native only for a direct, faultless RAM mapping.
                // Probe before publishing CCR, PC/queue or the An update so
                // MMIO and /BERR reach Moira with a pristine entry boundary.
                memProbe(a, L, ir.super, bits / 8, true, slow);
                commitEaBeforeAccess(a, L, dst, bits, dstAccess);
                a.ldrW(11, 1, 72);
                emitLogicFlags(a, L, 11, bits);
                observeDirectWrite(a, bits, in.pc);
                storeGuest(a, bits, 11);
            } else {
                memStoreGuest(a, L, ir.super, bits, 11, slow,
                              dstAccess, &dst);
            }
            if (src.memory)
                commitEaAfterAccess(a, L, src, bits, srcAccess);
            commitEaAfterAccess(a, L, dst, bits, dstAccess);
        } else {
            if (dst.idx == E_AN && bits == 16) a.sxtH(11, 11);
            storeSized(a, L, 11, dst.idx == E_AN, unsigned(dst.reg),
                       dst.idx == E_AN ? 32 : bits);
        }
        if (dst.idx != E_AN && !(dst.memory && restartWrite))
            emitLogicFlags(a, L, 11, bits);
        return true;
    }

    if (sem.operation == SemanticOp::Bit ||
        sem.operation == SemanticOp::ImmediateAlu) {
        const int mode = sem.eaMode;
        if (sem.operation == SemanticOp::Bit) {      // BTST/BCHG/BCLR/BSET
            const bool dynamicBit = sem.dynamic;
            if (mode == 1) return false;             // MOVEP overlap / An
            const bool toReg = mode == 0;
            const int bits = toReg ? 32 : 8;
            const int action = sem.action;           // 0 test,1 xor,2 clear,3 set
            const int extUsed = dynamicBit ? 0 : 1;
            Ea dst;
            if (!decodeEa(in, mode, sem.eaReg, bits, extUsed, dst) ||
                dst.idx == E_AN || dst.idx == E_IM ||
                (action != 0 && dst.idx == E_DIPC) ||
                in.words != unsigned(1 + extUsed + dst.ext)) return false;
            const int sz = toReg ? 2 : 0;
            const int base = kEaReadA64[dst.idx][sz];
            const int cycles = toReg ? 4 : base + 2;
            if (base < 0 || traced030(L, in) != unsigned(cycles)) return false;
            MemoryAccessPlan read, write;
            if (dst.memory) {
                read = memory.access(MemoryDirection::Read,
                                     MemoryOperand::Operand, 1,
                                     uint8_t(mode), sem.eaReg);
                if (!read.valid()) return false;
                if (action != 0) {
                    write = memory.access(MemoryDirection::Write,
                                          MemoryOperand::Operand, 1,
                                          uint8_t(mode), sem.eaReg);
                    if (!memoryRmwAccessPair(read, write))
                        return false;
                }
            }
            if (!memory.complete()) return false;
            if (dst.memory) {
                if (slow < 0) return false;
                addrOf(a, L, dst, bits);
                if (action == 0)
                    memLoadGuest(a, L, ir.super, bits, 11, slow, read, &dst);
                else {
                    memProbe(a, L, ir.super, bits / 8, true, slow);
                    commitEaBeforeAccess(a, L, dst, bits, read);
                    loadGuest(a, bits, 11);
                }
            } else {
                loadSized(a, L, 11, false, unsigned(dst.reg), 32);
            }
            if (dynamicBit) {
                a.ldrW(10, 0, L.d + sem.registerIndex * 4);
                a.movW(12, toReg ? 31 : 7); a.andW(10, 10, 12);
                a.movW(12, 1); a.lslVarW(12, 12, 10);
            } else {
                const uint32_t bit = in.extensionWord(0) & 0xFF;
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
                    a.ldrW(10, 0, L.d + sem.registerIndex * 4);
                    a.movW(12, toReg ? 31 : 7); a.andW(10, 10, 12);
                    a.movW(12, 1); a.lslVarW(12, 12, 10);
                } else {
                    const uint32_t bit = in.extensionWord(0) & 0xFF;
                    a.movW(12, 1u << (bit & (toReg ? 31u : 7u)));
                }
                if (action == 1) a.eorW(11, 11, 12);
                else if (action == 2) { a.mvnW(12, 12); a.andW(11, 11, 12); }
                else a.orrW(11, 11, 12);
                if (dst.memory) storeGuest(a, bits, 11);
                else a.strW(11, 0, L.d + unsigned(dst.reg) * 4);
            }
            if (dst.memory)
                commitEaAfterAccess(a, L, dst, bits,
                                    action == 0 ? read : write);
            return true;
        }
        const int sz = sem.sizeIndex;
        if (sz > 2) return false;
        const AluOperation kind = sem.alu;
        if (kind == AluOperation::None) return false;
        const int bits = bitsForSizeIndex(sz);
        const int immExt = bits == 32 ? 2 : 1;
        Ea dst;
        if (!decodeEa(in, sem.eaMode, sem.eaReg, bits, immExt, dst) ||
            dst.idx == E_AN || dst.idx == E_IM || dst.idx == E_DIPC ||
            in.words != unsigned(1 + immExt + dst.ext)) return false;
        const int base = kEaReadA64[dst.idx][sz];
        const int cycles = kind == AluOperation::Cmp ? base
                           : (dst.idx == E_DN ? 2 : base + 2);
        if (base < 0 || traced030(L, in) != unsigned(cycles)) return false;
        MemoryAccessPlan read, write;
        if (dst.memory) {
            read = memory.access(MemoryDirection::Read,
                                 MemoryOperand::Destination,
                                 uint8_t(bits / 8), sem.eaMode, sem.eaReg);
            if (!read.valid()) return false;
            if (kind != AluOperation::Cmp) {
                write = memory.access(MemoryDirection::Write,
                                      MemoryOperand::Destination,
                                      uint8_t(bits / 8), sem.eaMode,
                                      sem.eaReg);
                if (!memoryRmwAccessPair(read, write))
                    return false;
            }
        }
        if (!memory.complete()) return false;
        if (dst.memory) {
            if (slow < 0) return false;
            addrOf(a, L, dst, bits);
            if (kind == AluOperation::Cmp)
                memLoadGuest(a, L, ir.super, bits, 9, slow, read, &dst);
            else {
                memProbe(a, L, ir.super, bits / 8, true, slow);
                commitEaBeforeAccess(a, L, dst, bits, read);
                loadGuest(a, bits, 9);
            }
        } else {
            loadSized(a, L, 9, false, unsigned(dst.reg), bits);
        }
        a.movW(10, jit::immediateValue(in, sem.sizeIndex));
        emitAluResult(a, L, kind, bits,
                      !dst.memory && kind != AluOperation::Cmp,
                      false, unsigned(dst.reg), kind != AluOperation::Cmp);
        if (dst.memory) {
            if (kind != AluOperation::Cmp) storeGuest(a, bits, 11);
            commitEaAfterAccess(a, L, dst, bits,
                                kind == AluOperation::Cmp ? read : write);
        }
        return true;
    }

    if (sem.operation == SemanticOp::SetCondition) { // Scc <ea>
        const int mode = sem.eaMode, reg = sem.eaReg;
        const int cc = sem.condition;
        const auto materialise = [&] {
            const int isTrue = a.label(), done = a.label();
            a.movW(11, 0);
            branchIfCond(a, L, cc, isTrue);
            a.b(done);
            a.bind(isTrue); a.movW(11, 0xFF);
            a.bind(done);
        };

        if (mode == 0) {
            if (in.words != 1 || traced030(L, in) != 4 ||
                !memory.complete()) return false;
            materialise();
            a.strB(11, 0, L.d + unsigned(reg) * 4);
            return true;
        }

        Ea dst;
        if (!decodeEa(in, mode, reg, 8, 0, dst) || !dst.memory ||
            dst.idx == E_DIPC || dst.idx == E_IM ||
            in.words != unsigned(1 + dst.ext) || slow < 0) return false;
        // execSccEa, 68020 cycle column. The brief-indexed form is native on
        // AArch64 and costs 13; full-format indexed plans remain rejected by
        // decodeEa().
        static const int8_t kScc[E_COUNT] =
            {-1,-1,10,10,11,11,13,10,10,-1,-1,-1};
        const int cycles = kScc[dst.idx];
        if (cycles < 0 || traced030(L, in) != unsigned(cycles)) return false;
        const MemoryAccessPlan write = memory.access(
            MemoryDirection::Write, MemoryOperand::Destination, 1,
            uint8_t(mode), uint8_t(reg));
        if (!write.valid() || !memory.complete()) return false;

        materialise();
        a.strW(11, 1, 72);
        addrOf(a, L, dst, 8);
        memStoreGuest(a, L, ir.super, 8, 11, slow, write, &dst);
        commitEaAfterAccess(a, L, dst, 8, write);
        return true;
    }

    if (sem.operation == SemanticOp::AddSubQuick) {
        const int sz = sem.sizeIndex, mode = sem.eaMode;
        if (sz > 2) return false;
        int imm = sem.registerIndex; if (!imm) imm = 8;
        const bool sub = sem.alu == AluOperation::Sub;
        const int bits = mode == 1 ? 32 : bitsForSizeIndex(sz);
        Ea dst;
        if (!decodeEa(in, mode, sem.eaReg, bits, 0, dst) ||
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
        const unsigned tracedCycles = traced030(L, in);
        if (base < 0 || tracedCycles != unsigned(cycles)) return false;
        MemoryAccessPlan read, write;
        if (dst.memory) {
            read = memory.access(MemoryDirection::Read,
                                 MemoryOperand::Destination,
                                 uint8_t(bits / 8), uint8_t(mode),
                                 sem.eaReg);
            write = memory.access(MemoryDirection::Write,
                                  MemoryOperand::Destination,
                                  uint8_t(bits / 8), uint8_t(mode),
                                  sem.eaReg);
            if (!memoryRmwAccessPair(read, write))
                return false;
        }
        if (!memory.complete()) return false;
        if (dst.memory) {
            if (slow < 0) return false;
            addrOf(a, L, dst, bits);
            memProbe(a, L, ir.super, bits / 8, true, slow);
            commitEaBeforeAccess(a, L, dst, bits, read);
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
        emitAluResult(a, L, sub ? AluOperation::Sub : AluOperation::Add,
                      bits, !dst.memory, false, unsigned(dst.reg), true);
        if (dst.memory) {
            storeGuest(a, bits, 11);
            commitEaAfterAccess(a, L, dst, bits, write);
        }
        return true;
    }

    if (sem.operation == SemanticOp::AluEaToReg ||
        sem.operation == SemanticOp::AluRegToEa ||
        sem.operation == SemanticOp::AddressAlu) {
        const int direction = sem.operation == SemanticOp::AluRegToEa ? 1 : 0;
        const int mode = sem.eaMode;

        if (sem.operation == SemanticOp::AddressAlu) { // ADDA/SUBA/CMPA
            const int srcBits = bitsForSizeIndex(sem.sizeIndex);
            const int sz = srcBits == 16 ? 1 : 2;
            Ea src;
            if (!decodeEa(in, mode, sem.eaReg, srcBits, 0, src) ||
                in.words != unsigned(1 + src.ext) ||
                traced030(L, in) != unsigned(kEaReadA64[src.idx][sz])) return false;
            MemoryAccessPlan read;
            if (src.memory) {
                read = memory.access(MemoryDirection::Read,
                                     MemoryOperand::Source,
                                     uint8_t(srcBits / 8), uint8_t(mode),
                                     sem.eaReg);
                if (!read.valid()) return false;
            }
            if (!memory.complete()) return false;
            if (src.memory) {
                if (slow < 0) return false;
                addrOf(a, L, src, srcBits);
                memLoadGuest(a, L, ir.super, srcBits, 10, slow, read, &src);
                commitEaAfterAccess(a, L, src, srcBits, read);
            } else if (src.idx == E_IM) a.movW(10, uint32_t(src.value));
            else loadSized(a, L, 10, src.idx == E_AN, unsigned(src.reg), srcBits);
            if (srcBits == 16) a.sxtH(10, 10);
            a.ldrW(9, 0, L.a + sem.registerIndex * 4);
            const AluOperation kind = sem.alu;
            if (kind == AluOperation::Add) a.addW(11, 9, 10);
            else a.subW(11, 9, 10);
            if (kind != AluOperation::Cmp)
                a.strW(11, 0, L.a + sem.registerIndex * 4);
            else
                emitAddSubFlags(a, L, 32, true, false);
            return true;
        }

        const int bits = bitsForSizeIndex(sem.sizeIndex);
        if (bits == 8 && mode == 1) return false;
        const AluOperation kind = sem.alu;
        if (kind == AluOperation::None) return false;
        const unsigned dn = sem.registerIndex;
        if (direction == 0) {
            const int sz = bits == 8 ? 0 : bits == 16 ? 1 : 2;
            Ea src;
            if (!decodeEa(in, mode, sem.eaReg, bits, 0, src) ||
                in.words != unsigned(1 + src.ext) ||
                traced030(L, in) != unsigned(kEaReadA64[src.idx][sz])) return false;
            MemoryAccessPlan read;
            if (src.memory) {
                read = memory.access(MemoryDirection::Read,
                                     MemoryOperand::Source,
                                     uint8_t(bits / 8), uint8_t(mode),
                                     sem.eaReg);
                if (!read.valid()) return false;
            }
            if (!memory.complete()) return false;
            if (src.memory) {
                if (slow < 0) return false;
                addrOf(a, L, src, bits);
                memLoadGuest(a, L, ir.super, bits, 10, slow, read, &src);
                commitEaAfterAccess(a, L, src, bits, read);
            } else if (src.idx == E_IM) a.movW(10, uint32_t(src.value));
            else loadSized(a, L, 10, src.idx == E_AN, unsigned(src.reg), bits);
            loadSized(a, L, 9, false, dn, bits);       // destination
            return emitAluResult(a, L, kind, bits, kind != AluOperation::Cmp,
                                 false, dn, kind != AluOperation::Cmp);
        }
        if (mode == 0) {                                // EOR Dn,Dm
            if (kind != AluOperation::Eor || in.words != 1 ||
                traced030(L, in) != 2)
                return false;
            loadSized(a, L, 9, false, sem.eaReg, bits);
            loadSized(a, L, 10, false, dn, bits);
            return emitAluResult(a, L, kind, bits, true, false,
                                 sem.eaReg, false);
        }
        // Register-to-memory ALU: one writable translation serves the RMW,
        // so the fallback is still entered before any guest-visible change.
        const int sz = bits == 8 ? 0 : bits == 16 ? 1 : 2;
        Ea dst;
        if (!decodeEa(in, mode, sem.eaReg, bits, 0, dst) || !dst.memory ||
            dst.idx == E_DIPC || in.words != unsigned(1 + dst.ext)) return false;
        const int cycles = kEaReadA64[dst.idx][sz] + 2;
        if (traced030(L, in) != unsigned(cycles) || slow < 0) return false;
        const MemoryAccessPlan read = memory.access(
            MemoryDirection::Read, MemoryOperand::Destination,
            uint8_t(bits / 8), uint8_t(mode), sem.eaReg);
        const MemoryAccessPlan write = memory.access(
            MemoryDirection::Write, MemoryOperand::Destination,
            uint8_t(bits / 8), uint8_t(mode), sem.eaReg);
        if (!memoryRmwAccessPair(read, write) || !memory.complete())
            return false;
        addrOf(a, L, dst, bits);
        memProbe(a, L, ir.super, bits / 8, true, slow);
        commitEaBeforeAccess(a, L, dst, bits, read);
        loadGuest(a, bits, 9);
        loadSized(a, L, 10, false, dn, bits);
        emitAluResult(a, L, kind, bits, false, false, 0,
                      kind == AluOperation::Add || kind == AluOperation::Sub);
        storeGuest(a, bits, 11);
        commitEaAfterAccess(a, L, dst, bits, write);
        return true;
    }

    if (sem.operation == SemanticOp::Link ||
        sem.operation == SemanticOp::Unlink ||
        sem.operation == SemanticOp::Lea ||
        sem.operation == SemanticOp::Pea ||
        sem.operation == SemanticOp::Movem ||
        sem.operation == SemanticOp::Extend ||
        sem.operation == SemanticOp::Swap ||
        sem.operation == SemanticOp::Test ||
        sem.operation == SemanticOp::Clear ||
        sem.operation == SemanticOp::Negate ||
        sem.operation == SemanticOp::Complement) {
        const int mode = sem.eaMode, sz = sem.sizeIndex;
        if (sem.operation == SemanticOp::Link) {     // LINK.W An,#d16
            if (traced030(L, in) != 5 || in.words != 2 || slow < 0)
                return false;
            const unsigned an = sem.eaReg;
            const MemoryAccessPlan write = memory.access(
                MemoryDirection::Write, MemoryOperand::Stack, 4, 4, 7);
            if (!write.valid() || !memory.complete() ||
                write.eaCommit != EaCommit::BeforeAccess)
                return false;
            const int32_t disp = int16_t(in.extensionWord(0));
            a.ldrW(9, 0, L.a + 7 * 4); a.subImmW(9, 9, 4);
            a.strW(9, 1, 44);                       // new frame/address
            if (an == 7) a.movRegW(11, 9);
            else a.ldrW(11, 0, L.a + an * 4);
            a.strW(11, 1, 72);
            memStoreGuest(a, L, ir.super, 32, 11, slow, write);
            a.ldrW(9, 1, 44); a.strW(9, 0, L.a + an * 4);
            a.movW(10, uint32_t(disp)); a.addW(9, 9, 10);
            a.strW(9, 0, L.a + 7 * 4);
            return true;
        }
        if (sem.operation == SemanticOp::Unlink) {   // UNLK An
            if (traced030(L, in) != 6 || in.words != 1 || slow < 0)
                return false;
            const unsigned an = sem.eaReg;
            const MemoryAccessPlan read = memory.access(
                MemoryDirection::Read, MemoryOperand::Stack, 4, 2,
                uint8_t(an));
            if (!read.valid() || !memory.complete()) return false;
            a.ldrW(9, 0, L.a + an * 4); a.strW(9, 1, 44);
            memLoadGuest(a, L, ir.super, 32, 11, slow, read);
            a.strW(11, 0, L.a + an * 4);
            if (an != 7) {
                a.ldrW(9, 1, 44); a.addImmW(9, 9, 4);
                a.strW(9, 0, L.a + 7 * 4);
            }
            return true;
        }
        if (sem.operation == SemanticOp::Lea) {      // LEA <ea>,An
            Ea src;
            if (!decodeEa(in, mode, sem.eaReg, 32, 0, src) || !src.memory ||
                src.idx == E_PI || src.idx == E_PD ||
                in.words != unsigned(1 + src.ext)) return false;
            static const int8_t cost[E_COUNT] =
                {-1,-1,6,-1,-1,7,-1,6,6,7,-1,-1};
            if (cost[src.idx] < 0 ||
                traced030(L, in) != unsigned(cost[src.idx])) return false;
            addrOf(a, L, src, 32);
            a.strW(9, 0, L.a + sem.registerIndex * 4);
            return true;
        }
        if (sem.operation == SemanticOp::Pea) {      // PEA <ea>
            // Compute the source address before touching A7: PEA (A7) and
            // PEA d16(A7) use the old stack pointer as their source base.
            Ea src;
            if (!decodeEa(in, mode, sem.eaReg, 32, 0, src) || !src.memory ||
                src.idx == E_PI || src.idx == E_PD ||
                in.words != unsigned(1 + src.ext) || slow < 0) return false;
            static const int8_t cost[E_COUNT] =
                {-1,-1,9,-1,-1,10,12,9,9,10,12,-1};
            if (cost[src.idx] < 0 ||
                traced030(L, in) != unsigned(cost[src.idx])) return false;
            const MemoryAccessPlan write = memory.access(
                MemoryDirection::Write, MemoryOperand::Stack, 4, 4, 7);
            if (!write.valid() || !memory.complete()) return false;

            addrOf(a, L, src, 32);
            a.movRegW(11, 9); a.strW(11, 1, 72);
            a.ldrW(9, 0, L.a + 7 * 4); a.subImmW(9, 9, 4);
            a.strW(9, 1, 44);
            memStoreGuest(a, L, ir.super, 32, 11, slow, write);
            a.ldrW(9, 1, 44); a.strW(9, 0, L.a + 7 * 4);
            return true;
        }
        if (sem.operation == SemanticOp::Movem) {
            // Same explicit 030 refusal as x64 (JIT_BRINGUP § C.4.4): the
            // 030 MOVEM restart contract is unmodelled, and the cost
            // cross-check's i-cache side effect is not a guard.
            if (L.is030) return false;
            const bool toRegs = sem.toRegisters;
            const int bits = bitsForSizeIndex(sem.sizeIndex), bytes = bits / 8;
            const uint16_t mask = in.extensionWord(0);
            if (!mask || slow < 0) return false;
            Ea ea;
            if (!decodeEa(in, mode, sem.eaReg, bits, 1, ea) || !ea.memory ||
                in.words != unsigned(2 + ea.ext)) return false;
            if ((toRegs && ea.idx == E_PD) ||
                (!toRegs && (ea.idx == E_PI || ea.idx == E_DIPC))) return false;
            int n = 0; for (int b = 0; b < 16; b++) n += (mask >> b) & 1;
            static const int8_t toRegBase[E_COUNT] =
                {-1,-1,12,8,-1,13,-1,12,12,9,-1,-1};
            static const int8_t toMemBase[E_COUNT] =
                {-1,-1,8,-1,4,9,-1,8,8,-1,-1,-1};
            const int baseCost = toRegs ? toRegBase[ea.idx] : toMemBase[ea.idx];
            if (baseCost < 0 ||
                traced030(L, in) != unsigned(baseCost + 4 * n)) return false;
            const MemoryAccessPlan span = memory.access(
                toRegs ? MemoryDirection::Read : MemoryDirection::Write,
                MemoryOperand::RegisterList, uint8_t(bytes), uint8_t(mode),
                sem.eaReg);
            const MemoryOrder emittedOrder = ea.idx == E_PD
                ? MemoryOrder::RegisterDescending
                : MemoryOrder::RegisterAscending;
            if (!span.valid() || !span.preflight || !memory.complete() ||
                memory.proof.protocol != MemoryProofProtocol::OrderedSpan ||
                in.memory.order != emittedOrder ||
                span.eaCommit != EaCommit::PerElement)
                return false;
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
        if (sem.operation == SemanticOp::Extend) {   // EXT
            if (traced030(L, in) != 4 || in.words != 1) return false;
            const unsigned dn = sem.eaReg;
            if (sem.sizeIndex == 2) {
                a.ldrH(11, 0, L.d + dn * 4); a.sxtH(11, 11);
                a.strW(11, 0, L.d + dn * 4); emitLogicFlags(a, L, 11, 32);
            } else {
                a.ldrB(11, 0, L.d + dn * 4); a.sxtB(11, 11);
                a.strH(11, 0, L.d + dn * 4); maskResult(a, 11, 16);
                emitLogicFlags(a, L, 11, 16);
            }
            return true;
        }
        if (sem.operation == SemanticOp::Swap) {     // SWAP
            if (traced030(L, in) != 4 || in.words != 1) return false;
            a.ldrW(11, 0, L.d + sem.eaReg * 4); a.rorW(11, 11, 16);
            a.strW(11, 0, L.d + sem.eaReg * 4);
            emitLogicFlags(a, L, 11, 32);
            return true;
        }
        if (sz > 2) return false;
        const int bits = bitsForSizeIndex(sz);
        if (sem.operation != SemanticOp::Test &&
            sem.operation != SemanticOp::Clear &&
            sem.operation != SemanticOp::Complement &&
            sem.operation != SemanticOp::Negate) return false;
        Ea ea;
        if (!decodeEa(in, mode, sem.eaReg, bits, 0, ea) ||
            in.words != unsigned(1 + ea.ext) || ea.idx == E_IM ||
            (bits == 8 && ea.idx == E_AN)) return false;
        const bool tst = sem.operation == SemanticOp::Test;
        if (!tst && ea.idx == E_AN) return false;
        const int base = kEaReadA64[ea.idx][sz];
        const int cycles = tst ? base : (ea.idx <= E_AN ? 2 : base + 2);
        // TST (An) is read-only. Ordinarily its measured base component must
        // match the inline access cost. The hot LC II 4A11 exception below
        // uses the exact MMU/device thunk, so the device reproduces the
        // variable part of that component at run time.
        const unsigned tracedCycles = traced030(L, in);
        MemoryAccessPlan read, write;
        if (ea.memory) {
            if (tst) {
                read = memory.access(MemoryDirection::Read,
                                     MemoryOperand::Operand,
                                     uint8_t(bits / 8), uint8_t(mode),
                                     sem.eaReg);
                if (!read.valid()) return false;
            } else if (sem.operation == SemanticOp::Clear) {
                write = memory.access(MemoryDirection::Write,
                                      MemoryOperand::Destination,
                                      uint8_t(bits / 8), uint8_t(mode),
                                      sem.eaReg);
                if (!write.valid()) return false;
            } else {
                read = memory.access(MemoryDirection::Read,
                                     MemoryOperand::Destination,
                                     uint8_t(bits / 8), uint8_t(mode),
                                     sem.eaReg);
                write = memory.access(MemoryDirection::Write,
                                      MemoryOperand::Destination,
                                      uint8_t(bits / 8), uint8_t(mode),
                                      sem.eaReg);
                if (!memoryRmwAccessPair(read, write))
                    return false;
            }
        }
        if (!memory.complete()) return false;
        const bool exact030 = read.valid() && read.exactRequired;
        if (base < 0 || (exact030
                ? (in.baseCycles < unsigned(cycles) || in.postExceptionCycles != 0)
                : tracedCycles != unsigned(cycles))) return false;
        if (ea.memory) {
            if (slow < 0) return false;
            addrOf(a, L, ea, bits);
            if (tst)
                memLoadGuest(a, L, ir.super, bits, 9, slow, read, &ea);
            else if (sem.operation != SemanticOp::Clear) {
                memProbe(a, L, ir.super, bits / 8, true, slow);
                commitEaBeforeAccess(a, L, ea, bits, read);
                loadGuest(a, bits, 9);
            }
        } else {
            loadSized(a, L, 9, ea.idx == E_AN, unsigned(ea.reg), bits);
            maskResult(a, 9, bits);
        }
        switch (sem.operation) {
            case SemanticOp::Test: emitLogicFlags(a, L, 9, bits); break;
            case SemanticOp::Clear: a.movW(11, 0); break;
            case SemanticOp::Complement:
                a.mvnW(11, 9); maskResult(a, 11, bits);
                emitLogicFlags(a, L, 11, bits); break;
            case SemanticOp::Negate:
                a.movRegW(10, 9); a.movW(9, 0); a.subW(11, 9, 10);
                maskResult(a, 11, bits);
                emitAddSubFlags(a, L, bits, true, true); break;
            default: return false;
        }
        if (!tst) {
            if (ea.memory) {
                if (sem.operation == SemanticOp::Clear) {
                    a.strW(11, 1, 72);
                    addrOf(a, L, ea, bits);
                    memStoreGuest(a, L, ir.super, bits, 11, slow, write, &ea);
                } else {
                    storeGuest(a, bits, 11);
                }
            }
            else storeSized(a, L, 11, false, unsigned(ea.reg), bits);
        }
        if (ea.memory)
            commitEaAfterAccess(a, L, ea, bits,
                                tst ? read : write);
        if (sem.operation == SemanticOp::Clear)
            emitLogicFlags(a, L, 11, bits);
        return true;
    }

    if (sem.operation == SemanticOp::Bitfield) {     // register bitfield
        if (sem.eaMode != 0 || in.words != 2) return false;
        const uint16_t ext = in.extensionWord(0);
        if ((ext & 0x0820) != 0) return false;       // register offset/width later
        const int kind = sem.action;
        const unsigned dst = sem.eaReg, out = (ext >> 12) & 7;
        const unsigned offset = (ext >> 6) & 31;
        unsigned width = ext & 31; if (!width) width = 32;
        static const uint8_t cycles[8] = {6,8,12,8,12,18,12,10};
        if (traced030(L, in) != cycles[kind]) return false;
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

    if (sem.operation == SemanticOp::ShiftRegister) {
        const int sz = sem.sizeIndex, type = sem.action;
        if (sz > 2 || sem.dynamic || type == 2 || in.words != 1) return false;
        const int bits = bitsForSizeIndex(sz);
        int count = sem.registerIndex; if (!count) count = 8;
        const bool left = sem.left;
        const int expected = type == 1 ? 4 : type == 3 ? 8 : left ? 8 : 6;
        if (traced030(L, in) != unsigned(expected)) return false;
        const unsigned dn = sem.eaReg;
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
                     uint32_t linkMask, bool icache,
                     IcacheShadow& icacheShadow) {
    const uint16_t op = in.opcode;
    const InstructionSemantics& sem = in.semantics;
    const ControlFlowPlan& control = in.control;
    const uint16_t entryLookahead = ir.prefetchWord(in.pc + 2);
    // Mode-5 has no tail refill. Instructions that consume all but their
    // last extension with readExt(), then SKIP_LAST_RD/fullPrefetch(), hold
    // the last encoded word. A one-word transfer still holds its entry
    // lookahead. The traced path below must confirm every formula before a
    // 68030 branch emitter is admitted.
    const uint16_t lastHeld = in.words > 1
        ? in.extensionWord(in.words - 2) : entryLookahead;
    const auto tracedQueueIs = [&](uint16_t irc) {
        return !L.is030 || !in.terminalQueueValid ||
               (in.terminalIrd == op && in.terminalIrc == irc);
    };

    if (sem.operation == SemanticOp::ReturnSubroutine) {
        if (!control.valid || control.kind != ControlFlowKind::Return ||
            in.words != 1 || traced030(L, in) != 10) return false;
        auto memory = instructionMemoryPlan(in.memory, proofOptions(L));
        const MemoryAccessPlan read = memory.access(
            MemoryDirection::Read, MemoryOperand::Stack, 4, 3, 7);
        if (!read.valid() || !memory.complete()) return false;
        a.ldrW(9, 0, L.a + 7 * 4);
        memLoadGuest(a, L, ir.super, 32, 11, slow, read);
        a.movW(12, 1); a.andW(9, 11, 12); a.cbnzW(9, slow);
        a.ldrW(9, 0, L.a + 7 * 4); a.addImmW(9, 9, 4);
        a.strW(9, 0, L.a + 7 * 4);
        a.strW(11, 0, L.pc); a.strW(11, 0, L.pc0);
        if (!tracedQueueIs(entryLookahead)) return false;
        if (icache) chargeIcache(a, L, ir, in, icacheShadow);
        commitQueue(a, L, op, entryLookahead);
        chargeAndRetire(a, L, 10, paced, batch, in.pc,
                        uint32_t(in.words) + 1, ir.super);
        leaveToDynamic(a, L, ir, linkMask, epilogue);
        return true;
    }

    if (sem.operation == SemanticOp::BranchSubroutine) {
        const unsigned bsrCost = traced030(L, in);
        if (!control.valid || control.kind != ControlFlowKind::DirectCall ||
            !control.targetKnown || !control.pushesReturnAddress ||
            in.words > 3 || (bsrCost != 7 && bsrCost != 5)) return false;
        auto memory = instructionMemoryPlan(in.memory, proofOptions(L));
        const MemoryAccessPlan write = memory.access(
            MemoryDirection::Write, MemoryOperand::Stack, 4, 4, 7);
        if (!write.valid() || !memory.complete()) return false;
        const uint32_t target = control.target;
        if (target & 1) return false;
        a.ldrW(9, 0, L.a + 7 * 4); a.subImmW(9, 9, 4);
        a.strW(9, 1, 44);
        a.movW(11, control.returnAddress); a.strW(11, 1, 72);
        // The return-address push is BSR's only data access. Route it
        // through the same write-authorized copyback proof as MOVE stores;
        // a miss reaches the untouched instruction before A7 or the target
        // boundary is committed.
        memStoreGuest(a, L, ir.super, 32, 11, slow, write);
        a.ldrW(9, 1, 44); a.strW(9, 0, L.a + 7 * 4);
        if (!tracedQueueIs(lastHeld)) return false;
        if (icache) chargeIcache(a, L, ir, in, icacheShadow);
        commitBoundary(a, L, target); commitQueue(a, L, op, lastHeld);
        chargeAndRetire(a, L, bsrCost, paced, batch, in.pc,
                        uint32_t(in.words) + 1, ir.super);
        leaveTo(a, ir, target, linkMask, epilogue);
        return true;
    }

    if (sem.operation == SemanticOp::JumpSubroutine ||
        sem.operation == SemanticOp::Jump) {
        const bool jsr = sem.operation == SemanticOp::JumpSubroutine;
        if (!control.valid || control.pushesReturnAddress != jsr) return false;
        auto memory = instructionMemoryPlan(in.memory, proofOptions(L));
        MemoryAccessPlan write;
        if (jsr) {
            write = memory.access(MemoryDirection::Write,
                                  MemoryOperand::Stack, 4, 4, 7);
            if (!write.valid()) return false;
        }
        if (!memory.complete()) return false;
        Ea ea;
        if (!decodeEa(in, sem.eaMode, sem.eaReg, 32, 0, ea) ||
            !ea.memory || ea.idx == E_PI || ea.idx == E_PD ||
            in.words != unsigned(1 + ea.ext)) return false;
        static const int8_t cost[E_COUNT] =
            {-1,-1,4,-1,-1,5,-1,4,4,5,-1,-1};
        if (cost[ea.idx] < 0 ||
            traced030(L, in) != unsigned(cost[ea.idx])) return false;
        addrOf(a, L, ea, 32);
        a.strW(9, 1, 48);                           // target (Frame::saveV)
        a.movW(12, 1); a.andW(9, 9, 12); a.cbnzW(9, slow);
        if (jsr) {
            a.ldrW(9, 0, L.a + 7 * 4); a.subImmW(9, 9, 4);
            a.strW(9, 1, 44);
            a.movW(11, control.returnAddress); a.strW(11, 1, 72);
            memStoreGuest(a, L, ir.super, 32, 11, slow, write);
            a.ldrW(9, 1, 44); a.strW(9, 0, L.a + 7 * 4);
        }
        a.ldrW(11, 1, 48);
        a.strW(11, 0, L.pc); a.strW(11, 0, L.pc0);
        if (!tracedQueueIs(lastHeld)) return false;
        if (icache) chargeIcache(a, L, ir, in, icacheShadow);
        commitQueue(a, L, op, lastHeld);
        chargeAndRetire(a, L, unsigned(cost[ea.idx]), paced, batch, in.pc,
                        uint32_t(in.words) + 1, ir.super);
        const bool constant = control.targetKnown;
        if (constant) leaveTo(a, ir, control.target, linkMask, epilogue);
        else leaveToDynamic(a, L, ir, linkMask, epilogue);
        return true;
    }

    if (sem.operation == SemanticOp::Branch) {
        const int cc = sem.condition;
        if (!control.valid || !control.targetKnown || in.words > 3) return false;
        const uint32_t target = control.target;
        const uint32_t fall = control.fallthrough;
        if (target & 1) return false;
        const int takenCycles = cc == 0 ? 10 : 6;
        const int fallCycles = in.words == 1 ? 4 : 6;
        const unsigned traced = traced030(L, in);
        if (traced != unsigned(takenCycles) &&
            traced != unsigned(fallCycles)) return false;

        const uint16_t takenIrc = lastHeld;
        const uint16_t fallIrc = ir.prefetchWord(fall);
        if (L.is030 && in.terminalQueueValid) {
            const bool targetOnly = in.observedNextPc == target && target != fall;
            const bool fallOnly = in.observedNextPc == fall && target != fall;
            if (in.terminalIrd != op ||
                (targetOnly && in.terminalIrc != takenIrc) ||
                (fallOnly && in.terminalIrc != fallIrc) ||
                (!targetOnly && !fallOnly && in.terminalIrc != takenIrc &&
                 in.terminalIrc != fallIrc)) return false;
        }

        // A conditional Bcc.W fetches pc and pc+2 on both paths; only the
        // fall-through consumes the displacement with readExt and fetches
        // pc+4. Charge the common prefix here and the path-specific word
        // below. Longer conditional forms remain refused by compile().
        const bool bccWord = L.is030 && icache && in.words == 2 && cc != 0;
        if (icache)
            chargeIcache(a, L, ir, in, icacheShadow, bccWord ? 2u : 0u);

        const int taken = a.label();
        branchIfCond(a, L, cc, taken);
        // Not taken.
        if (bccWord) chargeIcacheExtraWord(a, L, ir, in.pc + 4);
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

    if (sem.operation == SemanticOp::DecrementBranch) {
        if (!control.valid || control.kind != ControlFlowKind::DecrementBranch ||
            !control.targetKnown || in.words != 2 ||
            (traced030(L, in) != 6 && traced030(L, in) != 10)) return false;
        const int cc = sem.condition, dn = sem.eaReg;
        const uint32_t target = control.target;
        const uint32_t fall = control.fallthrough;
        if (target & 1) return false;
        if (!tracedQueueIs(entryLookahead)) return false;
        if (icache) chargeIcache(a, L, ir, in, icacheShadow, 2);
        const int condTrue = a.label(), expired = a.label();
        if (cc != 1) branchIfCond(a, L, cc, condTrue);
        a.ldrH(9, 0, L.d + unsigned(dn) * 4);        // pre-decrement value
        a.subImmW(10, 9, 1);
        a.strH(10, 0, L.d + unsigned(dn) * 4);
        a.cbzW(9, expired);

        commitBoundary(a, L, target);
        commitQueue(a, L, op, in.extensionWord(0));
        chargeAndRetire(a, L, 6, paced, batch, in.pc,
                        uint32_t(in.words) + 1, ir.super);
        { const int ti = findTarget(ir, target);
          if (ti >= 0) a.b(entries[size_t(ti)]);
          else leaveTo(a, ir, target, linkMask, epilogue); }

        a.bind(expired);
        commitBoundary(a, L, fall);
        commitQueue(a, L, op, in.extensionWord(0));
        chargeAndRetire(a, L, 10, paced, batch, in.pc,
                        uint32_t(in.words) + 1, ir.super);
        leaveTo(a, ir, fall, linkMask, epilogue);

        a.bind(condTrue);
        commitBoundary(a, L, fall);
        commitQueue(a, L, op, in.extensionWord(0));
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
        // The 68030 emitter is lockstep-clean at the real 260480-cycle LC II
        // frame cadence, and since 2026-08-18 the declaration says so: an explicit
        // POM68K_JIT_BACKEND=a64 on a 68030 is honoured without the unsafe
        // override.
        c.guestFamilies = kGuest68040 | kGuest68030;
        // But NOT 030 in `autoFamilies`: charge-on-success and restart-write
        // linking are now conformant, but the 6,000-frame AArch64 result has
        // not beaten `threaded` (D.1 condition 3). Promotion is earned by a
        // repeated matching-fingerprint bench, never by backend symmetry.
        c.autoFamilies = kGuest68040;
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
    int diagLeft_ = -1;
};

Compiled* A64Backend::compile(const BlockIr& ir, const Context& ctx) {
    if (diagLeft_ < 0) diagLeft_ = verboseBlocks();
    gRuntimeReasonHisto = ctx.slowRuntimeReasonHisto != nullptr;
    gExactStoreGuardOpcode = a64StoreGuardOpcode();
    const auto reject = [this](const char* why) -> Compiled* {
        if (verbose() && diagLeft_ > 0) {
            diagLeft_--;
            std::fprintf(stderr, "[jit/a64] refused: %s\n", why);
        }
        return nullptr;
    };
    if (!ctx.cpu || ir.instrs.empty() || ir.code.empty() ||
        !ctx.cpu->pomJitSimpleIpl()) return reject("empty/context/IPL mode");
    if (verbose() && diagLeft_ > 0) {
        diagLeft_--;
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
                       a64PacingEnabled();
    const int batch = paced ? ctx.periphBatch : 0;
    // Restartable-write blocks may link once the i-cache charge sits past the
    // last runtime bail. The old chain-boundary divergence was the same stale
    // pre-charge signature: a helper could decline the replay after counters
    // and clock had already moved for an instruction that never executed.
    const uint32_t linkMask = ctx.linkTable ? ctx.linkMask : 0;

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
        markRuntimeReason(a, RuntimeOther);
        clearRuntimeAccess(a);
        exits.push_back({a.label(), a.label()});
        a.ldrX(10, 1, 0);                // Frame::clockTarget
        a.cmpX(21, 10);
        a.bCond(Asm::GE, exits.back().budget);
        a.ldrW(9, 0, L.flags);
        a.cbnzW(9, exits.back().flags);

        const Instr& in = ir.instrs[i];
        gCurrentOpcode = in.opcode;
        // Multi-word control flow needs a proved path-specific fetch model.
        // DBcc fetches exactly two words on all three paths. Conditional
        // Bcc.W has two common words plus pc+4 on fall-through. JSR d16(PC)
        // is a single path with a traced count. BSR.W and wider transfers
        // remain refused: the x64 lockstep observed a miss-for-hit swap when
        // those paper proofs were widened.
        const bool dbcc =
            in.semantics.operation == SemanticOp::DecrementBranch;
        const bool bccWord = icache && in.words == 2 &&
            in.semantics.operation == SemanticOp::Branch &&
            in.semantics.condition != 0;
        const bool jsrD16Pc = icache && in.words == 2 &&
            in.opcode == 0x4EBA &&
            in.semantics.operation == SemanticOp::BranchSubroutine;
        if (icache && in.kind == Kind::Branch && in.words > 1 &&
            !dbcc && !bccWord && !jsrD16Pc) {
            a.b(slowStatic[i]);
            continue;
        }
        // Do not guess an instruction-stream fetch count from encoded length:
        // SKIP_LAST_RD forms are the counterexample. A missing count also
        // invalidates the compile-time shadow, so later instructions perform
        // full runtime tag/valid checks instead of folding a presumed hit.
        if (icache && !dbcc && !in.fetchWords) {
            icacheShadow = {};
            a.b(slowStatic[i]);
            continue;
        }
        // A successful exact MMIO thunk may call Cpu030::stall/catchUp from
        // inside the operand access. If this instruction can still miss in
        // the i-cache, post-success charging would present that access to
        // peripherals four cycles too early. Replay the whole instruction
        // unless the block-local shadow proves a zero-penalty fetch. Plain
        // DTLB accesses and exact accesses on proved hits stay native.
        const bool exactDataThunk =
            memoryProofPlan(in.memory, proofOptions(L)).exactThunkMask != 0;
        const Asm::Mark mark = a.mark();
        if (icache && exactDataThunk &&
            !shadowProvesIcacheHits(ir, in, icacheShadow,
                                    dbcc ? 2u : 0u))
            guardIcacheHits(a, L, ir, in, icacheShadow, slowRuntime[i],
                            dbcc ? 2u : 0u);
        // mmu040InstrStart's POLL_IPL sample.
        a.ldrB(9, 0, L.iplPin);
        a.strB(9, 0, L.regIpl);

        bool native = false;
        if (in.kind == Kind::Branch) {
            native = emitBranchInstr(a, L, ir, in, entries, epilogue,
                                     slowRuntime[i],
                                     paced, batch, linkMask, icache,
                                     icacheShadow);
        } else if (canEmit(in.opcode)) {
            native = emitRegInstr(a, L, ir, in, slowRuntime[i]);
        }
        if (!native) {
            // An emitter is allowed to discover a length/cycle mismatch
            // after writing a few instructions. Erase that partial attempt
            // and hand the untouched guest instruction to Moira.
            a.rewind(mark);
            if (icache)
                shadowIcache(ir, in, icacheShadow, dbcc ? 2u : 0u);
            a.b(slowStatic[i]);
            continue;
        }
        nativeCount++;
        if (in.kind == Kind::Branch) continue;

        // Charge only after the instruction body has crossed its last
        // runtime-bail edge. A refused re-run can then never leave cache
        // counters, tags or miss cycles for an instruction that did not run.
        if (icache) chargeIcache(a, L, ir, in, icacheShadow);

        const uint16_t ird = in.terminalQueueValid
            ? in.terminalIrd : in.opcode;
        const uint16_t irc = in.terminalQueueValid
            ? in.terminalIrc
            : ir.prefetchWord(in.pc + uint32_t(in.words) * 2);
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
        // TST.B (A1) ($4A11) is the one exact 030 access whose helper owns
        // a variable device delay, so generated code owes only the fixed
        // six-cycle instruction component. `exactRequired` is a memory-
        // contract property, not a timing class: restartable LINK/MOVE/PEA
        // writes carry it too and must retain their own traced base cost.
        const bool exactTst030 = L.is030 && in.opcode == 0x4A11 &&
                                 memoryRequiresExactAccess(in.memory);
        const uint16_t nativeCycles = exactTst030
            ? uint16_t(kEaReadA64[E_AI][0])
            : L.is030 ? in.baseCycles : in.cycles;
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
        if (ctx.slowRuntimeReasonHisto) {
            // Flat [reason][opcode] table. One reason plane is 65536 * 8
            // bytes, hence the 19-bit shift.
            a.ldrX(14, 1, 128);
            a.ldrW(9, 1, 144);
            a.lslX(9, 9, 19);
            a.addX(14, 14, 9);
            a.address(15, 14, uint32_t(ir.instrs[i].opcode) * 8);
            a.ldrX(9, 15, 0);
            a.addImmX(9, 9, 1);
            a.strX(9, 15, 0);
        }
        if (ctx.runtimeAddressObserver)
            observeRuntimeAddress(a, ir.instrs[i].opcode);
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
    c->linked = dst + linkEntryOffset;
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
    f.slowRuntimeReasonHisto = ctx.slowRuntimeReasonHisto;
    f.dtlbFillReason = ctx.dtlbFillReason;
    f.runtimeAddressObserver = ctx.runtimeAddressObserver;
    f.runtimeAddressSelf = ctx.runtimeAddressSelf;
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
