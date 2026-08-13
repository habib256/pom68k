// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Cpu040.h"
#include "LleSession.h"
#include "Q605Memory.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace {
// Bound once, with captureless lambdas: they convert to plain function
// pointers, so the engine reaches the memory map with no virtual dispatch
// and without being templated on the machine type.
jit::MemoryHooks jitHooksFor(Q605Memory& mem) {
    jit::MemoryHooks h;
    h.self = &mem;
    h.codeSpan = [](void* s, uint32_t phys, uint32_t& len) {
        return static_cast<Q605Memory*>(s)->codeSpan(phys, len);
    };
    h.dataSpan = [](void* s, uint32_t phys, uint32_t& len, int write) {
        return static_cast<Q605Memory*>(s)->dataSpan(phys, len, write != 0);
    };
    h.setGuard = [](void* s, jit::CodeGuard* g) {
        static_cast<Q605Memory*>(s)->setJitGuard(g);
    };
    h.ramBytes = [](void* s) { return static_cast<Q605Memory*>(s)->ramBytes(); };
    return h;
}
}  // namespace

Cpu040::Cpu040(Q605Memory& mem)
    : mem_(mem), jit_(*this, jitHooksFor(mem), jit::kGuest68040) {
    // The JIT's generated code makes the peripheral catch-up test inline
    // rather than calling sync() on every instruction.
    jit_.setPeriphDeadline(&periphDeadline_);

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
    if (const char* nofpu = getenv("POM68K_Q605_NOFPU")) {
        setModel(moira::Model::M68LC040);
        if (nofpu[0] == '2' || nofpu[0] == 'b') {
            // LLE step 5: BARE 68LC040 — no FPU at all. F2xx opcodes take
            // the architectural vector-11 format-$4 frame; the ROM's own
            // FPU probe must conclude "absent" and select the no-FPU
            // UniversalInfo, like a real LC 475.
            setFPUModel(moira::FPUModel::NONE);
        } else {
            setFPUModel(moira::FPUModel::M68882);
        }
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
    schedulePeriphDeadline();
    pomIcache.reset();
    jit_.flushAll();
    reset();                                 // SSP/PC from $0 (ROM overlay)
}

void Cpu040::runCycles(moira::i64 n) {
    // The Egret/Cuda firmware asked for a host reset (RESET_SYSTEM $11, the
    // Finder's "Restart"). Apply it HERE, at a run boundary, never from
    // inside the memory callback that raised it — same contract as the
    // Duo's PMU wake (MscCpu.cpp:59). The machine has already re-armed its
    // ROM overlay, so this fetch takes the reset vectors out of ROM.
    if (mem_.consumeRestart()) reset();

    // n is a peripheral (machine) cycle budget; run cacheBoost_× more Moira
    // cycles so hot i-cache-resident code keeps up with a real 040 without
    // derailing ASC/VIA pacing (same contract as Cpu030).
    // The one and only switch point between the two engines.
    const moira::i64 target = getClock() + n * cacheBoost_;
    if (jit_.enabled()) jit_.executeUntil(target); else executeUntil(target);
    flushTicks();
}

void Cpu040::updateIpl() {
    setIPL(moira::u8(mem_.iplLevel()));
    if (onLockstepEvent) onLockstepEvent("ipl", lockstepDebug());
}

Cpu040::LockstepDebug Cpu040::lockstepDebug() const {
    LockstepDebug d;
    d.clock = clock;
    d.lastPeriphClock = lastPeriphClock_;
    d.periphAccum = periphAccum_;
    d.periphDeadline = periphDeadline_;
    d.iplChangeClock = iplChangeClock;
    d.iplChangeClockPrev = iplChangeClockPrev;
    d.flags = flags;
    d.iplDeferred = iplDeferred;
    d.irqDelay = irqDelay;
    d.iplPin = ipl;
    d.iplSampled = reg.ipl;
    d.iplPrev = iplPrev;
    return d;
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
    // CINV/CPUSH is the guest announcing that it just wrote code.
    jit_.flushAll();
}

moira::u8  Cpu040::read8(moira::u32 addr)  const { return mem_.read8(addr); }
moira::u16 Cpu040::read16(moira::u32 addr) const { return mem_.read16(addr); }
void Cpu040::write8(moira::u32 addr, moira::u8 v)   const { mem_.write8(addr, v); }
void Cpu040::write16(moira::u32 addr, moira::u16 v) const { mem_.write16(addr, v); }

// POM68K_PERIPH_STATS=1: how many times does the peripheral path actually
// run? The batching cost is either CALL COUNT or per-device WORK, and only
// counting separates them. Printed once at exit.
namespace {
struct PeriphStats {
    long long catchUps = 0, flushes = 0, ticks = 0, cycles = 0;
    bool on = std::getenv("POM68K_PERIPH_STATS") != nullptr;
    ~PeriphStats() {
        if (!on) return;
        std::fprintf(stderr,
            "[periph] catchUp=%lld flushTicks=%lld mem.tick=%lld "
            "machine-cycles=%lld (%.2f cycles per tick call)\n",
            catchUps, flushes, ticks, cycles,
            ticks ? double(cycles) / double(ticks) : 0.0);
    }
};
PeriphStats gPeriphStats;
}  // namespace

void Cpu040::catchUp() {
    if (gPeriphStats.on) gPeriphStats.catchUps++;
    if (clock < periphDeadline_) return;
    flushTicks();
}

void Cpu040::schedulePeriphDeadline() {
    const moira::i64 machine = std::max(1, mem_.cyclesToNextEvent());
    moira::i64 d = machine * cacheBoost_ - periphAccum_;
    if (d < 1) d = 1;
    periphDeadline_ = clock + d;
}

void Cpu040::flushTicks() {
    moira::i64 d = clock - lastPeriphClock_;
    if (d > 0) {
        lastPeriphClock_ = clock;
        // Scale Moira cycles down to machine cycles (Cpu030::flushTicks).
        periphAccum_ += d;
        int m = int(periphAccum_ / cacheBoost_);
        periphAccum_ -= moira::i64(m) * cacheBoost_;
        if (gPeriphStats.on) {
            gPeriphStats.flushes++;
            if (m) { gPeriphStats.ticks++; gPeriphStats.cycles += m; }
        }
        if (m) mem_.tick(m);
        schedulePeriphDeadline();
        if (onLockstepEvent) onLockstepEvent("flush", lockstepDebug());
    }
}

void Cpu040::sync(int cycles) {
    clock += cycles;
    if (onLockstepEvent) onLockstepEvent("sync", lockstepDebug());
    catchUp();
}

moira::u16 Cpu040::read16Dasm(moira::u32 addr) const {
    return moira::u16(moira::u16(mem_.peek8(addr)) << 8 | mem_.peek8(addr + 1));
}

void Cpu040::setEngine(int e) {
    if (!pom68k::lle::engineChangeAllowed(e)) return;
    // setEnabled() already flushes everything; the explicit disarm is belt
    // and braces — a code window left armed while the INTERPRETER runs would
    // have it fetching from a host pointer nobody maintains any more.
    jit_.setEnabled(e != 0);
    pomJitDisarm();
}
