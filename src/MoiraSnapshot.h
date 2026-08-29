// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Save states: the CPU chunk ──
// Every machine's CPU wrapper (Cpu68k, Cpu020, Cpu030, Cpu040, SonoraCpu,
// VaspCpu, RbvCpu, CentrisCpu, Q630Cpu, Q700Cpu) derives from Moira, and
// all of Moira's execution state is `protected`. A free function therefore
// cannot reach it — so this thin class sits between the wrappers and Moira
// and carries the shared visit() body once. The wrappers inherit it through
// the CRTP plumbing base (`MoiraCpu.h`), which derives from this class;
// Cpu68k, outside that base, derives from it directly.
//
// The alternative would have been to add serialization inside
// extern/moira/, which is VENDORED (POM68K_VENDOR.md tracks every local
// change and a submodule update can clobber it). Keeping the CPU chunk on
// the POM68K side of the seam costs one indirection and, in the end, a
// single vendored line: `Moira::pomFlushAtcs()`, because both ATC flushes
// are private and no public setter reaches the 030 one. That patch is
// recorded in POM68K_VENDOR.md; everything else lives here.
//
// What is NOT in the chunk, deliberately:
//   - the i-cache overlay (Moira.h § PomIcache) and the ATC. Both are pure
//     caches, re-derivable from RAM and the page tables, so a restore
//     flushes them instead of carrying them. A stale cache restored against
//     the right RAM would be a bug; an empty one can only cost a refill.
//   - `armed` / `missPenalty` / the CPU and FPU model, the dasm styles: the
//     machine's construction owns those, not the guest.

#pragma once
#include "Moira.h"
#include "SaveState.h"

class MoiraSnapshot : public moira::Moira {
protected:
    // The architectural state, in a fixed order. Anything added to Moira's
    // protected section that survives an instruction boundary belongs here.
    template <class Ar> void visitCpuCommon(Ar& ar) {
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

        // Prefetch queue + interrupt machinery. The IPL history (the WinUAE
        // ipl_fetch_next port) has to travel: dropping it would change
        // interrupt recognition for a few cycles after a restore, which is
        // exactly the kind of drift that shows up as a rare, unreproducible
        // hang rather than a clean failure.
        ar(queue.irc, queue.ird, irqMode, ipl,
           iplPrev, iplChangeClock, iplChangeClockPrev,
           iplDelay4, iplDelay2, iplDeferred, irqDelay);

        // `cp` is NOT here, and that is the point (v9, 2026-08-16). It is
        // Moira's cycle-penalty accumulator for the 68020+ extended
        // addressing modes, and `AVAILABILITY` zeroes it at the START of
        // every instruction on Core::C68020 (MoiraExec_cpp.h:11) — so at an
        // instruction boundary, which is where every snapshot is taken, it
        // holds the last instruction's leftover and means nothing. The JIT
        // does not maintain it, so serializing it made the fingerprint
        // depend on WHICH ENGINE ran the recent instructions: on
        // `q605_savestate_etalon` the direct run and the restored one (whose
        // load calls pomJitDisarm, so it re-warms through the interpreter)
        // came out identical over 33 165 131 bytes except this one — direct
        // 0, restored 2. Exactly the readBuffer/writeBuffer case of
        // 2026-08-12, and settled the same way: the engines agree by the
        // snapshot not carrying what only one of them keeps.
        ar(trace040Pending, tracePc040,
           fcl, fcSource, exception, loopModeDelay,
           readBuffer, writeBuffer, flags);

        // 68881/68882 state. FpuExtended is a plain {high, low} pair with no
        // visit(), so its halves are listed explicitly.
        for (auto& f : fpu.fp) ar(f.high, f.low);
        ar(fpu.fpcr, fpu.fpsr, fpu.fpiar,
           fpu.state, fpu.expState, fpu.expPend,
           fpu.fsaveCcr, fpu.fsaveEo, fpu.ea);

        if constexpr (Ar::loading) {
            cp = 0;                  // not carried; the next instruction
                                     // would zero it anyway (see above)
            // Caches are not carried (see the header note): drop them so the
            // first fetch after a restore refills against the restored RAM.
            // Both ATCs are flushed regardless of model — the set the model
            // does not use is already empty, so the extra call is free.
            pomIcache.reset();
            pomJitDisarm();
            pomFlushAtcs();
        }
    }
};
