// POM68K — portable instruction-boundary CPU state visitor.
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
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
       iplDelay4, iplDelay2, iplDeferred, irqDelay);

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
    ar(trace040Pending, tracePc040,
       fcl, fcSource, exception, loopModeDelay,
       readBuffer, writeBuffer, guestFlags);
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

    // Public restart latch, also available to hosts that re-arm it after
    // RTE. Ignore the stale EA of an inactive latch: native MOVEM does not
    // maintain that scratch value, so carrying it would depend on engine.
    u32 movemEa = mmu040MovemArmed ? mmu040MovemEa : 0;
    ar(mmu040MovemArmed, movemEa);
    if constexpr (Ar::loading) mmu040MovemEa = movemEa;

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
