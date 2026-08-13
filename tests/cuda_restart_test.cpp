// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The Finder's "Restart" on an Egret/Cuda machine
// (SIMPLIFICATIONS_REVIEW.md F4, LLE_VS_HLE.md § 1.9).
//
// The firmware's RESET_SYSTEM ($11) pulses the host /RESET line a SECOND
// time, on a machine that has already booted. Until 2026-08-13 nothing was
// bound to that edge, so Restart resumed stale state: the 68k was never
// reset and the ROM overlay never re-armed. No gate walked the path, which
// is why it survived — this is that gate.
//
// What must hold, and each half is a separate failure mode:
//   1. the machine RE-ARMS its ROM overlay when the line is pulled;
//   2. the CPU resets, but only at a run boundary — never inside the memory
//      callback that raised it, which would reset the MCU mid-instruction;
//   3. the reset is one-shot;
//   4. the MCU, its PRAM and the devices keep running: /RESET takes the CPU
//      and the gate array, not the Egret/Cuda that is pulling it;
//   5. the power-on release does NOT count as a restart.
//
// Both flavours and both bindings: the Quadra 605 (Cuda, rising edge,
// Cpu040) and the LC II (Egret, falling edge, Cpu030). Synthetic ROM, so no
// ROM asset is needed — but the MCU firmware dump is, and its absence is a
// SKIP rather than a pass, since the whole point is the firmware path.

#include "Cpu030.h"
#include "Cpu040.h"
#include "Q605Memory.h"
#include "V8Memory.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {
int gFails = 0;
void check(bool ok, const char* machine, const char* what) {
    std::printf("  %-6s %-58s %s\n", machine, what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}

// Reset PC → the ROM window, where an ADDQ/BRA pair increments a longword
// at $2000 forever. Same shape as savestate_040_test's rig.
std::vector<uint8_t> makeRom(uint32_t size, uint32_t romBase) {
    std::vector<uint8_t> rom(size, 0);
    auto w16 = [&](uint32_t a, uint16_t v) {
        rom[a] = uint8_t(v >> 8); rom[a + 1] = uint8_t(v);
    };
    auto w32 = [&](uint32_t a, uint32_t v) {
        w16(a, uint16_t(v >> 16)); w16(a + 2, uint16_t(v));
    };
    w32(4, romBase + 0x10);                     // reset PC
    w16(0x10, 0x5279); w32(0x12, 0x00002000);   // ADDQ.W #1,($2000).L
    w16(0x16, 0x60F8);                          // BRA.S -8
    for (uint32_t i = 0x20; i < rom.size(); i++) rom[i] = uint8_t(i * 7);
    uint32_t sum = 0;
    for (size_t i = 4; i + 1 < rom.size(); i += 2)
        sum += uint32_t(rom[i] << 8 | rom[i + 1]);
    w32(0, sum);
    return rom;
}

// The five properties, once per platform. `lle` is the machine's CudaLle.
template <class Mem, class Cpu>
void testRestart(const char* name, Mem& mem, Cpu& cpu, CudaLle& lle) {
    // Run until the boot overlay has been dropped by a ROM fetch: that is
    // the state a booted machine is in, and the one Restart has to undo.
    cpu.runCycles(200000);
    check(!mem.overlay(), name, "setup: the stub cleared the boot overlay");
    check(!mem.consumeRestart(), name,
          "power-on: the boot release is NOT a restart");

    const long mcuBefore = lle.mcu().instructions;
    const uint8_t pramBefore = lle.pram(0x8A);

    // ── The firmware pulls /RESET ────────────────────────────────────────
    lle.hostReset();

    check(mem.overlay(), name, "restart: the ROM overlay is re-armed");
    check(lle.mcu().instructions == mcuBefore, name,
          "restart: the MCU is not reset by the line it pulls");
    check(lle.pram(0x8A) == pramBefore, name, "restart: PRAM survives");

    // The reset must be PENDING, not applied: applying it inside the
    // callback is the re-entrancy this design exists to avoid.
    check(mem.consumeRestart(), name, "restart: the CPU reset is latched");
    check(!mem.consumeRestart(), name, "restart: the latch is one-shot");

    // …and the CPU takes it at its next run boundary. Re-arm and run.
    lle.hostReset();
    const uint32_t pcBefore = cpu.getPC();
    cpu.runCycles(2000);
    check(cpu.getPC() != pcBefore || !mem.overlay(), name,
          "restart: the CPU restarted from the reset vectors");
    check(!mem.overlay(), name,
          "restart: the re-armed overlay is dropped again by the ROM fetch");
    check(!mem.consumeRestart(), name, "restart: the CPU consumed the latch");

    // A machine that restarted must still be a working machine.
    const uint32_t counter0 =
        uint32_t(mem.peek8(0x2000)) << 8 | mem.peek8(0x2001);
    cpu.runCycles(200000);
    const uint32_t counter1 =
        uint32_t(mem.peek8(0x2000)) << 8 | mem.peek8(0x2001);
    check(counter1 != counter0, name, "restart: the machine runs on afterwards");
}
} // namespace

int main() {
    std::printf("cuda_restart_test — the Finder's Restart on Egret/Cuda (F4)\n");

    // ── Quadra 605: Cuda, rising-edge release, Cpu040 ───────────────────
    {
        Q605Memory mem(8u << 20);
        if (!mem.cudaLleActive()) {
            std::printf("SKIP: needs roms/cuda/341s0788.bin\n");
            return 0;
        }
        Cpu040 cpu(mem);
        mem.loadRom(makeRom(Q605Memory::kRomSize, 0x40000000));
        mem.setCpu(&cpu);
        cpu.hardReset();
        testRestart("q605", mem, cpu, mem.cudaLle());
    }

    // ── LC II: Egret, falling-edge release, Cpu030 ──────────────────────
    {
        V8Memory mem(0xA00000);
        if (!mem.egretLleActive()) {
            std::printf("SKIP: needs roms/egret/341s0850.bin or 341s0851.bin\n");
            return gFails ? 1 : 0;
        }
        Cpu030 cpu(mem);
        // The V8 ROM answers at $00A00000 in the 24-bit map (the reset PC
        // savestate_v8_test's rig uses), not at the 040 machines' $40000000.
        mem.loadRom(makeRom(V8Memory::kRomSize, 0x00A00000));
        mem.setCpu(&cpu);
        cpu.hardReset();
        testRestart("lcii", mem, cpu, mem.egretLle());
    }

    if (gFails) {
        std::printf("cuda_restart_test: %d failure(s)\n", gFails);
        return 1;
    }
    std::printf("cuda_restart_test: OK\n");
    return 0;
}
