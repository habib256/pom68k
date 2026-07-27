// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "RbvCpu.h"
#include "RbvMemory.h"
#include <cstdlib>

RbvCpu::RbvCpu(RbvMemory& mem, bool withFpu) : mem_(mem) {
    setModel(moira::Model::M68030);
    setFPUModel(withFpu ? moira::FPUModel::M68882 : moira::FPUModel::NONE);
    // History (2026-07-25): this machine shipped with cacheBoost_ = 1 because
    // the IIsi ROM's Egret transport is a tight HOST-paced bit-bang (it acks
    // each byte with a back-to-back bclr/bset of VIA1 PB4 / via_full) and the
    // boost wedged it after the first byte. The real cause was not the boost
    // itself but `viaSync`/`stall` computing E-clock alignment in the BOOSTED
    // clock domain — every VIA-paced pulse came out cacheBoost_× too short.
    // With bus time charged in machine cycles (RbvMemory::viaSync,
    // RbvCpu::stall) the transport is timed correctly and the IIsi boots at
    // the shared default boost. See CHANGELOG 2026-07-25.
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

void RbvCpu::hardReset() {
    mem_.reset();
    lastPeriphClock_ = getClock();
    pomIcache.reset();
    reset();                       // SSP/PC from $0 (ROM via overlay)
}

void RbvCpu::didChangeCACR(moira::u32 value) {
    if (value & 0x0C) pomIcache.reset();   // CI/CE strobes → flush model
}

void RbvCpu::runCycles(moira::i64 n) {
    executeUntil(getClock() + n * cacheBoost_);
    flushTicks();
}

void RbvCpu::runUntil(moira::i64 clockTarget) {
    if (getClock() < clockTarget) executeUntil(clockTarget);
    flushTicks();
}

void RbvCpu::updateIpl() {
    setIPL(moira::u8(mem_.iplLevel()));
}

void RbvCpu::stall(int cycles) {
    if (cycles <= 0) return;
    clock += moira::i64(cycles) * cacheBoost_;   // machine cycles → core clock
    catchUp();
}

moira::u8  RbvCpu::read8(moira::u32 addr)  const { return mem_.read8(addr); }
moira::u16 RbvCpu::read16(moira::u32 addr) const { return mem_.read16(addr); }
void RbvCpu::write8(moira::u32 addr, moira::u8 v)   const { mem_.write8(addr, v); }
void RbvCpu::write16(moira::u32 addr, moira::u16 v) const { mem_.write16(addr, v); }

void RbvCpu::catchUp() {
    if (clock - lastPeriphClock_ < kPeriphBatch) return;
    flushTicks();
}

void RbvCpu::flushTicks() {
    moira::i64 d = clock - lastPeriphClock_;
    if (d <= 0) return;
    lastPeriphClock_ = clock;
    periphAccum_ += d;
    int m = int(periphAccum_ / cacheBoost_);
    periphAccum_ -= moira::i64(m) * cacheBoost_;
    if (m) mem_.tick(m);
}

void RbvCpu::sync(int cycles) {
    clock += cycles;
    catchUp();
}

moira::u16 RbvCpu::read16Dasm(moira::u32 addr) const {
    return moira::u16(moira::u16(mem_.peek8(addr)) << 8 | mem_.peek8(addr + 1));
}
