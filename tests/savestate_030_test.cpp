// POM68K — save/load determinism on the 030 machine families that fan out
// from the LC II foundation: Sonora (LC III), VASP (IIvx) and RBV
// (IIsi + the IIci flavor, which swaps the Egret for the PIC1654S ADB
// modem + discrete 343-0042 RTC — so this gate is also what compiles and
// behaviour-checks the AdbVia/Pic1654s/Rtc chunks), plus the IIfx
// (platform #12), whose chunk nests something no other machine has: two
// Apple PIC IOPs, each a full R65c02 + 32 KB of host-uploaded firmware.
// Losing that RAM on restore would resurrect a machine whose I/O
// processors have no program — hence the coverage here. The PowerBook Duo
// 230 (platform #11, MSC + PG&E) closes the file for the same reason, one
// notch worse: its power manager's 32 KB SRAM holds the firmware the
// system ROM uploads over SPI *and* the machine's PRAM, and unlike a NuBus
// declaration ROM nothing on the host can put it back.
//
// Same two properties as savestate_v8_test, per family:
//   1. save → mutate → load → save is byte-identical;
//   2. run N cycles from a state == restore that state then run N cycles.
// No ROM or disk image: each machine runs a synthetic ROM whose stub is an
// infinite counter loop at the family's ROM window ($40000000 on all
// three), so CPU, RAM, VIA timers and the MCU keep evolving. Full-OS
// coverage belongs to the machine's savestate etalon (lcii_savestate_
// etalon is the template).

#include "AssetFingerprint.h"
#include "IIfxCpu.h"
#include "IIfxMemory.h"
#include "M68hc05Pge.h"
#include "MscCpu.h"
#include "MscMemory.h"
#include "PgePmu.h"
#include "RbvCpu.h"
#include "RbvMemory.h"
#include "SaveState.h"
#include "SaveStateMachines.h"
#include "SonoraCpu.h"
#include "SonoraMemory.h"
#include "VaspCpu.h"
#include "VaspMemory.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace {
int gFails = 0;
void check(bool ok, const char* family, const char* what) {
    std::printf("  %-8s %-52s %s\n", family, what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}

// ROM of the family's size: reset PC → the ROM window the machine decodes
// (the first fetch there clears the boot overlay), where an ADDQ/BRA pair
// increments a longword at $2000 forever. `entry` is the ROM offset of the
// stub: $10 for the families whose reset vector comes from the ROM image,
// $2A for the IIfx, whose wrapper hardcodes the Basilisk-style reset frame
// (SSP=$2000, PC=ROMBase+$2A) exactly like the Mac II's.
std::vector<uint8_t> makeRom(uint32_t size, uint32_t entry = 0x10) {
    std::vector<uint8_t> rom(size, 0);
    auto w16 = [&](uint32_t a, uint16_t v) {
        rom[a] = uint8_t(v >> 8); rom[a + 1] = uint8_t(v);
    };
    auto w32 = [&](uint32_t a, uint32_t v) {
        w16(a, uint16_t(v >> 16)); w16(a + 2, uint16_t(v));
    };
    w32(4, 0x40000000 + entry);          // reset PC
    w16(entry, 0x5279); w32(entry + 2, 0x00002000);   // ADDQ.W #1,($2000).L
    w16(entry + 6, 0x60F8);                           // BRA.S -8
    for (uint32_t i = entry + 0x10; i < rom.size(); i++) rom[i] = uint8_t(i * 7);
    uint32_t sum = 0;                   // stored checksum (bytes 4…end)
    for (size_t i = 4; i + 1 < rom.size(); i += 2)
        sum += uint32_t(rom[i] << 8 | rom[i + 1]);
    w32(0, sum);
    return rom;
}

using Blob = std::vector<uint8_t>;

// One rig per family: machine + CPU wired and reset. The ctor args are the
// family's profile defaults; the IIci rig exercises the other RBV front end.
// Heap-owned, never a local: see the GateCpu note in tests/jit_copyback_write_040_test.cpp.
struct SonoraRig {
    SonoraMemory mem{pom68k::defaultCoreConfig()};
    SonoraCpu cpu;
    static constexpr auto kKind = pom68k::SnapMachine::Lc3;
    explicit SonoraRig(const std::vector<uint8_t>& rom)
        : mem(pom68k::defaultCoreConfig()),
          cpu(mem, jit::defaultResolvedConfig(),
              pom68k::defaultCoreConfig().cpu) {
        mem.loadRom(rom); mem.setCpu(&cpu); cpu.hardReset();
    }
};
struct VaspRig {
    VaspMemory mem{pom68k::defaultCoreConfig()};
    VaspCpu cpu;
    static constexpr auto kKind = pom68k::SnapMachine::IIvx;
    explicit VaspRig(const std::vector<uint8_t>& rom)
        : mem(pom68k::defaultCoreConfig()),
          cpu(mem, jit::defaultResolvedConfig(),
              pom68k::defaultCoreConfig().cpu) {
        mem.loadRom(rom); mem.setCpu(&cpu); cpu.hardReset();
    }
};
struct RbvRig {
    RbvMemory mem{pom68k::defaultCoreConfig()};
    RbvCpu cpu;
    static constexpr auto kKind = pom68k::SnapMachine::IIsi;
    explicit RbvRig(const std::vector<uint8_t>& rom)
        : mem(pom68k::defaultCoreConfig()),
          cpu(mem, jit::defaultResolvedConfig(),
              pom68k::defaultCoreConfig().cpu) {
        mem.loadRom(rom); mem.setCpu(&cpu); cpu.hardReset();
    }
};
// Platform #12. Its chunk is the only one in the tree carrying two extra
// CPUs: each ApplePic nests an R65c02 plus 32 KB of RAM that IS the IOP
// firmware (host-uploaded, no ROM to reload) — the whole point of the
// coverage here.
struct IIfxRig {
    IIfxMemory mem{pom68k::defaultCoreConfig()};
    IIfxCpu cpu;
    static constexpr auto kKind = pom68k::SnapMachine::IIfx;
    explicit IIfxRig(const std::vector<uint8_t>& rom)
        : mem(pom68k::defaultCoreConfig()),
          cpu(mem, jit::defaultResolvedConfig()) {
        mem.loadRom(rom); mem.setCpu(&cpu); cpu.hardReset();
    }
};
struct RbvIiciRig {
    RbvMemory mem{pom68k::defaultCoreConfig()};
    RbvCpu cpu;
    static constexpr auto kKind = pom68k::SnapMachine::IIci;
    explicit RbvIiciRig(const std::vector<uint8_t>& rom)
        : mem(pom68k::defaultCoreConfig(), 0x800000,
              RbvMemory::kCpuHzCi, /*iici=*/true),
          cpu(mem, jit::defaultResolvedConfig(),
              pom68k::defaultCoreConfig().cpu) {
        mem.loadRom(rom); mem.setCpu(&cpu); cpu.hardReset();
    }
};

template <class Rig>
Blob saveOf(Rig& r) {
    Blob b;
    pom68k::save(r.mem, r.cpu, Rig::kKind, b);
    return b;
}

template <class Rig>
uint32_t counterOf(const Rig& r) {
    return uint32_t(r.mem.peek8(0x2000)) << 8 | r.mem.peek8(0x2001);
}

template <class Rig>
void testFamily(const char* family, const std::vector<uint8_t>& rom) {
    // ── 1. Re-save byte-identity ────────────────────────────────────────
    const auto mOwner = std::make_unique<Rig>(rom);
    Rig& m = *mOwner;
    m.cpu.runCycles(200000);
    check(!m.mem.overlay(), family, "setup: the stub cleared the boot overlay");
    const uint32_t atSnapshot = counterOf(m);
    check(atSnapshot != 0, family, "setup: the counter loop is running");

    const Blob snapshot = saveOf(m);
    check(snapshot.size() > 64, family, "save: produced a container");
    check(snapshot.size() < 2u << 20, family,
          "save: 8 MB machine compresses under 2 MB");

    m.cpu.runCycles(200000);
    check(counterOf(m) != atSnapshot, family, "mutate: the machine moved on");

    std::string err;
    check(pom68k::load(m.mem, m.cpu, Rig::kKind,
                       snapshot.data(), snapshot.size(), err),
          family, "load: accepted its own snapshot");
    check(err.empty(), family, "load: no warning on a same-build snapshot");
    check(counterOf(m) == atSnapshot, family, "load: guest RAM is back");
    check(saveOf(m) == snapshot, family, "load→save is byte-identical");

    // ── 2. Determinism across a restore ─────────────────────────────────
    const auto aOwner = std::make_unique<Rig>(rom);
    Rig& a = *aOwner;
    a.cpu.runCycles(200000);
    const Blob start = saveOf(a);
    a.cpu.runCycles(150000);
    const Blob direct = saveOf(a);

    const auto bOwner = std::make_unique<Rig>(rom);
    Rig& b = *bOwner;
    b.cpu.runCycles(200000);            // any state; the restore overwrites it
    std::string e2;
    check(pom68k::load(b.mem, b.cpu, Rig::kKind,
                       start.data(), start.size(), e2),
          family, "determinism: restored into a second machine");
    b.cpu.runCycles(150000);
    const Blob restored = saveOf(b);

    check(sav::hash(restored) == sav::hash(direct), family,
          "determinism: 150k cycles after a restore match");
    if (restored != direct) {
        size_t i = 0;
        const size_t n = std::min(restored.size(), direct.size());
        while (i < n && restored[i] == direct[i]) i++;
        std::printf("    first divergence at byte %zu of %zu\n", i, n);
    }
}

// ── Platform #11: the PowerBook Duo 230 (MSC + PG&E) ────────────────────
// The Duo cannot ride makeRom()/testFamily(): its 68030 is HELD at reset
// until the PG&E power manager has run its own 512 B mask ROM and raised
// port E bit 2 (msc.cpp:151, docs/DUO_BRINGUP.md), so the rig needs the
// real roms/pge/pge_boot.bin — and the state this entry exists to protect
// only appears under the real system ROM: the PMU firmware it uploads over
// SPI into the PG&E's 32 KB SRAM. That SRAM is the one store in the tree
// with no reload path — no ROM file backs it, so it either travels in the
// snapshot or the restored machine owns a power manager with no program
// (and, in the same 32 KB, no PRAM). No disk image is needed: the system
// ROM is already streaming SPI to the PMU long before it looks for a boot
// volume. Soft-skips without the two ROM files, like every asset-dependent
// gate in the tree.
std::string findAsset(const char* rel) {
    return testasset::find(rel);
}

// FNV-1a over a PG&E memory range, through the core's own byte accessors
// (M68hc05Pge hands no bulk pointer out — the same narrow contract
// MscMemory::savePram uses).
uint64_t pgeDigest(M68hc05Pge& mcu, bool sram) {
    uint64_t h = 1469598103934665603ull;
    const int n = sram ? M68hc05Pge::kSramSize : M68hc05Pge::kRamSize;
    for (int i = 0; i < n; i++) {
        h ^= sram ? mcu.sramByte(i) : mcu.ramByte(i);
        h *= 1099511628211ull;
    }
    return h;
}

struct DuoRig {
    MscMemory mem{pom68k::defaultCoreConfig()};
    MscCpu cpu;
    static constexpr auto kKind = pom68k::SnapMachine::Duo230;
    explicit DuoRig(const std::vector<uint8_t>& rom)
        : mem(pom68k::defaultCoreConfig(), 8u << 20,
              MscMemory::kCpuHz230, MscMemory::kIdDuo230),
          cpu(mem, jit::defaultResolvedConfig(),
              pom68k::defaultCoreConfig().cpu) {
        mem.loadRom(rom); mem.setCpu(&cpu); cpu.hardReset();
    }
    // The PMU boots FIRST; the 68030 may not be stepped while it is held.
    // Capped so a wedged PG&E fails loudly instead of spinning.
    bool release() {
        for (long i = 0; i < 400000 && mem.cpuHeld(); i++) mem.tick(1000);
        return !mem.cpuHeld();
    }
};

void testDuo() {
    const char* kRomRel =
        "roms/1MB ROMs/1992-10 - ECFA989B - Powerbook 210 & 230 & 250.ROM";
    const std::string romPath = findAsset(kRomRel);
    // MscMemory's ctor finds the PG&E dump itself (both cwd spellings); this
    // is only the presence probe that decides skip vs run.
    const std::string pgePath = findAsset("roms/pge/pge_boot.bin");
    if (romPath.empty() || pgePath.empty()) {
        std::printf("  %-8s SKIP: needs the ECFA989B ROM and "
                    "roms/pge/pge_boot.bin\n", "duo230");
        return;
    }
    testasset::report({ romPath, pgePath });
    std::ifstream rin(romPath, std::ios::binary);
    const std::vector<uint8_t> rom((std::istreambuf_iterator<char>(rin)),
                                   std::istreambuf_iterator<char>());
    if (rom.size() != MscMemory::kRomSize) {
        check(false, "duo230", "setup: ROM is 1 MB");
        return;
    }

    // ── 1. Re-save byte-identity, with the PG&E as the payload ──────────
    const auto mOwner = std::make_unique<DuoRig>(rom);
    DuoRig& m = *mOwner;
    // Both are hard prerequisites, not assertions to soldier past: without a
    // PG&E there is no mcu() to dereference, and a held 68030 never runs.
    check(m.mem.pgeActive(), "duo230", "setup: the PG&E boot ROM loaded");
    if (!m.mem.pgeActive()) return;
    check(m.release(), "duo230", "setup: the PG&E released the 68030");
    if (m.mem.cpuHeld()) return;
    m.cpu.runCycles(8000000);
    check(!m.mem.overlay(), "duo230", "setup: the ROM cleared the boot overlay");
    M68hc05Pge& mcu = m.mem.pmu().mcu();
    check(mcu.spiTransfers > 100, "duo230",
          "setup: the system ROM is uploading firmware over SPI");
    const uint64_t sram0 = pgeDigest(mcu, true);
    const uint64_t ram0  = pgeDigest(mcu, false);
    const uint16_t mcuPc0 = mcu.pc();
    const uint32_t rtc0 = mcu.rtc();
    long sramLive = 0;
    for (int i = 0; i < M68hc05Pge::kSramSize; i++)
        if (mcu.sramByte(i)) sramLive++;
    check(sramLive > 100, "duo230",
          "setup: real firmware bytes landed in the PG&E SRAM");

    const Blob snapshot = saveOf(m);
    check(snapshot.size() > 64, "duo230", "save: produced a container");

    // Mutate BOTH clocks of Duo-specific state: let the machine run on, then
    // scribble the PG&E's own memories directly (setRamByte/setSramByte are
    // the same narrow setters MscMemory::loadPram uses). A restore that
    // forgot the PG&E would leave the scribble in place.
    m.cpu.runCycles(3000000);
    for (int i = 0; i < 256; i++) {
        mcu.setSramByte(i * 71, uint8_t(0xA5 ^ i));
        mcu.setRamByte(i, uint8_t(0x5A ^ i));
    }
    check(pgeDigest(mcu, true) != sram0, "duo230",
          "mutate: the PG&E SRAM moved on");
    check(pgeDigest(mcu, false) != ram0, "duo230",
          "mutate: the PG&E internal RAM moved on");

    std::string err;
    check(pom68k::load(m.mem, m.cpu, DuoRig::kKind,
                       snapshot.data(), snapshot.size(), err),
          "duo230", "load: accepted its own snapshot");
    check(err.empty(), "duo230", "load: no warning on a same-build snapshot");
    check(pgeDigest(mcu, true) == sram0, "duo230",
          "load: the uploaded PG&E firmware/PRAM is back (32 KB SRAM)");
    check(pgeDigest(mcu, false) == ram0, "duo230",
          "load: the PG&E internal RAM is back (960 B)");
    check(mcu.pc() == mcuPc0, "duo230", "load: the PG&E resumes at its own PC");
    check(mcu.rtc() == rtc0, "duo230", "load: the PMU-side clock is back");
    check(saveOf(m) == snapshot, "duo230", "load→save is byte-identical");

    // ── 2. Determinism across a restore ─────────────────────────────────
    // Both 68030 and PG&E must resume in lockstep: the MCU runs off the
    // machine clock through MscCpu::flushTicks → MscMemory::tick, so a lost
    // accumulator shows up here as a diverging snapshot, not as a hang.
    const auto aOwner = std::make_unique<DuoRig>(rom);
    DuoRig& a = *aOwner;
    if (!a.release()) { check(false, "duo230", "determinism: rig a released"); return; }
    a.cpu.runCycles(8000000);
    const Blob start = saveOf(a);
    a.cpu.runCycles(3000000);
    const Blob direct = saveOf(a);

    const auto bOwner = std::make_unique<DuoRig>(rom);
    DuoRig& b = *bOwner;
    if (!b.release()) { check(false, "duo230", "determinism: rig b released"); return; }
    b.cpu.runCycles(8000000);           // any state; the restore overwrites it
    std::string e2;
    check(pom68k::load(b.mem, b.cpu, DuoRig::kKind,
                       start.data(), start.size(), e2),
          "duo230", "determinism: restored into a second machine");
    b.cpu.runCycles(3000000);
    const Blob restored = saveOf(b);

    check(sav::hash(restored) == sav::hash(direct), "duo230",
          "determinism: 3 M cycles after a restore match");
    if (restored != direct) {
        size_t i = 0;
        const size_t n = std::min(restored.size(), direct.size());
        while (i < n && restored[i] == direct[i]) i++;
        std::printf("    first divergence at byte %zu of %zu\n", i, n);
    }
}
}  // namespace

int main() {
    std::printf("savestate_030_test — Sonora / VASP / RBV / IIfx / Duo 230 "
                "save-state fan-out\n");
    const auto rom1M   = makeRom(0x100000);   // Sonora + VASP
    const auto rom512K = makeRom(0x80000);    // RBV

    testFamily<SonoraRig>("sonora", rom1M);
    testFamily<VaspRig>("vasp", rom1M);
    testFamily<RbvRig>("rbv", rom512K);
    testFamily<RbvIiciRig>("rbv-iici", rom512K);
    // The IIfx wrapper hardcodes PC = ROMBase+$2A, so its stub lives there.
    testFamily<IIfxRig>("iifx", makeRom(0x80000, 0x2A));
    // The Duo needs its real ROM pair (the PG&E holds the 68030 in reset
    // until its own mask ROM says otherwise) — soft-skips without them.
    testDuo();

    if (gFails) {
        std::printf("savestate_030_test: %d failure(s)\n", gFails);
        return 1;
    }
    std::printf("savestate_030_test: OK\n");
    return 0;
}
