// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// AArch64 store-guard gate. MOVE.L D0,d16(A7) is first trained
// against ordinary RAM, where a zero codeMask must permit a direct store.
// The cached block is then aimed at its own translated slice: that write must
// take the exact memory-map path and evict the stale block.

#include "Moira.h"
#include "jit/JitEngine.h"
#include "JitTestConfig.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace {

constexpr uint32_t kCode = 0x001100;
constexpr uint32_t kData = 0x004000;
constexpr uint32_t kStack = 0x008000;

const jit::ResolvedConfig& injectedJitConfig() {
    static const jit::ResolvedConfig config =
        testjit::resolveFromEnvironment();
    return config;
}

// Heap-owned, never a local: see the GateCpu note in tests/jit_copyback_write_040_test.cpp.
class GuardCpu final : public moira::Moira {
public:
    GuardCpu()
        : mem(1u << 24, 0), jit(*this, hooks(this), jit::kGuest68040,
                                injectedJitConfig()) {
        setModel(moira::Model::M68040);
    }

    std::vector<uint8_t> mem;
    jit::CodeGuard* guard = nullptr;
    uint64_t mappedWrites = 0;
    jit::Engine jit;

private:
    static jit::MemoryHooks hooks(GuardCpu* cpu) {
        jit::MemoryHooks h;
        h.self = cpu;
        h.codeSpan = [](void* s, uint32_t p, uint32_t& len) -> const uint8_t* {
            auto& c = *static_cast<GuardCpu*>(s);
            p &= 0xFFFFFF;
            len = uint32_t(c.mem.size()) - p;
            return c.mem.data() + p;
        };
        h.dataSpan = [](void* s, uint32_t p, uint32_t& len, int) -> uint8_t* {
            auto& c = *static_cast<GuardCpu*>(s);
            p &= 0xFFFFFF;
            len = uint32_t(c.mem.size()) - p;
            return c.mem.data() + p;
        };
        h.setGuard = [](void* s, jit::CodeGuard* g) {
            static_cast<GuardCpu*>(s)->guard = g;
        };
        h.ramBytes = [](void* s) {
            return uint32_t(static_cast<GuardCpu*>(s)->mem.size());
        };
        return h;
    }

    moira::u8 read8(moira::u32 a) const override {
        return mem[a & 0xFFFFFF];
    }
    moira::u16 read16(moira::u32 a) const override {
        const uint32_t p = a & 0xFFFFFF;
        return moira::u16(mem[p] << 8 | mem[(p + 1) & 0xFFFFFF]);
    }
    void write8(moira::u32 a, moira::u8 v) const override {
        auto& c = *const_cast<GuardCpu*>(this);
        const uint32_t p = a & 0xFFFFFF;
        c.mappedWrites++;
        if (c.guard) c.guard->note(p, 1);
        c.mem[p] = v;
    }
    void write16(moira::u32 a, moira::u16 v) const override {
        auto& c = *const_cast<GuardCpu*>(this);
        const uint32_t p = a & 0xFFFFFF;
        c.mappedWrites++;
        if (c.guard) c.guard->note(p, 2);
        c.mem[p] = moira::u8(v >> 8);
        c.mem[(p + 1) & 0xFFFFFF] = moira::u8(v);
    }
};

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %-68s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

void put16(GuardCpu& c, uint32_t a, uint16_t v) {
    c.mem[a] = uint8_t(v >> 8);
    c.mem[a + 1] = uint8_t(v);
}

void put32(GuardCpu& c, uint32_t a, uint32_t v) {
    put16(c, a, uint16_t(v >> 16));
    put16(c, a + 2, uint16_t(v));
}

uint32_t get32(const GuardCpu& c, uint32_t a) {
    return uint32_t(c.mem[a]) << 24 | uint32_t(c.mem[a + 1]) << 16 |
           uint32_t(c.mem[a + 2]) << 8 | c.mem[a + 3];
}

void install(GuardCpu& c) {
    put32(c, 0, kStack);
    put32(c, 4, kCode);
    put16(c, kCode + 0, 0x2F40); // MOVE.L D0,d16(A7)
    put16(c, kCode + 2, 0x0000);
    put16(c, kCode + 4, 0x60FA); // BRA.S kCode
    put16(c, kCode + 6, 0x4E71);
}

void installEorLoop(GuardCpu& c) {
    put32(c, 0, kStack);
    put32(c, 4, kCode);
    put16(c, kCode + 0, 0xB592); // EOR.L D2,(A2)
    put16(c, kCode + 2, 0x60FC); // BRA.S kCode
    put16(c, kCode + 4, 0x4E71);
}

void cloneBoundary(const GuardCpu& from, GuardCpu& to) {
    to.mem = from.mem;
    for (int i = 0; i < 8; i++) {
        to.setD(i, from.getD(i));
        to.setA(i, from.getA(i));
    }
    to.setPC(from.getPC()); to.setPC0(from.getPC0());
    to.setIRD(from.getIRD()); to.setIRC(from.getIRC());
    to.setSR(from.getSR()); to.setClock(from.getClock());
}

bool sameBoundary(const GuardCpu& a, const GuardCpu& b) {
    for (int i = 0; i < 8; i++) {
        if (a.getD(i) != b.getD(i) || a.getA(i) != b.getA(i)) return false;
    }
    return a.getPC() == b.getPC() && a.getPC0() == b.getPC0() &&
           a.getIRD() == b.getIRD() && a.getIRC() == b.getIRC() &&
           a.getSR() == b.getSR() && a.getClock() == b.getClock();
}

} // namespace

int main() {
    std::printf("jit_store_guard_a64_test — exact zero-mask + SMC gate\n");
    setenv("POM68K_JIT_BACKEND", "a64", 1);
    setenv("POM68K_JIT_BLOCKS", "1", 1);
    setenv("POM68K_JIT_HOT", "1", 1);

    const auto refOwner = std::make_unique<GuardCpu>();
    GuardCpu& ref = *refOwner;
    const auto nativeOwner = std::make_unique<GuardCpu>();
    GuardCpu& native = *nativeOwner;
    install(ref); install(native);
    ref.reset(); native.reset();
    ref.setTC(0); native.setTC(0); // identity translation

    if (std::strcmp(native.jit.backendName(), "aarch64") != 0) {
        std::printf("SKIP: AArch64 backend unavailable (%s)\n",
                    native.jit.backendName());
        return 0;
    }

    native.setA(7, kData);
    native.setD(0, 0x11223344);
    native.jit.setEnabled(true);
    native.jit.executeUntil(native.getClock() + 512);
    const auto trained = native.jit.stats().snapshot();
    check(trained.blocksCompiled != 0, "2F40 loop compiled before guard tests");
    check(native.guard && native.guard->pageMap[kCode >> jit::CodeGuard::kShift],
          "training marks the translated code slice");

    // Once compiled, a zero-mask destination must remain entirely outside
    // the memory-map callback: this is the optimization under test.
    native.setD(0, 0xCAFEBABE);
    const uint64_t writesBefore = native.mappedWrites;
    native.jit.executeUntil(native.getClock() + 128);
    const auto direct = native.jit.stats().snapshot();
    check(direct.blocksRun > trained.blocksRun, "cached 2F40 block ran against ordinary RAM");
    check(native.guard && native.guard->pageMap[kCode >> jit::CodeGuard::kShift],
          "ordinary direct stores retain the translated code slice");
    check(native.mappedWrites == writesBefore,
          "zero-mask 2F40 used the direct store path");
    check(get32(native, kData) == 0xCAFEBABE,
          "direct store preserved guest byte order and value");

    // Reuse that exact cached block with its EA aimed into its own live code
    // slice. The true mask must route the store through Moira and let
    // CodeGuard evict the translation.
    for (int i = 0; native.getPC() != kCode && i < 4; i++)
        native.jit.executeUntil(native.getClock() + 1);
    check(native.guard && native.guard->pageMap[kCode >> jit::CodeGuard::kShift],
          "translated code slice is armed before self-modification");
    check(native.getPC() == kCode, "cached loop is aligned at the direct store");

    // Clone the exact already-trained boundary into the interpreter oracle;
    // do not reset/reconfigure the native CPU, since doing so would make this
    // an address-map test instead of a cached-block SMC test.
    cloneBoundary(native, ref);
    ref.setA(7, kCode); native.setA(7, kCode);
    ref.setD(0, 0x4E714E71); native.setD(0, 0x4E714E71);
    ref.mappedWrites = 0; native.mappedWrites = 0;
    const auto beforeSmc = native.jit.stats().snapshot();
    ref.executeUntil(ref.getClock() + 1);
    native.jit.executeUntil(native.getClock() + 1);
    const auto afterSmc = native.jit.stats().snapshot();

    check(native.mappedWrites != 0,
          "true codeMask forced the store through the memory map");
    if (afterSmc.invalidations != beforeSmc.invalidations + 1)
        std::printf("    invalidations: before=%llu after=%llu\n",
                    static_cast<unsigned long long>(beforeSmc.invalidations),
                    static_cast<unsigned long long>(afterSmc.invalidations));
    check(afterSmc.invalidations == beforeSmc.invalidations + 1,
          "self-modifying write caused one precise JIT invalidation");
    check(get32(native, kCode) == 0x4E714E71,
          "self-modifying store updated all four code bytes");
    check(std::memcmp(ref.mem.data(), native.mem.data(), ref.mem.size()) == 0,
          "interpreter and isolated native path leave identical memory");
    check(sameBoundary(ref, native),
          "interpreter and isolated native path leave identical boundary state");

    // Repeat the complete contract for a read-modify-write form, so both a
    // sole store and a two-access store are covered by the global guard.
    const auto eorRefOwner = std::make_unique<GuardCpu>();
    GuardCpu& eorRef = *eorRefOwner;
    const auto eorNativeOwner = std::make_unique<GuardCpu>();
    GuardCpu& eorNative = *eorNativeOwner;
    installEorLoop(eorRef); installEorLoop(eorNative);
    eorRef.reset(); eorNative.reset();
    eorRef.setTC(0); eorNative.setTC(0);
    eorNative.setA(2, kData);
    eorNative.setD(2, 0x01020304);
    eorNative.jit.executeUntil(eorNative.getClock() + 512);
    const auto eorTrained = eorNative.jit.stats().snapshot();
    check(eorTrained.blocksCompiled != 0, "B592 loop compiled before guard tests");
    check(eorNative.guard &&
              eorNative.guard->pageMap[kCode >> jit::CodeGuard::kShift],
          "B592 training marks the translated code slice");

    eorNative.setD(2, 0xA5A5A5A5);
    const uint64_t eorWritesBefore = eorNative.mappedWrites;
    eorNative.jit.executeUntil(eorNative.getClock() + 128);
    const auto eorDirect = eorNative.jit.stats().snapshot();
    check(eorDirect.blocksRun > eorTrained.blocksRun,
          "cached B592 block ran against ordinary RAM");
    check(eorNative.mappedWrites == eorWritesBefore,
          "zero-mask B592 used the direct read-modify-write path");

    for (int i = 0; eorNative.getPC() != kCode && i < 4; i++)
        eorNative.jit.executeUntil(eorNative.getClock() + 1);
    check(eorNative.getPC() == kCode, "cached B592 loop is aligned at EOR.L");
    cloneBoundary(eorNative, eorRef);
    constexpr uint32_t replacement = 0x4E714E71;
    const uint32_t original = get32(eorNative, kCode);
    eorRef.setA(2, kCode); eorNative.setA(2, kCode);
    eorRef.setD(2, original ^ replacement);
    eorNative.setD(2, original ^ replacement);
    eorRef.mappedWrites = 0; eorNative.mappedWrites = 0;
    const auto eorBeforeSmc = eorNative.jit.stats().snapshot();
    eorRef.executeUntil(eorRef.getClock() + 1);
    eorNative.jit.executeUntil(eorNative.getClock() + 1);
    const auto eorAfterSmc = eorNative.jit.stats().snapshot();

    check(eorNative.mappedWrites != 0,
          "true codeMask forces B592 through the exact memory map");
    check(eorAfterSmc.invalidations == eorBeforeSmc.invalidations + 1,
          "B592 self-modification causes one precise invalidation");
    check(get32(eorNative, kCode) == replacement,
          "B592 self-modification updates the complete instruction pair");
    check(std::memcmp(eorRef.mem.data(), eorNative.mem.data(), eorRef.mem.size()) == 0,
          "B592 interpreter and native paths leave identical memory");
    check(sameBoundary(eorRef, eorNative),
          "B592 interpreter and native paths leave identical boundary state");

    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
