// POM68K — save/load determinism on the last two machine families of the
// fan-out: the Mac II (GLUE board — two VIAs, NuBus + Toby video card,
// PIC1654S ADB modem, discrete RTC, 020/030 CPU) and the compact 68000
// (MacMemory: Plus with the M0110 keyboard engine, SE with the ADB
// transceiver). Between them these compile and behaviour-check the
// MacKeyboard/MacMouse, NuBus and TobyVideo chunks plus both remaining
// CPU wrappers (Cpu020, Cpu68k).
//
// Same two properties as the other savestate_* gates:
//   1. save → mutate → load → save is byte-identical;
//   2. run N cycles from a state == restore that state then run N cycles.
// Synthetic counter-loop ROMs, no assets. The overlay-clear prologue is
// per-family, because the hardware differs:
//   Plus   — DDRA=$FF then ORA_NH=0 (PA4 low clears; portA()-gated),
//   SE     — nothing: the first write to low RAM clears (mac128 ram_w_se),
//   Mac II — ORA_NH write with bit 4 clear (GLUE latch, v-gated).

#include "Cpu020.h"
#include "Cpu68k.h"
#include "MacIIMemory.h"
#include "MacMemory.h"
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

using Rom = std::vector<uint8_t>;

struct RomWriter {
    Rom rom;
    explicit RomWriter(uint32_t size) : rom(size, 0) {}
    void w16(uint32_t a, uint16_t v) {
        rom[a] = uint8_t(v >> 8); rom[a + 1] = uint8_t(v);
    }
    void w32(uint32_t a, uint32_t v) {
        w16(a, uint16_t(v >> 16)); w16(a + 2, uint16_t(v));
    }
    // MOVE.B #imm,(abs).L — the overlay-clear prologue building block.
    uint32_t moveB(uint32_t at, uint8_t imm, uint32_t abs) {
        w16(at, 0x13FC); w16(at + 2, imm); w32(at + 4, abs);
        return at + 8;
    }
    // ADDQ.W #1,($2000).L ; BRA.S back to the ADDQ.
    void counterLoop(uint32_t at) {
        w16(at, 0x5279); w32(at + 2, 0x00002000);
        w16(at + 6, 0x60F8);
    }
    Rom finish() {
        for (uint32_t i = 0x40; i < rom.size(); i++) rom[i] = uint8_t(i * 7);
        uint32_t sum = 0;
        for (size_t i = 4; i + 1 < rom.size(); i += 2)
            sum += uint32_t(rom[i] << 8 | rom[i + 1]);
        w32(0, sum);
        return rom;
    }
};

// Plus: 128 KB ROM at $400000. DDRA first — the overlay latch samples
// portA(), which reads input pins where DDRA bits are 0.
Rom makePlusRom() {
    RomWriter w(0x20000);
    w.w32(4, 0x00400010);
    uint32_t at = w.moveB(0x10, 0xFF, 0x00E807FE);   // VIA DDRA = $FF
    at = w.moveB(at, 0x00, 0x00E81FFE);              // ORA_NH: PA4=0
    w.counterLoop(at);
    return w.finish();
}
// SE-family: 256 KB ROM, no prologue (first low-RAM write clears).
Rom makeSeRom() {
    RomWriter w(0x40000);
    w.w32(4, 0x00400010);
    w.counterLoop(0x10);
    return w.finish();
}
// Mac II: 256 KB ROM. Cpu020::hardReset hardcodes the entry point the way
// Basilisk does (SSP=$2000, PC=ROMBase+$2A — the real ROM's header carries
// a checksum at $0, not vectors), so the stub lives at offset $2A. At
// reset hmmu24_ is OFF (32-bit until VIA2 PB3 is cleared), so it runs in
// the 32-bit ROM window ($40800000) and reaches VIA1 at $50F01E00
// (ORA_NH), not the 24-bit aliases.
Rom makeIIRom() {
    RomWriter w(0x40000);
    uint32_t at = w.moveB(0x2A, 0x00, 0x50F01E00);   // ORA_NH: bit4=0
    w.counterLoop(at);
    return w.finish();
}

using Blob = std::vector<uint8_t>;

struct PlusRig {
    MacMemory mem{pom68k::defaultCoreConfig(), MacMemory::Model::Plus};
    Cpu68k cpu{mem, jit::defaultResolvedConfig()};
    static constexpr auto kKind = pom68k::SnapMachine::Plus;
    explicit PlusRig(const Rom& rom) {
        mem.loadRom(rom); mem.setCpu(&cpu); cpu.hardReset();
    }
    uint32_t counter() const {
        return uint32_t(mem.ram()[0x2000]) << 8 | mem.ram()[0x2001];
    }
};
struct SeRig {
    MacMemory mem{pom68k::defaultCoreConfig(), MacMemory::Model::SE};
    Cpu68k cpu{mem, jit::defaultResolvedConfig()};
    static constexpr auto kKind = pom68k::SnapMachine::SE;
    explicit SeRig(const Rom& rom) {
        mem.loadRom(rom); mem.setCpu(&cpu); cpu.hardReset();
    }
    uint32_t counter() const {
        return uint32_t(mem.ram()[0x2000]) << 8 | mem.ram()[0x2001];
    }
};
struct MacIIRig {
    MacIIMemory mem{pom68k::defaultCoreConfig(), 0x800000,
                    MacIIMemory::Model::MacII};
    Cpu020 cpu{mem, jit::defaultResolvedConfig(),
               pom68k::defaultCoreConfig().cpu};
    static constexpr auto kKind = pom68k::SnapMachine::MacII;
    explicit MacIIRig(const Rom& rom) {
        mem.loadRom(rom);
        // Exercise the TobyVideo + NuBus chunks when the card installs
        // (it soft-skips without its decl ROM; determinism holds either way
        // because every rig in a run makes the same choice).
        mem.installTobyVideo();
        mem.setCpu(&cpu);
        cpu.hardReset();
    }
    uint32_t counter() const {
        return uint32_t(mem.peek8(0x2000)) << 8 | mem.peek8(0x2001);
    }
};

template <class Rig>
Blob saveOf(Rig& r) {
    Blob b;
    pom68k::save(r.mem, r.cpu, Rig::kKind, b);
    return b;
}

template <class Rig>
void testFamily(const char* family, const Rom& rom) {
    // ── 1. Re-save byte-identity ────────────────────────────────────────
    Rig m(rom);
    m.cpu.runCycles(200000);
    check(!m.mem.overlay(), family, "setup: the stub cleared the boot overlay");
    const uint32_t atSnapshot = m.counter();
    check(atSnapshot != 0, family, "setup: the counter loop is running");

    const Blob snapshot = saveOf(m);
    check(snapshot.size() > 64, family, "save: produced a container");

    m.cpu.runCycles(200000);
    check(m.counter() != atSnapshot, family, "mutate: the machine moved on");

    std::string err;
    check(pom68k::load(m.mem, m.cpu, Rig::kKind,
                       snapshot.data(), snapshot.size(), err),
          family, "load: accepted its own snapshot");
    check(err.empty(), family, "load: no warning on a same-build snapshot");
    check(m.counter() == atSnapshot, family, "load: guest RAM is back");
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
    std::printf("savestate_68k_test — Mac II + compact 68000 fan-out\n");

    testFamily<PlusRig>("plus", makePlusRom());
    testFamily<SeRig>("se", makeSeRom());
    testFamily<MacIIRig>("macii", makeIIRom());

    if (gFails) {
        std::printf("savestate_68k_test: %d failure(s)\n", gFails);
        return 1;
    }
    std::printf("savestate_68k_test: OK\n");
    return 0;
}
