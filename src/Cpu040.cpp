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
    // on. With it present, Mac OS 8.1 runs the ROM's FPU init
    // (`$408E9AC0 fmove.l fpcr,D0`) and boots.
    //
    // POM68K_Q605_NOFPU selects the LC 475 CPU identity (M68LC040) but keeps
    // Moira's 68882 as a SoftwareFPU-equivalent. Bare FPUModel::NONE still
    // reaches SysError 90 (dsNoFPU): Mac OS installs PACK 4's F-line glue,
    // which does not replace FPSP for raw 040 FPU opcodes. True NONE + FPSP
    // remains a follow-up; the soft-FPU path is what makes LC040 Finder-usable.
    if (getenv("POM68K_Q605_NOFPU")) {
        setModel(moira::Model::M68LC040);
        setFPUModel(moira::FPUModel::M68882);
        setFpuDisabledSaneFline(false);
    } else {
        setModel(moira::Model::M68040);
        setFPUModel(moira::FPUModel::M68882);
    }

    // Q8: walk-per-access comparison mode (disables the I/D ATC overlay).
    if (getenv("POM68K_MMU040_WALK")) setMmu040AtcArmed(false);

    if (const char* b = getenv("POM68K_Q605_CACHE_BOOST")) {
        int v = atoi(b);
        if (v >= 1 && v <= 64) cacheBoost_ = v;
    }
    if (const char* p = getenv("POM68K_Q605_ICACHE_MISS")) {
        int v = atoi(p);
        if (v >= 0 && v <= 64) icacheMiss_ = v;
    }
    // 040 has a larger on-chip i-cache than the 030; reuse the 030 overlay
    // as a throughput model (not an architectural copyback/snoop model).
    pomIcache.armed = true;
    pomIcache.missPenalty = icacheMiss_;
    pomIcache.reset();
}

void Cpu040::hardReset() {
    mem_.reset();
    lastPeriphClock_ = getClock();
    periphAccum_ = 0;
    pomIcache.reset();
    reset();                                 // SSP/PC from $0 (ROM overlay)
}

void Cpu040::runCycles(moira::i64 n) {
    // n is a peripheral (machine) cycle budget; run cacheBoost_× more Moira
    // cycles so hot i-cache-resident code keeps up with a real 040 without
    // derailing ASC/VIA pacing (same contract as Cpu030).
    executeUntil(getClock() + n * cacheBoost_);
    flushTicks();
}

void Cpu040::updateIpl() {
    setIPL(moira::u8(mem_.iplLevel()));
}

void Cpu040::stall(int cycles) {
    // Wait states are specified in machine cycles (VIA E-clock, SWIM +5).
    // Scale into Moira time so flushTicks() still yields `cycles` of
    // peripheral time under cacheBoost_ > 1.
    if (cycles <= 0) return;
    clock += moira::i64(cycles) * cacheBoost_;
    catchUp();
}

void Cpu040::didChangeCACR(moira::u32 value) {
    // 040 CACR: bit 15 = enable i-cache, bit 11 = clear i-cache (approx.
    // the strobes System uses). Flush the throughput model conservatively.
    if (value & 0x0800) pomIcache.reset();
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
    if (d > 0) {
        lastPeriphClock_ = clock;
        // Scale Moira cycles down to machine cycles (Cpu030::flushTicks).
        periphAccum_ += d;
        int m = int(periphAccum_ / cacheBoost_);
        periphAccum_ -= moira::i64(m) * cacheBoost_;
        if (m) mem_.tick(m);
    }
    // Clock-gated ROM UniversalInfo FPU clear — must run even when the
    // peripheral batch was already drained by sync() during executeUntil.
    mem_.maybePatchRomNoFpu(getPC());
}

void Cpu040::sync(int cycles) {
    clock += cycles;
    catchUp();
}
