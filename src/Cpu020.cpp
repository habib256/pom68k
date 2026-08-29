// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Cpu020.h"
#include <cstdlib>

Cpu020::Cpu020(MacIIMemory& mem, const jit::ResolvedConfig& jitConfig,
               const pom68k::CoreCpuConfig& cpuConfig,
               bool withFpu, bool is030)
      // The guest family cannot be read off getModel() here — setModel()
      // has not run yet at member-init time, and sampling it is exactly the
      // mistake JitEngine.h documents (it cost the Quadra its x64 backend).
    : MoiraCpu(mem, is030 ? jit::kGuest68030 : jit::kGuest68020, jitConfig) {
    setModel(is030 ? moira::Model::M68030 : moira::Model::M68020);
    setFPUModel(withFpu ? (is030 ? moira::FPUModel::M68882
                                 : moira::FPUModel::M68881)
                        : moira::FPUModel::NONE);
    eventDriven_ = cpuConfig.macIiEventDriven;
    // The JIT is paced by the same batch that caps the deadline below: the
    // cap is the worst case, so telling the engine the batch keeps it
    // conservative whatever the devices ask for on a given quantum.
    jit_.setPeriphPacing(&lastPeriphClock_, kPeriphBatch);
}

void Cpu020::hardReset() {
    mem_.reset();
    lastPeriphClock_ = getClock();
    schedulePeriphDeadline();
    jit_.flushAll();
    reset();
    setA(7, 0x2000);
    setISP(0x2000);
}

// A cache-control write is the guest announcing freshly written code — the
// same SMC hint every other wrapper honours. Bit 3 (CI) / bit 11 on the 030
// are strobes; flushing on any CACR write is conservative and cheap (the
// System writes it a handful of times per boot).
void Cpu020::didChangeCACR(moira::u32 /*value*/) {
    jit_.flushAll();
}

void Cpu020::runCycles(moira::i64 n) {
    const moira::i64 target = getClock() + n;
    if (jit_.enabled()) jit_.executeUntil(target); else executeUntil(target);
    flushTicks();
}

void Cpu020::updateIpl() {
    setIPL(moira::u8(mem_.iplLevel()));
    // Moira checkForIrq() samples reg.ipl (last POLL_IPL), not the pin.
    // Peripheral updates often land between instructions; force a poll so a
    // newly raised VIA1 CA1 is visible on the next CHECK_IRQ (Mac II POST
    // $6DD8 VBL wait was starving with pin=1 / reg.ipl=0).
    pollIpl();
}

void Cpu020::stall(int cycles) {
    if (cycles <= 0) return;
    clock += cycles;
    catchUp();
}

// Mac II 256 KB ROM: header at $0 is checksum, not vectors — Basilisk
// hardcodes SSP=$2000 and PC=ROMBase+$2A (newcpu.cpp m68k_reset).
// ROMBase is $40800000 (HMMU maps $8xxxxx → ROM); $4000002A would fetch
// RAM once VIA2 PB3 enables 24-bit mode (MAME m68kmmu.h ENABLE_II).
moira::u16 Cpu020::read16OnReset(moira::u32 addr) const {
    switch (addr) {
    case 0: return 0;
    case 2: return 0x2000;
    case 4: return 0x4080;
    case 6: return 0x002A;
    default: return mem_.read16(addr);
    }
}

void Cpu020::catchUp() {
    if (clock < periphDeadline_) return;
    flushTicks();
}

// min(next observable device bound, the historical batch). The cap is what
// makes this safe to land: every device without a deadline API keeps exactly
// its former cadence, so the change can only ever wake the fan-out EARLIER
// than the old fixed batch, never later. There is no cache boost on this
// wrapper, so the core clock IS the machine clock and no scaling is needed —
// unlike Cpu030/Cpu040, where the deadline has to be converted.
void Cpu020::schedulePeriphDeadline() {
    // OPT-IN, and the measurement is why (2026-08-13, macii_boot_etalon,
    // two pairs on one binary): deadline 65.28/66.37 s against the fixed
    // batch's 57.18/56.49 s — a repeated **+14.2 %/+17.5 %**, with the
    // etalon's three observables (menu bar 0.10, desktop 0.49, 1159 SCSI
    // commands) IDENTICAL either way. The deadline is strictly more correct
    // — IRQ jitter falls from ≤ 4.1 µs to zero — but on the only workload
    // this tree can measure, that exactness is not observable while the cost
    // is. The eight converted platforms took the same trade for almost
    // nothing because they were replacing a far coarser batch (Q605: 256,
    // and exact-1 cost +76 % there); this board's batch was already 64 and
    // its binding source, the PIC1654S at 460.8 kHz, is only ~2× finer, so
    // there is no slack to recover — only the per-entry fan-out cost.
    // Precedent: the Q605 ASC event scheduler was withdrawn ENTIRELY on a
    // throughput regression despite seven green gates (TODO § 0·A).
    // → Turn it on by default the day a gate can SEE the difference (a
    //   jitter-sensitive beyond-boot gate), or a guest symptom appears.
    if (!eventDriven_) { periphDeadline_ = clock + kPeriphBatch; return; }

    moira::i64 d = mem_.cyclesToNextEvent();
    if (d < 1) d = 1;
    if (d > kPeriphBatch) d = kPeriphBatch;
    periphDeadline_ = clock + d;
}

void Cpu020::flushTicks() {
    moira::i64 d = clock - lastPeriphClock_;
    if (d <= 0) return;
    lastPeriphClock_ = clock;
    mem_.tick(int(d));
    schedulePeriphDeadline();
}
