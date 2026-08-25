// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Native-backend 68030 restartable-write gate (a64, and x64 since
// 2026-08-18 — the declaration made the generators reachable without the
// unsafe override, and this oracle now judges whichever native backend the
// host carries). MOVE.B destinations d16(A6), -(A6),
// brief indexed and (A6)+ are trained against RAM, then replayed into a /BERR
// hole. The successful (A6)+ path is additionally checked against complete
// RAM and against the CPU state observable from an exact MMIO callback.

#include "Moira.h"
#include "jit/JitEngine.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

constexpr uint32_t kHole = 0xF00000;
constexpr uint32_t kMmio = 0xE00000;
constexpr uint32_t kCode = 0x001100;
constexpr uint32_t kHandler = 0x003100;
constexpr uint32_t kStack = 0x008000;
constexpr uint32_t kSubroutine = 0x006000;

class FaultCpu final : public moira::Moira {
public:
    struct WriteObservation {
        int count = 0;
        uint32_t address = 0;
        uint32_t value = 0;
        unsigned bytes = 0;
        uint32_t a6 = 0;
        uint32_t pc = 0;
        uint32_t pc0 = 0;
        uint16_t ird = 0;
        uint16_t irc = 0;
        uint16_t sr = 0;
        int64_t clock = 0;
    };

    FaultCpu()
        : mem(1u << 24, 0), jit(*this, hooks(this), jit::kGuest68030) {
        setModel(moira::Model::M68030);
    }

    std::vector<uint8_t> mem;
    // The engine attaches its production write guard during construction.
    // Construct the callback destination first so its initializer cannot
    // erase that attachment afterwards.
    jit::CodeGuard* guard = nullptr;
    jit::Engine jit;
    int writeFaults = 0;
    WriteObservation observedWrite;

    bool inHole(uint32_t a) const {
        return a >= kHole && a < kHole + 0x10000;
    }

    bool inMmio(uint32_t a) const {
        return a >= kMmio && a < kMmio + 0x10000;
    }

    void observeWrite(uint32_t a, uint32_t v, unsigned bytes) {
        observedWrite.count++;
        observedWrite.address = a & 0xFFFFFF;
        observedWrite.value = v;
        observedWrite.bytes = bytes;
        observedWrite.a6 = getA(6);
        observedWrite.pc = getPC();
        observedWrite.pc0 = getPC0();
        observedWrite.ird = getIRD();
        observedWrite.irc = getIRC();
        observedWrite.sr = getSR();
        observedWrite.clock = getClock();
    }

private:
    static jit::MemoryHooks hooks(FaultCpu* cpu) {
        jit::MemoryHooks h;
        h.self = cpu;
        h.codeSpan = [](void* s, uint32_t p, uint32_t& len) -> const uint8_t* {
            auto& c = *static_cast<FaultCpu*>(s);
            p &= 0xFFFFFF;
            if (c.inHole(p) || c.inMmio(p)) return nullptr;
            len = uint32_t(c.mem.size()) - p;
            return c.mem.data() + p;
        };
        h.dataSpan = [](void* s, uint32_t p, uint32_t& len, int) -> uint8_t* {
            auto& c = *static_cast<FaultCpu*>(s);
            p &= 0xFFFFFF;
            // dataSpan is the JIT's plain-memory proof. MMIO must take the
            // exact map callback even though this synthetic fixture keeps a
            // backing byte vector for convenient full-image comparisons.
            if (c.inHole(p) || c.inMmio(p)) return nullptr;
            len = uint32_t(c.mem.size()) - p;
            return c.mem.data() + p;
        };
        h.setGuard = [](void* s, jit::CodeGuard* g) {
            static_cast<FaultCpu*>(s)->guard = g;
        };
        h.ramBytes = [](void* s) {
            return uint32_t(static_cast<FaultCpu*>(s)->mem.size());
        };
        return h;
    }

    moira::u8 read8(moira::u32 a) const override {
        if (inHole(a)) const_cast<FaultCpu*>(this)->extBusError();
        return mem[a & 0xFFFFFF];
    }
    moira::u16 read16(moira::u32 a) const override {
        if (inHole(a)) const_cast<FaultCpu*>(this)->extBusError();
        return moira::u16(mem[a & 0xFFFFFF] << 8 |
                          mem[(a + 1) & 0xFFFFFF]);
    }
    void write8(moira::u32 a, moira::u8 v) const override {
        auto& c = *const_cast<FaultCpu*>(this);
        if (inHole(a)) {
            c.writeFaults++;
            c.extBusError();
        }
        if (inMmio(a)) {
            c.observeWrite(a, v, 1);
            return;
        }
        const uint32_t p = a & 0xFFFFFF;
        if (c.guard) c.guard->note(p, 1);
        c.mem[p] = v;
    }
    void write16(moira::u32 a, moira::u16 v) const override {
        auto& c = *const_cast<FaultCpu*>(this);
        if (inHole(a)) {
            c.writeFaults++;
            c.extBusError();
        }
        if (inMmio(a)) {
            c.observeWrite(a, v, 2);
            return;
        }
        const uint32_t p = a & 0xFFFFFF;
        if (c.guard) c.guard->note(p, 2);
        c.mem[p] = moira::u8(v >> 8);
        c.mem[(p + 1) & 0xFFFFFF] = moira::u8(v);
    }
};

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %-66s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

void put16(FaultCpu& c, uint32_t a, uint16_t v) {
    c.mem[a] = uint8_t(v >> 8); c.mem[a + 1] = uint8_t(v);
}

void put32(FaultCpu& c, uint32_t a, uint32_t v) {
    put16(c, a, uint16_t(v >> 16)); put16(c, a + 2, uint16_t(v));
}

uint16_t get16(const FaultCpu& c, uint32_t a) {
    return uint16_t(c.mem[a] << 8 | c.mem[a + 1]);
}

uint32_t get32(const FaultCpu& c, uint32_t a) {
    return uint32_t(get16(c, a)) << 16 | get16(c, a + 2);
}

void install(FaultCpu& c) {
    put32(c, 0, kStack);
    put32(c, 4, kCode);
    put32(c, 8, kHandler);
    put16(c, kCode + 0, 0x1D40);        // MOVE.B D0,d16(A6)
    put16(c, kCode + 2, 0x0000);
    put16(c, kCode + 4, 0x60FA);        // BRA.S kCode (training loop)
    put16(c, kHandler, 0x4E71);         // harmless common post-vector boundary
}

void installQueueLoop(FaultCpu& c, bool jumpAbsoluteLong) {
    put32(c, 0, kStack);
    put32(c, 4, kCode);
    put32(c, 8, kHandler);
    if (jumpAbsoluteLong) {
        put16(c, kCode + 0, 0x4EF9);    // JMP (xxx).L kCode
        put32(c, kCode + 2, kCode);
    } else {
        put16(c, kCode + 0, 0x60FF);    // BRA.L kCode, displacement -2
        put32(c, kCode + 2, 0xFFFFFFFE);
    }
    put16(c, kCode + 6, 0x4E71);        // copied lookahead, never executed
    put16(c, kHandler, 0x4E71);
}

void installWriteLoop(FaultCpu& c, uint16_t opcode, uint16_t extension,
                      bool hasExtension) {
    put32(c, 0, kStack);
    put32(c, 4, kCode);
    put32(c, 8, kHandler);
    put16(c, kCode, opcode);
    const uint32_t branch = kCode + (hasExtension ? 4 : 2);
    if (hasExtension) put16(c, kCode + 2, extension);
    put16(c, branch, hasExtension ? 0x60FA : 0x60FC);
    put16(c, branch + 2, 0x4E71);
    put16(c, kHandler, 0x4E71);
}

void installPeaLoop(FaultCpu& c) {
    put32(c, 0, kStack);
    put32(c, 4, kCode);
    put32(c, 8, kHandler);
    put16(c, kCode + 0, 0x486E);        // PEA 4(A6)
    put16(c, kCode + 2, 0x0004);
    put16(c, kCode + 4, 0x588F);        // ADDQ.L #4,A7
    put16(c, kCode + 6, 0x60F8);        // BRA.S kCode
    put16(c, kCode + 8, 0x4E71);
    put16(c, kHandler, 0x4E71);
}

void installSccRegisterLoop(FaultCpu& c) {
    put32(c, 0, kStack);
    put32(c, 4, kCode);
    put32(c, 8, kHandler);
    put16(c, kCode + 0, 0x50C0);        // ST D0
    put16(c, kCode + 2, 0x60FC);        // BRA.S kCode
    put16(c, kCode + 4, 0x4E71);
    put16(c, kHandler, 0x4E71);
}

void installIndexedReadLoop(FaultCpu& c) {
    put32(c, 0, kStack);
    put32(c, 4, kCode);
    put32(c, 8, kHandler);
    put16(c, kCode + 0, 0x2430);        // MOVE.L 4(A0,D1.W*2),D2
    put16(c, kCode + 2, 0x1204);
    put16(c, kCode + 4, 0x47F0);        // LEA 4(A0,D1.W*2),A3
    put16(c, kCode + 6, 0x1204);
    put16(c, kCode + 8, 0x49FB);        // LEA 8(PC,A1.L*4),A4
    put16(c, kCode + 10, 0x9C08);
    put16(c, kCode + 12, 0x4BF0);       // LEA (32,A0,A1.L*4),A5
    put16(c, kCode + 14, 0x9D30);       // full/direct, long BD
    put32(c, kCode + 16, 0x00000020);
    put16(c, kCode + 20, 0x4DFB);       // LEA (16,ZPC,A1.L*4),A6
    put16(c, kCode + 22, 0x9DA0);       // full/direct, base suppressed
    put16(c, kCode + 24, 0x0010);
    put16(c, kCode + 26, 0x45F0);       // LEA (48,A0,Zn),A2
    put16(c, kCode + 28, 0x1D60);       // full/direct, index suppressed
    put16(c, kCode + 30, 0x0030);
    put16(c, kCode + 32, 0x60DE);       // BRA.S kCode
    put16(c, kHandler, 0x4E71);
    put32(c, 0x006022, 0x01234567);
}

void installJsrLoop(FaultCpu& c) {
    put32(c, 0, kStack);
    put32(c, 4, kCode);
    put32(c, 8, kHandler);
    put16(c, kCode + 0, 0x4EA8);        // JSR d16(A0)
    put16(c, kCode + 2, 0x0000);
    put16(c, kCode + 4, 0x60FA);        // BRA.S kCode
    put16(c, kCode + 6, 0x4E71);
    put16(c, kSubroutine + 0, 0x5280);  // ADDQ.L #1,D0
    put16(c, kSubroutine + 2, 0x4E75);  // RTS
    put16(c, kSubroutine + 4, 0x4E71);
    put16(c, kHandler, 0x4E71);
}

void installMovemLoop(FaultCpu& c) {
    put32(c, 0, kStack);
    put32(c, 4, kCode);
    put32(c, 8, kHandler);
    put16(c, kCode + 0, 0x48E7);        // MOVEM.L D0-D2/A0-A1,-(A7)
    put16(c, kCode + 2, 0xE0C0);        // predecrement-reversed mask
    put16(c, kCode + 4, 0x4CDF);        // MOVEM.L (A7)+,D0-D2/A0-A1
    put16(c, kCode + 6, 0x0307);        // ordinary register mask
    put16(c, kCode + 8, 0x60F6);        // BRA.S kCode
    put16(c, kCode + 10, 0x4E71);
    put16(c, kHandler, 0x4E71);
}

void prepareFault(FaultCpu& c) {
    c.setPC(kCode);
    c.setA(6, kHole);
    c.setA(7, kStack);
    c.setD(0, 0x80);
    c.setSR(0x2700);
    c.setClock(0);
    c.writeFaults = 0;
    c.observedWrite = {};
    const auto layout = c.pomJitLayout();
    *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(&c) +
                                 layout.flags) = 0;
}

}  // namespace

int main() {
    std::printf("jit_restart_write_030_test — injected 68030 last-write /BERR\n");
    // Judge the HOST's native generator: a64 on AArch64, x64 on x86-64 —
    // an explicit POM68K_JIT_BACKEND in the environment wins. No unsafe
    // override since the 68030 declaration (2026-08-18): if selection falls
    // back to `threaded`, that is a real regression and the SKIP below
    // will say so on a host that should have a generator.
#if defined(__aarch64__) || defined(_M_ARM64)
    const char* nativeKey = "a64";
    const char* nativeName = "aarch64";
#else
    const char* nativeKey = "x64";
    const char* nativeName = "x86-64";
#endif
    setenv("POM68K_JIT_BACKEND", nativeKey, /*overwrite=*/0);
    if (const char* b = getenv("POM68K_JIT_BACKEND")) {
        if (!std::strcmp(b, "a64")) nativeName = "aarch64";
        else if (!std::strcmp(b, "x64")) nativeName = "x86-64";
    }
    setenv("POM68K_JIT_BLOCKS", "1", 1);
    setenv("POM68K_JIT_HOT", "1", 1);
    // This is a one-visit synthetic code corpus. Production AArch64/030
    // deliberately waits for score 64 before compiling cold blocks; the
    // restart oracle needs immediate compilation to exercise every thunk.
    setenv("POM68K_JIT_PROFIT_SCORE", "0", 1);
    setenv("POM68K_JIT_ACCESS_THUNK", "2", 1);
    // The queue cases deliberately exercise multiword control flow. Its
    // variable 030 fetch count is conservatively replayed while emitted
    // i-cache accounting is enabled; disabling that attribution layer lets
    // this gate isolate the queue contract (CACR remains disabled, so no
    // architectural cycle cost is removed).
    setenv("POM68K_JIT_ICACHE_EMIT", "0", 1);

    FaultCpu ref, native;
    install(ref); install(native);
    ref.reset(); native.reset();
    // TC.E stays clear (identity translation), but the page-size field still
    // defines the 030 JIT window granularity. Use the normal 4 KiB size.
    ref.setTC(12u << 20);
    native.setTC(12u << 20);

    if (std::strcmp(native.jit.backendName(), nativeName) != 0) {
        std::printf("SKIP: native backend '%s' unavailable (%s)\n",
                    nativeName, native.jit.backendName());
        return 0;
    }

    // The ordinary read half of the brief-index lowering: signed word
    // index, scale and displacement must agree at every 030 queue/cycle
    // boundary and must never enter the per-instruction slow stub.
    {
        FaultCpu readRef, readNative;
        installIndexedReadLoop(readRef); installIndexedReadLoop(readNative);
        readRef.reset(); readNative.reset();
        readRef.setTC(12u << 20); readNative.setTC(12u << 20);
        for (FaultCpu* c : {&readRef, &readNative}) {
            c->setA(0, 0x006020);
            c->setD(1, 0x0000FFFF);
            c->setA(1, 1);
        }
        readNative.jit.setEnabled(true);

        bool same = true;
        for (int step = 0; step < 128 && same; step++) {
            const int64_t target = readRef.getClock() + 37;
            readRef.executeUntil(target);
            readNative.jit.executeUntil(target);
            for (int r = 0; r < 8; r++)
                same = same && readRef.getD(r) == readNative.getD(r) &&
                       readRef.getA(r) == readNative.getA(r);
            same = same && readRef.getPC() == readNative.getPC() &&
                   readRef.getPC0() == readNative.getPC0() &&
                   readRef.getIRD() == readNative.getIRD() &&
                   readRef.getIRC() == readNative.getIRC() &&
                   readRef.getSR() == readNative.getSR() &&
                   readRef.getClock() == readNative.getClock();
            if (!same) std::printf("    indexed-read divergence at checkpoint %d\n",
                                   step);
        }
        const auto readStats = readNative.jit.stats().snapshot();
        check(same && readNative.getD(2) == 0x01234567 &&
              readNative.getA(3) == 0x006022 &&
              readNative.getA(4) == kCode + 0x16 &&
              readNative.getA(5) == 0x006044 &&
              readNative.getA(6) == 0x00000014 &&
              readNative.getA(2) == 0x006050 &&
              readStats.blocksCompiled != 0 && readStats.blocksRun != 0 &&
              readStats.slowInstrs == 0,
              "brief/direct-full indexed LEA stay native and exact on the 030");
    }

    // Train and compile the exact write while its destination is direct RAM.
    native.setA(6, 0x004000);
    native.setD(0, 0x80);
    native.jit.setEnabled(true);
    native.jit.executeUntil(native.getClock() + 256);
    const auto trained = native.jit.stats().snapshot();
    check(trained.blocksCompiled != 0, "the 1D40 block compiled before fault injection");

    prepareFault(ref);
    prepareFault(native);

    ref.executeUntil(ref.getClock() + 1);       // pure interpreter oracle
    native.jit.executeUntil(native.getClock() + 1); // cached native block

    const uint32_t refSp = ref.getA(7);
    const uint32_t jitSp = native.getA(7);
    const auto after = native.jit.stats().snapshot();
    check(ref.writeFaults == 1, "interpreter performed one faulting write");
    check(native.writeFaults == 2,
          "native thunk faulted once, then untouched interpreter replay faulted");
    check(after.blocksRun > trained.blocksRun,
          "fault injection entered the already-compiled native block");
    check(refSp == kStack - 32 && jitSp == refSp,
          "both engines stacked one 32-byte short bus-fault frame");
    check(std::memcmp(ref.mem.data() + refSp, native.mem.data() + jitSp, 32) == 0,
          "all 32 bytes of the restartable format-$A frame are identical");

    check(get16(native, jitSp + 0) == 0x2708,
          "stacked SR keeps MOVE flags from the last-write boundary");
    check(get32(native, jitSp + 2) == kCode + 4,
          "stacked PC is the next instruction");
    check(get16(native, jitSp + 6) == 0xA008,
          "format/vector word is $A008");
    check((get16(native, jitSp + 8) & 0x0100) != 0,
          "internal state carries LASTWRITE");
    check(get16(native, jitSp + 10) == 0x0315,
          "SSW is DF|DF2|byte-write|supervisor-data ($0315)");
    check(get32(native, jitSp + 16) == kHole,
          "data-cycle fault address is exact");
    check(get32(native, jitSp + 20) == 0x00001D40,
          "opcode storage contains 1D40");
    check(get32(native, jitSp + 24) == 0xFFFFFF80,
          "data output buffer contains the sign-extended pending byte");
    check(ref.getPC() == native.getPC() && ref.getPC() >= kHandler,
          "both engines leave vector 2 at the same handler boundary");

    const auto checkQueueLoop = [&](bool jumpAbsoluteLong,
                                    uint16_t expectedIrc,
                                    const char* name) {
        FaultCpu queueRef, queueNative;
        installQueueLoop(queueRef, jumpAbsoluteLong);
        installQueueLoop(queueNative, jumpAbsoluteLong);
        queueRef.reset(); queueNative.reset();
        queueRef.setTC(12u << 20); queueNative.setTC(12u << 20);
        queueNative.jit.setEnabled(true);
        queueNative.jit.executeUntil(queueNative.getClock() + 256);
        const auto trainedQueue = queueNative.jit.stats().snapshot();

        prepareFault(queueRef); prepareFault(queueNative);
        queueRef.setA(6, 0x004000); queueNative.setA(6, 0x004000);
        queueRef.executeUntil(queueRef.getClock() + 1);
        queueNative.jit.executeUntil(queueNative.getClock() + 1);
        const auto afterQueue = queueNative.jit.stats().snapshot();

        char what[160];
        std::snprintf(what, sizeof(what), "%s compiled and entered native code", name);
        check(trainedQueue.blocksCompiled != 0 &&
              afterQueue.blocksRun > trainedQueue.blocksRun, what);
        std::snprintf(what, sizeof(what), "%s leaves identical PC/IRD/IRC", name);
        check(queueRef.getPC() == queueNative.getPC() &&
              queueRef.getIRD() == queueNative.getIRD() &&
              queueRef.getIRC() == queueNative.getIRC(), what);
        std::snprintf(what, sizeof(what), "%s keeps the exact last consumed word in IRC", name);
        check(queueNative.getIRD() == (jumpAbsoluteLong ? 0x4EF9 : 0x60FF) &&
              queueNative.getIRC() == expectedIrc, what);
    };

    checkQueueLoop(true, uint16_t(kCode), "JMP (xxx).L exit");
    checkQueueLoop(false, 0xFFFE, "BRA.L taken exit");

    const auto checkSuccessfulPostincrement = [&](bool mmio, const char* name) {
        FaultCpu successRef, successNative;
        installWriteLoop(successRef, 0x1CC0, 0, false);
        installWriteLoop(successNative, 0x1CC0, 0, false);
        successRef.reset(); successNative.reset();
        successRef.setTC(12u << 20); successNative.setTC(12u << 20);

        successNative.setA(6, 0x004000);
        successNative.setD(0, 0x80);
        successNative.jit.setEnabled(true);
        successNative.jit.executeUntil(successNative.getClock() + 256);
        const auto trainedSuccess = successNative.jit.stats().snapshot();

        // Start both engines from byte-identical storage after native training;
        // this makes the whole 16 MiB comparison an oracle, not just the target.
        successRef.mem = successNative.mem;
        prepareFault(successRef); prepareFault(successNative);
        const uint32_t target = mmio ? kMmio : 0x005000;
        successRef.setA(6, target); successNative.setA(6, target);
        successRef.setD(0, 0xA5); successNative.setD(0, 0xA5);

        successRef.executeUntil(successRef.getClock() + 1);
        successNative.jit.executeUntil(successNative.getClock() + 1);
        const auto afterSuccess = successNative.jit.stats().snapshot();

        char what[184];
        std::snprintf(what, sizeof(what), "%s entered its compiled block", name);
        check(trainedSuccess.blocksCompiled != 0 &&
              afterSuccess.blocksRun > trainedSuccess.blocksRun, what);
        std::snprintf(what, sizeof(what), "%s writes $A5 at the preincrement address", name);
        check(successRef.getA(6) == target + 1 &&
              successNative.getA(6) == target + 1 &&
              (mmio || (successRef.mem[target] == 0xA5 &&
                        successNative.mem[target] == 0xA5)), what);
        std::snprintf(what, sizeof(what), "%s leaves the complete 16 MiB memory image identical", name);
        check(std::memcmp(successRef.mem.data(), successNative.mem.data(),
                          successRef.mem.size()) == 0, what);
        std::snprintf(what, sizeof(what), "%s leaves identical architectural boundary state", name);
        check(successRef.getD(0) == successNative.getD(0) &&
              successRef.getA(6) == successNative.getA(6) &&
              successRef.getPC() == successNative.getPC() &&
              successRef.getPC0() == successNative.getPC0() &&
              successRef.getIRD() == successNative.getIRD() &&
              successRef.getIRC() == successNative.getIRC() &&
              successRef.getSR() == successNative.getSR() &&
              successRef.getClock() == successNative.getClock(), what);

        if (mmio) {
            const auto& r = successRef.observedWrite;
            const auto& n = successNative.observedWrite;
            std::snprintf(what, sizeof(what), "%s callback sees identical address/value/width", name);
            check(r.count == 1 && n.count == 1 &&
                  r.address == n.address && r.value == n.value &&
                  r.bytes == n.bytes, what);
            std::snprintf(what, sizeof(what), "%s callback sees identical A6 and PC/PC0", name);
            check(r.a6 == n.a6 && r.pc == n.pc && r.pc0 == n.pc0, what);
            std::snprintf(what, sizeof(what), "%s callback sees identical IRD/IRC and SR", name);
            check(r.ird == n.ird && r.irc == n.irc && r.sr == n.sr, what);
            std::snprintf(what, sizeof(what), "%s callback sees identical CPU clock", name);
            check(r.clock == n.clock, what);
            if (r.count == 1 && n.count == 1 &&
                (r.a6 != n.a6 || r.pc != n.pc || r.pc0 != n.pc0 ||
                 r.ird != n.ird || r.irc != n.irc || r.sr != n.sr ||
                 r.clock != n.clock)) {
                std::printf("    ref: A6=%08X PC=%08X PC0=%08X IRD=%04X IRC=%04X SR=%04X clock=%lld\n"
                            "    jit: A6=%08X PC=%08X PC0=%08X IRD=%04X IRC=%04X SR=%04X clock=%lld\n",
                            r.a6, r.pc, r.pc0, r.ird, r.irc, r.sr,
                            static_cast<long long>(r.clock), n.a6, n.pc, n.pc0,
                            n.ird, n.irc, n.sr, static_cast<long long>(n.clock));
            }
        }
    };

    checkSuccessfulPostincrement(false, "MOVE.B D0,(A6)+ RAM success");
    checkSuccessfulPostincrement(true, "MOVE.B D0,(A6)+ MMIO success");

    // AArch64's large Moira-layout accesses use x15 as an address scratch.
    // A two-memory MOVE once kept the destination host pointer there across
    // the source (A7)+ commit; the commit clobbered x15 and the byte store
    // landed in the CPU object's A7 field ($..86 -> $..40) instead of RAM.
    {
        constexpr uint32_t source = 0x006000;
        constexpr uint32_t destination = 0x007000;
        FaultCpu moveRef, moveNative;
        installWriteLoop(moveRef, 0x129F, 0, false); // MOVE.B (A7)+,(A1)
        installWriteLoop(moveNative, 0x129F, 0, false);
        moveRef.reset(); moveNative.reset();
        moveRef.setTC(12u << 20); moveNative.setTC(12u << 20);
        moveNative.setA(7, source); moveNative.setA(1, destination);
        moveNative.mem[source] = 0xA5;
        moveNative.jit.setEnabled(true);
        // Two independent DTLB fills each replay the instruction once;
        // leave enough loop iterations to enter the fully native path.
        moveNative.jit.executeUntil(moveNative.getClock() + 1024);
        moveNative.setPC(kCode);
        moveNative.setA(7, source); moveNative.setA(1, destination);
        moveNative.jit.executeUntil(moveNative.getClock() + 256);
        const auto trainedMove = moveNative.jit.stats().snapshot();

        moveRef.mem = moveNative.mem;
        const auto prepareMove = [&](FaultCpu& c) {
            c.setPC(kCode);
            c.setA(7, source);
            c.setA(1, destination);
            c.mem[source] = 0xA5;
            c.mem[destination] = 0;
            c.setSR(0x2700);
            c.setClock(0);
            const auto layout = c.pomJitLayout();
            *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(&c) +
                                         layout.flags) = 0;
        };
        prepareMove(moveRef); prepareMove(moveNative);
        moveRef.executeUntil(1);
        moveNative.jit.executeUntil(1);
        const auto afterMove = moveNative.jit.stats().snapshot();

        if (!(trainedMove.blocksCompiled != 0 &&
              afterMove.instrs > trainedMove.instrs &&
              afterMove.slowInstrs == trainedMove.slowInstrs))
            std::printf("    move stats compiled=%llu instrs=%llu->%llu slow=%llu->%llu blocks=%llu->%llu\n",
                        (unsigned long long)trainedMove.blocksCompiled,
                        (unsigned long long)trainedMove.instrs,
                        (unsigned long long)afterMove.instrs,
                        (unsigned long long)trainedMove.slowInstrs,
                        (unsigned long long)afterMove.slowInstrs,
                        (unsigned long long)trainedMove.blocksRun,
                        (unsigned long long)afterMove.blocksRun);
        check(trainedMove.blocksCompiled != 0 &&
              afterMove.instrs > trainedMove.instrs &&
              afterMove.slowInstrs == trainedMove.slowInstrs,
              "MOVE.B (A7)+,(A1) executes natively without fallback");
        check(moveRef.getA(7) == source + 2 &&
              moveNative.getA(7) == moveRef.getA(7) &&
              moveRef.getA(1) == destination &&
              moveNative.getA(1) == destination,
              "two-memory MOVE preserves both EA register updates");
        check(moveRef.mem[destination] == 0xA5 &&
              moveNative.mem[destination] == 0xA5 &&
              std::memcmp(moveRef.mem.data(), moveNative.mem.data(),
                          moveRef.mem.size()) == 0,
              "two-memory MOVE stores through the preserved host pointer");
    }

    {
        FaultCpu sccRef, sccNative;
        installSccRegisterLoop(sccRef); installSccRegisterLoop(sccNative);
        sccRef.reset(); sccNative.reset();
        sccRef.setTC(12u << 20); sccNative.setTC(12u << 20);
        sccNative.setD(0, 0x12345600);
        sccNative.jit.setEnabled(true);
        sccNative.jit.executeUntil(sccNative.getClock() + 256);
        const auto trainedScc = sccNative.jit.stats().snapshot();

        sccRef.mem = sccNative.mem;
        const auto prepareScc = [](FaultCpu& c) {
            c.setPC(kCode);
            c.setD(0, 0x12345600);
            c.setA(7, kStack);
            c.setSR(0x2700);
            c.setClock(0);
            const auto layout = c.pomJitLayout();
            *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(&c) +
                                         layout.flags) = 0;
        };
        prepareScc(sccRef); prepareScc(sccNative);
        sccRef.executeUntil(1);
        sccNative.jit.executeUntil(1);
        const auto afterScc = sccNative.jit.stats().snapshot();

        check(trainedScc.blocksCompiled != 0 &&
              afterScc.instrs > trainedScc.instrs &&
              afterScc.slowInstrs == trainedScc.slowInstrs,
              "ST D0 executes natively without an instruction fallback");
        check(sccRef.getD(0) == 0x123456FF &&
              sccNative.getD(0) == sccRef.getD(0) &&
              sccRef.getPC() == sccNative.getPC() &&
              sccRef.getClock() == sccNative.getClock(),
              "ST D0 preserves the upper 24 bits and matches the interpreter");
    }

    // PEA was the remaining line-$4 AArch64 asymmetry. Keep its old-A7
    // source ordering and sole stack write under a direct native assertion.
    {
        FaultCpu peaRef, peaNative;
        installPeaLoop(peaRef); installPeaLoop(peaNative);
        peaRef.reset(); peaNative.reset();
        peaRef.setTC(12u << 20); peaNative.setTC(12u << 20);
        peaNative.setA(6, 0x004000); peaNative.setA(7, kStack);
        peaNative.jit.setEnabled(true);
        peaNative.jit.executeUntil(peaNative.getClock() + 256);
        const auto trainedPea = peaNative.jit.stats().snapshot();

        peaRef.mem = peaNative.mem;
        const auto preparePea = [](FaultCpu& c) {
            c.setPC(kCode);
            c.setA(6, 0x005000);
            c.setA(7, kStack);
            c.setSR(0x2700);
            c.setClock(0);
            const auto layout = c.pomJitLayout();
            *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(&c) +
                                         layout.flags) = 0;
        };
        preparePea(peaRef); preparePea(peaNative);
        peaRef.executeUntil(1);
        peaNative.jit.executeUntil(1);
        const auto afterPea = peaNative.jit.stats().snapshot();

        check(trainedPea.blocksCompiled != 0 &&
              afterPea.instrs > trainedPea.instrs &&
              afterPea.slowInstrs == trainedPea.slowInstrs,
              "PEA d16(A6) executes natively without an instruction fallback");
        check(peaRef.getA(7) == kStack - 4 &&
              peaNative.getA(7) == peaRef.getA(7) &&
              get32(peaNative, kStack - 4) == 0x005004,
              "PEA computes its source before A7 and pushes the exact address");
        check(peaRef.getPC() == peaNative.getPC() &&
              peaRef.getPC0() == peaNative.getPC0() &&
              peaRef.getIRD() == peaNative.getIRD() &&
              peaRef.getIRC() == peaNative.getIRC() &&
              peaRef.getClock() == peaNative.getClock() &&
              std::memcmp(peaRef.mem.data(), peaNative.mem.data(),
                          peaRef.mem.size()) == 0,
              "PEA leaves an interpreter-identical architectural boundary");
    }

    const auto checkWriteEa = [&](uint16_t opcode, uint16_t extension,
                                  bool hasExtension, uint32_t initialA6,
                                  uint32_t finalA6, bool exactNativeThunk,
                                  const char* name) {
        FaultCpu eaRef, eaNative;
        installWriteLoop(eaRef, opcode, extension, hasExtension);
        installWriteLoop(eaNative, opcode, extension, hasExtension);
        eaRef.reset(); eaNative.reset();
        eaRef.setTC(12u << 20); eaNative.setTC(12u << 20);

        eaNative.setA(6, hasExtension ? 0x004000 :
                                (opcode == 0x1D00 ? 0x004100 : 0x004000));
        eaNative.setD(0, 0x80);
        eaNative.setD(1, 0);
        eaNative.jit.setEnabled(true);
        eaNative.jit.executeUntil(eaNative.getClock() + 256);
        const auto trainedEa = eaNative.jit.stats().snapshot();

        prepareFault(eaRef); prepareFault(eaNative);
        eaRef.setA(6, initialA6); eaNative.setA(6, initialA6);
        eaRef.setD(1, 0); eaNative.setD(1, 0);
        eaRef.executeUntil(eaRef.getClock() + 1);
        eaNative.jit.executeUntil(eaNative.getClock() + 1);
        const auto afterEa = eaNative.jit.stats().snapshot();
        const uint32_t refEaSp = eaRef.getA(7), jitEaSp = eaNative.getA(7);

        char what[176];
        std::snprintf(what, sizeof(what), "%s entered its compiled container block", name);
        check(trainedEa.blocksCompiled != 0 &&
              afterEa.blocksRun > trainedEa.blocksRun, what);
        std::snprintf(what, sizeof(what), "%s %s", name,
                      exactNativeThunk ? "takes thunk plus exact replay"
                                       : "remains on conservative replay");
        const bool faultCountOk = eaRef.writeFaults == 1 &&
              eaNative.writeFaults == (exactNativeThunk ? 2 : 1);
        if (!faultCountOk)
            std::printf("    writeFaults ref=%d jit=%d (want jit=%d)\n",
                        eaRef.writeFaults, eaNative.writeFaults,
                        exactNativeThunk ? 2 : 1);
        check(faultCountOk, what);
        std::snprintf(what, sizeof(what), "%s format-$A frame is byte-exact", name);
        check(refEaSp == kStack - 32 && jitEaSp == refEaSp &&
              std::memcmp(eaRef.mem.data() + refEaSp,
                          eaNative.mem.data() + jitEaSp, 32) == 0, what);
        std::snprintf(what, sizeof(what), "%s leaves the exact updated A6", name);
        check(eaRef.getA(6) == finalA6 && eaNative.getA(6) == finalA6, what);
    };

    // A PI miss is deliberately replayed before any native bus access: the
    // RAM hit remains native, while MMIO keeps Moira's intra-cycle pacing.
    checkWriteEa(0x1CC0, 0, false, kHole, kHole + 1, false,
                 "MOVE.B D0,(A6)+ fault");
    checkWriteEa(0x1D00, 0, false, kHole + 1, kHole, true,
                 "MOVE.B D0,-(A6) fault");
    // Brief-indexed destination is native on both generators. Its exact
    // thunk faults once before untouched replay faults again; the format-$A
    // frame and the pre-access A6 commit must still match Moira byte for byte.
    checkWriteEa(0x1D80, 0x1000, true, kHole, kHole, true,
                 "MOVE.B D0,d8(A6,D1.W) fault");
    // The 040 admits indexed Scc, but the 030 trace-cost guard still keeps
    // this form on pristine replay on both hosts. Pin that conservative edge
    // separately from the native indexed MOVE acceptance above.
    checkWriteEa(0x50F6, 0x1000, true, kHole, kHole, false,
                 "ST d8(A6,D1.W) fault");
    checkWriteEa(0x50DE, 0, false, kHole, kHole + 1, false,
                 "ST (A6)+ fault");

    // A 68030 JSR does not keep a compile-time copy of the first target
    // word: trap patching may change it after the caller block was emitted.
    // Keep caller and callee on different code-guard slices, patch only the
    // callee, then demand an exact native/interpreter queue boundary.
    {
        FaultCpu jsrRef, jsrNative;
        installJsrLoop(jsrRef); installJsrLoop(jsrNative);
        jsrRef.reset(); jsrNative.reset();
        jsrRef.setTC(12u << 20); jsrNative.setTC(12u << 20);
        for (FaultCpu* c : {&jsrRef, &jsrNative}) {
            c->setA(0, kSubroutine);
            c->setA(7, kStack);
            c->setD(0, 0);
            c->setSR(0x2700);
        }
        jsrNative.jit.setEnabled(true);

        const auto runJsrLockstep = [&](int checkpoints) {
            bool same = true;
            for (int step = 0; step < checkpoints && same; step++) {
                const int64_t target = jsrRef.getClock() + 37;
                jsrRef.executeUntil(target);
                jsrNative.jit.executeUntil(target);
                for (int r = 0; r < 8; r++)
                    same = same && jsrRef.getD(r) == jsrNative.getD(r) &&
                           jsrRef.getA(r) == jsrNative.getA(r);
                same = same && jsrRef.getPC() == jsrNative.getPC() &&
                       jsrRef.getPC0() == jsrNative.getPC0() &&
                       jsrRef.getIRD() == jsrNative.getIRD() &&
                       jsrRef.getIRC() == jsrNative.getIRC() &&
                       jsrRef.getSR() == jsrNative.getSR() &&
                       jsrRef.getClock() == jsrNative.getClock() &&
                       std::memcmp(jsrRef.mem.data() + kStack - 8,
                                   jsrNative.mem.data() + kStack - 8, 8) == 0;
            }
            return same;
        };

        const bool trainedExact = runJsrLockstep(64);
        const auto trainedJsr = jsrNative.jit.stats().snapshot();
        put16(jsrRef, kSubroutine, 0x5480);    // ADDQ.L #2,D0
        put16(jsrNative, kSubroutine, 0x5480);
        if (jsrNative.guard) jsrNative.guard->note(kSubroutine, 2);
        const bool patchedExact = runJsrLockstep(64);
        const auto warmedJsr = jsrNative.jit.stats().snapshot();
        const bool steadyExact = runJsrLockstep(64);
        const auto afterJsr = jsrNative.jit.stats().snapshot();

        check(trainedExact && patchedExact && steadyExact,
              "JSR d16(A0) keeps exact 68030 state after target patching");
        check(trainedJsr.blocksCompiled != 0 &&
              afterJsr.blocksRun > trainedJsr.blocksRun &&
              afterJsr.slowInstrs == warmedJsr.slowInstrs,
              "patched JSR caller stays native with a run-time target word");
    }

    // MOVEM's native 030 contract is all-or-nothing: one proved contiguous
    // span, no partially committed format-$B restart state. Exercise both
    // register directions and the predecrement mask reversal in one loop.
    {
        FaultCpu movemRef, movemNative;
        installMovemLoop(movemRef); installMovemLoop(movemNative);
        movemRef.reset(); movemNative.reset();
        movemRef.setTC(12u << 20); movemNative.setTC(12u << 20);
        for (FaultCpu* c : {&movemRef, &movemNative}) {
            c->setD(0, 0x01234567);
            c->setD(1, 0x89ABCDEF);
            c->setD(2, 0x13579BDF);
            c->setA(0, 0x002468AC);
            c->setA(1, 0x00FEDCBA);
            c->setA(7, kStack);
            c->setSR(0x2700);
        }
        movemNative.jit.setEnabled(true);

        bool same = true;
        for (int step = 0; step < 192 && same; step++) {
            const int64_t target = movemRef.getClock() + 37;
            movemRef.executeUntil(target);
            movemNative.jit.executeUntil(target);
            for (int r = 0; r < 8; r++)
                same = same && movemRef.getD(r) == movemNative.getD(r) &&
                       movemRef.getA(r) == movemNative.getA(r);
            same = same && movemRef.getPC() == movemNative.getPC() &&
                   movemRef.getPC0() == movemNative.getPC0() &&
                   movemRef.getIRD() == movemNative.getIRD() &&
                   movemRef.getIRC() == movemNative.getIRC() &&
                   movemRef.getSR() == movemNative.getSR() &&
                   movemRef.getClock() == movemNative.getClock() &&
                   std::memcmp(movemRef.mem.data() + kStack - 20,
                               movemNative.mem.data() + kStack - 20, 20) == 0;
        }
        const auto movemStats = movemNative.jit.stats().snapshot();
        check(same && movemNative.getA(7) == kStack,
              "MOVEM.L push/pop stays exact at every 68030 boundary");
        check(movemStats.blocksCompiled != 0 && movemStats.blocksRun != 0 &&
              movemStats.slowInstrs == 0,
              "MOVEM.L push/pop executes wholly in native code");
    }

    // The full-machine $533E corruption was not the PI write itself. The
    // following memory-to-memory MOVE hit its source DTLB probe, committed
    // (A0)+, then missed the destination probe and replayed with A0 already
    // advanced. Both probes must now refuse before either EA is mutated.
    {
        constexpr uint32_t source = 0x006000;
        FaultCpu mmRef, mmNative;
        installWriteLoop(mmRef, 0x2CD8, 0, false);   // MOVE.L (A0)+,(A6)+
        installWriteLoop(mmNative, 0x2CD8, 0, false);
        put32(mmRef, source, 0x40A00000);
        put32(mmNative, source, 0x40A00000);
        mmRef.reset(); mmNative.reset();
        mmRef.setTC(12u << 20); mmNative.setTC(12u << 20);
        mmNative.setA(0, source); mmNative.setA(6, 0x004000);
        mmNative.jit.setEnabled(true);
        mmNative.jit.executeUntil(mmNative.getClock() + 256);
        const auto trainedMm = mmNative.jit.stats().snapshot();

        prepareFault(mmRef); prepareFault(mmNative);
        mmRef.setA(0, source); mmNative.setA(0, source);
        mmRef.setA(6, kHole); mmNative.setA(6, kHole);
        mmRef.executeUntil(mmRef.getClock() + 1);
        mmNative.jit.executeUntil(mmNative.getClock() + 1);
        const auto afterMm = mmNative.jit.stats().snapshot();
        const uint32_t refMmSp = mmRef.getA(7), jitMmSp = mmNative.getA(7);

        check(trainedMm.blocksCompiled != 0 &&
              afterMm.blocksRun > trainedMm.blocksRun,
              "memory-to-memory MOVE entered its compiled block");
        check(mmRef.writeFaults == 1 && mmNative.writeFaults == 1,
              "destination miss replayed before any native bus write");
        check(mmRef.getA(0) == source + 4 && mmNative.getA(0) == source + 4,
              "source (A0)+ advances exactly once across destination replay");
        check(mmRef.getA(6) == kHole + 4 && mmNative.getA(6) == kHole + 4,
              "destination (A6)+ keeps the exact fault-time update");
        check(refMmSp == kStack - 32 && jitMmSp == refMmSp &&
              std::memcmp(mmRef.mem.data() + refMmSp,
                          mmNative.mem.data() + jitMmSp, 32) == 0,
              "memory-to-memory destination fault frame is byte-exact");
    }

    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
