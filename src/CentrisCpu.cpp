// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "CentrisCpu.h"
#include "CentrisMemory.h"
#include <cstdlib>

static constexpr moira::i64 kPeriphBatch = 256;

CentrisCpu::CentrisCpu(CentrisMemory& mem) : mem_(mem) {
    // Centris 610/650 = 68LC040. Default to the LC040 identity + Moira's
    // soft 68882 (Finder-usable, the Q605 no-FPU precedent).
    if (getenv("POM68K_CENTRIS_FPU")) {
        setModel(moira::Model::M68040);
        setFPUModel(moira::FPUModel::M68882);
    } else {
        setModel(moira::Model::M68LC040);
        setFPUModel(getenv("POM68K_CENTRIS_BAREFPU") ? moira::FPUModel::NONE
                                                     : moira::FPUModel::M68882);
    }
    if (getenv("POM68K_MMU040_WALK")) setMmu040AtcArmed(false);
    if (const char* b = getenv("POM68K_CENTRIS_CACHE_BOOST")) {
        int v = atoi(b);
        if (v >= 1 && v <= 64) cacheBoost_ = v;
    }
    pomIcache.armed = true;
    pomIcache.missPenalty = icacheMiss_;
    pomIcache.reset();
}

void CentrisCpu::hardReset() {
    mem_.reset();
    lastPeriphClock_ = getClock();
    periphAccum_ = 0;
    pomIcache.reset();
    reset();
}

void CentrisCpu::runCycles(moira::i64 n) {
    executeUntil(getClock() + n * cacheBoost_);
    flushTicks();
}

void CentrisCpu::updateIpl() {
    setIPL(moira::u8(mem_.iplLevel()));
}

void CentrisCpu::stall(int cycles) {
    if (cycles <= 0) return;
    clock += moira::i64(cycles) * cacheBoost_;
    catchUp();
}

void CentrisCpu::didChangeCACR(moira::u32 value) {
    if (value & 0x0800) pomIcache.reset();
}

moira::u8  CentrisCpu::read8(moira::u32 addr)  const { return mem_.read8(addr); }
moira::u16 CentrisCpu::read16(moira::u32 addr) const { return mem_.read16(addr); }
void CentrisCpu::write8(moira::u32 addr, moira::u8 v)   const { mem_.write8(addr, v); }
void CentrisCpu::write16(moira::u32 addr, moira::u16 v) const { mem_.write16(addr, v); }

void CentrisCpu::catchUp() {
    if (clock - lastPeriphClock_ < kPeriphBatch) return;
    flushTicks();
}

void CentrisCpu::flushTicks() {
    moira::i64 d = clock - lastPeriphClock_;
    if (d > 0) {
        lastPeriphClock_ = clock;
        periphAccum_ += d;
        int m = int(periphAccum_ / cacheBoost_);
        periphAccum_ -= moira::i64(m) * cacheBoost_;
        if (m) mem_.tick(m);
    }
}

void CentrisCpu::sync(int cycles) {
    clock += cycles;
    catchUp();
}
