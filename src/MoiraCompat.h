// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── The Macintosh layer on Dirk Hoffmann's Moira (FPU branch) ──
// Since 2026-11-22 the base is Dirk's rewritten Moira (docs/MOIRA_CONVERGENCE.md):
// his core, his MMU030/MMU040, his MFloat80 FPU and his caches, unmodified
// in extern/moira/Moira. POM68K's CPU wrappers were written against the
// fork, whose class carried ~120 `pom*` extensions. This class is the one
// place that maps the names the wrappers still use onto Dirk's API, so the
// wrappers keep their shape while the layer above is ported.
//
// What each group is, and what it costs:
//   - FPU selection: the fork chose the FPU apart from the CPU (MoiraFpu);
//     Dirk selects the 68040's FPU by CPU model and the 6888x by
//     coprocessor model.
//   - read32/write32: required by his interface; a long access is two word
//     cycles, the default DSACK_16 port, as the fork's default was.
//   - Host bus errors: the fork's extBusError*() entry points, all
//     [[noreturn]], throw moira::BusError with the address of the last bus
//     access; his core completes the frame.
//   - The JIT seam (pomJit*), the fork's 020/030 i-cache timing overlay
//     (pomIcache) and its 68040 cache-model switches: INERT here. This build
//     has no JIT (POM68K_JIT=OFF) and his core runs its own caches. The
//     overlay's timing is therefore absent: machine fingerprints move.
//   - Fork pipeline latches the debugger touched (mmuPipeCnt,
//     trace040Pending, irqDelay): his core has no such state; they are
//     inert members so the debugger seam compiles unchanged.
//   - pomVisitState: the save-state payload, rewritten for his state.

#pragma once
#include "Moira.h"

#include <cstdint>
#include <string>

// The fork's FPU choice, independent of the CPU model.
enum class MoiraFpu { NONE, M68881, M68882, M68040 };

class MoiraCompat : public moira::Moira {
public:
    // ── Models ──────────────────────────────────────────────────────────
    // The LC040 can use the optional soft 68882 while retaining its CPU
    // identity. Call after setModel().
    void setFPUModel(MoiraFpu f) {
        using moira::CPUModel;
        if (moira::hasCPI(cpuModel)) {
            setCPModel(f == MoiraFpu::M68881 ? moira::CPModel::M68881
                       : f == MoiraFpu::NONE ? moira::CPModel::NONE
                                             : moira::CPModel::M68882);
            return;
        }
        setLC040SoftFpu(cpuModel == CPUModel::M68LC040 && f == MoiraFpu::M68882);
        if (cpuModel == CPUModel::M68040 && f == MoiraFpu::NONE)
            setModel(CPUModel::M68LC040);
        else if (cpuModel == CPUModel::M68LC040 && f == MoiraFpu::M68040)
            setModel(CPUModel::M68040);
    }
    MoiraFpu getFPUModel() const {
        if (cpuModel == moira::CPUModel::M68040) return MoiraFpu::M68040;
        if (cpuModel == moira::CPUModel::M68LC040 && lc040SoftFpu) return MoiraFpu::M68882;
        if (!moira::hasCPI(cpuModel)) return MoiraFpu::NONE;
        switch (cpModel) {
            case moira::CPModel::M68881: return MoiraFpu::M68881;
            case moira::CPModel::M68882: return MoiraFpu::M68882;
            default:                     return MoiraFpu::NONE;
        }
    }
    moira::CPUModel getModel() const { return cpuModel; }

    // ── Registers the fork exposed directly ─────────────────────────────
    moira::u8 readFC() const { return getFC(); }
    // Prefetch queue, for tests that start mid-stream (his core has the
    // getters only).
    void setIRC(moira::u16 v) { queue.irc = v; }
    void setIRD(moira::u16 v) { queue.ird = v; }
    // Whether the core checks catchpoints on every exception (CHECK_CP).
    bool pomCatchpointsChecked() const {
        return (flags & moira::State::CHECK_CP) != 0;
    }

    // 68030 PMMU. Setters go through his MMU030 so the MMU state flag and
    // the enable/disable callbacks follow, as a write to the register would.
    moira::u64 getCRP() const { return mmu.mmu030.crp; }
    void setCRP(moira::u64 v) { mmu.mmu030.crp = v; }
    moira::u64 getSRP() const { return mmu.mmu030.srp; }
    void setSRP(moira::u64 v) { mmu.mmu030.srp = v; }
    moira::u32 getTC() const { return mmu.mmu030.tc; }
    void setTC(moira::u32 v) { mmu.mmu030.setTC(v); }
    moira::u32 getTT0() const { return mmu.mmu030.tt0; }
    void setTT0(moira::u32 v) { mmu.mmu030.setTT0(v); }
    moira::u32 getTT1() const { return mmu.mmu030.tt1; }
    void setTT1(moira::u32 v) { mmu.mmu030.setTT1(v); }
    moira::u16 getMMUSR() const { return moira::u16(mmu.mmu030.mmusr); }
    void setMMUSR(moira::u16 v) { mmu.mmu030.mmusr = v; }

    // 68040 MMU.
    moira::u32 getURP040() const { return mmu.mmu040.urp; }
    void setURP040(moira::u32 v) { mmu.mmu040.urp = v; }
    moira::u32 getSRP040() const { return mmu.mmu040.srp; }
    void setSRP040(moira::u32 v) { mmu.mmu040.srp = v; }
    moira::u32 getTC040() const { return mmu.mmu040.tc; }
    void setTC040(moira::u32 v) { mmu.mmu040.setTC(v); }
    moira::u32 getITT0() const { return mmu.mmu040.itt0; }
    void setITT0(moira::u32 v) { mmu.mmu040.setITT0(v); }
    moira::u32 getITT1() const { return mmu.mmu040.itt1; }
    void setITT1(moira::u32 v) { mmu.mmu040.setITT1(v); }
    moira::u32 getDTT0() const { return mmu.mmu040.dtt0; }
    void setDTT0(moira::u32 v) { mmu.mmu040.setDTT0(v); }
    moira::u32 getDTT1() const { return mmu.mmu040.dtt1; }
    void setDTT1(moira::u32 v) { mmu.mmu040.setDTT1(v); }
    moira::u32 getMMUSR040() const { return mmu.mmu040.mmusr; }
    void setMMUSR040(moira::u32 v) { mmu.mmu040.mmusr = v; }

    // FPU control registers; his FPU applies the architectural masks.
    moira::u32 getFPCR() const { return fpu.getFPCR(); }
    void setFPCR(moira::u32 v) { fpu.setFPCR(v); }
    moira::u32 getFPSR() const { return fpu.getFPSR(); }
    void setFPSR(moira::u32 v) { fpu.setFPSR(v); }
    moira::u32 getFPIAR() const { return fpu.getFPIAR(); }
    void setFPIAR(moira::u32 v) { fpu.setFPIAR(v); }
    // FPn as the three longwords of the extended-precision memory format
    // ([B] MC68881/882UM § 3.1.2): sign and exponent in bits 31-16 of w[0],
    // bits 15-0 zero, then the 64-bit mantissa in w[1]:w[2].
    void getFP(int n, moira::u32 w[3]) const {
        const moira::MFloat80& f = fpu.fpr[n & 7];
        w[0] = moira::u32(f.signExp()) << 16;
        w[1] = moira::u32(f.mantissa() >> 32);
        w[2] = moira::u32(f.mantissa());
    }
    void setFP(int n, const moira::u32 w[3]) {
        fpu.fpr[n & 7] = moira::MFloat80(moira::u16(w[0] >> 16),
                                         moira::u64(w[1]) << 32 | w[2]);
    }

    // The fork's disassembly entry: the instruction text only.
    int disassemble(char* out, moira::u32 addr) const {
        int len = 0;
        const std::string s = moira::Moira::disassemble("%i", addr, &len);
        std::snprintf(out, 128, "%s", s.c_str());
        return len;
    }

    // ── Host bus errors ─────────────────────────────────────────────────
    [[noreturn]] void extBusError() { throw moira::BusError(pomBusAddr); }
    [[noreturn]] void extBusError040() { throw moira::BusError(pomBusAddr); }
    [[noreturn]] void extBusError000(moira::u32 addr, bool) {
        throw moira::BusError(addr);
    }
    // Set by the wrappers' bus forwarders before every access. Every
    // override of read8/read16/write8/write16 must store it: a stale value
    // puts a wrong fault address in the frame (the LC II ROM's I/O probe
    // handler at $40A06CCC reads its high byte).
    mutable moira::u32 pomBusAddr = 0;

    // ── Inert fork hooks (see the header comment) ───────────────────────
    struct PomIcache {
        bool armed = false;
        int missPenalty = 0;
        moira::u32 tag[16] = {};
        moira::u8 valid[16] = {};
        moira::i64 fetches = 0, hits = 0, misses = 0;
        void reset() {}
    };
    PomIcache pomIcache;
    void pomInvalidateIcache030(moira::u32) {}
    void setPomCache040(bool) {}
    bool pomCache040Armed() const { return false; }
    void setPomCmStats040(bool) {}
    void setPomExceptionDiag(bool) {}
    void setMmu040AtcArmed(bool) {}
    void pomJitDisarm() {}
    void pomJitMapMoved() {}
    void pomJitDtlbFlush() {}
    template <class... A> void pomJitSetBusStall(A&&...) {}
    bool pomJitCache040Consumer = false;
    // Dirk's setIPL() raises CHECK_IRQ itself: a level change is seen at
    // the next instruction boundary without a forced poll.
    void pollIpl() {}

    // ── State the fork carried and his core does not ────────────────────
    int mmuPipeCnt = 0;
    bool trace040Pending = false;
    int irqDelay = 0;

    // The address map moved under the CPU: empty the ATCs.
    void pomFlushAtcs() {
        mmu.mmu030.atcFlush();
        mmu.mmu040.atcFlush();
    }

    // ── Save states ─────────────────────────────────────────────────────
    // The instruction-boundary state of his core. The 020/030 cache lines
    // and the i-cache latch travel, so a restored machine keeps the timing
    // of one that ran on (icache020_test § save state); the ATCs are emptied
    // on load, as the fork did (MOIRA_CONVERGENCE.md row 23).
    template <class Ar> void pomVisitState(Ar& ar) {
        auto regs = [&](moira::Registers& r) {
            ar(r.pc, r.pc0, r.prev_pc0, r.r, r.usp, r.isp, r.msp,
               r.ipl, r.vbr, r.sfc, r.dfc, r.cacr, r.caar);
            ar(r.sr.t1, r.sr.t0, r.sr.s, r.sr.m, r.sr.x, r.sr.n,
               r.sr.z, r.sr.v, r.sr.c, r.sr.ipl);
        };
        ar(clock);
        regs(reg);

        // A fault in flight at the boundary: the 68020's restart record and
        // the data cycle a handler completed before RTE (patch 6). A snapshot
        // between that RTE and the restarted instruction needs both.
        ar(restart.valid);
        regs(restart.pre);
        regs(restart.fault);
        ar(restart.preSr, restart.pc, restart.frameSp, restart.frameSr,
           restart.framePC, restart.entrySsp, restart.entryUsp);
        ar(completed.armed, completed.pc, completed.addr, completed.write,
           completed.data);
        ar(queue.edb, queue.irc, queue.ird);
        ar(budget.penalty, budget.accumulated, budget.stalled);
        ar(ipl, nmiTaken, fcl, fcSource, loopModeDelay, readBuffer, writeBuffer);
        // Debugger requests are the host's, not the guest's: they stay out of
        // the archive and survive a load (moira_state_test, hostState).
        constexpr int hostFlags = moira::State::CHECK_BP | moira::State::CHECK_WP |
                                  moira::State::CHECK_CP | moira::State::LOGGING;
        int guestFlags = flags & ~hostFlags;
        ar(guestFlags);
        if constexpr (Ar::loading) flags = (guestFlags & ~hostFlags) | (flags & hostFlags);

        auto& m3 = mmu.mmu030;
        ar(m3.crp, m3.srp, m3.tc, m3.tt0, m3.tt1, m3.mmusr);
        auto& m4 = mmu.mmu040;
        ar(m4.urp, m4.srp, m4.tc, m4.itt0, m4.itt1, m4.dtt0, m4.dtt1, m4.mmusr);

        auto ext80 = [&](moira::MFloat80& f) {
            moira::u16 se = f.signExp();
            moira::u64 man = f.mantissa();
            ar(se, man);
            if constexpr (Ar::loading) f = moira::MFloat80(se, man);
        };
        for (auto& f : fpu.fpr) ext80(f);
        moira::u32 fpcr = fpu.getFPCR(), fpsr = fpu.getFPSR(),
                   fpiar = fpu.getFPIAR();
        ar(fpcr, fpsr, fpiar);
        // FPU state between instructions: the reset (NULL frame) state, an
        // arithmetic exception taken at the next FPU instruction, the operands
        // of an instruction left to the 68040 FPSP (patch 5) and the
        // arithmetic environment.
        ar(fpu.resetState, fpu.pendingTrap);
        ar(fpu.unimp.valid, fpu.unimp.cmdreg1b, fpu.unimp.stag, fpu.unimp.dtag);
        ext80(fpu.unimp.etemp);
        ext80(fpu.unimp.fptemp);
        int envMode = int(fpu.env.roundingMode()), envPrec = int(fpu.env.precision());
        bool envLimit = fpu.env.limitsExponent();
        moira::u8 envFlags = fpu.env.flags();
        ar(envMode, envPrec, envLimit, envFlags);
        if constexpr (Ar::loading) {
            fpu.env.setRoundingMode(moira::RoundingMode(envMode));
            fpu.env.setPrecision(moira::Precision(envPrec));
            fpu.env.setLimitsExponent(envLimit);
            fpu.env.clearFlags();
            fpu.env.raiseFlags(envFlags);
        }
        if constexpr (Ar::loading) {
            fpu.setFPCR(fpcr);
            fpu.setFPSR(fpsr);
            fpu.setFPIAR(fpiar);
            pomFlushAtcs();
        }
        auto entry = [&](moira::u32& d, moira::u32& t, bool& v) { ar(d, t, v); };
        iCache.visitEntries(entry);
        dCache.visitEntries(entry);
        iCache.visitLatch([&](moira::u32& a, moira::u32& d) { ar(a, d); });
        if constexpr (Ar::loading) {
            iCache.refresh();               // active/frozen follow the CACR
            dCache.refresh();
        }
    }

protected:
    moira::u32 read32(moira::u32 addr) const override {
        return moira::u32(read16(addr)) << 16 | read16(addr + 2);
    }
    void write32(moira::u32 addr, moira::u32 val) const override {
        write16(addr, moira::u16(val >> 16));
        write16(addr + 2, moira::u16(val));
    }
};
