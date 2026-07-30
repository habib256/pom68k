// POM68K — end-to-end save/load on a live V8 (LC II) machine.
//
// The archive core has its own gate (savestate_test); this one proves the
// thing that actually matters: that a snapshot of a RUNNING machine restores
// the machine. Two properties, in order of strength:
//
//   1. Byte-identity of a re-save. save → mutate → load → save must
//      reproduce the first blob exactly. Cheap, and it catches a visit()
//      whose read order does not match its write order.
//
//   2. DETERMINISM ACROSS A RESTORE. From one state: run N cycles and
//      snapshot the result; then restore the original, run the same N
//      cycles, and snapshot again. The two must be identical. This is the
//      property that finds OMITTED state — a field left out of a visit()
//      still round-trips (nobody wrote it, nobody read it) and property 1
//      stays green, but the machine diverges the moment it runs. That is
//      exactly the failure mode a boot-signature gate cannot see.
//
// No ROM or disk image: the machine runs a synthetic ROM whose stub is an
// infinite counter loop, so CPU, RAM, the VIA timers and the MCU keep
// evolving while the test runs. Coverage is therefore the plumbing and the
// devices that tick — a full-OS soak belongs in an etalon.

#include "Cpu030.h"
#include "SaveState.h"
#include "SaveStateMachines.h"
#include "V8Memory.h"
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {
int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-62s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}

// 512 KB ROM: reset vector → $A00010, where an ADDQ/BRA pair increments a
// longword in RAM forever. A halting stub (STOP) would make the determinism
// check vacuous — nothing would change between the two runs.
std::vector<uint8_t> makeRom() {
    std::vector<uint8_t> rom(V8Memory::kRomSize, 0);
    auto w16 = [&](uint32_t a, uint16_t v) {
        rom[a] = uint8_t(v >> 8); rom[a + 1] = uint8_t(v);
    };
    auto w32 = [&](uint32_t a, uint32_t v) {
        w16(a, uint16_t(v >> 16)); w16(a + 2, uint16_t(v));
    };

    w32(4, 0x00A00010);                 // reset PC
    // $A00010: ADDQ.W #1, ($2000).L    5279 0000 2000
    w16(0x10, 0x5279); w32(0x12, 0x00002000);
    // $A00016: BRA.S  -8               60 F8
    w16(0x16, 0x60F8);
    for (uint32_t i = 0x20; i < rom.size(); i++) rom[i] = uint8_t(i * 7);

    uint32_t sum = 0;                   // stored checksum (bytes 4…end)
    for (size_t i = 4; i + 1 < rom.size(); i += 2)
        sum += uint32_t(rom[i] << 8 | rom[i + 1]);
    w32(0, sum);
    return rom;
}

using Blob = std::vector<uint8_t>;

// A machine brought up to a non-trivial state: booted past the overlay, with
// input queued on the ADB path and the counter loop already spinning.
struct Machine {
    V8Memory mem{0xA00000};             // 10 MB: 4 soldered + 8 SIMM
    Cpu030   cpu{mem};

    explicit Machine(const std::vector<uint8_t>& rom) {
        mem.loadRom(rom);
        mem.setCpu(&cpu);
        cpu.hardReset();
    }
    void run(int64_t cycles) { cpu.runCycles(cycles); }
    Blob save() {
        Blob b;
        pom68k::save(mem, cpu, pom68k::SnapMachine::LcII, b);
        return b;
    }
    uint32_t counter() const {
        return uint32_t(mem.peek8(0x2000)) << 8 | mem.peek8(0x2001);
    }
};
}  // namespace

int main() {
    std::printf("savestate_v8_test — save/load a live LC II machine\n");
    const auto rom = makeRom();

    // ── 1. Re-save byte-identity ────────────────────────────────────────
    Blob snapshot;
    {
        Machine m(rom);
        m.run(200000);                  // clear the overlay, spin the loop
        check(!m.mem.overlay(), "setup: the stub cleared the boot overlay");
        const uint32_t atSnapshot = m.counter();
        check(atSnapshot != 0, "setup: the counter loop is actually running");

        snapshot = m.save();
        check(snapshot.size() > 64, "save: produced a container");
        // 10 MB of mostly-zero RAM must not cost 10 MB.
        check(snapshot.size() < 2u << 20,
              "save: 10 MB machine compresses under 2 MB");

        m.run(200000);                  // diverge
        check(m.counter() != atSnapshot, "mutate: the machine moved on");

        std::string err;
        check(pom68k::load(m.mem, m.cpu, pom68k::SnapMachine::LcII,
                           snapshot.data(), snapshot.size(), err),
              "load: accepted its own snapshot");
        check(err.empty(), "load: no warning on a same-build snapshot");
        check(m.counter() == atSnapshot, "load: guest RAM is back to the snapshot");

        const Blob again = m.save();
        check(again == snapshot, "load→save is byte-identical to the original");
    }

    // ── 2. Determinism across a restore ─────────────────────────────────
    // The strong property: same start, same cycles, same result.
    {
        Machine a(rom);
        a.run(200000);
        const Blob start = a.save();
        a.run(150000);
        const Blob direct = a.save();

        Machine b(rom);
        b.run(200000);                  // any state; the restore overwrites it
        std::string err;
        check(pom68k::load(b.mem, b.cpu, pom68k::SnapMachine::LcII,
                           start.data(), start.size(), err),
              "determinism: restored the start state into a second machine");
        b.run(150000);
        const Blob restored = b.save();

        check(restored.size() == direct.size(),
              "determinism: same snapshot size after the same run");
        check(sav::hash(restored) == sav::hash(direct),
              "determinism: 150k cycles after a restore match 150k cycles "
              "without one");

        if (restored != direct) {
            // Locating the first divergence is what makes a failure here
            // actionable: the byte offset points at the chunk, and the
            // chunk order in SaveStateMachines.cpp points at the device.
            size_t i = 0;
            const size_t n = std::min(restored.size(), direct.size());
            while (i < n && restored[i] == direct[i]) i++;
            std::printf("    first divergence at byte %zu of %zu\n", i, n);
        }
    }

    // ── 3. A snapshot that does not belong is refused, cleanly ──────────
    {
        Machine m(rom);
        m.run(200000);
        const uint32_t before = m.counter();
        std::string err;

        Blob bad = snapshot;
        bad[0] ^= 0xFF;                 // corrupt the magic
        check(!pom68k::load(m.mem, m.cpu, pom68k::SnapMachine::LcII,
                            bad.data(), bad.size(), err),
              "reject: a bad magic is refused");
        check(!err.empty(), "reject: refusal explains itself");

        // Truncation at every length must be refused and must not crash.
        bool anyAccepted = false;
        for (size_t n = 0; n < snapshot.size(); n += 997) {
            std::string e;
            if (pom68k::load(m.mem, m.cpu, pom68k::SnapMachine::LcII,
                             snapshot.data(), n, e))
                anyAccepted = true;
        }
        check(!anyAccepted, "reject: no truncated prefix is accepted");

        // A machine with a DIFFERENT ROM must refuse the snapshot outright,
        // because the guest's cached ROM addresses would land elsewhere.
        auto otherRom = rom;
        otherRom[0x40] ^= 0x5A;
        uint32_t sum = 0;
        for (size_t i = 4; i + 1 < otherRom.size(); i += 2)
            sum += uint32_t(otherRom[i] << 8 | otherRom[i + 1]);
        otherRom[0] = uint8_t(sum >> 24); otherRom[1] = uint8_t(sum >> 16);
        otherRom[2] = uint8_t(sum >> 8);  otherRom[3] = uint8_t(sum);

        Machine other(otherRom);
        other.run(1000);
        std::string e2;
        check(!pom68k::load(other.mem, other.cpu, pom68k::SnapMachine::LcII,
                            snapshot.data(), snapshot.size(), e2),
              "reject: a different ROM is refused");

        // And the machine that refused a snapshot must be untouched: the
        // counter it had before the failed loads is still there (the loop is
        // stopped, so nothing else could have moved it).
        check(m.counter() == before,
              "reject: a refused load left the machine untouched");
    }

    if (gFails) {
        std::printf("savestate_v8_test: %d failure(s)\n", gFails);
        return 1;
    }
    std::printf("savestate_v8_test: OK\n");
    return 0;
}
