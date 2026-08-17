// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Native 68040 copyback-write gate. A misaligned MOVE.L is first trained
// against a copyback DTT region, then one selected generated hit must update
// the resident bytes, publish both covered dirty-longword bits, and leave
// backing RAM stale. The same cached block is finally aimed at a /BERR hole
// and compared with the interpreter's format-$7 last-write frame. A faulting
// predecrement read covers the opposite (restart) half of that dichotomy.
// Additional registrations cover the native-write-disabled control, BSR's
// hot return-address push, and a two-line read+write MOVE whose second proof
// can fail only by replaying the complete untouched instruction.

#include "Moira.h"
#include "jit/JitEngine.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

constexpr uint32_t kCode = 0x001100;
constexpr uint32_t kHandler = 0x003100;
constexpr uint32_t kStack = 0x008000;
constexpr uint32_t kAbs = 0x004000;
constexpr uint32_t kData = 0x40004000;
constexpr uint32_t kHole = 0x00F00000;

class GateCpu final : public moira::Moira {
public:
    GateCpu()
        : mem(1u << 24, 0), jit(*this, hooks(this), jit::kGuest68040) {
        setModel(moira::Model::M68040);
    }

    std::vector<uint8_t> mem;
    jit::Engine jit;
    jit::CodeGuard* guard = nullptr;
    uint64_t mappedWrites = 0;
    int faults = 0;

    static uint32_t bus(uint32_t a) { return a & 0x00FFFFFFu; }
    static bool inHole(uint32_t a) {
        const uint32_t p = bus(a);
        return p >= kHole && p < kHole + 0x10000;
    }

private:
    static jit::MemoryHooks hooks(GateCpu* cpu) {
        jit::MemoryHooks h;
        h.self = cpu;
        h.codeSpan = [](void* s, uint32_t p, uint32_t& len) -> const uint8_t* {
            auto& c = *static_cast<GateCpu*>(s);
            p = bus(p);
            if (inHole(p)) return nullptr;
            len = uint32_t(c.mem.size()) - p;
            return c.mem.data() + p;
        };
        h.dataSpan = [](void* s, uint32_t p, uint32_t& len, int) -> uint8_t* {
            auto& c = *static_cast<GateCpu*>(s);
            p = bus(p);
            if (inHole(p)) return nullptr;
            len = uint32_t(c.mem.size()) - p;
            return c.mem.data() + p;
        };
        h.setGuard = [](void* s, jit::CodeGuard* g) {
            static_cast<GateCpu*>(s)->guard = g;
        };
        h.ramBytes = [](void* s) {
            return uint32_t(static_cast<GateCpu*>(s)->mem.size());
        };
        return h;
    }

    moira::u8 read8(moira::u32 a) const override {
        if (inHole(a)) {
            auto& c = *const_cast<GateCpu*>(this);
            c.faults++;
            c.extBusError040();
        }
        return mem[bus(a)];
    }
    moira::u16 read16(moira::u32 a) const override {
        if (inHole(a)) {
            auto& c = *const_cast<GateCpu*>(this);
            c.faults++;
            c.extBusError040();
        }
        const uint32_t p = bus(a);
        return moira::u16(mem[p] << 8 | mem[bus(p + 1)]);
    }
    void write8(moira::u32 a, moira::u8 v) const override {
        auto& c = *const_cast<GateCpu*>(this);
        if (inHole(a)) {
            c.faults++;
            c.extBusError040();
        }
        const uint32_t p = bus(a);
        c.mappedWrites++;
        if (c.guard) c.guard->note(p, 1);
        c.mem[p] = v;
    }
    void write16(moira::u32 a, moira::u16 v) const override {
        auto& c = *const_cast<GateCpu*>(this);
        if (inHole(a)) {
            c.faults++;
            c.extBusError040();
        }
        const uint32_t p = bus(a);
        c.mappedWrites++;
        if (c.guard) c.guard->note(p, 2);
        c.mem[p] = moira::u8(v >> 8);
        c.mem[bus(p + 1)] = moira::u8(v);
    }
};

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %-70s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

int unavailableNative(const GateCpu& cpu) {
    std::printf("%s: native backend unavailable (%s)\n",
                std::getenv("POM68K_JIT_REQUIRE_NATIVE") ? "FAIL" : "SKIP",
                cpu.jit.backendName());
    return std::getenv("POM68K_JIT_REQUIRE_NATIVE") ? 1 : 0;
}

void put16(GateCpu& c, uint32_t a, uint16_t v) {
    const uint32_t p = GateCpu::bus(a);
    c.mem[p] = uint8_t(v >> 8);
    c.mem[GateCpu::bus(p + 1)] = uint8_t(v);
}

void put32(GateCpu& c, uint32_t a, uint32_t v) {
    put16(c, a, uint16_t(v >> 16));
    put16(c, a + 2, uint16_t(v));
}

uint16_t get16(const GateCpu& c, uint32_t a) {
    const uint32_t p = GateCpu::bus(a);
    return uint16_t(c.mem[p] << 8 | c.mem[GateCpu::bus(p + 1)]);
}

uint32_t get32(const GateCpu& c, uint32_t a) {
    return uint32_t(get16(c, a)) << 16 | get16(c, a + 2);
}

enum class Program { Write, RestartRead, Bsr, PairMove };

void install(GateCpu& c, Program p) {
    put32(c, 0, kStack);
    put32(c, 4, kCode);
    put32(c, 8, kHandler);
    if (p == Program::Write) {
        put16(c, kCode + 0, 0x2140); // MOVE.L D0,2(A0)
        put16(c, kCode + 2, 0x0002);
        put16(c, kCode + 4, 0x60FA); // BRA.S kCode
        put16(c, kCode + 6, 0x4E71);
    } else if (p == Program::RestartRead) {
        put16(c, kCode + 0, 0x2020); // MOVE.L -(A0),D0
        put16(c, kCode + 2, 0x60FC); // BRA.S kCode
        put16(c, kCode + 4, 0x4E71);
    } else if (p == Program::Bsr) {
        put16(c, kCode + 0, 0x6106); // BSR.S kCode+8
        put16(c, kCode + 2, 0x60FC); // BRA.S kCode
        put16(c, kCode + 4, 0x4E71); // padding
        put16(c, kCode + 6, 0x4E71);
        put16(c, kCode + 8, 0x4E75); // RTS
    } else {
        put16(c, kCode + 0, 0x2F38); // MOVE.L $4000.W,-(A7)
        put16(c, kCode + 2, uint16_t(kAbs));
        put16(c, kCode + 4, 0x588F); // ADDQ.L #4,A7
        put16(c, kCode + 6, 0x60F8); // BRA.S kCode
    }
    put16(c, kHandler, 0x4E71);
    put16(c, kHandler + 2, 0x60FC);
}

void configure(GateCpu& c, Program p) {
    install(c, p);
    c.reset();
    c.setTC(0);                         // identity translation
    c.setDTT0(0x4000C020);              // $40xxxxxx, S/U, copyback
    if (p == Program::PairMove)
        c.setDTT1(0x0000C020);          // $00xxxxxx, S/U, copyback
    c.setPomCache040(true);
    c.setPomCache040Timing({0, 0, 2});   // native hits owe no sync()
    c.setCACR(0x80008000);               // instruction + data cache
}

void alignAtCode(GateCpu& c) {
    for (int i = 0; c.getPC() != kCode && i < 16; i++)
        c.jit.executeUntil(c.getClock() + 1);
}

void clearRunFlags(GateCpu& c) {
    const auto layout = c.pomJitLayout();
    *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(&c) + layout.flags) = 0;
}

void prepareFault(GateCpu& c, Program p) {
    c.setPC(kCode); c.setPC0(kCode);
    c.setIRD(p == Program::Write ? 0x2140 : 0x2020);
    c.setIRC(p == Program::Write ? 0x0002 : 0x60FC);
    c.setA(0, p == Program::Write ? kHole - 2 : kHole + 4);
    c.setA(7, kStack);
    c.setD(0, p == Program::Write ? 0x80010203 : 0x11223344);
    c.setSR(p == Program::Write ? 0x2700 : 0x271Fu);
    c.setClock(0);
    c.faults = 0;
    clearRunFlags(c);
}

bool sameBoundary(const GateCpu& a, const GateCpu& b) {
    for (int i = 0; i < 8; i++)
        if (a.getD(i) != b.getD(i) || a.getA(i) != b.getA(i)) return false;
    return a.getPC() == b.getPC() && a.getSR() == b.getSR() &&
           a.getClock() == b.getClock();
}

struct TimedRun {
    int64_t ns = 0;
    uint64_t blocks = 0;
    uint64_t slow = 0;
};

TimedRun timeWriteLoop(bool useJit) {
    GateCpu cpu;
    configure(cpu, Program::Write);
    cpu.setA(0, kData);
    cpu.setD(0, 0x12345678);
    if (useJit) cpu.jit.setEnabled(true);

    // Warm reset/ATC/cache state and, for the native leg, compile the loop.
    constexpr int64_t kWarmCycles = 20000;
    if (useJit) cpu.jit.executeUntil(cpu.getClock() + kWarmCycles);
    else cpu.executeUntil(cpu.getClock() + kWarmCycles);
    const auto before = cpu.jit.stats().snapshot();

    constexpr int64_t kMeasuredCycles = 5000000;
    const auto t0 = std::chrono::steady_clock::now();
    if (useJit) cpu.jit.executeUntil(cpu.getClock() + kMeasuredCycles);
    else cpu.executeUntil(cpu.getClock() + kMeasuredCycles);
    const auto t1 = std::chrono::steady_clock::now();
    const auto after = cpu.jit.stats().snapshot();
    TimedRun r;
    r.ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    r.blocks = after.blocksRun - before.blocksRun;
    r.slow = after.slowInstrs - before.slowInstrs;
    return r;
}

void checkPerformanceBudget() {
    // Same guest-cycle budget, same two-instruction loop, same process. The
    // generous 2x ceiling is a regression tripwire, not a benchmark claim;
    // it tolerates shared CI noise while catching a disabled native path or
    // a proof that unexpectedly falls back on every access.
    const TimedRun interp = timeWriteLoop(false);
    const TimedRun native = timeWriteLoop(true);
    const double ratio = interp.ns > 0 ? double(native.ns) / double(interp.ns) : 99.0;
    std::printf("  asset-free perf budget: interp=%.3f ms native=%.3f ms "
                "ratio=%.3f blocks=%llu slow=%llu\n",
                double(interp.ns) / 1.0e6, double(native.ns) / 1.0e6, ratio,
                (unsigned long long)native.blocks,
                (unsigned long long)native.slow);
    check(native.blocks != 0, "performance budget executed cached native blocks");
    check(native.slow <= POM68K_PERF_COPYBACK_MAX_SLOW_INSTRUCTIONS,
          "performance budget stayed within the versioned fallback ceiling");
    const int64_t allowed =
        interp.ns * POM68K_PERF_COPYBACK_MAX_RATIO_PERMILLE / 1000 +
        int64_t(POM68K_PERF_COPYBACK_SLACK_US) * 1000;
    check(native.ns <= allowed,
          "native fixed-cycle loop stays within the versioned host budget");
}

int runBsrGate() {
    std::printf("jit_copyback_bsr_040_test — native return-address push gate\n");
    GateCpu native;
    configure(native, Program::Bsr);
    if (!native.jit.nativeBackend()) {
        return unavailableNative(native);
    }

    native.setSR(0x0710);               // user stack; ISP stays at kStack
    native.setA(7, kData + 8);
    native.jit.setEnabled(true);
    native.jit.executeUntil(native.getClock() + 512);
    check(native.jit.stats().snapshot().blocksCompiled != 0,
          "BSR/RTS copyback loop compiled");
    alignAtCode(native);
    check(native.getPC() == kCode, "trained block aligned at selected BSR");

    check(native.pomJitWriteData(kData + 4, 4, 0),
          "exact bootstrap published the stack's write-authorized line");
    auto* line = native.pomCache040Data().lookup(kData + 4);
    check(line != nullptr, "bootstrap left the copyback line resident");
    if (line) {
        std::memset(line->data, 0x5A, sizeof(line->data));
        line->dirty = 0;
        std::memset(native.mem.data() + GateCpu::bus(kData), 0x5A, 16);
    }
    native.setA(7, kData + 8);
    clearRunFlags(native);
    const uint64_t hitsBefore = native.pomJitCache040NativeWrites();
    const uint64_t mappedBefore = native.mappedWrites;
    native.jit.executeUntil(native.getClock() + 1);
    check(native.pomJitCache040NativeWrites() == hitsBefore + 1,
          "selected BSR executed one native copyback stack push");
    check(line && line->dirty == 0x02,
          "BSR publishes exactly the pushed return-address longword");
    check(line && (uint32_t(line->data[4]) << 24 |
                   uint32_t(line->data[5]) << 16 |
                   uint32_t(line->data[6]) << 8 | line->data[7]) == kCode + 2,
          "BSR stored the big-endian return address in the cache line");
    check(get32(native, kData + 4) == 0x5A5A5A5A,
          "native BSR push left copyback backing RAM stale");
    check(native.mappedWrites == mappedBefore,
          "native BSR push bypassed external memory-map write callbacks");
    check(native.getA(7) == kData + 4 && native.getPC() == kCode + 8,
          "native BSR committed A7 and the branch target after the push");

    // A W-table miss must occur before A7 or the branch boundary changes.
    // Aim the already compiled push at /BERR and compare the complete frame.
    GateCpu ref;
    configure(ref, Program::Bsr);
    for (GateCpu* c : {&ref, &native}) {
        c->setPC(kCode); c->setPC0(kCode);
        c->setIRD(0x6106); c->setIRC(0x60FC);
        c->setSR(0x071F);               // fault switches to the valid ISP
        c->setA(7, kHole + 4);
        c->setClock(0); c->faults = 0; clearRunFlags(*c);
    }
    const auto beforeFault = native.jit.stats().snapshot();
    ref.executeUntil(ref.getClock() + 1);
    native.jit.executeUntil(native.getClock() + 1);
    const auto afterFault = native.jit.stats().snapshot();
    const uint32_t refSp = ref.getA(7), nativeSp = native.getA(7);
    check(afterFault.blocksRun > beforeFault.blocksRun,
          "faulting BSR entered the already-compiled native block");
    check(refSp == kStack - 60 && nativeSp == refSp &&
          std::memcmp(ref.mem.data() + refSp,
                      native.mem.data() + nativeSp, 60) == 0,
          "BSR cache miss and interpreter fault frames are byte-identical");
    check(sameBoundary(ref, native),
          "faulting BSR leaves identical architectural boundary state");

    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}

int runPairMoveGate(bool pairEnabled) {
    std::printf("jit_copyback_pair_040_test — dual-line MOVE proof gate (%s)\n",
                pairEnabled ? "native pair" : "exact-pair control");
    GateCpu native;
    configure(native, Program::PairMove);
    if (!native.jit.nativeBackend()) {
        return unavailableNative(native);
    }

    constexpr uint32_t value = 0x81223344;
    native.setSR(0x0710);               // user stack; ISP stays at kStack
    native.setA(7, kData + 8);
    put32(native, kAbs, value);
    native.jit.setEnabled(true);
    native.jit.executeUntil(native.getClock() + 512);
    check(native.jit.stats().snapshot().blocksCompiled != 0,
          "abs.W-to-stack MOVE loop compiled");
    alignAtCode(native);
    check(native.getPC() == kCode, "trained block aligned at selected MOVE");

    uint32_t ignored = 0;
    check(native.pomJitWriteData(kData + 4, 4, 0),
          "exact bootstrap published the destination write line");
    // The first write-side translation may set the descriptor M bit and
    // advance the shared DATA-ATC epoch. Publish R after W so both entries
    // deliberately carry the same final generation.
    check(native.pomJitReadData(kAbs, 4, ignored) && ignored == value,
          "exact bootstrap published the source read line");
    auto* srcLine = native.pomCache040Data().lookup(kAbs);
    auto* dstLine = native.pomCache040Data().lookup(kData + 4);
    check(srcLine && dstLine && srcLine != dstLine,
          "both physical cache lines are resident independently");
    if (srcLine) {
        srcLine->data[0] = uint8_t(value >> 24);
        srcLine->data[1] = uint8_t(value >> 16);
        srcLine->data[2] = uint8_t(value >> 8);
        srcLine->data[3] = uint8_t(value);
    }
    if (dstLine) {
        std::memset(dstLine->data, 0x5A, sizeof(dstLine->data));
        dstLine->dirty = 0;
        std::memset(native.mem.data() + GateCpu::bus(kData), 0x5A, 16);
    }
    native.setA(7, kData + 8);
    native.setSR(0x0710);               // X survives MOVE's flag update
    clearRunFlags(native);
    const uint64_t readsBefore = native.pomJitCache040NativeReads();
    const uint64_t writesBefore = native.pomJitCache040NativeWrites();
    const uint64_t mappedBefore = native.mappedWrites;
    const auto pairBefore = native.jit.stats().snapshot();
    native.jit.executeUntil(native.getClock() + 1);
    const auto pairAfter = native.jit.stats().snapshot();
    if (native.pomJitCache040NativeReads() != readsBefore + 1 ||
        native.pomJitCache040NativeWrites() != writesBefore + 1)
        std::printf("    pair hits: R=%llu->%llu W=%llu->%llu\n",
                    (unsigned long long)readsBefore,
                    (unsigned long long)native.pomJitCache040NativeReads(),
                    (unsigned long long)writesBefore,
                    (unsigned long long)native.pomJitCache040NativeWrites());
    if (pairAfter.slowInstrs != pairBefore.slowInstrs)
        std::printf("    pair fallback: %llu->%llu\n",
                    (unsigned long long)pairBefore.slowInstrs,
                    (unsigned long long)pairAfter.slowInstrs);
    check(native.pomJitCache040NativeReads() ==
              readsBefore + (pairEnabled ? 1u : 0u) &&
          native.pomJitCache040NativeWrites() ==
              writesBefore + (pairEnabled ? 1u : 0u),
          pairEnabled
              ? "selected MOVE committed one native R+W line pair"
              : "disabled pair used the exact path with no native hits");
    check(dstLine && dstLine->dirty == 0x02,
          "paired MOVE dirtied exactly the pushed destination longword");
    if (dstLine) {
        const uint32_t actual = uint32_t(dstLine->data[4]) << 24 |
                                uint32_t(dstLine->data[5]) << 16 |
                                uint32_t(dstLine->data[6]) << 8 |
                                dstLine->data[7];
        if (actual != value)
            std::printf("    pair value: expected=%08X actual=%08X SR=%04X\n",
                        value, actual, native.getSR());
    }
    check(dstLine && (uint32_t(dstLine->data[4]) << 24 |
                      uint32_t(dstLine->data[5]) << 16 |
                      uint32_t(dstLine->data[6]) << 8 | dstLine->data[7]) == value,
          "paired MOVE copied the big-endian longword between cache lines");
    check(get32(native, kData + 4) == 0x5A5A5A5A,
          "paired copyback write left destination RAM stale");
    check(native.mappedWrites == mappedBefore,
          "paired native MOVE bypassed external write callbacks");
    check(native.getA(7) == kData + 4 && (native.getSR() & 0x1F) == 0x18,
          "paired MOVE committed predecrement A7 and exact logical flags");

    // The second proof must miss before the first read becomes observable.
    // Keep the source line hot, redirect only -(A7) to /BERR, and compare
    // the complete fault frame with an interpreter executing the same MOVE.
    GateCpu ref;
    configure(ref, Program::PairMove);
    put32(ref, kAbs, value); put32(native, kAbs, value);
    for (GateCpu* c : {&ref, &native}) {
        c->setPC(kCode); c->setPC0(kCode);
        c->setIRD(0x2F38); c->setIRC(uint16_t(kAbs));
        c->setSR(0x071F); c->setA(7, kHole + 4);
        c->setClock(0); c->faults = 0; clearRunFlags(*c);
    }
    const auto beforeFault = native.jit.stats().snapshot();
    ref.executeUntil(ref.getClock() + 1);
    native.jit.executeUntil(native.getClock() + 1);
    const auto afterFault = native.jit.stats().snapshot();
    const uint32_t refSp = ref.getA(7), nativeSp = native.getA(7);
    check(afterFault.blocksRun > beforeFault.blocksRun,
          "second-line fault entered the already-compiled native block");
    check(refSp == kStack - 60 && nativeSp == refSp &&
          std::memcmp(ref.mem.data() + refSp,
                      native.mem.data() + nativeSp, 60) == 0,
          "second-line miss and interpreter fault frames are byte-identical");
    check(sameBoundary(ref, native),
          "faulting paired MOVE leaves identical architectural state");

    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}

} // namespace

int main(int argc, char** argv) {
    const bool bsrOnly = argc >= 2 && std::strcmp(argv[1], "--bsr") == 0;
    const bool pairOnly = argc >= 2 && std::strcmp(argv[1], "--pair") == 0;
    const bool pairControl =
        argc >= 2 && std::strcmp(argv[1], "--pair-disabled") == 0;
    const bool writeHitEnabled =
        argc < 2 || std::strcmp(argv[1], "--disabled") != 0;
    unsetenv("POM68K_JIT_BACKEND");
    setenv("POM68K_JIT_BLOCKS", "1", 1);
    setenv("POM68K_JIT_HOT", "1", 1);
    setenv("POM68K_JIT_ACCESS_THUNK", "2", 1);
    setenv("POM68K_JIT_040_LINE_READ", "1", 1);
    setenv("POM68K_JIT_040_LINE_WRITE", writeHitEnabled ? "1" : "0", 1);
    setenv("POM68K_JIT_040_LINE_PAIR", pairControl ? "0" : "1", 1);
    setenv("POM68K_JIT_040_LINE_STATS", "1", 1);

    if (bsrOnly) return runBsrGate();
    if (pairOnly || pairControl) return runPairMoveGate(!pairControl);

    std::printf("jit_copyback_write_040_test — dirty longword + format-$7 gate (%s)\n",
                writeHitEnabled ? "native write hit" : "exact-write control");

    GateCpu native;
    configure(native, Program::Write);
    if (!native.jit.nativeBackend()) {
        return unavailableNative(native);
    }

    native.setA(0, kData);
    native.setD(0, 0x11223344);
    native.jit.setEnabled(true);
    native.jit.executeUntil(native.getClock() + 512);
    const auto trained = native.jit.stats().snapshot();
    check(trained.blocksCompiled != 0, "copyback MOVE loop compiled");
    alignAtCode(native);
    check(native.getPC() == kCode, "trained block aligned at selected MOVE");

    // Code-slice admission may bump the shared DATA-ATC epoch after the
    // trace that first dirtied this line. Re-publish once at the selected
    // boundary so this assertion and the following execution isolate the
    // generated hit rather than the one-time block-admission invalidation.
    check(native.pomJitWriteData(kData + 2, 4, 0x11223344),
          "exact bootstrap write completed before the selected native hit");

    auto* line = native.pomCache040Data().lookup(kData + 2);
    const auto& writeEntry = native.pomJitCache040W.e[
        ((kData + 2) >> 4) & (moira::Moira::PomJitCache040Table::kEntries - 1)];
    if (!(line && writeEntry.line == line &&
          writeEntry.generation == native.pomJitCache040Gen))
        std::printf("    write entry: tag=%08X gen=%u/%u line=%p/%p dirty=%02X\n",
                    writeEntry.tag, writeEntry.generation,
                    native.pomJitCache040Gen, static_cast<void*>(writeEntry.line),
                    static_cast<void*>(line), line ? line->dirty : 0);
    check(line && writeEntry.line == line &&
          writeEntry.generation == native.pomJitCache040Gen,
          "exact bootstrap published a write-authorized copyback line");

    if (line) {
        std::memset(line->data, 0x5A, sizeof(line->data));
        line->dirty = 0;
        std::memset(native.mem.data() + GateCpu::bus(kData), 0x5A, 16);
    }
    native.setD(0, 0xA1B2C3D4);
    const uint64_t hitsBefore = native.pomJitCache040NativeWrites();
    const uint64_t mappedBefore = native.mappedWrites;
    native.jit.executeUntil(native.getClock() + 1);
    check(native.pomJitCache040NativeWrites() ==
              hitsBefore + (writeHitEnabled ? 1u : 0u),
          writeHitEnabled
              ? "selected MOVE executed one native copyback write hit"
              : "disabled gate executed no native copyback write hit");
    check(line && line->dirty == 0x03,
          "misaligned long publishes exactly dirty longwords 0 and 1");
    check(line && line->data[2] == 0xA1 && line->data[3] == 0xB2 &&
          line->data[4] == 0xC3 && line->data[5] == 0xD4,
          writeHitEnabled
              ? "native hit stored the four big-endian bytes in the cache line"
              : "exact control stored the four big-endian bytes in the cache line");
    check(get32(native, kData + 2) == 0x5A5A5A5A,
          writeHitEnabled
              ? "copyback hit left backing RAM stale"
              : "exact copyback control left backing RAM stale");
    check(native.mappedWrites == mappedBefore,
          writeHitEnabled
              ? "native hit bypassed the external memory-map write callbacks"
              : "exact cache hit bypassed the external memory-map callbacks");

    GateCpu writeRef;
    configure(writeRef, Program::Write);
    prepareFault(writeRef, Program::Write);
    prepareFault(native, Program::Write);
    const auto beforeFault = native.jit.stats().snapshot();
    writeRef.executeUntil(writeRef.getClock() + 1);
    native.jit.executeUntil(native.getClock() + 1);
    const auto afterFault = native.jit.stats().snapshot();
    const uint32_t refSp = writeRef.getA(7), nativeSp = native.getA(7);
    check(afterFault.blocksRun > beforeFault.blocksRun,
          "last-write fault entered the already-compiled native block");
    check(refSp == kStack - 60 && nativeSp == refSp,
          "both engines stacked one 60-byte format-$7 frame");
    check(std::memcmp(writeRef.mem.data() + refSp,
                      native.mem.data() + nativeSp, 60) == 0,
          "native-miss and interpreter last-write frames are byte-identical");
    check(get16(native, nativeSp + 0) == 0x2708 &&
          get32(native, nativeSp + 2) == kCode + 4 &&
          get16(native, nativeSp + 6) == 0x7008,
          "last-write frame keeps final CCR and stacks the next PC");
    check(get32(native, nativeSp + 28) == 0x80010203,
          "format-$7 WB3D carries the pending longword");
    check(sameBoundary(writeRef, native),
          "last-write fault leaves identical architectural boundary state");

    GateCpu restartRef, restartNative;
    configure(restartRef, Program::RestartRead);
    configure(restartNative, Program::RestartRead);
    restartNative.setA(0, kData + 0x1000);
    restartNative.jit.setEnabled(true);
    restartNative.jit.executeUntil(restartNative.getClock() + 512);
    const auto restartTrained = restartNative.jit.stats().snapshot();
    check(restartTrained.blocksCompiled != 0, "restart-read loop compiled");
    prepareFault(restartRef, Program::RestartRead);
    prepareFault(restartNative, Program::RestartRead);
    restartRef.executeUntil(restartRef.getClock() + 1);
    restartNative.jit.executeUntil(restartNative.getClock() + 1);
    const uint32_t restartRefSp = restartRef.getA(7);
    const uint32_t restartNativeSp = restartNative.getA(7);
    check(restartRefSp == kStack - 60 && restartNativeSp == restartRefSp &&
          std::memcmp(restartRef.mem.data() + restartRefSp,
                      restartNative.mem.data() + restartNativeSp, 60) == 0,
          "restart-side format-$7 frames are byte-identical");
    check(get32(restartNative, restartNativeSp + 2) == kCode &&
          get16(restartNative, restartNativeSp + 0) == 0x271F &&
          restartNative.getA(0) == kHole + 4,
          "restart frame restores CCR, PC and predecrement A0");
    check(sameBoundary(restartRef, restartNative),
          "restart fault leaves identical architectural boundary state");

    if (writeHitEnabled) checkPerformanceBudget();

    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
