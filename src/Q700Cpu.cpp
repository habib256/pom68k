// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Q700Cpu.h"
#include "Q700Memory.h"
#include <cstdlib>

static constexpr moira::i64 kPeriphBatch = 256;

Q700Cpu::Q700Cpu(Q700Memory& mem) : mem_(mem) {
    // The Quadra 700 ships a FULL 68040 (macquadra700.cpp M68040 @ 50/2 MHz),
    // so unlike the Centris the default is the 040 identity + Moira's 68882.
    // POM68K_Q700_LC040 forces the LC040 (no hardware FPU) for experiments.
    if (getenv("POM68K_Q700_LC040")) {
        setModel(moira::Model::M68LC040);
        setFPUModel(getenv("POM68K_Q700_BAREFPU") ? moira::FPUModel::NONE
                                                  : moira::FPUModel::M68882);
    } else {
        setModel(moira::Model::M68040);
        setFPUModel(moira::FPUModel::M68882);
    }
    if (getenv("POM68K_MMU040_WALK")) setMmu040AtcArmed(false);
    if (const char* b = getenv("POM68K_Q700_CACHE_BOOST")) {
        int v = atoi(b);
        if (v >= 1 && v <= 64) cacheBoost_ = v;
    }
    pomIcache.armed = true;
    pomIcache.missPenalty = icacheMiss_;
    pomIcache.reset();
}

void Q700Cpu::hardReset() {
    mem_.reset();
    lastPeriphClock_ = getClock();
    periphAccum_ = 0;
    pomIcache.reset();
    reset();
}

void Q700Cpu::runCycles(moira::i64 n) {
    executeUntil(getClock() + n * cacheBoost_);
    flushTicks();
}

void Q700Cpu::updateIpl() {
    setIPL(moira::u8(mem_.iplLevel()));
}

void Q700Cpu::stall(int cycles) {
    if (cycles <= 0) return;
    clock += moira::i64(cycles) * cacheBoost_;
    catchUp();
}

void Q700Cpu::didChangeCACR(moira::u32 value) {
    if (value & 0x0800) pomIcache.reset();
}

moira::u8  Q700Cpu::read8(moira::u32 addr)  const { return mem_.read8(addr); }
moira::u16 Q700Cpu::read16(moira::u32 addr) const { return mem_.read16(addr); }
void Q700Cpu::write8(moira::u32 addr, moira::u8 v)   const { mem_.write8(addr, v); }
void Q700Cpu::write16(moira::u32 addr, moira::u16 v) const { mem_.write16(addr, v); }

void Q700Cpu::catchUp() {
    if (clock - lastPeriphClock_ < kPeriphBatch) return;
    flushTicks();
}

void Q700Cpu::flushTicks() {
    moira::i64 d = clock - lastPeriphClock_;
    if (d > 0) {
        lastPeriphClock_ = clock;
        periphAccum_ += d;
        int m = int(periphAccum_ / cacheBoost_);
        periphAccum_ -= moira::i64(m) * cacheBoost_;
        if (m) mem_.tick(m);
    }
}

void Q700Cpu::sync(int cycles) {
    clock += cycles;
    catchUp();
}

moira::u16 Q700Cpu::read16Dasm(moira::u32 addr) const {
    return moira::u16(moira::u16(mem_.peek8(addr)) << 8 | mem_.peek8(addr + 1));
}
