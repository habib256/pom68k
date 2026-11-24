// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "IIfxCpu.h"

IIfxCpu::IIfxCpu(IIfxMemory& mem, const jit::ResolvedConfig& jitConfig,
                 bool withFpu)
      // kGuest68030 is declared, never sampled from getModel(): setModel()
      // has not run at member-init time, and reading it there is the mistake
      // JitEngine.h documents (it cost the Quadra its x64 backend).
    : MoiraCpu(mem, jit::kGuest68030, jitConfig) {
    setModel(moira::CPUModel::M68030);
    setFPUModel(withFpu ? MoiraFpu::M68882 : MoiraFpu::NONE);
    // Fixed batch, like the Mac II family: the IIfx is one of the four
    // platforms still on kPeriphBatch rather than a device-derived
    // deadline (TODO.md § Fidélité matérielle et LLE).
    jit_.setPeriphPacing(&lastPeriphClock_, kPeriphBatch);
}

void IIfxCpu::hardReset() {
    mem_.reset();
    lastPeriphClock_ = getClock();
    jit_.flushAll();
    reset();
    setA(7, 0x2000);
    setISP(0x2000);
}

// The CACR SMC hint, retired behind this board's own audited store
// inventory (IIfxMemory::kJitStoreInventoryComplete, pinned by
// store_inventory_test): every RAM store already crosses CodeGuard::note().
// Until 2026-11-19 every CACR write — not only the CI/CEI strobes — dropped
// all generated code: 25 201 flushes in the first 611 M boot cycles, and an
// x86-64 host recompiled 857 695 blocks where a64 rode the fetch window.
// The IIfx has no i-cache model to invalidate (IIfxCpu.h).
void IIfxCpu::didChangeCACR(moira::u32 value) {
    const bool flush = cacrFlushPolicy_ < 0
        ? !IIfxMemory::kJitStoreInventoryComplete : cacrFlushPolicy_ != 0;
    if ((value & 0x0C) && flush) jit_.flushAll();
}

void IIfxCpu::runCycles(moira::i64 n) {
    const moira::i64 target = getClock() + n;
    if (jit_.enabled()) jit_.executeUntil(target); else executeUntil(target);
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
