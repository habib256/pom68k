// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Naming a byte offset inside the CPU chunk ──
// The save-state determinism checks report "first divergence at byte N".
// That number is where a real defect was found on 2026-08-12 — and it cost a
// diagnostic round to learn that byte 312 meant `writeBuffer`, because
// nothing in the tree mapped an offset back to a field. This header is that
// map, so the next failure names itself.
//
// WHY THIS IS THE CLASS DETECTOR, and worth keeping sharp. A restore
// disarms the JIT (`pomJitDisarm`), so the restored machine runs an
// interpreted warm-up window the direct machine never runs. Any SERIALIZED
// field that the interpreter maintains and the JIT does not therefore
// diverges — and `jit_lockstep_*` cannot see it, since that gate compares
// registers, the stacks, the clock, 2 KB of RAM and a dozen IPL fields, not
// the chunk. The determinism check is the only thing standing between this
// tree and that whole class of defect.
//
// Scope note, so nobody over-trusts it: these gates run a synthetic
// counter loop. They catch a field touched by ORDINARY integer work — which
// is exactly what `writeBuffer` was, a store buffer written by every
// `writeOp`. They do NOT exercise exceptions, traces, the FPU or the PMMU,
// so a field that only moves there is out of reach. That is a smaller gap
// than it looks: the JIT only diverges on instructions it EMITS NATIVELY,
// and those are the integer moves, ALU ops and branches this loop is made
// of; everything else falls back through `pomJitExecOne` into the same
// interpreter both engines share.
//
// The layout mirrors `MoiraSnapshot.h`'s visitCpuCommon(), in order. Keep
// the two in step: a field added there and not here shifts every name after
// it, which is worse than no map at all.

#pragma once
#include <cstddef>
#include <cstdio>

namespace cpuchunk {

struct Field { const char* name; int size; };

inline constexpr Field kFields[] = {
    {"clock", 8},
    {"reg.pc", 4}, {"reg.pc0", 4},
    {"reg.r[0..15]", 64},
    {"reg.usp", 4}, {"reg.isp", 4}, {"reg.msp", 4}, {"reg.ipl", 1},
    {"reg.vbr", 4}, {"reg.sfc", 4}, {"reg.dfc", 4},
    {"reg.cacr", 4}, {"reg.caar", 4},
    {"sr.t1", 1}, {"sr.t0", 1}, {"sr.s", 1}, {"sr.m", 1}, {"sr.x", 1},
    {"sr.n", 1}, {"sr.z", 1}, {"sr.v", 1}, {"sr.c", 1}, {"sr.ipl", 1},
    {"reg.crp", 8}, {"reg.srp", 8}, {"reg.tc", 4},
    {"reg.tt0", 4}, {"reg.tt1", 4}, {"reg.mmusr", 2},
    {"reg.urp040", 4}, {"reg.srp040", 4}, {"reg.tc040", 4},
    {"reg.itt0", 4}, {"reg.itt1", 4}, {"reg.dtt0", 4}, {"reg.dtt1", 4},
    {"reg.mmusr040", 4},
    {"queue.irc", 2}, {"queue.ird", 2}, {"irqMode", 4},
    {"ipl", 1}, {"iplPrev", 1},
    {"iplChangeClock", 8}, {"iplChangeClockPrev", 8},
    {"iplDelay4", 8}, {"iplDelay2", 8},
    {"iplDeferred", 4}, {"irqDelay", 4},
    {"trace040Pending", 1}, {"tracePc040", 4},
    {"fcl", 1}, {"fcSource", 1}, {"exception", 4}, {"cp", 4},
    {"loopModeDelay", 4},
    {"readBuffer", 2}, {"writeBuffer", 2}, {"flags", 4},
    // Past this point: the 68881/68882 block (fp[8] as {high,low} pairs,
    // fpcr/fpsr/fpiar, state/expState/expPend, fsaveCcr/fsaveEo/ea).
};

// Name the field containing `off`, an offset into the CPU chunk PAYLOAD
// (not into the container). Writes the offset within that field to
// `fieldOff`. Offsets past the integer block are reported as FPU state.
inline const char* fieldAt(std::size_t off, std::size_t* fieldOff) {
    std::size_t at = 0;
    for (const Field& f : kFields) {
        if (off < at + std::size_t(f.size)) {
            if (fieldOff) *fieldOff = off - at;
            return f.name;
        }
        at += f.size;
    }
    if (fieldOff) *fieldOff = off - at;
    return "fpu.* block";
}

// Print "byte N of M" together with the chunk and field it lands in, given
// the container offset and the CPU chunk's payload start. Pass
// `cpuPayloadAt` = 0 when the offset is already chunk-relative.
inline void explain(std::size_t containerOffset, std::size_t cpuPayloadAt,
                    std::size_t total) {
    if (containerOffset < cpuPayloadAt) {
        std::printf("    first divergence at byte %zu of %zu (before the CPU chunk)\n",
                    containerOffset, total);
        return;
    }
    const std::size_t rel = containerOffset - cpuPayloadAt;
    std::size_t fo = 0;
    const char* fn = fieldAt(rel, &fo);
    std::printf("    first divergence at byte %zu of %zu"
                " — CPU chunk +%zu = %s +%zu\n",
                containerOffset, total, rel, fn, fo);
}

} // namespace cpuchunk
