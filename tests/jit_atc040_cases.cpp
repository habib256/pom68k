// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ATC effects must survive every path that bypasses the MMU accessor:
// interpreter windows, threaded replay, native DTLB and native cache lines.
// Compare against a bare interpreter, including the full access counts.
#include "jit_atc040_cases.h"
#include "Moira.h"
#include "jit/JitEngine.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace {
constexpr uint32_t Mask = (1u << 20) - 1;
constexpr uint32_t Code = 0x10000100, Data = 0x20006100;
constexpr uint32_t Stack = 0x3000DF00, Handler = 0x1800;
constexpr uint32_t Root = 0x40000, DataLeaf = 0x44000;

class Cpu final : public moira::Moira {
public:
    std::vector<uint8_t> ram = std::vector<uint8_t>(Mask + 1);
    jit::CodeGuard* guard = nullptr;
    jit::Engine engine;
    uint32_t hole = ~0u;
    unsigned faults = 0;
    explicit Cpu(const jit::ResolvedConfig& config, moira::Model model)
        : engine(*this, hooks(this), jit::kGuest68040, config) { setModel(model); }
    void word(uint32_t a, uint16_t v) {
        ram[a & Mask] = uint8_t(v >> 8); ram[(a + 1) & Mask] = uint8_t(v);
    }
    void longword(uint32_t a, uint32_t v) { word(a, uint16_t(v >> 16)); word(a + 2, uint16_t(v)); }
    uint32_t longwordValue(uint32_t a) const {
        return uint32_t(ram[a & Mask]) << 24 | uint32_t(ram[(a + 1) & Mask]) << 16 |
               uint32_t(ram[(a + 2) & Mask]) << 8 | ram[(a + 3) & Mask];
    }
    uint32_t leafAddress(uint32_t la, bool page8k, uint32_t leaf) const {
        return leaf + (page8k ? (la >> 11) & 0x7C : (la >> 10) & 0xFC);
    }
    void map(uint32_t la, bool page8k, uint32_t pointer, uint32_t leaf, uint32_t cm) {
        longword(Root + ((la >> 23) & 0x1FC), pointer | 2);
        longword(pointer + ((la >> 16) & 0x1FC), leaf | 2);
        const uint32_t pageMask = page8k ? ~0x1FFFu : ~0xFFFu;
        longword(leafAddress(la, page8k, leaf), (la & Mask & pageMask) | 0x19 | cm);
    }
    void install(bool simple = false) {
        std::fill(ram.begin(), ram.end(), 0);
        longword(0, 0xE000); longword(4, Code & Mask); longword(8, Handler);
        word(Handler, 0x4E71); word(Handler + 2, 0x60FC);
        uint32_t pc = Code;
        const auto emit = [&](uint16_t op) { word(pc, op); pc += 2; };
        emit(0x2010);                   // MOVE.L (A0),D0
        if (!simple) {
            emit(0x5290);               // ADDQ.L #1,(A0), read + write
            emit(0x2280);               // MOVE.L D0,(A1)
            emit(0x2491);               // MOVE.L (A1),(A2)
            emit(0xB98B);               // CMPM.L (A3)+,(A4)+
            emit(0x48EA); emit(3); emit(16); // MOVEM.L D0-D1,16(A2)
            emit(0x4CEA); emit(12); emit(16);// MOVEM.L 16(A2),D2-D3
            emit(0xEAD2); emit(0x01C0); // BFCHG (A2){7:32}, four accesses
            emit(0xE8D2); emit(8);      // BFTST (A2){0:8}, no tail
            // Three-byte fields: Moira's word + byte in each direction, two
            // data-ATC accesses each that a longword proof must still count.
            emit(0x7C10);               // MOVEQ #16,D6
            emit(0xE9D2); emit(0x5110); // BFEXTU (A2){4:16},D5
            emit(0xEFD2); emit(0x5094); // BFINS D5,(A2){2:20}
            emit(0xEAD2); emit(0x0126); // BFCHG (A2){4:D6}, three bytes at run time
            emit(0x4DE8); emit(4);      // LEA 4(A0),A6
            emit(0x2836); emit(0x0151); // MOVE.L ([A6]),D4, index suppressed
            emit(0x4EB5); emit(0x0151); // JSR ([A5]), native on A64
            emit(0x4EB9);               // JSR absolute long
            longword(pc, Code + 0x80); pc += 4;
        }
        emit(uint16_t(0x6000 | uint8_t(Code - (pc + 2))));
        word(Code + 0x80, 0x5287); word(Code + 0x82, 0x4E75);
        for (unsigned i = 0; i < 0x500; ++i) ram[(Data + i) & Mask] = uint8_t(i * 37 + 11);
        longword(Data + 4, Data + 0xC0);
        longword(Data + 0xD0, Code + 0x80);
    }
    void installWrite() {
        install(true);
        word(Code, 0x2140); word(Code + 2, 2); // MOVE.L D0,2(A0)
        word(Code + 4, 0x60FA); word(Code + 6, 0x4E71);
    }
    void start(bool page8k, bool cache, unsigned cm, bool super) {
        reset(); hole = ~0u; faults = 0;
        pomCache040Inst().invalidateAll(); pomCache040Data().invalidateAll();
        // Training runs advance the §4.1 counters; CINV does not reset them.
        pomCache040Inst().rot = 0; pomCache040Data().rot = 0;
        setITT0(0x0000C040); setDTT0(0x0000C040);
        setITT1(0); setDTT1(0);
        // A cache-active NC instruction page cannot arm the production
        // code window: it never has resident I-cache bytes. Keep code WT
        // while independently exercising NC, WT and CB data translations.
        map(Code, page8k, 0x41000, 0x43000, cache ? 0 : cm);
        map(Data, page8k, 0x42000, DataLeaf, cm);
        map(Stack, page8k, 0x42200, 0x45000, cm);
        setURP040(Root); setSRP040(Root); setTC040(page8k ? 0xC000 : 0x8000);
        setPomCache040(cache); setCACR(cache ? 0x80008000 : 0);
        setISP(0xE000); setUSP(Stack); setSR(super ? 0x271F : 0x071F);
        for (int i = 0; i < 8; ++i) { setD(i, 0x13579BDFu + unsigned(i)); setA(i, Data); }
        setA(0, Data); setA(1, Data + 0x40); setA(2, Data + 0x80);
        setA(3, Data + 0xA0); setA(4, Data + 0xB0); setA(7, Stack);
        setA(5, Data + 0xD0);
        setPC(Code); setPC0(Code); setIRD(0x2010); setIRC(0x5290); setClock(0);
        *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(this) + pomJitLayout().flags) = 0;
    }
private:
    static jit::MemoryHooks hooks(Cpu* c) {
        jit::MemoryHooks h; h.self = c;
        h.codeSpan = [](void* self, uint32_t p, uint32_t& len) -> const uint8_t* {
            auto& c = *static_cast<Cpu*>(self); p &= Mask;
            if (p >= c.hole && p - c.hole < 0x1000) return nullptr;
            len = Mask + 1 - p;
            if (p < c.hole) len = std::min(len, c.hole - p);
            return c.ram.data() + p;
        };
        h.dataSpan = [](void* self, uint32_t p, uint32_t& len, int) -> uint8_t* {
            auto& c = *static_cast<Cpu*>(self); p &= Mask;
            if (p >= c.hole && p - c.hole < 0x1000) return nullptr;
            len = Mask + 1 - p;
            if (p < c.hole) len = std::min(len, c.hole - p);
            return c.ram.data() + p;
        };
        h.setGuard = [](void* self, jit::CodeGuard* g) { static_cast<Cpu*>(self)->guard = g; };
        h.ramBytes = [](void*) { return Mask + 1; };
        return h;
    }
    void fault(uint32_t p) const {
        if (p >= hole && p - hole < 0x1000) {
            auto& c = *const_cast<Cpu*>(this);
            ++c.faults; c.extBusError040();
        }
    }
    moira::u8 read8(moira::u32 a) const override { a &= Mask; fault(a); return ram[a]; }
    moira::u16 read16(moira::u32 a) const override {
        a &= Mask; fault(a); return moira::u16(ram[a] << 8 | ram[(a + 1) & Mask]);
    }
    void write8(moira::u32 a, moira::u8 v) const override {
        auto& c = *const_cast<Cpu*>(this); a &= Mask; fault(a);
        if (guard) guard->note(a, 1);
        c.ram[a] = v;
    }
    void write16(moira::u32 a, moira::u16 v) const override {
        auto& c = *const_cast<Cpu*>(this); a &= Mask; fault(a);
        if (guard) guard->note(a, 2);
        c.ram[a] = uint8_t(v >> 8); c.ram[(a + 1) & Mask] = uint8_t(v);
    }
};

bool same(const Cpu& a, const Cpu& b) {
    bool ok = true;
    for (int i = 0; i < 8; ++i) ok &= a.getD(i) == b.getD(i) && a.getA(i) == b.getA(i);
    ok &= a.getPC() == b.getPC() && a.getPC0() == b.getPC0() && a.getSR() == b.getSR() &&
          a.getClock() == b.getClock() && a.getIRD() == b.getIRD() && a.getIRC() == b.getIRC() &&
          a.getUSP() == b.getUSP() && a.getISP() == b.getISP() && a.getMSP() == b.getMSP();
    ok &= a.pomJitAtcAccesses040(false) == b.pomJitAtcAccesses040(false) &&
          a.pomJitAtcAccesses040(true) == b.pomJitAtcAccesses040(true) && a.ram == b.ram;
    // MC68040UM-1993 §4.1 replacement counters: every path that serves a
    // cache access natively must count it (POM68K_VENDOR.md row 85).
    ok &= a.pomCache040Inst().rot == b.pomCache040Inst().rot &&
          a.pomCache040Data().rot == b.pomCache040Data().rot;
    if (!ok) {
        std::printf("      cache rot I=%u/%u D=%u/%u\n", a.pomCache040Inst().rot,
                    b.pomCache040Inst().rot, a.pomCache040Data().rot, b.pomCache040Data().rot);
        std::printf("      PC=%08X/%08X clock=%lld/%lld SR=%04X/%04X I=%u/%u D=%u/%u queue=%04X:%04X/%04X:%04X\n",
            a.getPC0(), b.getPC0(), (long long)a.getClock(), (long long)b.getClock(), a.getSR(), b.getSR(),
            a.pomJitAtcAccesses040(false), b.pomJitAtcAccesses040(false),
            a.pomJitAtcAccesses040(true), b.pomJitAtcAccesses040(true),
            a.getIRD(), a.getIRC(), b.getIRD(), b.getIRC());
        for (int i = 0; i < 8; ++i)
            if (a.getD(i) != b.getD(i) || a.getA(i) != b.getA(i))
                std::printf("      D%d=%08X/%08X A%d=%08X/%08X\n", i, a.getD(i), b.getD(i), i, a.getA(i), b.getA(i));
        const auto diff = std::mismatch(a.ram.begin(), a.ram.end(), b.ram.begin());
        if (diff.first != a.ram.end()) std::printf("      RAM=%05X %02X/%02X\n", unsigned(diff.first - a.ram.begin()), *diff.first, *diff.second);
    }
    return ok;
}

bool run(const jit::ResolvedConfig& base, moira::Model model, bool page8k,
         unsigned profile, bool super, unsigned strategy, bool packed) {
    auto config = base; config.backend = strategy == 1 ? "threaded" : "auto";
    if (strategy == 1) config.requireNative = false;   // deliberate threaded arm
    config.packedCcr = packed; config.cache040LineReadStats = true;
    config.dataWindow = true; config.dataWindowExplicit = true;
    auto ref = std::make_unique<Cpu>(config, model), target = std::make_unique<Cpu>(config, model);
    ref->engine.setEnabled(false); target->engine.setEnabled(strategy != 2);
    const unsigned cm = profile < 2 ? 0x40 : profile == 2 ? 0 : 0x20;
    ref->install(); target->install();
    ref->start(page8k, profile != 0, cm, super); target->start(page8k, profile != 0, cm, super);
    ref->pomSetInterpWindow(false, nullptr, nullptr); ref->pomJitDtlbFillFn = nullptr;
    for (int step = 0; step < 96; ++step) {
        const auto deadline = ref->getClock() + 1;
        ref->executeUntil(deadline);
        if (strategy == 2) target->executeUntil(deadline); else target->engine.executeUntil(deadline);
        if (!same(*ref, *target)) {
            std::printf("    ATC accounting FAIL model=%u page8k=%u profile=%u S=%u strategy=%u packed=%u checkpoint=%d\n",
                unsigned(model), page8k, profile, super, strategy, packed, step);
            return false;
        }
    }
    const auto stats = target->engine.stats().snapshot();
    const bool native = strategy != 0 || (stats.blocksCompiled && stats.blocksRun && stats.instrs);
    const bool lines = strategy != 0 || profile < 2 ||
        target->pomJitCache040NativeReads();
    if (!native || !lines || !ref->pomJitAtcAccesses040(false) || !ref->pomJitAtcAccesses040(true)) {
        std::printf("    ATC accounting missing access/native witness profile=%u S=%u strategy=%u: compiled=%llu runs=%llu native=%llu lineR=%llu lineW=%llu\n",
            profile, super, strategy,
            (unsigned long long)stats.blocksCompiled, (unsigned long long)stats.blocksRun, (unsigned long long)stats.instrs,
            (unsigned long long)target->pomJitCache040NativeReads(), (unsigned long long)target->pomJitCache040NativeWrites());
        return false;
    }
    return true;
}

// Enter an already compiled block with a cold or denied data translation.
// The failed exact thunk must restore its speculative ATC effects before
// Moira repeats the instruction and constructs the architectural frame.
bool faultCase(const jit::ResolvedConfig& base, moira::Model model, bool page8k,
               bool cache, bool super, unsigned kind, bool packed) {
    auto config = base; config.packedCcr = packed;
    config.dataWindow = true; config.dataWindowExplicit = true;
    auto ref = std::make_unique<Cpu>(config, model), target = std::make_unique<Cpu>(config, model);
    ref->engine.setEnabled(false); target->engine.setEnabled(true);
    for (auto* c : {ref.get(), target.get()}) {
        if (kind == 2) c->installWrite(); else c->install(true);
        c->start(page8k, cache, 0x40, super);
    }
    target->engine.executeUntil(512);
    if (!target->engine.stats().snapshot().blocksRun) {
        const auto s = target->engine.stats().snapshot();
        std::printf("    ATC fault training failed cache=%u S=%u kind=%u compiled=%llu runs=%llu native=%llu PC=%08X clock=%lld\n",
            cache, super, kind, (unsigned long long)s.blocksCompiled, (unsigned long long)s.blocksRun,
            (unsigned long long)s.instrs, target->getPC0(), (long long)target->getClock());
        return false;
    }
    for (auto* c : {ref.get(), target.get()}) {
        if (kind == 2) c->installWrite(); else c->install(true);
        c->start(page8k, cache, 0x40, super);
        // Exception stacking stays in the low, transparent region even
        // when the descriptor bus itself is the failing physical region.
        c->setISP(0xE000);
        if (super) c->setA(7, 0xE000);
        uint16_t word = 0;
        if (!c->pomJitReadProg(Code, word)) return false;
        const uint32_t pte = c->leafAddress(Data, page8k, DataLeaf);
        const uint32_t physical = (Data & Mask) & (page8k ? ~0x1FFFu : ~0xFFFu);
        if (kind == 0) c->longword(pte, 0); // cold invalid descriptor
        if (kind == 1) {
            c->longword(pte, physical | 0xD9); // cached supervisor denial
            c->setSR(0x071F);
            uint32_t ignored = 0;
            if (c->pomJitReadData(Data, 4, ignored)) return false;
        }
        if (kind == 2) {
            c->longword(pte, physical | 0x5D); // resident write protection
            uint32_t ignored = 0;
            if (!c->pomJitReadData(Data, 4, ignored)) return false;
        }
        if (kind == 3) c->hole = Root; // descriptor /BERR after code warm-up
        if (kind == 4) c->hole = Data & Mask & ~0xFFFu; // operand /BERR
        *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(c) + c->pomJitLayout().flags) = 0;
    }
    ref->pomSetInterpWindow(false, nullptr, nullptr); ref->pomJitDtlbFillFn = nullptr;
    const auto before = target->engine.stats().snapshot();
    const auto deadline = ref->getClock() + 1;
    ref->executeUntil(deadline); target->engine.executeUntil(deadline);
    const auto after = target->engine.stats().snapshot();
    const bool ok = same(*ref, *target) && after.blocksRun > before.blocksRun &&
                    ref->getPC0() >= Handler && ref->getPC0() <= Handler + 2 &&
                    ref->getSP() == 0xE000 - 60 && !ref->isHalted() && !target->isHalted();
    if (!ok) std::printf("    ATC fault FAIL model=%u 8k=%u cache=%u S=%u kind=%u packed=%u runs=%llu slow=%llu\n",
        unsigned(model), page8k, cache, super, kind, packed,
        (unsigned long long)(after.blocksRun - before.blocksRun),
        (unsigned long long)(after.slowInstrs - before.slowInstrs));
    return ok;
}

bool replacementCase(const jit::ResolvedConfig& base, moira::Model model, bool page8k,
                     bool cache, bool super, bool data, unsigned strategy,
                     unsigned form, unsigned accesses) {
    auto config = base; config.backend = strategy == 1 ? "threaded" : "auto";
    if (strategy == 1) config.requireNative = false;   // deliberate threaded arm
    config.dataWindow = true; config.dataWindowExplicit = true;
    config.cache040LineReadStats = true;
    // Plain RAM, no peripheral deadlines: exercise the optional cache-active
    // positioned-poll admission as well as the production fallback arm above.
    config.cache040LatePoll = true;
    auto ref = std::make_unique<Cpu>(config, model), target = std::make_unique<Cpu>(config, model);
    ref->engine.setEnabled(false); target->engine.setEnabled(strategy != 2);
    const auto install = [&](Cpu& c) {
        if (form == 2) c.installWrite(); else c.install(true);
        if (form == 1) { c.word(Code, 0x2028); c.word(Code + 2, 4); c.word(Code + 4, 0x60FA); }
        c.start(page8k, cache, cache ? 0x20 : 0x40, super);
    };
    install(*ref); install(*target);
    if (strategy == 0) {
        uint16_t opcode = 0; uint32_t value = 0;
        if (!target->pomJitReadProg(Code, opcode) || !target->pomJitReadData(Data, 4, value) ||
            (form == 2 && !target->pomJitWriteData(Data + 2, 4, target->getD(0)))) return false;
        target->engine.executeUntil(target->getClock() + 512);
    }
    // Recreate identical architectural state while retaining generated code.
    install(*ref); install(*target);
    const uint32_t spacing = (page8k ? 8192 : 4096) * 16;
    const uint32_t first = data ? Data : Code;
    uint32_t ptes[5] {};
    for (auto* c : {ref.get(), target.get()}) {
        ptes[0] = c->leafAddress(first, page8k, data ? DataLeaf : 0x43000);
        for (unsigned way = 1; way < 5; ++way) {
            const uint32_t la = first + way * spacing;
            const uint32_t pointer = data ? 0x42000 : 0x41000;
            const uint32_t pointerIndex = (la >> 16) & 0x1FC;
            const uint32_t leaf = pointerIndex == ((first >> 16) & 0x1FC)
                ? (data ? DataLeaf : 0x43000)
                : (data ? 0x60000 : 0x50000) + pointerIndex * 0x100;
            c->map(la, page8k, pointer, leaf, data && cache ? 0x20 : 0);
            ptes[way] = c->leafAddress(la, page8k, leaf);
            // Distinct physical pages make a repaired victim observable.
            c->longword(ptes[way], (0x20000 + way * 0x2000) | 0x19 | (data && cache ? 0x20 : 0));
            c->word(0x20000 + way * 0x2000 + (la & (page8k ? 8191 : 4095)), 0x4E71);
        }
        uint16_t opcode = 0; uint32_t value = 0;
        const bool codeOk = c->pomJitReadProg(Code, opcode);
        const bool dataOk = c->pomJitReadData(Data, 4, value);
        if (!codeOk || !dataOk) {
            std::printf("      replacement warm-up failed I=%u D=%u roots=%08X/%08X pointers=%08X/%08X leaves=%08X/%08X\n",
                codeOk, dataOk, c->longwordValue(Root + ((Code >> 23) & 0x1FC)),
                c->longwordValue(Root + ((Data >> 23) & 0x1FC)),
                c->longwordValue(0x41000), c->longwordValue(0x42000),
                c->longwordValue(c->leafAddress(Code, page8k, 0x43000)),
                c->longwordValue(c->leafAddress(Data, page8k, DataLeaf)));
            return false;
        }
    }
    ref->pomSetInterpWindow(false, nullptr, nullptr); ref->pomJitDtlbFillFn = nullptr;
    // Resolve the engine's one-time generation/guard invalidation before
    // publishing the cache-line hit used by the measured native prefix.
    const auto admissionDeadline = ref->getClock() + 1;
    ref->executeUntil(admissionDeadline);
    if (strategy == 2) target->executeUntil(admissionDeadline);
    else target->engine.executeUntil(admissionDeadline);
    if (!same(*ref, *target)) return false;
    for (auto* c : {ref.get(), target.get()}) {
        c->setPC(Code); c->setPC0(Code);
        uint16_t opcode = 0; uint32_t value = 0;
        for (unsigned way = 1; way < 4; ++way) {
            const uint32_t la = first + way * spacing;
            if (data ? !c->pomJitReadData(la, 4, value) : !c->pomJitReadProg(la, opcode)) {
                std::printf("      replacement conflicting-way warm-up failed\n"); return false;
            }
        }
        // An old-policy ATC fill changes both last-hit memos, including
        // the write memo on a read fill. Select the actual write operand
        // after those fills so the before/after witness reaches its native
        // cache hit under either policy, even for a one-instruction prefix.
        if (form == 2 && !c->pomJitWriteData(Data + 2, 4, c->getD(0))) {
            std::printf("      replacement write warm-up failed\n"); return false;
        }
        for (unsigned way = 0; way < 4; ++way)
            c->longword(ptes[way], c->longwordValue(ptes[way]) & ~8u);
    }
    ref->pomSetInterpWindow(false, nullptr, nullptr); ref->pomJitDtlbFillFn = nullptr;
    const auto before = target->engine.stats().snapshot();
    const auto nativeWritesBefore = target->pomJitCache040NativeWrites();
    for (unsigned n = 0; n < accesses; ++n) {
        for (auto* c : {ref.get(), target.get()}) {
            c->setPC(Code); c->setPC0(Code);
            if (form == 2) c->setD(0, c->getD(0) + 0x01020304);
        }
        const auto deadline = ref->getClock() + 1;
        ref->executeUntil(deadline);
        if (strategy == 2) target->executeUntil(deadline); else target->engine.executeUntil(deadline);
        if (!same(*ref, *target)) return false;
    }
    if (strategy == 0 && target->engine.stats().snapshot().blocksRun == before.blocksRun) {
        std::printf("      replacement did not enter trained native block\n"); return false;
    }
    if (strategy == 0 && cache && form == 2 &&
        target->pomJitCache040NativeWrites() == nativeWritesBefore) {
        std::printf("      replacement did not execute a native copyback write hit\n"); return false;
    }
    uint16_t opcode = 0; uint32_t value = 0;
    for (auto* c : {ref.get(), target.get()}) {
        const uint32_t fifth = first + 4 * spacing;
        if (data ? !c->pomJitReadData(fifth, 4, value) : !c->pomJitReadProg(fifth, opcode)) {
            std::printf("      replacement fifth access failed\n"); return false;
        }
    }
    const auto probe = [&](Cpu& c, unsigned way) {
        uint32_t pa = 0, logical = 0, bytes = 0;
        return data ? c.pomJitProbeData(first + way * spacing, super, false, pa, logical, bytes, true) :
                      c.pomJitProbeCode(first + way * spacing, super, pa, logical, bytes);
    };
    int victim = -1; unsigned missing = 0;
    for (unsigned way = 0; way < 4; ++way) {
        const bool present = probe(*ref, way);
        if (present != probe(*target, way)) {
            std::printf("      replacement victim differs from bare interpreter\n"); return false;
        }
        if (!present) { victim = int(way); ++missing; }
    }
    if (missing != 1 || !probe(*ref, 4) || !probe(*target, 4)) {
        std::printf("      replacement capacity witness: missing=%u (expected one)\n", missing); return false;
    }
    for (auto* c : {ref.get(), target.get()}) {
        const uint32_t la = first + unsigned(victim) * spacing;
        // This descriptor was cleared after warm-up. Only the evicted
        // translation may rewalk and set U; a retained entry ignores it.
        if (data ? !c->pomJitReadData(la, 4, value) : !c->pomJitReadProg(la, opcode)) return false;
        for (unsigned way = 0; way < 4; ++way)
            if (bool(c->longwordValue(ptes[way]) & 8) != (int(way) == victim)) return false;
    }
    uint32_t refValue = 0, targetValue = 0;
    const uint32_t operand = Data + (form == 2 ? 2 : 0);
    return ref->pomJitReadData(operand, 4, refValue) && target->pomJitReadData(operand, 4, targetValue) &&
           refValue == targetValue && (form != 2 || refValue == ref->getD(0)) && same(*ref, *target);
}

// Identity translations through the mirrored physical bus keep the same
// (logical page, physical page, bytes) block proof across TC/TTR changes.
// No instruction writes code. Reusing the same generated blocks must still
// consult live ATC provenance for both instruction and data accesses.
bool transitionCase(const jit::ResolvedConfig& base, moira::Model model, bool page8k,
                    bool cache, bool super, unsigned strategy, bool packed) {
    auto config = base; config.backend = strategy == 1 ? "threaded" : "auto";
    if (strategy == 1) config.requireNative = false;   // deliberate threaded arm
    config.maxBlockInstrs = 1; config.packedCcr = packed;
    config.dataWindow = true; config.dataWindowExplicit = true;
    config.cache040LineReadStats = true;
    config.cache040LatePoll = true;
    auto ref = std::make_unique<Cpu>(config, model), target = std::make_unique<Cpu>(config, model);
    ref->engine.setEnabled(false); target->engine.setEnabled(strategy != 2);
    const auto install = [&](Cpu& c) {
        c.install(true); c.start(page8k, cache, cache ? 0x20 : 0x40, super);
        const uint32_t mask = page8k ? ~0x1FFFu : ~0xFFFu;
        c.longword(c.leafAddress(Code, page8k, 0x43000), (Code & mask) | 0x19 | (cache ? 0 : 0x40));
        c.longword(c.leafAddress(Data, page8k, DataLeaf), (Data & mask) | 0x19 | (cache ? 0x20 : 0x40));
    };
    install(*ref); install(*target);
    if (strategy == 0) {
        uint16_t opcode = 0; uint32_t value = 0;
        if (!target->pomJitReadProg(Code, opcode) || !target->pomJitReadData(Data, 4, value)) return false;
        for (unsigned i = 0; i < 32; ++i) target->engine.executeUntil(target->getClock() + 1);
        const auto trained = target->engine.stats().snapshot();
        if (trained.blocksCompiled < 2 || trained.blocksRun == 0) {
            std::printf("      transition training failed compiled=%llu runs=%llu PC=%08X clock=%lld\n",
                (unsigned long long)trained.blocksCompiled, (unsigned long long)trained.blocksRun,
                target->getPC0(), (long long)target->getClock());
            return false;
        }
    }
    install(*ref); install(*target);
    ref->pomSetInterpWindow(false, nullptr, nullptr); ref->pomJitDtlbFillFn = nullptr;
    uint64_t compiled = 0;
    for (unsigned phase = 0; phase < 5; ++phase) {
        const bool transparent = phase == 1 || phase == 3;
        for (auto* c : {ref.get(), target.get()}) {
            c->setITT1(transparent ? 0x1000C000 | (cache ? 0 : 0x40) : 0);
            c->setDTT1(transparent ? 0x2000C000 | (cache ? 0x20 : 0x40) : 0);
            c->setTC040((page8k ? 0x4000 : 0) | (phase == 3 ? 0 : 0x8000));
            uint16_t opcode = 0;
            if (!c->pomJitReadProg(Code, opcode)) return false;
            c->setPC(Code + 2); c->setPC0(Code + 2);
        }
        // Resolve the new generation on a data-free branch, then publish
        // the data hit after that one-time invalidation has completed.
        const auto admission = ref->getClock() + 1;
        ref->executeUntil(admission);
        if (strategy == 2) target->executeUntil(admission); else target->engine.executeUntil(admission);
        if (!same(*ref, *target)) return false;
        for (auto* c : {ref.get(), target.get()}) {
            uint32_t value = 0;
            if (!c->pomJitReadData(Data, 4, value)) return false;
            c->setPC(Code); c->setPC0(Code);
        }
        const auto before = target->engine.stats().snapshot();
        const auto readsBefore = target->pomJitCache040NativeReads();
        for (unsigned i = 0; i < 16; ++i) {
            const auto deadline = ref->getClock() + 1;
            ref->executeUntil(deadline);
            if (strategy == 2) target->executeUntil(deadline); else target->engine.executeUntil(deadline);
            if (!same(*ref, *target)) {
                std::printf("      transition phase=%u instruction=%u\n", phase, i); return false;
            }
        }
        const auto after = target->engine.stats().snapshot();
        if (strategy == 0 && (after.blocksRun == before.blocksRun ||
            (cache && target->pomJitCache040NativeReads() == readsBefore) ||
            (phase && after.blocksCompiled != compiled))) {
            std::printf("      transition lost native access/block reuse phase=%u compiled=%llu/%llu reads=%llu/%llu\n",
                phase, (unsigned long long)after.blocksCompiled, (unsigned long long)compiled,
                (unsigned long long)target->pomJitCache040NativeReads(), (unsigned long long)readsBefore);
            return false;
        }
        compiled = after.blocksCompiled;
    }
    return true;
}
} // namespace

bool runJitAtc040Cases(const jit::ResolvedConfig& config) {
    unsigned cases = 0, failures = 0;
    for (auto model : {moira::Model::M68LC040, moira::Model::M68040})
    for (bool page8k : {false, true})
    for (unsigned profile = 0; profile != 4; ++profile)
    for (bool super : {false, true})
    for (unsigned strategy = 0; strategy != 3; ++strategy)
    for (bool packed : {false, true}) {
        ++cases;
        if (!run(config, model, page8k, profile, super, strategy, packed)) ++failures;
    }
    for (auto model : {moira::Model::M68LC040, moira::Model::M68040})
    for (bool page8k : {false, true})
    for (bool cache : {false, true})
    for (bool super : {false, true})
    for (unsigned kind = 0; kind != 5; ++kind)
    for (bool packed : {false, true}) {
        if (kind == 1 && super) continue; // denial is observed in user mode
        ++cases;
        if (!faultCase(config, model, page8k, cache, super, kind, packed)) ++failures;
    }
    for (auto model : {moira::Model::M68LC040, moira::Model::M68040})
    for (bool page8k : {false, true})
    for (bool cache : {false, true})
    for (bool super : {false, true})
    for (unsigned strategy = 0; strategy != 3; ++strategy)
    for (bool packed : {false, true}) {
        ++cases;
        if (!transitionCase(config, model, page8k, cache, super, strategy, packed)) {
            ++failures;
            std::printf("    ATC transition FAIL model=%u 8k=%u cache=%u S=%u strategy=%u packed=%u\n",
                unsigned(model), page8k, cache, super, strategy, packed);
        }
    }
    std::printf("    ATC accounting: %u cases, %u failures\n", cases, failures);
    return failures == 0;
}

bool runJitAtc040ReplacementCases(const jit::ResolvedConfig& config) {
    unsigned cases = 0, failures = 0;
    for (auto model : {moira::Model::M68LC040, moira::Model::M68040})
    for (bool page8k : {false, true})
    for (bool cache : {false, true})
    for (bool super : {false, true})
    for (bool data : {false, true})
    for (unsigned strategy = 0; strategy != 3; ++strategy)
    for (unsigned form = 0; form != 3; ++form)
    for (unsigned accesses = 1; accesses != 5; ++accesses) {
        ++cases;
        if (!replacementCase(config, model, page8k, cache, super, data, strategy, form, accesses)) {
            ++failures;
            std::printf("    ATC replacement FAIL model=%u 8k=%u cache=%u S=%u D=%u strategy=%u form=%u accesses=%u\n",
                unsigned(model), page8k, cache, super, data, strategy, form, accesses);
        }
    }
    std::printf("    ATC pipeline replacement: %u cases, %u failures\n", cases, failures);
    return failures == 0;
}
