// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Cpu040.h"
#include "Q605Memory.h"
#include <cstdlib>

// Peripheral catch-up batching (the Cpu030 pattern): run the machine's
// tick() at most once per this many CPU cycles from sync(), so hot code
// isn't dominated by per-cycle peripheral bookkeeping.
static constexpr moira::i64 kPeriphBatch = 256;

Cpu040::Cpu040(Q605Memory& mem) : mem_(mem) {
    // Q6.6: model the full 68040 with an FPU, matching the MAME golden
    // oracle `macqd605` (macquadra605.cpp:158 `M68040(...)`; only its
    // lc475/lc575 variants use M68LC040). In Moira M68040 and M68LC040 are
    // identical except the FPU-availability bit, so this only turns the FPU
    // on. Our MacOS 8.1 boot disk (validated on that oracle) unconditionally
    // runs the ROM's FPU init (`$408E9AC0 fmove.l fpcr,D0`); on a no-FPU
    // 68LC040 that F-line trapped to the System's vector-11 "no FP package"
    // stub ($0002747E), which SysError(10)=dsLineFErr → the modal-alert spin
    // at $40802A38. With the FPU present the instruction executes in-core and
    // the boot proceeds. POM68K_Q605_NOFPU restores the 68LC040/no-FPU config
    // (accurate to bare Quadra 605 hardware, but this System has no SANE FP
    // emulator so it does not reach the Finder). The FPU model is the 68882
    // superset (Moira's only FPU) — the 040's would trap transcendentals to
    // the FPSP; results match either way.
    if (getenv("POM68K_Q605_NOFPU")) {
        setModel(moira::Model::M68LC040);
        setFPUModel(moira::FPUModel::NONE);  // 68LC040: no FPU, format $4
    } else {
        setModel(moira::Model::M68040);
        setFPUModel(moira::FPUModel::M68882);
    }
}

void Cpu040::hardReset() {
    mem_.reset();
    lastPeriphClock_ = getClock();
    reset();                                 // SSP/PC from $0 (ROM overlay)
}

void Cpu040::runCycles(moira::i64 n) {
    executeUntil(getClock() + n);
    flushTicks();
}

void Cpu040::updateIpl() {
    setIPL(moira::u8(mem_.iplLevel()));
}

void Cpu040::stall(int cycles) {
    if (cycles <= 0) return;
    clock += cycles;
    catchUp();
}

moira::u8  Cpu040::read8(moira::u32 addr)  const { return mem_.read8(addr); }
moira::u16 Cpu040::read16(moira::u32 addr) const { return mem_.read16(addr); }
void Cpu040::write8(moira::u32 addr, moira::u8 v)   const { mem_.write8(addr, v); }
void Cpu040::write16(moira::u32 addr, moira::u16 v) const { mem_.write16(addr, v); }

void Cpu040::catchUp() {
    if (clock - lastPeriphClock_ < kPeriphBatch) return;
    flushTicks();
}

void Cpu040::flushTicks() {
    moira::i64 d = clock - lastPeriphClock_;
    if (d <= 0) return;
    lastPeriphClock_ = clock;
    mem_.tick(int(d));
}

void Cpu040::sync(int cycles) {
    clock += cycles;
    catchUp();
}
