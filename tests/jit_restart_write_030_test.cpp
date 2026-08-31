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
#include "JitTestConfig.h"

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
constexpr uint32_t kBitfieldData = 0x007000;

const jit::ResolvedConfig& injectedJitConfig() {
    static const jit::ResolvedConfig config =
        testjit::resolveFromEnvironment();
    return config;
}

class FaultCpu final : public moira::Moira {
public:
    struct WriteObservation {
        int count = 0;
        uint32_t address = 0;
        uint32_t value = 0;
        unsigned bytes = 0;
        uint32_t a6 = 0;
        uint32_t a7 = 0;
        uint32_t pc = 0;
        uint32_t pc0 = 0;
        uint16_t ird = 0;
        uint16_t irc = 0;
        uint16_t sr = 0;
        int64_t clock = 0;
    };

    FaultCpu()
        : mem(1u << 24, 0), jit(*this, hooks(this), jit::kGuest68030,
                                injectedJitConfig()) {
        setModel(moira::Model::M68030);
    }

    struct IcacheObservation {
        int64_t fetches = 0;
        int64_t hits = 0;
        int64_t misses = 0;
    };

    void enableIcacheOverlay() {
        pomIcache.armed = true;
        pomIcache.missPenalty = 4;
        pomIcache.reset();
        pomIcache.fetches = pomIcache.hits = pomIcache.misses = 0;
        setCACR(1);
    }

    IcacheObservation icacheObservation() const {
        return {pomIcache.fetches, pomIcache.hits, pomIcache.misses};
    }

    std::vector<uint8_t> mem;
    // The engine attaches its production write guard during construction.
    // Construct the callback destination first so its initializer cannot
    // erase that attachment afterwards.
    jit::CodeGuard* guard = nullptr;
    jit::Engine jit;
    int writeFaults = 0;
    int readFaults = 0;
    int mmioReads = 0;
    int mmioReadDelay = 0;
    int mmioWriteDelay = 0;
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
        observedWrite.a7 = getA(7);
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
        if (inHole(a)) {
            auto& c = *const_cast<FaultCpu*>(this);
            c.readFaults++;
            c.extBusError();
        }
        if (inMmio(a)) {
            auto& c = *const_cast<FaultCpu*>(this);
            c.mmioReads++;
            if (c.mmioReadDelay) c.pomJitSync(c.mmioReadDelay);
        }
        return mem[a & 0xFFFFFF];
    }
    moira::u16 read16(moira::u32 a) const override {
        if (inHole(a)) {
            auto& c = *const_cast<FaultCpu*>(this);
            c.readFaults++;
            c.extBusError();
        }
        if (inMmio(a)) {
            auto& c = *const_cast<FaultCpu*>(this);
            c.mmioReads++;
            if (c.mmioReadDelay) c.pomJitSync(c.mmioReadDelay);
        }
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
            if (c.mmioWriteDelay) c.pomJitSync(c.mmioWriteDelay);
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
            if (c.mmioWriteDelay) c.pomJitSync(c.mmioWriteDelay);
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

void installSpeedometerPollLoop(FaultCpu& c) {
    put32(c, 0, kStack);
    put32(c, 4, kCode);
    put32(c, 8, kHandler);
    put16(c, kCode + 0x00, 0x0829);     // BTST #5,d16(A1)
    put16(c, kCode + 0x02, 0x0005);
    put16(c, kCode + 0x04, 0x0000);
    put16(c, kCode + 0x06, 0x1029);     // MOVE.B d16(A1),D0
    put16(c, kCode + 0x08, 0x0001);
    put16(c, kCode + 0x0A, 0x1429);     // MOVE.B d16(A1),D2
    put16(c, kCode + 0x0C, 0x0002);
    put16(c, kCode + 0x0E, 0x60F0);     // BRA.S kCode
    put16(c, kHandler, 0x4E71);
}

void installSpeedometerSelectorWriteLoop(FaultCpu& c) {
    put32(c, 0, kStack);
    put32(c, 4, kCode);
    put32(c, 8, kHandler);
    put16(c, kCode + 0x00, 0x137C);    // MOVE.B #2,$1A00(A1)
    put16(c, kCode + 0x02, 0x0002);
    put16(c, kCode + 0x04, 0x1A00);
    put16(c, kCode + 0x06, 0x60F8);    // BRA.S kCode
    put16(c, kHandler, 0x4E71);
}

enum class QueueTransfer {
    BranchWord, BranchLong, BranchSubroutineLong, JumpAbsoluteLong
};

void installQueueLoop(FaultCpu& c, QueueTransfer transfer) {
    put32(c, 0, kStack);
    put32(c, 4, kCode);
    put32(c, 8, kHandler);
    uint32_t words = 0;
    switch (transfer) {
        case QueueTransfer::BranchWord:
            put16(c, kCode + 0, 0x6000); // BRA.W kCode, displacement -2
            put16(c, kCode + 2, 0xFFFE);
            words = 2;
            break;
        case QueueTransfer::BranchLong:
            put16(c, kCode + 0, 0x60FF); // BRA.L kCode, displacement -2
            put32(c, kCode + 2, 0xFFFFFFFE);
            words = 3;
            break;
        case QueueTransfer::BranchSubroutineLong:
            put16(c, kCode + 0, 0x61FF); // BSR.L kCode, displacement -2
            put32(c, kCode + 2, 0xFFFFFFFE);
            words = 3;
            break;
        case QueueTransfer::JumpAbsoluteLong:
            put16(c, kCode + 0, 0x4EF9); // JMP (xxx).L kCode
            put32(c, kCode + 2, kCode);
            words = 3;
            break;
    }
    put16(c, kCode + words * 2, 0x4E71); // copied lookahead, never executed
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

void installBitfieldWriteLoop(FaultCpu& c) {
    put32(c, 0, kStack);
    put32(c, 4, kCode);
    put32(c, 8, kHandler);
    put16(c, kCode + 0x00, 0x72FD);    // MOVEQ #-3,D1
    put16(c, kCode + 0x02, 0x203C);    // MOVE.L #$13579BDF,D0
    put32(c, kCode + 0x04, 0x13579BDF);
    put16(c, kCode + 0x08, 0xEAD0);    // BFCHG (A0){0:D0}
    put16(c, kCode + 0x0A, 0x0020);
    put16(c, kCode + 0x0C, 0xE8D0);    // BFTST (A0){0:D0}
    put16(c, kCode + 0x0E, 0x0020);
    put16(c, kCode + 0x10, 0xECE9);    // BFCLR 4(A1){2:15}
    put16(c, kCode + 0x12, 0x008F);
    put16(c, kCode + 0x14, 0x0004);
    put16(c, kCode + 0x16, 0xE8E9);    // BFTST 4(A1){2:15}
    put16(c, kCode + 0x18, 0x008F);
    put16(c, kCode + 0x1A, 0x0004);
    put16(c, kCode + 0x1C, 0xEED0);    // BFSET (A0){D1:8}
    put16(c, kCode + 0x1E, 0x0848);
    put16(c, kCode + 0x20, 0xE8D0);    // BFTST (A0){D1:8}
    put16(c, kCode + 0x22, 0x0848);
    put16(c, kCode + 0x24, 0xEFD1);    // BFINS D0,(A1){7:9} (SimCity)
    put16(c, kCode + 0x26, 0x01C9);
    put16(c, kCode + 0x28, 0xE8D1);    // BFTST (A1){7:9}
    put16(c, kCode + 0x2A, 0x01C9);
    put16(c, kCode + 0x2C, 0xEFD0);    // BFINS D0,(A0){0:D0}
    put16(c, kCode + 0x2E, 0x0020);
    put16(c, kCode + 0x30, 0xE8D0);    // BFTST (A0){0:D0}
    put16(c, kCode + 0x32, 0x0020);
    put16(c, kCode + 0x34, 0x60D2);    // BRA.S kCode+$08
    put16(c, kHandler, 0x4E71);
    put32(c, kBitfieldData + 0x00, 0xA55A3CC3);
    put32(c, kBitfieldData + 0x04, 0x5AA5C33C);
    put32(c, kBitfieldData + 0x08, 0x89ABCDEF);
}

void installSpeedometerBitfieldTailLoop(FaultCpu& c) {
    put32(c, 0, kStack);
    put32(c, 4, kCode);
    put32(c, 8, kHandler);
    put16(c, kCode + 0x00, 0x7A07);    // MOVEQ #7,D5 (tail offset)
    put16(c, kCode + 0x02, 0x7600);    // MOVEQ #0,D3 (width 32)
    put16(c, kCode + 0x04, 0xE9D4);    // Speedometer BFEXTU (A4){D5:D3},D0
    put16(c, kCode + 0x06, 0x0963);
    put16(c, kCode + 0x08, 0x72FD);    // MOVEQ #-3,D1 (signed adjust + tail)
    put16(c, kCode + 0x0A, 0xE9D4);    // BFEXTU (A4){D1:32},D0
    put16(c, kCode + 0x0C, 0x0840);
    put16(c, kCode + 0x0E, 0x7C00);    // MOVEQ #0,D6 (runtime no-tail arm)
    put16(c, kCode + 0x10, 0xE9D4);    // BFEXTU (A4){D6:32},D0
    put16(c, kCode + 0x12, 0x0980);
    put16(c, kCode + 0x14, 0x60EA);    // BRA.S kCode
    put16(c, kCode + 0x16, 0x4E71);
    put16(c, kHandler, 0x4E71);
    put32(c, kBitfieldData + 0x00, 0xA55A3CC3);
    put32(c, kBitfieldData + 0x04, 0x5AA5C33C);
    put32(c, kBitfieldData + 0x08, 0x89ABCDEF);
}

void installSpeedometerIndirectLeaLoop(FaultCpu& c) {
    put32(c, 0, kStack);
    put32(c, 4, kCode);
    put32(c, 8, kHandler);
    put16(c, kCode + 0x00, 0x41F6);    // LEA ([bd.W,A6],D6.L),A0
    put16(c, kCode + 0x02, 0x6925);    // full/postindexed, null outer disp
    put16(c, kCode + 0x04, 0x0010);    // word base displacement
    put16(c, kCode + 0x06, 0x60F8);    // BRA.S kCode
    put16(c, kHandler, 0x4E71);
    put32(c, kBitfieldData + 0x10, kBitfieldData + 0x100);
}

void installSpeedometerIndirectMoveLoop(FaultCpu& c) {
    put32(c, 0, kStack);
    put32(c, 4, kCode);
    put32(c, 8, kHandler);
    put16(c, kCode + 0x00, 0x2470);    // MOVEA.L ([bd.W],Zn),A2
    put16(c, kCode + 0x02, 0x81E1);    // full/preindexed, base/index suppressed
    put16(c, kCode + 0x04, 0x7000);    // pointer at kBitfieldData
    put16(c, kCode + 0x06, 0x2070);    // MOVEA.L ([bd.W],Zn),A0
    put16(c, kCode + 0x08, 0x81E1);
    put16(c, kCode + 0x0A, 0x7004);    // pointer at kBitfieldData + 4
    put16(c, kCode + 0x0C, 0x60F2);    // BRA.S kCode
    put16(c, kHandler, 0x4E71);
    put32(c, kBitfieldData + 0x00, kBitfieldData + 0x100);
    put32(c, kBitfieldData + 0x04, kBitfieldData + 0x104);
    put32(c, kBitfieldData + 0x100, 0x12345678);
    put32(c, kBitfieldData + 0x104, 0x89ABCDEF);
}

void installSpeedometerIndirectTstLoop(FaultCpu& c) {
    put32(c, 0, kStack);
    put32(c, 4, kCode);
    put32(c, 8, kHandler);
    put16(c, kCode + 0x00, 0x4A76);    // TST.W ([40,A6],4)
    put16(c, kCode + 0x02, 0x8162);    // full/preindexed, index suppressed
    put16(c, kCode + 0x04, 0x0028);    // word base displacement
    put16(c, kCode + 0x06, 0x0004);    // word outer displacement
    put16(c, kCode + 0x08, 0x60F6);    // BRA.S kCode
    put16(c, kHandler, 0x4E71);
    put32(c, kBitfieldData + 0x28, kBitfieldData + 0xFC);
    put16(c, kBitfieldData + 0x100, 0x8001);
}

void installSpeedometerIndexedDestinationLoop(FaultCpu& c) {
    put32(c, 0, kStack);
    put32(c, 4, kCode);
    put32(c, 8, kHandler);
    put16(c, kCode + 0x00, 0x2191);    // MOVE.L (A1),0(A0,D1.W)
    put16(c, kCode + 0x02, 0x1000);    // Speedometer brief destination
    put16(c, kCode + 0x04, 0x31A9);    // MOVE.W 4(A1),0(A0,D1.W)
    put16(c, kCode + 0x06, 0x0004);    // source displacement
    put16(c, kCode + 0x08, 0x1000);    // Speedometer brief destination
    put16(c, kCode + 0x0A, 0x60F4);    // BRA.S kCode
    put16(c, kHandler, 0x4E71);
    put32(c, kBitfieldData + 0x00, 0x12345678);
    put16(c, kBitfieldData + 0x04, 0xABCD);
}

void installSpeedometerDependentMoveLoop(FaultCpu& c, uint16_t opcode) {
    put32(c, 0, kStack);
    put32(c, 4, kCode);
    put32(c, 8, kHandler);
    put16(c, kCode + 0x00, opcode);     // MOVE.W/L (A7)+,4(A7)
    put16(c, kCode + 0x02, 0x0004);     // destination uses updated A7
    put16(c, kCode + 0x04, 0x60FA);     // BRA.S kCode
    put16(c, kHandler, 0x4E71);
}

void installSpeedometerMuluLongLoop(FaultCpu& c) {
    put32(c, 0, kStack);
    put32(c, 4, kCode);
    put32(c, 8, kHandler);
    put16(c, kCode + 0x00, 0x4C00);    // MULU.L D0,D4
    put16(c, kCode + 0x02, 0x4004);    // Speedometer exact 32-bit selector
    put16(c, kCode + 0x04, 0x60FA);    // BRA.S kCode
    put16(c, kHandler, 0x4E71);
}

void installSpeedometerMulsLongMemoryLoop(FaultCpu& c) {
    put32(c, 0, kStack);
    put32(c, 4, kCode);
    put32(c, 8, kHandler);
    put16(c, kCode + 0x00, 0x4C2F);    // MULS.L 24(A7),D0:D1
    put16(c, kCode + 0x02, 0x1C00);    // signed 64-bit, Dh=D0/Dl=D1
    put16(c, kCode + 0x04, 0x0018);
    put16(c, kCode + 0x06, 0x60F8);    // BRA.S kCode
    put16(c, kHandler, 0x4E71);
    put32(c, kStack + 0x18, 0xFFFF'FFFDu); // -3
}

void installSpeedometerRoxrLoop(FaultCpu& c) {
    put32(c, 0, kStack);
    put32(c, 4, kCode);
    put32(c, 8, kHandler);
    put16(c, kCode + 0x00, 0xE410);    // ROXR.B #2,D0
    put16(c, kCode + 0x02, 0xE291);    // ROXR.L #1,D1
    put16(c, kCode + 0x04, 0x60FA);    // BRA.S kCode
    put16(c, kHandler, 0x4E71);
}

void installJsrLoop(FaultCpu& c) {
    put32(c, 0, kStack);
    put32(c, 4, kCode);
    put32(c, 8, kHandler);
    put16(c, kCode + 0, 0x4EA8);        // JSR d16(A0)
    put16(c, kCode + 2, 0x0000);
    put16(c, kCode + 4, 0x4EB9);        // JSR kSubroutine.l (Speedometer)
    put32(c, kCode + 6, kSubroutine);
    put16(c, kCode + 10, 0x4EB2);       // JSR 0(A2,D1.W) (Speedometer)
    put16(c, kCode + 12, 0x1000);
    put16(c, kCode + 14, 0x4EB4);       // JSR 0(A4,D0.W) (Speedometer)
    put16(c, kCode + 16, 0x0000);
    put16(c, kCode + 18, 0x60EC);       // BRA.S kCode
    put16(c, kSubroutine + 0, 0x5287);  // ADDQ.L #1,D7
    put16(c, kSubroutine + 2, 0x4E75);  // RTS
    put16(c, kSubroutine + 4, 0x4E71);
    put16(c, kHandler, 0x4E71);
}

void installFullIndirectJsrLoop(FaultCpu& c) {
    put32(c, 0, kStack);
    put32(c, 4, kCode);
    put32(c, 8, kHandler);
    put16(c, kCode + 0, 0x4E71);
    put16(c, kCode + 2, 0x4E71);
    put16(c, kCode + 4, 0x4E71);
    put16(c, kCode + 6, 0x4EB0);        // JSR ([ZA0,D2.W*4])
    put16(c, kCode + 8, 0x2591);        // Speedometer, null BD/OD
    put16(c, kCode + 10, 0x4EB0);       // JSR ([16,ZA0,D2.W*4])
    put16(c, kCode + 12, 0x25A1);       // Speedometer, word BD/null OD
    put16(c, kCode + 14, 0x0010);
    put16(c, kCode + 16, 0x4EB0);       // JSR ([A0,D1.L*4])
    put16(c, kCode + 18, 0x1D11);       // arbitrary-pointer replay oracle
    put16(c, kCode + 20, 0x60EA);       // BRA.S kCode
    put16(c, kSubroutine + 0, 0x5280);  // ADDQ.L #1,D0
    put16(c, kSubroutine + 2, 0x4E75);  // RTS
    put16(c, kSubroutine + 4, 0x4E71);
    put32(c, kBitfieldData - 0x10, kSubroutine);
    put32(c, kBitfieldData, kSubroutine);
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
    // This test is the cross-backend product-default oracle for 030 memory
    // bitfields. An inherited attribution veto must not mask that default.
    unsetenv("POM68K_JIT_030_MEMBF");
    // The queue cases deliberately exercise multiword control flow with the
    // real 68030 i-cache overlay armed. This makes the gate judge the shared
    // linear-fetch proof, its emitted accounting and the terminal queue as
    // one contract.
    setenv("POM68K_JIT_ICACHE_EMIT", "1", 1);

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

    // Speedometer 4 polls the LC II's $50F0xxxx device aperture with these
    // exact three forms. A synthetic delay makes the trace's base cost
    // deliberately exceed the opcode table: generated code must keep the
    // access in the exact thunk, charge only the fixed tail and never enter
    // the per-instruction interpreter fallback on a successful device read.
    {
        FaultCpu pollRef, pollNative;
        installSpeedometerPollLoop(pollRef);
        installSpeedometerPollLoop(pollNative);
        pollRef.reset(); pollNative.reset();
        pollRef.setTC(12u << 20); pollNative.setTC(12u << 20);
        for (FaultCpu* c : {&pollRef, &pollNative}) {
            c->setA(1, kMmio);
            c->setD(0, 0xA5A5A500);
            c->setD(2, 0x5A5A5A00);
            c->mem[kMmio + 0] = 0x20;
            c->mem[kMmio + 1] = 0x3C;
            c->mem[kMmio + 2] = 0x80;
            c->mmioReadDelay = 23;
        }
        pollNative.jit.setEnabled(true);

        bool same = true;
        for (int step = 0; step < 128 && same; step++) {
            const int64_t target = pollRef.getClock() + 79;
            pollRef.executeUntil(target);
            pollNative.jit.executeUntil(target);
            for (int r = 0; r < 8; r++)
                same = same && pollRef.getD(r) == pollNative.getD(r) &&
                       pollRef.getA(r) == pollNative.getA(r);
            same = same && pollRef.getPC() == pollNative.getPC() &&
                   pollRef.getPC0() == pollNative.getPC0() &&
                   pollRef.getIRD() == pollNative.getIRD() &&
                   pollRef.getIRC() == pollNative.getIRC() &&
                   pollRef.getSR() == pollNative.getSR() &&
                   pollRef.getClock() == pollNative.getClock() &&
                   pollRef.mmioReads == pollNative.mmioReads;
            if (!same)
                std::printf("    Speedometer-poll divergence at checkpoint %d\n",
                            step);
        }
        const auto pollStats = pollNative.jit.stats().snapshot();
        check(same && pollNative.getD(0) == 0xA5A5A53C &&
              pollNative.getD(2) == 0x5A5A5A80 &&
              pollNative.mmioReads != 0,
              "Speedometer device polls preserve live delay and CPU state");
        check(pollStats.blocksCompiled != 0 && pollStats.blocksRun != 0 &&
              pollStats.slowInstrs == 0,
              "Speedometer 0829/1029/1429 execute through native exact reads");
    }

    // The companion selector write is MOVE.B #imm,$1A00(A1). Its traced
    // base cost contains the same live device wait as the polls above. The
    // generated instruction must publish flags/queue at the last-write
    // boundary, execute exactly one delayed MMIO write, and charge only the
    // fixed seven-cycle MOVE tail.
    {
        FaultCpu writeRef, writeNative;
        installSpeedometerSelectorWriteLoop(writeRef);
        installSpeedometerSelectorWriteLoop(writeNative);
        writeRef.reset(); writeNative.reset();
        writeRef.setTC(12u << 20); writeNative.setTC(12u << 20);
        for (FaultCpu* c : {&writeRef, &writeNative}) {
            c->setA(1, kMmio - 0x1A00);
            c->setSR(0x2710);                 // MOVE preserves X
            c->mmioWriteDelay = 23;
        }
        writeNative.jit.setEnabled(true);

        bool same = true;
        for (int step = 0; step < 128 && same; step++) {
            const int64_t target = writeRef.getClock() + 79;
            writeRef.executeUntil(target);
            writeNative.jit.executeUntil(target);
            for (int r = 0; r < 8; r++)
                same = same && writeRef.getD(r) == writeNative.getD(r) &&
                       writeRef.getA(r) == writeNative.getA(r);
            const auto& refWrite = writeRef.observedWrite;
            const auto& nativeWrite = writeNative.observedWrite;
            same = same && writeRef.getPC() == writeNative.getPC() &&
                   writeRef.getPC0() == writeNative.getPC0() &&
                   writeRef.getIRD() == writeNative.getIRD() &&
                   writeRef.getIRC() == writeNative.getIRC() &&
                   writeRef.getSR() == writeNative.getSR() &&
                   writeRef.getClock() == writeNative.getClock() &&
                   refWrite.count == nativeWrite.count &&
                   refWrite.address == nativeWrite.address &&
                   refWrite.value == nativeWrite.value &&
                   refWrite.bytes == nativeWrite.bytes &&
                   refWrite.pc == nativeWrite.pc &&
                   refWrite.pc0 == nativeWrite.pc0 &&
                   refWrite.ird == nativeWrite.ird &&
                   refWrite.irc == nativeWrite.irc &&
                   refWrite.sr == nativeWrite.sr &&
                   refWrite.clock == nativeWrite.clock;
            if (!same)
                std::printf("    Speedometer 137C divergence at checkpoint %d\n",
                            step);
        }
        const auto writeStats = writeNative.jit.stats().snapshot();
        check(same && writeNative.observedWrite.count != 0 &&
              writeNative.observedWrite.address == kMmio &&
              writeNative.observedWrite.value == 2 &&
              writeNative.observedWrite.bytes == 1 &&
              writeStats.blocksCompiled != 0 && writeStats.blocksRun != 0 &&
              writeStats.slowInstrs == 0,
              "Speedometer 137C exact write owns live delay and stays native");

        // Re-enter that compiled block with only the destination moved to a
        // bus-error hole. The exact thunk faults once; untouched Moira replay
        // faults once more and must build the same restartable format-$A
        // frame as the pure interpreter.
        const auto prepareSelectorFault = [](FaultCpu& c) {
            c.setPC(kCode); c.setPC0(kCode);
            c.setIRD(0x137C); c.setIRC(0x0002);
            c.setA(1, kHole - 0x1A00); c.setA(7, kStack);
            c.setSR(0x2710); c.setClock(0);
            c.writeFaults = 0; c.observedWrite = {};
            const auto layout = c.pomJitLayout();
            *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(&c) +
                                         layout.flags) = 0;
        };
        prepareSelectorFault(writeRef);
        prepareSelectorFault(writeNative);
        const auto beforeFault = writeNative.jit.stats().snapshot();
        writeRef.executeUntil(1);
        writeNative.jit.executeUntil(1);
        const auto afterFault = writeNative.jit.stats().snapshot();
        const uint32_t refSp = writeRef.getA(7);
        const uint32_t nativeSp = writeNative.getA(7);
        check(writeRef.writeFaults == 1 && writeNative.writeFaults == 2 &&
              afterFault.blocksRun > beforeFault.blocksRun &&
              refSp == kStack - 32 && nativeSp == refSp &&
              std::memcmp(writeRef.mem.data() + refSp,
                          writeNative.mem.data() + nativeSp, 32) == 0 &&
              writeRef.getPC() == writeNative.getPC() &&
              writeRef.getClock() == writeNative.getClock(),
              "Speedometer 137C exact-write /BERR replay frame is identical");
    }

    // Speedometer's 2470/2070 sites use one full-indirect pointer read and
    // then a longword operand read before committing MOVEA's destination.
    // Both mappings stay direct in the native path; changing the resolved
    // operand to MMIO must replay the whole pristine instruction instead.
    {
        FaultCpu moveRef, moveNative;
        installSpeedometerIndirectMoveLoop(moveRef);
        installSpeedometerIndirectMoveLoop(moveNative);
        moveRef.reset(); moveNative.reset();
        moveRef.setTC(12u << 20); moveNative.setTC(12u << 20);
        moveNative.jit.setEnabled(true);

        bool same = true;
        for (int step = 0; step < 256 && same; step++) {
            const int64_t target = moveRef.getClock() + 43;
            moveRef.executeUntil(target);
            moveNative.jit.executeUntil(target);
            for (int r = 0; r < 8; r++)
                same = same && moveRef.getD(r) == moveNative.getD(r) &&
                       moveRef.getA(r) == moveNative.getA(r);
            same = same && moveRef.getPC() == moveNative.getPC() &&
                   moveRef.getPC0() == moveNative.getPC0() &&
                   moveRef.getIRD() == moveNative.getIRD() &&
                   moveRef.getIRC() == moveNative.getIRC() &&
                   moveRef.getSR() == moveNative.getSR() &&
                   moveRef.getClock() == moveNative.getClock();
            if (!same)
                std::printf("    030 2470/2070 divergence at checkpoint %d\n",
                            step);
        }
        const auto moveStats = moveNative.jit.stats().snapshot();
        const bool nativeProduction =
            (!std::strcmp(moveNative.jit.backendName(), "aarch64") ||
             !std::strcmp(moveNative.jit.backendName(), "x86-64")) &&
            !moveNative.jit.config().packedCcr;
        check(same && moveNative.getA(2) == 0x12345678 &&
              moveNative.getA(0) == 0x89ABCDEF &&
              moveStats.blocksCompiled != 0 && moveStats.blocksRun != 0 &&
              (!nativeProduction || moveStats.slowInstrs == 0),
              "Speedometer 2470/2070 indirect MOVEA stays native on 030");

        // Keep the pointer itself in proved RAM, but make its result a
        // device address. The second probe must refuse before A2 changes;
        // Moira then owns both 16-bit halves and their live delay exactly.
        for (FaultCpu* c : {&moveRef, &moveNative}) {
            put32(*c, kBitfieldData, kMmio);
            put32(*c, kMmio, 0xCAFEBABE);
            c->mmioReadDelay = 11;
        }
        bool replaySame = true;
        for (int step = 0; step < 32 && replaySame; step++) {
            const int64_t target = moveRef.getClock() + 43;
            moveRef.executeUntil(target);
            moveNative.jit.executeUntil(target);
            for (int r = 0; r < 8; r++)
                replaySame = replaySame &&
                    moveRef.getD(r) == moveNative.getD(r) &&
                    moveRef.getA(r) == moveNative.getA(r);
            replaySame = replaySame &&
                moveRef.getPC() == moveNative.getPC() &&
                moveRef.getPC0() == moveNative.getPC0() &&
                moveRef.getIRD() == moveNative.getIRD() &&
                moveRef.getIRC() == moveNative.getIRC() &&
                moveRef.getSR() == moveNative.getSR() &&
                moveRef.getClock() == moveNative.getClock() &&
                moveRef.mmioReads == moveNative.mmioReads;
            if (!replaySame)
                std::printf("    2470 MMIO replay divergence at checkpoint %d\n",
                            step);
        }
        const auto replayStats = moveNative.jit.stats().snapshot();
        check(replaySame && moveNative.getA(2) == 0xCAFEBABE &&
              moveNative.mmioReads != 0 &&
              (!nativeProduction ||
               replayStats.slowInstrs > moveStats.slowInstrs),
              "2470 non-plain final operand replays before its An commit");
    }

    // Speedometer's 4A76 is TST.W ([40,A6],4): a plain longword pointer
    // read followed by a word operand read, then flags. Keep both reads in
    // one replayable transaction. Either MMIO position and a final /BERR
    // must remain wholly owned by untouched Moira replay.
    {
        FaultCpu tstRef, tstNative;
        installSpeedometerIndirectTstLoop(tstRef);
        installSpeedometerIndirectTstLoop(tstNative);
        tstRef.reset(); tstNative.reset();
        tstRef.setTC(12u << 20); tstNative.setTC(12u << 20);
        for (FaultCpu* c : {&tstRef, &tstNative}) {
            c->setA(6, kBitfieldData);
            c->setSR(0x2710);                 // TST preserves X
        }
        tstNative.jit.setEnabled(true);

        const auto sameBoundary = [&] {
            bool same = true;
            for (int r = 0; r < 8; r++)
                same = same && tstRef.getD(r) == tstNative.getD(r) &&
                       tstRef.getA(r) == tstNative.getA(r);
            return same && tstRef.getPC() == tstNative.getPC() &&
                   tstRef.getPC0() == tstNative.getPC0() &&
                   tstRef.getIRD() == tstNative.getIRD() &&
                   tstRef.getIRC() == tstNative.getIRC() &&
                   tstRef.getSR() == tstNative.getSR() &&
                   tstRef.getClock() == tstNative.getClock();
        };

        bool ramSame = true;
        for (int step = 0; step < 256 && ramSame; step++) {
            const int64_t target = tstRef.getClock() + 43;
            tstRef.executeUntil(target);
            tstNative.jit.executeUntil(target);
            ramSame = sameBoundary();
            if (!ramSame)
                std::printf("    030 4A76 RAM divergence at checkpoint %d\n",
                            step);
        }
        const auto ramStats = tstNative.jit.stats().snapshot();
        check(ramSame && (tstNative.getSR() & 0x1F) == 0x18 &&
              ramStats.blocksCompiled != 0 && ramStats.blocksRun != 0 &&
              ramStats.slowInstrs == 0,
              "Speedometer 4A76 full-indirect TST stays native in RAM");

        // A non-plain final operand is discovered only after the proved RAM
        // pointer has been read. Repeating that RAM read in the fallback is
        // harmless; the device word itself must still be read exactly once.
        for (FaultCpu* c : {&tstRef, &tstNative}) {
            put32(*c, kBitfieldData + 0x28, kMmio - 4);
            put16(*c, kMmio, 0x0000);
            c->mmioReads = 0;
            c->mmioReadDelay = 13;
        }
        bool operandMmioSame = true;
        for (int step = 0; step < 32 && operandMmioSame; step++) {
            const int64_t target = tstRef.getClock() + 43;
            tstRef.executeUntil(target);
            tstNative.jit.executeUntil(target);
            operandMmioSame = sameBoundary() &&
                tstRef.mmioReads == tstNative.mmioReads;
        }
        const auto operandMmioStats = tstNative.jit.stats().snapshot();
        check(operandMmioSame && tstNative.mmioReads != 0 &&
              operandMmioStats.slowInstrs > ramStats.slowInstrs,
              "4A76 non-plain operand is read only by pristine replay");

        // The pointer itself may also be a device read. The native path must
        // refuse before consuming even its first half.
        for (FaultCpu* c : {&tstRef, &tstNative}) {
            c->setA(6, kMmio - 0x28);
            put32(*c, kMmio, kBitfieldData + 0xFC);
            put16(*c, kBitfieldData + 0x100, 0x8001);
            c->mmioReads = 0;
        }
        bool pointerMmioSame = true;
        for (int step = 0; step < 32 && pointerMmioSame; step++) {
            const int64_t target = tstRef.getClock() + 43;
            tstRef.executeUntil(target);
            tstNative.jit.executeUntil(target);
            pointerMmioSame = sameBoundary() &&
                tstRef.mmioReads == tstNative.mmioReads;
        }
        check(pointerMmioSame && tstNative.mmioReads != 0,
              "4A76 non-plain pointer is read only by pristine replay");

        const auto prepareTstFault = [](FaultCpu& c) {
            put32(c, kBitfieldData + 0x28, kHole - 4);
            c.setPC(kCode); c.setPC0(kCode);
            c.setIRD(0x4A76); c.setIRC(0x8162);
            c.setA(6, kBitfieldData); c.setA(7, kStack);
            c.setSR(0x2710); c.setClock(0);
            c.readFaults = 0; c.mmioReadDelay = 0;
            const auto layout = c.pomJitLayout();
            *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(&c) +
                                         layout.flags) = 0;
        };
        prepareTstFault(tstRef);
        prepareTstFault(tstNative);
        const auto beforeFault = tstNative.jit.stats().snapshot();
        tstRef.executeUntil(1);
        tstNative.jit.executeUntil(1);
        const auto afterFault = tstNative.jit.stats().snapshot();
        const uint32_t refSp = tstRef.getA(7);
        const uint32_t nativeSp = tstNative.getA(7);
        constexpr uint32_t kTstFaultFrameBytes = 92;
        const bool faultSame =
              tstRef.readFaults == 1 && tstNative.readFaults == 1 &&
              afterFault.blocksRun > beforeFault.blocksRun &&
              refSp == kStack - kTstFaultFrameBytes && nativeSp == refSp &&
              std::memcmp(tstRef.mem.data() + refSp,
                          tstNative.mem.data() + nativeSp,
                          kTstFaultFrameBytes) == 0 &&
              get16(tstNative, nativeSp + 6) == 0xB008 &&
              tstRef.getPC() == tstNative.getPC() &&
              tstRef.getClock() == tstNative.getClock();
        if (!faultSame) {
            int firstDiff = -1;
            if (refSp + kTstFaultFrameBytes <= tstRef.mem.size() &&
                nativeSp + kTstFaultFrameBytes <= tstNative.mem.size()) {
                for (uint32_t i = 0; i < kTstFaultFrameBytes; i++)
                    if (tstRef.mem[refSp + i] !=
                        tstNative.mem[nativeSp + i]) {
                        firstDiff = int(i);
                        break;
                    }
            }
            std::printf("    4A76 /BERR refFaults=%d jitFaults=%d "
                        "blocks=%llu->%llu SP=%08X/%08X PC=%08X/%08X "
                        "clock=%lld/%lld frameDiff=%d\n",
                        tstRef.readFaults, tstNative.readFaults,
                        (unsigned long long)beforeFault.blocksRun,
                        (unsigned long long)afterFault.blocksRun,
                        refSp, nativeSp, tstRef.getPC(), tstNative.getPC(),
                        (long long)tstRef.getClock(),
                        (long long)tstNative.getClock(), firstDiff);
        }
        check(faultSame,
              "4A76 /BERR preserves the complete 030 format-$B frame");
    }

    // Speedometer's 4C00 4004 is the non-trapping 32-bit-result form of
    // MULL. Exercise its exact selector with an odd multiplier so D4 keeps
    // changing instead of converging to zero and crosses from V=0 to V=1;
    // X must survive while N/Z/V/C and the 030 boundary remain identical.
    {
        FaultCpu mulRef, mulNative;
        installSpeedometerMuluLongLoop(mulRef);
        installSpeedometerMuluLongLoop(mulNative);
        mulRef.reset(); mulNative.reset();
        mulRef.setTC(12u << 20); mulNative.setTC(12u << 20);
        for (FaultCpu* c : {&mulRef, &mulNative}) {
            c->setD(0, 3);
            c->setD(4, 1);
            c->setSR(0x2710);                     // X set, NZVC clear
        }
        mulNative.jit.setEnabled(true);

        bool same = true, sawV0 = false, sawV1 = false;
        for (int step = 0; step < 256 && same; step++) {
            const int64_t target = mulRef.getClock() + 47;
            mulRef.executeUntil(target);
            mulNative.jit.executeUntil(target);
            for (int r = 0; r < 8; r++)
                same = same && mulRef.getD(r) == mulNative.getD(r) &&
                       mulRef.getA(r) == mulNative.getA(r);
            same = same && mulRef.getPC() == mulNative.getPC() &&
                   mulRef.getPC0() == mulNative.getPC0() &&
                   mulRef.getIRD() == mulNative.getIRD() &&
                   mulRef.getIRC() == mulNative.getIRC() &&
                   mulRef.getSR() == mulNative.getSR() &&
                   mulRef.getClock() == mulNative.getClock();
            sawV0 = sawV0 || (mulNative.getSR() & 0x02u) == 0;
            sawV1 = sawV1 || (mulNative.getSR() & 0x02u) != 0;
            if (!same)
                std::printf("    030 4C00/4004 divergence at checkpoint %d: "
                            "D4=%08X/%08X SR=%04X/%04X PC=%08X/%08X "
                            "PC0=%08X/%08X IRD=%04X/%04X IRC=%04X/%04X "
                            "clock=%lld/%lld\n",
                            step, mulRef.getD(4), mulNative.getD(4),
                            mulRef.getSR(), mulNative.getSR(),
                            mulRef.getPC(), mulNative.getPC(),
                            mulRef.getPC0(), mulNative.getPC0(),
                            mulRef.getIRD(), mulNative.getIRD(),
                            mulRef.getIRC(), mulNative.getIRC(),
                            (long long)mulRef.getClock(),
                            (long long)mulNative.getClock());
        }
        const auto mulStats = mulNative.jit.stats().snapshot();
        const bool nativeProduction =
            (!std::strcmp(mulNative.jit.backendName(), "aarch64") ||
             !std::strcmp(mulNative.jit.backendName(), "x86-64")) &&
            !mulNative.jit.config().packedCcr;
        std::printf("    030 MULU.L compiled=%llu runs=%llu slow=%llu\n",
                    (unsigned long long)mulStats.blocksCompiled,
                    (unsigned long long)mulStats.blocksRun,
                    (unsigned long long)mulStats.slowInstrs);
        check(same && sawV0 && sawV1 && mulNative.getD(4) != 1 &&
              (mulNative.getD(4) & 1u) != 0 &&
              (mulNative.getSR() & 0x10u) != 0 &&
              (mulNative.getSR() & 0x01u) == 0 &&
              mulStats.blocksCompiled != 0 && mulStats.blocksRun != 0 &&
              (!nativeProduction || mulStats.slowInstrs == 0),
              "Speedometer 4C00 4004 MULU.L stays native on 030");
    }

    // Speedometer's later 4C2F 1C00 sites multiply a d16(A7) source into a
    // signed 64-bit D0:D1 result. -3 * INT_MIN is a stable loop value with
    // a non-zero high half, so it pins source memory, signed widening,
    // low-then-high 030 register publication and 64-bit N/Z in one oracle.
    {
        FaultCpu mulRef, mulNative;
        installSpeedometerMulsLongMemoryLoop(mulRef);
        installSpeedometerMulsLongMemoryLoop(mulNative);
        mulRef.reset(); mulNative.reset();
        mulRef.setTC(12u << 20); mulNative.setTC(12u << 20);
        for (FaultCpu* c : {&mulRef, &mulNative}) {
            c->setD(0, 0);
            c->setD(1, 0x8000'0000u);
            c->setSR(0x2710);                     // X set, NZVC clear
        }
        mulNative.jit.setEnabled(true);

        bool same = true;
        for (int step = 0; step < 256 && same; step++) {
            const int64_t target = mulRef.getClock() + 48;
            mulRef.executeUntil(target);
            mulNative.jit.executeUntil(target);
            for (int r = 0; r < 8; r++)
                same = same && mulRef.getD(r) == mulNative.getD(r) &&
                       mulRef.getA(r) == mulNative.getA(r);
            same = same && mulRef.getPC() == mulNative.getPC() &&
                   mulRef.getPC0() == mulNative.getPC0() &&
                   mulRef.getIRD() == mulNative.getIRD() &&
                   mulRef.getIRC() == mulNative.getIRC() &&
                   mulRef.getSR() == mulNative.getSR() &&
                   mulRef.getClock() == mulNative.getClock();
        }
        const auto mulStats = mulNative.jit.stats().snapshot();
        const bool nativeProduction =
            (!std::strcmp(mulNative.jit.backendName(), "aarch64") ||
             !std::strcmp(mulNative.jit.backendName(), "x86-64")) &&
            !mulNative.jit.config().packedCcr;
        std::printf("    030 MULS.L memory compiled=%llu runs=%llu slow=%llu\n",
                    (unsigned long long)mulStats.blocksCompiled,
                    (unsigned long long)mulStats.blocksRun,
                    (unsigned long long)mulStats.slowInstrs);
        check(same && mulNative.getD(0) == 1 &&
              mulNative.getD(1) == 0x8000'0000u &&
              (mulNative.getSR() & 0x10u) != 0 &&
              (mulNative.getSR() & 0x0Fu) == 0 &&
              mulStats.blocksCompiled != 0 && mulStats.blocksRun != 0 &&
              (!nativeProduction || mulStats.slowInstrs == 0),
              "Speedometer 4C2F 1C00 MULS.L memory stays native on 030");
    }

    // Speedometer's E410 and E291 rows are fixed immediate ROXR forms. They
    // rotate X through a 9- or 33-bit ring, then make the last outgoing bit
    // both C and X while preserving the unused upper bits of byte D0.
    {
        FaultCpu roxRef, roxNative;
        installSpeedometerRoxrLoop(roxRef);
        installSpeedometerRoxrLoop(roxNative);
        roxRef.reset(); roxNative.reset();
        roxRef.setTC(12u << 20); roxNative.setTC(12u << 20);
        for (FaultCpu* c : {&roxRef, &roxNative}) {
            c->setD(0, 0x123456B5u);
            c->setD(1, 0x89ABCDEFu);
            c->setSR(0x2710);                     // X begins set
        }
        roxNative.jit.setEnabled(true);

        bool same = true;
        for (int step = 0; step < 256 && same; step++) {
            const int64_t target = roxRef.getClock() + 31;
            roxRef.executeUntil(target);
            roxNative.jit.executeUntil(target);
            for (int r = 0; r < 8; r++)
                same = same && roxRef.getD(r) == roxNative.getD(r) &&
                       roxRef.getA(r) == roxNative.getA(r);
            same = same && roxRef.getPC() == roxNative.getPC() &&
                   roxRef.getPC0() == roxNative.getPC0() &&
                   roxRef.getIRD() == roxNative.getIRD() &&
                   roxRef.getIRC() == roxNative.getIRC() &&
                   roxRef.getSR() == roxNative.getSR() &&
                   roxRef.getClock() == roxNative.getClock();
            if (!same)
                std::printf("    030 E410/E291 divergence at checkpoint %d\n", step);
        }
        const auto roxStats = roxNative.jit.stats().snapshot();
        const bool nativeProduction =
            (!std::strcmp(roxNative.jit.backendName(), "aarch64") ||
             !std::strcmp(roxNative.jit.backendName(), "x86-64")) &&
            !roxNative.jit.config().packedCcr;
        std::printf("    030 ROXR immediate compiled=%llu runs=%llu slow=%llu\n",
                    (unsigned long long)roxStats.blocksCompiled,
                    (unsigned long long)roxStats.blocksRun,
                    (unsigned long long)roxStats.slowInstrs);
        check(same && (roxNative.getD(0) & 0xFFFFFF00u) == 0x12345600u &&
              roxNative.getD(1) != 0x89ABCDEFu &&
              roxStats.blocksCompiled != 0 && roxStats.blocksRun != 0 &&
              (!nativeProduction || roxStats.slowInstrs == 0),
              "Speedometer E410/E291 immediate ROXR stays native on 030");
    }

    // Speedometer's 2191/31A9 pair writes two independent RAM sources to
    // the same brief-indexed destination. Both source and destination must
    // be proved before the first load; a refused write mapping then replays
    // without duplicating a read or publishing flags early.
    {
        FaultCpu moveRef, moveNative;
        installSpeedometerIndexedDestinationLoop(moveRef);
        installSpeedometerIndexedDestinationLoop(moveNative);
        moveRef.reset(); moveNative.reset();
        moveRef.setTC(12u << 20); moveNative.setTC(12u << 20);
        for (FaultCpu* c : {&moveRef, &moveNative}) {
            c->setA(0, kBitfieldData + 0x200);
            c->setA(1, kBitfieldData);
            c->setD(1, 8);
        }
        moveNative.jit.setEnabled(true);

        bool same = true;
        for (int step = 0; step < 256 && same; step++) {
            const int64_t target = moveRef.getClock() + 43;
            moveRef.executeUntil(target);
            moveNative.jit.executeUntil(target);
            for (int r = 0; r < 8; r++)
                same = same && moveRef.getD(r) == moveNative.getD(r) &&
                       moveRef.getA(r) == moveNative.getA(r);
            same = same && moveRef.getPC() == moveNative.getPC() &&
                   moveRef.getPC0() == moveNative.getPC0() &&
                   moveRef.getIRD() == moveNative.getIRD() &&
                   moveRef.getIRC() == moveNative.getIRC() &&
                   moveRef.getSR() == moveNative.getSR() &&
                   moveRef.getClock() == moveNative.getClock() &&
                   get32(moveRef, kBitfieldData + 0x208) ==
                       get32(moveNative, kBitfieldData + 0x208);
            if (!same)
                std::printf("    030 2191/31A9 divergence at checkpoint %d\n",
                            step);
        }
        const auto moveStats = moveNative.jit.stats().snapshot();
        const bool nativeProduction =
            (!std::strcmp(moveNative.jit.backendName(), "aarch64") ||
             !std::strcmp(moveNative.jit.backendName(), "x86-64")) &&
            !moveNative.jit.config().packedCcr;
        const uint32_t destinationValue =
            get32(moveNative, kBitfieldData + 0x208);
        std::printf("    030 indexed-destination compiled=%llu runs=%llu slow=%llu\n",
                    (unsigned long long)moveStats.blocksCompiled,
                    (unsigned long long)moveStats.blocksRun,
                    (unsigned long long)moveStats.slowInstrs);
        // A checkpoint may land after 2191 or after 31A9. The first value is
        // therefore a valid boundary too; the next instruction replaces its
        // high word with ABCD without touching the low word.
        check(same && (destinationValue == 0x12345678 ||
                       destinationValue == 0xABCD5678) &&
              moveStats.blocksCompiled != 0 && moveStats.blocksRun != 0 &&
              (!nativeProduction || moveStats.slowInstrs == 0),
              "Speedometer 2191/31A9 indexed destinations stay native on 030");

        // Move only the destination onto synthetic MMIO. The compiled pair
        // must reject its write mapping before reading the source; untouched
        // interpreter replay owns every long/word callback and boundary.
        for (FaultCpu* c : {&moveRef, &moveNative})
            c->setA(0, kMmio - 8);
        bool replaySame = true;
        for (int step = 0; step < 32 && replaySame; step++) {
            const int64_t target = moveRef.getClock() + 43;
            moveRef.executeUntil(target);
            moveNative.jit.executeUntil(target);
            for (int r = 0; r < 8; r++)
                replaySame = replaySame &&
                    moveRef.getD(r) == moveNative.getD(r) &&
                    moveRef.getA(r) == moveNative.getA(r);
            const auto& refWrite = moveRef.observedWrite;
            const auto& nativeWrite = moveNative.observedWrite;
            replaySame = replaySame &&
                moveRef.getPC() == moveNative.getPC() &&
                moveRef.getPC0() == moveNative.getPC0() &&
                moveRef.getIRD() == moveNative.getIRD() &&
                moveRef.getIRC() == moveNative.getIRC() &&
                moveRef.getSR() == moveNative.getSR() &&
                moveRef.getClock() == moveNative.getClock() &&
                refWrite.count == nativeWrite.count &&
                refWrite.address == nativeWrite.address &&
                refWrite.value == nativeWrite.value &&
                refWrite.bytes == nativeWrite.bytes &&
                refWrite.pc == nativeWrite.pc &&
                refWrite.pc0 == nativeWrite.pc0 &&
                refWrite.ird == nativeWrite.ird &&
                refWrite.irc == nativeWrite.irc &&
                refWrite.sr == nativeWrite.sr &&
                refWrite.clock == nativeWrite.clock;
            if (!replaySame)
                std::printf("    indexed-destination MMIO divergence at checkpoint %d\n",
                            step);
        }
        const auto replayStats = moveNative.jit.stats().snapshot();
        check(replaySame && moveNative.observedWrite.count != 0 &&
              (!nativeProduction ||
               replayStats.slowInstrs > moveStats.slowInstrs),
              "indexed MMIO destination replays before source/flags escape");
    }

    // Speedometer's 3F5F/2F5F stack shuffles are not ordinary two-EA
    // MOVEs: d16(A7) is calculated after the source (A7)+ has advanced.
    // Exercise both sizes in RAM, then redirect only the dependent
    // destination to MMIO and /BERR. The native transaction must refuse
    // before the source read or A7 update and let one pristine Moira replay
    // expose the architecturally updated A7 to the callback/fault frame.
    struct DependentMoveForm {
        uint16_t opcode;
        unsigned bytes;
        const char* name;
    };
    for (const DependentMoveForm form : {
             DependentMoveForm{0x3F5F, 2, "3F5F MOVE.W (A7)+,4(A7)"},
             DependentMoveForm{0x2F5F, 4, "2F5F MOVE.L (A7)+,4(A7)"}}) {
        FaultCpu moveRef, moveNative;
        installSpeedometerDependentMoveLoop(moveRef, form.opcode);
        installSpeedometerDependentMoveLoop(moveNative, form.opcode);
        moveRef.reset(); moveNative.reset();
        moveRef.setTC(12u << 20); moveNative.setTC(12u << 20);
        for (FaultCpu* c : {&moveRef, &moveNative}) {
            c->setISP(kStack);
            c->setSR(0x0710);                 // user A7; faults use valid ISP
            c->setA(7, kBitfieldData);
            for (uint32_t p = 0; p < 0x2000; p++)
                c->mem[kBitfieldData + p] = uint8_t((p * 37 + 11) & 0xFF);
        }
        moveNative.jit.setEnabled(true);

        bool ramSame = true;
        for (int step = 0; step < 96 && ramSame; step++) {
            const int64_t target = moveRef.getClock() + 43;
            moveRef.executeUntil(target);
            moveNative.jit.executeUntil(target);
            for (int r = 0; r < 8; r++)
                ramSame = ramSame &&
                    moveRef.getD(r) == moveNative.getD(r) &&
                    moveRef.getA(r) == moveNative.getA(r);
            ramSame = ramSame &&
                moveRef.getPC() == moveNative.getPC() &&
                moveRef.getPC0() == moveNative.getPC0() &&
                moveRef.getIRD() == moveNative.getIRD() &&
                moveRef.getIRC() == moveNative.getIRC() &&
                moveRef.getSR() == moveNative.getSR() &&
                moveRef.getClock() == moveNative.getClock() &&
                std::memcmp(moveRef.mem.data() + kBitfieldData,
                            moveNative.mem.data() + kBitfieldData,
                            0x2000) == 0;
            if (!ramSame)
                std::printf("    030 %04X dependent MOVE divergence at checkpoint %d\n",
                            form.opcode, step);
        }
        const auto ramStats = moveNative.jit.stats().snapshot();
        const bool nativeProduction =
            (!std::strcmp(moveNative.jit.backendName(), "aarch64") ||
             !std::strcmp(moveNative.jit.backendName(), "x86-64")) &&
            !moveNative.jit.config().packedCcr;
        char what[176];
        std::snprintf(what, sizeof(what), "%s stays native with updated-base destination",
                      form.name);
        check(ramSame && ramStats.blocksCompiled != 0 &&
              ramStats.blocksRun != 0 &&
              (!nativeProduction || ramStats.slowInstrs == 0), what);

        const auto prepareBoundary = [&](FaultCpu& c, uint32_t source) {
            c.setPC(kCode); c.setPC0(kCode);
            c.setIRD(form.opcode); c.setIRC(0x0004);
            c.setISP(kStack); c.setSR(0x0710); c.setA(7, source);
            c.setClock(0);
            c.writeFaults = c.readFaults = c.mmioReads = 0;
            c.observedWrite = {};
            const auto layout = c.pomJitLayout();
            *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(&c) +
                                         layout.flags) = 0;
        };
        const auto putSource = [&](FaultCpu& c, uint32_t source) {
            if (form.bytes == 2) put16(c, source, 0x89AB);
            else put32(c, source, 0x89ABCDEF);
        };

        // old A7 + source step + displacement == first MMIO byte.
        const uint32_t mmioSource = kMmio - form.bytes - 4;
        for (FaultCpu* c : {&moveRef, &moveNative}) {
            prepareBoundary(*c, mmioSource);
            putSource(*c, mmioSource);
        }
        const auto beforeMmio = moveNative.jit.stats().snapshot();
        moveRef.executeUntil(1);
        moveNative.jit.executeUntil(1);
        const auto afterMmio = moveNative.jit.stats().snapshot();
        bool mmioSame = true;
        for (int r = 0; r < 8; r++)
            mmioSame = mmioSame &&
                moveRef.getD(r) == moveNative.getD(r) &&
                moveRef.getA(r) == moveNative.getA(r);
        const auto& refWrite = moveRef.observedWrite;
        const auto& nativeWrite = moveNative.observedWrite;
        mmioSame = mmioSame &&
            moveRef.getPC() == moveNative.getPC() &&
            moveRef.getPC0() == moveNative.getPC0() &&
            moveRef.getIRD() == moveNative.getIRD() &&
            moveRef.getIRC() == moveNative.getIRC() &&
            moveRef.getSR() == moveNative.getSR() &&
            moveRef.getClock() == moveNative.getClock() &&
            refWrite.count == nativeWrite.count &&
            refWrite.address == nativeWrite.address &&
            refWrite.value == nativeWrite.value &&
            refWrite.bytes == nativeWrite.bytes &&
            refWrite.a7 == nativeWrite.a7 &&
            refWrite.pc == nativeWrite.pc &&
            refWrite.pc0 == nativeWrite.pc0 &&
            refWrite.ird == nativeWrite.ird &&
            refWrite.irc == nativeWrite.irc &&
            refWrite.sr == nativeWrite.sr &&
            refWrite.clock == nativeWrite.clock;
        std::snprintf(what, sizeof(what), "%s MMIO replay observes incremented A7",
                      form.name);
        check(mmioSame && refWrite.count != 0 &&
              refWrite.a7 == mmioSource + form.bytes &&
              (!nativeProduction ||
               (afterMmio.blocksRun > beforeMmio.blocksRun &&
                afterMmio.slowInstrs > beforeMmio.slowInstrs)), what);

        // The destination mapping faults, but the source has already
        // postincremented USP architecturally. Native code must preflight
        // and replay, yielding one fault and the exact format-$A frame.
        const uint32_t berrSource = kHole - form.bytes - 4;
        for (FaultCpu* c : {&moveRef, &moveNative}) {
            prepareBoundary(*c, berrSource);
            putSource(*c, berrSource);
        }
        const auto beforeBerr = moveNative.jit.stats().snapshot();
        moveRef.executeUntil(1);
        moveNative.jit.executeUntil(1);
        const auto afterBerr = moveNative.jit.stats().snapshot();
        const uint32_t refSp = moveRef.getA(7);
        const uint32_t nativeSp = moveNative.getA(7);
        const int frameDiff = refSp == kStack - 32 && nativeSp == refSp
            ? std::memcmp(moveRef.mem.data() + refSp,
                          moveNative.mem.data() + nativeSp, 32)
            : -1;
        const bool berrExact =
            moveRef.writeFaults == 1 && moveNative.writeFaults == 1 &&
            moveRef.getUSP() == berrSource + form.bytes &&
            moveNative.getUSP() == moveRef.getUSP() &&
            refSp == kStack - 32 && nativeSp == refSp &&
            frameDiff == 0 &&
            moveRef.getPC() == moveNative.getPC() &&
            moveRef.getClock() == moveNative.getClock() &&
            (!nativeProduction ||
             afterBerr.blocksRun > beforeBerr.blocksRun);
        if (!berrExact)
            std::printf("    %04X BERR faults=%d/%d usp=%08X/%08X want=%08X "
                        "sp=%08X/%08X pc=%08X/%08X clk=%lld/%lld "
                        "runs=%llu->%llu slow=%llu->%llu frame=%d\n",
                        form.opcode, moveRef.writeFaults, moveNative.writeFaults,
                        moveRef.getUSP(), moveNative.getUSP(),
                        berrSource + form.bytes, refSp, nativeSp,
                        moveRef.getPC(), moveNative.getPC(),
                        (long long)moveRef.getClock(),
                        (long long)moveNative.getClock(),
                        (unsigned long long)beforeBerr.blocksRun,
                        (unsigned long long)afterBerr.blocksRun,
                        (unsigned long long)beforeBerr.slowInstrs,
                        (unsigned long long)afterBerr.slowInstrs,
                        frameDiff);
        std::snprintf(what, sizeof(what), "%s destination /BERR frame is exact",
                      form.name);
        check(berrExact, what);
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

    // The 030 admission is a separate policy switch from the 040 emitter.
    // Exercise the four tailless memory-write actions directly so the hot
    // EFD1 path cannot be declared from a 040-only synthetic proof.
    {
        FaultCpu bfRef, bfNative;
        installBitfieldWriteLoop(bfRef); installBitfieldWriteLoop(bfNative);
        bfRef.reset(); bfNative.reset();
        bfRef.setTC(12u << 20); bfNative.setTC(12u << 20);
        for (FaultCpu* c : {&bfRef, &bfNative}) {
            c->setA(0, kBitfieldData + 4);
            c->setA(1, kBitfieldData + 4);
        }
        bfNative.jit.setEnabled(true);

        bool same = true;
        for (int step = 0; step < 256 && same; step++) {
            const int64_t target = bfRef.getClock() + 43;
            bfRef.executeUntil(target);
            bfNative.jit.executeUntil(target);
            for (int r = 0; r < 8; r++)
                same = same && bfRef.getD(r) == bfNative.getD(r) &&
                       bfRef.getA(r) == bfNative.getA(r);
            same = same && bfRef.getPC() == bfNative.getPC() &&
                   bfRef.getPC0() == bfNative.getPC0() &&
                   bfRef.getIRD() == bfNative.getIRD() &&
                   bfRef.getIRC() == bfNative.getIRC() &&
                   bfRef.getSR() == bfNative.getSR() &&
                   bfRef.getClock() == bfNative.getClock() &&
                   std::memcmp(bfRef.mem.data() + kBitfieldData - 4,
                               bfNative.mem.data() + kBitfieldData - 4,
                               24) == 0;
            if (!same)
                std::printf("    030 bitfield-write divergence at checkpoint %d\n",
                            step);
        }
        const auto bfStats = bfNative.jit.stats().snapshot();
        const bool nativeProduction =
            (!std::strcmp(bfNative.jit.backendName(), "aarch64") ||
             !std::strcmp(bfNative.jit.backendName(), "x86-64")) &&
            !bfNative.jit.config().packedCcr;
        std::printf("    030 bitfield-write compiled=%llu runs=%llu native=%llu slow=%llu\n",
                    (unsigned long long)bfStats.blocksCompiled,
                    (unsigned long long)bfStats.blocksRun,
                    (unsigned long long)bfStats.instrs,
                    (unsigned long long)bfStats.slowInstrs);
        check(same && bfStats.blocksCompiled != 0 && bfStats.blocksRun != 0 &&
              (!nativeProduction || bfStats.slowInstrs == 0),
              "tailless memory bitfield writes stay native and exact on native 030");
    }

    // Speedometer's E9D4 uses a dynamic offset and width that may require a
    // fifth byte. Exercise two real tail paths plus the same compiled shape's
    // runtime no-tail arm; both mappings must be proved before the first load.
    {
        FaultCpu bfRef, bfNative;
        installSpeedometerBitfieldTailLoop(bfRef);
        installSpeedometerBitfieldTailLoop(bfNative);
        bfRef.reset(); bfNative.reset();
        bfRef.setTC(12u << 20); bfNative.setTC(12u << 20);
        bfRef.setA(4, kBitfieldData + 4);
        bfNative.setA(4, kBitfieldData + 4);
        bfNative.jit.setEnabled(true);

        bool same = true;
        for (int step = 0; step < 256 && same; step++) {
            const int64_t target = bfRef.getClock() + 43;
            bfRef.executeUntil(target);
            bfNative.jit.executeUntil(target);
            for (int r = 0; r < 8; r++)
                same = same && bfRef.getD(r) == bfNative.getD(r) &&
                       bfRef.getA(r) == bfNative.getA(r);
            same = same && bfRef.getPC() == bfNative.getPC() &&
                   bfRef.getPC0() == bfNative.getPC0() &&
                   bfRef.getIRD() == bfNative.getIRD() &&
                   bfRef.getIRC() == bfNative.getIRC() &&
                   bfRef.getSR() == bfNative.getSR() &&
                   bfRef.getClock() == bfNative.getClock();
            if (!same)
                std::printf("    030 E9D4 tail divergence at checkpoint %d\n",
                            step);
        }
        const auto bfStats = bfNative.jit.stats().snapshot();
        const bool nativeProduction =
            (!std::strcmp(bfNative.jit.backendName(), "aarch64") ||
             !std::strcmp(bfNative.jit.backendName(), "x86-64")) &&
            !bfNative.jit.config().packedCcr;
        check(same && bfStats.blocksCompiled != 0 && bfStats.blocksRun != 0 &&
              (!nativeProduction || bfStats.slowInstrs == 0),
              "Speedometer E9D4 tail/no-tail reads stay native on the 030 default");
    }

    // Speedometer's 41F6 is a full-format postindexed LEA: one direct-RAM
    // pointer read followed by register-only address formation. A failed
    // mapping must replay before A0 changes; a proved mapping stays native.
    {
        FaultCpu leaRef, leaNative;
        installSpeedometerIndirectLeaLoop(leaRef);
        installSpeedometerIndirectLeaLoop(leaNative);
        leaRef.reset(); leaNative.reset();
        leaRef.setTC(12u << 20); leaNative.setTC(12u << 20);
        for (FaultCpu* c : {&leaRef, &leaNative}) {
            c->setA(6, kBitfieldData);
            c->setD(6, 0x20);
        }
        leaNative.jit.setEnabled(true);

        bool same = true;
        for (int step = 0; step < 256 && same; step++) {
            const int64_t target = leaRef.getClock() + 43;
            leaRef.executeUntil(target);
            leaNative.jit.executeUntil(target);
            for (int r = 0; r < 8; r++)
                same = same && leaRef.getD(r) == leaNative.getD(r) &&
                       leaRef.getA(r) == leaNative.getA(r);
            same = same && leaRef.getPC() == leaNative.getPC() &&
                   leaRef.getPC0() == leaNative.getPC0() &&
                   leaRef.getIRD() == leaNative.getIRD() &&
                   leaRef.getIRC() == leaNative.getIRC() &&
                   leaRef.getSR() == leaNative.getSR() &&
                   leaRef.getClock() == leaNative.getClock();
            if (!same)
                std::printf("    030 41F6 divergence at checkpoint %d\n",
                            step);
        }
        const auto leaStats = leaNative.jit.stats().snapshot();
        const bool nativeProduction =
            (!std::strcmp(leaNative.jit.backendName(), "aarch64") ||
             !std::strcmp(leaNative.jit.backendName(), "x86-64")) &&
            !leaNative.jit.config().packedCcr;
        check(same && leaNative.getA(0) == kBitfieldData + 0x120 &&
              leaStats.blocksCompiled != 0 && leaStats.blocksRun != 0 &&
              (!nativeProduction || leaStats.slowInstrs == 0),
              "Speedometer 41F6 full-indirect LEA stays native and exact on 030");

        // Move the already-compiled pointer EA onto synthetic MMIO. The
        // direct-only admission must now replay before changing A0, leaving
        // the model to own both longword halves and their live delay.
        for (FaultCpu* c : {&leaRef, &leaNative}) {
            c->setA(6, kMmio - 0x10);
            put32(*c, kMmio, kBitfieldData + 0x180);
            c->mmioReadDelay = 11;
        }
        bool replaySame = true;
        for (int step = 0; step < 32 && replaySame; step++) {
            const int64_t target = leaRef.getClock() + 43;
            leaRef.executeUntil(target);
            leaNative.jit.executeUntil(target);
            for (int r = 0; r < 8; r++)
                replaySame = replaySame &&
                    leaRef.getD(r) == leaNative.getD(r) &&
                    leaRef.getA(r) == leaNative.getA(r);
            replaySame = replaySame &&
                leaRef.getPC() == leaNative.getPC() &&
                leaRef.getPC0() == leaNative.getPC0() &&
                leaRef.getIRD() == leaNative.getIRD() &&
                leaRef.getIRC() == leaNative.getIRC() &&
                leaRef.getSR() == leaNative.getSR() &&
                leaRef.getClock() == leaNative.getClock() &&
                leaRef.mmioReads == leaNative.mmioReads;
        }
        const auto replayStats = leaNative.jit.stats().snapshot();
        check(replaySame && leaNative.getA(0) == kBitfieldData + 0x1A0 &&
              leaNative.mmioReads != 0 &&
              (!nativeProduction ||
               replayStats.slowInstrs > leaStats.slowInstrs),
              "41F6 non-plain pointer replays before its An commit");
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

    const auto checkQueueLoop = [&](QueueTransfer transfer,
                                    uint16_t expectedIrc,
                                    int64_t expectedFetches,
                                    const char* name) {
        FaultCpu queueRef, queueNative;
        installQueueLoop(queueRef, transfer);
        installQueueLoop(queueNative, transfer);
        queueRef.reset(); queueNative.reset();
        queueRef.setTC(12u << 20); queueNative.setTC(12u << 20);
        queueRef.enableIcacheOverlay(); queueNative.enableIcacheOverlay();
        queueNative.jit.setEnabled(true);
        queueNative.jit.executeUntil(queueNative.getClock() + 256);
        const auto trainedQueue = queueNative.jit.stats().snapshot();

        queueRef.mem = queueNative.mem;
        prepareFault(queueRef); prepareFault(queueNative);
        queueRef.setA(6, 0x004000); queueNative.setA(6, 0x004000);
        // Keep the compiled block, but give both engines the same cold cache
        // and zeroed counters for the one-instruction comparison.
        queueRef.enableIcacheOverlay(); queueNative.enableIcacheOverlay();
        queueRef.executeUntil(queueRef.getClock() + 1);
        queueNative.jit.executeUntil(queueNative.getClock() + 1);
        const auto afterQueue = queueNative.jit.stats().snapshot();
        const auto refIcache = queueRef.icacheObservation();
        const auto nativeIcache = queueNative.icacheObservation();

        char what[160];
        std::snprintf(what, sizeof(what), "%s compiled and entered native code", name);
        check(trainedQueue.blocksCompiled != 0 &&
              afterQueue.blocksRun > trainedQueue.blocksRun, what);
        std::snprintf(what, sizeof(what), "%s retires without fallback", name);
        check(afterQueue.slowInstrs == trainedQueue.slowInstrs, what);
        std::snprintf(what, sizeof(what), "%s leaves identical PC/IRD/IRC/clock", name);
        check(queueRef.getPC() == queueNative.getPC() &&
              queueRef.getIRD() == queueNative.getIRD() &&
              queueRef.getIRC() == queueNative.getIRC() &&
              queueRef.getClock() == queueNative.getClock(), what);
        std::snprintf(what, sizeof(what), "%s keeps the exact last consumed word in IRC", name);
        const uint16_t expectedIrd = transfer == QueueTransfer::JumpAbsoluteLong
            ? 0x4EF9
            : transfer == QueueTransfer::BranchSubroutineLong ? 0x61FF
            : transfer == QueueTransfer::BranchLong ? 0x60FF : 0x6000;
        check(queueNative.getIRD() == expectedIrd &&
              queueNative.getIRC() == expectedIrc, what);
        if (transfer == QueueTransfer::BranchSubroutineLong) {
            std::snprintf(what, sizeof(what),
                          "%s pushes PC+6 and commits A7 exactly once", name);
            check(queueRef.getA(7) == kStack - 4 &&
                  queueNative.getA(7) == queueRef.getA(7) &&
                  get32(queueRef, kStack - 4) == kCode + 6 &&
                  get32(queueNative, kStack - 4) == kCode + 6, what);
        }
        std::snprintf(what, sizeof(what), "%s reproduces exact i-cache accounting", name);
        check(refIcache.fetches == expectedFetches &&
              nativeIcache.fetches == refIcache.fetches &&
              nativeIcache.hits == refIcache.hits &&
              nativeIcache.misses == refIcache.misses, what);
    };

    checkQueueLoop(QueueTransfer::BranchWord, 0xFFFE, 2,
                   "BRA.W taken exit");
    checkQueueLoop(QueueTransfer::BranchLong, 0xFFFE, 3,
                   "BRA.L taken exit");
    checkQueueLoop(QueueTransfer::BranchSubroutineLong, 0xFFFE, 3,
                   "BSR.L taken exit");
    checkQueueLoop(QueueTransfer::JumpAbsoluteLong, uint16_t(kCode), 3,
                   "JMP (xxx).L exit");

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
            c->setA(2, kSubroutine);
            c->setA(4, kSubroutine);
            c->setA(7, kStack);
            c->setD(0, 0);
            c->setD(1, 0);
            c->setD(7, 0);
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
        put16(jsrRef, kSubroutine, 0x5487);    // ADDQ.L #2,D7
        put16(jsrNative, kSubroutine, 0x5487);
        if (jsrNative.guard) jsrNative.guard->note(kSubroutine, 2);
        const bool patchedExact = runJsrLockstep(64);
        const auto warmedJsr = jsrNative.jit.stats().snapshot();
        const bool steadyExact = runJsrLockstep(64);
        const auto afterJsr = jsrNative.jit.stats().snapshot();

        check(trainedExact && patchedExact && steadyExact,
              "JSR d16/abs.l/brief-indexed keep exact state after target patching");
        check(trainedJsr.blocksCompiled != 0 &&
              afterJsr.blocksRun > trainedJsr.blocksRun &&
              afterJsr.slowInstrs == warmedJsr.slowInstrs,
              "patched simple/brief JSR callers stay native with live target words");

        // The native transaction proves the push, reads the target word,
        // then stores. If the stack aliases that word, Moira's architectural
        // push-before-read order is observable and the caller must replay.
        constexpr uint32_t aliasTarget = kBitfieldData + 0x100;
        const auto prepareAlias = [&](FaultCpu& c) {
            put16(c, aliasTarget, 0x4E71);
            c.setA(0, aliasTarget);           // d16(A0) target
            c.setA(7, aliasTarget + 4);       // pushed long starts at target
            c.setPC(kCode); c.setPC0(kCode);
            c.setIRD(0x4EA8); c.setIRC(0x0000);
            c.setSR(0x2700); c.setClock(0);
            const auto layout = c.pomJitLayout();
            *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(&c) +
                                         layout.flags) = 0;
        };
        prepareAlias(jsrRef); prepareAlias(jsrNative);
        const auto beforeAlias = jsrNative.jit.stats().snapshot();
        jsrRef.executeUntil(1);
        jsrNative.jit.executeUntil(1);
        const auto afterAlias = jsrNative.jit.stats().snapshot();
        check(jsrRef.getPC() == jsrNative.getPC() &&
              jsrRef.getIRD() == jsrNative.getIRD() &&
              jsrRef.getIRC() == jsrNative.getIRC() &&
              jsrRef.getA(7) == jsrNative.getA(7) &&
              jsrRef.getClock() == jsrNative.getClock() &&
              std::memcmp(jsrRef.mem.data() + aliasTarget,
                          jsrNative.mem.data() + aliasTarget, 4) == 0,
              "aliased JSR stack/target preserves push-before-read order");
        check(afterAlias.blocksRun > beforeAlias.blocksRun &&
              afterAlias.slowInstrs == beforeAlias.slowInstrs + 1,
              "aliased JSR range guard replays before either native access");
    }

    // Speedometer 4's dominant $4EB0 sites are full-format preindexed
    // calls. Exercise both observed extension lengths, then force each
    // transactional escape: an unprovable MMIO pointer, a pointer-read bus
    // fault, and a stack/target alias whose push-before-read order matters.
    {
        FaultCpu jsrRef, jsrNative;
        installFullIndirectJsrLoop(jsrRef);
        installFullIndirectJsrLoop(jsrNative);
        jsrRef.reset(); jsrNative.reset();
        jsrRef.setTC(12u << 20); jsrNative.setTC(12u << 20);
        for (FaultCpu* c : {&jsrRef, &jsrNative}) {
            c->setD(0, 0);
            c->setD(1, 0);
            c->setD(2, (kBitfieldData - 0x10) / 4);
            c->setA(0, kBitfieldData);
            c->setA(7, kStack);
            c->setSR(0x2700);
        }
        jsrNative.jit.setEnabled(true);

        const auto sameBoundary = [&] {
            bool same = true;
            for (int r = 0; r < 8; r++)
                same = same && jsrRef.getD(r) == jsrNative.getD(r) &&
                       jsrRef.getA(r) == jsrNative.getA(r);
            return same && jsrRef.getPC() == jsrNative.getPC() &&
                   jsrRef.getPC0() == jsrNative.getPC0() &&
                   jsrRef.getIRD() == jsrNative.getIRD() &&
                   jsrRef.getIRC() == jsrNative.getIRC() &&
                   jsrRef.getSR() == jsrNative.getSR() &&
                   jsrRef.getClock() == jsrNative.getClock();
        };
        bool fullExact = true;
        for (int step = 0; step < 128 && fullExact; step++) {
            const int64_t target = jsrRef.getClock() + 37;
            jsrRef.executeUntil(target);
            jsrNative.jit.executeUntil(target);
            fullExact = sameBoundary() &&
                std::memcmp(jsrRef.mem.data() + kStack - 8,
                            jsrNative.mem.data() + kStack - 8, 8) == 0;
        }
        const auto fullStats = jsrNative.jit.stats().snapshot();
        check(fullExact,
              "Speedometer full-indirect JSR forms stay exact in 030 lockstep");
        check(fullStats.blocksCompiled != 0 && fullStats.blocksRun != 0 &&
              fullStats.slowInstrs == 0,
              "Speedometer $2591/$25A1 JSR forms execute wholly natively");

        const auto preparePointerCall = [&](FaultCpu& c, uint32_t pointer) {
            c.setPC(kCode + 16); c.setPC0(kCode + 16);
            c.setIRD(0x4EB0); c.setIRC(0x1D11);
            c.setD(0, 0); c.setD(1, 0); c.setA(0, pointer);
            c.setA(7, kStack); c.setSR(0x2700); c.setClock(0);
            const auto layout = c.pomJitLayout();
            *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(&c) +
                                         layout.flags) = 0;
        };

        put32(jsrRef, kMmio, kSubroutine);
        put32(jsrNative, kMmio, kSubroutine);
        preparePointerCall(jsrRef, kMmio);
        preparePointerCall(jsrNative, kMmio);
        jsrRef.mmioReads = jsrNative.mmioReads = 0;
        const auto beforeMmio = jsrNative.jit.stats().snapshot();
        jsrRef.executeUntil(1);
        jsrNative.jit.executeUntil(1);
        const auto afterMmio = jsrNative.jit.stats().snapshot();
        check(sameBoundary() && jsrRef.mmioReads == jsrNative.mmioReads &&
              jsrRef.mmioReads != 0,
              "full-indirect MMIO pointer is read only by the single replay");
        check(afterMmio.blocksRun > beforeMmio.blocksRun &&
              afterMmio.slowInstrs == beforeMmio.slowInstrs + 1,
              "MMIO pointer refusal replays the pristine full-indirect JSR");

        preparePointerCall(jsrRef, kHole);
        preparePointerCall(jsrNative, kHole);
        jsrRef.readFaults = jsrNative.readFaults = 0;
        const auto beforeHole = jsrNative.jit.stats().snapshot();
        jsrRef.executeUntil(1);
        jsrNative.jit.executeUntil(1);
        const auto afterHole = jsrNative.jit.stats().snapshot();
        const uint32_t refFaultSp = jsrRef.getA(7);
        const uint32_t nativeFaultSp = jsrNative.getA(7);
        check(jsrRef.readFaults == 1 && jsrNative.readFaults == 1 &&
              refFaultSp == nativeFaultSp &&
              std::memcmp(jsrRef.mem.data() + refFaultSp,
                          jsrNative.mem.data() + nativeFaultSp, 32) == 0,
              "pointer-read fault preserves the exact 030 restart frame");
        check(afterHole.blocksRun > beforeHole.blocksRun &&
              afterHole.slowInstrs == beforeHole.slowInstrs,
              "pointer-hole preflight faults only during untouched replay");

        constexpr uint32_t aliasTarget = kBitfieldData + 0x180;
        put32(jsrRef, kBitfieldData, aliasTarget);
        put32(jsrNative, kBitfieldData, aliasTarget);
        put16(jsrRef, aliasTarget, 0x4E71);
        put16(jsrNative, aliasTarget, 0x4E71);
        preparePointerCall(jsrRef, kBitfieldData);
        preparePointerCall(jsrNative, kBitfieldData);
        jsrRef.setA(7, aliasTarget + 4);
        jsrNative.setA(7, aliasTarget + 4);
        const auto beforeAlias = jsrNative.jit.stats().snapshot();
        jsrRef.executeUntil(1);
        jsrNative.jit.executeUntil(1);
        const auto afterAlias = jsrNative.jit.stats().snapshot();
        check(sameBoundary() &&
              std::memcmp(jsrRef.mem.data() + aliasTarget,
                          jsrNative.mem.data() + aliasTarget, 4) == 0,
              "full-indirect alias preserves architectural push-before-read");
        check(afterAlias.blocksRun > beforeAlias.blocksRun &&
              afterAlias.slowInstrs == beforeAlias.slowInstrs + 1,
              "full-indirect alias guard replays before pointer/push effects");
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
