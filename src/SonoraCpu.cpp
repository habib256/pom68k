// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "SonoraCpu.h"
#include "SonoraMemory.h"
#include <cstdlib>

SonoraCpu::SonoraCpu(SonoraMemory& mem, bool withFpu) : mem_(mem) {
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

void SonoraCpu::hardReset() {
    mem_.reset();
    lastPeriphClock_ = getClock();
    pomIcache.reset();
    reset();                       // SSP/PC from $0 (ROM via overlay)
}

void SonoraCpu::didChangeCACR(moira::u32 value) {
    if (value & 0x0C) pomIcache.reset();   // CI/CE strobes → flush model
}

void SonoraCpu::runCycles(moira::i64 n) {
    executeUntil(getClock() + n * cacheBoost_);
    flushTicks();
}

void SonoraCpu::runUntil(moira::i64 clockTarget) {
    if (getClock() < clockTarget) executeUntil(clockTarget);
    flushTicks();
}

void SonoraCpu::updateIpl() {
    setIPL(moira::u8(mem_.iplLevel()));
}

void SonoraCpu::stall(int cycles) {
    if (cycles <= 0) return;
    clock += cycles;
    catchUp();
}

moira::u8  SonoraCpu::read8(moira::u32 addr)  const { return mem_.read8(addr); }
moira::u16 SonoraCpu::read16(moira::u32 addr) const { return mem_.read16(addr); }
void SonoraCpu::write8(moira::u32 addr, moira::u8 v)   const { mem_.write8(addr, v); }
void SonoraCpu::write16(moira::u32 addr, moira::u16 v) const { mem_.write16(addr, v); }

void SonoraCpu::catchUp() {
    if (clock - lastPeriphClock_ < kPeriphBatch) return;
    flushTicks();
}

void SonoraCpu::flushTicks() {
    moira::i64 d = clock - lastPeriphClock_;
    if (d <= 0) return;
    lastPeriphClock_ = clock;
    periphAccum_ += d;
    int m = int(periphAccum_ / cacheBoost_);
    periphAccum_ -= moira::i64(m) * cacheBoost_;
    if (m) mem_.tick(m);
}

void SonoraCpu::sync(int cycles) {
    clock += cycles;
    catchUp();
}
