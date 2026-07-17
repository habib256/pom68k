// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Cpu030.h"
#include "V8Memory.h"
#include <cstdio>
#include <cstdlib>

Cpu030::Cpu030(V8Memory& mem, bool withFpu) : mem_(mem) {
    setModel(moira::Model::M68030);
    setFPUModel(withFpu ? moira::FPUModel::M68882 : moira::FPUModel::NONE);
    if (const char* b = getenv("POM68K_CACHE_BOOST")) {
        int v = atoi(b);
        if (v >= 1 && v <= 16) cacheBoost_ = v;
    }
    if (maxBoost_ < cacheBoost_) maxBoost_ = cacheBoost_;   // base is a floor
}

void Cpu030::hardReset() {
    mem_.reset();
    lastPeriphClock_ = getClock();
    reset();                       // SSP/PC from $0 (ROM via overlay)
}

void Cpu030::runCycles(moira::i64 n) {
    if (fpuLog_) {                 // single-step so the PC ring stays current
        moira::i64 target = getClock() + n;
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
        // n is a peripheral (machine) cycle budget; run `boost`× more Moira
        // cycles so the core does a real 68030's worth of work per frame
        // (i-cache model, see Cpu030.h). Adaptive: a heavy redraw (many IRQs
        // last frame) runs at maxBoost_ so it finishes in budget; otherwise
        // the base boost keeps normal play fast.
        if (irqPrevFrame_ > kIrqBoostThreshold) boostHold_ = 45;  // ~0.75s hysteresis
        int boost = boostHold_ > 0 ? maxBoost_ : cacheBoost_;
        if (boostHold_ > 0) boostHold_--;
        executeUntil(getClock() + n * boost);
        irqPrevFrame_ = irqThisFrame_;
        irqThisFrame_ = 0;
    }
    flushTicks();                  // callers sample ASC/VBL state after a slice
}

void Cpu030::willInterrupt(moira::u8 /*level*/) { irqThisFrame_++; }

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
    if (cycles <= 0) return;
    clock += cycles;
    catchUp();
}

moira::u8  Cpu030::read8(moira::u32 addr)  const { return mem_.read8(addr); }
moira::u16 Cpu030::read16(moira::u32 addr) const { return mem_.read16(addr); }
void Cpu030::write8(moira::u32 addr, moira::u8 v)   const { mem_.write8(addr, v); }
void Cpu030::write16(moira::u32 addr, moira::u16 v) const { mem_.write16(addr, v); }

void Cpu030::catchUp() {
    if (clock - lastPeriphClock_ < kPeriphBatch) return;
    flushTicks();
}

void Cpu030::flushTicks() {
    moira::i64 d = clock - lastPeriphClock_;
    if (d <= 0) return;
    lastPeriphClock_ = clock;
    // Scale elapsed Moira cycles down to machine cycles so peripherals keep
    // their real cadence while the core runs kCacheBoost× more instructions.
    periphAccum_ += d;
    int m = int(periphAccum_ / cacheBoost_);
    periphAccum_ -= moira::i64(m) * cacheBoost_;
    if (m) mem_.tick(m);           // VIA1 timers (φ2 = CPU/20) + 60.15 Hz
}

void Cpu030::sync(int cycles) {
    clock += cycles;
    catchUp();
}
