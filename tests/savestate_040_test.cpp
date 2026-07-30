// POM68K — save/load determinism on the four 040 machine families:
// Q605 (MEMCjr + DAFB + AscIosb + 53C96), Centris (djMEMC + IOSB, discrete
// RTC + PIC1654S ADB), Q700 (discrete 040: two real VIAs + DAFB TurboSCSI)
// and Q630 (F108 + PrimeTime II + Valkyrie). Between them these compile
// and behaviour-check every 040-side device chunk: Dafb, Valkyrie,
// AscIosb, Ncr53c96, plus the Rtc/AdbVia pair on their 040 wiring.
//
// Same two properties as savestate_v8_test / savestate_030_test:
//   1. save → mutate → load → save is byte-identical;
//   2. run N cycles from a state == restore that state then run N cycles.
// Synthetic counter-loop ROM at $40000010 (the shared ROM window), no
// assets. Full-OS coverage belongs to a per-machine savestate etalon.

#include "CentrisCpu.h"
#include "CentrisMemory.h"
#include "Cpu040.h"
#include "Q605Memory.h"
#include "Q630Cpu.h"
#include "Q630Memory.h"
#include "Q700Cpu.h"
#include "Q700Memory.h"
#include "SaveState.h"
#include "SaveStateMachines.h"
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

// 1 MB ROM: reset PC → $40000010 (ROM window; first fetch clears the boot
// overlay), where an ADDQ/BRA pair increments a longword at $2000 forever.
std::vector<uint8_t> makeRom(uint32_t size) {
    std::vector<uint8_t> rom(size, 0);
    auto w16 = [&](uint32_t a, uint16_t v) {
        rom[a] = uint8_t(v >> 8); rom[a + 1] = uint8_t(v);
    };
    auto w32 = [&](uint32_t a, uint32_t v) {
        w16(a, uint16_t(v >> 16)); w16(a + 2, uint16_t(v));
    };
    w32(4, 0x40000010);                 // reset PC
    w16(0x10, 0x5279); w32(0x12, 0x00002000);   // ADDQ.W #1,($2000).L
    w16(0x16, 0x60F8);                          // BRA.S -8
    for (uint32_t i = 0x20; i < rom.size(); i++) rom[i] = uint8_t(i * 7);
    uint32_t sum = 0;                   // stored checksum (bytes 4…end)
    for (size_t i = 4; i + 1 < rom.size(); i += 2)
        sum += uint32_t(rom[i] << 8 | rom[i + 1]);
    w32(0, sum);
    return rom;
}

using Blob = std::vector<uint8_t>;

struct Q605Rig {
    Q605Memory mem;
    Cpu040 cpu;
    static constexpr auto kKind = pom68k::SnapMachine::Q605;
    explicit Q605Rig(const std::vector<uint8_t>& rom)
        : mem(8u << 20), cpu(mem) {
        mem.loadRom(rom); mem.setCpu(&cpu); cpu.hardReset();
    }
};
struct CentrisRig {
    CentrisMemory mem;
    CentrisCpu cpu;
    static constexpr auto kKind = pom68k::SnapMachine::Centris650;
    explicit CentrisRig(const std::vector<uint8_t>& rom)
        : mem(8u << 20), cpu(mem) {
        mem.loadRom(rom); mem.setCpu(&cpu); cpu.hardReset();
    }
};
struct Q700Rig {
    Q700Memory mem;
    Q700Cpu cpu;
    static constexpr auto kKind = pom68k::SnapMachine::Q700;
    explicit Q700Rig(const std::vector<uint8_t>& rom)
        : mem(8u << 20), cpu(mem) {
        mem.loadRom(rom); mem.setCpu(&cpu); cpu.hardReset();
    }
};
struct Q630Rig {
    Q630Memory mem;
    Q630Cpu cpu;
    static constexpr auto kKind = pom68k::SnapMachine::Q630;
    explicit Q630Rig(const std::vector<uint8_t>& rom)
        : mem(8u << 20), cpu(mem) {
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
    std::printf("savestate_040_test — Q605 / Centris / Q700 / Q630 fan-out\n");
    const auto rom = makeRom(0x100000);

    testFamily<Q605Rig>("q605", rom);
    testFamily<CentrisRig>("centris", rom);
    testFamily<Q700Rig>("q700", rom);
    testFamily<Q630Rig>("q630", rom);

    if (gFails) {
        std::printf("savestate_040_test: %d failure(s)\n", gFails);
        return 1;
    }
    std::printf("savestate_040_test: OK\n");
    return 0;
}
