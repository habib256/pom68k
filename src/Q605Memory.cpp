// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Q605Memory.h"
#include "Cpu040.h"
#include "Moira.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

Q605Memory::Q605Memory(uint32_t totalRam)
    : totalRam_(totalRam)
{
    // The ROM's bank prober sizes RAM by ALIASING (write a pattern,
    // find where it reappears): the size must be a power of two and
    // the whole $0-$3FFFFFFF window must mirror modulo the size, like
    // undecoded address lines on a real bank. Round down to a power of
    // two.
    while (totalRam_ & (totalRam_ - 1)) totalRam_ &= totalRam_ - 1;
    ram_.assign(totalRam_, 0);
    rom_.assign(kRomSize, 0xFF);
    vram_.assign(kVramSize, 0);
    cuda_.setAdbBus(&adb_);
    // EASC half-empty IRQ → pseudo-VIA2 bit 4 (iosb.cpp:358-361).
    asc_.onIrq = [this](bool s) { ascIrq(s); };
    // Two MFD-75W SuperDrives behind PrimeTime's SWIM2.
    drive0_.setSuperDrive(true);
    drive1_.setSuperDrive(true);
    swim_.attachDrive(&drive0_, &drive1_);
    // A cold (unsigned) XPRAM makes the ROM run its LONG full-RAM
    // burn-in on every boot and boots B&W — seed the Basilisk-verified
    // 'NuMc' defaults (docs: macemu main.cpp:106-141)
    cuda_.factoryDefaults();
    // XPRAM $78/$7A: boot drive/driver 0 (Basilisk defaults) already 0;
    // $8A |= $05 = 32-bit mode (Mac OS 8 requires 32-bit clean)
    cuda_.setPram(0x8A, uint8_t(cuda_.pram(0x8A) | 0x05));
    // SCSI bus-service latency knob (Q6.5b diagnostics): cycles between a
    // Transfer Info and its interrupt. Tested against the async-SIM NULL-
    // continuation crash: the SIM's send-CDB handler SPIN-POLLS S_INTERRUPT
    // (collector $11E386 loop), so deferring the IRQ only delays the same
    // ordering — the crash is structural, not a latency race. Default 0
    // (instant, historical behaviour); POM68K_SCSI_LAT=N opts in for tests.
    {
        const char* e = std::getenv("POM68K_SCSI_LAT");
        scsi_.setLatency(e ? std::atoi(e) : 0);
    }
    if (const char* id = std::getenv("POM68K_Q605_ID"))
        machineId_ = uint32_t(std::strtoul(id, nullptr, 16));
}

bool Q605Memory::loadRom(const std::vector<uint8_t>& data) {
    if (data.size() != kRomSize) return false;
    std::memcpy(rom_.data(), data.data(), kRomSize);
    return true;
}


void Q605Memory::reset() {
    overlay_ = true;
    pvIfr_ = pvIer_ = pvPortB_ = 0;
    nubusIrqs_ = 0xFF;
    std::memset(memcjr_, 0, sizeof memcjr_);
    std::memset(dafb_, 0, sizeof dafb_);
    std::memset(iosbRegs_, 0, sizeof iosbRegs_);
    std::memset(clut_, 0, sizeof clut_);
    dafbHolding_ = 0;
    dafbIntStatus_ = 0;
    swatchIntEnable_ = 0;
    dafbCursorLine_ = 0;
    palAddress_ = palIdx_ = 0;
    ac842Pbctrl_ = pcbr1_ = 0;
    dafbBase_ = 0;
    dafbStride_ = 1024;
    dafbConfig_ = 0;
    dafbMode_ = 0;
    prevLine_ = 0;
    via1_.reset();
    cuda_.reset();
    scc_.reset();
    scsi_.reset();
    asc_.reset();
    swim_.reset();
    swim_.attachDrive(&drive0_, &drive1_);
    drive0_.reset();
    drive1_.reset();
    ascLine_ = false;
    ascCycAcc_ = 0;
    swimLastCpu_ = -1;
    swimCycAcc_ = 0;
    scc_.setCtsHigh(false);        // no serial debugger attached (POST check)
    scc_.setAbortIdle(true);       // no LocalTalk peer — SDLC hunt streams the
                                   // standing Break/Abort as a level-4 ext/status
                                   // interrupt so OS 8.1's .MPP LAP carrier-sense
                                   // sees "wire dead" and its send/receive times
                                   // out instead of wedging (Q6.6; O6.10 on LC II)
    viaPhase_ = 0;
    tickAcc_ = 0;
    framePos_ = 0;
    vblState_ = false;
    sccIrq_ = false;
}

void Q605Memory::busError(uint32_t addr, bool write) const {
    if (onBusError) onBusError(addr, write);
    if (cpu_) cpu_->extBusError040();
    throw moira::MmuBusError{};
}

int Q605Memory::iplLevel() const {
    if (sccIrq_) return 4;                   // iosb field_interrupts
    if (via2IrqAsserted()) return 2;
    if (via1_.irqAsserted()) return 1;
    return 0;
}

void Q605Memory::updateIrq() {
    if (cpu_) cpu_->updateIpl();
}

void Q605Memory::via2Recalc() {
    if (ascLine_) pvIfr_ |= 0x10;
    if (via2IrqAsserted()) pvIfr_ |= 0x80; else pvIfr_ &= ~0x80;
    updateIrq();
}

void Q605Memory::vblIrq(bool s) {
    // MEMCjr video IRQ -> IOSB via2_irq_w<0x40>: nubus bit 6 (active
    // low) + the pseudo-VIA slot-summary IFR bit 1
    if (s) nubusIrqs_ &= ~0x40; else nubusIrqs_ |= 0x40;
    if ((nubusIrqs_ & 0x79) != 0x79) pvIfr_ |= 0x02;
    else                             pvIfr_ &= ~0x02;
    via2Recalc();
}

void Q605Memory::scsiIrq(bool s) {
    if (s) pvIfr_ |= 0x08;                   // CB2 bit (pseudovia.cpp:148)
    else   pvIfr_ &= ~0x08;
    via2Recalc();
}

// MAME swim2_device::read/write begin with sync(). Drive SWIM from the CPU
// clock so sync-on-access and the batched machine tick share one timeline.
// Moira's clock runs cacheBoost_× ahead of machine time; convert deltas
// before the C15M ratio so SWIM stays locked to VIA/ASC/SCSI under boost.
void Q605Memory::syncSwimFromCpu() {
    if (!cpu_) return;
    const int64_t now = int64_t(cpu_->getClock());
    if (swimLastCpu_ < 0) { swimLastCpu_ = now; return; }
    const int64_t delta = now - swimLastCpu_;
    if (delta <= 0) return;
    swimLastCpu_ = now;
    const int boost = std::max(1, cpu_->cacheBoost());
    swimCycAcc_ += (delta * AscIosb::kCpuHz) / boost;
    const int cyc = int(swimCycAcc_ / kCpuHz);
    swimCycAcc_ -= int64_t(cyc) * kCpuHz;
    if (cyc) swim_.tick(cyc);
}

void Q605Memory::ascIrq(bool s) {
    ascLine_ = s;
    if (s) pvIfr_ |= 0x10;                   // EASC bit 4 (IFR/IER mask $1B)
    else   pvIfr_ &= ~0x10;
    via2Recalc();
}

// VIA1 E-clock sync (iosb via_sync, same arithmetic as the LC II):
// cpuClk/viaClk = 25 MHz / 783.36 kHz ≈ 31.91 — use the same integer
// scheme with a 32:1 approximation. Work in machine-cycle space so the
// alignment is invariant under POM68K_Q605_CACHE_BOOST.
void Q605Memory::viaSync() {
    if (!cpu_) return;
    const int boost = std::max(1, cpu_->cacheBoost());
    int64_t c = int64_t(cpu_->getClock()) / boost;
    int64_t viaCycle = c / 32;
    int64_t target = (viaCycle * 2 + 3) * 16 + 1;
    if (target > c) cpu_->stall(int(target - c));
}

uint8_t Q605Memory::viaAccess8(uint32_t addr, bool write, uint8_t v) {
    if (cpu_) cpu_->flushTicks();
    viaSync();
    int reg = (addr >> 9) & 0x0F;            // $200 stride
    if (write) {
        via1_.write(reg, v);
        // Port B outputs carry the Cuda handshake (PB4 BYTEACK/VIA_FULL,
        // PB5 TIP/SYS_SESSION — macquadra605.cpp:230-233)
        if (reg == Via6522::ORB || reg == Via6522::DDRB)
            cuda_.portBChanged(via1_.portB());
        updateIrq();
        return 0;
    }
    if (reg == Via6522::ORB)                 // PB3 = TREQ, live
        via1_.setInB(uint8_t(0xC7 | (cuda_.xcvrSession() << 3)));
    uint8_t d = via1_.read(reg);
    updateIrq();
    return d;
}

uint8_t Q605Memory::via2Access8(uint32_t addr, bool write, uint8_t v) {
    if (cpu_) cpu_->flushTicks();
    viaSync();
    int reg = (addr >> 9) & 0x0F;            // pseudovia.cpp quadra flavor
    if (write) {
        switch (reg) {
            case 0:  pvPortB_ = v; break;    // DFAC latches — ignored
            case 1:  break;                  // port A is input-only here
            case 13: pvIfr_ &= ~(v & 0x1B); via2Recalc(); break;
            case 14:
                if (v & 0x80) pvIer_ |= v & 0x1B;
                else          pvIer_ &= ~(v & 0x1B);
                via2Recalc();
                break;
            default: break;
        }
        return 0;
    }
    switch (reg) {
        case 0:  return pvPortB_;
        case 1: case 15: return nubusIrqs_;
        // IFR bit0 reflects the live SCSI DRQ line (pseudovia.cpp:162
        // scsi_drq_w sets reg[3] |= 0x01). The Mac OS 8.1 SCSI driver polls
        // this bit ($408D1FA2) between the 53C96's S_TC0 and its 16-byte
        // pseudo-DMA window burst.
        case 13: return (pvIfr_ & ~0x01) | (scsi_.drq() ? 0x01 : 0);
        case 14: return pvIer_;
        default: return 0;
    }
}

// ── DAFB HLE (MEMCjr integrated cell) — MAME dafb.cpp ──
// Map (dafb_base::map): +$000 main regs, +$100 Swatch, +$200 RAMDAC
// (Antelope = revised AC842a), +$300 clockgen (Gazelle, reads 0).
// Registers are ≤12 bits, accessed as u32; unknown offsets fall back
// to the raw echo file.

void Q605Memory::dafbRecalcIrq() {
    // write_irq -> primetime via2_irq_w<0x40>: nubus bit 6, active low
    vblIrq(dafbIntStatus_ != 0);
}

uint32_t Q605Memory::dafbRegRead(uint32_t off) {
    uint32_t full = dafbRegReadRaw(off);
    // MEMCjr DAFB bus-holding split (djmemc.cpp:149-165, dafb_holding_r):
    // the $F9800000-$F98001FF window ($000 main + $100 Swatch) is a 12-bit
    // port — a read returns only the low-6 bits and latches the high-6, which
    // the ROM reads back at $50F0E07C (memcjr_r $7C → `holding>>6`) to rebuild
    // the 12-bit value. We keep `dafbHolding_` in bits [11:6] throughout (the
    // memcjr_w/r + DAFB-write convention, djmemc.cpp:172/186/197); MAME's
    // dafb_holding_r stashes it unshifted, an internal asymmetry that would
    // make the high-half read-back return 0 — but this ROM DOES read the high
    // half back after a register read, so we store it shifted so memcjr_r's
    // `>>6` recovers it. Side-effect reads already ran in dafbRegReadRaw above.
    if ((off & 0x3FC) < 0x200) {
        dafbHolding_ = uint16_t(((full >> 6) & 0x3f) << 6);
        return full & 0x3f;
    }
    return full;
}

uint32_t Q605Memory::dafbRegReadRaw(uint32_t off) {
    switch (off & 0x3FC) {
        // main ($000): dafb_r
        case 0x00: return (dafbBase_ >> 9) & 0xFFF;
        case 0x04: return (dafbBase_ >> 5) & 0x0F;
        case 0x08: return dafbStride_ >> 2;       // stride in 32-bit words
        case 0x10: return dafbConfig_;
        case 0x1C:
            // inverse of monitor sense — 13" RGB 640×480 = plain code 6
            return 6u ^ 7u;
        case 0x24: return dafb_[0x24 >> 2];      // SCSI ctrl (DRQ=0 for now)
        case 0x28: return dafb_[0x28 >> 2];
        case 0x2C: return (dafb_[0x2C >> 2] & 0x1FF) | (3u << 9);  // version 3
        // Swatch ($100): swatch_r
        case 0x108: return dafbIntStatus_;
        case 0x10C: dafbIntStatus_ &= ~4u; dafbRecalcIrq(); return 0;
        case 0x114: dafbIntStatus_ &= ~1u; dafbRecalcIrq(); return 0;
        // RAMDAC ($200): ramdac_r (Antelope PCBR1 dance)
        case 0x200: palIdx_ = 0; return palAddress_;
        case 0x210: {
            uint8_t c = clut_[palAddress_][palIdx_ % 3];
            if (++palIdx_ == 3) palIdx_ = 0;
            return c;
        }
        case 0x220:
            if (palAddress_ == 1 && (ac842Pbctrl_ & 0x06) == 0x06)
                return pcbr1_;
            return ac842Pbctrl_;
        default:
            if ((off & 0x300) == 0x300) return 0;   // Gazelle clockgen
            return dafb_[(off >> 2) & 0xFF];
    }
}

void Q605Memory::dafbRegWrite(uint32_t off, uint32_t v) {
    // MEMCjr DAFB bus-holding split (djmemc.cpp:167-178, dafb_holding_w):
    // the $F9800000-$F98001FF window ($000 main + $100 Swatch) is a
    // 12-bit port whose write value is `(this write & 0x3f) | holding`,
    // the high-6 half having been latched by a prior $50F0E07C write;
    // the latch then clears. Outside that window (RAMDAC $200+) the DAFB
    // is written whole.
    if ((off & 0x3FC) < 0x200) {
        v = (v & 0x3f) | uint32_t(dafbHolding_);
        dafbHolding_ = 0;
        v &= 0xFFF;                              // 12-bit DAFB register port
    }
    uint32_t idx = (off >> 2) & 0xFF;
    dafb_[idx] = v;
    switch (off & 0x3FC) {
        case 0x00:
            dafbBase_ = (dafbBase_ & 0x1E0) | ((v & 0xFFF) << 9);
            break;
        case 0x04:
            dafbBase_ = (dafbBase_ & ~0x1E0u) | ((v & 0x0F) << 5);
            break;
        case 0x08:
            dafbStride_ = v << 2;                 // register is 32-bit words
            break;
        case 0x10:
            dafbConfig_ = uint16_t(v);
            break;
        case 0x104:                              // Swatch int enable
            swatchIntEnable_ = v;
            if (!(v & 1)) dafbIntStatus_ &= ~1u;
            if (!(v & 4)) dafbIntStatus_ &= ~4u;
            dafbRecalcIrq();
            break;
        case 0x10C: dafbIntStatus_ &= ~4u; dafbRecalcIrq(); break;
        case 0x114: dafbIntStatus_ &= ~1u; dafbRecalcIrq(); break;
        case 0x118: dafbCursorLine_ = v & 0xFFF; break;
        case 0x200: palAddress_ = uint8_t(v); palIdx_ = 0; break;
        case 0x210:
            clut_[palAddress_][palIdx_] = uint8_t(v);
            if (++palIdx_ == 3) { palIdx_ = 0; palAddress_++; }
            break;
        case 0x220:                              // Antelope PCBR0/PCBR1
            if (palAddress_ == 1 && (ac842Pbctrl_ & 0x06) == 0x06)
                pcbr1_ = uint8_t(v & 0xF0) | 0x02;   // Antelope version ID
            else {
                ac842Pbctrl_ = uint8_t(v);
                if ((pcbr1_ & 0xC0) == 0xC0 && (ac842Pbctrl_ & 0x06) == 0x06) {
                    dafbMode_ = 5;                // Antelope x555
                } else {
                    switch (ac842Pbctrl_ & 0x1C) {
                        case 0x00: dafbMode_ = 0; break; // 1 bpp
                        case 0x08: dafbMode_ = 1; break; // 2 bpp
                        case 0x10: dafbMode_ = 2; break; // 4 bpp
                        case 0x18: dafbMode_ = 3; break; // 8 bpp
                        case 0x1C: dafbMode_ = 4; break; // 24 bpp
                    }
                }
            }
            break;
        default: break;
    }
}

uint8_t Q605Memory::dafbRead8(uint32_t addr) {
    // Byte lanes of the u32 register; side-effectful reads (int clears,
    // pal index) repeat within one long access but are idempotent —
    // except the CLUT data register, served on the low lane only.
    if (onIoAccess) onIoAccess(0xF9800000 + (addr & 0x3FF), false, 0xFFFFFFFF);
    if ((addr & 0x3FC) == 0x210 && (addr & 3) != 3) return 0;
    uint32_t val = dafbRegRead(addr & ~3u);
    return uint8_t(val >> (8 * (3 - (addr & 3))));
}

void Q605Memory::dafbWrite8(uint32_t addr, uint8_t v) {
    // Registers are ≤12 bits: commit semantics once, on the low lane.
    if (onIoAccess) onIoAccess(0xF9800000 + (addr & 0x3FF), true, v);
    uint32_t idx = (addr >> 2) & 0xFF;
    int sh = 8 * (3 - (addr & 3));
    uint32_t merged = (dafb_[idx] & ~(0xFFu << sh)) | (uint32_t(v) << sh);
    if ((addr & 3) == 3) dafbRegWrite(addr & ~3u, merged);
    else                 dafb_[idx] = merged;
}

uint8_t Q605Memory::ioRead8(uint32_t addr) {
    uint32_t sub = addr & 0x0FFFFFFF;

    if (onIoAccess) onIoAccess(addr, false, 0xFFFFFFFF);   // pre-access probe log

    if (sub >= 0x0FFFFFFC)                       // machine ID (LC 475)
        return uint8_t(machineId_ >> (8 * (3 - (sub & 3))));

    uint32_t base = sub & 0x0003FFFF;            // pre-mirror window

    if (base < 0x02000) return viaAccess8(base, false, 0);
    if ((sub & ~0xF00000u) >= 0x02000 && (sub & ~0xF00000u) < 0x04000)
        return via2Access8(sub & 0x1FFF, false, 0);
    if (base >= 0x0C000 && base < 0x0E000) {     // SCC, byte on D8-15
        int ch = (base >> 1) & 1;
        uint8_t d = ((base >> 2) & 1) ? scc_.readData(ch) : scc_.readCtl(ch);
        sccIrqLine(scc_.irqAsserted());          // reading RR0 / data can clear
        return d;                                // or (re-)assert the level-4 line
    }
    if (base >= 0x0E000 && base < 0x10000) {     // MEMCjr regs
        // MAME memcjr_r (djmemc.cpp:181-190): every register reads back 0
        // except $7C, the DAFB bus-holding register, which returns the
        // latched high-6 bits (`m_dafb_holding >> 6`). The high-6 half of
        // a 12-bit DAFB register is transferred through this latch; the
        // low-6 half goes directly at $F9800000 (dafb_holding_r/w).
        uint32_t idx = (base & 0x7F) >> 2;
        if (idx == (0x7C >> 2)) {
            uint32_t hi = uint32_t(dafbHolding_) >> 6;   // 6 bits
            return uint8_t(hi >> (8 * (3 - (base & 3))));
        }
        return 0;
    }
    if (base >= 0x10000 && base < 0x10100) {     // TurboSCSI 53C96 regs — Q6
        // reg select = (addr>>4)&0xF (iosb.cpp:58-59 turboscsi_r reads
        // m_ncr->read(offset>>4)); absolute reg N at PrimeTime+$10000+N*$10.
        uint8_t d = scsi_.read((base >> 4) & 0xF);
        scsiPoll_();
        return d;
    }
    if (base >= 0x10100 && base < 0x10104)       // TurboSCSI pseudo-DMA — Q6
        return scsiDmaRead_();
    if ((sub & ~0xF00000u) >= 0x14000 && (sub & ~0xF00000u) < 0x15000)
        return asc_.read(addr & 0xFFF);          // PrimeTime/IOSB ASC ($BB)
    if ((sub & ~0xF00000u) >= 0x18000 && (sub & ~0xF00000u) < 0x1A000) {
        uint32_t reg = ((sub & 0x1FFF) >> 8) & 0x1F;
        uint32_t byteInWord = sub & 1;
        return uint8_t(iosbRegs_[reg] >> (8 * (1 - byteInWord)));
    }
    if ((sub & ~0xF00000u) >= 0x1A100 && (sub & ~0xF00000u) < 0x1A110)
        return 0;   // PrimeTime II ATA/status window — ROM probes $1A101

    if ((sub & ~0xF00000u) >= 0x1E000 && (sub & ~0xF00000u) < 0x20000) {
        if (cpu_) cpu_->stall(5);                 // iosb.cpp swim_r wait states
        syncSwimFromCpu();                        // MAME swim2::read → sync()
        return (addr & 1) ? 0 : swim_.read((sub >> 9) & 0x0F);
    }

    busError(addr, false);
}

void Q605Memory::ioWrite8(uint32_t addr, uint8_t v) {
    uint32_t sub = addr & 0x0FFFFFFF;

    if (onIoAccess) onIoAccess(addr, true, v);

    if (sub >= 0x0FFFFFFC) return;               // ID is read-only

    uint32_t base = sub & 0x0003FFFF;

    if (base < 0x02000) { viaAccess8(base, true, v); return; }
    if ((sub & ~0xF00000u) >= 0x02000 && (sub & ~0xF00000u) < 0x04000) {
        via2Access8(sub & 0x1FFF, true, v);
        return;
    }
    if (base >= 0x0C000 && base < 0x0E000) {
        int ch = (base >> 1) & 1;
        if ((base >> 2) & 1) scc_.writeData(ch, v);
        else scc_.writeCtl(ch, v);
        sccIrqLine(scc_.irqAsserted());          // WR1/WR9/WR15 + Reset Ext/Status
        return;                                  // change the pending interrupt
    }
    if (base >= 0x0E000 && base < 0x10000) {
        uint32_t idx = (base & 0x7F) >> 2;
        int sh = 8 * (3 - (base & 3));
        memcjr_[idx] = (memcjr_[idx] & ~(0xFFu << sh)) | (uint32_t(v) << sh);
        // MAME memcjr_w (djmemc.cpp:192-199): a write to $7C latches the
        // high-6 half of the next DAFB register write —
        // `m_dafb_holding = (data & 0x3f) << 6`. The 12-bit DAFB write at
        // $F9800000 then ORs this latch in (dafb_holding_w). Sample the
        // full u32 written to $7C on its low lane ($7F) so all 4 bytes
        // have landed in memcjr_[].
        if (idx == (0x7C >> 2) && (base & 3) == 3)
            dafbHolding_ = uint16_t((memcjr_[idx] & 0x3f) << 6);
        return;
    }
    if (base >= 0x10000 && base < 0x10100) {     // TurboSCSI 53C96 regs — Q6
        scsi_.write((base >> 4) & 0xF, v);
        scsiPoll_();
        return;
    }
    if (base >= 0x10100 && base < 0x10104) {     // TurboSCSI pseudo-DMA — Q6
        scsiDmaWrite_(v);
        return;
    }
    if ((sub & ~0xF00000u) >= 0x14000 && (sub & ~0xF00000u) < 0x15000) {
        asc_.write(addr & 0xFFF, v);                 // PrimeTime/IOSB ASC
        return;
    }
    if ((sub & ~0xF00000u) >= 0x18000 && (sub & ~0xF00000u) < 0x1A000) {
        uint32_t reg = ((sub & 0x1FFF) >> 8) & 0x1F;
        if (sub & 1) iosbRegs_[reg] = uint16_t((iosbRegs_[reg] & 0xFF00) | v);
        else         iosbRegs_[reg] = uint16_t((iosbRegs_[reg] & 0x00FF) | (v << 8));
        return;
    }
    if ((sub & ~0xF00000u) >= 0x1E000 && (sub & ~0xF00000u) < 0x20000) {
        if (cpu_) cpu_->stall(5);                 // iosb.cpp swim_w wait states
        syncSwimFromCpu();                        // MAME swim2::write → sync()
        swim_.write((sub >> 9) & 0x0F, v);
        return;
    }

    busError(addr, true);
}

// ── TurboSCSI pseudo-DMA (PrimeTime + $10100) ──
// On a CPU access to the DMA window the PrimeTime/IOSB holds off completion
// while !DRQ (MAME iosb.cpp:498-591 turboscsi_dma_r/w spin on the DRQ line).
// Functionally we mirror the LC II V8 path: no data available (!drq) raises
// /BERR, which the SCSI Manager's blind-transfer loops catch to terminate;
// with a proper transfer count the 53C96 keeps DRQ asserted until the
// payload is drained. macquadra605.cpp:206 drq_handler -> primetime
// scsi_drq_w -> via2.
uint8_t Q605Memory::scsiDmaRead_() {
    if (!scsi_.drq()) busError(0x50010100, false);
    uint8_t d = scsi_.dmaRead();
    scsiPoll_();
    return d;
}

void Q605Memory::scsiDmaWrite_(uint8_t v) {
    if (!scsi_.drq()) busError(0x50010100, true);
    scsi_.dmaWrite(v);
    scsiPoll_();
}

// Level-sensitive IRQ/DRQ from the 53C96 into the Quadra pseudo-VIA2:
// macquadra605.cpp:204-206 wires ncr1->irq_handler_cb -> primetime
// scsi_irq_w -> via2 (pseudovia.cpp:148); IntStatus-read clears the IRQ.
void Q605Memory::scsiPoll_() {
    scsiIrq(scsi_.irq());
}

uint8_t Q605Memory::read8(uint32_t addr) {
    if (addr < 0x40000000) {
        if (overlay_) return rom_[addr & (kRomSize - 1)];
        if (addr < totalRam_) return ram_[addr];
        return 0xFF;        // open bus above the installed bank(s): the
                            // sizer must find NO alias there or it
                            // invents phantom banks (5 x mirror) whose
                            // burn-in self-corrupts
    }
    if (addr < 0x50000000) {                     // ROM window
        if (overlay_) overlay_ = false;          // djmemc rom_switch_r
        return rom_[addr & (kRomSize - 1)];
    }
    if (addr < 0x60000000) return ioRead8(addr);
    if (addr >= 0xF9000000 && addr < 0xF9000000 + kVramSize)
        return vram_[addr - 0xF9000000];
    if (addr >= 0xF9800000 && addr < 0xF9800400)
        return dafbRead8(addr - 0xF9800000);
    busError(addr, false);
}

uint16_t Q605Memory::read16(uint32_t addr) {
    // RAM/ROM fast paths (aligned by Moira's splitting)
    if (addr < 0x40000000) {
        if (overlay_) {
            uint32_t o = addr & (kRomSize - 1);
            return uint16_t(rom_[o & (kRomSize - 1)] << 8 | rom_[(o + 1) & (kRomSize - 1)]);
        }
        if (addr + 1 < totalRam_)
            return uint16_t(ram_[addr] << 8 | ram_[addr + 1]);
        return 0xFFFF;
    }
    if (addr < 0x50000000) {
        if (overlay_) overlay_ = false;
        uint32_t o = addr & (kRomSize - 1);
        return uint16_t(rom_[o & (kRomSize - 1)] << 8 | rom_[(o + 1) & (kRomSize - 1)]);
    }
    if (addr >= 0xF9000000 && addr + 1 < 0xF9000000 + kVramSize) {
        uint32_t o = addr - 0xF9000000;
        return uint16_t(vram_[o] << 8 | vram_[o + 1]);
    }
    if (addr >= 0x50000000 && addr < 0x60000000) {
        uint32_t swimOff = (addr & 0x0FFFFFFF) & ~0xF00000u;
        if (swimOff >= 0x1E000 && swimOff < 0x20000)
            return uint16_t(read8(addr) << 8);    // IOSB result on D15-D8
    }
    return uint16_t(read8(addr) << 8 | read8(addr + 1));
}

void Q605Memory::write8(uint32_t addr, uint8_t v) {
    if (addr < 0x40000000) {
        if (overlay_) return;                    // ROM mirror: writes drop
        if (ramWatch_ && onRamWrite && addr == ramWatch_) onRamWrite(addr, 1, v);
        if (addr < totalRam_) ram_[addr] = v;
        return;
    }
    if (addr < 0x50000000) return;               // ROM window (nopw)
    if (addr < 0x60000000) { ioWrite8(addr, v); return; }
    if (addr >= 0xF9000000 && addr < 0xF9000000 + kVramSize) {
        vram_[addr - 0xF9000000] = v;
        return;
    }
    if (addr >= 0xF9800000 && addr < 0xF9800400) {
        dafbWrite8(addr - 0xF9800000, v);
        return;
    }
    busError(addr, true);
}

void Q605Memory::write16(uint32_t addr, uint16_t v) {
    if (addr < 0x40000000 && !overlay_) {
        if (ramWatch_ && onRamWrite && (addr == ramWatch_ || addr + 1 == ramWatch_))
            onRamWrite(addr, 2, v);
        if (addr + 1 < totalRam_) {
            ram_[addr] = uint8_t(v >> 8);
            ram_[addr + 1] = uint8_t(v);
        }
        return;
    }
    if (addr >= 0xF9000000 && addr + 1 < 0xF9000000 + kVramSize) {
        uint32_t o = addr - 0xF9000000;
        vram_[o] = uint8_t(v >> 8);
        vram_[o + 1] = uint8_t(v);
        return;
    }
    if (addr >= 0x50000000 && addr < 0x60000000) {
        uint32_t swimOff = (addr & 0x0FFFFFFF) & ~0xF00000u;
        if (swimOff >= 0x1E000 && swimOff < 0x20000) {
            write8(addr + 1, uint8_t(v));         // IOSB accepts low byte
            return;
        }
    }
    write8(addr, uint8_t(v >> 8));
    write8(addr + 1, uint8_t(v));
}

uint8_t Q605Memory::peek8(uint32_t addr) const {
    if (addr < 0x40000000) {
        if (overlay_) return rom_[addr & (kRomSize - 1)];
        if (addr < totalRam_) return ram_[addr];
        return 0xFF;
    }
    if (addr < 0x50000000) return rom_[addr & (kRomSize - 1)];
    if (addr >= 0xF9000000 && addr < 0xF9000000 + kVramSize)
        return vram_[addr - 0xF9000000];
    return 0xFF;
}

void Q605Memory::tick(int cpuCycles) {
    // VIA1 φ2 = 783.36 kHz — CPU/32 approximation at 25 MHz
    viaPhase_ += cpuCycles;
    int viaCycles = viaPhase_ / 32;
    viaPhase_ %= 32;
    if (viaCycles && via1_.tick(viaCycles)) updateIrq();

    cuda_.tick(cpuCycles);

    // SCC open-line Break/Abort stream (Q6.6): on a machine with no LocalTalk
    // peer the SDLC receiver keeps detecting aborts, so the level-4 ext/status
    // interrupt must re-assert periodically — this is the carrier-sense signal
    // OS 8.1's .MPP LAP sleeps on. A de-asserted SCC must also lower the line.
    scc_.tick(cpuCycles);
    if (sccIrq_ != scc_.irqAsserted()) { sccIrq_ = scc_.irqAsserted(); updateIrq(); }

    // IOSB ASC and SWIM2 run on C15M (15.6672 MHz),
    // independent of the 25 MHz CPU — convert cpuCycles into ASC-clock ticks
    // via a fractional accumulator so the 22 257 Hz sample rate stays exact.
    ascCycAcc_ += int64_t(cpuCycles) * AscIosb::kCpuHz;
    int ascCyc = int(ascCycAcc_ / kCpuHz);
    ascCycAcc_ -= int64_t(ascCyc) * kCpuHz;
    asc_.tick(ascCyc);
    syncSwimFromCpu();
    drive0_.tick(cpuCycles);
    drive1_.tick(cpuCycles);

    // SCSI bus-service latency countdown (Q6.5b) → reflect the deferred IRQ
    // into the pseudo-VIA2 line when it lands.
    if (scsi_.irq() != ((pvIfr_ & 0x08) != 0)) scsiPoll_();
    scsi_.tick(cpuCycles);
    if (scsi_.irq() != ((pvIfr_ & 0x08) != 0)) scsiPoll_();

    // 60.15 Hz CA1 tick (iosb 6015_timer)
    tickAcc_ += int64_t(cpuCycles) * 6015;
    if (tickAcc_ >= kCpuHz * 100) {
        tickAcc_ -= kCpuHz * 100;
        via1_.raiseCa1();
        updateIrq();
    }

    // DAFB Swatch — 640×480@60 as a 525-line frame; the VBL timer fires
    // at line 480, the cursor timer at the programmed line, each frame,
    // when enabled by Swatch $104 (MAME dafb.cpp vbl_tick/cursor_tick).
    framePos_ += cpuCycles;
    int64_t frameLen = kCpuHz / 60;
    if (framePos_ >= frameLen) framePos_ -= frameLen;
    int line = int(framePos_ * 525 / frameLen);
    if (line != prevLine_) {
        bool wrap = line < prevLine_;
        auto crossed = [&](int target) {
            return wrap ? (target > prevLine_ || target <= line)
                        : (target > prevLine_ && target <= line);
        };
        uint8_t st = dafbIntStatus_;
        if ((swatchIntEnable_ & 1) && crossed(480)) st |= 1;
        if ((swatchIntEnable_ & 4) && crossed(int(dafbCursorLine_ % 525)))
            st |= 4;
        prevLine_ = line;
        if (st != dafbIntStatus_) { dafbIntStatus_ = st; dafbRecalcIrq(); }
    }
}

// Q6.6 — HLE LocalTalk-LAP unwedge (Quadra / Mac OS 8.1 .MPP). See the header
