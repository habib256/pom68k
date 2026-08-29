// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Cpu68k.h"
#include "MacMemory.h"
#include "MoiraCpu.h"          // pom68k::makeJitMemoryHooks only — the
                               // wrapper base is NOT used here: this
                               // family is cycle-exact and its bus
                               // forwarders charge the contention model
                               // on every access.

Cpu68k::Cpu68k(MacMemory& mem, const jit::ResolvedConfig& jitConfig)
    : mem_(mem), jit_(*this, pom68k::makeJitMemoryHooks(mem),
           jit::kGuest68000, jitConfig) {
    setModel(moira::Model::M68000);
    // Hand the window this machine's bus model. Without it a windowed fetch
    // would skip the video/RAM contention read16() carries, and the Mac
    // Plus would run the JIT on a different clock than the interpreter —
    // which on the one cycle-exact family in the tree is not a speed-up,
    // it is a different machine.
    pomJitSetBusStall(&Cpu68k::jitBusStall, this);
}

void Cpu68k::hardReset() {
    mem_.reset();
    lastPeriphClock_ = getClock();
    jit_.flushAll();
    reset();                       // fetches SSP/PC from $0 (ROM via overlay)
}

void Cpu68k::runCycles(moira::i64 n) {
    const moira::i64 target = getClock() + n;
    if (jit_.enabled()) jit_.executeUntil(target); else executeUntil(target);
}

// The compacts' MAIN loop, unlike every other family's: MacFrameClock
// subdivides a frame into 16 absolute targets and calls this, so routing
// the engine through runCycles() alone left it switched on and idle (the
// first measurement of this seam retired exactly 0 instructions). The
// clock-target contract is the same one runCycles honours — a block chain
// never runs past `clockTarget` (POM68K_JIT.md invariant 4) — so the
// subdivision the frame clock relies on is preserved.
void Cpu68k::runUntil(moira::i64 clockTarget) {
    if (getClock() >= clockTarget) return;
    if (jit_.enabled()) jit_.executeUntil(clockTarget);
    else executeUntil(clockTarget);
}

// Mac Plus IPL: VIA → 1, SCC → 2 — but the glue DISCONNECTS the VIA's
// /IPL0 while the SCC interrupts, so level 3 never occurs (its ROM vector
// is a bare RTE → livelock). Mini vMac: IPL = (VIA & ~SCC) | (SCC << 1).
void Cpu68k::updateIpl() {
    bool scc = mem_.sccIrq();
    int ipl = ((mem_.via().irqAsserted() && !scc) ? 1 : 0) | (scc ? 2 : 0);
    setIPL(moira::u8(ipl));
}

// Wait states for contended RAM accesses. Const because Moira's bus API is
// const; the clock bump is real state, hence the cast (NeoST does the same
// for Mega STE wait states).
void Cpu68k::applyContention(moira::u32 addr) const {
    addr &= 0xFFFFFF;
    bool ram = (addr < 0x400000 && !mem_.overlay())
            || (addr >= 0x600000 && addr < 0x800000);
    if (!ram) return;
    int d = contentionDelay(clock);
    if (d) {
        auto* self = const_cast<Cpu68k*>(this);
        self->clock += d;
        self->catchUp();
    }
}

void Cpu68k::jitBusStall(void* self, moira::u32 addr) {
    static_cast<const Cpu68k*>(self)->applyContention(addr);
}

moira::u8  Cpu68k::read8(moira::u32 addr)  const { applyContention(addr); return mem_.read8(addr); }
moira::u16 Cpu68k::read16(moira::u32 addr) const { applyContention(addr); return mem_.read16(addr); }
void Cpu68k::write8(moira::u32 addr, moira::u8 v)   const { applyContention(addr); mem_.write8(addr, v); }
void Cpu68k::write16(moira::u32 addr, moira::u16 v) const { applyContention(addr); mem_.write16(addr, v); }

void Cpu68k::catchUp() {
    int d = int(clock - lastPeriphClock_);
    if (d <= 0) return;
    lastPeriphClock_ = clock;
    mem_.tick(d);                  // VIA timers (φ2 = CPU/10)
}

// PRECISE_TIMING: called before every bus access with the elapsed cycles.
void Cpu68k::sync(int cycles) {
    clock += cycles;
    catchUp();
}
