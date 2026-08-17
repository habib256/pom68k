// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Short, deterministic, asset-free lockstep for the native 68040 JIT.
// Unlike the machine boot lockstep this gate needs no ROM, disk, GUI or
// peripheral model. It proves the daily architectural floor on both native
// hosts: registers/CCR/queue/clock, direct RAM writes and EA commits over a
// linked multi-block loop, then restart-vs-last-write /BERR boundaries and
// their complete format-$7 stack frames.

#include "Moira.h"
#include "jit/JitEngine.h"
#include "jit/JitMetrics.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

#ifndef POM68K_PERF_MIN_BLOCKS_COMPILED
#error "performance_budgets.tsv was not resolved by CMake"
#endif

constexpr uint32_t kRamBytes = 1u << 20;
constexpr uint32_t kRamMask = kRamBytes - 1;
constexpr uint32_t kCode = 0x001100;
constexpr uint32_t kHandler = 0x001800;
constexpr uint32_t kData = 0x004000;
constexpr uint32_t kStack = 0x00E000;
constexpr uint32_t kHole = 0x0F0000;

class SyntheticCpu final : public moira::Moira {
public:
    SyntheticCpu()
        : mem(kRamBytes, 0), jit(*this, hooks(this), jit::kGuest68040) {
        setModel(moira::Model::M68040);
    }

    std::vector<uint8_t> mem;
    jit::Engine jit;
    jit::CodeGuard* guard = nullptr;
    uint32_t busFaults = 0;

    static uint32_t bus(uint32_t a) { return a & kRamMask; }
    static bool inHole(uint32_t a) {
        const uint32_t p = bus(a);
        return p >= kHole && p < kHole + 0x10000;
    }

private:
    static jit::MemoryHooks hooks(SyntheticCpu* cpu) {
        jit::MemoryHooks h;
        h.self = cpu;
        h.codeSpan = [](void* s, uint32_t p, uint32_t& len) -> const uint8_t* {
            auto& c = *static_cast<SyntheticCpu*>(s);
            p = bus(p);
            if (inHole(p)) return nullptr;
            len = uint32_t(c.mem.size()) - p;
            return c.mem.data() + p;
        };
        h.dataSpan = [](void* s, uint32_t p, uint32_t& len, int) -> uint8_t* {
            auto& c = *static_cast<SyntheticCpu*>(s);
            p = bus(p);
            if (inHole(p)) return nullptr;
            len = uint32_t(c.mem.size()) - p;
            return c.mem.data() + p;
        };
        h.setGuard = [](void* s, jit::CodeGuard* g) {
            static_cast<SyntheticCpu*>(s)->guard = g;
        };
        h.ramBytes = [](void* s) {
            return uint32_t(static_cast<SyntheticCpu*>(s)->mem.size());
        };
        return h;
    }

    void fault() const {
        auto& c = *const_cast<SyntheticCpu*>(this);
        c.busFaults++;
        c.extBusError040();
    }

    moira::u8 read8(moira::u32 a) const override {
        if (inHole(a)) fault();
        return mem[bus(a)];
    }
    moira::u16 read16(moira::u32 a) const override {
        if (inHole(a)) fault();
        const uint32_t p = bus(a);
        return moira::u16(mem[p] << 8 | mem[bus(p + 1)]);
    }
    void write8(moira::u32 a, moira::u8 v) const override {
        auto& c = *const_cast<SyntheticCpu*>(this);
        if (inHole(a)) fault();
        const uint32_t p = bus(a);
        if (c.guard) c.guard->note(p, 1);
        c.mem[p] = v;
    }
    void write16(moira::u32 a, moira::u16 v) const override {
        auto& c = *const_cast<SyntheticCpu*>(this);
        if (inHole(a)) fault();
        const uint32_t p = bus(a);
        if (c.guard) c.guard->note(p, 2);
        c.mem[p] = moira::u8(v >> 8);
        c.mem[bus(p + 1)] = moira::u8(v);
    }
};

int failures = 0;
jit::MetricsRecord metrics;

void check(bool ok, const char* what) {
    std::printf("  %-70s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

int unavailableNative(const SyntheticCpu& cpu) {
    const bool required = std::getenv("POM68K_JIT_REQUIRE_NATIVE") != nullptr;
    std::printf("%s: native backend unavailable (%s)\n",
                required ? "FAIL" : "SKIP", cpu.jit.backendName());
    return required ? 1 : 0;
}

void put16(SyntheticCpu& c, uint32_t a, uint16_t v) {
    const uint32_t p = SyntheticCpu::bus(a);
    c.mem[p] = uint8_t(v >> 8);
    c.mem[SyntheticCpu::bus(p + 1)] = uint8_t(v);
}

void put32(SyntheticCpu& c, uint32_t a, uint32_t v) {
    put16(c, a, uint16_t(v >> 16));
    put16(c, a + 2, uint16_t(v));
}

void installVectors(SyntheticCpu& c) {
    put32(c, 0, kStack);
    put32(c, 4, kCode);
    put32(c, 8, kHandler);             // vector 2: bus error
    put16(c, kHandler + 0, 0x4E71);    // NOP
    put16(c, kHandler + 2, 0x60FC);    // BRA.S handler
}

void seedData(SyntheticCpu& c) {
    uint32_t x = 0x6804'05EDu;
    for (uint32_t i = 0; i < 512; i++) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        c.mem[kData + i] = uint8_t(x);
    }
}

void installLoop(SyntheticCpu& c) {
    installVectors(c);
    seedData(c);
    put16(c, kCode + 0x00, 0x7001);    // MOVEQ #1,D0
    put16(c, kCode + 0x02, 0x7E1F);    // MOVEQ #31,D7
    put16(c, kCode + 0x04, 0x207C);    // MOVEA.L #kData,A0
    put32(c, kCode + 0x06, kData);
    put16(c, kCode + 0x0A, 0x247C);    // MOVEA.L #kData,A2
    put32(c, kCode + 0x0C, kData);

    // The steady loop intentionally crosses the memory-contract shapes the
    // native generators share: RMW, sole read with postincrement, address
    // update, register ALU, DBcc, then a stack write/read through BSR/RTS.
    put16(c, kCode + 0x10, 0x2418);    // MOVE.L (A0)+,D2
    put16(c, kCode + 0x12, 0x5988);    // SUBQ.L #4,A0
    put16(c, kCode + 0x14, 0x5280);    // ADDQ.L #1,D0
    put16(c, kCode + 0x16, 0xB592);    // EOR.L D2,(A2)
    put16(c, kCode + 0x18, 0xB580);    // EOR.L D2,D0
    put16(c, kCode + 0x1A, 0x51CF);    // DBF D7,kCode+$10
    put16(c, kCode + 0x1C, 0xFFF4);
    put16(c, kCode + 0x1E, 0x6106);    // BSR.S subroutine
    put16(c, kCode + 0x20, 0x60EE);    // BRA.S kCode+$10
    put16(c, kCode + 0x22, 0x4E71);    // padding
    put16(c, kCode + 0x24, 0x4E71);
    put16(c, kCode + 0x26, 0x2612);    // MOVE.L (A2),D3
    put16(c, kCode + 0x28, 0x4843);    // SWAP D3
    put16(c, kCode + 0x2A, 0x4E75);    // RTS
}

enum class FaultProgram { LastWrite, RestartRead };

void installFaultLoop(SyntheticCpu& c, FaultProgram p) {
    installVectors(c);
    if (p == FaultProgram::LastWrite) {
        put16(c, kCode + 0, 0x2140);   // MOVE.L D0,2(A0)
        put16(c, kCode + 2, 0x0002);
        put16(c, kCode + 4, 0x60FA);   // BRA.S kCode
    } else {
        put16(c, kCode + 0, 0x2020);   // MOVE.L -(A0),D0
        put16(c, kCode + 2, 0x60FC);   // BRA.S kCode
    }
}

void resetCpu(SyntheticCpu& c) {
    c.reset();
    c.setTC(0);                        // identity translation
    c.setCACR(0);                      // isolate the ordinary DTLB protocol
}

void clearRunFlags(SyntheticCpu& c) {
    const auto layout = c.pomJitLayout();
    *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(&c) +
                                 layout.flags) = 0;
}

bool sameCpu(const SyntheticCpu& a, const SyntheticCpu& b, bool diagnose) {
    for (int i = 0; i < 8; i++) {
        if (a.getD(i) != b.getD(i) || a.getA(i) != b.getA(i)) {
            if (diagnose)
                std::printf("    register D%d=%08X/%08X A%d=%08X/%08X\n",
                            i, a.getD(i), b.getD(i), i, a.getA(i), b.getA(i));
            return false;
        }
    }
    const bool same = a.getPC() == b.getPC() && a.getPC0() == b.getPC0() &&
        a.getSR() == b.getSR() && a.getClock() == b.getClock() &&
        a.getIRD() == b.getIRD() && a.getIRC() == b.getIRC() &&
        a.getUSP() == b.getUSP() && a.getISP() == b.getISP() &&
        a.getMSP() == b.getMSP();
    if (!same && diagnose) {
        std::printf("    boundary PC=%08X/%08X PC0=%08X/%08X SR=%04X/%04X "
                    "clock=%lld/%lld IRD=%04X/%04X IRC=%04X/%04X faults=%u/%u\n",
                    a.getPC(), b.getPC(), a.getPC0(), b.getPC0(),
                    a.getSR(), b.getSR(), (long long)a.getClock(),
                    (long long)b.getClock(), a.getIRD(), b.getIRD(),
                    a.getIRC(), b.getIRC(), a.busFaults, b.busFaults);
    }
    return same;
}

bool sameMemory(const SyntheticCpu& a, const SyntheticCpu& b,
                uint32_t first, uint32_t bytes, bool diagnose) {
    for (uint32_t i = 0; i < bytes; i++) {
        const uint32_t p = SyntheticCpu::bus(first + i);
        if (a.mem[p] != b.mem[p]) {
            if (diagnose)
                std::printf("    RAM $%08X=%02X/%02X\n",
                            first + i, a.mem[p], b.mem[p]);
            return false;
        }
    }
    return true;
}

bool runDeterministicLoop() {
    SyntheticCpu ref, native;
    installLoop(ref);
    installLoop(native);
    resetCpu(ref);
    resetCpu(native);
    for (int i = 1; i < 7; i++) {
        const uint32_t v = 0x1020'3040u * uint32_t(i) ^ 0xA5A5'5A5Au;
        ref.setD(i, v);
        native.setD(i, v);
    }
    native.jit.setEnabled(true);
    metrics.gate = "jit_asset_free_lockstep_test";
    metrics.workload = "synthetic_68040_lockstep";
    metrics.cpuFamily = "68040";
    metrics.backend = native.jit.backendName();
    metrics.engine = "jit";

    constexpr int kCheckpoints = 768;
    constexpr int64_t kCycleBudget = 97;
    const int64_t startClock = ref.getClock();
    const auto started = std::chrono::steady_clock::now();
    for (int step = 0; step < kCheckpoints; step++) {
        const int64_t target = ref.getClock() + kCycleBudget;
        ref.executeUntil(target);
        native.jit.executeUntil(target);
        if (!sameCpu(ref, native, true) ||
            !sameMemory(ref, native, kData, 512, true) ||
            !sameMemory(ref, native, kStack - 128, 128, true)) {
            std::printf("    divergence at checkpoint %d, target=%lld\n",
                        step, (long long)target);
            return false;
        }
    }
    const auto stopped = std::chrono::steady_clock::now();

    const auto s = native.jit.stats().snapshot();
    std::printf("    backend=%s compiled=%llu runs=%llu native=%llu "
                "interp=%llu slow=%llu\n",
                native.jit.backendName(),
                (unsigned long long)s.blocksCompiled,
                (unsigned long long)s.blocksRun,
                (unsigned long long)s.instrs,
                (unsigned long long)s.interpInstrs,
                (unsigned long long)s.slowInstrs);
    const uint64_t retired = s.instrs + s.interpInstrs;
    const uint64_t nonNative = s.slowInstrs + s.windowInstrs;
    metrics.coreCycles = uint64_t(ref.getClock() - startClock);
    metrics.machineCycles = metrics.coreCycles;
    metrics.wallNs = uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
        stopped - started).count());
    metrics.blocksCompiled = s.blocksCompiled;
    metrics.blocksRun = s.blocksRun;
    metrics.jitInstrs = s.instrs;
    metrics.nativeInstrs = s.instrs > nonNative ? s.instrs - nonNative : 0;
    metrics.interpInstrs = s.interpInstrs;
    metrics.slowInstrs = s.slowInstrs;
    metrics.windowInstrs = s.windowInstrs;
    metrics.nativeSharePermille = retired
        ? metrics.nativeInstrs * 1000 / retired : 0;
    std::printf("    budgets: compiled>=%d runs>=%d native>=%d‰ slow<=%d\n",
                POM68K_PERF_MIN_BLOCKS_COMPILED,
                POM68K_PERF_MIN_BLOCKS_RUN,
                POM68K_PERF_MIN_NATIVE_SHARE_PERMILLE,
                POM68K_PERF_MAX_SLOW_INSTRUCTIONS);
    check(s.blocksCompiled >= POM68K_PERF_MIN_BLOCKS_COMPILED &&
          s.blocksRun >= POM68K_PERF_MIN_BLOCKS_RUN,
          "versioned structural budget retained compiled native execution");
    check(metrics.nativeSharePermille >= POM68K_PERF_MIN_NATIVE_SHARE_PERMILLE,
          "versioned native-share budget rejects broad fallback regressions");
    check(s.slowInstrs <= POM68K_PERF_MAX_SLOW_INSTRUCTIONS,
          "versioned fallback budget remains within policy");
    check(sameMemory(ref, native, 0, kRamBytes, true),
          "complete synthetic RAM image is identical after the loop");
    return true;
}

void prepareFault(SyntheticCpu& c, FaultProgram p) {
    c.setPC(kCode);
    c.setPC0(kCode);
    c.setIRD(p == FaultProgram::LastWrite ? 0x2140 : 0x2020);
    c.setIRC(p == FaultProgram::LastWrite ? 0x0002 : 0x60FC);
    c.setA(0, p == FaultProgram::LastWrite ? kHole - 2 : kHole + 4);
    c.setA(7, kStack);
    c.setD(0, p == FaultProgram::LastWrite ? 0x80010203 : 0x11223344);
    c.setSR(p == FaultProgram::LastWrite ? 0x2700 : 0x271F);
    c.setClock(0);
    c.busFaults = 0;
    clearRunFlags(c);
}

bool runFaultLockstep(FaultProgram p) {
    SyntheticCpu ref, native;
    installFaultLoop(ref, p);
    installFaultLoop(native, p);
    resetCpu(ref);
    resetCpu(native);

    native.setA(0, kData + (p == FaultProgram::LastWrite ? 0 : 4));
    native.setD(0, 0x55667788);
    native.jit.setEnabled(true);
    native.jit.executeUntil(native.getClock() + 512);
    const auto trained = native.jit.stats().snapshot();
    if (trained.blocksCompiled == 0) return false;

    // Training is deliberately native-only. Equalise all observable RAM
    // before injecting the fault while retaining the compiled native block.
    ref.mem = native.mem;
    prepareFault(ref, p);
    prepareFault(native, p);
    const auto before = native.jit.stats().snapshot();
    ref.executeUntil(ref.getClock() + 1);
    native.jit.executeUntil(native.getClock() + 1);
    const auto after = native.jit.stats().snapshot();

    const bool boundary = sameCpu(ref, native, true);
    const bool frame = ref.getA(7) == kStack - 60 &&
        native.getA(7) == ref.getA(7) &&
        sameMemory(ref, native, ref.getA(7), 60, true);
    const bool ram = sameMemory(ref, native, 0, kRamBytes, true);
    const bool nativeEntry = after.blocksRun > before.blocksRun;
    // The exact number of 16-bit bus callbacks used to form one faulting
    // longword is deliberately not architectural. What must agree is the
    // resulting boundary/frame, and both engines must have observed /BERR.
    return boundary && frame && ram && nativeEntry &&
           ref.busFaults != 0 && native.busFaults != 0;
}

}  // namespace

int main() {
    std::printf("jit_asset_free_lockstep_test — deterministic native 68040 proof\n");
    unsetenv("POM68K_JIT_BACKEND");
    setenv("POM68K_JIT_PROFILE", "production", 1);
    setenv("POM68K_JIT_BLOCKS", "1", 1);
    setenv("POM68K_JIT_HOT", "1", 1);
    setenv("POM68K_JIT_ACCESS_THUNK", "2", 1);
    setenv("POM68K_JIT_LINKS", "1", 1);
    setenv("POM68K_JIT_PARANOID", "0", 1);

    SyntheticCpu probe;
    if (!probe.jit.nativeBackend()) {
        metrics.gate = "jit_asset_free_lockstep_test";
        metrics.workload = "synthetic_68040_lockstep";
        metrics.cpuFamily = "68040";
        metrics.backend = probe.jit.backendName();
        metrics.engine = "jit";
        metrics.status = "unavailable";
        if (!jit::emitMetrics(metrics))
            std::fprintf(stderr, "FAIL: cannot write requested metrics artifact\n");
        return unavailableNative(probe);
    }
    std::printf("  host generator: %s\n", probe.jit.backendName());

    check(runDeterministicLoop(),
          "768 deterministic checkpoints match registers, queue, cycles and RAM");
    check(runFaultLockstep(FaultProgram::LastWrite),
          "last-write /BERR boundary and format-$7 frame match the interpreter");
    check(runFaultLockstep(FaultProgram::RestartRead),
          "restart-read /BERR boundary and format-$7 frame match the interpreter");

    metrics.status = failures ? "fail" : "pass";
    check(jit::emitMetrics(metrics), "structured JIT metrics artifact is writable");
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
