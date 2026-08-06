// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Cpu020.h"
#include "MacIIMemory.h"

namespace {
jit::MemoryHooks macIIJitHooks(MacIIMemory& mem) {
    jit::MemoryHooks h;
    h.self = &mem;
    h.codeSpan = [](void* s, uint32_t phys, uint32_t& len) {
        return static_cast<MacIIMemory*>(s)->codeSpan(phys, len);
    };
    h.dataSpan = [](void* s, uint32_t phys, uint32_t& len, int write) {
        return static_cast<MacIIMemory*>(s)->dataSpan(phys, len, write != 0);
    };
    h.setGuard = [](void* s, jit::CodeGuard* g) {
        static_cast<MacIIMemory*>(s)->setJitGuard(g);
    };
    h.ramBytes = [](void* s) { return static_cast<MacIIMemory*>(s)->ramBytes(); };
    return h;
}
}  // namespace

Cpu020::Cpu020(MacIIMemory& mem, bool withFpu, bool is030)
      // The guest family cannot be read off getModel() here — setModel()
      // has not run yet at member-init time, and sampling it is exactly the
      // mistake JitEngine.h documents (it cost the Quadra its x64 backend).
    : mem_(mem), jit_(*this, macIIJitHooks(mem),
                      is030 ? jit::kGuest68030 : jit::kGuest68020) {
    setModel(is030 ? moira::Model::M68030 : moira::Model::M68020);
    setFPUModel(withFpu ? (is030 ? moira::FPUModel::M68882
                                 : moira::FPUModel::M68881)
                        : moira::FPUModel::NONE);
    // Fixed batch, not a device-derived deadline: the Mac II family is one
    // of the four platforms still on kPeriphBatch (TODO.md § 4), so the
    // engine is told the batch it has to respect rather than a deadline.
    jit_.setPeriphPacing(&lastPeriphClock_, kPeriphBatch);
}

void Cpu020::hardReset() {
    mem_.reset();
    lastPeriphClock_ = getClock();
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

void Cpu020::runUntil(moira::i64 clockTarget) {
    if (getClock() < clockTarget) executeUntil(clockTarget);
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

moira::u8  Cpu020::read8(moira::u32 addr)  const { return mem_.read8(addr); }
moira::u16 Cpu020::read16(moira::u32 addr) const { return mem_.read16(addr); }

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
void Cpu020::write8(moira::u32 addr, moira::u8 v)   const { mem_.write8(addr, v); }
void Cpu020::write16(moira::u32 addr, moira::u16 v) const { mem_.write16(addr, v); }

void Cpu020::catchUp() {
    if (clock - lastPeriphClock_ < kPeriphBatch) return;
    flushTicks();
}

void Cpu020::flushTicks() {
    moira::i64 d = clock - lastPeriphClock_;
    if (d <= 0) return;
    lastPeriphClock_ = clock;
    mem_.tick(int(d));
}

void Cpu020::sync(int cycles) {
    clock += cycles;
    catchUp();
}

moira::u16 Cpu020::read16Dasm(moira::u32 addr) const {
    return moira::u16(moira::u16(mem_.peek8(addr)) << 8 | mem_.peek8(addr + 1));
}
