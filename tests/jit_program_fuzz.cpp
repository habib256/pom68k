// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — MIT (see LICENSE-MIT)
//
// Random multi-instruction programs, interpreter against translator.
//
// The SST replays (tests/SstJitLockstep.h) judge the translator one oracle
// vector at a time: one instruction, or a short `steps` chain. This fuzzer
// judges what those vectors cannot reach — blocks of dozens of instructions
// that the engine traces, compiles, links and re-enters across loop laps,
// slice boundaries and exceptions taken in the middle of generated code.
//
// Each seed builds one program in a flat 16 MB bus with no Macintosh object:
//
//   head:  SUBQ.L #1,counter ; BEQ done
//          MOVEA.L #ptr,A0..A6 ; MOVEA.L #stack,A7
//          <N random instructions>
//          BRA head
//   done:  BRA done
//
// and every exception vector points at `JMP head`, so a trap costs one lap
// instead of the run. The body is drawn from opcode templates (ALU, shifts,
// bit and bit-field operations, MOVEM, multiply/divide, BCD, Bcc/DBcc/Scc,
// ...) over register and memory operands, plus a share of raw random words
// that reach whatever the decoder holds. The interpreter is the reference
// (CLAUDE.md, "The interpreter is the accuracy oracle"); both cores run to
// the same random slice targets and must agree on every register, the
// clock and the whole bus after each slice.
//
//   jit_program_fuzz [--model 0|20|30|40] [--seeds N] [--first S] [--extra S,S..]
//                    [--len N] [--raw PERMILLE] [--slices N] [--budget CYCLES]
//                    [-v] [--trace] [--shrink] [--bisect] [--watch-jit]
//                    [--patch ADDR=WORD] [--progress] [--smc] [--irq] [--irq-timed] [--fpu] [--ext] [--savestate] [--dcache] [--mmu]
//
// A failure prints the seed; `--first S --seeds 1 -v` replays it alone.
// `--shrink` turns body instructions into NOPs while a divergence remains,
// `--bisect` finds the smallest single-slice clock target that still
// diverges, `--trace` adds the interpreter's instruction trace and its
// stores into the program, and POM68K_JIT_DISPATCH_RING=1 the engine's last
// dispatches. A host fault in generated code prints lr, the code before it
// and the ring (onCrash).

#include "Moira.h"
#include "JitTestConfig.h"
#include "SaveState.h"
#include "jit/JitEngine.h"

#include <algorithm>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#if defined(__APPLE__) && defined(__aarch64__)
#include <sys/ucontext.h>
#endif

namespace {

// SIGSEGV inside generated code: name the host site and the guest state.
jit::Engine* gEngine = nullptr;
moira::Moira* gNat = nullptr;
void onCrash(int sig, siginfo_t* info, void* uc) {
    std::fprintf(stderr, "\n[jit_program_fuzz] signal %d at %p\n", sig,
                 sig == SIGTERM ? nullptr : info->si_addr);
#if defined(__APPLE__) && defined(__aarch64__)
    const auto* ss = &static_cast<ucontext_t*>(uc)->uc_mcontext->__ss;
    std::fprintf(stderr, "  pc=%llx lr=%llx x14=%llx x15=%llx x16=%llx\n",
                 (unsigned long long)ss->__pc, (unsigned long long)ss->__lr,
                 (unsigned long long)ss->__x[14], (unsigned long long)ss->__x[15],
                 (unsigned long long)ss->__x[16]);
    const uint32_t* lr = reinterpret_cast<const uint32_t*>(ss->__lr);
    std::fprintf(stderr, "  code before lr:");
    for (int i = -48; i < 4; i++) std::fprintf(stderr, "%s%08x", (i + 48) % 8 ? " " : "\n    ", lr[i]);
    std::fprintf(stderr, "\n");
#else
    (void)uc;
#endif
    if (gNat) std::fprintf(stderr, "  guest pc=%08X clock=%lld\n", gNat->getPC(), (long long)gNat->getClock());
    if (gEngine) {
        unsigned count = 0, head = 0;
        const auto* ev = gEngine->dispatchRing(count, head);
        for (unsigned k = count < 12 ? count : 12; k > 0; k--) {
            const auto& e = ev[(head + jit::Engine::kDispatchRing - k) % jit::Engine::kDispatchRing];
            std::fprintf(stderr, "  ring pc=%08X clock=%lld kind=%u exit=%u instrs=%u\n",
                         e.pc, (long long)e.clock, e.kind, e.exit, e.instrs);
        }
    }
    std::_Exit(128 + sig);
}

class FuzzCpu : public moira::Moira {
public:
    FuzzCpu() : mem(1 << 24, 0) {}
    std::vector<uint8_t> mem;
    jit::CodeGuard* guard = nullptr;
    // --trace: report interpreter stores into the program itself.
    uint32_t watchLo = 1, watchHi = 0;
    uint64_t interrupts = 0;          // --irq: levels actually taken

    // --irq-timed: a one-line "device" that raises IPL `evLevel` at the
    // absolute guest clock `assertAt` and drops it at `dropAt`. Both are
    // reached through sync(), i.e. in the middle of instructions and of
    // generated blocks, which is where a real board's VIA or SCSI chip
    // raises its line. `deadline` is what the engine's generated code
    // compares the clock with (Engine::setPeriphDeadline), as the
    // Cpu030/Cpu040 wrappers publish theirs.
    moira::i64 assertAt = INT64_MAX, dropAt = INT64_MAX, deadline = INT64_MAX;
    moira::u8 evLevel = 0;
    void arm(moira::i64 at, moira::i64 drop, moira::u8 level) {
        assertAt = at; dropAt = drop; evLevel = level; deadline = at;
    }
    void serviceEvents() {
        if (clock >= assertAt) {
            setIPL(evLevel);
            assertAt = INT64_MAX;
            deadline = dropAt;
        }
        if (clock >= dropAt) {
            setIPL(0);
            dropAt = INT64_MAX;
            deadline = INT64_MAX;
        }
    }
    void sync(int cycles) override {
        clock += cycles;
        if (clock >= deadline) serviceEvents();
    }

    uint64_t vectors[256] = {};       // --stats: exceptions taken, by vector
    uint64_t fpuInstrs = 0;           // --stats: F-line opcodes executed

private:
public:
    const char* logTag = nullptr;     // FUZZ_LOG_SLICES: name printed with each exception
    // FUZZ_CACR_FLUSH=0 disables it: the board wrappers' contract
    // (Cpu040::didChangeCACR, CentrisCpu, Q630Cpu, Q700Cpu) drops every
    // translated block on a CACR write; the harness honours it unless told.
    void (*onCacr)(FuzzCpu&) = nullptr;
private:
    void didChangeCACR(moira::u32) override { if (onCacr) onCacr(*this); }
    void willInterrupt(moira::u8 level) override {
        interrupts++;
        if (logTag) std::printf("    %s irq level %u pc=%08X clock=%lld\n", logTag, level,
                                getPC0(), (long long)getClock());
    }
    void willExecute(moira::M68kException, moira::u16 vector) override {
        vectors[vector & 0xFF]++;
        if (logTag) std::printf("    %s exception vector %u pc=%08X clock=%lld sr=%04X\n", logTag,
                                vector, getPC0(), (long long)getClock(), getSR());
    }
    moira::u8 read8(moira::u32 a) const override { return mem[a & 0xFFFFFF]; }
    moira::u16 read16(moira::u32 a) const override {
        return moira::u16((mem[a & 0xFFFFFF] << 8) | mem[(a + 1) & 0xFFFFFF]);
    }
    void write8(moira::u32 a, moira::u8 v) const override {
        auto& self = *const_cast<FuzzCpu*>(this);
        if (self.guard) self.guard->note(a & 0xFFFFFF, 1);
        if ((a & 0xFFFFFF) >= watchLo && (a & 0xFFFFFF) < watchHi)
            std::printf("    %s store $%06X <- $%02X by PC=%08X clock=%lld\n",
                        self.logTag ? self.logTag : "", a & 0xFFFFFF, v,
                        self.getPC0(), (long long)self.getClock());
        self.mem[a & 0xFFFFFF] = v;
    }
    void write16(moira::u32 a, moira::u16 v) const override {
        write8(a, moira::u8(v >> 8));
        write8(a + 1, moira::u8(v));
    }
};

jit::MemoryHooks hooks(FuzzCpu* cpu) {
    jit::MemoryHooks h;
    h.self = cpu;
    // The bus mirrors its 16 MB every 16 MB (read8/write8 mask), but the
    // direct views do NOT: a mirrored view would let a store at $FF040044
    // reach the bytes of code at $040044 through a DTLB entry whose code
    // mask describes page $FF040, and the engine only learns of such an
    // alias through MemoryHooks::aliasCodeMask. Above 16 MB every access
    // therefore goes through the map, where the write guard sees it.
    h.codeSpan = [](void* s, uint32_t p, uint32_t& len) -> const uint8_t* {
        auto& c = *static_cast<FuzzCpu*>(s);
        if (p >= c.mem.size()) { len = 0; return nullptr; }
        len = uint32_t(c.mem.size()) - p;
        return c.mem.data() + p;
    };
    h.dataSpan = [](void* s, uint32_t p, uint32_t& len, int) -> uint8_t* {
        auto& c = *static_cast<FuzzCpu*>(s);
        if (p >= c.mem.size()) { len = 0; return nullptr; }
        len = uint32_t(c.mem.size()) - p;
        return c.mem.data() + p;
    };
    h.setGuard = [](void* s, jit::CodeGuard* g) { static_cast<FuzzCpu*>(s)->guard = g; };
    h.ramBytes = [](void* s) { return uint32_t(static_cast<FuzzCpu*>(s)->mem.size()); };
    return h;
}

// splitmix64: a stable stream per seed on every host and compiler.
struct Rng {
    uint64_t s;
    uint64_t next() {
        uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    uint32_t below(uint32_t n) { return uint32_t(next() % n); }
    uint16_t w() { return uint16_t(next()); }
    bool chance(uint32_t permille) { return below(1000) < permille; }
};

constexpr uint32_t kHandler = 0x00001000;  // JMP head.L
constexpr uint32_t kCounter = 0x00001100;  // lap counter (long), then IRQ count
constexpr uint32_t kIrqHandler = 0x00001200;
constexpr uint32_t kStack   = 0x00030000;
constexpr uint32_t kData    = 0x00010000;  // A0..A6 start in [kData, kData+64K)
constexpr uint32_t kCode    = 0x00040000;
constexpr uint32_t kBodyWord = 29;         // first body word: after the prologue
bool gFpu = false;   // --fpu: 68040 with its FPU, 68030 with a 68882
bool gExt = false;   // --ext: the wider template set (Gen::ext); changes every program
bool gDcache = false;   // --dcache: the architectural 68040 caches (POM68K_040_DCACHE), both on
bool gMmu = false;      // --mmu: identity translation through real tables (68030/68040)
int gModel = 40;

class Gen {
public:
    Gen(Rng& r, std::vector<uint16_t>& out) : r_(r), o_(out) {}

    // Dn, (An), (An)+, -(An), d16(An), d8(An,Xn) — An in A0..A6.
    // `alterable` excludes nothing extra here (all of these are), `withAn`
    // adds An direct, `withImm` adds #imm of `size` (0 b, 1 w, 2 l).
    uint16_t ea(bool memOnly, bool withAn, bool withImm, int size,
                std::vector<uint16_t>& ext) {
        for (;;) {
            const uint32_t k = r_.below(withImm ? 10 : 9);
            const uint16_t reg = uint16_t(r_.below(7));
            switch (k) {
            case 0: case 1:
                if (memOnly) continue;
                return uint16_t(r_.below(8));                 // Dn
            case 2:
                if (memOnly || !withAn) continue;
                return uint16_t(0x08 | reg);                  // An
            case 3: return uint16_t(0x10 | reg);              // (An)
            case 4: return uint16_t(0x18 | reg);              // (An)+
            case 5: return uint16_t(0x20 | reg);              // -(An)
            case 6:
                ext.push_back(uint16_t(int16_t(r_.below(0x400)) - 0x200));
                return uint16_t(0x28 | reg);                  // d16(An)
            case 7: case 8: {
                // Brief format: D/A, Xn, W/L, scale (020+), disp8.
                const uint16_t xn = uint16_t(r_.below(16));
                ext.push_back(uint16_t(xn << 12 | (r_.below(2) << 11) |
                                       (r_.below(4) << 9) | (r_.w() & 0xFF)));
                return uint16_t(0x30 | reg);
            }
            default:                                          // #imm
                if (size == 2) { ext.push_back(r_.w()); ext.push_back(r_.w()); }
                else ext.push_back(size == 0 ? uint16_t(r_.w() & 0xFF) : r_.w());
                return 0x3C;
            }
        }
    }

    // Control addressing only: (An), d16(An), d8(An,Xn).
    uint16_t control(std::vector<uint16_t>& ext) {
        for (;;) {
            const uint16_t m = ea(true, false, false, 2, ext);
            if ((m & 0x38) == 0x18 || (m & 0x38) == 0x20) { ext.clear(); continue; }
            return m;
        }
    }

    void emit(uint16_t op, const std::vector<uint16_t>& a = {},
              const std::vector<uint16_t>& b = {}) {
        o_.push_back(op);
        o_.insert(o_.end(), a.begin(), a.end());
        o_.insert(o_.end(), b.begin(), b.end());
    }

    void one(uint32_t rawPermille) {
        if (r_.chance(rawPermille)) {
            const uint32_t n = 1 + r_.below(3);
            for (uint32_t i = 0; i < n; i++) o_.push_back(r_.w());
            return;
        }
        if (gFpu && r_.chance(200)) { fpu(); return; }
        if (gExt && r_.chance(250)) { ext(); return; }
        std::vector<uint16_t> e1, e2;
        const uint16_t dn = uint16_t(r_.below(8));
        const uint16_t sz = uint16_t(r_.below(3));
        switch (r_.below(30)) {
        case 0: case 1: case 2: {   // MOVE / MOVEA
            static const uint16_t code[3] = {1, 3, 2};
            const uint16_t src = ea(false, sz != 0, true, sz, e1);
            uint16_t dst;
            if (sz != 0 && r_.chance(150)) dst = uint16_t(0x08 | r_.below(7));
            else dst = ea(false, false, false, sz, e2);
            const uint16_t dmode = (dst >> 3) & 7, dreg = dst & 7;
            emit(uint16_t(code[sz] << 12 | dreg << 9 | dmode << 6 | src), e1, e2);
            break;
        }
        case 3: case 4: case 5: {   // ADD/SUB/AND/OR/CMP <ea>,Dn | Dn,<ea>
            static const uint16_t base[5] = {0xD000, 0x9000, 0xC000, 0x8000, 0xB000};
            const uint32_t k = r_.below(5);
            const bool toEa = k < 4 && r_.chance(400);
            const bool an = (k < 2 || k == 4) && sz != 0;
            const uint16_t m = toEa ? ea(true, false, false, sz, e1)
                                    : ea(false, an, true, sz, e1);
            emit(uint16_t(base[k] | dn << 9 | (toEa ? 0x100 : 0) | sz << 6 | m), e1);
            break;
        }
        case 6: {                   // EOR Dn,<ea>
            emit(uint16_t(0xB100 | dn << 9 | sz << 6 | ea(false, false, false, sz, e1)), e1);
            break;
        }
        case 7: {                   // ADDA/SUBA/CMPA .W/.L
            static const uint16_t base[3] = {0xD0C0, 0x90C0, 0xB0C0};
            const uint16_t l = uint16_t(r_.below(2));
            const uint16_t m = ea(false, true, true, l ? 2 : 1, e1);
            emit(uint16_t(base[r_.below(3)] | r_.below(7) << 9 | l << 8 | m), e1);
            break;
        }
        case 8: case 9: {           // ORI/ANDI/SUBI/ADDI/EORI/CMPI
            static const uint16_t base[6] = {0x0000, 0x0200, 0x0400, 0x0600, 0x0A00, 0x0C00};
            if (sz == 2) { e1.push_back(r_.w()); e1.push_back(r_.w()); }
            else e1.push_back(sz == 0 ? uint16_t(r_.w() & 0xFF) : r_.w());
            const uint16_t m = ea(false, false, false, sz, e2);
            emit(uint16_t(base[r_.below(6)] | sz << 6 | m), e1, e2);
            break;
        }
        case 10: case 11: {         // ADDQ/SUBQ
            const bool an = sz != 0 && r_.chance(150);
            const uint16_t m = an ? uint16_t(0x08 | r_.below(7)) : ea(false, false, false, sz, e1);
            emit(uint16_t(0x5000 | r_.below(8) << 9 | r_.below(2) << 8 | sz << 6 | m), e1);
            break;
        }
        case 12: {                  // MOVEQ
            emit(uint16_t(0x7000 | dn << 9 | (r_.w() & 0xFF)));
            break;
        }
        case 13: case 14: {         // shifts/rotates, register and memory
            if (r_.chance(150)) {
                const uint16_t m = ea(true, false, false, 1, e1);
                emit(uint16_t(0xE0C0 | r_.below(4) << 9 | r_.below(2) << 8 | m), e1);
            } else {
                emit(uint16_t(0xE000 | r_.below(8) << 9 | r_.below(2) << 8 | sz << 6 |
                              r_.below(2) << 5 | r_.below(4) << 3 | r_.below(8)));
            }
            break;
        }
        case 15: {                  // CLR/NEG/NOT/NEGX/TST
            static const uint16_t base[5] = {0x4200, 0x4400, 0x4600, 0x4000, 0x4A00};
            const uint32_t k = r_.below(5);
            emit(uint16_t(base[k] | sz << 6 | ea(false, false, k == 4, sz, e1)), e1);
            break;
        }
        case 16: {                  // SWAP/EXT.W/EXT.L/EXTB.L
            static const uint16_t base[4] = {0x4840, 0x4880, 0x48C0, 0x49C0};
            emit(uint16_t(base[r_.below(4)] | dn));
            break;
        }
        case 17: {                  // LEA / PEA
            const uint16_t m = control(e1);
            if (r_.chance(800)) emit(uint16_t(0x41C0 | r_.below(7) << 9 | m), e1);
            else emit(uint16_t(0x4840 | m), e1);
            break;
        }
        case 18: {                  // MULU/MULS/DIVU/DIVS .W
            static const uint16_t base[4] = {0xC0C0, 0xC1C0, 0x80C0, 0x81C0};
            emit(uint16_t(base[r_.below(4)] | dn << 9 | ea(false, false, true, 1, e1)), e1);
            break;
        }
        case 19: {                  // MULx.L / DIVx.L (32 and 64 bit forms)
            const bool div = r_.chance(500);
            const uint16_t ext = uint16_t(r_.below(8) << 12 | r_.below(2) << 11 |
                                          r_.below(2) << 10 | r_.below(8));
            const uint16_t m = ea(false, false, true, 2, e2);
            emit(uint16_t((div ? 0x4C40 : 0x4C00) | m), {ext}, e2);
            break;
        }
        case 20: {                  // BTST/BCHG/BCLR/BSET, dynamic and static
            const uint16_t kind = uint16_t(r_.below(4) << 6);
            if (r_.chance(500)) {
                emit(uint16_t(0x0100 | dn << 9 | kind | ea(false, false, kind == 0, 0, e1)), e1);
            } else {
                e1.push_back(uint16_t(r_.below(64)));
                emit(uint16_t(0x0800 | kind | ea(false, false, false, 0, e2)), e1, e2);
            }
            break;
        }
        case 21: {                  // bit fields
            const uint16_t kind = uint16_t(r_.below(8));
            uint16_t m;
            if (r_.chance(500)) m = dn;
            else m = control(e2);
            const uint16_t ext = uint16_t(r_.below(8) << 12 | r_.below(2) << 11 |
                                          r_.below(32) << 6 | r_.below(2) << 5 | r_.below(32));
            emit(uint16_t(0xE8C0 | kind << 8 | m), {ext}, e2);
            break;
        }
        case 22: {                  // ADDX/SUBX/ABCD/SBCD, register and -(An)
            static const uint16_t base[4] = {0xD100, 0x9100, 0xC100, 0x8100};
            const uint32_t k = r_.below(4);
            const uint16_t s = k < 2 ? uint16_t(sz << 6) : 0;
            const uint16_t rm = r_.chance(300) ? 0x08 : 0;
            const uint16_t rx = rm ? uint16_t(r_.below(7)) : dn;
            const uint16_t ry = rm ? uint16_t(r_.below(7)) : uint16_t(r_.below(8));
            emit(uint16_t(base[k] | rx << 9 | s | rm | ry));
            break;
        }
        case 23: {                  // EXG / CMPM / CHK.W
            switch (r_.below(3)) {
            case 0: {
                static const uint16_t mode[3] = {0x40, 0x48, 0x88};
                emit(uint16_t(0xC100 | dn << 9 | mode[r_.below(3)] | r_.below(7)));
                break;
            }
            case 1:
                emit(uint16_t(0xB108 | r_.below(7) << 9 | sz << 6 | r_.below(7)));
                break;
            default:
                emit(uint16_t(0x4180 | dn << 9 | ea(false, false, true, 1, e1)), e1);
                break;
            }
            break;
        }
        case 24: {                  // MOVEM, both directions
            const uint16_t mask = uint16_t(r_.w() & 0x7FFF);  // never A7
            const uint16_t l = uint16_t(r_.below(2) << 6);
            if (r_.chance(500)) {
                uint16_t m = r_.chance(300) ? uint16_t(0x20 | r_.below(7)) : control(e2);
                emit(uint16_t(0x4880 | l | m), {mask}, e2);
            } else {
                uint16_t m = r_.chance(300) ? uint16_t(0x18 | r_.below(7)) : control(e2);
                emit(uint16_t(0x4C80 | l | m), {mask}, e2);
            }
            break;
        }
        case 25: {                  // Bcc.B / Bcc.W, short forward hops
            const uint16_t cc = uint16_t(r_.below(16));
            if (cc == 1) { emit(0x4E71); break; }             // no BSR: the stack is reset per lap
            if (r_.chance(500)) emit(uint16_t(0x6000 | cc << 8 | (2 + 2 * r_.below(8))));
            else emit(uint16_t(0x6000 | cc << 8), {uint16_t(4 + 2 * r_.below(8))});
            break;
        }
        case 26: {                  // DBcc, short backward or forward hop
            const int16_t disp = int16_t(r_.chance(300) ? -int(2 * r_.below(6)) : int(2 + 2 * r_.below(8)));
            emit(uint16_t(0x50C8 | r_.below(16) << 8 | dn), {uint16_t(disp)});
            break;
        }
        case 27: {                  // Scc / TAS
            if (r_.chance(800))
                emit(uint16_t(0x50C0 | r_.below(16) << 8 | ea(false, false, false, 0, e1)), e1);
            else
                emit(uint16_t(0x4AC0 | ea(false, false, false, 0, e1)), e1);
            break;
        }
        case 28: {                  // MOVE to/from CCR, MOVE from SR, ANDI/ORI/EORI to CCR
            switch (r_.below(4)) {
            case 0: emit(uint16_t(0x44C0 | ea(false, false, true, 1, e1)), e1); break;
            case 1: emit(uint16_t(0x42C0 | ea(false, false, false, 1, e1)), e1); break;
            case 2: emit(uint16_t(0x40C0 | ea(false, false, false, 1, e1)), e1); break;
            default: {
                static const uint16_t op[3] = {0x003C, 0x023C, 0x0A3C};
                emit(op[r_.below(3)], {uint16_t(r_.w() & 0xFF)});
                break;
            }
            }
            break;
        }
        default: {                  // LINK/UNLK pair or NOP
            if (r_.chance(500)) {
                const uint16_t an = uint16_t(r_.below(7));
                emit(uint16_t(0x4E50 | an), {uint16_t(-int(2 * r_.below(16)))});
                emit(uint16_t(0x4E58 | an));
            } else {
                emit(0x4E71);
            }
            break;
        }
        }
    }

    // FPU general instructions ($F200 + EA, command word). Arithmetic the
    // 68040 implements in silicon (MC68040UM § 9.5 lists the rest as
    // unimplemented and trapping — FSIN/FETOX... — drawn here too, rarely,
    // since the trap is itself a path to judge), register and memory
    // sources in every format but packed, FMOVE out to Dn/memory, FPCR
    // rounding and precision bits with every exception left disabled, and
    // FBcc/FScc on the result.
    void fpu() {
        static const uint8_t ops[] = {
            0x00, 0x01, 0x03, 0x04, 0x18, 0x1A, 0x20, 0x21, 0x22, 0x23, 0x24,
            0x25, 0x26, 0x27, 0x28, 0x38, 0x3A, 0x40, 0x41, 0x44, 0x45, 0x58,
            0x5A, 0x5C, 0x60, 0x62, 0x64, 0x66, 0x68, 0x6C,
            0x0E, 0x10, 0x14, 0x1E, 0x1D, 0x30};
        std::vector<uint16_t> e;
        const uint16_t src = uint16_t(r_.below(8)), dst = uint16_t(r_.below(8));
        const uint8_t op = ops[r_.below(sizeof ops)];
        switch (r_.below(8)) {
        case 0: case 1: case 2:      // FPm,FPn
            emit(0xF200, {uint16_t(src << 10 | dst << 7 | op)});
            break;
        case 3: case 4: {            // <ea>,FPn in L/S/X/W/D/B
            static const uint16_t fmt[6] = {0, 1, 2, 4, 5, 6};
            static const int bytes[6] = {4, 4, 12, 2, 8, 1};
            const int f = int(r_.below(6));
            uint16_t m;
            if (bytes[f] <= 4 && fmt[f] != 2 && r_.chance(400)) m = uint16_t(r_.below(8));   // Dn
            else if (r_.chance(300)) {                                  // #imm
                m = 0x3C;
                for (int k = 0; k < (bytes[f] + 1) / 2; k++) e.push_back(r_.w());
                if (bytes[f] == 1) e.back() &= 0xFF;
            } else m = control(e);
            emit(uint16_t(0xF200 | m), {uint16_t(0x4000 | fmt[f] << 10 | dst << 7 | op)}, e);
            break;
        }
        case 5: {                    // FMOVE FPn,<ea> (L/S/W/B to Dn, any to memory)
            static const uint16_t fmt[6] = {0, 1, 2, 4, 5, 6};
            const int f = int(r_.below(6));
            uint16_t m;
            if (fmt[f] != 2 && fmt[f] != 5 && r_.chance(500)) m = uint16_t(r_.below(8));
            else m = control(e);
            emit(uint16_t(0xF200 | m), {uint16_t(0x6000 | fmt[f] << 10 | src << 7)}, e);
            break;
        }
        case 6: {                    // FMOVE.L #mode,FPCR: rounding/precision only
            emit(0xF23C, {0x9000, 0, uint16_t(r_.below(16) << 4)});
            break;
        }
        default:                     // FBcc.W short hop / FScc Dn
            if (r_.chance(500))
                emit(uint16_t(0xF280 | r_.below(32)), {uint16_t(4 + 2 * r_.below(6))});
            else
                emit(uint16_t(0xF240 | r_.below(8)), {uint16_t(r_.below(32))});
            break;
        }
    }

    // --ext: calls and returns that really pair up, traps, CMP2/CHK2, CAS,
    // PACK/UNPK, MOVEP, NBCD, MOVE16 (68040) and cache enables through
    // MOVEC CACR — each a block terminator, a multi-access instruction or a
    // state the translator must flush on.
    void ext() {
        std::vector<uint16_t> e;
        const uint16_t dn = uint16_t(r_.below(8));
        switch (r_.below(11)) {
        case 0: {   // BSR.S sub / BRA.S over / sub: MOVEQ; RTS|RTD
            const bool rtd = r_.chance(300);
            const uint16_t ret = rtd ? 0x4E74 : 0x4E75;
            const uint16_t skip = rtd ? 6 : 4;
            emit(0x6102);                                   // BSR.S +2
            emit(uint16_t(0x6000 | skip));                  // BRA.S past sub
            emit(uint16_t(0x7000 | dn << 9 | (r_.w() & 0xFF)));
            if (rtd) emit(ret, {uint16_t(r_.below(3) * 2)});
            else emit(ret);
            break;
        }
        case 1: {   // JSR (d16,PC) sub / BRA.S over / sub: ADDQ; RTS
            emit(0x4EBA, {0x0004});                         // JSR (4,PC) -> +6
            emit(0x6004);                                   // BRA.S +4
            emit(uint16_t(0x5080 | r_.below(8) << 9 | dn)); // ADDQ.L #q,Dn
            emit(0x4E75);
            break;
        }
        case 2: {   // TRAPcc / TRAPcc.W / TRAPV / TRAP #n
            switch (r_.below(4)) {
            case 0: emit(uint16_t(0x50FC | r_.below(16) << 8)); break;
            case 1: emit(uint16_t(0x50FA | r_.below(16) << 8), {r_.w()}); break;
            case 2: emit(0x4E76); break;
            default: emit(uint16_t(0x4E40 | r_.below(16))); break;
            }
            break;
        }
        case 3: {   // CMP2 / CHK2 .B/.W/.L <control>,Rn
            const uint16_t m = control(e);
            const uint16_t ext16 = uint16_t(r_.below(16) << 12 | (r_.chance(500) ? 0x800 : 0));
            emit(uint16_t(0x00C0 | r_.below(3) << 9 | m), {ext16}, e);
            break;
        }
        case 4: {   // CAS .B/.W/.L Dc,Du,<memory>
            const uint16_t m = ea(true, false, false, 1, e);
            emit(uint16_t(0x0AC0 | (1 + r_.below(3)) << 9 | m),
                 {uint16_t(r_.below(8) << 6 | r_.below(8))}, e);
            break;
        }
        case 5: {   // PACK / UNPK, register and -(An) forms
            const uint16_t rm = r_.chance(400) ? 0x08 : 0;
            const uint16_t x = rm ? uint16_t(r_.below(7)) : dn;
            const uint16_t y = rm ? uint16_t(r_.below(7)) : uint16_t(r_.below(8));
            emit(uint16_t((r_.chance(500) ? 0x8140 : 0x8180) | y << 9 | rm | x), {r_.w()});
            break;
        }
        case 6: {   // MOVEP .W/.L both directions
            emit(uint16_t(0x0108 | dn << 9 | (4 + r_.below(4)) << 6 | r_.below(7)),
                 {uint16_t(int16_t(r_.below(0x200)) - 0x100)});
            break;
        }
        case 7:     // NBCD <ea>
            emit(uint16_t(0x4800 | ea(false, false, false, 0, e)), e);
            break;
        case 8:     // MOVE16 (Ax)+,(Ay)+ on the 68040; PFLUSHA under --mmu; NOP
            if (gMmu && r_.chance(400)) {
                if (gModel == 30) emit(0xF000, {0x2400});   // PFLUSHA
                else emit(0xF518);                          // PFLUSHA (68040)
            } else if (gModel == 40) emit(uint16_t(0xF620 | r_.below(7)), {uint16_t(0x8000 | r_.below(7) << 12)});
            else emit(0x4E71);
            break;
        case 9: {   // MOVE.L #cacr,D0 ; MOVEC D0,CACR — caches on, off, cleared
            uint32_t v;
            if (gModel == 30) {
                static const uint32_t pick[] = {0, 0x1, 0x9, 0x11, 0x101, 0x0809, 0x3111};
                v = pick[r_.below(sizeof pick / sizeof pick[0])];
            } else {
                static const uint32_t pick[] = {0, 0x8000, 0x80008000u, 0x80000000u};
                // The D-cache only with the architectural model on (--dcache).
                v = gDcache ? pick[r_.below(4)] : pick[r_.below(2)];
            }
            emit(0x203C, {uint16_t(v >> 16), uint16_t(v)});
            emit(0x4E7B, {0x0002});
            break;
        }
        default: {  // LINK.L / UNLK
            const uint16_t an = uint16_t(r_.below(7));
            emit(uint16_t(0x4808 | an), {0xFFFF, uint16_t(-int(4 * r_.below(8)))});
            emit(uint16_t(0x4E58 | an));
            break;
        }
        }
    }

private:
    Rng& r_;
    std::vector<uint16_t>& o_;
};

struct Program {
    std::vector<uint16_t> code;      // at kCode
    uint32_t laps = 0;
    uint32_t bodyEnd = 0;            // word index past the random body
    std::vector<uint32_t> regs;      // D0..D7
};

// --patch ADDR=WORD (repeatable): overwrite a program word after building,
// to replay a self-modified program as plain code.
std::vector<std::pair<uint32_t, uint16_t>> gPatches;
bool gSmc = false;        // --smc: A0..A3 point into the program
bool gIrq = false;        // --irq: IPL changes at slice boundaries
bool gIrqTimed = false;   // --irq-timed: IPL edges at clock deadlines (implies --irq vectors/SR)
bool gSave = false;       // --savestate: continue the translated side on a reloaded core

Program build(uint64_t seed, uint32_t len, uint32_t rawPermille) {
    Rng r{seed * 0x2545F4914F6CDD1Dull + 1};
    Program p;
    p.laps = 2 + r.below(40);
    for (int i = 0; i < 8; i++) {
        switch (r.below(4)) {
        case 0: p.regs.push_back(r.below(16)); break;
        case 1: p.regs.push_back(uint32_t(r.next())); break;
        case 2: p.regs.push_back(0x80000000u >> r.below(32)); break;
        default: p.regs.push_back(uint32_t(int32_t(int16_t(r.w())))); break;
        }
    }
    auto& c = p.code;
    auto l = [&](uint32_t v) { c.push_back(uint16_t(v >> 16)); c.push_back(uint16_t(v)); };
    c.push_back(0x53B9); l(kCounter);              // SUBQ.L #1,counter.L
    c.push_back(0x6700); const size_t beq = c.size(); c.push_back(0);  // BEQ.W done
    for (int a = 0; a < 7; a++) {                  // MOVEA.L #ptr,An
        c.push_back(uint16_t(0x207C | a << 9));
        // --smc: pointers into the program itself, so stores rewrite
        // translated code all the time (the write guard's whole job).
        if (gSmc && a < 4) l(kCode + 0x40 + (r.below(0x100) & ~1u));
        else l(kData + (r.below(0x8000) & ~1u) + 0x2000);
    }
    c.push_back(0x2E7C); l(kStack);                // MOVEA.L #stack,A7
    Gen g(r, c);
    for (uint32_t i = 0; i < len; i++) g.one(rawPermille);
    p.bodyEnd = uint32_t(c.size());
    // Pad: a forward branch or a raw word may run past the body end.
    for (int i = 0; i < 8; i++) c.push_back(0x4E71);
    c.push_back(0x4EF9); l(kCode);                 // JMP head.L
    const size_t done = c.size();
    c.push_back(0x60FE);                           // done: BRA.S done
    c[beq] = uint16_t((done - beq) * 2);
    for (const auto& [addr, word] : gPatches)
        if (addr >= kCode && (addr - kCode) / 2 < c.size()) c[(addr - kCode) / 2] = word;
    return p;
}

moira::FPUModel fpuModelFor(moira::Model model) {
    return !gFpu || model == moira::Model::M68000 ? moira::FPUModel::NONE
         : model == moira::Model::M68030 || model == moira::Model::M68020
             ? moira::FPUModel::M68882
                                         : moira::FPUModel::M68040;
}

void load(FuzzCpu& cpu, moira::Model model, const Program& p, uint32_t ramSeed) {
    std::fill(cpu.mem.begin(), cpu.mem.end(), 0);
    auto put16 = [&](uint32_t a, uint16_t v) { cpu.mem[a] = uint8_t(v >> 8); cpu.mem[a + 1] = uint8_t(v); };
    auto put32 = [&](uint32_t a, uint32_t v) { put16(a, uint16_t(v >> 16)); put16(a + 2, uint16_t(v)); };
    for (uint32_t v = 0; v < 256; v++) put32(v * 4, kHandler);
    put32(0, kStack);
    put32(4, kCode);
    put16(kHandler, 0x4EF9); put32(kHandler + 2, kCode);   // JMP head.L
    // Interrupts (vectors 24..31): count, then RTE to the interrupted code.
    put16(kIrqHandler, 0x52B9); put32(kIrqHandler + 2, kCounter + 4);  // ADDQ.L #1,abs.L
    put16(kIrqHandler + 6, 0x4E73);                                   // RTE
    for (uint32_t v = 24; v < 32; v++) put32(v * 4, kIrqHandler);
    put32(kCounter, p.laps);
    Rng r{uint64_t(ramSeed) * 0x9E3779B97F4A7C15ull + 7};
    for (uint32_t a = kData; a < kData + 0x20000; a += 8) {
        const uint64_t x = r.next();
        std::memcpy(&cpu.mem[a], &x, 8);
    }
    for (size_t i = 0; i < p.code.size(); i++) put16(uint32_t(kCode + 2 * i), p.code[i]);

    cpu.setModel(model);
    cpu.setFPUModel(fpuModelFor(model));
    cpu.reset();
    cpu.setIPL(0);
    cpu.arm(INT64_MAX, INT64_MAX, 0);
    cpu.setSR(gIrq ? 0x2000 : 0x2700);
    cpu.setISP(kStack);
    cpu.setMSP(kStack - 0x1000);
    cpu.setUSP(kStack - 0x2000);
    cpu.setSP(kStack);
    for (int i = 0; i < 8; i++) cpu.setD(i, p.regs[i]);
    if (gFpu) {
        // Finite, zero, denormal-ish, infinity and NaN patterns from the seed.
        Rng fr{uint64_t(ramSeed) * 0xD6E8FEB86659FD93ull + 11};
        for (int i = 0; i < 8; i++) {
            uint32_t w[3];
            const uint32_t kind = fr.below(8);
            const uint32_t exp = kind == 0 ? 0 : kind == 1 ? 0x7FFF
                               : 0x3FFF + int32_t(fr.below(64)) - 32;
            w[0] = (fr.below(2) << 15 | exp) << 16;
            w[1] = kind == 0 && fr.chance(500) ? 0 : uint32_t(fr.next()) | (kind >= 2 ? 0x80000000u : 0);
            w[2] = uint32_t(fr.next());
            cpu.setFP(i, w);
        }
        cpu.setFPCR(0);
        cpu.setFPSR(0);
        cpu.setFPIAR(0);
    }
    for (int i = 0; i < 7; i++) cpu.setA(i, kData);
    cpu.setVBR(0);
    cpu.setPomCache040(gDcache);
    // The previous program's lines would otherwise survive the RAM this
    // function just rewrote behind the caches' back (seed-order dependent
    // results until 2026-11-18): start every program from empty caches,
    // and retire anything the translator published from them.
    cpu.pomCache040Data().invalidateAll();
    cpu.pomCache040Inst().invalidateAll();
    cpu.pomCache040Data().rot = 0;   // one divergence must not leak into later seeds
    cpu.pomCache040Inst().rot = 0;
    cpu.pomJitCache040Bump();
    cpu.setCACR(gDcache && model >= moira::Model::M68EC040 ? 0x80008000 : 0);
    if (gMmu) {
        // Identity map of the 16 MB bus through real descriptors, so every
        // access goes through the ATC, table walks and U/M write-backs.
        // Above 16 MB nothing is mapped: those accesses now take a page
        // fault (vector 2) where the plain bus mirrored them.
        constexpr uint32_t kTables = 0x00800000;
        if (model == moira::Model::M68030) {
            // TC: E, PS 4 KB, IS 8, TIA 4, TIB 8 (8+4+8+12 = 32). Level A:
            // sixteen short table descriptors; level B: 256 short page
            // descriptors of 4 KB each. Seed parity picks one 1 MB
            // early-termination descriptor for the first megabyte instead,
            // so both walk shapes are judged.
            for (uint32_t i = 0; i < 16; i++) {
                const uint32_t tb = kTables + 0x1000 + 0x400 * i;
                put32(kTables + 4 * i, tb | 2);
                for (uint32_t j = 0; j < 256; j++) put32(tb + 4 * j, (i << 20 | j << 12) | 1);
            }
            if (ramSeed & 1) put32(kTables, 1);
            cpu.setCRP(0x7FFF000200000000ull | kTables);
            cpu.setSRP(0);
            cpu.setTT0(0);
            cpu.setTT1(0);
            cpu.setTC(0x80000000u | 0xCu << 20 | 8u << 16 | 4u << 12 | 8u << 8);
        } else {
            // 68040, 4 KB pages: root (512 B) -> one pointer table (512 B)
            // -> 64 page tables (256 B each) of 64 resident pages.
            const uint32_t root = kTables, ptr = kTables + 0x200, pages = kTables + 0x1000;
            const uint32_t cm = gDcache ? 0x20 : 0;   // copyback when the caches are modelled
            put32(root, ptr | 2);
            for (uint32_t j = 0; j < 64; j++) {
                put32(ptr + 4 * j, (pages + 0x100 * j) | 2);
                for (uint32_t k = 0; k < 64; k++)
                    put32(pages + 0x100 * j + 4 * k, ((j << 18) | (k << 12)) | cm | 1);
            }
            cpu.setITT0(0); cpu.setITT1(0); cpu.setDTT0(0); cpu.setDTT1(0);
            cpu.setURP040(root);
            cpu.setSRP040(root);
            cpu.setTC040(0x8000);
        }
    }
    cpu.setPC(kCode);
    cpu.setPC0(kCode);
    cpu.setIRD(p.code[0]);
    cpu.setIRC(p.code[1]);
}

bool same(FuzzCpu& a, FuzzCpu& b, std::string& why) {
    char buf[160];
    auto diff = [&](const char* what, unsigned long long x, unsigned long long y) {
        std::snprintf(buf, sizeof buf, "%s interp $%llX jit $%llX", what, x, y);
        why = buf;
        return false;
    };
    if (a.getPC0() != b.getPC0()) return diff("PC", a.getPC0(), b.getPC0());
    if (a.getClock() != b.getClock()) return diff("clock", a.getClock(), b.getClock());
    for (int i = 0; i < 8; i++)
        if (a.getD(i) != b.getD(i)) { char w[3] = {'D', char('0' + i), 0}; return diff(w, a.getD(i), b.getD(i)); }
    for (int i = 0; i < 7; i++)
        if (a.getA(i) != b.getA(i)) { char w[3] = {'A', char('0' + i), 0}; return diff(w, a.getA(i), b.getA(i)); }
    if (a.getSP() != b.getSP()) return diff("A7", a.getSP(), b.getSP());
    if (a.getUSP() != b.getUSP()) return diff("USP", a.getUSP(), b.getUSP());
    if (a.getISP() != b.getISP()) return diff("ISP", a.getISP(), b.getISP());
    if (a.getMSP() != b.getMSP()) return diff("MSP", a.getMSP(), b.getMSP());
    if (a.getSR() != b.getSR()) return diff("SR", a.getSR(), b.getSR());
    if (a.getIRD() != b.getIRD()) return diff("IRD", a.getIRD(), b.getIRD());
    if (a.getIRC() != b.getIRC()) return diff("IRC", a.getIRC(), b.getIRC());
    if (a.isHalted() != b.isHalted()) return diff("halted", a.isHalted(), b.isHalted());
    if (a.getVBR() != b.getVBR()) return diff("VBR", a.getVBR(), b.getVBR());
    if (a.getCACR() != b.getCACR()) return diff("CACR", a.getCACR(), b.getCACR());
    if (a.getTC() != b.getTC()) return diff("TC", a.getTC(), b.getTC());
    if (a.getTC040() != b.getTC040()) return diff("TC040", a.getTC040(), b.getTC040());
    if (gDcache) {
        // Copyback lines can hold data RAM has never seen: compare both
        // caches line by line (tags, validity, dirty bits, bytes) and the
        // replacement counter, which decides every later victim.
        for (int c = 0; c < 2; c++) {
            const moira::Cache040& x = c ? a.pomCache040Inst() : a.pomCache040Data();
            const moira::Cache040& y = c ? b.pomCache040Inst() : b.pomCache040Data();
            if (x.rot != y.rot) return diff(c ? "I-cache rot" : "D-cache rot", x.rot, y.rot);
            for (int set = 0; set < moira::Cache040::SETS; set++)
                for (int way = 0; way < moira::Cache040::WAYS; way++) {
                    const auto& l = x.sets[set][way];
                    const auto& r = y.sets[set][way];
                    if (l.valid != r.valid || (l.valid && (l.tag != r.tag || l.dirty != r.dirty ||
                                                           std::memcmp(l.data, r.data, 16)))) {
                        char w[40];
                        std::snprintf(w, sizeof w, "%s set %d way %d tag",
                                      c ? "I-cache" : "D-cache", set, way);
                        return diff(w, l.valid ? l.tag : ~0u, r.valid ? r.tag : ~0u);
                    }
                }
        }
    }
    if (gFpu) {
        for (int i = 0; i < 8; i++) {
            uint32_t x[3], y[3];
            a.getFP(i, x); b.getFP(i, y);
            for (int j = 0; j < 3; j++)
                if (x[j] != y[j]) {
                    char w[12];
                    std::snprintf(w, sizeof w, "FP%d.%d", i, j);
                    return diff(w, x[j], y[j]);
                }
        }
        if (a.getFPCR() != b.getFPCR()) return diff("FPCR", a.getFPCR(), b.getFPCR());
        if (a.getFPSR() != b.getFPSR()) return diff("FPSR", a.getFPSR(), b.getFPSR());
        if (a.getFPIAR() != b.getFPIAR()) return diff("FPIAR", a.getFPIAR(), b.getFPIAR());
    }
    if (std::memcmp(a.mem.data(), b.mem.data(), a.mem.size()) != 0) {
        size_t i = 0;
        while (a.mem[i] == b.mem[i]) i++;
        return diff("RAM byte", i, (unsigned long long)a.mem[i] << 8 | b.mem[i]);
    }
    return true;
}

// Engine counters summed across the engines a --savestate run replaces.
struct Totals {
    unsigned long long blocksCompiled = 0, blocksRun = 0, instrs = 0,
                       slowInstrs = 0, windowInstrs = 0, traceInstrs = 0;
    void add(const jit::Engine& e) {
        const auto s = e.stats().snapshot();
        blocksCompiled += s.blocksCompiled; blocksRun += s.blocksRun;
        instrs += s.instrs; slowInstrs += s.slowInstrs;
        windowInstrs += s.windowInstrs; traceInstrs += s.traceInstrs;
    }
};

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);   // a crash keeps every line before it
    int model = 40;
    uint64_t seeds = 200, first = 1;
    uint32_t len = 48, raw = 30, slices = 4;
    std::vector<uint64_t> extra;
    long long budget = 200000;
    bool verbose = false, traceAll = false, shrink = false, bisect = false, watchJit = false, progress = false, stats = false;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        auto num = [&]() { return i + 1 < argc ? std::strtoull(argv[++i], nullptr, 0) : 0ull; };
        if (a == "--model") model = int(num());
        else if (a == "--seeds") seeds = num();
        else if (a == "--first") first = num();
        else if (a == "--len") len = uint32_t(num());
        else if (a == "--raw") raw = uint32_t(num());
        else if (a == "--slices") slices = uint32_t(num());
        else if (a == "--budget") budget = (long long)num();
        else if (a == "-v") verbose = true;
        else if (a == "--shrink") shrink = true;
        else if (a == "--bisect") bisect = true;
        else if (a == "--watch-jit") watchJit = true;
        else if (a == "--progress") progress = true;
        else if (a == "--smc") gSmc = true;
        else if (a == "--irq") gIrq = true;
        else if (a == "--irq-timed") gIrq = gIrqTimed = true;
        else if (a == "--fpu") gFpu = true;
        else if (a == "--ext") gExt = true;
        else if (a == "--savestate") gSave = true;
        else if (a == "--dcache") gDcache = true;
        else if (a == "--mmu") gMmu = true;
        else if (a == "--stats") stats = true;
        else if (a == "--extra" && i + 1 < argc) {
            // Comma-separated seeds run after the range: the regressions.
            for (char* q = argv[++i]; *q;) {
                char* end = nullptr;
                extra.push_back(std::strtoull(q, &end, 0));
                q = *end ? end + 1 : end;
            }
        }
        else if (a == "--patch" && i + 1 < argc) {
            char* end = nullptr;
            const uint32_t addr = uint32_t(std::strtoul(argv[++i], &end, 16));
            gPatches.push_back({addr, uint16_t(std::strtoul(end + 1, nullptr, 16))});
        }
        else if (a == "--trace") verbose = traceAll = true;
        else { std::fprintf(stderr, "unknown argument %s\n", a.c_str()); return 2; }
    }
    gModel = model;
    if ((model != 0 && model != 20 && model != 30 && model != 40) || slices == 0) {
        std::fprintf(stderr, "bad --model/--slices\n");
        return 2;
    }
    const moira::Model m = model == 0 ? moira::Model::M68000
                         : model == 20 ? moira::Model::M68020
                         : model == 30 ? moira::Model::M68030
                         : gFpu ? moira::Model::M68040 : moira::Model::M68LC040;
    const uint32_t family = model == 0 ? jit::kGuest68000
                          : model == 20 ? jit::kGuest68020
                          : model == 30 ? jit::kGuest68030 : jit::kGuest68040;

    auto ref = std::make_unique<FuzzCpu>();
    auto nat = std::make_unique<FuzzCpu>();
    ref->setModel(m);
    nat->setModel(m);
    const jit::ResolvedConfig config = testjit::resolveFromEnvironment();
    auto makeEngine = [&](FuzzCpu& cpu) {
        auto e = std::make_unique<jit::Engine>(cpu, hooks(&cpu), family, config);
        e->setEnabled(true);
        if (gIrqTimed)
            e->setPeriphDeadline(&cpu.deadline, [](moira::Moira* c) {
                static_cast<FuzzCpu*>(c)->serviceEvents();
            });
        return e;
    };
    auto engine = makeEngine(*nat);
    if (!std::getenv("FUZZ_CACR_FLUSH") || std::strcmp(std::getenv("FUZZ_CACR_FLUSH"), "0") != 0)
        nat->onCacr = [](FuzzCpu&) { if (gEngine) gEngine->flushAll(); };
    if (std::getenv("FUZZ_LOG_SLICES")) { ref->logTag = "interp"; nat->logTag = "jit   "; }
    gEngine = engine.get();
    gNat = nat.get();
    {
        struct sigaction sa {};
        sa.sa_sigaction = onCrash;
        sa.sa_flags = SA_SIGINFO;
        sigaction(SIGSEGV, &sa, nullptr);
        sigaction(SIGBUS, &sa, nullptr);
        sigaction(SIGTERM, &sa, nullptr);   // `timeout` on a wedged run: print where
    }
    std::printf("[jit_program_fuzz] model=680%02d backend=%s seeds=%llu..%llu len=%u raw=%u%% slices=%u budget=%lld\n",
                model, engine->backendName(),
                (unsigned long long)first, (unsigned long long)(first + seeds - 1),
                len, raw / 10, slices, budget);

    // One judged run of `p`: both cores from the same state to the same
    // random slice targets. Returns false with `why` on the first slice
    // whose states differ.
    moira::i64 c0 = 0, target = 0;
    uint64_t saves = 0;          // --savestate: round trips taken
    Totals retired;              // counters of engines replaced by --savestate
    long long fixedTarget = 0;   // --bisect: one slice to exactly c0 + this
    auto attempt = [&](const Program& p, uint64_t s, std::string& why) {
        load(*ref, m, p, uint32_t(s));
        load(*nat, m, p, uint32_t(s));
        engine->flushAll();               // new bytes under any old block
        if (watchJit) {
            nat->watchLo = kCode;
            nat->watchHi = kCode + 2 * uint32_t(p.code.size());
        }
        if (const char* w = std::getenv("FUZZ_WATCH")) {   // "lo,hi" hex, both cores
            char* end = nullptr;
            const uint32_t lo = uint32_t(std::strtoul(w, &end, 16));
            const uint32_t hi = uint32_t(std::strtoul(end + 1, nullptr, 16));
            ref->watchLo = nat->watchLo = lo;
            ref->watchHi = nat->watchHi = hi;
        }
        c0 = ref->getClock();
        nat->setClock(c0);
        Rng r{s ^ 0xC0FFEEull};
        bool ok = true;
        target = c0;
        // --irq: the IPL pins change only at slice boundaries, identically
        // on both cores; an asserted level is held for a short slice and
        // then dropped, so the autovector handler (count, RTE) runs a few
        // times and the program goes on.
        Rng irqR{s * 0x5851F42D4C957F2Dull + 3};
        bool asserted = false;
        for (uint32_t k = 0; k < slices && ok; k++) {
            target += asserted ? 20 + (long long)irqR.below(400)
                               : 1 + (long long)r.below(uint32_t(2 * budget / slices));
            if (fixedTarget > 0) { if (k) break; target = c0 + fixedTarget; }
            if (gIrqTimed && ref->assertAt == INT64_MAX && ref->dropAt == INT64_MAX &&
                irqR.chance(700)) {
                const moira::i64 now = ref->getClock();
                const moira::i64 at = now + 1 + irqR.below(3000);
                const moira::i64 drop = at + 1 + irqR.below(600);
                const auto level = moira::u8(1 + irqR.below(7));
                ref->arm(at, drop, level);
                nat->arm(at, drop, level);
            }
            try {
                ref->executeUntil(target);
                engine->executeUntil(target);
            } catch (const std::exception& e) {
                why = std::string("threw: ") + e.what();
                return false;
            }
            ok = same(*ref, *nat, why);
            if (ok && gSave && irqR.chance(400)) {
                // --savestate: the translated side continues on a FRESH core
                // and engine loaded from its own save state (the visitor the
                // product uses, Moira::pomVisitState). The host keeps RAM,
                // the models and the code guard, as the save-state contract
                // says; anything the visitor forgets diverges from here on.
                std::vector<uint8_t> blob;
                sav::Writer w(blob);
                nat->pomVisitState(w);
                auto fresh = std::make_unique<FuzzCpu>();
                fresh->mem = nat->mem;
                fresh->setModel(m);
                fresh->setFPUModel(fpuModelFor(m));
                fresh->setPomCache040(gDcache);
                sav::Reader rd(blob.data(), blob.size());
                fresh->pomVisitState(rd);
                if (!rd.ok()) { why = "save state did not read back"; return false; }
                fresh->assertAt = nat->assertAt;
                fresh->dropAt = nat->dropAt;
                fresh->deadline = nat->deadline;
                fresh->evLevel = nat->evLevel;
                fresh->interrupts = nat->interrupts;
                fresh->watchLo = nat->watchLo;
                fresh->watchHi = nat->watchHi;
                fresh->logTag = nat->logTag;
                fresh->onCacr = nat->onCacr;
                retired.add(*engine);
                auto freshEngine = makeEngine(*fresh);
                engine = std::move(freshEngine);   // the old engine still sees the old core
                nat = std::move(fresh);
                gEngine = engine.get();
                gNat = nat.get();
                saves++;
                if (std::getenv("FUZZ_LOG_SLICES"))
                    std::printf("    [slice %u] save-state round trip at +%lld pc=%08X\n", k,
                                (long long)(nat->getClock() - c0), nat->getPC0());
            }
            if (std::getenv("FUZZ_LOG_SLICES"))
                std::printf("    [slice %u] end +%lld pc=%08X A0=%08X A3=%08X ipl-at=%lld drop=%lld\n", k,
                            (long long)(target - c0), ref->getPC0(), ref->getA(0), ref->getA(3),
                            ref->assertAt == INT64_MAX ? -1LL : (long long)(ref->assertAt - c0),
                            ref->dropAt == INT64_MAX ? -1LL : (long long)(ref->dropAt - c0));
            if (ok && gIrq && !gIrqTimed) {
                uint8_t level = 0;
                if (!asserted && irqR.chance(500)) level = uint8_t(1 + irqR.below(7));
                asserted = level != 0;
                ref->setIPL(level);
                nat->setIPL(level);
            }
            if (!ok) {
                char at[64];
                std::snprintf(at, sizeof at, " (slice %u, target +%lld)", k, (long long)(target - c0));
                why += at;
            }
        }
        return ok;
    };

    uint64_t failures = 0;
    std::vector<uint8_t> refMemAtFail, jitMemAtFail;
    std::vector<uint64_t> order;
    for (uint64_t s = first; s < first + seeds; s++) order.push_back(s);
    order.insert(order.end(), extra.begin(), extra.end());
    for (const uint64_t s : order) {
        Program p = build(s, len, raw);
        if (progress) std::fprintf(stderr, "seed %llu\n", (unsigned long long)s);
        std::string why;
        bool ok = attempt(p, s, why);
        if (!ok && shrink) {
            // Replace whole body instructions by NOPs, first to last, as
            // long as SOME divergence remains; repeat until a pass changes
            // nothing. Boundaries come from the disassembler on the
            // original bytes.
            load(*ref, m, p, uint32_t(s));
            std::vector<std::pair<uint32_t, uint32_t>> insns;   // word index, words
            char line[128];
            for (uint32_t w = kBodyWord; w < p.bodyEnd;) {
                const int n = ref->disassemble(line, kCode + 2 * w);
                const uint32_t words = uint32_t(n > 0 ? n : 2) / 2;
                insns.push_back({w, words});
                w += words;
            }
            for (bool progress = true; progress;) {
                progress = false;
                for (const auto& [w, words] : insns) {
                    bool allNop = true;
                    for (uint32_t k = 0; k < words && w + k < p.code.size(); k++)
                        allNop &= p.code[w + k] == 0x4E71;
                    if (allNop) continue;
                    Program q = p;
                    for (uint32_t k = 0; k < words && w + k < q.code.size(); k++) q.code[w + k] = 0x4E71;
                    std::string why2;
                    if (!attempt(q, s, why2)) { p = q; why = why2; progress = true; }
                }
            }
            attempt(p, s, why);
            std::printf("  shrunk seed=%llu to %zu live instruction(s)\n", (unsigned long long)s,
                        size_t(std::count_if(insns.begin(), insns.end(), [&](auto in) {
                            return p.code[in.first] != 0x4E71; })));
        }
        if (!ok && bisect) {
            // Smallest single-slice target that still diverges: the JIT
            // then stops right after the first wrong boundary.
            long long lo = 0, hi = (long long)(target - c0);
            while (lo + 1 < hi) {
                const long long mid = (lo + hi) / 2;
                fixedTarget = mid;
                std::string w;
                if (attempt(p, s, w)) lo = mid; else hi = mid;
            }
            fixedTarget = hi;
            ok = attempt(p, s, why);
            std::printf("  bisect seed=%llu: first diverging single-slice target +%lld\n",
                        (unsigned long long)s, hi);
            fixedTarget = 0;
            if (ok) { std::printf("  (bisect lost the failure)\n"); ok = false; }
            else { fixedTarget = hi; }
        }
        if (!ok) {
            refMemAtFail = ref->mem;
            jitMemAtFail = nat->mem;
            failures++;
            std::printf("  FAIL seed=%llu laps=%u words=%zu: %s\n", (unsigned long long)s,
                        p.laps, p.code.size(), why.c_str());
            if (verbose) {
                // Listing, then the interpreter's own trace up to the
                // failing slice (bounded): the first instruction whose
                // effect differs is usually visible against the JIT's state.
                load(*ref, m, p, uint32_t(s));
                char line[128];
                for (uint32_t a = kCode; a < kCode + 2 * p.code.size();) {
                    const int n = ref->disassemble(line, a);
                    std::printf("    %06X  %s\n", a, line);
                    a += uint32_t(n > 0 ? n : 2);
                }
                ref->setClock(c0);
                ref->watchLo = kCode;
                ref->watchHi = kCode + 2 * uint32_t(p.code.size());
                unsigned shown = 0;
                while (ref->getClock() < target && shown < 4000) {
                    const uint32_t pc = ref->getPC0();
                    ref->disassemble(line, pc);
                    const moira::i64 before = ref->getClock();
                    ref->execute();
                    if (shown++ < 4000 && traceAll)
                        std::printf("    t %06X +%-6lld %-40s D0=%08X D1=%08X D3=%08X SR=%04X\n", pc,
                                    (long long)(before - c0), line, ref->getD(0), ref->getD(1), ref->getD(3),
                                    ref->getSR());
                }
                ref->watchHi = 0;
                unsigned count = 0, head = 0;
                const auto* ev = engine->dispatchRing(count, head);
                const unsigned n = count < 24 ? count : 24;
                if (!n) std::printf("    (no dispatch ring: set POM68K_JIT_DISPATCH_RING=1)\n");
                for (unsigned k = n; k > 0; k--) {
                    const auto& e = ev[(head + jit::Engine::kDispatchRing - k) % jit::Engine::kDispatchRing];
                    std::printf("    ring pc=%08X clock=+%lld target=+%lld kind=%u exit=%u instrs=%u\n",
                                e.pc, (long long)(e.clock - c0), (long long)(e.target - c0),
                                e.kind, e.exit, e.instrs);
                }
                {
                    unsigned shownDiff = 0;
                    for (size_t i = 0; i < nat->mem.size() && shownDiff < 16; i++)
                        if (jitMemAtFail[i] != refMemAtFail[i]) {
                            std::printf("    ram $%06zX interp $%02X jit $%02X\n", i, refMemAtFail[i], jitMemAtFail[i]);
                            shownDiff++;
                        }
                }
                std::printf("    jit final: PC=%08X clock=+%lld SR=%04X\n", nat->getPC0(),
                            (long long)(nat->getClock() - c0), nat->getSR());
            }
            if (failures >= 20) { std::printf("  (stopping after 20 failures)\n"); break; }
        } else if (verbose) {
            std::printf("  ok seed=%llu pc=%08X clock=+%lld\n", (unsigned long long)s,
                        ref->getPC0(), (long long)(ref->getClock() - c0));
            if (traceAll) {
                // The first instructions of a passing seed, for runs that
                // look wrong without diverging (cycle cost, exception storms).
                load(*ref, m, p, uint32_t(s));
                ref->setClock(c0);
                char line[128];
                // TRACE_FROM (hex pc): start printing at the first visit.
                const char* from = std::getenv("FUZZ_TRACE_FROM");
                const uint32_t fromPc = from ? uint32_t(std::strtoul(from, nullptr, 16)) : 0;
                bool on = !from;
                int printed = 0;
                for (int i = 0; i < 2000000 && printed < 120 && ref->getClock() < target; i++) {
                    const uint32_t pc = ref->getPC0();
                    if (!on && pc == fromPc) on = true;
                    const moira::i64 before = ref->getClock();
                    if (on) ref->disassemble(line, pc);
                    ref->execute();
                    if (on) {
                        std::printf("    t %06X +%-6lld %-36s SR=%04X CACR=%08X\n", pc,
                                    (long long)(before - c0), line, ref->getSR(), ref->getCACR());
                        printed++;
                    }
                }
            }
        }
    }
    Totals st = retired;
    st.add(*engine);
    const unsigned long long native = st.instrs - st.slowInstrs - st.windowInstrs - st.traceInstrs;
    if (stats) {
        std::printf("[jit_program_fuzz] exceptions by vector (interpreter):");
        for (int v = 0; v < 256; v++)
            if (ref->vectors[v]) std::printf(" %d:%llu", v, (unsigned long long)ref->vectors[v]);
        std::printf("\n");
    }
    if (stats && gMmu) {
        // Descriptor U bits the last program's walks set: proof translation ran.
        unsigned used = 0;
        for (uint32_t a = 0x00800000; a < 0x00806000; a += 4) used += (ref->mem[a + 3] & 0x08) != 0;
        std::printf("[jit_program_fuzz] descriptors with U set (last program): %u\n", used);
    }
    if (gSave) std::printf("[jit_program_fuzz] save-state round trips: %llu\n",
                           (unsigned long long)saves);
    if (gIrq) std::printf("[jit_program_fuzz] interrupts taken: interpreter %llu, translator side %llu\n",
                          (unsigned long long)ref->interrupts, (unsigned long long)nat->interrupts);
    std::printf("[jit_program_fuzz] %s: %llu failure(s); blocks compiled=%llu run=%llu instrs=%llu native=%llu slow=%llu\n",
                failures ? "FAIL" : "OK", (unsigned long long)failures,
                (unsigned long long)st.blocksCompiled, (unsigned long long)st.blocksRun,
                (unsigned long long)st.instrs, native, (unsigned long long)st.slowInstrs);
    // POM68K_JIT_REQUIRE_NATIVE (the jit-fast contract, as in
    // SstJitLockstep.h): on a native host a run that compiled nothing
    // proves only the interpreter against itself.
    if (std::getenv("POM68K_JIT_REQUIRE_NATIVE") &&
        (!engine->nativeBackend() || native == 0)) {
        std::printf("[jit_program_fuzz] FAIL: no native code ran (backend %s)\n",
                    engine->backendName());
        return 1;
    }
    return failures ? 1 : 0;
}
