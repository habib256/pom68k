// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "IIfxCpu.h"
#include "IIfxMemory.h"

IIfxCpu::IIfxCpu(IIfxMemory& mem, bool withFpu) : mem_(mem) {
    setModel(moira::Model::M68030);
    setFPUModel(withFpu ? moira::FPUModel::M68882 : moira::FPUModel::NONE);
}

void IIfxCpu::hardReset() {
    mem_.reset();
    lastPeriphClock_ = getClock();
    reset();
    setA(7, 0x2000);
    setISP(0x2000);
}

void IIfxCpu::runCycles(moira::i64 n) {
    executeUntil(getClock() + n);
    flushTicks();
}

void IIfxCpu::runUntil(moira::i64 clockTarget) {
    if (getClock() < clockTarget) executeUntil(clockTarget);
    flushTicks();
}

void IIfxCpu::updateIpl() {
    setIPL(moira::u8(mem_.iplLevel()));
    // Force a poll so a between-instructions OSS level change is visible
    // on the next CHECK_IRQ (the Cpu020 VBL-starvation lesson).
    pollIpl();
}

void IIfxCpu::stall(int cycles) {
    if (cycles <= 0) return;
    clock += cycles;
    catchUp();
}

moira::u8  IIfxCpu::read8(moira::u32 addr)  const { return mem_.read8(addr); }
moira::u16 IIfxCpu::read16(moira::u32 addr) const { return mem_.read16(addr); }

// IIfx 512 KB ROM header: $0 = checksum $4147DD77, $4 = entry $4080002A.
// Same Basilisk-style reset frame as Cpu020: SSP=$2000, PC from the ROM's
// own entry longword (the checksum would otherwise become the SSP).
moira::u16 IIfxCpu::read16OnReset(moira::u32 addr) const {
    switch (addr) {
    case 0: return 0;
    case 2: return 0x2000;
    case 4: return 0x4080;
    case 6: return 0x002A;
    default: return mem_.read16(addr);
    }
}

void IIfxCpu::write8(moira::u32 addr, moira::u8 v)   const { mem_.write8(addr, v); }
void IIfxCpu::write16(moira::u32 addr, moira::u16 v) const { mem_.write16(addr, v); }

void IIfxCpu::catchUp() {
    if (clock - lastPeriphClock_ < kPeriphBatch) return;
    flushTicks();
}

void IIfxCpu::flushTicks() {
    moira::i64 d = clock - lastPeriphClock_;
    if (d <= 0) return;
    lastPeriphClock_ = clock;
    mem_.tick(int(d));
}

void IIfxCpu::sync(int cycles) {
    clock += cycles;
    catchUp();
}

moira::u16 IIfxCpu::read16Dasm(moira::u32 addr) const {
    return moira::u16(moira::u16(mem_.peek8(addr)) << 8 | mem_.peek8(addr + 1));
}
