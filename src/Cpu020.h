// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Moira M68020 @ 15.6672 MHz on the Mac II GLUE map (functional accuracy).
// Shared wrapper plumbing (bus forwarders, hooks, sync): MoiraCpu.h.

#pragma once
#include "CoreConfig.h"
#include "MacIIMemory.h"
#include "MoiraCpu.h"
#include <cstdint>

class Cpu020 : public pom68k::MoiraCpu<Cpu020, MacIIMemory> {
public:
    // is030 = the IIx/IIcx/SE-30 variant: a 68030 (built-in PMMU + 68882) on
    // the same Mac II GLUE board, sharing the mac2fdhd ROM. The GLUE still
    // does the 24-bit remap (MacIIMemory::physAddr via VIA2 PB3); the 030's
    // own PMMU stays transparent until the ROM enables it.
    explicit Cpu020(MacIIMemory& mem, const jit::ResolvedConfig& jitConfig,
                    const pom68k::CoreCpuConfig& cpuConfig,
                    bool withFpu = true, bool is030 = false);

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
    void setEngine(int e) { jit_.setEnabled(e != 0); pomJitDisarm(); }

    void runCycles(moira::i64 n);
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
    // contract in TODO § Fidélité names this line explicitly.
    template <class Ar> void visit(Ar& ar) {
        visitCpuCommon(ar);
        ar(lastPeriphClock_, periphDeadline_);
    }

private:
    friend pom68k::MoiraCpu<Cpu020, MacIIMemory>;
    bool eventDriven_ = false;
    int cacrFlushPolicy_ = -1;      // -1 = the board's own answer (didChangeCACR)
    moira::u16 read16OnReset(moira::u32 addr) const override;
    void didChangeCACR(moira::u32 value) override;
    // The port width Moira's 68020 i-cache sizes its line fills and fetch
    // charges by (MoiraDataflow readInstr, POM68K_VENDOR.md row 147): 32
    // bits, the Mac II's 68020 data bus. A line fill is the default
    // read32, two 16-bit calls; data longwords keep the fork's two-word
    // transfer on a 32-bit port (MoiraDataflow read<>), so the board's
    // access sequence for data is unchanged (berr020_test, DSACK_32).
    // Asked of the 68020 only (Moira::pomSized020): the IIx/IIcx/SE-30
    // 68030 of this wrapper has its own bus layer.
    // [F] Unknown: no document in the tree grades the bus width, and the
    // GLUE's DSACK per region (I/O devices are likely narrower) is not
    // modelled: 32 bits everywhere, which matters only for the code
    // fetch's charge.
    // Settled by: the GLUE decode's DSACK lines from a bus capture or the
    // Mac II schematics.
    moira::u8 dsack(moira::u32) const override { return moira::DSACK_32; }
    // Inline: this is the tail of every sync() charge (MoiraCpu::sync).
    void catchUp() { if (clock >= periphDeadline_) [[unlikely]] flushTicks(); }

    moira::i64 periphDeadline_ = 0;
    void schedulePeriphDeadline();
    // Upper bound on the computed deadline, and the cadence the JIT is
    // paced at. Was the fixed batch until 2026-08-13.
    static constexpr int kPeriphBatch = 64;
};
