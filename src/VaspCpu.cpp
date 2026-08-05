// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "VaspCpu.h"
#include "VaspMemory.h"
#include <algorithm>
#include <cstdlib>

namespace {
jit::MemoryHooks vaspJitHooks(VaspMemory& mem) {
    jit::MemoryHooks h;
    h.self = &mem;
    h.codeSpan = [](void* s, uint32_t phys, uint32_t& len) {
        return static_cast<VaspMemory*>(s)->codeSpan(phys, len);
    };
    h.dataSpan = [](void* s, uint32_t phys, uint32_t& len, int write) {
        return static_cast<VaspMemory*>(s)->dataSpan(phys, len, write != 0);
    };
    h.setGuard = [](void* s, jit::CodeGuard* g) {
        static_cast<VaspMemory*>(s)->setJitGuard(g);
    };
    h.ramBytes = [](void* s) { return static_cast<VaspMemory*>(s)->ramBytes(); };
    return h;
}
}  // namespace

VaspCpu::VaspCpu(VaspMemory& mem, bool withFpu)
    : mem_(mem), jit_(*this, vaspJitHooks(mem), jit::kGuest68030) {
    setModel(moira::Model::M68030);
    setFPUModel(withFpu ? moira::FPUModel::M68882 : moira::FPUModel::NONE);
    if (const char* b = getenv("POM68K_CACHE_BOOST")) {
        int v = atoi(b);
        if (v >= 1 && v <= 64) cacheBoost_ = v;
    }
    if (const char* p = getenv("POM68K_ICACHE_MISS")) {
        int v = atoi(p);
        if (v >= 0 && v <= 64) icacheMiss_ = v;
    }
    boost_ = cacheBoost_;
    if (const char* g = getenv("POM68K_FLOPPY_BOOST_GATE"))
        floppyGate_ = atoi(g) != 0;
    pomIcache.armed = true;
    pomIcache.missPenalty = icacheMiss_;
    pomIcache.reset();
}

void VaspCpu::hardReset() {
    mem_.reset();
    lastPeriphClock_ = getClock();
    pomIcache.reset();
    jit_.flushAll();
    reset();                       // SSP/PC from $0 (ROM via overlay)
}

void VaspCpu::didChangeCACR(moira::u32 value) {
    if (value & 0x0C) pomIcache.reset();   // CI/CE strobes → flush model
    jit_.flushAll();                       // SMC hint, as on every wrapper
}

void VaspCpu::runCycles(moira::i64 n) {
    // Deliver the machine-cycle budget in bounded chunks: the floppy gate
    // can flip boost_ mid-slice, and a single core-clock target computed
    // with the old ratio would mis-deliver machine time (Cpu030 pattern).
    const moira::i64 end = machineClock() + n;
    while (machineClock() < end && !isHalted()) {
        const moira::i64 chunk =
            std::min<moira::i64>(end - machineClock(), 4096);
        const moira::i64 target = getClock() + chunk * boost_;
        if (jit_.enabled()) jit_.executeUntil(target); else executeUntil(target);
        flushTicks();
    }
    flushTicks();
}

void VaspCpu::runUntil(moira::i64 clockTarget) {
    if (getClock() < clockTarget) executeUntil(clockTarget);
    flushTicks();
}

void VaspCpu::updateIpl() {
    setIPL(moira::u8(mem_.iplLevel()));
}

void VaspCpu::stall(int cycles) {
    if (cycles <= 0) return;
    clock += moira::i64(cycles) * boost_;        // machine cycles → core clock
    catchUp();
}

moira::u8  VaspCpu::read8(moira::u32 addr)  const { return mem_.read8(addr); }
moira::u16 VaspCpu::read16(moira::u32 addr) const { return mem_.read16(addr); }
void VaspCpu::write8(moira::u32 addr, moira::u8 v)   const { mem_.write8(addr, v); }
void VaspCpu::write16(moira::u32 addr, moira::u16 v) const { mem_.write16(addr, v); }

void VaspCpu::catchUp() {
    if (clock < periphDeadline_) return;
    flushTicks();
}

void VaspCpu::flushTicks() {
    moira::i64 d = clock - lastPeriphClock_;
    if (d <= 0) return;
    lastPeriphClock_ = clock;
    periphAccum_ += d;
    int m = int(periphAccum_ / boost_);
    periphAccum_ -= moira::i64(m) * boost_;
    if (m) mem_.tick(m);
    pollBoostGate();
    schedulePeriphDeadline();
}

// The Cpu030 floppy boost gate: re-evaluated at a settled point, rebased
// so machineClock() never jumps (rationale in Cpu030.h/.cpp).
void VaspCpu::pollBoostGate() {
    const int want =
        (floppyGate_ && mem_.floppyStreaming()) ? 1 : cacheBoost_;
    if (want == boost_) return;
    machineBase_ += (clock - clockBase_) / boost_;
    clockBase_ = clock;
    periphAccum_ = periphAccum_ * want / boost_;
    boost_ = want;
}
void VaspCpu::schedulePeriphDeadline() {
    // min(next observable machine-cycle bound, the historical batch): the
    // cap keeps every transport without a deadline API (ADB PIC, IOPs) at
    // exactly its former cadence, so exactness can only refine, never
    // coarsen (TODO § 4, extension inventory 2026-08-04).
    moira::i64 machine = mem_.cyclesToNextEvent();
    if (machine < 1) machine = 1;
    moira::i64 d = machine * boost_ - periphAccum_;
    if (d > kPeriphBatch) d = kPeriphBatch;
    if (d < 1) d = 1;
    periphDeadline_ = clock + d;
}


void VaspCpu::sync(int cycles) {
    clock += cycles;
    catchUp();
}

moira::u16 VaspCpu::read16Dasm(moira::u32 addr) const {
    return moira::u16(moira::u16(mem_.peek8(addr)) << 8 | mem_.peek8(addr + 1));
}
