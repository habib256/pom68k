// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Cpu68k.h"
#include "MacMemory.h"

Cpu68k::Cpu68k(MacMemory& mem) : mem_(mem) {
    setModel(moira::Model::M68000);
}

void Cpu68k::hardReset() {
    mem_.reset();
    lastPeriphClock_ = getClock();
    reset();                       // fetches SSP/PC from $0 (ROM via overlay)
}

void Cpu68k::runCycles(moira::i64 n) {
    executeUntil(getClock() + n);
}

void Cpu68k::runUntil(moira::i64 clockTarget) {
    if (getClock() < clockTarget) executeUntil(clockTarget);
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
