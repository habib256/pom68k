// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── 68000 CPU (Moira wrapper) ──
// Integrates Moira (vendored from NeoST, see extern/moira/POM68K_VENDOR.md)
// as the Mac Plus 68000 @ 7.8336 MHz, cycle-exact (MOIRA_PRECISE_TIMING).
// Bus callbacks route through MacMemory; Mac interrupts are all autovectored
// (VIA = level 1, SCC = level 2, programmer's switch = level 4..7).
// Integration pattern: NeoST src/core/Cpu68k.cpp (NeostMoira).
// Gate: tests/cpu_smoke.cpp.

#pragma once
#include "MoiraSnapshot.h"
#include "jit/JitEngine.h"

class MacMemory;

class Cpu68k : public MoiraSnapshot {
public:
    explicit Cpu68k(MacMemory& mem);

    void hardReset();                       // memory overlay + CPU reset

    // ── JIT engine (src/jit/POM68K_JIT.md) ──────────────────────────────
    // The compacts are the LAST family to get the second engine, and the
    // only one where the fetch window had to be written rather than reused:
    // on Core::C68020 the SYNC(x) macro expands to nothing, so the 020/030/
    // 040 window is free of cycle accounting, while here SYNC is real and
    // MOIRA_PRECISE_TIMING is what the whole Mac Plus timing claim rests
    // on. Moira::pomJitFetch000 therefore replaces the BUS READ only, and
    // charges the same cycles — including this wrapper's video/RAM
    // contention, routed back in through pomJitSetBusStall (below).
    //
    // Expect little: the window's job is to skip an ATC walk and a virtual
    // read, and a 68000 has no MMU to walk. What it does save is the map
    // decode — MacMemory::read16 is two read8() switch dispatches per
    // opcode word. Measured numbers live in src/jit/POM68K_JIT.md § 7.
    // Off by default for the cycle-exact 68000 family.
    jit::Engine& jit() { return jit_; }
    const jit::Engine& jit() const { return jit_; }
    int  engine() const { return jit_.enabled() ? 1 : 0; }
    void setEngine(int e) { jit_.setEnabled(e != 0); pomJitDisarm(); }

    void runCycles(moira::i64 n);           // execute ≥ n CPU cycles
    void runUntil(moira::i64 clockTarget);  // execute until clock ≥ target
    void updateIpl();                       // recompute IPL from VIA/SCC lines

    // RAM/video bus contention (DEV.md § Timing): during the 512 visible
    // dots of each of the 342 display lines, video owns alternate 4-cycle
    // slots — a CPU RAM access started in a video slot waits for its own.
    // The sound/PWM word fetch steals the last 4 cycles of every line's
    // hblank (all 370 lines). ROM and I/O are never contended.
    // Frame = 370 lines × 352 cycles = 130 240; visible = pos 0-255.
    static int contentionDelay(moira::i64 clock) {
        moira::i64 t = clock;
        for (;;) {                          // a wait can land in the next busy slot
            int f = int(t % 130240);
            int line = f / 352, pos = f % 352;
            int d = 0;
            if (line < 342 && pos < 256) {
                int ph = pos & 7;
                if (ph < 4) d = 4 - ph;
            } else if (pos >= 348) {
                d = 352 - pos;              // sound/PWM fetch slot
            }
            if (!d) return int(t - clock);
            t += d;
        }
    }

private:
    moira::u8  read8(moira::u32 addr) const override;
    moira::u16 read16(moira::u32 addr) const override;
    void write8(moira::u32 addr, moira::u8 v) const override;
    void write16(moira::u32 addr, moira::u16 v) const override;
    void sync(int cycles) override;

    void applyContention(moira::u32 addr) const;
    // The contention charge as Moira's fetch window can call it: a windowed
    // fetch performs no read16(), so the wait states it carries have to be
    // handed back explicitly (Moira.h § pomJitFetch000). Static so the hook
    // stays a plain function pointer in the fetch path — a virtual there is
    // the ~11 % the i-cache overlay was folded inline to avoid.
    static void jitBusStall(void* self, moira::u32 addr);
    void catchUp();                         // feed elapsed cycles to peripherals
    void willInterrupt(moira::u8 level) override { irqServed[level & 7]++; }

public:
    long irqServed[8] = {};                 // debug: interrupts taken per level

    // ── Save states (chunk "CPU ") — the Cpu030 wrapper pattern ─────────
    // irqServed is a debug counter, not guest state. No cache boost on the
    // 68000: the core clock IS machine time.
    moira::i64 machineClock() const { return clock; }
    template <class Ar> void visit(Ar& ar) {
        visitCpuCommon(ar);
        ar(lastPeriphClock_);
    }

private:

    MacMemory& mem_;
    jit::Engine jit_;
    moira::i64 lastPeriphClock_ = 0;
};
