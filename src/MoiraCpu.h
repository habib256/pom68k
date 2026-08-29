// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Moira wrapper base (CRTP) ──
// Eleven of the twelve CPU wrappers used to carry a private copy of the
// same plumbing: the jit::MemoryHooks factory, the four bus forwarders,
// the side-effect-free disassembly read, sync()/catchUp() dispatch and the
// engine accessors — near-identical text, of which the coverage report
// said the truth: tested in one copy, never in the ten others. This base
// carries that plumbing ONCE, the MachineHost.h precedent applied to the
// CPU side.
//
// What deliberately STAYS in each derived wrapper is everything that makes
// a platform a platform: runCycles pacing, flushTicks scaling, the floppy
// boost gate, the peripheral-deadline policy (capped / uncapped / opt-in),
// CACR semantics, model/FPU selection and the setEngine lock — each with
// the measurement or incident that shaped it. A platform difference must
// be readable in the platform's file, not in a policy flag here.
//
// Cpu68k (the cycle-exact 68000 compacts) stays outside the base: its bus
// forwarders charge the video/RAM contention model on every access and its
// catchUp() has no deadline. It shares only makeJitMemoryHooks().

#pragma once
#include "MoiraSnapshot.h"
#include "jit/JitEngine.h"
#include <cstdint>

namespace pom68k {

// Bound once, with captureless lambdas: they convert to plain function
// pointers, so the engine reaches the memory map with no virtual dispatch
// and without being templated on the machine type. One copy of what used
// to be pasted into every wrapper's anonymous namespace.
template <class Memory>
jit::MemoryHooks makeJitMemoryHooks(Memory& mem) {
    jit::MemoryHooks h;
    h.self = &mem;
    h.codeSpan = [](void* s, uint32_t phys, uint32_t& len) {
        return static_cast<Memory*>(s)->codeSpan(phys, len);
    };
    h.dataSpan = [](void* s, uint32_t phys, uint32_t& len, int write) {
        return static_cast<Memory*>(s)->dataSpan(phys, len, write != 0);
    };
    h.setGuard = [](void* s, jit::CodeGuard* g) {
        static_cast<Memory*>(s)->setJitGuard(g);
    };
    h.ramBytes = [](void* s) { return static_cast<Memory*>(s)->ramBytes(); };
    // Wired only where the map exposes an alias mask (today: V8Memory,
    // whose A31 decode aliases RAM under $80xxxxxx).
    if constexpr (requires(Memory& m, uint32_t phys, const uint8_t* map,
                           uint32_t pages) {
                      m.jitAliasCodeMask(phys, map, pages);
                  }) {
        h.aliasCodeMask = [](void* s, uint32_t phys, const uint8_t* map,
                             uint32_t pages) {
            return static_cast<Memory*>(s)->jitAliasCodeMask(phys, map,
                                                             pages);
        };
    }
    return h;
}

template <class Derived, class Memory>
class MoiraCpu : public MoiraSnapshot {
public:
    jit::Engine& jit() { return jit_; }
    const jit::Engine& jit() const { return jit_; }
    int  engine() const { return jit_.enabled() ? 1 : 0; }

    void runUntil(moira::i64 clockTarget) {
        if (getClock() < clockTarget) executeUntil(clockTarget);
        self().flushTicks();
    }

    // IPL recomputed from the platform's priority resolver. Wrappers with
    // more to do shadow this: Cpu020/IIfxCpu force a pollIpl() so a
    // between-instructions level change is seen (the Mac II POST VBL-wait
    // starvation), Cpu030/Cpu040 tap their trace/lockstep hooks.
    void updateIpl() { setIPL(moira::u8(mem_.iplLevel())); }

protected:
    MoiraCpu(Memory& mem, jit::GuestFamily family,
             const jit::ResolvedConfig& jitConfig)
        : mem_(mem), jit_(*this, makeJitMemoryHooks(mem), family, jitConfig) {}

    moira::u8  read8(moira::u32 addr)  const override { return mem_.read8(addr); }
    moira::u16 read16(moira::u32 addr) const override { return mem_.read16(addr); }
    void write8(moira::u32 addr, moira::u8 v)   const override { mem_.write8(addr, v); }
    void write16(moira::u32 addr, moira::u16 v) const override { mem_.write16(addr, v); }

    // Moira's disassembler falls back to read16() unless this is overridden,
    // which sent every disassembly read through the LIVE bus: device registers
    // with read side effects (SCC status latches, IWM state lines) and, on
    // unmapped I/O, a busError() that mutates An/MMU fault state and throws.
    // peek8() is the side-effect-free path the tracers already use.
    moira::u16 read16Dasm(moira::u32 addr) const override {
        return moira::u16(moira::u16(mem_.peek8(addr)) << 8
                          | mem_.peek8(addr + 1));
    }

    // The per-charge path. onSyncCharge() is the trace tap (Cpu030's
    // peripheral trace, Cpu040's lockstep events); the default inlines to
    // nothing. catchUp() is each wrapper's own — deadline-based on the
    // event-paced families, batch-based on the IIfx.
    void sync(int cycles) override {
        clock += cycles;
        self().onSyncCharge(cycles);
        self().catchUp();
    }
    void onSyncCharge(int) {}

    // Arm the i-cache timing overlay folded into Moira's fetch path
    // (Moira.h § PomIcache; model rationale in Cpu030.h).
    void armIcacheOverlay(int missPenalty) {
        pomIcache.armed = true;
        pomIcache.missPenalty = missPenalty;
        pomIcache.reset();
    }

    Derived&       self()       { return static_cast<Derived&>(*this); }
    const Derived& self() const { return static_cast<const Derived&>(*this); }

    Memory& mem_;
    jit::Engine jit_;
    moira::i64 lastPeriphClock_ = 0;
};

}  // namespace pom68k
