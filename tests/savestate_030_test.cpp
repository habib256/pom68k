// POM68K — save/load determinism on the 030 machine families that fan out
// from the LC II foundation: Sonora (LC III), VASP (IIvx) and RBV
// (IIsi + the IIci flavor, which swaps the Egret for the PIC1654S ADB
// modem + discrete 343-0042 RTC — so this gate is also what compiles and
// behaviour-checks the AdbVia/Pic1654s/Rtc chunks), plus the IIfx
// (platform #12), whose chunk nests something no other machine has: two
// Apple PIC IOPs, each a full R65c02 + 32 KB of host-uploaded firmware.
// Losing that RAM on restore would resurrect a machine whose I/O
// processors have no program — hence the coverage here.
//
// Same two properties as savestate_v8_test, per family:
//   1. save → mutate → load → save is byte-identical;
//   2. run N cycles from a state == restore that state then run N cycles.
// No ROM or disk image: each machine runs a synthetic ROM whose stub is an
// infinite counter loop at the family's ROM window ($40000000 on all
// three), so CPU, RAM, VIA timers and the MCU keep evolving. Full-OS
// coverage belongs to the machine's savestate etalon (lcii_savestate_
// etalon is the template).

#include "IIfxCpu.h"
#include "IIfxMemory.h"
#include "RbvCpu.h"
#include "RbvMemory.h"
#include "SaveState.h"
#include "SaveStateMachines.h"
#include "SonoraCpu.h"
#include "SonoraMemory.h"
#include "VaspCpu.h"
#include "VaspMemory.h"
#include <cstdint>
#include <cstdio>
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
struct SonoraRig {
    SonoraMemory mem;
    SonoraCpu cpu;
    static constexpr auto kKind = pom68k::SnapMachine::Lc3;
    explicit SonoraRig(const std::vector<uint8_t>& rom) : mem(), cpu(mem) {
        mem.loadRom(rom); mem.setCpu(&cpu); cpu.hardReset();
    }
};
struct VaspRig {
    VaspMemory mem;
    VaspCpu cpu;
    static constexpr auto kKind = pom68k::SnapMachine::IIvx;
    explicit VaspRig(const std::vector<uint8_t>& rom) : mem(), cpu(mem) {
        mem.loadRom(rom); mem.setCpu(&cpu); cpu.hardReset();
    }
};
struct RbvRig {
    RbvMemory mem;
    RbvCpu cpu;
    static constexpr auto kKind = pom68k::SnapMachine::IIsi;
    explicit RbvRig(const std::vector<uint8_t>& rom) : mem(), cpu(mem) {
        mem.loadRom(rom); mem.setCpu(&cpu); cpu.hardReset();
    }
};
// Platform #12. Its chunk is the only one in the tree carrying two extra
// CPUs: each ApplePic nests an R65c02 plus 32 KB of RAM that IS the IOP
// firmware (host-uploaded, no ROM to reload) — the whole point of the
// coverage here.
struct IIfxRig {
    IIfxMemory mem;
    IIfxCpu cpu;
    static constexpr auto kKind = pom68k::SnapMachine::IIfx;
    explicit IIfxRig(const std::vector<uint8_t>& rom) : mem(), cpu(mem) {
        mem.loadRom(rom); mem.setCpu(&cpu); cpu.hardReset();
    }
};
struct RbvIiciRig {
    RbvMemory mem;
    RbvCpu cpu;
    static constexpr auto kKind = pom68k::SnapMachine::IIci;
    explicit RbvIiciRig(const std::vector<uint8_t>& rom)
        : mem(0x800000, RbvMemory::kCpuHzCi, /*iici=*/true), cpu(mem) {
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
    Rig m(rom);
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
    Rig a(rom);
    a.cpu.runCycles(200000);
    const Blob start = saveOf(a);
    a.cpu.runCycles(150000);
    const Blob direct = saveOf(a);

    Rig b(rom);
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
}  // namespace

int main() {
    std::printf("savestate_030_test — Sonora / VASP / RBV save-state fan-out\n");
    const auto rom1M   = makeRom(0x100000);   // Sonora + VASP
    const auto rom512K = makeRom(0x80000);    // RBV

    testFamily<SonoraRig>("sonora", rom1M);
    testFamily<VaspRig>("vasp", rom1M);
    testFamily<RbvRig>("rbv", rom512K);
    testFamily<RbvIiciRig>("rbv-iici", rom512K);
    // The IIfx wrapper hardcodes PC = ROMBase+$2A, so its stub lives there.
    testFamily<IIfxRig>("iifx", makeRom(0x80000, 0x2A));

    if (gFails) {
        std::printf("savestate_030_test: %d failure(s)\n", gFails);
        return 1;
    }
    std::printf("savestate_030_test: OK\n");
    return 0;
}
