// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Q630Cpu.h"
#include "Q630Memory.h"
#include <cstdlib>

static constexpr moira::i64 kPeriphBatch = 256;

Q630Cpu::Q630Cpu(Q630Memory& mem) : mem_(mem) {
    // The Quadra 630 ships a FULL 68040 (macquadra630.cpp M68040 @ 33 MHz);
    // POM68K_Q630_LC040 forces the 68LC040 of the LC/Performa 630 and 580.
    if (getenv("POM68K_Q630_LC040")) {
        setModel(moira::Model::M68LC040);
        setFPUModel(getenv("POM68K_Q630_BAREFPU") ? moira::FPUModel::NONE
                                                  : moira::FPUModel::M68882);
    } else {
        setModel(moira::Model::M68040);
        setFPUModel(moira::FPUModel::M68882);
    }
    if (getenv("POM68K_MMU040_WALK")) setMmu040AtcArmed(false);
    if (const char* b = getenv("POM68K_Q630_CACHE_BOOST")) {
        int v = atoi(b);
        if (v >= 1 && v <= 64) cacheBoost_ = v;
    }
    pomIcache.armed = true;
    pomIcache.missPenalty = icacheMiss_;
    pomIcache.reset();
}

void Q630Cpu::hardReset() {
    mem_.reset();
    lastPeriphClock_ = getClock();
    periphAccum_ = 0;
    pomIcache.reset();
    reset();
}

void Q630Cpu::runCycles(moira::i64 n) {
    executeUntil(getClock() + n * cacheBoost_);
    flushTicks();
}

void Q630Cpu::updateIpl() {
    setIPL(moira::u8(mem_.iplLevel()));
}

void Q630Cpu::stall(int cycles) {
    if (cycles <= 0) return;
    clock += moira::i64(cycles) * cacheBoost_;
    catchUp();
}

void Q630Cpu::didChangeCACR(moira::u32 value) {
    if (value & 0x0800) pomIcache.reset();
}

moira::u8  Q630Cpu::read8(moira::u32 addr)  const { return mem_.read8(addr); }
moira::u16 Q630Cpu::read16(moira::u32 addr) const { return mem_.read16(addr); }
void Q630Cpu::write8(moira::u32 addr, moira::u8 v)   const { mem_.write8(addr, v); }
void Q630Cpu::write16(moira::u32 addr, moira::u16 v) const { mem_.write16(addr, v); }

void Q630Cpu::catchUp() {
    if (clock - lastPeriphClock_ < kPeriphBatch) return;
    flushTicks();
}

void Q630Cpu::flushTicks() {
    moira::i64 d = clock - lastPeriphClock_;
    if (d > 0) {
        lastPeriphClock_ = clock;
        periphAccum_ += d;
        int m = int(periphAccum_ / cacheBoost_);
        periphAccum_ -= moira::i64(m) * cacheBoost_;
        if (m) mem_.tick(m);
    }
}

void Q630Cpu::sync(int cycles) {
    clock += cycles;
    catchUp();
}
