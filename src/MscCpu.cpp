// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "MscCpu.h"
#include "MscMemory.h"
#include <cstdlib>

namespace {
jit::MemoryHooks mscJitHooks(MscMemory& mem) {
    jit::MemoryHooks h;
    h.self = &mem;
    h.codeSpan = [](void* s, uint32_t phys, uint32_t& len) {
        return static_cast<MscMemory*>(s)->codeSpan(phys, len);
    };
    h.dataSpan = [](void* s, uint32_t phys, uint32_t& len, int write) {
        return static_cast<MscMemory*>(s)->dataSpan(phys, len, write != 0);
    };
    h.setGuard = [](void* s, jit::CodeGuard* g) {
        static_cast<MscMemory*>(s)->setJitGuard(g);
    };
    h.ramBytes = [](void* s) { return static_cast<MscMemory*>(s)->ramBytes(); };
    return h;
}
}  // namespace

MscCpu::MscCpu(MscMemory& mem, bool withFpu)
    : mem_(mem), jit_(*this, mscJitHooks(mem), jit::kGuest68030) {
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
    pomIcache.armed = true;
    pomIcache.missPenalty = icacheMiss_;
    pomIcache.reset();
}

void MscCpu::hardReset() {
    mem_.reset();
    lastPeriphClock_ = getClock();
    pomIcache.reset();
    jit_.flushAll();
    reset();                       // SSP/PC from $0 (ROM via overlay)
}

void MscCpu::didChangeCACR(moira::u32 value) {
    pomInvalidateIcache030(value);
    jit_.flushAll();
}

void MscCpu::runCycles(moira::i64 n) {
    // PMU wake (port G bit 5 falling) requested a CPU reset — apply it at
    // this safe boundary, never from inside a memory callback.
    if (mem_.consumeWakeReset()) reset();
    const moira::i64 target = getClock() + n * cacheBoost_;
    if (jit_.enabled()) jit_.executeUntil(target); else executeUntil(target);
    flushTicks();
}

void MscCpu::runUntil(moira::i64 clockTarget) {
    if (getClock() < clockTarget) executeUntil(clockTarget);
    flushTicks();
}

void MscCpu::updateIpl() {
    setIPL(moira::u8(mem_.iplLevel()));
}

void MscCpu::stall(int cycles) {
    if (cycles <= 0) return;
    clock += moira::i64(cycles) * cacheBoost_;
    catchUp();
}

moira::u8  MscCpu::read8(moira::u32 addr)  const { return mem_.read8(addr); }
moira::u16 MscCpu::read16(moira::u32 addr) const { return mem_.read16(addr); }
void MscCpu::write8(moira::u32 addr, moira::u8 v)   const { mem_.write8(addr, v); }
void MscCpu::write16(moira::u32 addr, moira::u16 v) const { mem_.write16(addr, v); }

void MscCpu::catchUp() {
    if (clock < periphDeadline_) return;
    flushTicks();
}

// min(next observable device bound, the historical batch), in CORE cycles:
// the bound is machine cycles, and this wrapper runs cacheBoost_× faster, so
// it has to be scaled up — with the fractional remainder already banked in
// periphAccum_ subtracted, exactly as Cpu030/Cpu040 do. Bus time stays on
// the machine counter (pom68k-rbv-iisi-icache-egret); this only decides WHEN
// to enter the fan-out, never how much time the devices are given.
void MscCpu::schedulePeriphDeadline() {
    // OPT-IN for the same measured reason as the Mac II twin (see the long
    // note in Cpu020.cpp): duo230_boot_etalon 109.49 s with the deadline
    // against 100.50 s on the historical batch, **+9.0 %**, same Finder.
    // The PG&E's 68HC05 binds at ~16 machine cycles against a batch of 128,
    // so this board pays 8× more fan-out entries for exactness no gate can
    // currently see — and the Duo's product contract (§ 0·A) is "usable on
    // a Pi 400", which is the tier least able to afford it.
    static const bool kEventDriven = [] {
        const char* e = std::getenv("POM68K_DUO_EVENT");
        return e && std::atoi(e) != 0;
    }();
    if (!kEventDriven) { periphDeadline_ = clock + kPeriphBatch; return; }

    moira::i64 machine = mem_.cyclesToNextEvent();
    if (machine < 1) machine = 1;
    moira::i64 d = machine * cacheBoost_ - periphAccum_;
    if (d < 1) d = 1;
    if (d > kPeriphBatch) d = kPeriphBatch;
    periphDeadline_ = clock + d;
}

void MscCpu::flushTicks() {
    moira::i64 d = clock - lastPeriphClock_;
    if (d <= 0) return;
    lastPeriphClock_ = clock;
    periphAccum_ += d;
    int m = int(periphAccum_ / cacheBoost_);
    periphAccum_ -= moira::i64(m) * cacheBoost_;
    if (m) mem_.tick(m);
    schedulePeriphDeadline();
}

void MscCpu::sync(int cycles) {
    clock += cycles;
    catchUp();
}

moira::u16 MscCpu::read16Dasm(moira::u32 addr) const {
    return moira::u16(moira::u16(mem_.peek8(addr)) << 8 | mem_.peek8(addr + 1));
}
