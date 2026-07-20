// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Cpu020.h"
#include "MacIIMemory.h"

Cpu020::Cpu020(MacIIMemory& mem, bool withFpu) : mem_(mem) {
    setModel(moira::Model::M68020);
    setFPUModel(withFpu ? moira::FPUModel::M68881 : moira::FPUModel::NONE);
}

void Cpu020::hardReset() {
    mem_.reset();
    lastPeriphClock_ = getClock();
    reset();
    setA(7, 0x2000);
    setISP(0x2000);
}

void Cpu020::runCycles(moira::i64 n) {
    executeUntil(getClock() + n);
    flushTicks();
}

void Cpu020::runUntil(moira::i64 clockTarget) {
    if (getClock() < clockTarget) executeUntil(clockTarget);
    flushTicks();
}

void Cpu020::updateIpl() {
    setIPL(moira::u8(mem_.iplLevel()));
}

void Cpu020::stall(int cycles) {
    if (cycles <= 0) return;
    clock += cycles;
    catchUp();
}

moira::u8  Cpu020::read8(moira::u32 addr)  const { return mem_.read8(addr); }
moira::u16 Cpu020::read16(moira::u32 addr) const { return mem_.read16(addr); }

// Mac II 256 KB ROM: header at $0 is checksum, not vectors — Basilisk
// hardcodes SSP=$2000 and PC=ROMBase+$2A (newcpu.cpp m68k_reset).
moira::u16 Cpu020::read16OnReset(moira::u32 addr) const {
    switch (addr) {
    case 0: return 0;
    case 2: return 0x2000;
    case 4: return 0x4000;
    case 6: return 0x002A;
    default: return mem_.read16(addr);
    }
}
void Cpu020::write8(moira::u32 addr, moira::u8 v)   const { mem_.write8(addr, v); }
void Cpu020::write16(moira::u32 addr, moira::u16 v) const { mem_.write16(addr, v); }

void Cpu020::catchUp() {
    if (clock - lastPeriphClock_ < kPeriphBatch) return;
    flushTicks();
}

void Cpu020::flushTicks() {
    moira::i64 d = clock - lastPeriphClock_;
    if (d <= 0) return;
    lastPeriphClock_ = clock;
    mem_.tick(int(d));
}

void Cpu020::sync(int cycles) {
    clock += cycles;
    catchUp();
}
