// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Cpu030.h"
#include "V8Memory.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace {
jit::MemoryHooks v8JitHooks(V8Memory& mem) {
    jit::MemoryHooks h;
    h.self = &mem;
    h.codeSpan = [](void* s, uint32_t phys, uint32_t& len) {
        return static_cast<V8Memory*>(s)->codeSpan(phys, len);
    };
    h.dataSpan = [](void* s, uint32_t phys, uint32_t& len, int write) {
        return static_cast<V8Memory*>(s)->dataSpan(phys, len, write != 0);
    };
    h.aliasCodeMask = [](void* s, uint32_t phys, const uint8_t* map,
                         uint32_t pages) {
        return static_cast<V8Memory*>(s)->jitAliasCodeMask(phys, map, pages);
    };
    h.setGuard = [](void* s, jit::CodeGuard* g) {
        static_cast<V8Memory*>(s)->setJitGuard(g);
    };
    h.ramBytes = [](void* s) { return static_cast<V8Memory*>(s)->ramBytes(); };
    return h;
}
}  // namespace

Cpu030::Cpu030(V8Memory& mem, bool withFpu, bool as020)
      // The LC profile is a plain 68020 on the same V8 bus; every other V8
      // model is a 68030. Declared here rather than read from getModel(),
      // which is not set yet at member-init time (JitEngine.h).
    : mem_(mem), jit_(*this, v8JitHooks(mem),
                      as020 ? jit::kGuest68020 : jit::kGuest68030) {
    // Generated code can charge ordinary instruction cycles inline and call
    // sync() only when the event-driven machine actually becomes due.
    jit_.setPeriphDeadline(&periphDeadline_, [](moira::Moira* cpu) {
        static_cast<Cpu030*>(cpu)->flushTicks();
    });
    // as020: the Macintosh LC profile — MAME maclc.cpp:342 runs the same
    // V8 machine on a 68020 (Apple HMMU part; the V8's $80FFFFFF decode
    // makes the HMMU translation a no-op for us). FPU socket empty by
    // default on both (maclc.cpp:325-330 config port).
    setModel(as020 ? moira::Model::M68020 : moira::Model::M68030);
    setFPUModel(withFpu ? moira::FPUModel::M68882 : moira::FPUModel::NONE);
    if (const char* b = getenv("POM68K_CACHE_BOOST")) {
        int v = atoi(b);
        if (v >= 1 && v <= 64) cacheBoost_ = v;   // resident-code ceiling ratio
    }
    if (const char* p = getenv("POM68K_ICACHE_MISS")) {
        int v = atoi(p);
        if (v >= 0 && v <= 64) icacheMiss_ = v;   // boosted cycles charged per miss
    }
    boost_ = cacheBoost_;
    if (const char* g = getenv("POM68K_FLOPPY_BOOST_GATE"))
        floppyGate_ = atoi(g) != 0;
    // Arm the i-cache timing overlay folded into Moira's fetch path
    // (Moira.h § PomIcache; model rationale in Cpu030.h).
    pomIcache.armed = true;
    pomIcache.missPenalty = icacheMiss_;
    pomIcache.reset();
}

void Cpu030::hardReset() {
    mem_.reset();
    lastPeriphClock_ = getClock();
    periphAccum_ = 0;
    schedulePeriphDeadline();
    pomIcache.reset();
    jit_.flushAll();
    reset();                       // SSP/PC from $0 (ROM via overlay)
}

// The System (and self-modifying code like SimCity's dynamic blit generator)
// clears the i-cache via CACR: bit 3 = Clear Instruction cache, bit 2 = Clear
// Entry (CAAR). These are write-only strobes (raw value, not the stored CACR).
// CI flushes the model; CEI invalidates only the CAAR-selected longword.
// (The i-cache timing model itself runs inline in Moira's fetch path —
// Moira.h § PomIcache.)
void Cpu030::didChangeCACR(moira::u32 value) {
    pomInvalidateIcache030(value);
    // A cache clear is the guest announcing freshly written code — but only
    // an INSTRUCTION-cache clear says that. CI (bit 3) and CEI (bit 2) are
    // the two strobes that do; everything else in CACR is the data cache
    // (ED/FD/CD/CED/WA) or an enable/freeze bit, and a data-cache command
    // cannot announce code — on a 68030 the data cache is write-through, so
    // a guest that wrote code and cleared only THAT is already broken on
    // real hardware. Measured on the LC II (jit_bench_lcii, 2000 frames):
    // flushing on every CACR write dropped the whole block cache 26 544
    // times, against 2 264 for a translation move and 8 for a write into
    // code — 2.8 compiled blocks per flush, i.e. the cache never got to
    // keep anything and 82 % of the guest ran on the window path.
    // What actually protects the JIT from unannounced code is `CodeGuard`
    // (201 030 precise invalidations in that same run), not this hint.
    //
    // On THIS board the hint is retired: V8Memory::kJitStoreInventoryComplete
    // documents that every store into RAM — pseudo-DMA included, which is a
    // guest MOVE here, not a bus master — passes CodeGuard::note(), so the
    // "segment loaded by SCSI" case the hint existed for is already caught
    // precisely. The i-cache MODEL invalidation above is untouched (that is
    // architecture); only the drop-all-generated-code hint goes.
    //
    // POM68K_JIT_030_CACR_FLUSH remains the measurement instrument, now
    // three-valued: unset = the board's own answer; =1 forces the hint back
    // ON (prices it on a proven board); =0 forces it OFF — on THIS wrapper
    // only: VASP/RBV/MSC never read the knob and always flush on the
    // CI/CEI strobes, precisely because their store inventories are NOT
    // proven (see their didChangeCACR). A fingerprint taken under a forced
    // value MUST be compared against one taken without.
    static const int cacrFlushKnob = [] {
        const char* v = getenv("POM68K_JIT_030_CACR_FLUSH");
        return v ? (atoi(v) != 0 ? 1 : 0) : -1;
    }();
    const bool flush = cacrFlushKnob < 0 ? !V8Memory::kJitStoreInventoryComplete
                                         : cacrFlushKnob != 0;
    if ((value & 0x0C) && flush) jit_.flushAll();
}

void Cpu030::runCycles(moira::i64 n) {
    // The Egret/Cuda firmware asked for a host reset (RESET_SYSTEM $11, the
    // Finder's "Restart"). Apply it HERE, at a run boundary, never from
    // inside the memory callback that raised it — same contract as the
    // Duo's PMU wake (MscCpu.cpp:59). The machine has already re-armed its
    // ROM overlay, so this fetch takes the reset vectors out of ROM.
    if (mem_.consumeRestart()) reset();
    if (fpuLog_) {                 // single-step so the PC ring stays current
        // runCycles(n) is a budget of n MACHINE cycles — the normal path below
        // multiplies by cacheBoost_ and flushTicks() divides by it, so running
        // only n core cycles here delivered a quarter of the peripheral time:
        // VBL, VIA timers, RTC seconds and ASC output all ran at 1/4 cadence
        // for as long as the logger was armed, perturbing the very timing the
        // logger exists to diagnose.
        moira::i64 target = getClock() + n * boost_;
        while (getClock() < target && !isHalted()) {
            uint32_t pc = getPC();
            pcRing_[pcRingPos_++ % pcRing_.size()] = pc;
            // A non-sequential PC delta ≈ a control transfer (jsr/rts/jmp/
            // taken branch). The coarse jump ring reaches thousands of
            // instructions back — it shows where the PC first derailed.
            if (lastPc_ && (pc > lastPc_ + 0x100 || pc + 0x100 < lastPc_))
                jumpRing_[jumpRingPos_++ % jumpRing_.size()] = { lastPc_, pc };
            lastPc_ = pc;
            execute();
            uint32_t a5 = getA(5);             // track A5 (CurrentA5) writes
            if (a5 != lastA5_) {
                a5Ring_[a5RingPos_++ % a5Ring_.size()] = { pc, lastA5_, a5 };
                lastA5_ = a5;
            }
        }
    } else {
        // n is a peripheral (machine) cycle budget; run boost_× more Moira
        // cycles so the core does a real 68030's worth of work per frame
        // (constant i-cache throughput ratio, see Cpu030.h). One fixed
        // ratio → uniform sound/timer tempo. The budget is delivered in
        // MACHINE terms through bounded chunks: the floppy gate can flip
        // boost_ mid-slice (inside a flushTicks at a peripheral deadline),
        // and a single core-clock target computed with the old ratio would
        // then over- or under-deliver machine time for the whole slice —
        // the sound-tempo wobble failure class.
        const moira::i64 end = machineClock() + n;
        while (machineClock() < end && !isHalted()) {
            const moira::i64 chunk =
                std::min<moira::i64>(end - machineClock(), 4096);
            const moira::i64 target = getClock() + chunk * boost_;
            if (periphTraceFn_) emitPeriphTrace(0, 0, target);
            if (jit_.enabled()) jit_.executeUntil(target);
            else executeUntil(target);
            if (periphTraceFn_) emitPeriphTrace(1, 0, target);
            flushTicks();
        }
    }
    flushTicks();                  // callers sample ASC/VBL state after a slice
}

void Cpu030::enableFpuLog(const std::string& path, size_t ringSize) {
    fpuLog_ = true;
    fpuLogPath_ = path;
    pcRing_.assign(ringSize, 0);
    pcRingPos_ = 0;
    jumpRing_.assign(1024, {0, 0});
    jumpRingPos_ = 0;
    a5Ring_.assign(64, {0, 0, 0});
    a5RingPos_ = 0;
    lastA5_ = 0;
    std::FILE* f = std::fopen(path.c_str(), "w");   // truncate any old log
    if (f) { std::fprintf(f, "POM68K LC II Line-F log\n"); std::fclose(f); }
}

// willExecute fires just before the exception is taken: reg.pc0 is still
// the faulting instruction, the register file is untouched.
void Cpu030::willExecute(moira::M68kException exc, moira::u16 vector) {
    if (fpuLog_ && vector == 11) {          // Line-F / F-line emulator vector
        // A crashed app re-triggers the Line-F handler thousands of times a
        // frame; writing the file per hit would freeze the GUI (the "souris
        // figée" symptom). Log the first few, dump once, then stop single-
        // stepping so the machine stays responsive (you can Reset / close).
        if (flineSeen_ < 8) {
            std::FILE* f = std::fopen(fpuLogPath_.c_str(), "a");
            if (f) {
                std::fprintf(f, "[%12lld] LINE-F #%ld pc0=$%08X ird=$%04X\n",
                             (long long)getClock(), flineSeen_ + 1, getPC0(), queue.ird);
                std::fclose(f);
            }
        }
        ++flineSeen_;
        if (!fpuDumped_) {
            fpuDumped_ = true;
            dumpFpuLog(vector);
            fpuLog_ = false;                // dump captured — run full speed now
        }
    }
}

void Cpu030::dumpFpuLog(moira::u16 vector) {
    std::FILE* f = std::fopen(fpuLogPath_.c_str(), "a");
    if (!f) return;
    std::fprintf(f, "\n===== first LINE-F full dump (vector %u) =====\n", vector);
    std::fprintf(f, "clock=%lld PC0=$%08X PC=$%08X ird=$%04X irc=$%04X SR=%04X\n",
                 (long long)getClock(), getPC0(), getPC(), queue.ird, queue.irc, getSR());
    for (int r = 0; r < 8; r++)
        std::fprintf(f, "D%d=%08X  A%d=%08X\n", r, getD(r), r, getA(r));
    std::fprintf(f, "SP=%08X USP=%08X VBR=%08X\n", getA(7), getUSP(), getVBR());
    std::fprintf(f, "FPCR=%08X FPSR=%08X FPIAR=%08X\n",
                 getFPCR(), getFPSR(), getFPIAR());
    for (int n = 0; n < 8; n++) {
        moira::u32 w[3]; getFP(n, w);
        std::fprintf(f, "FP%d=%04X %08X%08X\n", n, w[0] & 0xFFFF, w[1], w[2]);
    }
    char da[128];
    std::fprintf(f, "--- A5 (CurrentA5) write history (oldest first): pc: from -> to ---\n");
    for (size_t i = 0; i < a5Ring_.size(); i++) {
        A5Chg c = a5Ring_[(a5RingPos_ + i) % a5Ring_.size()];
        if (!c.pc) continue;
        std::fprintf(f, "  $%08X: $%08X -> $%08X\n", c.pc, c.from, c.to);
    }
    std::fprintf(f, "--- control-transfer history (oldest first): from -> to ---\n");
    for (size_t i = 0; i < jumpRing_.size(); i++) {
        Jump j = jumpRing_[(jumpRingPos_ + i) % jumpRing_.size()];
        if (!j.from && !j.to) continue;
        std::fprintf(f, "  $%08X -> $%08X\n", j.from, j.to);
    }
    std::fprintf(f, "--- last %zu instructions (oldest first) ---\n", pcRing_.size());
    for (size_t i = 0; i < pcRing_.size(); i++) {
        uint32_t pc = pcRing_[(pcRingPos_ + i) % pcRing_.size()];
        if (!pc) continue;
        try { disassemble(da, pc); }
        catch (...) { std::snprintf(da, sizeof da, "<dasm fault>"); }
        std::fprintf(f, "  $%08X  %s\n", pc, da);
    }
    std::fprintf(f, "===== end dump (keep playing — later LINE-Fs are appended above-style) =====\n\n");
    std::fclose(f);
}

void Cpu030::runUntil(moira::i64 clockTarget) {
    if (getClock() < clockTarget) executeUntil(clockTarget);
    flushTicks();
}

void Cpu030::updateIpl() {
    setIPL(moira::u8(mem_.iplLevel()));
}

// Wait states (VIA1 E-clock sync, SWIM +5), applied by V8Memory from
// inside a bus access — the clock bump lands mid-instruction, like the
// Plus contention model.
void Cpu030::stall(int cycles) {
    // Wait states are machine cycles (VIA E-clock, SWIM +5) — scale into
    // Moira time so they keep their real duration under boost_ > 1
    // (the Cpu040 convention; CHANGELOG 2026-07-25).
    if (cycles <= 0) return;
    clock += moira::i64(cycles) * boost_;
    catchUp();
}

moira::u8  Cpu030::read8(moira::u32 addr)  const { return mem_.read8(addr); }
moira::u16 Cpu030::read16(moira::u32 addr) const { return mem_.read16(addr); }
void Cpu030::write8(moira::u32 addr, moira::u8 v)   const { mem_.write8(addr, v); }
void Cpu030::write16(moira::u32 addr, moira::u16 v) const { mem_.write16(addr, v); }

void Cpu030::catchUp() {
    if (clock < periphDeadline_) return;
    flushTicks();
}

void Cpu030::schedulePeriphDeadline() {
    const moira::i64 machine = std::max(1, mem_.cyclesToNextEvent());
    moira::i64 d = machine * boost_ - periphAccum_;
    if (d < 1) d = 1;
    periphDeadline_ = clock + d;
}

// Re-evaluate the floppy boost gate at a settled point (peripherals just
// ticked, so motorOn() is current). Rebasing keeps machineClock()
// continuous; the sub-boost remainder dropped at clockBase_ is < 1 machine
// cycle per switch, and switches happen twice per floppy session.
void Cpu030::pollBoostGate() {
    const int want =
        (floppyGate_ && mem_.floppyStreaming()) ? 1 : cacheBoost_;
    if (want == boost_) return;
    machineBase_ += (clock - clockBase_) / boost_;
    clockBase_ = clock;
    periphAccum_ = periphAccum_ * want / boost_;   // keep the tick fraction
    boost_ = want;
}

void Cpu030::flushTicks() {
    moira::i64 d = clock - lastPeriphClock_;
    if (d <= 0) return;
    lastPeriphClock_ = clock;
    // Scale elapsed Moira cycles down to machine cycles so peripherals keep
    // their real cadence while the core runs boost_× more instructions.
    periphAccum_ += d;
    int m = int(periphAccum_ / boost_);         // scale elapsed Moira cycles back
    periphAccum_ -= moira::i64(m) * boost_;     // to real machine cycles
    if (m) mem_.tick(m);           // VIA1 timers (φ2 = CPU/20) + 60.15 Hz
    pollBoostGate();
    schedulePeriphDeadline();
    if (periphTraceFn_) emitPeriphTrace(2, m);
}

void Cpu030::emitPeriphTrace(int phase, int delivered, moira::i64 target) {
    if (!periphTraceFn_) return;
    PeriphTracePoint p;
    p.pc = getPC();
    p.clock = clock;
    p.machine = machineClock();
    p.deadline = periphDeadline_;
    p.remainder = periphAccum_;
    p.target = target;
    p.phase = phase;
    p.delivered = delivered;
    p.nextEvent = mem_.cyclesToNextEvent();
    p.deviceHash = mem_.debugDeviceHash();
    p.icFetches = pomIcache.fetches;
    p.icHits = pomIcache.hits;
    p.icMisses = pomIcache.misses;
    periphTraceFn_(periphTraceOpaque_, p);
}

void Cpu030::sync(int cycles) {
    clock += cycles;
    catchUp();
}

moira::u16 Cpu030::read16Dasm(moira::u32 addr) const {
    return moira::u16(moira::u16(mem_.peek8(addr)) << 8 | mem_.peek8(addr + 1));
}
