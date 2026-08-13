// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Moira M68020 @ 15.6672 MHz on the Mac II GLUE map (functional accuracy).

#pragma once
#include "MoiraSnapshot.h"
#include "jit/JitEngine.h"
#include <cstdint>
#include <string>

class MacIIMemory;

class Cpu020 : public MoiraSnapshot {
public:
    // is030 = the IIx/IIcx/SE-30 variant: a 68030 (built-in PMMU + 68882) on
    // the same Mac II GLUE board, sharing the mac2fdhd ROM. The GLUE still
    // does the 24-bit remap (MacIIMemory::physAddr via VIA2 PB3); the 030's
    // own PMMU stays transparent until the ROM enables it.
    explicit Cpu020(MacIIMemory& mem, bool withFpu = true, bool is030 = false);

    void hardReset();

    // ── JIT engine (src/jit/POM68K_JIT.md) ──────────────────────────────
    // The Mac II family was the last Core::C68020 machine still running
    // interpreter-only, and it needed no new seam: Moira's plain-020 fetch
    // window (pomJitFetch020) and the identity branch of pomJitProbeCode
    // have been there since the 030 extension landed. What was missing was
    // on this side — the memory hooks (MacIIMemory::codeSpan) and this
    // member. Off by default for this 020/030 family.
    //
    // Both models of this wrapper are served: the plain 68020 takes the
    // identity probe, the IIx/IIcx/SE-30 68030 takes the ATC probe and
    // mmuFetchWord — the same two paths the LC/LC II pair already uses.
    jit::Engine& jit() { return jit_; }
    const jit::Engine& jit() const { return jit_; }
    int  engine() const { return jit_.enabled() ? 1 : 0; }
    void setEngine(int e) { jit_.setEnabled(e != 0); pomJitDisarm(); }

    void runCycles(moira::i64 n);
    void runUntil(moira::i64 clockTarget);
    void updateIpl();
    void stall(int cycles);
    void flushTicks();
    // No i-cache throughput overlay on this core, so the core clock IS
    // machine time — the accessor exists so bus/wire models (E-clock, the
    // PIC1654S co-step) read the same idiom on every machine (SonoraCpu.h).
    moira::i64 machineClock() const { return clock; }

    // ── Save states (chunk "CPU ") — the Cpu030 wrapper pattern ─────────
    // periphDeadline_ is real scheduling state, not a cache: a restore that
    // dropped it would run the fan-out on a stale deadline until the next
    // flush. The savestate gates catch the omission — that is why the
    // contract in TODO § 4 names this line explicitly.
    template <class Ar> void visit(Ar& ar) {
        visitCpuCommon(ar);
        ar(lastPeriphClock_, periphDeadline_);
    }

private:
    moira::u8  read8(moira::u32 addr) const override;
    moira::u16 read16(moira::u32 addr) const override;
    // Moira's disassembler falls back to read16() unless this is overridden,
    // which sent every disassembly read through the LIVE bus: device registers
    // with read side effects (SCC status latches, IWM state lines) and, on
    // unmapped I/O, a busError() that mutates An/MMU fault state and throws.
    // peek8() is the side-effect-free path the tracers already use.
    moira::u16 read16Dasm(moira::u32 addr) const override;
    moira::u16 read16OnReset(moira::u32 addr) const override;
    void write8(moira::u32 addr, moira::u8 v) const override;
    void write16(moira::u32 addr, moira::u16 v) const override;
    void sync(int cycles) override;
    void didChangeCACR(moira::u32 value) override;
    void catchUp();

    MacIIMemory& mem_;
    jit::Engine jit_;
    moira::i64 lastPeriphClock_ = 0;
    moira::i64 periphDeadline_ = 0;
    void schedulePeriphDeadline();
    // Upper bound on the computed deadline, and the cadence the JIT is
    // paced at. Was the fixed batch until 2026-08-13.
    static constexpr int kPeriphBatch = 64;
};
