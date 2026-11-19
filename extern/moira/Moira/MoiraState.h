// POM68K — portable instruction-boundary CPU state visitor.
// VERHILLE Arnaud — Copyright (C) 2026 — MIT (see LICENSE-MIT)
//
// Included by Moira.h after the complete class. The host supplies an archive
// with operator()(fields...) and static constexpr bool loading. No machine,
// archive implementation or JIT engine is required. Field order is a host
// format contract: POM68K wraps this payload in its versioned state file.
#pragma once

namespace moira {

template<class Ar> void Moira::pomVisitState(Ar& ar)
{
    ar(clock);

    // Registers. reg.r[16] is the union's flat view — it covers d[0-7],
    // a[0-7] and the visible sp in one go.
    ar(reg.pc, reg.pc0, reg.r,
       reg.usp, reg.isp, reg.msp, reg.ipl,
       reg.vbr, reg.sfc, reg.dfc, reg.cacr, reg.caar);

    // Status register (a struct of flags, no visit() of its own).
    ar(reg.sr.t1, reg.sr.t0, reg.sr.s, reg.sr.m,
       reg.sr.x, reg.sr.n, reg.sr.z, reg.sr.v, reg.sr.c, reg.sr.ipl);

    // 68030 PMMU, then the separate 68040 MMU set.
    ar(reg.crp, reg.srp, reg.tc, reg.tt0, reg.tt1, reg.mmusr);
    ar(reg.urp040, reg.srp040, reg.tc040,
       reg.itt0, reg.itt1, reg.dtt0, reg.dtt1, reg.mmusr040);

    // Prefetch queue + interrupt machinery. The IPL history has to travel:
    // dropping it would change
    // interrupt recognition for a few cycles after a restore, which is
    // exactly the kind of drift that shows up as a rare, unreproducible
    // hang rather than a clean failure.
    ar(queue.irc, queue.ird, irqMode, ipl,
       iplPrev, iplChangeClock, iplChangeClockPrev,
       iplDelay4, iplDelay2, iplDeferred, irqDelay, stopGraceUntil);
    // The level-7 transition latch (Moira.h, nmiLatch). Format v35.
    ar(nmiLatch);

    // cp is a per-instruction EA penalty, reset by AVAILABILITY on C68020.
    // It is not maintained by the JIT. Leaving it out keeps snapshots
    // independent of engine (POM68K format v9; q605_savestate_etalon).
    // The debugger's request bits in `flags` are host state, not guest
    // state (DebugSession.h): a snapshot neither records them nor
    // overwrites the live session's — a breakpoint armed at save time
    // must not arm a later machine, and one armed now survives a load.
    constexpr int kDebugFlags = State::CHECK_BP |
        State::CHECK_WP | State::CHECK_CP |
        State::LOGGING;
    const int liveDebug = flags & kDebugFlags;
    int guestFlags = flags & ~kDebugFlags;
    // `fcl` is the function code of the LAST bus access. Nothing reads it
    // at an instruction boundary — every access sets it before use
    // (readFC, pomJitData030Ok) — and generated code never updates it, so
    // its live value says whether that access ran native or interpreted.
    // Carried verbatim, it made a snapshot engine-dependent: a Quadra 605
    // restored into a cold JIT and run 1 200 frames differed from the
    // unsaved run in this byte alone once interrupts were billed
    // (q605_savestate_etalon, 2026-11-19). The canonical reset value is
    // saved and restored instead. Format v36.
    u8 fclCanonical = FC::USER_PROG;
    ar(trace040Pending, tracePc040,
       fclCanonical, fcSource, exception, loopModeDelay,
       readBuffer, writeBuffer, guestFlags);
    if constexpr (Ar::loading) fcl = fclCanonical;
    if constexpr (Ar::loading)
        flags = (guestFlags & ~kDebugFlags) | liveDebug;

    // 68881/68882 state. FpuExtended is a plain {high, low} pair with no
    // visit(), so its halves are listed explicitly.
    for (auto& f : fpu.fp) ar(f.high, f.low);
    ar(fpu.fpcr, fpu.fpsr, fpu.fpiar,
       fpu.state, fpu.expState, fpu.expPend,
       fpu.fsaveCcr, fpu.fsaveEo, fpu.ea);

    // State that survives a CPU instruction boundary, even though the
    // operation it belongs to has not completed (moira_state_test).
    ar(mmuPipeAddr, mmuPipe, mmuPipeCnt);
    ar(mmuRteSubstArmed, mmuRteSubstWrite, mmuRteSubstAddr,
       mmuRteSubstData, mmuRteSubstPc);
    ar(fpu.busy, fpu.busyUntil, fpu.busyOpcode, fpu.busyExt, fpu.busyIa);
    for (auto& f : fpu.busyFp) ar(f.high, f.low);
    ar(fpu.busyFpsr);
    ar(fpu.cmdreg1b, fpu.cmdreg3b, fpu.stag, fpu.dtag, fpu.wbtm66,
       fpu.grs, fpu.wbte15, fpu.e1, fpu.e3, fpu.t, fpu.fpiarCu,
       fpu.fpt, fpu.et, fpu.wbt);
    // softfloat's intermediate-result precision and rounding mode outlive
    // the instruction that set them: a later single-precision move-out
    // rounds its trapped operand with them (fpuResetState). Format v32.
    u16 fpuModes = pomFpuCarriedModes();
    ar(fpuModes);
    if constexpr (Ar::loading) pomSetFpuCarriedModes(fpuModes);

    // Public restart latch, also available to hosts that re-arm it after
    // RTE. Ignore the stale EA of an inactive latch: native MOVEM does not
    // maintain that scratch value, so carrying it would depend on engine.
    u32 movemEa = mmu040MovemArmed ? mmu040MovemEa : 0;
    ar(mmu040MovemArmed, movemEa);
    if constexpr (Ar::loading) mmu040MovemEa = movemEa;

    // 68040 access-error frame latches that outlive their instruction:
    // the MOVE16 line buffer (PD0-PD3, rewritten only by the next MOVE16)
    // and WB2A (rewritten only by a MOVE16 write fault). Every later
    // format-$7 frame stacks them. Format v33: a v32 load left them zero,
    // and the first access error after it stacked a frame the unsaved
    // machine would not have (tests/jit_program_fuzz.cpp --savestate, 68040
    // seed 362; moira_state_test frameLatches040).
    for (auto& w : mmu040Move16) ar(w);
    ar(mmu040Wb2Address);

    // The 68030 counterpart: the per-instruction restart bookkeeping that
    // mmuExecuteStart resets at the NEXT instruction start. Until then it is
    // live — an interrupt or trace exception whose frame push faults (a
    // protected stack page) stacks a format-$B frame from it, and the fault
    // builder applies its (An)± fixups. A snapshot taken between the two —
    // typically while STOPped, the interrupt still to come — lost it: the
    // reloaded core stacked the prefetch-phase opcode $FFFF and a stale CCR
    // where the unsaved one stacked STOP's (tests/jit_program_fuzz.cpp
    // --mmuprot --irq-timed --savestate, 68030 seed 192). Format v34.
    //
    // …but only while STOPped. Generated code keeps none of these latches
    // (Moira.h, PomJitLayout: re-established by the next interpreted
    // instruction start), so outside STOP their live values depend on
    // whether the last instruction ran native or interpreted — on the
    // engine's warm-up, not on the guest. Carrying them verbatim made a
    // snapshot engine-dependent: an LC II restored into a cold engine and
    // run 150 000 cycles saved a different mmuCcrSave than the unsaved
    // machine (savestate_v8_test determinism, x86-64 only — a64 lined up
    // by chance). STOP itself always runs interpreted, so in STOP the
    // latches are guest state. Elsewhere the visitor saves what the next
    // mmuExecuteStart would set; the live core is not touched.
    if (Ar::loading || isStopped()) {
        ar(mmuState, mmuAd, mmuFmovemStore, mmuIdx, mmuIdxDone, mmuDataBuffer,
           mmuDispStore, mmuOpcodeV, mmuFixupReg, mmuFixupVal, mmuLogging,
           mmuRmw, mmuCcrSave, mmuLastWritePc);
    } else {
        u16 state[3] {};
        u32 ad[10] {}, fmovem[2] {}, disp[2] {}, fixVal[2] {};
        u8 fixReg[2] {};
        int idx = 0, idxDone = 0;
        u32 dataBuffer = 0, opcodeV = 0xFFFFFFFF, lastWritePc = 0;
        bool logging = true;
        u16 ccrSave = getCCR();
        ar(state, ad, fmovem, idx, idxDone, dataBuffer, disp, opcodeV,
           fixReg, fixVal, logging, mmuRmw, ccrSave, lastWritePc);
    }

    if constexpr (Ar::loading) {
        cp = 0;                  // not carried; the next instruction
                                 // would zero it anyway (see above)
        // Drop derived translations and native mappings. The architectural
        // 040 caches are restored from their payload below, after the old
        // target's lines have been invalidated by pomFlushAtcs().
        // Both ATCs are flushed regardless of model — the set the model
        // does not use is already empty, so the extra call is free.
        pomIcache.reset();
#if MOIRA_ENABLE_JIT
        pomJitDisarm();
#endif
        pomFlushAtcs();
    }

    // POM68K: the 040 I/D caches can contain bytes newer OR older than RAM.
    // Saving must not push them onto the guest bus or change guest time.
    // The execution mode travels too, so a host constructed with the cache
    // model off can restore a cache-active snapshot faithfully.
    if (cpuModel >= Model::M68EC040) {
        ar(pomCache040On);
        pomCache040I.visit(ar);
        pomCache040D.visit(ar);
    }
}

} // namespace moira
