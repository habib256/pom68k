// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Q630Cpu.h"
#include "LleSession.h"
#include "Q630Memory.h"
#include <cstdlib>

namespace {
// Bound once, with captureless lambdas: they convert to plain function
// pointers, so the engine reaches the memory map with no virtual dispatch
// and without being templated on the machine type.
jit::MemoryHooks jitHooksFor(Q630Memory& mem) {
    jit::MemoryHooks h;
    h.self = &mem;
    h.codeSpan = [](void* s, uint32_t phys, uint32_t& len) {
        return static_cast<Q630Memory*>(s)->codeSpan(phys, len);
    };
    h.dataSpan = [](void* s, uint32_t phys, uint32_t& len, int write) {
        return static_cast<Q630Memory*>(s)->dataSpan(phys, len, write != 0);
    };
    h.setGuard = [](void* s, jit::CodeGuard* g) {
        static_cast<Q630Memory*>(s)->setJitGuard(g);
    };
    h.ramBytes = [](void* s) { return static_cast<Q630Memory*>(s)->ramBytes(); };
    return h;
}
}  // namespace

Q630Cpu::Q630Cpu(Q630Memory& mem)
    : mem_(mem), jit_(*this, jitHooksFor(mem), jit::kGuest68040) {
    // The JIT's generated code makes the peripheral catch-up test inline
    // rather than calling sync() on every instruction.
    jit_.setPeriphDeadline(&periphDeadline_, [](moira::Moira* cpu) {
        static_cast<Q630Cpu*>(cpu)->flushTicks();
    });

    // The Quadra 630 ships a full 68040 with its integrated FPU
    // (macquadra630.cpp M68040 @ 33 MHz);
    // POM68K_Q630_LC040 forces the 68LC040 of the LC/Performa 630 and 580.
    if (getenv("POM68K_Q630_LC040")) {
        setModel(moira::Model::M68LC040);
        setFPUModel(getenv("POM68K_Q630_BAREFPU") ? moira::FPUModel::NONE
                                                  : moira::FPUModel::M68882);
    } else {
        setModel(moira::Model::M68040);
        setFPUModel(moira::FPUModel::M68040);
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
    jit_.flushAll();
    reset();
}

void Q630Cpu::runCycles(moira::i64 n) {
    // The Egret/Cuda firmware asked for a host reset (RESET_SYSTEM $11, the
    // Finder's "Restart"). Apply it HERE, at a run boundary, never from
    // inside the memory callback that raised it — same contract as the
    // Duo's PMU wake (MscCpu.cpp:59). The machine has already re-armed its
    // ROM overlay, so this fetch takes the reset vectors out of ROM.
    if (mem_.consumeRestart()) reset();

    // The one and only switch point between the two engines.
    const moira::i64 target = getClock() + n * cacheBoost_;
    if (jit_.enabled()) jit_.executeUntil(target); else executeUntil(target);
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
    // CINV/CPUSH is the guest announcing that it just wrote code.
    jit_.flushAll();
}

moira::u8  Q630Cpu::read8(moira::u32 addr)  const { return mem_.read8(addr); }
moira::u16 Q630Cpu::read16(moira::u32 addr) const { return mem_.read16(addr); }
void Q630Cpu::write8(moira::u32 addr, moira::u8 v)   const { mem_.write8(addr, v); }
void Q630Cpu::write16(moira::u32 addr, moira::u16 v) const { mem_.write16(addr, v); }

void Q630Cpu::catchUp() {
    if (clock < periphDeadline_) return;
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
        schedulePeriphDeadline();
    }
}
void Q630Cpu::schedulePeriphDeadline() {
    moira::i64 machine = mem_.cyclesToNextEvent();
    if (machine < 1) machine = 1;
    moira::i64 d = machine * cacheBoost_ - periphAccum_;
    if (d < 1) d = 1;
    periphDeadline_ = clock + d;
}


void Q630Cpu::sync(int cycles) {
    clock += cycles;
    catchUp();
}

moira::u16 Q630Cpu::read16Dasm(moira::u32 addr) const {
    return moira::u16(moira::u16(mem_.peek8(addr)) << 8 | mem_.peek8(addr + 1));
}

void Q630Cpu::setEngine(int e) {
    if (!pom68k::lle::engineChangeAllowed(e)) return;
    // setEnabled() already flushes everything; the explicit disarm is belt
    // and braces — a code window left armed while the INTERPRETER runs would
    // have it fetching from a host pointer nobody maintains any more.
    jit_.setEnabled(e != 0);
    pomJitDisarm();
}
