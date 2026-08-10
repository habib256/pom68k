// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// AArch64 68030 restartable-write gate. MOVE.B destinations d16(A6), -(A6),
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
    jit::Engine jit;
    jit::CodeGuard* guard = nullptr;
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
            if (c.inHole(p)) return nullptr;
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
    setenv("POM68K_JIT_BACKEND", "a64", 1);
    setenv("POM68K_JIT_UNSAFE_BACKEND", "1", 1);
    setenv("POM68K_JIT_BLOCKS", "1", 1);
    setenv("POM68K_JIT_HOT", "1", 1);
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

    if (std::strcmp(native.jit.backendName(), "aarch64") != 0) {
        std::printf("SKIP: AArch64 backend unavailable (%s)\n",
                    native.jit.backendName());
        return 0;
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
        check(eaRef.writeFaults == 1 &&
              eaNative.writeFaults == (exactNativeThunk ? 2 : 1), what);
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
    checkWriteEa(0x1D80, 0x1000, true, kHole, kHole, true,
                 "MOVE.B D0,d8(A6,D1.W) fault");

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
