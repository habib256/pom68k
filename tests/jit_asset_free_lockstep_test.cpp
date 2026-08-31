// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Short, deterministic, asset-free lockstep for the native 68040 JIT.
// Unlike the machine boot lockstep this gate needs no ROM, disk, GUI or
// peripheral model. It proves the daily architectural floor on both native
// hosts: registers/CCR/queue/clock, direct RAM writes and EA commits over a
// linked multi-block loop, restart-vs-last-write /BERR boundaries and their
// complete format-$7 stack frames, brief-vs-full indexed-EA admission, and
// exact write-guard slice indexing. Classic bit operations additionally pin
// the irregular legal-EA mask and the prove-write-before-read RMW protocol.

#include "Moira.h"
#include "jit/JitEngine.h"
#include "JitTestConfig.h"
#include "jit/JitMetrics.h"

#include <chrono>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace jit {

// Rebuild the complete guard index from the live block cache, then compare
// keys, sub-slice masks and 4 KB DTLB exclusion flags. This is deliberately
// stronger than sampling pageMap: a stale key is invisible to recomputation
// once its block is gone, yet that exact stale key caused the 4.4 GB soak
// leak fixed on 2026-08-22.
struct EngineGuardIndexProbe {
    static bool consistent(const Engine& e, uint64_t* entries = nullptr) {
        std::unordered_map<uint32_t, std::unordered_set<uint64_t>> expected;
        std::unordered_map<uint32_t, uint8_t> expectedMasks;
        std::unordered_map<uint64_t, uint32_t> expectedShiftVersions;
        uint64_t expectedEntries = 0;

        for (const auto& [key, block] : e.blocks_) {
            if (block.shiftVersion != 0xFF) {
                if (!Engine::isShiftVersionKey(key) ||
                    Engine::shiftVersionFromKey(key) != block.shiftVersion)
                    return false;
                expectedShiftVersions[ShiftVersionCache::baseKey(key)] |=
                    uint32_t(1) << block.shiftVersion;
            } else if (Engine::isShiftVersionKey(key)) {
                return false;
            }
            uint32_t lo = 0, len = 0;
            Engine::blockSpan(block.ir, lo, len);
            if (!len) return false;
            const uint32_t hi = lo + len - 1;
            const uint32_t first = lo >> CodeGuard::kShift;
            const uint32_t last = hi >> CodeGuard::kShift;
            for (uint32_t slice = first;
                 slice <= last && slice < e.pageMap_.size(); slice++) {
                if (!expected[slice].insert(key).second) return false;
                const uint32_t sliceLo = slice << CodeGuard::kShift;
                const uint32_t sliceHi = sliceLo + CodeGuard::kUnit - 1;
                expectedMasks[slice] |= CodeGuard::subMask(
                    lo > sliceLo ? lo : sliceLo,
                    hi < sliceHi ? hi : sliceHi);
                expectedEntries++;
            }
        }

        uint64_t actualEntries = 0;
        if (e.sliceIndex_.size() != expected.size()) return false;
        for (const auto& [slice, keys] : e.sliceIndex_) {
            auto want = expected.find(slice);
            if (want == expected.end() || keys.size() != want->second.size())
                return false;
            std::unordered_set<uint64_t> unique;
            for (uint64_t key : keys) {
                if (!unique.insert(key).second || !want->second.count(key))
                    return false;
                actualEntries++;
            }
        }
        if (actualEntries != expectedEntries) return false;
        if (entries) *entries = actualEntries;
        if (e.shiftVersions_.masks() != expectedShiftVersions) return false;

        for (uint32_t slice = 0; slice < e.pageMap_.size(); slice++) {
            auto mark = expectedMasks.find(slice);
            const uint8_t want = mark == expectedMasks.end() ? 0 : mark->second;
            if (e.pageMap_[slice] != want) return false;
        }
        for (uint32_t page = 0; page < e.codePage_.size(); page++) {
            bool marked = false;
            const uint32_t first = (page << 12) >> CodeGuard::kShift;
            const uint32_t count = 4096 >> CodeGuard::kShift;
            for (uint32_t i = first;
                 i < first + count && i < e.pageMap_.size(); i++)
                if (expectedMasks.count(i)) { marked = true; break; }
            if (bool(e.codePage_[page]) != marked) return false;
        }
        return true;
    }

    static void dump(const Engine& e) {
        for (const auto& [key, block] : e.blocks_) {
            uint32_t lo = 0, len = 0;
            Engine::blockSpan(block.ir, lo, len);
            std::printf("      key=%08llX entry=%08X span=%08X+%u\n",
                        (unsigned long long)key, block.ir.entryPc, lo, len);
        }
        for (const auto& [slice, keys] : e.sliceIndex_)
            std::printf("      slice=%08X mask=%02X keys=%zu\n",
                        slice, e.pageMap_[slice], keys.size());
    }

    static uint32_t shiftVersionMask(const Engine& e, uint32_t pc,
                                     bool super = true) {
        return e.shiftVersions_.mask(Engine::key(pc, super));
    }

    static unsigned compiledShiftVersions(const Engine& e, uint32_t pc) {
        unsigned result = 0;
        for (const auto& [blockKey, block] : e.blocks_) {
            (void)blockKey;
            if (block.ir.entryPc == pc && block.shiftVersion != 0xFF &&
                block.code)
                result++;
        }
        return result;
    }

    static bool shiftTargetUnpublished(const Engine& e, uint32_t pc,
                                       bool super = true) {
        const Engine::LinkSlot& slot = e.linkTable_[Engine::linkIndex(pc)];
        return slot.tag != (pc | uint32_t(super));
    }

    static bool knowsShiftSite(const Engine& e, uint32_t pc,
                               bool super = true) {
        return e.shiftVersions_.knows(Engine::key(pc, super));
    }
};

}  // namespace jit

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
constexpr uint32_t kDevice = 0x00500000;
constexpr uint32_t kGuardA = kCode;
constexpr uint32_t kGuardB = kCode + 0x40;
constexpr uint32_t kGuardCross = kCode + 0xFC;

const jit::ResolvedConfig& injectedJitConfig() {
    static const jit::ResolvedConfig config =
        testjit::resolveFromEnvironment();
    return config;
}

class SyntheticCpu final : public moira::Moira {
public:
    explicit SyntheticCpu(const jit::ResolvedConfig& config =
                              injectedJitConfig())
        : mem(kRamBytes, 0), jit(*this, hooks(this), jit::kGuest68040,
                                 config) {
        setModel(moira::Model::M68040);
    }

    std::vector<uint8_t> mem;
    // Engine construction may enable the production 68040 JIT immediately
    // and attach its guard through hooks(). Keep the callback destination
    // constructed first; declaring it after `jit` would overwrite that
    // attachment with this default initializer.
    jit::CodeGuard* guard = nullptr;
    jit::Engine jit;
    uint32_t busFaults = 0;
    uint32_t deviceReads = 0;
    uint32_t deviceWrites = 0;
    uint8_t deviceValue = 0xA5;

    static uint32_t bus(uint32_t a) { return a & kRamMask; }
    static bool inHole(uint32_t a) {
        const uint32_t p = bus(a);
        return p >= kHole && p < kHole + 0x10000;
    }
    static bool inDevice(uint32_t a) {
        return a >= kDevice && a < kDevice + 0x1000;
    }

private:
    static jit::MemoryHooks hooks(SyntheticCpu* cpu) {
        jit::MemoryHooks h;
        h.self = cpu;
        h.codeSpan = [](void* s, uint32_t p, uint32_t& len) -> const uint8_t* {
            auto& c = *static_cast<SyntheticCpu*>(s);
            if (inDevice(p)) return nullptr;
            p = bus(p);
            if (inHole(p)) return nullptr;
            len = uint32_t(c.mem.size()) - p;
            return c.mem.data() + p;
        };
        h.dataSpan = [](void* s, uint32_t p, uint32_t& len, int) -> uint8_t* {
            auto& c = *static_cast<SyntheticCpu*>(s);
            if (inDevice(p)) return nullptr;
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
        if (inDevice(a)) {
            auto& c = *const_cast<SyntheticCpu*>(this);
            const uint32_t n = c.deviceReads++;
            // The exact access owns a deliberately varying bus delay. Native
            // code must add only MOVE's fixed cycles and must never duplicate
            // this side-effecting read after the destination was preflighted.
            c.setClock(c.getClock() + 3 + int64_t(n & 3));
            return moira::u8(c.deviceValue ^ n);
        }
        if (inHole(a)) fault();
        return mem[bus(a)];
    }
    moira::u16 read16(moira::u32 a) const override {
        if (inDevice(a)) {
            auto& c = *const_cast<SyntheticCpu*>(this);
            const uint32_t n = c.deviceReads++;
            c.setClock(c.getClock() + 3 + int64_t(n & 3));
            return 7;                   // non-zero divide source
        }
        if (inHole(a)) fault();
        const uint32_t p = bus(a);
        return moira::u16(mem[p] << 8 | mem[bus(p + 1)]);
    }
    void write8(moira::u32 a, moira::u8 v) const override {
        auto& c = *const_cast<SyntheticCpu*>(this);
        if (inDevice(a)) {
            const uint32_t n = c.deviceWrites++;
            c.setClock(c.getClock() + 2 + int64_t(n & 1));
            c.deviceValue = v;
            return;
        }
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
    put16(c, kCode + 0x12, 0x5948);    // SUBQ.W #4,A0 (full-width An form)
    put16(c, kCode + 0x14, 0x5280);    // ADDQ.L #1,D0
    put16(c, kCode + 0x16, 0xB592);    // EOR.L D2,(A2)
    put16(c, kCode + 0x18, 0xB580);    // EOR.L D2,D0
    put16(c, kCode + 0x1A, 0xCD4B);    // EXG A6,A3
    put16(c, kCode + 0x1C, 0xCD4B);    // restore A6/A3
    put16(c, kCode + 0x1E, 0xB90B);    // CMPM.B (A3)+,(A4)+
    put16(c, kCode + 0x20, 0x534B);    // SUBQ.W #1,A3
    put16(c, kCode + 0x22, 0x534C);    // SUBQ.W #1,A4
    put16(c, kCode + 0x24, 0xB5C8);    // CMPA.L A0,A2
    put16(c, kCode + 0x26, 0x51CF);    // DBF D7,kCode+$10
    put16(c, kCode + 0x28, 0xFFE8);
    put16(c, kCode + 0x2A, 0x61FF);    // BSR.L subroutine
    put32(c, kCode + 0x2C, 0x00000008); // target kCode+$34 from PC+2
    put16(c, kCode + 0x30, 0x60DE);    // BRA.S kCode+$10
    put16(c, kCode + 0x32, 0x4E71);    // padding
    put16(c, kCode + 0x34, 0x2612);    // MOVE.L (A2),D3
    put16(c, kCode + 0x36, 0x4843);    // SWAP D3
    put16(c, kCode + 0x38, 0x4E75);    // RTS
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

void installExactSourceLoop(SyntheticCpu& c) {
    installVectors(c);
    put16(c, kCode + 0, 0x12D0);       // MOVE.B (A0),(A1)+
    put16(c, kCode + 2, 0x4E71);       // keep the backward branch on the
    put16(c, kCode + 4, 0x4E71);       // ordinary six-cycle path
    put16(c, kCode + 6, 0x4E71);
    put16(c, kCode + 8, 0x60F6);       // BRA.S kCode
}

void installBriefIndexedLoop(SyntheticCpu& c) {
    installVectors(c);
    put16(c, kCode + 0x00, 0x2430);    // MOVE.L 4(A0,D1.W*2),D2
    put16(c, kCode + 0x02, 0x1204);
    put16(c, kCode + 0x04, 0x263B);    // MOVE.L 38(PC,A1.L*4),D3
    put16(c, kCode + 0x06, 0x9C26);
    put16(c, kCode + 0x08, 0x50F2);    // ST 0(A2,D4.L*4)
    put16(c, kCode + 0x0A, 0x4C00);
    put16(c, kCode + 0x0C, 0x487B);    // PEA 8(PC,A1.L*4)
    put16(c, kCode + 0x0E, 0x9C08);
    put16(c, kCode + 0x10, 0x588F);    // ADDQ.L #4,A7 (discard PEA result)
    put16(c, kCode + 0x12, 0x47F0);    // LEA 4(A0,D1.W*2),A3
    put16(c, kCode + 0x14, 0x1204);
    put16(c, kCode + 0x16, 0x49FB);    // LEA 8(PC,A1.L*4),A4
    put16(c, kCode + 0x18, 0x9C08);
    put16(c, kCode + 0x1A, 0x60E4);    // BRA.S kCode
    for (uint32_t p = 0x1C; p < 0x30; p += 2)
        put16(c, kCode + p, 0x4E71);   // padding before PC-relative literal
    put32(c, kCode + 0x30, 0x89AB'CDEFu);
    put32(c, kData + 0x22, 0x0123'4567u);
}

void installFullDirectLeaLoop(SyntheticCpu& c) {
    installVectors(c);
    put16(c, kCode + 0x00, 0x47F0);    // LEA (32,A0,D1.W*4),A3
    put16(c, kCode + 0x02, 0x1530);    // full/direct, long BD
    put32(c, kCode + 0x04, 0x0000'0020u);
    put16(c, kCode + 0x08, 0x49FB);    // LEA (16,ZPC,A1.L*4),A4
    put16(c, kCode + 0x0A, 0x9DA0);    // full/direct, base suppressed, word BD
    put16(c, kCode + 0x0C, 0x0010);
    put16(c, kCode + 0x0E, 0x4BFB);    // LEA (48,PC,Zn),A5
    put16(c, kCode + 0x10, 0x1D60);    // full/direct, index suppressed, word BD
    put16(c, kCode + 0x12, 0x0030);
    put16(c, kCode + 0x14, 0x4DF0);    // LEA (A0,D0.L),A6
    put16(c, kCode + 0x16, 0x0910);    // full/direct, null BD
    put16(c, kCode + 0x18, 0x2430);    // MOVE.L (16,A0,D1.W*4),D2
    put16(c, kCode + 0x1A, 0x1520);    // full/direct, word BD
    put16(c, kCode + 0x1C, 0x0010);
    put16(c, kCode + 0x1E, 0x60E0);    // BRA.S kCode
    put32(c, kData + 0x0C, 0x2468'ACE0u);
}

void installFullIndirectFallbackLoop(SyntheticCpu& c) {
    installVectors(c);
    // Lead with native work so the refused form sits inside a compiled
    // container and must visibly enter its per-instruction slow stub.
    put16(c, kCode + 0x00, 0x4E71);
    put16(c, kCode + 0x02, 0x4E71);
    put16(c, kCode + 0x04, 0x4E71);
    put16(c, kCode + 0x06, 0x47F0);    // LEA ([A0,D1.L*4]),A3
    put16(c, kCode + 0x08, 0x1D11);    // full/preindexed, null BD/OD
    put16(c, kCode + 0x0A, 0x2470);    // MOVE.L ([$4000],Zn),A2 (Rogue)
    put16(c, kCode + 0x0C, 0x81E1);    // full/preindexed, base+index suppressed
    put16(c, kCode + 0x0E, 0x4000);    // word base displacement
    put16(c, kCode + 0x10, 0xB2B0);    // CMP.L ([$4000],Zn),D1 (Rogue)
    put16(c, kCode + 0x12, 0x81E1);
    put16(c, kCode + 0x14, 0x4000);
    put16(c, kCode + 0x16, 0x60E8);    // BRA.S kCode
    put32(c, kData, 0x0000'7654u);
    put32(c, 0x7654, 0x89AB'CDEFu);
}

void installFullIndirectJsrLoop(SyntheticCpu& c) {
    installVectors(c);
    put16(c, kCode + 0x00, 0x4E71);
    put16(c, kCode + 0x02, 0x4E71);
    put16(c, kCode + 0x04, 0x4E71);
    put16(c, kCode + 0x06, 0x4EB0);    // JSR ([A0,D1.L*4])
    put16(c, kCode + 0x08, 0x1D11);    // full/preindexed, null BD/OD
    put16(c, kCode + 0x0A, 0x60F4);    // BRA.S kCode
    put16(c, kCode + 0x20, 0x5282);    // ADDQ.L #1,D2
    put16(c, kCode + 0x22, 0x4E75);    // RTS
    put32(c, kData, kCode + 0x20);
}

void installDependentMoveLoop(SyntheticCpu& c) {
    installVectors(c);
    put16(c, kCode + 0x00, 0x4E71);
    put16(c, kCode + 0x02, 0x4E71);
    put16(c, kCode + 0x04, 0x4E71);
    put16(c, kCode + 0x06, 0x2E9F);    // MOVE.L (A7)+,(A7)
    put16(c, kCode + 0x08, 0x3F5F);    // MOVE.W (A7)+,4(A7)
    put16(c, kCode + 0x0A, 0x0004);
    put16(c, kCode + 0x0C, 0x2F5F);    // MOVE.L (A7)+,4(A7)
    put16(c, kCode + 0x0E, 0x0004);
    put16(c, kCode + 0x10, 0x60EE);    // BRA.S kCode
    put32(c, kStack, 0x1357'9BDFu);
}

// Classic bit operations exercise the shared, deliberately irregular EA
// mask as executable code. Register operands use modulo 32; memory and the
// legal dynamic-immediate BTST operand use modulo 8. PI/PD make a missing EA
// commit visible, while the three modifying actions cover XOR/AND/OR.
void installClassicBitLoop(SyntheticCpu& c) {
    installVectors(c);
    put16(c, kCode + 0x00, 0x7EFF);    // MOVEQ #-1,D7
    put16(c, kCode + 0x02, 0xDE87);    // ADD.L D7,D7 (X/N/C witnesses)
    put16(c, kCode + 0x04, 0x283C);    // MOVE.L #$81234567,D4
    put32(c, kCode + 0x06, 0x8123'4567u);
    put16(c, kCode + 0x0A, 0x7225);    // MOVEQ #37,D1
    put16(c, kCode + 0x0C, 0x0344);    // BCHG D1,D4 (bit 5)
    put16(c, kCode + 0x0E, 0x0884);    // BCLR #31,D4
    put16(c, kCode + 0x10, 31);
    put16(c, kCode + 0x12, 0x08C4);    // BSET #30,D4
    put16(c, kCode + 0x14, 30);
    put16(c, kCode + 0x16, 0x0804);    // BTST #30,D4
    put16(c, kCode + 0x18, 30);
    put16(c, kCode + 0x1A, 0x207C);    // MOVEA.L #kData,A0
    put32(c, kCode + 0x1C, kData);
    put16(c, kCode + 0x20, 0x227C);    // MOVEA.L #kData+2,A1
    put32(c, kCode + 0x22, kData + 2);
    put16(c, kCode + 0x26, 0x247C);    // MOVEA.L #kData+2,A2
    put32(c, kCode + 0x28, kData + 2);
    put16(c, kCode + 0x2C, 0x0358);    // BCHG D1,(A0)+ (bit 5)
    put16(c, kCode + 0x2E, 0x08A1);    // BCLR #10,-(A1) (bit 2)
    put16(c, kCode + 0x30, 10);
    put16(c, kCode + 0x32, 0x08EA);    // BSET #15,0(A2) (bit 7)
    put16(c, kCode + 0x34, 15);
    put16(c, kCode + 0x36, 0);
    put16(c, kCode + 0x38, 0x033C);    // BTST D1,#$20 (legal 020+ form)
    put16(c, kCode + 0x3A, 0x0020);
    put16(c, kCode + 0x3C, 0x60C2);    // BRA.S kCode
    c.mem[kData + 0] = 0x20;
    c.mem[kData + 1] = 0xFF;
    c.mem[kData + 2] = 0x00;
}

void installBitRmwLoop(SyntheticCpu& c) {
    installVectors(c);
    put16(c, kCode + 0x00, 0x0850);    // BCHG #0,(A0)
    put16(c, kCode + 0x02, 0);
    put16(c, kCode + 0x04, 0x60FA);    // BRA.S kCode
    c.mem[kData] = 0xA5;
}

// Two read-only 68020+ EA extensions that were hidden among x64's broad
// opcode-level over-claims: TST accepts an immediate operand and CMPI accepts
// PC-relative operands.  Their neighbours (writes to An/PC/#imm) are illegal
// and are pinned separately by jit_backend_test.
void installLegalReadOnlyEaLoop(SyntheticCpu& c) {
    installVectors(c);
    put16(c, kCode + 0x00, 0x7000);    // MOVEQ #0,D0 (index = 0)
    put16(c, kCode + 0x02, 0x4A3C);    // TST.B #$80
    put16(c, kCode + 0x04, 0x0080);
    put16(c, kCode + 0x06, 0x4A7C);    // TST.W #0
    put16(c, kCode + 0x08, 0x0000);
    put16(c, kCode + 0x0A, 0x4ABC);    // TST.L #$80000000
    put32(c, kCode + 0x0C, 0x8000'0000u);
    put16(c, kCode + 0x10, 0x0C3A);    // CMPI.B #$5A,28(PC)
    put16(c, kCode + 0x12, 0x005A);
    put16(c, kCode + 0x14, 0x001C);    // EA base $14 -> literal $30
    put16(c, kCode + 0x16, 0x0C7B);    // CMPI.W #$1234,24(PC,D0.W)
    put16(c, kCode + 0x18, 0x1234);
    put16(c, kCode + 0x1A, 0x0018);    // EA base $1A -> literal $32
    put16(c, kCode + 0x1C, 0x0CBA);    // CMPI.L #$89ABCDEF,18(PC)
    put32(c, kCode + 0x1E, 0x89AB'CDEFu);
    put16(c, kCode + 0x22, 0x0012);    // EA base $22 -> literal $34
    put16(c, kCode + 0x24, 0x60DA);    // BRA.S kCode
    c.mem[kCode + 0x30] = 0x5A;
    put16(c, kCode + 0x32, 0x1234);
    put32(c, kCode + 0x34, 0x89AB'CDEFu);
}

void installDynamicBitfieldLoop(SyntheticCpu& c, bool includeMemory) {
    installVectors(c);
    put16(c, kCode + 0x00, 0x2A3C);    // MOVE.L #$89ABCDEF,D5
    put32(c, kCode + 0x02, 0x89AB'CDEFu);
    put16(c, kCode + 0x06, 0x2C3C);    // MOVE.L #$13579BDF,D6
    put32(c, kCode + 0x08, 0x1357'9BDFu);
    put16(c, kCode + 0x0C, 0x72FD);    // MOVEQ #-3,D1 (raw signed offset)
    put16(c, kCode + 0x0E, 0x7402);    // MOVEQ #2,D2
    put16(c, kCode + 0x10, 0x7605);    // MOVEQ #5,D3
    put16(c, kCode + 0x12, 0x7804);    // MOVEQ #4,D4
    put16(c, kCode + 0x14, 0xE8C5);    // BFTST D5{D1:D3}
    put16(c, kCode + 0x16, 0x0863);
    put16(c, kCode + 0x18, 0xE9C5);    // BFEXTU D5{D1:D3},D0 (Rogue)
    put16(c, kCode + 0x1A, 0x0863);
    put16(c, kCode + 0x1C, 0xEAC5);    // BFCHG D5{D1:D3}
    put16(c, kCode + 0x1E, 0x0863);
    put16(c, kCode + 0x20, 0xEBC5);    // BFEXTS D5{D1:D3},D0
    put16(c, kCode + 0x22, 0x0863);
    put16(c, kCode + 0x24, 0xECC5);    // BFCLR D5{D1:D3}
    put16(c, kCode + 0x26, 0x0863);
    put16(c, kCode + 0x28, 0xEDC5);    // BFFFO D5{D1:D3},D0
    put16(c, kCode + 0x2A, 0x0863);
    put16(c, kCode + 0x2C, 0xEEC5);    // BFSET D5{D1:D3}
    put16(c, kCode + 0x2E, 0x0863);
    put16(c, kCode + 0x30, 0xEFC6);    // BFINS D0,D6{D2:D4} (Rogue)
    put16(c, kCode + 0x32, 0x08A4);
    if (!includeMemory) {
        put16(c, kCode + 0x34, 0xEDC5); // BFFFO D5{D1:D3},D0, nonzero arm
        put16(c, kCode + 0x36, 0x0863);
        put16(c, kCode + 0x38, 0xE8C5); // BFTST D5{4:D7}, D7=0 => width 32
        put16(c, kCode + 0x3A, 0x0127);
        put16(c, kCode + 0x3C, 0xE8C5); // BFTST D5{D1:8}
        put16(c, kCode + 0x3E, 0x0848);
        put16(c, kCode + 0x40, 0x60BE); // BRA.S kCode
        return;
    }
    put16(c, kCode + 0x34, 0xE9D0);    // BFEXTU (A0){0:D0},D2 (Rogue)
    put16(c, kCode + 0x36, 0x2020);
    put16(c, kCode + 0x38, 0xE9D0);    // BFEXTU (A0){D1:8},D5 (Rogue)
    put16(c, kCode + 0x3A, 0x5848);
    put16(c, kCode + 0x3C, 0xE9D0);    // BFEXTU (A0){D1:32},D0 (Rogue)
    put16(c, kCode + 0x3E, 0x0840);
    put16(c, kCode + 0x40, 0xE9E9);    // BFEXTU d16(A1){D0:D2},D1 (Rogue)
    put16(c, kCode + 0x42, 0x1022);
    put16(c, kCode + 0x44, 0x0000);
    put16(c, kCode + 0x46, 0x60B8);    // BRA.S kCode
    put32(c, kData, 0xA55A'3CC3u);
    put32(c, kData + 4, 0x5AA5'C33Cu);
}

// Static offset/width register bitfields — the subset BOTH production
// generators lower (x64 gained it on 2026-08-30; a64 has carried it since
// the family landed). Edge cases on purpose: {0:32} full-long, {31:1} the
// last bit, {12:20} ending exactly at bit 32, and a BFFFO over a field
// wiped by the preceding BFCLR so the zero-field arm (offset+width, where
// x64's BSR is undefined) executes every lap.
void installStaticBitfieldLoop(SyntheticCpu& c) {
    installVectors(c);
    put16(c, kCode + 0x00, 0x2A3C);    // MOVE.L #$89ABCDEF,D5
    put32(c, kCode + 0x02, 0x89AB'CDEFu);
    put16(c, kCode + 0x06, 0x2C3C);    // MOVE.L #$13579BDF,D6
    put32(c, kCode + 0x08, 0x1357'9BDFu);
    put16(c, kCode + 0x0C, 0x7E00);    // MOVEQ #0,D7
    put16(c, kCode + 0x0E, 0xE8C5);    // BFTST  D5{0:32}
    put16(c, kCode + 0x10, 0x0000);
    put16(c, kCode + 0x12, 0xE9C5);    // BFEXTU D5{5:8},D0
    put16(c, kCode + 0x14, 0x0148);
    put16(c, kCode + 0x16, 0xEAC5);    // BFCHG  D5{12:20}
    put16(c, kCode + 0x18, 0x0314);
    put16(c, kCode + 0x1A, 0xEBC5);    // BFEXTS D5{31:1},D0
    put16(c, kCode + 0x1C, 0x07C1);
    put16(c, kCode + 0x1E, 0xECC5);    // BFCLR  D5{0:16}
    put16(c, kCode + 0x20, 0x0010);
    put16(c, kCode + 0x22, 0xEDC5);    // BFFFO  D5{4:12},D1
    put16(c, kCode + 0x24, 0x110C);
    put16(c, kCode + 0x26, 0xEEC5);    // BFSET  D5{24:8}
    put16(c, kCode + 0x28, 0x0608);
    put16(c, kCode + 0x2A, 0xEFC6);    // BFINS  D0,D6{7:9}
    put16(c, kCode + 0x2C, 0x01C9);
    put16(c, kCode + 0x2E, 0xEDC7);    // BFFFO  D7{3:5},D2 (zero field)
    put16(c, kCode + 0x30, 0x20C5);
    put16(c, kCode + 0x32, 0x60CC);    // BRA.S kCode
}

// Read-only TAILLESS memory bitfields — the subset BOTH generators lower
// since 2026-08-30 (x64 gained it; a64 has carried it). The census's hot
// shapes on purpose: {D1:8} with a NEGATIVE Dn offset (the signed
// byte-displacement adjust), {0:D0} dynamic width reaching 32, a field
// ending exactly at bit 32, and the d16(An) column. Every form here
// provably fits one longword; the five-byte tail stays in the dynamic
// scenario above, where both production generators now carry the claim.
void installMemoryBitfieldLoop(SyntheticCpu& c) {
    installVectors(c);
    put16(c, kCode + 0x00, 0x72FD);    // MOVEQ #-3,D1
    put16(c, kCode + 0x02, 0x7014);    // MOVEQ #20,D0
    put16(c, kCode + 0x04, 0xE8D0);    // BFTST  (A0){4:12}
    put16(c, kCode + 0x06, 0x010C);
    put16(c, kCode + 0x08, 0xE9D0);    // BFEXTU (A0){D1:8},D5 (SC2K/Rogue)
    put16(c, kCode + 0x0A, 0x5848);
    put16(c, kCode + 0x0C, 0xEBD0);    // BFEXTS (A0){2:15},D3
    put16(c, kCode + 0x0E, 0x308F);
    put16(c, kCode + 0x10, 0xEDD0);    // BFFFO  (A0){0:D0},D2 (Rogue)
    put16(c, kCode + 0x12, 0x2020);
    put16(c, kCode + 0x14, 0xE9E9);    // BFEXTU d16(A1){5:8},D4
    put16(c, kCode + 0x16, 0x4148);
    put16(c, kCode + 0x18, 0x0004);
    put16(c, kCode + 0x1A, 0xE8D0);    // BFTST  (A0){0:32}
    put16(c, kCode + 0x1C, 0x0000);
    put16(c, kCode + 0x1E, 0x60E0);    // BRA.S kCode
    put32(c, kData, 0xA55A'3CC3u);
    put32(c, kData + 4, 0x5AA5'C33Cu);
}

// TAILLESS memory bitfield writes — the shared IR RMW contract consumed by
// both generators. All four actions run, including SimCity's EFD1 BFINS,
// signed dynamic offset, dynamic width and both admitted EA columns. A second
// BFINS reaches its dynamic-width source/mask branch; read witnesses make any
// one-action regression visible in the final residency count.
void installMemoryBitfieldWriteLoop(SyntheticCpu& c) {
    installVectors(c);
    put16(c, kCode + 0x00, 0x72FD);    // MOVEQ #-3,D1
    put16(c, kCode + 0x02, 0x203C);    // MOVE.L #$13579BDF,D0
    put32(c, kCode + 0x04, 0x1357'9BDFu);
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
    put32(c, kData + 0x00, 0xA55A'3CC3u);
    put32(c, kData + 0x04, 0x5AA5'C33Cu);
    put32(c, kData + 0x08, 0x89AB'CDEFu);
}

// Speedometer's exact three hot forms plus predecrement/postincrement memory
// sources. ADD establishes X=1 before every lap; every multiply must preserve
// it while publishing its own 32-bit N/Z and clearing V/C.
void installWordMultiplyLoop(SyntheticCpu& c) {
    installVectors(c);
    put16(c, kCode + 0x00, 0x7EFF);    // MOVEQ #-1,D7
    put16(c, kCode + 0x02, 0xDE87);    // ADD.L D7,D7 (set X)
    put16(c, kCode + 0x04, 0x70FD);    // MOVEQ #-3,D0
    put16(c, kCode + 0x06, 0x7807);    // MOVEQ #7,D4
    put16(c, kCode + 0x08, 0xC9C0);    // MULS.W D0,D4 (Speedometer)
    put16(c, kCode + 0x0A, 0x7202);    // MOVEQ #2,D1
    put16(c, kCode + 0x0C, 0xC2FC);    // MULU.W #$FFFF,D1 (Speedometer)
    put16(c, kCode + 0x0E, 0xFFFF);
    put16(c, kCode + 0x10, 0x769C);    // MOVEQ #-100,D3
    put16(c, kCode + 0x12, 0xC7FC);    // MULS.W #300,D3 (Speedometer)
    put16(c, kCode + 0x14, 300);
    put16(c, kCode + 0x16, 0x207C);    // MOVEA.L #kData+2,A0
    put32(c, kCode + 0x18, kData + 2);
    put16(c, kCode + 0x1C, 0x74FD);    // MOVEQ #-3,D2
    put16(c, kCode + 0x1E, 0xC5E0);    // MULS.W -(A0),D2
    put16(c, kCode + 0x20, 0x7A02);    // MOVEQ #2,D5
    put16(c, kCode + 0x22, 0xCAD8);    // MULU.W (A0)+,D5
    put16(c, kCode + 0x24, 0x60DA);    // BRA.S kCode
    put16(c, kData, 0xFFF9);           // -7 signed / 65529 unsigned
}

// Register ADDX/SUBX is Speedometer's hottest remaining unsupported family.
// Rebuild every operand and CCR seed on each lap, then exercise both
// operations at byte/word/long width. The long chain contains the exact
// D981/9381 opcodes from the application and the source=$FFFFFFFF,X=1 case
// that requires a 33-bit intermediate for conforming SUBX borrow handling.
void installAddSubExtendLoop(SyntheticCpu& c) {
    installVectors(c);
    uint32_t p = kCode;
    const auto word = [&](uint16_t v) { put16(c, p, v); p += 2; };
    const auto moveLong = [&](unsigned dn, uint32_t v) {
        word(uint16_t(0x203C | (dn << 9))); put32(c, p, v); p += 4;
    };
    const auto seedX1Z1 = [&]() {
        word(0x7CFF);                   // MOVEQ #-1,D6
        word(0x7E01);                   // MOVEQ #1,D7
        word(0xDC87);                   // ADD.L D7,D6 -> 0, X=1, Z=1
    };

    moveLong(0, 0);
    moveLong(1, 0x7F);
    moveLong(2, 0);
    moveLong(3, 0);
    seedX1Z1();
    word(0xD300);                       // ADDX.B D0,D1 -> $80, V=1,Z=0
    word(0xD702);                       // ADDX.B D2,D3 -> 0, sticky Z=0
    word(0x9702);                       // SUBX.B D2,D3 -> 0, sticky Z=0

    moveLong(0, 0xFFFF);
    moveLong(1, 0);
    moveLong(2, 0x7FFF);
    moveLong(3, 0x8000);
    seedX1Z1();
    word(0xD340);                       // ADDX.W D0,D1 -> 0, C/X=1,Z=1
    word(0x9742);                       // SUBX.W D2,D3 -> 0, V=1,Z=1

    moveLong(0, 0xFFFF'FFFFu);
    moveLong(1, 0);
    moveLong(2, 0xFFFF'FFFFu);
    moveLong(3, 0);
    moveLong(4, 0xFFFF'FFFFu);
    seedX1Z1();
    word(0xD380);                       // ADDX.L D0,D1 -> 0, carry
    word(0x9782);                       // SUBX.L D2,D3, source+X = 2^32
    word(0xD981);                       // ADDX.L D1,D4 (Speedometer)
    word(0x9381);                       // SUBX.L D1,D1 (Speedometer)

    const int32_t displacement = int32_t(kCode) - int32_t(p + 2);
    word(uint16_t(0x6000 | uint8_t(displacement))); // BRA.S kCode
}

// Speedometer executes EXTB.L D2/D7 throughout launch and CPU phases.  The
// three values cover negative, zero and positive byte results while carrying
// unrelated high bits into the instruction; each EXTB must overwrite all 32
// destination bits, set N/Z, clear V/C and preserve X.
void installExtendByteLoop(SyntheticCpu& c) {
    installVectors(c);
    put16(c, kCode + 0x00, 0x2E3C);    // MOVE.L #$12345680,D7
    put32(c, kCode + 0x02, 0x1234'5680u);
    put16(c, kCode + 0x06, 0x49C7);    // EXTB.L D7 -> $FFFFFF80
    put16(c, kCode + 0x08, 0x243C);    // MOVE.L #$89ABCD00,D2
    put32(c, kCode + 0x0A, 0x89AB'CD00u);
    put16(c, kCode + 0x0E, 0x49C2);    // EXTB.L D2 -> 0
    put16(c, kCode + 0x10, 0x2E3C);    // MOVE.L #$7654327F,D7
    put32(c, kCode + 0x12, 0x7654'327Fu);
    put16(c, kCode + 0x16, 0x49C7);    // EXTB.L D7 -> $0000007F
    put16(c, kCode + 0x18, 0x60E6);    // BRA.S kCode
}

void installMemoryMultiplyDeviceLoop(SyntheticCpu& c) {
    installVectors(c);
    put16(c, kCode + 0x00, 0xC0D8);    // MULU.W (A0)+,D0
    put16(c, kCode + 0x02, 0x60FC);    // BRA.S kCode
    put16(c, kData, 7);
}

// Successful word divisions cover the three immediate opcodes observed in
// SimCity plus both signed/unsigned register sources. The preceding ADD sets
// X, which every division must preserve while replacing N/Z/V/C.
void installWordDivisionLoop(SyntheticCpu& c) {
    installVectors(c);
    put16(c, kCode + 0x00, 0x74FF);    // MOVEQ #-1,D2
    put16(c, kCode + 0x02, 0xD482);    // ADD.L D2,D2 (set X)
    put16(c, kCode + 0x04, 0x203C);    // MOVE.L #100000,D0
    put32(c, kCode + 0x06, 100000);
    put16(c, kCode + 0x0A, 0x2C3C);    // MOVE.L #-100000,D6
    put32(c, kCode + 0x0C, uint32_t(-100000));
    put16(c, kCode + 0x10, 0x2E3C);    // MOVE.L #32767,D7
    put32(c, kCode + 0x12, 32767);
    put16(c, kCode + 0x16, 0x2A3C);    // MOVE.L #123456,D5
    put32(c, kCode + 0x18, 123456);
    put16(c, kCode + 0x1C, 0x263C);    // MOVE.L #60000,D3
    put32(c, kCode + 0x1E, 60000);
    put16(c, kCode + 0x22, 0x7825);    // MOVEQ #37,D4
    put16(c, kCode + 0x24, 0x81FC);    // DIVS.W #10,D0 (SimCity)
    put16(c, kCode + 0x26, 10);
    put16(c, kCode + 0x28, 0x8DFC);    // DIVS.W #300,D6 (SimCity)
    put16(c, kCode + 0x2A, 300);
    put16(c, kCode + 0x2C, 0x8FFC);    // DIVS.W #-7,D7 (SimCity)
    put16(c, kCode + 0x2E, 0xFFF9);
    put16(c, kCode + 0x30, 0x8BC4);    // DIVS.W D4,D5
    put16(c, kCode + 0x32, 0x86C4);    // DIVU.W D4,D3
    put16(c, kCode + 0x34, 0x60CA);    // BRA.S kCode
}

// All three quotient-overflow shapes must enter Moira before changing Dn or
// CCR. INT_MIN/-1 is explicit because x86 IDIV would otherwise raise #DE.
void installWordDivisionOverflowLoop(SyntheticCpu& c) {
    installVectors(c);
    put16(c, kCode + 0x00, 0x81FC);    // DIVS.W #1,D0: positive overflow
    put16(c, kCode + 0x02, 1);
    put16(c, kCode + 0x04, 0x83FC);    // DIVS.W #-1,D1: host #DE guard
    put16(c, kCode + 0x06, 0xFFFF);
    put16(c, kCode + 0x08, 0x84FC);    // DIVU.W #1,D2: unsigned overflow
    put16(c, kCode + 0x0A, 1);
    put16(c, kCode + 0x0C, 0x60F2);    // BRA.S kCode
}

// A recurrent vector-5 path proves that zero reaches the interpreter's exact
// exception implementation instead of executing a host divide instruction.
void installWordDivisionZeroLoop(SyntheticCpu& c) {
    installVectors(c);
    put32(c, 5 * 4, kHandler);          // vector 5: integer divide by zero
    put16(c, kHandler + 0, 0x4E73);    // RTE
    put16(c, kHandler + 2, 0x4E71);    // prefetch padding
    put16(c, kCode + 0x00, 0x81C1);    // DIVS.W D1,D0 (trained nonzero)
    put16(c, kCode + 0x02, 0x60FC);    // BRA.S kCode
}

// The $4C40 witness spans all four legal extension actions: unsigned/signed
// with a 32-/64-bit dividend, plus the architecturally observable Dh==Dl
// write-order alias. Each operand is rebuilt on every lap so all five DIVL
// instructions remain successful and must stay native.
void installLongDivisionLoop(SyntheticCpu& c) {
    installVectors(c);
    put16(c, kCode + 0x00, 0x7EFF);    // MOVEQ #-1,D7
    put16(c, kCode + 0x02, 0xDE87);    // ADD.L D7,D7 (set X)
    put16(c, kCode + 0x04, 0x7007);    // MOVEQ #7,D0
    put16(c, kCode + 0x06, 0x223C);    // MOVE.L #100,D1
    put32(c, kCode + 0x08, 100);
    put16(c, kCode + 0x0C, 0x7437);    // MOVEQ #55,D2
    put16(c, kCode + 0x0E, 0x4C40);    // DIVUL.L D0,D2:D1
    put16(c, kCode + 0x10, 0x1002);
    put16(c, kCode + 0x12, 0x700A);    // MOVEQ #10,D0
    put16(c, kCode + 0x14, 0x263C);    // MOVE.L #5,D3
    put32(c, kCode + 0x16, 5);
    put16(c, kCode + 0x1A, 0x7801);    // MOVEQ #1,D4
    put16(c, kCode + 0x1C, 0x4C40);    // DIVU.L D0,D4:D3
    put16(c, kCode + 0x1E, 0x3404);
    put16(c, kCode + 0x20, 0x70F9);    // MOVEQ #-7,D0
    put16(c, kCode + 0x22, 0x2A3C);    // MOVE.L #-100,D5
    put32(c, kCode + 0x24, uint32_t(-100));
    put16(c, kCode + 0x28, 0x7C01);    // MOVEQ #1,D6
    put16(c, kCode + 0x2A, 0x4C40);    // DIVSL.L D0,D6:D5
    put16(c, kCode + 0x2C, 0x5806);
    put16(c, kCode + 0x2E, 0x700A);    // MOVEQ #10,D0
    put16(c, kCode + 0x30, 0x74FF);    // MOVEQ #-1,D2
    put16(c, kCode + 0x32, 0x2E3C);    // MOVE.L #-123,D7
    put32(c, kCode + 0x34, uint32_t(-123));
    put16(c, kCode + 0x38, 0x4C40);    // DIVS.L D0,D2:D7
    put16(c, kCode + 0x3A, 0x7C02);
    put16(c, kCode + 0x3C, 0x7007);    // MOVEQ #7,D0
    put16(c, kCode + 0x3E, 0x764D);    // MOVEQ #77,D3
    put16(c, kCode + 0x40, 0x4C40);    // DIVUL.L D0,D3:D3 (quotient wins)
    put16(c, kCode + 0x42, 0x3003);
    put16(c, kCode + 0x44, 0x60BA);    // BRA.S kCode
}

void installLongDivisionOverflowLoop(SyntheticCpu& c) {
    installVectors(c);
    put16(c, kCode + 0x00, 0x4C40);    // signed 32-bit dividend / D0
    put16(c, kCode + 0x02, 0x1802);    // Dl=D1, Dh=D2
    put16(c, kCode + 0x04, 0x4C40);    // unsigned 64-bit dividend / D0
    put16(c, kCode + 0x06, 0x3404);    // D4:D3
    put16(c, kCode + 0x08, 0x4C40);    // signed 64-bit dividend / D0
    put16(c, kCode + 0x0A, 0x5C06);    // D6:D5
    put16(c, kCode + 0x0C, 0x60F2);    // BRA.S kCode
}

void installLongDivisionZeroLoop(SyntheticCpu& c) {
    installVectors(c);
    put32(c, 5 * 4, kHandler);
    put16(c, kHandler + 0, 0x4E73);    // RTE
    put16(c, kHandler + 2, 0x4E71);
    put16(c, kCode + 0x00, 0x4C40);    // DIVS.L D0,D2:D1
    put16(c, kCode + 0x02, 0x1C02);
    put16(c, kCode + 0x04, 0x60FA);    // BRA.S kCode
}

// Successful memory sources cover transactional (An)+/-(An), plain (An),
// d16(An), and DIVL immediate ordering. The loop rebuilds every address and
// dividend so an admitted form has no excuse to visit the cold stub.
void installMemoryDivisionLoop(SyntheticCpu& c) {
    installVectors(c);
    put16(c, kCode + 0x00, 0x7EFF);    // MOVEQ #-1,D7
    put16(c, kCode + 0x02, 0xDE87);    // ADD.L D7,D7 (set X)
    put16(c, kCode + 0x04, 0x207C);    // MOVEA.L #kData,A0
    put32(c, kCode + 0x06, kData);
    put16(c, kCode + 0x0A, 0x203C);    // MOVE.L #100000,D0
    put32(c, kCode + 0x0C, 100000);
    put16(c, kCode + 0x10, 0x80D8);    // DIVU.W (A0)+,D0
    put16(c, kCode + 0x12, 0x223C);    // MOVE.L #-100000,D1
    put32(c, kCode + 0x14, uint32_t(-100000));
    put16(c, kCode + 0x18, 0x83D0);    // DIVS.W (A0),D1
    put16(c, kCode + 0x1A, 0x243C);    // MOVE.L #12345,D2
    put32(c, kCode + 0x1C, 12345);
    put16(c, kCode + 0x20, 0x85E0);    // DIVS.W -(A0),D2
    put16(c, kCode + 0x22, 0x207C);    // MOVEA.L #kData+4,A0
    put32(c, kCode + 0x24, kData + 4);
    put16(c, kCode + 0x28, 0x263C);    // MOVE.L #100,D3
    put32(c, kCode + 0x2A, 100);
    put16(c, kCode + 0x2E, 0x7801);    // MOVEQ #1,D4
    put16(c, kCode + 0x30, 0x4C58);    // DIVUL.L (A0)+,D4:D3
    put16(c, kCode + 0x32, 0x3004);
    put16(c, kCode + 0x34, 0x2A3C);    // MOVE.L #-100,D5
    put32(c, kCode + 0x36, uint32_t(-100));
    put16(c, kCode + 0x3A, 0x7C01);    // MOVEQ #1,D6
    put16(c, kCode + 0x3C, 0x4C50);    // DIVSL.L (A0),D6:D5
    put16(c, kCode + 0x3E, 0x5806);
    put16(c, kCode + 0x40, 0x7E05);    // MOVEQ #5,D7
    put16(c, kCode + 0x42, 0x7401);    // MOVEQ #1,D2
    put16(c, kCode + 0x44, 0x4C60);    // DIVU.L -(A0),D2:D7
    put16(c, kCode + 0x46, 0x7402);
    put16(c, kCode + 0x48, 0x223C);    // MOVE.L #100,D1
    put32(c, kCode + 0x4A, 100);
    put16(c, kCode + 0x4E, 0x7801);    // MOVEQ #1,D4
    put16(c, kCode + 0x50, 0x4C7C);    // DIVUL.L #10,D4:D1
    put16(c, kCode + 0x52, 0x1004);
    put32(c, kCode + 0x54, 10);
    put16(c, kCode + 0x58, 0x203C);    // MOVE.L #700,D0
    put32(c, kCode + 0x5A, 700);
    put16(c, kCode + 0x5E, 0x80E8);    // DIVU.W 2(A0),D0
    put16(c, kCode + 0x60, 0x0002);
    put16(c, kCode + 0x62, 0x609C);    // BRA.S kCode
    put16(c, kData + 0, 7);
    put16(c, kData + 2, 0xFFF9);       // -7
    put32(c, kData + 4, 10);
    put32(c, kData + 8, 0xFFFF'FFF9u); // -7
}

void installMemoryDivisionGuardLoop(SyntheticCpu& c) {
    installVectors(c);
    put16(c, kCode + 0x00, 0x81D0);    // DIVS.W (A0),D0
    put16(c, kCode + 0x02, 0x4C50);    // DIVS.L (A0),D2:D1
    put16(c, kCode + 0x04, 0x1C02);
    put16(c, kCode + 0x06, 0x60F8);    // BRA.S kCode
    put32(c, kData, 0x0001'0001u);     // both divisors non-zero while training
}

void installMemoryLongDivisionZeroLoop(SyntheticCpu& c) {
    installVectors(c);
    put32(c, 5 * 4, kHandler);
    put16(c, kHandler + 0, 0x4E73);
    put16(c, kHandler + 2, 0x4E71);
    put16(c, kCode + 0x00, 0x4C50);    // DIVS.L (A0),D2:D1
    put16(c, kCode + 0x02, 0x1C02);
    put16(c, kCode + 0x04, 0x60FA);    // BRA.S kCode
    put32(c, kData, 1);
}

void installMemoryDivisionDeviceLoop(SyntheticCpu& c) {
    installVectors(c);
    put16(c, kCode + 0x00, 0x80D0);    // DIVU.W (A0),D0
    put16(c, kCode + 0x02, 0x60FC);    // BRA.S kCode
    put16(c, kData, 7);
}

void installCachedMemoryDivisionLoop(SyntheticCpu& c) {
    installVectors(c);
    put16(c, kCode + 0x00, 0x4A50);    // TST.W (A0), publishes resident line
    put16(c, kCode + 0x02, 0x203C);    // MOVE.L #100000,D0
    put32(c, kCode + 0x04, 100000);
    put16(c, kCode + 0x08, 0x80D0);    // DIVU.W (A0),D0
    put16(c, kCode + 0x0A, 0x60F4);    // BRA.S kCode
    put16(c, kData, 7);
}

void installGuardedDynamicShiftLoop(SyntheticCpu& c, uint8_t count) {
    installVectors(c);
    put16(c, kCode + 0x00, uint16_t(0x7400 | count)); // MOVEQ #count,D2
    put16(c, kCode + 0x02, 0x203C);    // MOVE.L #$89ABCDEF,D0
    put32(c, kCode + 0x04, 0x89AB'CDEFu);
    put16(c, kCode + 0x08, 0xE5A8);    // LSL.L D2,D0 (Rogue)
    put16(c, kCode + 0x0A, 0x60F4);    // BRA.S kCode
}

void installMultiVersionShiftLoop(SyntheticCpu& c) {
    installVectors(c);
    put16(c, kCode + 0x00, 0x0A81);    // EORI.L #7,D1: 24 <-> 31
    put32(c, kCode + 0x02, 7);
    put16(c, kCode + 0x06, 0x263C);    // MOVE.L #$89ABCDEF,D3
    put32(c, kCode + 0x08, 0x89AB'CDEFu);
    put16(c, kCode + 0x0C, 0xE2AB);    // LSR.L D1,D3 (Speedometer)
    put16(c, kCode + 0x0E, 0x60F0);    // BRA.S kCode
}

void installBoundedShiftVersionLoop(SyntheticCpu& c) {
    installVectors(c);
    put16(c, kCode + 0x00, 0xE2AB);    // LSR.L D1,D3 (Speedometer)
    put16(c, kCode + 0x02, 0x60FC);    // BRA.S kCode
}

void installExactShiftVersionMatrix(SyntheticCpu& c) {
    installVectors(c);
    static constexpr std::array<uint16_t, 4> ops{
        0xE0A9, 0xE2AB, 0xE4A4, 0xE2AA
    };
    for (unsigned op = 0; op < ops.size(); op++)
        for (unsigned count = 0; count <= 31; count++) {
            const uint32_t site = kCode + (op * 32 + count) * 4;
            put16(c, site, ops[op]);
            put16(c, site + 2, 0x60FC); // BRA.S site
        }
}

void installGuardIndexLoops(SyntheticCpu& c) {
    installVectors(c);
    put16(c, kGuardA + 0, 0x5280);      // ADDQ.L #1,D0
    put16(c, kGuardA + 2, 0x60FC);      // BRA.S kGuardA
    put16(c, kGuardA + 4, 0x4E71);
    put16(c, kGuardA + 6, 0x4E71);
    put16(c, kGuardB + 0, 0x5281);      // ADDQ.L #1,D1
    put16(c, kGuardB + 2, 0x60FC);      // BRA.S kGuardB
    put16(c, kGuardB + 4, 0x4E71);
    put16(c, kGuardB + 6, 0x4E71);
    put16(c, kGuardCross + 0, 0x5282);  // ADDQ.L #1,D2
    put16(c, kGuardCross + 2, 0x60FC);  // BRA.S kGuardCross
    put16(c, kGuardCross + 4, 0x4E71);  // copied prefetch words deliberately
    put16(c, kGuardCross + 6, 0x4E71);  // cross the 256-byte slice boundary
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
    ref.setA(3, kData + 0x100);
    native.setA(3, kData + 0x100);
    ref.setA(4, kData + 0x180);
    native.setA(4, kData + 0x180);
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

bool runExactSourceMove() {
    SyntheticCpu ref, native;
    installExactSourceLoop(ref);
    installExactSourceLoop(native);
    resetCpu(ref);
    resetCpu(native);
    for (SyntheticCpu* c : {&ref, &native}) {
        c->setA(0, kDevice);
        c->setA(1, kData);
        c->setSR(0x2710);
    }
    native.jit.setEnabled(true);

    for (int step = 0; step < 256; step++) {
        const int64_t target = ref.getClock() + 37;
        ref.executeUntil(target);
        native.jit.executeUntil(target);
        if (!sameCpu(ref, native, true) ||
            !sameMemory(ref, native, 0, kRamBytes, true) ||
            ref.deviceReads != native.deviceReads) {
            std::printf("    exact-source divergence at checkpoint %d, reads=%u/%u\n",
                        step, ref.deviceReads, native.deviceReads);
            return false;
        }
    }
    const auto s = native.jit.stats().snapshot();
    std::printf("    exact-source compiled=%llu runs=%llu native=%llu slow=%llu reads=%u\n",
                (unsigned long long)s.blocksCompiled,
                (unsigned long long)s.blocksRun,
                (unsigned long long)s.instrs,
                (unsigned long long)s.slowInstrs, native.deviceReads);
    return s.blocksCompiled != 0 && s.blocksRun != 0 && s.slowInstrs == 0 &&
           native.deviceReads != 0;
}

bool runBriefIndexedLockstep() {
    SyntheticCpu ref, native;
    installBriefIndexedLoop(ref);
    installBriefIndexedLoop(native);
    resetCpu(ref);
    resetCpu(native);
    for (SyntheticCpu* c : {&ref, &native}) {
        c->setA(0, kData + 0x20);
        c->setD(1, 0x0000'FFFFu);      // sign-extended word index = -1
        c->setA(1, 1);                 // scaled address-register index
        c->setA(2, kData + 0x80);
        c->setD(4, 2);                 // indexed ST destination = kData+$88
        c->setSR(0x2715);              // ST is independent of incoming CCR
    }
    native.jit.setEnabled(true);

    for (int step = 0; step < 256; step++) {
        const int64_t target = ref.getClock() + 37;
        ref.executeUntil(target);
        native.jit.executeUntil(target);
        if (!sameCpu(ref, native, true) ||
            !sameMemory(ref, native, 0, kRamBytes, true)) {
            std::printf("    brief-index divergence at checkpoint %d\n", step);
            return false;
        }
    }
    const auto s = native.jit.stats().snapshot();
    std::printf("    brief-index compiled=%llu runs=%llu native=%llu slow=%llu\n",
                (unsigned long long)s.blocksCompiled,
                (unsigned long long)s.blocksRun,
                (unsigned long long)s.instrs,
                (unsigned long long)s.slowInstrs);
    return s.blocksCompiled != 0 && s.blocksRun != 0 && s.slowInstrs == 0 &&
           native.getD(2) == 0x0123'4567u &&
           native.getD(3) == 0x89AB'CDEFu &&
           native.getA(3) == kData + 0x22 &&
           native.getA(4) == kCode + 0x24 &&
           native.mem[kData + 0x88] == 0xFF && native.getA(7) == kStack;
}

bool runFullDirectLeaLockstep() {
    SyntheticCpu ref, native;
    installFullDirectLeaLoop(ref);
    installFullDirectLeaLoop(native);
    resetCpu(ref);
    resetCpu(native);
    for (SyntheticCpu* c : {&ref, &native}) {
        c->setA(0, kData);
        c->setD(0, 4);
        c->setD(1, 0x0000'FFFFu);      // signed word index = -1
        c->setA(1, 2);
    }
    native.jit.setEnabled(true);

    for (int step = 0; step < 128; step++) {
        const int64_t target = ref.getClock() + 41;
        ref.executeUntil(target);
        native.jit.executeUntil(target);
        if (!sameCpu(ref, native, true) ||
            !sameMemory(ref, native, 0, kRamBytes, true)) {
            std::printf("    full-direct LEA divergence at checkpoint %d\n",
                        step);
            return false;
        }
    }
    const auto s = native.jit.stats().snapshot();
    std::printf("    full-direct LEA compiled=%llu runs=%llu native=%llu slow=%llu\n",
                (unsigned long long)s.blocksCompiled,
                (unsigned long long)s.blocksRun,
                (unsigned long long)s.instrs,
                (unsigned long long)s.slowInstrs);
    // The full-format sub-forms across base/index suppression are lowered
    // by a64 only; x64 still falls back on part of them. The gap is a
    // dated row in jit_backend_parity_test's exception table, and holding
    // x64 to a64's residency here is what kept the x86-64 CI leg red and
    // unread from 2026-08-24 to 2026-08-28. The lockstep equality above
    // still binds every backend; only the "stays native" half is a64's.
    const bool a64Production = !std::strcmp(native.jit.backendName(), "aarch64") &&
                               !native.jit.config().packedCcr;
    return s.blocksCompiled != 0 && s.blocksRun != 0 &&
           (!a64Production || s.slowInstrs == 0) &&
           native.getA(3) == kData + 0x1C && native.getA(4) == 0x18 &&
           native.getA(5) == kCode + 0x40 && native.getA(6) == kData + 4 &&
           native.getD(2) == 0x2468'ACE0u;
}

bool runFullIndirectLeaLockstep() {
    SyntheticCpu ref, native;
    installFullIndirectFallbackLoop(ref);
    installFullIndirectFallbackLoop(native);
    resetCpu(ref);
    resetCpu(native);
    for (SyntheticCpu* c : {&ref, &native}) {
        c->setA(0, kData);
        c->setD(1, 0);
    }
    native.jit.setEnabled(true);

    for (int step = 0; step < 128; step++) {
        const int64_t target = ref.getClock() + 41;
        ref.executeUntil(target);
        native.jit.executeUntil(target);
        if (!sameCpu(ref, native, true) ||
            !sameMemory(ref, native, 0, kRamBytes, true)) {
            std::printf("    full-indirect fallback divergence at checkpoint %d\n",
                        step);
            return false;
        }
    }
    const auto s = native.jit.stats().snapshot();
    std::printf("    full-indirect compiled=%llu runs=%llu native=%llu slow=%llu\n",
                (unsigned long long)s.blocksCompiled,
                (unsigned long long)s.blocksRun,
                (unsigned long long)s.instrs,
                (unsigned long long)s.slowInstrs);
    // Memory-indirect full-index plans are a64-only lowerings (x64 keeps
    // them replay-only); see the parity gate's dated exception rows.
    const bool a64Production = !std::strcmp(native.jit.backendName(), "aarch64") &&
                               !native.jit.config().packedCcr;
    return s.blocksCompiled != 0 && s.blocksRun != 0 &&
           (!a64Production || s.slowInstrs == 0) &&
           native.getA(3) == 0x0000'7654u &&
           native.getA(2) == 0x89AB'CDEFu;
}

bool runFullIndirectJsrLockstep() {
    SyntheticCpu ref, native;
    installFullIndirectJsrLoop(ref);
    installFullIndirectJsrLoop(native);
    resetCpu(ref);
    resetCpu(native);
    for (SyntheticCpu* c : {&ref, &native}) {
        c->setA(0, kData);
        c->setD(1, 0);
    }
    native.jit.setEnabled(true);

    for (int step = 0; step < 128; step++) {
        const int64_t target = ref.getClock() + 41;
        ref.executeUntil(target);
        native.jit.executeUntil(target);
        if (!sameCpu(ref, native, true) ||
            !sameMemory(ref, native, 0, kRamBytes, true)) {
            std::printf("    full-indirect JSR divergence at checkpoint %d\n",
                        step);
            return false;
        }
    }
    const auto s = native.jit.stats().snapshot();
    std::printf("    full-indirect JSR compiled=%llu runs=%llu native=%llu slow=%llu\n",
                (unsigned long long)s.blocksCompiled,
                (unsigned long long)s.blocksRun,
                (unsigned long long)s.instrs,
                (unsigned long long)s.slowInstrs);
    // Same a64-only nativeness claim as the indirect LEA leg above.
    const bool a64Production = !std::strcmp(native.jit.backendName(), "aarch64") &&
                               !native.jit.config().packedCcr;
    return s.blocksCompiled != 0 && s.blocksRun != 0 &&
           (!a64Production || s.slowInstrs == 0) &&
           native.getD(2) != 0 && native.getA(7) == kStack;
}

bool runDependentMoveLockstep() {
    SyntheticCpu ref, native;
    installDependentMoveLoop(ref);
    installDependentMoveLoop(native);
    resetCpu(ref);
    resetCpu(native);
    native.jit.setEnabled(true);

    for (int step = 0; step < 64; step++) {
        const int64_t target = ref.getClock() + 41;
        ref.executeUntil(target);
        native.jit.executeUntil(target);
        if (!sameCpu(ref, native, true) ||
            !sameMemory(ref, native, 0, kRamBytes, true)) {
            std::printf("    dependent MOVE divergence at checkpoint %d\n",
                        step);
            return false;
        }
    }
    const auto s = native.jit.stats().snapshot();
    std::printf("    dependent MOVE compiled=%llu runs=%llu native=%llu slow=%llu\n",
                (unsigned long long)s.blocksCompiled,
                (unsigned long long)s.blocksRun,
                (unsigned long long)s.instrs,
                (unsigned long long)s.slowInstrs);
    const bool nativeProduction =
        (!std::strcmp(native.jit.backendName(), "aarch64") ||
         !std::strcmp(native.jit.backendName(), "x86-64")) &&
        !native.jit.config().packedCcr;
    return s.blocksCompiled != 0 && s.blocksRun != 0 &&
           (!nativeProduction || s.slowInstrs == 0) &&
           native.getA(7) > kStack;
}

bool runClassicBitLockstep(bool packedCcr) {
    jit::ResolvedConfig config = injectedJitConfig();
    config.packedCcr = packedCcr;
    SyntheticCpu ref(config), native(config);
    installClassicBitLoop(ref);
    installClassicBitLoop(native);
    resetCpu(ref);
    resetCpu(native);
    native.jit.setEnabled(true);

    for (int step = 0; step < 256; step++) {
        const int64_t target = ref.getClock() + 43;
        ref.executeUntil(target);
        native.jit.executeUntil(target);
        if (!sameCpu(ref, native, true) ||
            !sameMemory(ref, native, 0, kRamBytes, true)) {
            std::printf("    classic-bit divergence at checkpoint %d\n", step);
            return false;
        }
    }
    const auto s = native.jit.stats().snapshot();
    std::printf("    classic-bit packed=%d compiled=%llu runs=%llu "
                "native=%llu slow=%llu\n",
                int(packedCcr),
                (unsigned long long)s.blocksCompiled,
                (unsigned long long)s.blocksRun,
                (unsigned long long)s.instrs,
                (unsigned long long)s.slowInstrs);
    const bool nativeProduction =
        (!std::strcmp(native.jit.backendName(), "aarch64") ||
         !std::strcmp(native.jit.backendName(), "x86-64"));
    return s.blocksCompiled != 0 && s.blocksRun != 0 &&
           (!nativeProduction || s.slowInstrs == 0) &&
           native.getA(0) == kData + 1 && native.getA(1) == kData + 1;
}

bool runLegalReadOnlyEaLockstep(bool packedCcr) {
    jit::ResolvedConfig config = injectedJitConfig();
    config.packedCcr = packedCcr;
    SyntheticCpu ref(config), native(config);
    installLegalReadOnlyEaLoop(ref);
    installLegalReadOnlyEaLoop(native);
    resetCpu(ref);
    resetCpu(native);
    ref.setSR(ref.getSR() | 0x10);       // X must survive every TST/CMPI
    native.setSR(native.getSR() | 0x10);
    native.jit.setEnabled(true);

    for (int step = 0; step < 256; step++) {
        const int64_t target = ref.getClock() + 47;
        ref.executeUntil(target);
        native.jit.executeUntil(target);
        if (!sameCpu(ref, native, true) ||
            !sameMemory(ref, native, 0, kRamBytes, true)) {
            std::printf("    legal read-only EA divergence at checkpoint %d\n",
                        step);
            return false;
        }
    }
    const auto s = native.jit.stats().snapshot();
    std::printf("    legal-read-EA packed=%d compiled=%llu runs=%llu "
                "native=%llu slow=%llu\n",
                int(packedCcr),
                (unsigned long long)s.blocksCompiled,
                (unsigned long long)s.blocksRun,
                (unsigned long long)s.instrs,
                (unsigned long long)s.slowInstrs);
    const bool nativeProduction =
        !std::strcmp(native.jit.backendName(), "aarch64") ||
        !std::strcmp(native.jit.backendName(), "x86-64");
    return s.blocksCompiled != 0 && s.blocksRun != 0 &&
           (!nativeProduction || s.slowInstrs == 0) &&
           s.instrs == 2047 && // also pins packed CCR/retired-count isolation
           (native.getSR() & 0x10) != 0;
}

enum class BitRmwBoundary { Device, Fault };

void prepareBitRmwBoundary(SyntheticCpu& c, BitRmwBoundary boundary) {
    c.setPC(kCode);
    c.setPC0(kCode);
    c.setIRD(0x0850);
    c.setIRC(0x0000);
    c.setA(0, boundary == BitRmwBoundary::Device ? kDevice : kHole);
    c.setA(7, kStack);
    c.setSR(0x271F);
    c.setClock(0);
    c.busFaults = 0;
    c.deviceReads = 0;
    c.deviceWrites = 0;
    c.deviceValue = 0xA5;
    clearRunFlags(c);
}

bool runBitRmwBoundary(BitRmwBoundary boundary, bool packedCcr) {
    jit::ResolvedConfig config = injectedJitConfig();
    config.packedCcr = packedCcr;
    SyntheticCpu ref(config), native(config);
    installBitRmwLoop(ref);
    installBitRmwLoop(native);
    resetCpu(ref);
    resetCpu(native);

    native.setA(0, kData);
    native.jit.setEnabled(true);
    native.jit.executeUntil(native.getClock() + 512);
    const auto trained = native.jit.stats().snapshot();
    if (trained.blocksCompiled == 0) return false;

    ref.mem = native.mem;
    prepareBitRmwBoundary(ref, boundary);
    prepareBitRmwBoundary(native, boundary);
    const auto before = native.jit.stats().snapshot();
    ref.executeUntil(ref.getClock() + 1);
    native.jit.executeUntil(native.getClock() + 1);
    const auto after = native.jit.stats().snapshot();

    const bool same = sameCpu(ref, native, true) &&
        sameMemory(ref, native, 0, kRamBytes, true);
    const bool nativeEntry = after.blocksRun > before.blocksRun;
    if (boundary == BitRmwBoundary::Device) {
        std::printf("    bit-RMW MMIO packed=%d reads=%u/%u writes=%u/%u "
                    "value=%02X/%02X slow=%llu\n",
                    int(packedCcr),
                    ref.deviceReads, native.deviceReads,
                    ref.deviceWrites, native.deviceWrites,
                    ref.deviceValue, native.deviceValue,
                    (unsigned long long)(after.slowInstrs - before.slowInstrs));
        return same && nativeEntry && ref.deviceReads == 1 &&
               native.deviceReads == 1 && ref.deviceWrites == 1 &&
               native.deviceWrites == 1 && ref.deviceValue == 0xA4 &&
               native.deviceValue == ref.deviceValue &&
               after.slowInstrs == before.slowInstrs + 1;
    }

    const bool frame = ref.getA(7) == kStack - 60 &&
        native.getA(7) == ref.getA(7) &&
        sameMemory(ref, native, ref.getA(7), 60, true);
    return same && frame && nativeEntry && ref.busFaults != 0 &&
           native.busFaults != 0 && ref.deviceReads == 0 &&
           native.deviceReads == 0 && ref.deviceWrites == 0 &&
           native.deviceWrites == 0;
}

bool runDynamicBitfieldLockstep() {
    SyntheticCpu ref, native;
    installDynamicBitfieldLoop(ref, true);
    installDynamicBitfieldLoop(native, true);
    resetCpu(ref);
    resetCpu(native);
    // Keep D1=-3's adjusted longword and optional fifth byte on one guest
    // page so this test exercises the native two-read protocol, not its
    // intentional cross-page fallback.
    ref.setA(0, kData + 4);
    native.setA(0, kData + 4);
    ref.setA(1, kData);
    native.setA(1, kData);
    native.jit.setEnabled(true);

    for (int step = 0; step < 256; step++) {
        const int64_t target = ref.getClock() + 43;
        ref.executeUntil(target);
        native.jit.executeUntil(target);
        if (!sameCpu(ref, native, true) ||
            !sameMemory(ref, native, 0, kRamBytes, true)) {
            std::printf("    dynamic-bitfield divergence at checkpoint %d\n",
                        step);
            return false;
        }
    }
    const auto s = native.jit.stats().snapshot();
    std::printf("    dynamic-bitfield compiled=%llu runs=%llu native=%llu slow=%llu\n",
                (unsigned long long)s.blocksCompiled,
                (unsigned long long)s.blocksRun,
                (unsigned long long)s.instrs,
                (unsigned long long)s.slowInstrs);
    const bool nativeProduction =
        (!std::strcmp(native.jit.backendName(), "aarch64") ||
         !std::strcmp(native.jit.backendName(), "x86-64")) &&
        !native.jit.config().packedCcr;
    return s.blocksCompiled != 0 && s.blocksRun != 0 &&
           (!nativeProduction || s.slowInstrs == 0);
}

bool runDynamicRegisterBitfieldLockstep() {
    SyntheticCpu ref, native;
    installDynamicBitfieldLoop(ref, false);
    installDynamicBitfieldLoop(native, false);
    resetCpu(ref);
    resetCpu(native);
    native.jit.setEnabled(true);

    for (int step = 0; step < 256; step++) {
        const int64_t target = ref.getClock() + 43;
        ref.executeUntil(target);
        native.jit.executeUntil(target);
        if (!sameCpu(ref, native, true) ||
            !sameMemory(ref, native, 0, kRamBytes, true)) {
            std::printf("    dynamic-register-bitfield divergence at checkpoint %d\n",
                        step);
            return false;
        }
    }
    const auto s = native.jit.stats().snapshot();
    std::printf("    dynamic-register-bitfield compiled=%llu runs=%llu native=%llu slow=%llu\n",
                (unsigned long long)s.blocksCompiled,
                (unsigned long long)s.blocksRun,
                (unsigned long long)s.instrs,
                (unsigned long long)s.slowInstrs);
    const bool nativeProduction =
        (!std::strcmp(native.jit.backendName(), "aarch64") ||
         !std::strcmp(native.jit.backendName(), "x86-64")) &&
        !native.jit.config().packedCcr;
    return s.blocksCompiled != 0 && s.blocksRun != 0 &&
           (!nativeProduction || s.slowInstrs == 0);
}

bool runStaticBitfieldLockstep() {
    SyntheticCpu ref, native;
    installStaticBitfieldLoop(ref);
    installStaticBitfieldLoop(native);
    resetCpu(ref);
    resetCpu(native);
    native.jit.setEnabled(true);

    for (int step = 0; step < 256; step++) {
        const int64_t target = ref.getClock() + 43;
        ref.executeUntil(target);
        native.jit.executeUntil(target);
        if (!sameCpu(ref, native, true) ||
            !sameMemory(ref, native, 0, kRamBytes, true)) {
            std::printf("    static-bitfield divergence at checkpoint %d\n",
                        step);
            return false;
        }
    }
    const auto s = native.jit.stats().snapshot();
    std::printf("    static-bitfield compiled=%llu runs=%llu native=%llu slow=%llu\n",
                (unsigned long long)s.blocksCompiled,
                (unsigned long long)s.blocksRun,
                (unsigned long long)s.instrs,
                (unsigned long long)s.slowInstrs);
    // Like the dedicated dynamic-register scenario above, this residency
    // claim holds on BOTH production generators (2026-08-30).
    const bool nativeProduction =
        (!std::strcmp(native.jit.backendName(), "aarch64") ||
         !std::strcmp(native.jit.backendName(), "x86-64")) &&
        !native.jit.config().packedCcr;
    return s.blocksCompiled != 0 && s.blocksRun != 0 &&
           (!nativeProduction || s.slowInstrs == 0);
}

bool runMemoryBitfieldLockstep() {
    SyntheticCpu ref, native;
    installMemoryBitfieldLoop(ref);
    installMemoryBitfieldLoop(native);
    resetCpu(ref);
    resetCpu(native);
    // D1 = -3 adjusts the base by floor(-3/8) = -1 byte; keep the adjusted
    // longword inside kData's page so the native path is exercised, not the
    // cross-page fallback.
    ref.setA(0, kData + 4);
    native.setA(0, kData + 4);
    ref.setA(1, kData);
    native.setA(1, kData);
    native.jit.setEnabled(true);

    for (int step = 0; step < 256; step++) {
        const int64_t target = ref.getClock() + 43;
        ref.executeUntil(target);
        native.jit.executeUntil(target);
        if (!sameCpu(ref, native, true) ||
            !sameMemory(ref, native, 0, kRamBytes, true)) {
            std::printf("    memory-bitfield divergence at checkpoint %d\n",
                        step);
            return false;
        }
    }
    const auto s = native.jit.stats().snapshot();
    std::printf("    memory-bitfield compiled=%llu runs=%llu native=%llu slow=%llu\n",
                (unsigned long long)s.blocksCompiled,
                (unsigned long long)s.blocksRun,
                (unsigned long long)s.instrs,
                (unsigned long long)s.slowInstrs);
    const bool nativeProduction =
        (!std::strcmp(native.jit.backendName(), "aarch64") ||
         !std::strcmp(native.jit.backendName(), "x86-64")) &&
        !native.jit.config().packedCcr;
    return s.blocksCompiled != 0 && s.blocksRun != 0 &&
           (!nativeProduction || s.slowInstrs == 0);
}

bool runMemoryBitfieldWriteLockstep() {
    SyntheticCpu ref, native;
    installMemoryBitfieldWriteLoop(ref);
    installMemoryBitfieldWriteLoop(native);
    resetCpu(ref);
    resetCpu(native);
    ref.setA(0, kData + 4);
    native.setA(0, kData + 4);
    ref.setA(1, kData + 4);
    native.setA(1, kData + 4);
    native.jit.setEnabled(true);

    for (int step = 0; step < 256; step++) {
        const int64_t target = ref.getClock() + 43;
        ref.executeUntil(target);
        native.jit.executeUntil(target);
        if (!sameCpu(ref, native, true) ||
            !sameMemory(ref, native, 0, kRamBytes, true)) {
            std::printf("    memory-bitfield-write divergence at checkpoint %d\n",
                        step);
            return false;
        }
    }
    const auto s = native.jit.stats().snapshot();
    std::printf("    memory-bitfield-write compiled=%llu runs=%llu native=%llu slow=%llu\n",
                (unsigned long long)s.blocksCompiled,
                (unsigned long long)s.blocksRun,
                (unsigned long long)s.instrs,
                (unsigned long long)s.slowInstrs);
    const bool nativeProduction =
        (!std::strcmp(native.jit.backendName(), "aarch64") ||
         !std::strcmp(native.jit.backendName(), "x86-64")) &&
        !native.jit.config().packedCcr;
    return s.blocksCompiled != 0 && s.blocksRun != 0 &&
           (!nativeProduction || s.slowInstrs == 0);
}

bool runWordMultiplyLockstep() {
    SyntheticCpu ref, native;
    installWordMultiplyLoop(ref);
    installWordMultiplyLoop(native);
    resetCpu(ref);
    resetCpu(native);
    native.jit.setEnabled(true);
    for (int step = 0; step < 256; step++) {
        const int64_t target = ref.getClock() + 67;
        ref.executeUntil(target);
        native.jit.executeUntil(target);
        if (!sameCpu(ref, native, true) ||
            !sameMemory(ref, native, 0, kRamBytes, true)) {
            std::printf("    word-multiply divergence checkpoint=%d\n", step);
            return false;
        }
    }
    const auto s = native.jit.stats().snapshot();
    std::printf("    word-multiply compiled=%llu runs=%llu native=%llu slow=%llu\n",
                (unsigned long long)s.blocksCompiled,
                (unsigned long long)s.blocksRun,
                (unsigned long long)s.instrs,
                (unsigned long long)s.slowInstrs);
    const bool nativeGenerator =
        !std::strcmp(native.jit.backendName(), "aarch64") ||
        !std::strcmp(native.jit.backendName(), "x86-64");
    return s.blocksCompiled != 0 && s.blocksRun != 0 &&
           (!nativeGenerator || s.slowInstrs == 0);
}

bool runAddSubExtendLockstep() {
    SyntheticCpu ref, native;
    installAddSubExtendLoop(ref);
    installAddSubExtendLoop(native);
    resetCpu(ref);
    resetCpu(native);
    native.jit.setEnabled(true);
    for (int step = 0; step < 256; step++) {
        const int64_t target = ref.getClock() + 71;
        ref.executeUntil(target);
        native.jit.executeUntil(target);
        if (!sameCpu(ref, native, true) ||
            !sameMemory(ref, native, 0, kRamBytes, true)) {
            std::printf("    ADDX/SUBX divergence checkpoint=%d\n", step);
            return false;
        }
    }
    const auto s = native.jit.stats().snapshot();
    std::printf("    ADDX/SUBX compiled=%llu runs=%llu native=%llu slow=%llu\n",
                (unsigned long long)s.blocksCompiled,
                (unsigned long long)s.blocksRun,
                (unsigned long long)s.instrs,
                (unsigned long long)s.slowInstrs);
    const bool nativeGenerator =
        !std::strcmp(native.jit.backendName(), "aarch64") ||
        !std::strcmp(native.jit.backendName(), "x86-64");
    return s.blocksCompiled != 0 && s.blocksRun != 0 &&
           (!nativeGenerator || s.slowInstrs == 0);
}

bool runExtendByteLockstep() {
    SyntheticCpu ref, native;
    installExtendByteLoop(ref);
    installExtendByteLoop(native);
    resetCpu(ref);
    resetCpu(native);
    ref.setSR(0x2710);                 // X=1 must survive all three EXTB.L
    native.setSR(0x2710);
    native.jit.setEnabled(true);
    for (int step = 0; step < 256; step++) {
        const int64_t target = ref.getClock() + 43;
        ref.executeUntil(target);
        native.jit.executeUntil(target);
        if (!sameCpu(ref, native, true) ||
            !sameMemory(ref, native, 0, kRamBytes, true)) {
            std::printf("    EXTB.L divergence checkpoint=%d\n", step);
            return false;
        }
    }
    const auto s = native.jit.stats().snapshot();
    std::printf("    EXTB.L compiled=%llu runs=%llu native=%llu slow=%llu\n",
                (unsigned long long)s.blocksCompiled,
                (unsigned long long)s.blocksRun,
                (unsigned long long)s.instrs,
                (unsigned long long)s.slowInstrs);
    const bool nativeGenerator =
        !std::strcmp(native.jit.backendName(), "aarch64") ||
        !std::strcmp(native.jit.backendName(), "x86-64");
    return s.blocksCompiled != 0 && s.blocksRun != 0 &&
           (!nativeGenerator || s.slowInstrs == 0);
}

bool runMemoryMultiplyDeviceOnce() {
    SyntheticCpu ref, native;
    installMemoryMultiplyDeviceLoop(ref);
    installMemoryMultiplyDeviceLoop(native);
    resetCpu(ref);
    resetCpu(native);
    native.setA(0, kData);
    native.setD(0, 3);
    native.jit.setEnabled(true);
    native.jit.executeUntil(native.getClock() + 1024);
    if (native.jit.stats().snapshot().blocksCompiled == 0) return false;
    ref.mem = native.mem;
    for (SyntheticCpu* c : {&ref, &native}) {
        c->setA(0, kDevice);
        c->setA(7, kStack);
        c->setD(0, 3);
        c->setPC(kCode);
        c->setPC0(kCode);
        c->setIRD(0xC0D8);
        c->setIRC(0x60FC);
        c->setSR(0x2710);
        c->setClock(0);
        c->deviceReads = 0;
        clearRunFlags(*c);
    }
    const auto before = native.jit.stats().snapshot();
    ref.executeUntil(ref.getClock() + 1);
    native.jit.executeUntil(native.getClock() + 1);
    const auto after = native.jit.stats().snapshot();
    const bool same = sameCpu(ref, native, true) &&
        sameMemory(ref, native, 0, kRamBytes, true);
    std::printf("    word-multiply device reads=%u/%u d0=%u/%u "
                "runs=%llu/%llu slow=%llu/%llu\n",
                ref.deviceReads, native.deviceReads,
                ref.getD(0), native.getD(0),
                (unsigned long long)before.blocksRun,
                (unsigned long long)after.blocksRun,
                (unsigned long long)before.slowInstrs,
                (unsigned long long)after.slowInstrs);
    return same && ref.deviceReads == 1 && native.deviceReads == 1 &&
           ref.getD(0) == 21 && ref.getA(0) == kDevice + 2 &&
           after.blocksRun > before.blocksRun &&
           after.slowInstrs == before.slowInstrs;
}

enum class DivisionProgram { Success, Overflow, Zero };

bool runWordDivisionLockstep(DivisionProgram program) {
    SyntheticCpu ref, native;
    switch (program) {
        case DivisionProgram::Success:
            installWordDivisionLoop(ref);
            installWordDivisionLoop(native);
            break;
        case DivisionProgram::Overflow:
            installWordDivisionOverflowLoop(ref);
            installWordDivisionOverflowLoop(native);
            break;
        case DivisionProgram::Zero:
            installWordDivisionZeroLoop(ref);
            installWordDivisionZeroLoop(native);
            break;
    }
    resetCpu(ref);
    resetCpu(native);
    if (program == DivisionProgram::Overflow) {
        // Train all three immediate forms on their full-cost success path.
        // The same generated code is then entered with overflow dividends,
        // proving the runtime guards rather than a trace-time cost refusal.
        native.setD(0, 100);
        native.setD(1, 0);
        native.setD(2, 100);
        native.jit.setEnabled(true);
        native.jit.executeUntil(native.getClock() + 1024);
        if (native.jit.stats().snapshot().blocksCompiled == 0) return false;
        ref.mem = native.mem;
        for (SyntheticCpu* c : {&ref, &native}) {
            for (int reg = 0; reg < 8; reg++) {
                c->setD(reg, 0);
                if (reg != 7) c->setA(reg, 0);
            }
            c->setD(0, 65536);
            c->setD(1, 0x8000'0000u);
            c->setD(2, 65536);
            c->setA(7, kStack);
            c->setPC(kCode);
            c->setPC0(kCode);
            c->setIRD(0x81FC);
            c->setIRC(0x0001);
            c->setSR(0x2710);
            c->setClock(0);
            clearRunFlags(*c);
        }
    } else if (program == DivisionProgram::Zero) {
        // An immediate zero traps before the entry can become hot. Train the
        // same register-divisor block with D1=1, then retain its generated
        // code while presenting zero to both architectural states.
        native.setPC(kCode);
        native.setPC0(kCode);
        native.setIRD(0x81C1);
        native.setIRC(0x60FC);
        native.setD(0, 123);
        native.setD(1, 1);
        clearRunFlags(native);
        native.jit.setEnabled(true);
        native.jit.executeUntil(native.getClock() + 1024);
        if (native.jit.stats().snapshot().blocksCompiled == 0) return false;
        ref.mem = native.mem;
        for (SyntheticCpu* c : {&ref, &native}) {
            for (int reg = 0; reg < 8; reg++) {
                c->setD(reg, 0);
                if (reg != 7) c->setA(reg, 0);
            }
            c->setD(1, 0);
            c->setA(7, kStack);
            c->setD(0, 123);
            c->setPC(kCode);
            c->setPC0(kCode);
            c->setIRD(0x81C1);
            c->setIRC(0x60FC);
            c->setSR(0x2710);
            c->setClock(0);
            clearRunFlags(*c);
        }
        // Like the /BERR oracle, start directly on the faulting instruction
        // and give it a one-cycle target so both sides stop at the vector-5
        // boundary instead of consuming the handler's RTE as well.
        const auto before = native.jit.stats().snapshot();
        ref.executeUntil(ref.getClock() + 1);
        native.jit.executeUntil(native.getClock() + 1);
        const auto after = native.jit.stats().snapshot();
        const bool boundary = sameCpu(ref, native, true);
        const bool frame = ref.getA(7) == kStack - 12 &&
            native.getA(7) == ref.getA(7) &&
            sameMemory(ref, native, ref.getA(7), 12, true);
        const bool ram = sameMemory(ref, native, 0, kRamBytes, true);
        std::printf("    word-division zero compiled=%llu runs=%llu/%llu "
                    "sp=%08X/%08X boundary=%d frame=%d ram=%d\n",
                    (unsigned long long)after.blocksCompiled,
                    (unsigned long long)before.blocksRun,
                    (unsigned long long)after.blocksRun,
                    ref.getA(7), native.getA(7), boundary, frame, ram);
        return boundary && frame && ram && after.blocksRun > before.blocksRun;
    } else {
        native.jit.setEnabled(true);
    }

    for (int step = 0; step < 256; step++) {
        const int64_t target = ref.getClock() + 67;
        ref.executeUntil(target);
        native.jit.executeUntil(target);
        if (!sameCpu(ref, native, true) ||
            !sameMemory(ref, native, 0, kRamBytes, true)) {
            std::printf("    word-division divergence program=%d checkpoint=%d\n",
                        int(program), step);
            return false;
        }
    }
    const auto s = native.jit.stats().snapshot();
    std::printf("    word-division program=%d compiled=%llu runs=%llu "
                "native=%llu slow=%llu\n",
                int(program), (unsigned long long)s.blocksCompiled,
                (unsigned long long)s.blocksRun,
                (unsigned long long)s.instrs,
                (unsigned long long)s.slowInstrs);
    const bool nativeGenerator =
        !std::strcmp(native.jit.backendName(), "aarch64") ||
        !std::strcmp(native.jit.backendName(), "x86-64");
    const bool expectedResidency = program == DivisionProgram::Success
        ? s.slowInstrs == 0 : s.slowInstrs != 0;
    return s.blocksCompiled != 0 && s.blocksRun != 0 &&
           (!nativeGenerator || expectedResidency);
}

bool runLongDivisionLockstep(DivisionProgram program) {
    SyntheticCpu ref, native;
    switch (program) {
        case DivisionProgram::Success:
            installLongDivisionLoop(ref);
            installLongDivisionLoop(native);
            break;
        case DivisionProgram::Overflow:
            installLongDivisionOverflowLoop(ref);
            installLongDivisionOverflowLoop(native);
            break;
        case DivisionProgram::Zero:
            installLongDivisionZeroLoop(ref);
            installLongDivisionZeroLoop(native);
            break;
    }
    resetCpu(ref);
    resetCpu(native);
    if (program == DivisionProgram::Overflow) {
        // First compile the three guards on stable, successful zero
        // dividends, then retain that code while presenting each overflow.
        native.setD(0, 0xFFFF'FFFFu);
        native.jit.setEnabled(true);
        native.jit.executeUntil(native.getClock() + 2048);
        if (native.jit.stats().snapshot().blocksCompiled == 0) return false;
        ref.mem = native.mem;
        for (SyntheticCpu* c : {&ref, &native}) {
            for (int reg = 0; reg < 8; reg++) {
                c->setD(reg, 0);
                if (reg != 7) c->setA(reg, 0);
            }
            c->setD(0, 0xFFFF'FFFFu);
            c->setD(1, 0x8000'0000u);  // int32 MIN / -1
            c->setD(2, 0x1357'9BDFu);
            c->setD(3, 0xFFFF'FFFFu);  // UINT64_MAX / UINT32_MAX
            c->setD(4, 0xFFFF'FFFFu);
            c->setD(5, 0);             // INT64_MIN / -1
            c->setD(6, 0x8000'0000u);
            c->setA(7, kStack);
            c->setPC(kCode);
            c->setPC0(kCode);
            c->setIRD(0x4C40);
            c->setIRC(0x1802);
            c->setSR(0x2710);
            c->setClock(0);
            clearRunFlags(*c);
        }
    } else if (program == DivisionProgram::Zero) {
        native.setD(0, 1);
        native.setD(1, 123);
        native.jit.setEnabled(true);
        native.jit.executeUntil(native.getClock() + 2048);
        if (native.jit.stats().snapshot().blocksCompiled == 0) return false;
        ref.mem = native.mem;
        for (SyntheticCpu* c : {&ref, &native}) {
            for (int reg = 0; reg < 8; reg++) {
                c->setD(reg, 0);
                if (reg != 7) c->setA(reg, 0);
            }
            c->setD(1, 123);
            c->setA(7, kStack);
            c->setPC(kCode);
            c->setPC0(kCode);
            c->setIRD(0x4C40);
            c->setIRC(0x1C02);
            c->setSR(0x2710);
            c->setClock(0);
            clearRunFlags(*c);
        }
        const auto before = native.jit.stats().snapshot();
        ref.executeUntil(ref.getClock() + 1);
        native.jit.executeUntil(native.getClock() + 1);
        const auto after = native.jit.stats().snapshot();
        const bool boundary = sameCpu(ref, native, true);
        const bool frame = ref.getA(7) == kStack - 12 &&
            native.getA(7) == ref.getA(7) &&
            sameMemory(ref, native, ref.getA(7), 12, true);
        const bool ram = sameMemory(ref, native, 0, kRamBytes, true);
        std::printf("    long-division zero compiled=%llu runs=%llu/%llu "
                    "sp=%08X/%08X boundary=%d frame=%d ram=%d\n",
                    (unsigned long long)after.blocksCompiled,
                    (unsigned long long)before.blocksRun,
                    (unsigned long long)after.blocksRun,
                    ref.getA(7), native.getA(7), boundary, frame, ram);
        return boundary && frame && ram && after.blocksRun > before.blocksRun;
    } else {
        native.jit.setEnabled(true);
    }

    for (int step = 0; step < 256; step++) {
        const int64_t target = ref.getClock() + 97;
        ref.executeUntil(target);
        native.jit.executeUntil(target);
        if (!sameCpu(ref, native, true) ||
            !sameMemory(ref, native, 0, kRamBytes, true)) {
            std::printf("    long-division divergence program=%d checkpoint=%d\n",
                        int(program), step);
            return false;
        }
    }
    const auto s = native.jit.stats().snapshot();
    std::printf("    long-division program=%d compiled=%llu runs=%llu "
                "native=%llu slow=%llu\n",
                int(program), (unsigned long long)s.blocksCompiled,
                (unsigned long long)s.blocksRun,
                (unsigned long long)s.instrs,
                (unsigned long long)s.slowInstrs);
    const bool nativeGenerator =
        !std::strcmp(native.jit.backendName(), "aarch64") ||
        !std::strcmp(native.jit.backendName(), "x86-64");
    const bool expectedResidency = program == DivisionProgram::Success
        ? s.slowInstrs == 0 : s.slowInstrs != 0;
    return s.blocksCompiled != 0 && s.blocksRun != 0 &&
           (!nativeGenerator || expectedResidency);
}

bool runMemoryDivisionLockstep() {
    SyntheticCpu ref, native;
    installMemoryDivisionLoop(ref);
    installMemoryDivisionLoop(native);
    resetCpu(ref);
    resetCpu(native);
    native.jit.setEnabled(true);
    for (int step = 0; step < 256; step++) {
        const int64_t target = ref.getClock() + 97;
        ref.executeUntil(target);
        native.jit.executeUntil(target);
        if (!sameCpu(ref, native, true) ||
            !sameMemory(ref, native, 0, kRamBytes, true)) {
            std::printf("    memory-division divergence checkpoint=%d\n", step);
            return false;
        }
    }
    const auto s = native.jit.stats().snapshot();
    std::printf("    memory-division compiled=%llu runs=%llu native=%llu slow=%llu\n",
                (unsigned long long)s.blocksCompiled,
                (unsigned long long)s.blocksRun,
                (unsigned long long)s.instrs,
                (unsigned long long)s.slowInstrs);
    const bool nativeGenerator =
        !std::strcmp(native.jit.backendName(), "aarch64") ||
        !std::strcmp(native.jit.backendName(), "x86-64");
    return s.blocksCompiled != 0 && s.blocksRun != 0 &&
           (!nativeGenerator || s.slowInstrs == 0);
}

bool runMemoryDivisionOverflowLockstep() {
    SyntheticCpu ref, native;
    installMemoryDivisionGuardLoop(ref);
    installMemoryDivisionGuardLoop(native);
    resetCpu(ref);
    resetCpu(native);
    native.setA(0, kData);
    native.jit.setEnabled(true);
    native.jit.executeUntil(native.getClock() + 2048);
    if (native.jit.stats().snapshot().blocksCompiled == 0) return false;
    ref.mem = native.mem;
    for (SyntheticCpu* c : {&ref, &native}) {
        for (int reg = 0; reg < 8; reg++) {
            c->setD(reg, 0);
            if (reg != 7) c->setA(reg, 0);
        }
        put32(*c, kData, 0xFFFF'FFFFu);
        c->setD(0, 0x8000'0000u);      // signed word overflow / -1
        c->setD(1, 0);
        c->setD(2, 0x8000'0000u);      // signed INT64_MIN / -1
        c->setA(0, kData);
        c->setA(7, kStack);
        c->setPC(kCode);
        c->setPC0(kCode);
        c->setIRD(0x81D0);
        c->setIRC(0x4C50);
        c->setSR(0x2710);
        c->setClock(0);
        clearRunFlags(*c);
    }
    for (int step = 0; step < 256; step++) {
        const int64_t target = ref.getClock() + 97;
        ref.executeUntil(target);
        native.jit.executeUntil(target);
        if (!sameCpu(ref, native, true) ||
            !sameMemory(ref, native, 0, kRamBytes, true)) {
            std::printf("    memory-division overflow divergence checkpoint=%d\n",
                        step);
            return false;
        }
    }
    const auto s = native.jit.stats().snapshot();
    std::printf("    memory-division overflow compiled=%llu runs=%llu "
                "native=%llu slow=%llu\n",
                (unsigned long long)s.blocksCompiled,
                (unsigned long long)s.blocksRun,
                (unsigned long long)s.instrs,
                (unsigned long long)s.slowInstrs);
    const bool nativeGenerator =
        !std::strcmp(native.jit.backendName(), "aarch64") ||
        !std::strcmp(native.jit.backendName(), "x86-64");
    return s.blocksCompiled != 0 && s.blocksRun != 0 &&
           (!nativeGenerator || s.slowInstrs != 0);
}

bool runMemoryLongDivisionZeroLockstep() {
    SyntheticCpu ref, native;
    installMemoryLongDivisionZeroLoop(ref);
    installMemoryLongDivisionZeroLoop(native);
    resetCpu(ref);
    resetCpu(native);
    native.setA(0, kData);
    native.setD(1, 123);
    native.jit.setEnabled(true);
    native.jit.executeUntil(native.getClock() + 2048);
    if (native.jit.stats().snapshot().blocksCompiled == 0) return false;
    ref.mem = native.mem;
    for (SyntheticCpu* c : {&ref, &native}) {
        for (int reg = 0; reg < 8; reg++) {
            c->setD(reg, 0);
            if (reg != 7) c->setA(reg, 0);
        }
        put32(*c, kData, 0);
        c->setD(1, 123);
        c->setA(0, kData);
        c->setA(7, kStack);
        c->setPC(kCode);
        c->setPC0(kCode);
        c->setIRD(0x4C50);
        c->setIRC(0x1C02);
        c->setSR(0x2710);
        c->setClock(0);
        clearRunFlags(*c);
    }
    const auto before = native.jit.stats().snapshot();
    ref.executeUntil(ref.getClock() + 1);
    native.jit.executeUntil(native.getClock() + 1);
    const auto after = native.jit.stats().snapshot();
    const bool boundary = sameCpu(ref, native, true);
    const bool frame = ref.getA(7) == kStack - 12 &&
        native.getA(7) == ref.getA(7) &&
        sameMemory(ref, native, ref.getA(7), 12, true);
    const bool ram = sameMemory(ref, native, 0, kRamBytes, true);
    std::printf("    memory-division zero compiled=%llu runs=%llu/%llu "
                "boundary=%d frame=%d ram=%d\n",
                (unsigned long long)after.blocksCompiled,
                (unsigned long long)before.blocksRun,
                (unsigned long long)after.blocksRun,
                boundary, frame, ram);
    return boundary && frame && ram && after.blocksRun > before.blocksRun;
}

bool runMemoryDivisionDeviceOnce() {
    SyntheticCpu ref, native;
    installMemoryDivisionDeviceLoop(ref);
    installMemoryDivisionDeviceLoop(native);
    resetCpu(ref);
    resetCpu(native);
    native.setA(0, kData);
    native.setD(0, 100);
    native.jit.setEnabled(true);
    native.jit.executeUntil(native.getClock() + 1024);
    if (native.jit.stats().snapshot().blocksCompiled == 0) return false;
    ref.mem = native.mem;
    for (SyntheticCpu* c : {&ref, &native}) {
        c->setA(0, kDevice);
        c->setA(7, kStack);
        c->setD(0, 100);
        c->setPC(kCode);
        c->setPC0(kCode);
        c->setIRD(0x80D0);
        c->setIRC(0x60FC);
        c->setSR(0x2710);
        c->setClock(0);
        c->deviceReads = 0;
        clearRunFlags(*c);
    }
    const auto before = native.jit.stats().snapshot();
    ref.executeUntil(ref.getClock() + 1);
    native.jit.executeUntil(native.getClock() + 1);
    const auto after = native.jit.stats().snapshot();
    const bool same = sameCpu(ref, native, true) &&
        sameMemory(ref, native, 0, kRamBytes, true);
    std::printf("    memory-division device reads=%u/%u runs=%llu/%llu slow=%llu\n",
                ref.deviceReads, native.deviceReads,
                (unsigned long long)before.blocksRun,
                (unsigned long long)after.blocksRun,
                (unsigned long long)after.slowInstrs);
    return same && ref.deviceReads == 1 && native.deviceReads == 1 &&
           after.blocksRun > before.blocksRun &&
           after.slowInstrs > before.slowInstrs;
}

bool runCachedMemoryDivisionLockstep() {
    SyntheticCpu ref, native;
    installCachedMemoryDivisionLoop(ref);
    installCachedMemoryDivisionLoop(native);
    resetCpu(ref);
    resetCpu(native);
    for (SyntheticCpu* c : {&ref, &native}) {
        c->setCACR(0x8000'0000u);       // 68040 data cache enabled
        c->setA(0, kData);
    }
    native.jit.setEnabled(true);
    for (int step = 0; step < 256; step++) {
        const int64_t target = ref.getClock() + 67;
        ref.executeUntil(target);
        native.jit.executeUntil(target);
        if (!sameCpu(ref, native, true) ||
            !sameMemory(ref, native, 0, kRamBytes, true)) {
            std::printf("    cached memory-division divergence checkpoint=%d\n",
                        step);
            return false;
        }
    }
    const auto s = native.jit.stats().snapshot();
    std::printf("    cached memory-division compiled=%llu runs=%llu "
                "native=%llu slow=%llu\n",
                (unsigned long long)s.blocksCompiled,
                (unsigned long long)s.blocksRun,
                (unsigned long long)s.instrs,
                (unsigned long long)s.slowInstrs);
    const bool nativeGenerator =
        !std::strcmp(native.jit.backendName(), "aarch64") ||
        !std::strcmp(native.jit.backendName(), "x86-64");
    return s.blocksCompiled != 0 && s.blocksRun != 0 &&
           (!nativeGenerator || s.slowInstrs == 0);
}

bool runGuardedDynamicShiftLockstep(uint8_t count) {
    SyntheticCpu ref, native;
    installGuardedDynamicShiftLoop(ref, count);
    installGuardedDynamicShiftLoop(native, count);
    resetCpu(ref);
    resetCpu(native);
    native.jit.setEnabled(true);
    for (int step = 0; step < 128; step++) {
        const int64_t target = ref.getClock() + 41;
        ref.executeUntil(target);
        native.jit.executeUntil(target);
        if (!sameCpu(ref, native, true) ||
            !sameMemory(ref, native, 0, kRamBytes, true)) {
            std::printf("    guarded dynamic-shift divergence at checkpoint %d\n",
                        step);
            return false;
        }
    }
    const auto s = native.jit.stats().snapshot();
    std::printf("    guarded dynamic-shift count=%u compiled=%llu runs=%llu "
                "native=%llu slow=%llu\n", unsigned(count),
                (unsigned long long)s.blocksCompiled,
                (unsigned long long)s.blocksRun,
                (unsigned long long)s.instrs,
                (unsigned long long)s.slowInstrs);
    const bool nativeProduction =
        (!std::strcmp(native.jit.backendName(), "aarch64") ||
         !std::strcmp(native.jit.backendName(), "x86-64")) &&
        !native.jit.config().packedCcr;
    return s.blocksCompiled != 0 && s.blocksRun != 0 &&
           (!nativeProduction || s.slowInstrs == 0);
}

void positionAt(SyntheticCpu& c, uint32_t pc);

bool runMultiVersionShiftLockstep() {
    SyntheticCpu ref, native;
    installMultiVersionShiftLoop(ref);
    installMultiVersionShiftLoop(native);
    resetCpu(ref);
    resetCpu(native);
    for (SyntheticCpu* c : {&ref, &native}) c->setD(1, 31);
    native.jit.setEnabled(true);
    for (int step = 0; step < 384; step++) {
        const int64_t target = ref.getClock() + 97;
        ref.executeUntil(target);
        native.jit.executeUntil(target);
        if (!sameCpu(ref, native, true) ||
            !sameMemory(ref, native, 0, kRamBytes, true)) {
            std::printf("    multi-version shift divergence checkpoint=%d\n",
                        step);
            return false;
        }
    }
    const auto s = native.jit.stats().snapshot();
    const uint32_t mask = jit::EngineGuardIndexProbe::shiftVersionMask(
        native.jit, kCode + 0x0C);
    const unsigned compiled =
        jit::EngineGuardIndexProbe::compiledShiftVersions(native.jit,
                                                           kCode + 0x0C);
    const bool unpublished =
        jit::EngineGuardIndexProbe::shiftTargetUnpublished(native.jit,
                                                            kCode + 0x0C);
    std::printf("    multi-version shift mask=%08X compiled=%u slow=%llu\n",
                mask, compiled, (unsigned long long)s.slowInstrs);
    const bool nativeProduction =
        (!std::strcmp(native.jit.backendName(), "aarch64") ||
         !std::strcmp(native.jit.backendName(), "x86-64")) &&
        !native.jit.config().packedCcr;
    return mask == ((uint32_t(1) << 24) | (uint32_t(1) << 31)) &&
           unpublished && (!nativeProduction ||
               (compiled == 2 && s.slowInstrs == 0));
}

bool runBoundedShiftVersionLockstep() {
    SyntheticCpu ref, native;
    installBoundedShiftVersionLoop(ref);
    installBoundedShiftVersionLoop(native);
    resetCpu(ref);
    resetCpu(native);
    native.jit.setEnabled(true);
    for (uint8_t count = 0; count <= 31; count++) {
        for (SyntheticCpu* c : {&ref, &native}) {
            c->setD(1, count);
            c->setD(3, 0x89AB'CDEFu);
            positionAt(*c, kCode);
        }
        for (int step = 0; step < 96; step++) {
            const int64_t target = ref.getClock() + 89;
            ref.executeUntil(target);
            native.jit.executeUntil(target);
            if (!sameCpu(ref, native, true) ||
                !sameMemory(ref, native, 0, kRamBytes, true)) {
                std::printf("    bounded shift divergence count=%u checkpoint=%d\n",
                            unsigned(count), step);
                return false;
            }
        }
    }
    const uint32_t mask = jit::EngineGuardIndexProbe::shiftVersionMask(
        native.jit, kCode);
    std::printf("    bounded shift versions=%u mask=%08X\n",
                unsigned(std::popcount(mask)), mask);
    if (std::popcount(mask) != 32 || mask != 0xFFFF'FFFFu ||
        !jit::EngineGuardIndexProbe::consistent(native.jit) || !native.guard)
        return false;

    // One code write must evict all 32 physical-footprint aliases, clear
    // their version mask and forget the site hint once no base block remains.
    native.guard->note(kCode, 2);
    positionAt(native, kCode + 2);
    native.jit.executeUntil(native.getClock() + 1);
    return jit::EngineGuardIndexProbe::shiftVersionMask(native.jit, kCode) == 0 &&
           !jit::EngineGuardIndexProbe::knowsShiftSite(native.jit, kCode) &&
           jit::EngineGuardIndexProbe::consistent(native.jit);
}

bool runExactShiftVersionMatrixLockstep() {
    SyntheticCpu ref, native;
    installExactShiftVersionMatrix(ref);
    installExactShiftVersionMatrix(native);
    resetCpu(ref);
    resetCpu(native);
    native.jit.setEnabled(true);
    static constexpr std::array<uint16_t, 4> ops{
        0xE0A9, 0xE2AB, 0xE4A4, 0xE2AA
    };
    static constexpr std::array<unsigned, 4> countRegs{0, 1, 2, 1};
    unsigned compiled = 0;
    for (unsigned op = 0; op < ops.size(); op++)
        for (unsigned count = 0; count <= 31; count++) {
            const uint32_t site = kCode + (op * 32 + count) * 4;
            for (SyntheticCpu* c : {&ref, &native}) {
                for (unsigned reg = 0; reg < 8; reg++)
                    c->setD(reg, 0x89AB'CDEFu ^ (reg * 0x1111'1111u));
                c->setD(countRegs[op], count);
                c->setSR(uint16_t(0x2700 | ((op + count) & 1 ? 0x10 : 0)));
                positionAt(*c, site);
            }
            for (int step = 0; step < 64; step++) {
                const int64_t target = ref.getClock() + 47;
                ref.executeUntil(target);
                native.jit.executeUntil(target);
                if (!sameCpu(ref, native, true) ||
                    !sameMemory(ref, native, 0, kRamBytes, true)) {
                    std::printf("    exact shift matrix divergence op=%04X "
                                "count=%u checkpoint=%d\n",
                                ops[op], count, step);
                    return false;
                }
            }
            const uint32_t mask =
                jit::EngineGuardIndexProbe::shiftVersionMask(native.jit, site);
            const unsigned siteCompiled =
                jit::EngineGuardIndexProbe::compiledShiftVersions(native.jit,
                                                                   site);
            const bool nativeProduction =
                (!std::strcmp(native.jit.backendName(), "aarch64") ||
                 !std::strcmp(native.jit.backendName(), "x86-64")) &&
                !native.jit.config().packedCcr;
            if (nativeProduction &&
                (mask != (uint32_t(1) << count) || siteCompiled != 1)) {
                std::printf("    exact shift matrix not native op=%04X "
                            "count=%u mask=%08X compiled=%u\n",
                            ops[op], count, mask, siteCompiled);
                return false;
            }
            compiled += siteCompiled;
        }
    const auto s = native.jit.stats().snapshot();
    std::printf("    exact shift matrix 4 opcodes x 32 counts: "
                "compiled=%u slow=%llu\n", compiled,
                (unsigned long long)s.slowInstrs);
    const bool nativeProduction =
        (!std::strcmp(native.jit.backendName(), "aarch64") ||
         !std::strcmp(native.jit.backendName(), "x86-64")) &&
        !native.jit.config().packedCcr;
    return !nativeProduction || (compiled == 128 && s.slowInstrs == 0);
}

void positionAt(SyntheticCpu& c, uint32_t pc) {
    c.setPC(pc);
    c.setPC0(pc);
    c.setIRD(moira::u16(c.mem[pc] << 8 | c.mem[pc + 1]));
    c.setIRC(moira::u16(c.mem[pc + 2] << 8 | c.mem[pc + 3]));
    clearRunFlags(c);
}

bool runGuardIndexInvariant() {
    SyntheticCpu c;
    installGuardIndexLoops(c);
    resetCpu(c);
    c.jit.setEnabled(true);

    c.jit.executeUntil(c.getClock() + 256);
    positionAt(c, kGuardB);
    c.jit.executeUntil(c.getClock() + 256);
    positionAt(c, kGuardCross);
    c.jit.executeUntil(c.getClock() + 256);

    // The first 040 I-cache fill can leave one-instruction bootstrap blocks.
    // Drop those after warming all three lines, then record the canonical
    // blocks: A and B share a slice; Cross spans that slice and the next.
    if (!c.guard) return false;
    c.jit.flushAll();
    positionAt(c, kGuardA);
    c.jit.executeUntil(c.getClock() + 64);
    positionAt(c, kGuardB);
    c.jit.executeUntil(c.getClock() + 64);
    positionAt(c, kGuardCross);
    c.jit.executeUntil(c.getClock() + 64);

    constexpr uint32_t slice = kCode >> jit::CodeGuard::kShift;
    constexpr uint32_t nextSlice = slice + 1;
    constexpr uint8_t maskA = 1u <<
        ((kGuardA & (jit::CodeGuard::kUnit - 1)) >> jit::CodeGuard::kSubShift);
    constexpr uint8_t maskB = 1u <<
        ((kGuardB & (jit::CodeGuard::kUnit - 1)) >> jit::CodeGuard::kSubShift);
    constexpr uint8_t maskCross = 1u <<
        ((kGuardCross & (jit::CodeGuard::kUnit - 1)) >> jit::CodeGuard::kSubShift);
    constexpr uint8_t maskCrossNext = 1;
    uint64_t entries = 0;
    const auto verify = [&](uint8_t expectedMask, uint8_t expectedNextMask,
                            uint64_t expectedEntries, const char* phase,
                            int round) {
        const bool exact = jit::EngineGuardIndexProbe::consistent(c.jit, &entries);
        const uint8_t actualMask = c.guard ? c.guard->pageMap[slice] : 0;
        const uint8_t actualNextMask = c.guard
            ? c.guard->pageMap[nextSlice] : 0;
        const bool ok = c.guard && actualMask == expectedMask &&
                        actualNextMask == expectedNextMask && exact &&
                        entries == expectedEntries;
        if (!ok)
            std::printf("    guard-index %s round=%d masks=%02X:%02X/"
                        "%02X:%02X entries=%llu/%llu live=%llu exact=%d\n",
                        phase, round, actualMask, actualNextMask,
                        expectedMask, expectedNextMask,
                        (unsigned long long)entries,
                        (unsigned long long)expectedEntries,
                        (unsigned long long)c.jit.stats().snapshot().blocksLive,
                        exact ? 1 : 0);
        if (!ok) jit::EngineGuardIndexProbe::dump(c.jit);
        return ok;
    };
    if (!verify(uint8_t(maskA | maskB | maskCross), maskCrossNext, 4,
                "trained", -1))
        return false;

    const auto before = c.jit.stats().snapshot();
    constexpr int kRounds = 128;
    for (int round = 0; round < kRounds; round++) {
        // A redundant guest write is still a real guard hit. Service it from
        // B so the evicted A block is not immediately re-recorded.
        c.guard->note(kGuardA, 2);
        positionAt(c, kGuardB);
        c.jit.executeUntil(c.getClock() + 1);
        if (!verify(uint8_t(maskB | maskCross), maskCrossNext, 3,
                    "evict A", round))
            return false;

        positionAt(c, kGuardA);
        c.jit.executeUntil(c.getClock() + 64);
        if (!verify(uint8_t(maskA | maskB | maskCross), maskCrossNext, 4,
                    "record A", round))
            return false;

        c.guard->note(kGuardB, 2);
        positionAt(c, kGuardA);
        c.jit.executeUntil(c.getClock() + 1);
        if (!verify(uint8_t(maskA | maskCross), maskCrossNext, 3,
                    "evict B", round))
            return false;

        positionAt(c, kGuardB);
        c.jit.executeUntil(c.getClock() + 64);
        if (!verify(uint8_t(maskA | maskB | maskCross), maskCrossNext, 4,
                    "record B", round))
            return false;

        // Cross is filed under TWO slices. Evicting it through the first
        // must remove its key and mark from the second as well.
        c.guard->note(kGuardCross, 2);
        positionAt(c, kGuardA);
        c.jit.executeUntil(c.getClock() + 1);
        if (!verify(uint8_t(maskA | maskB), 0, 2, "evict Cross", round))
            return false;

        positionAt(c, kGuardCross);
        c.jit.executeUntil(c.getClock() + 64);
        if (!verify(uint8_t(maskA | maskB | maskCross), maskCrossNext, 4,
                    "record Cross", round))
            return false;
    }
    const auto after = c.jit.stats().snapshot();
    return after.evictions == before.evictions + 3 * kRounds &&
           after.blocksLive == 3 && entries == 4;
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
        if (!jit::emitMetrics(metrics,
                              std::getenv("POM68K_JIT_METRICS_FILE"),
                              std::getenv("POM68K_PERF_HOST_PROFILE")))
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
    check(runExactSourceMove(),
          "exact MMIO source + preflighted RAM destination stays native and exact");
    check(runBriefIndexedLockstep(),
          "brief indexed reads, Scc, PEA and LEA stay native and exact");
    check(runFullDirectLeaLockstep(),
          "direct full-index LEA stays native across base/index suppression");
    check(runFullIndirectLeaLockstep(),
          "memory-indirect full-index LEA/MOVE/CMP stay native and exact");
    check(runFullIndirectJsrLockstep(),
          "memory-indirect full-index JSR preflights pointer and stack exactly");
    check(runDependentMoveLockstep(),
          "dependent (A7)+ destinations use the postincremented base exactly");
    check(runClassicBitLockstep(false),
          "BTST/BCHG/BCLR/BSET register and RAM forms stay native and exact");
    check(runBitRmwBoundary(BitRmwBoundary::Device, false),
          "bit RMW refuses before MMIO read, then replays exactly once");
    check(runBitRmwBoundary(BitRmwBoundary::Fault, false),
          "bit RMW /BERR replays pristine and builds the exact format-$7 frame");
    check(runClassicBitLockstep(true),
          "classic bit operations preserve packed X/N/V/C and update only Z");
    check(runBitRmwBoundary(BitRmwBoundary::Device, true),
          "packed-CCR bit RMW replays MMIO exactly once");
    check(runBitRmwBoundary(BitRmwBoundary::Fault, true),
          "packed-CCR cold fallback preserves the /BERR result and frame");
    check(runLegalReadOnlyEaLockstep(false),
          "TST immediate and CMPI PC-relative stay native and exact");
    check(runLegalReadOnlyEaLockstep(true),
          "legal read-only 020 EAs preserve packed X and publish exact NZVC");
    check(runDynamicBitfieldLockstep(),
          "dynamic register and tailed memory bitfields stay native on BOTH generators");
    check(runDynamicRegisterBitfieldLockstep(),
          "dynamic register bitfields stay native and exact on BOTH generators");
    check(runStaticBitfieldLockstep(),
          "static register bitfields stay native and exact on BOTH generators");
    check(runMemoryBitfieldLockstep(),
          "tailless memory bitfield reads stay native and exact on BOTH generators");
    check(runMemoryBitfieldWriteLockstep(),
          "tailless memory bitfield writes stay native and exact on BOTH generators");
    check(runWordMultiplyLockstep(),
          "Speedometer MULU.W/MULS.W register, immediate and RAM forms stay native");
    check(runAddSubExtendLockstep(),
          "Speedometer ADDX/SUBX B/W/L preserve X and cumulative Z natively");
    check(runExtendByteLockstep(),
          "Speedometer EXTB.L overwrites Dn, preserves X and publishes N/Z/V/C");
    check(runMemoryMultiplyDeviceOnce(),
          "word multiplication consumes one exact MMIO read and stays native");
    check(runWordDivisionLockstep(DivisionProgram::Success),
          "DIVU.W/DIVS.W register and hot immediate forms stay native and exact");
    check(runWordDivisionLockstep(DivisionProgram::Overflow),
          "word-division quotient overflow reaches Moira before guest mutation");
    check(runWordDivisionLockstep(DivisionProgram::Zero),
          "word division by zero reaches Moira's exact vector-5 path");
    check(runLongDivisionLockstep(DivisionProgram::Success),
          "all DIVL Dn extension actions stay native and exact");
    check(runLongDivisionLockstep(DivisionProgram::Overflow),
          "DIVL quotient overflow reaches Moira before guest mutation");
    check(runLongDivisionLockstep(DivisionProgram::Zero),
          "DIVL by zero reaches Moira's exact vector-5 path");
    check(runMemoryDivisionLockstep(),
          "word/long memory divisors and DIVL immediate stay native and exact");
    check(runMemoryDivisionOverflowLockstep(),
          "memory-divisor overflow replays after a side-effect-free probe");
    check(runMemoryLongDivisionZeroLockstep(),
          "zero memory divisor reaches Moira's exact vector-5 path");
    check(runMemoryDivisionDeviceOnce(),
          "MMIO divisor falls back before one and only one device read");
    check(runCachedMemoryDivisionLockstep(),
          "resident 040 cache divisors publish their hit only after guards");
    check(runGuardedDynamicShiftLockstep(4),
          "guarded Rogue count-4 LSL remains exact and native");
    check(runGuardedDynamicShiftLockstep(16),
          "guarded Speedometer count-16 LSL remains exact and native");
    check(runMultiVersionShiftLockstep(),
          "Speedometer count-24/31 LSR selects two native cache versions");
    check(runBoundedShiftVersionLockstep(),
          "dynamic-shift cache covers exactly the bounded 0..31 count domain");
    check(runExactShiftVersionMatrixLockstep(),
          "all four measured shifts stay exact and native for counts 0..31");
    check(runGuardIndexInvariant(),
          "mark/unmark inverse stays exact across 384 one/two-slice evictions");

    metrics.status = failures ? "fail" : "pass";
    check(jit::emitMetrics(metrics, std::getenv("POM68K_JIT_METRICS_FILE"),
                           std::getenv("POM68K_PERF_HOST_PROFILE")),
          "structured JIT metrics artifact is writable");
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
