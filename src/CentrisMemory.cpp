// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "CentrisMemory.h"
#include "CentrisCpu.h"
#include "Moira.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

CentrisMemory::CentrisMemory(uint32_t totalRam, int64_t cpuHz, uint8_t modelPins)
    : totalRam_(totalRam), cpuHz_(cpuHz), modelPins_(modelPins),
      // djMEMC = dafb_memc_device: DP8534 clock chip (djmemc.cpp, dafb.cpp:1197)
      dafbCell_(cpuHz, Dafb::Clockgen::Dp8534) {
    while (totalRam_ & (totalRam_ - 1)) totalRam_ &= totalRam_ - 1;   // pow2
    ram_.assign(totalRam_, 0);
    rom_.assign(kRomSize, 0xFF);
    vram_.assign(kVramSize, 0);
    adbVia_.attach(via1_, adb_, cpuHz_);
    dafbCell_.onIrq = [this](bool s) { vblIrq(s); };
    asc_.onIrq = [this](bool s) { ascIrq(s); };
    drive0_.setSuperDrive(true);
    drive1_.setSuperDrive(true);
    drive0_.setSpinClockHz(cpuHz_);
    drive1_.setSpinClockHz(cpuHz_);
    swim_.attachDrive(&drive0_, &drive1_);
    rtc_.factoryDefaults();
    // 32-bit clean OS (Mac OS 7.x/8) — XPRAM $8A |= $05 (the Q605 seed).
    rtc_.setXpram(0x8A, uint8_t(rtc_.xpram(0x8A) | 0x05));
    {
        const char* e = std::getenv("POM68K_SCSI_LAT");
        scsi_.setLatency(e ? std::atoi(e) : -1);
    }
}

bool CentrisMemory::loadRom(const std::vector<uint8_t>& data) {
    if (data.size() != kRomSize) return false;
    std::memcpy(rom_.data(), data.data(), kRomSize);
    jitMapChanged();
    return true;
}

// The file I/O moved onto the chip that owns the PRAM (Rtc.cpp, the
// Egret::loadPram precedent) when the compacts / Mac II / IIfx gained the
// same persistence — same flat 256-byte format, so `.pram` files written
// by earlier builds still load.
bool CentrisMemory::loadPram(const std::string& path) {
    return rtc_.loadPram(path);
}

void CentrisMemory::savePram(const std::string& path) {
    rtc_.savePram(path);
}

void CentrisMemory::reset() {
    overlay_ = true;
    jitMapChanged();
    pvIfr_ = pvIer_ = pvPortB_ = 0;
    nubusIrqs_ = 0xFF;
    std::memset(memcjr_, 0, sizeof memcjr_);
    std::memset(iosbRegs_, 0, sizeof iosbRegs_);
    dafbHolding_ = 0;
    dafbCell_.reset();
    via1_.reset();
    rtc_.reset();
    adbVia_.reset();
    scc_.reset();
    scsi_.reset();
    sccDebt_ = scsiDebt_ = 0;
    asc_.reset();
    swim_.reset();
    swim_.attachDrive(&drive0_, &drive1_);
    drive0_.reset();
    drive1_.reset();
    ascLine_ = false;
    scsiReadCycles_ = scsiWriteCycles_ = 3;
    scsiDmaReadCycles_ = scsiDmaWriteCycles_ = 3;
    ascCycAcc_ = 0;
    swimLastCpu_ = -1;
    swimCycAcc_ = 0;
    scc_.setClocks(cpuHz_, 7833600);
    scc_.setCtsHigh(false);
    scc_.setAbortIdle(true);       // no hardwired LocalTalk peer — the
                                   // standing abort is a line state, not
                                   // machine config (Scc8530::openLine,
                                   // LLE steps 7+8: virgin line = clean)
    viaEClock_ = {};
    tickAcc_ = 0;
    secAcc_ = 0;
    sccIrq_ = false;
    // VIA1 port A: the model ID pins (iosb_base::via_in_a). Bit 5 (hdsel)
    // is a CPU output; the rest read the strapped ID.
    via1_.setInA(modelPins_);
    refreshVia1PortB();
}

void CentrisMemory::busError(uint32_t addr, bool write) const {
    if (onBusError) onBusError(addr, write);
    if (cpu_) cpu_->extBusError040();
    throw moira::MmuBusError{};
}

int CentrisMemory::iplLevel() const {
    if (sccIrq_) return 4;
    if (via2IrqAsserted()) return 2;
    if (via1_.irqAsserted()) return 1;
    return 0;
}

void CentrisMemory::updateIrq() {
    if (cpu_) cpu_->updateIpl();
}

void CentrisMemory::via2Recalc() {
    if (ascLine_) pvIfr_ |= 0x10;
    if (via2IrqAsserted()) pvIfr_ |= 0x80; else pvIfr_ &= ~0x80;
    updateIrq();
}

void CentrisMemory::vblIrq(bool s) {
    if (s) nubusIrqs_ &= ~0x40; else nubusIrqs_ |= 0x40;
    if ((nubusIrqs_ & 0x79) != 0x79) pvIfr_ |= 0x02;
    else                             pvIfr_ &= ~0x02;
    via2Recalc();
}

void CentrisMemory::scsiIrq(bool s) {
    if (s) pvIfr_ |= 0x08; else pvIfr_ &= ~0x08;
    via2Recalc();
}

void CentrisMemory::ascIrq(bool s) {
    ascLine_ = s;
    if (s) pvIfr_ |= 0x10; else pvIfr_ &= ~0x10;
    via2Recalc();
}

void CentrisMemory::syncSwimFromCpu() {
    if (!cpu_) return;
    const int64_t now = int64_t(cpu_->getClock());
    if (swimLastCpu_ < 0) { swimLastCpu_ = now; return; }
    const int64_t delta = now - swimLastCpu_;
    if (delta <= 0) return;
    swimLastCpu_ = now;
    const int boost = std::max(1, cpu_->cacheBoost());
    swimCycAcc_ += (delta * AscIosb::kCpuHz) / boost;
    const int cyc = int(swimCycAcc_ / cpuHz_);
    swimCycAcc_ -= int64_t(cyc) * cpuHz_;
    if (cyc) swim_.tick(cyc);
}

void CentrisMemory::viaSync() {
    if (!cpu_) return;
    const int boost = std::max(1, cpu_->cacheBoost());
    int64_t c = int64_t(cpu_->getClock()) / boost;
    const int64_t target = via_eclock::syncTarget(c, cpuHz_);
    if (target > c) cpu_->stall(int(target - c));
}

// VIA1 port B input: PB0 = RTC serial data, PB3 = /ADB IRQ (active low),
// the rest pulled up (MAME macii/iosb via_in_b).
void CentrisMemory::refreshVia1PortB() {
    uint8_t in = rtc_.dataBit() & 1;
    in |= 0xC6;
    if (!adbVia_.irqPending()) in |= 0x08;
    via1_.setInB(in);
}

uint8_t CentrisMemory::viaAccess8(uint32_t addr, bool write, uint8_t v) {
    if (cpu_) cpu_->flushTicks();
    viaSync();
    // Run the PIC1654S up to this exact cycle before the ROM touches VIA1.
    if (cpu_) adbVia_.syncTo(cpu_->machineClock());
    int reg = (addr >> 9) & 0x0F;
    if (write) {
        via1_.write(reg, v);
        // PA5 = floppy head-select (iosb via_out_a hdsel); SWIM2 tracks the
        // side through its own soft-select, so no extra wiring is needed to
        // reach the Finder off SCSI.
        if (reg == Via6522::ORB || reg == Via6522::DDRB || reg == Via6522::SR
            || reg == Via6522::ACR) {
            adbVia_.sync();
            // RTC on PB0-2: /enable = PB2 (active low), clock = PB1, data = PB0.
            rtc_.setLines(!(via1_.portB() & 0x04),
                          (via1_.portB() & 0x02) != 0,
                          (via1_.portB() & 0x01) != 0);
            refreshVia1PortB();
        }
        updateIrq();
        return 0;
    }
    if (reg == Via6522::ORB) refreshVia1PortB();
    uint8_t d = via1_.read(reg);
    updateIrq();
    return d;
}

uint8_t CentrisMemory::via2Access8(uint32_t addr, bool write, uint8_t v) {
    if (cpu_) cpu_->flushTicks();
    flushScsi();
    viaSync();
    int reg = (addr >> 9) & 0x0F;
    if (write) {
        switch (reg) {
            case 0:  pvPortB_ = v; break;
            case 1:  break;
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
        case 13: return (pvIfr_ & ~0x01) | (scsi_.drq() ? 0x01 : 0);
        case 14: return pvIer_;
        default: return 0;
    }
}

uint32_t CentrisMemory::dafbRegRead(uint32_t off) {
    uint32_t full = dafbCell_.read32(off);
    if ((off & 0x3FC) < 0x200) {
        dafbHolding_ = uint16_t(((full >> 6) & 0x3f) << 6);
        return full & 0x3f;
    }
    return full;
}

void CentrisMemory::dafbRegWrite(uint32_t off, uint32_t v) {
    if ((off & 0x3FC) < 0x200) {
        v = (v & 0x3f) | uint32_t(dafbHolding_);
        dafbHolding_ = 0;
        v &= 0xFFF;
    }
    dafbCell_.write32(off, v);
}

uint8_t CentrisMemory::dafbRead8(uint32_t addr) {
    if (onIoAccess) onIoAccess(0xF9800000 + (addr & 0x3FF), false, 0xFFFFFFFF);
    if ((addr & 0x3FC) == 0x210 && (addr & 3) != 3) return 0;
    uint32_t val = dafbRegRead(addr & ~3u);
    return uint8_t(val >> (8 * (3 - (addr & 3))));
}

void CentrisMemory::dafbWrite8(uint32_t addr, uint8_t v) {
    if (onIoAccess) onIoAccess(0xF9800000 + (addr & 0x3FF), true, v);
    if ((addr & 0x300) == 0x300) {
        dafbCell_.clockgenWrite8(addr & 0x3FF, v);
        return;
    }
    int sh = 8 * (3 - (addr & 3));
    uint32_t merged = (dafbCell_.rawReg(addr) & ~(0xFFu << sh))
                    | (uint32_t(v) << sh);
    if ((addr & 3) == 3) dafbRegWrite(addr & ~3u, merged);
    else                 dafbCell_.setRawReg(addr, merged);
}

uint8_t CentrisMemory::ioRead8(uint32_t addr) {
    uint32_t sub = addr & 0x0FFFFFFF;
    if (onIoAccess) onIoAccess(addr, false, 0xFFFFFFFF);

    if (sub >= 0x0FFF0000)                        // machine ID = $A55A2BAD
        return uint8_t(kBoxId >> (8 * (3 - (sub & 3))));

    uint32_t base = sub & 0x0003FFFF;

    if (base < 0x02000) return viaAccess8(base, false, 0);
    if ((sub & ~0xF00000u) >= 0x02000 && (sub & ~0xF00000u) < 0x04000)
        return via2Access8(sub & 0x1FFF, false, 0);
    // Ethernet address ROM (macquadra800.cpp ethernet_mac_r): six MAC bytes
    // at +0..5, and at +7 the XOR of all six inverted — the ROM's SONIC
    // presence/sanity probe. Present on every model of this board; the SONIC
    // registers themselves ($5000A000) stay unmapped-0, so no driver binds.
    if (base >= 0x08000 && base < 0x08008) {
        int off = int(base & 7);
        if (off < 6) return kMacAddr[off];
        if (off == 7) {
            uint8_t x = 0;
            for (uint8_t b : kMacAddr) x ^= b;
            return uint8_t(x ^ 0xFF);
        }
        return 0;
    }
    if (base >= 0x0C000 && base < 0x0E000) {      // SCC
        flushScc();
        int ch = (base >> 1) & 1;
        uint8_t d = ((base >> 2) & 1) ? scc_.readData(ch) : scc_.readCtl(ch);
        sccIrqLine(scc_.irqAsserted());
        return d;
    }
    if (base >= 0x0E000 && base < 0x10000) {      // djMEMC regs
        uint32_t idx = (base & 0x7F) >> 2;
        if (idx == (0x7C >> 2)) {
            uint32_t hi = uint32_t(dafbHolding_) >> 6;
            return uint8_t(hi >> (8 * (3 - (base & 3))));
        }
        return 0;
    }
    if (base >= 0x10000 && base < 0x10100) {      // TurboSCSI 53C96
        flushScsi();
        if (cpu_) cpu_->stall(scsiReadCycles_);
        uint8_t d = scsi_.read((base >> 4) & 0xF);
        scsiPoll_();
        return d;
    }
    if (base >= 0x10100 && base < 0x10104) {      // TurboSCSI pseudo-DMA
        if (cpu_ && ((sub >> 19) & 1)) cpu_->stall(scsiDmaReadCycles_);
        return scsiDmaRead_();
    }
    if ((sub & ~0xF00000u) >= 0x14000 && (sub & ~0xF00000u) < 0x15000)
        return asc_.read(addr & 0xFFF);
    if ((sub & ~0xF00000u) >= 0x18000 && (sub & ~0xF00000u) < 0x1A000) {
        uint32_t reg = ((sub & 0x1FFF) >> 8) & 0x1F;
        uint32_t byteInWord = sub & 1;
        return uint8_t(iosbRegs_[reg] >> (8 * (1 - byteInWord)));
    }
    if ((sub & ~0xF00000u) >= 0x1E000 && (sub & ~0xF00000u) < 0x20000) {
        if (cpu_) cpu_->stall(5);
        syncSwimFromCpu();
        return (addr & 1) ? 0 : swim_.read((sub >> 9) & 0x0F);
    }
    return 0x00;                                  // unmapped I/O (incl. SONIC)
}

void CentrisMemory::ioWrite8(uint32_t addr, uint8_t v) {
    uint32_t sub = addr & 0x0FFFFFFF;
    if (onIoAccess) onIoAccess(addr, true, v);
    if (sub >= 0x0FFF0000) return;

    uint32_t base = sub & 0x0003FFFF;

    if (base < 0x02000) { viaAccess8(base, true, v); return; }
    if ((sub & ~0xF00000u) >= 0x02000 && (sub & ~0xF00000u) < 0x04000) {
        via2Access8(sub & 0x1FFF, true, v);
        return;
    }
    if (base >= 0x0C000 && base < 0x0E000) {
        flushScc();
        int ch = (base >> 1) & 1;
        if ((base >> 2) & 1) scc_.writeData(ch, v);
        else scc_.writeCtl(ch, v);
        sccIrqLine(scc_.irqAsserted());
        return;
    }
    if (base >= 0x0E000 && base < 0x10000) {
        uint32_t idx = (base & 0x7F) >> 2;
        int sh = 8 * (3 - (base & 3));
        memcjr_[idx] = (memcjr_[idx] & ~(0xFFu << sh)) | (uint32_t(v) << sh);
        if (idx == (0x7C >> 2) && (base & 3) == 3)
            dafbHolding_ = uint16_t((memcjr_[idx] & 0x3f) << 6);
        return;
    }
    if (base >= 0x10000 && base < 0x10100) {
        flushScsi();
        if (cpu_) cpu_->stall(scsiWriteCycles_);
        scsi_.write((base >> 4) & 0xF, v);
        scsiPoll_();
        return;
    }
    if (base >= 0x10100 && base < 0x10104) {
        if (cpu_ && ((sub >> 19) & 1)) cpu_->stall(scsiDmaWriteCycles_);
        scsiDmaWrite_(v);
        return;
    }
    if ((sub & ~0xF00000u) >= 0x14000 && (sub & ~0xF00000u) < 0x15000) {
        asc_.write(addr & 0xFFF, v);
        return;
    }
    if ((sub & ~0xF00000u) >= 0x18000 && (sub & ~0xF00000u) < 0x1A000) {
        uint32_t reg = ((sub & 0x1FFF) >> 8) & 0x1F;
        if (sub & 1) iosbRegs_[reg] = uint16_t((iosbRegs_[reg] & 0xFF00) | v);
        else         iosbRegs_[reg] = uint16_t((iosbRegs_[reg] & 0x00FF) | (v << 8));
        if (reg == 2) {
            static constexpr int times[4] = { 5, 5, 4, 3 };
            scsiDmaReadCycles_  = times[(iosbRegs_[2] >> 8) & 3];
            scsiDmaWriteCycles_ = times[(iosbRegs_[2] >> 11) & 3];
        }
        return;
    }
    if ((sub & ~0xF00000u) >= 0x1E000 && (sub & ~0xF00000u) < 0x20000) {
        if (cpu_) cpu_->stall(5);
        syncSwimFromCpu();
        swim_.write((sub >> 9) & 0x0F, v);
        return;
    }
    return;
}

uint8_t CentrisMemory::scsiDmaRead_() {
    flushScsi();
    if (!scsi_.drq()) busError(0x50010100, false);
    uint8_t d = scsi_.dmaRead();
    scsiPoll_();
    return d;
}

void CentrisMemory::scsiDmaWrite_(uint8_t v) {
    flushScsi();
    if (!scsi_.drq()) busError(0x50010100, true);
    scsi_.dmaWrite(v);
    scsiPoll_();
}

void CentrisMemory::scsiPoll_() {
    scsiIrq(scsi_.irq());
}

// POM68K JIT: the address map itself moved (overlay flip, ROM reload).
// No byte was written, so the write guard cannot see it — say so directly.
void CentrisMemory::jitMapChanged() {
    if (jitGuard_) jitGuard_->invalidate();
    // J3: the interpreter's data window bypasses this map entirely, so a
    // remap the MMU cannot see (the boot overlay flip writes no ATC) has
    // to reach the CPU's DTLB directly — the guard above is only serviced
    // by the JIT engine's own loop, which the interpreter never enters.
    if (cpu_) cpu_->pomJitDtlbFlush();
}

const uint8_t* CentrisMemory::codeSpan(uint32_t phys, uint32_t& len) const {
    // Exactly two regions may be handed out, and only with the overlay down.
    len = 0;
    if (phys < 0x40000000) {
        // Under the overlay this gigabyte is the ROM mirrored modulo 1 MB,
        // and the mapping flips out from under the caller the moment
        // read8/read16 touches $40000000. Refuse until it has flipped: the
        // boot ROM then runs interpreted, which costs about a second once.
        if (overlay_) return nullptr;
        // RAM is FLAT here, not mirrored — above the bank read8 returns
        // open bus 0xFF, which is not memory and must never be a span.
        if (phys >= totalRam_) return nullptr;
        len = totalRam_ - phys;
        return ram_.data() + phys;
    }
    if (phys < 0x50000000) {
        // ROM window. Reading it clears the overlay (read8), a side effect
        // a const probe cannot perform — so refuse while it is still set.
        if (overlay_) return nullptr;
        const uint32_t o = phys & (kRomSize - 1);
        len = kRomSize - o;              // stop at the 1 MB mirror seam
        return rom_.data() + o;
    }
    // $50000000-$5FFFFFFF I/O (VIA IFR clears, SCC RR0, 53C96 registers and
    // pseudo-DMA pops that can throw), $F9xxxxxx VRAM and the video cell
    // registers (reads clear interrupts and auto-increment the RAMDAC), and
    // every unmapped address (those bus-error). None of them is code.
    return nullptr;
}

uint8_t* CentrisMemory::dataSpan(uint32_t phys, uint32_t& len, bool write) {
    len = 0;
    if (phys < 0x40000000) {
        if (overlay_) return nullptr;          // this gigabyte is ROM for now
        if (phys >= totalRam_) return nullptr; // open bus, not memory
        len = totalRam_ - phys;
        return ram_.data() + phys;
    }
    if (phys < 0x50000000) {
        // The ROM window: readable, and a store there is dropped by the
        // hardware rather than stored, which an inline store cannot model.
        if (write || overlay_) return nullptr;
        const uint32_t o = phys & (kRomSize - 1);
        len = kRomSize - o;
        return rom_.data() + o;
    }
    // The framebuffer. QuickDraw drawing a 640x480x8 desktop is a very
    // large number of stores, and leaving them on the slow path made the
    // code generator SLOWER than the interpreter for the whole Finder
    // phase — each store paid a TLB probe, a remembered refusal and a call,
    // to reach what is a plain array write. read8/read16/write8/write16 all
    // treat this window as bytes and nothing else: no latch, no
    // auto-increment, no dirty tracking. (The video CELL registers at
    // $F98000xx are a different window and stay out.)
    //
    // The offset is taken modulo the framebuffer size and the span stops at
    // that seam, because two of the four maps mirror this window.
    if (phys >= 0xF9000000 && phys < 0xF9000000 + kVramSize) {
        const uint32_t o = (phys - 0xF9000000) & (kVramSize - 1);
        len = kVramSize - o;
        return vram_.data() + o;
    }

    // I/O and the video cell registers stay on the slow path: reads there
    // latch and auto-increment.
    return nullptr;
}

uint8_t CentrisMemory::read8(uint32_t addr) {
    if (addr < 0x40000000) {
        if (overlay_) return rom_[addr & (kRomSize - 1)];
        if (addr < totalRam_) return ram_[addr];
        return 0xFF;
    }
    if (addr < 0x50000000) {
        if (overlay_) { overlay_ = false; jitMapChanged(); }
        return rom_[addr & (kRomSize - 1)];
    }
    if (addr < 0x60000000) return ioRead8(addr);
    // djMEMC maps a 2 MB VRAM window ($F9000000-$F91FFFFF, djmemc.cpp:30);
    // POM68K models 1 MB, mirrored across it so the ROM's VRAM sizer sees
    // the real aliasing instead of a bus error at the top of the window.
    if (addr >= 0xF9000000 && addr < 0xF9200000)
        return vram_[(addr - 0xF9000000) & (kVramSize - 1)];
    if (addr >= 0xF9800000 && addr < 0xF9800400)
        return dafbRead8(addr - 0xF9800000);
    busError(addr, false);
}

uint16_t CentrisMemory::read16(uint32_t addr) {
    if (addr < 0x40000000) {
        if (overlay_) {
            uint32_t o = addr & (kRomSize - 1);
            return uint16_t(rom_[o] << 8 | rom_[(o + 1) & (kRomSize - 1)]);
        }
        if (addr + 1 < totalRam_)
            return uint16_t(ram_[addr] << 8 | ram_[addr + 1]);
        return 0xFFFF;
    }
    if (addr < 0x50000000) {
        if (overlay_) { overlay_ = false; jitMapChanged(); }
        uint32_t o = addr & (kRomSize - 1);
        return uint16_t(rom_[o] << 8 | rom_[(o + 1) & (kRomSize - 1)]);
    }
    if (addr >= 0xF9000000 && addr < 0xF9200000 - 1) {
        uint32_t o = (addr - 0xF9000000) & (kVramSize - 1);
        return uint16_t(vram_[o] << 8 | vram_[(o + 1) & (kVramSize - 1)]);
    }
    if (addr >= 0x50000000 && addr < 0x60000000) {
        uint32_t swimOff = (addr & 0x0FFFFFFF) & ~0xF00000u;
        if (swimOff >= 0x1E000 && swimOff < 0x20000)
            return uint16_t(read8(addr) << 8);
    }
    return uint16_t(read8(addr) << 8 | read8(addr + 1));
}

void CentrisMemory::write8(uint32_t addr, uint8_t v) {
    if (addr < 0x40000000) {
        if (overlay_) return;
        if (jitGuard_) jitGuard_->note(addr, 1);
        if (addr < totalRam_) ram_[addr] = v;
        return;
    }
    if (addr < 0x50000000) return;
    if (addr < 0x60000000) { ioWrite8(addr, v); return; }
    if (addr >= 0xF9000000 && addr < 0xF9200000) {
        vram_[(addr - 0xF9000000) & (kVramSize - 1)] = v;
        return;
    }
    if (addr >= 0xF9800000 && addr < 0xF9800400) {
        dafbWrite8(addr - 0xF9800000, v);
        return;
    }
    busError(addr, true);
}

void CentrisMemory::write16(uint32_t addr, uint16_t v) {
    if (addr < 0x40000000 && !overlay_) {
        if (addr + 1 < totalRam_) {
            if (jitGuard_) jitGuard_->note(addr, 2);
            ram_[addr] = uint8_t(v >> 8);
            ram_[addr + 1] = uint8_t(v);
        }
        return;
    }
    if (addr >= 0xF9000000 && addr < 0xF9200000 - 1) {
        uint32_t o = (addr - 0xF9000000) & (kVramSize - 1);
        vram_[o] = uint8_t(v >> 8);
        vram_[(o + 1) & (kVramSize - 1)] = uint8_t(v);
        return;
    }
    if (addr >= 0x50000000 && addr < 0x60000000) {
        uint32_t swimOff = (addr & 0x0FFFFFFF) & ~0xF00000u;
        if (swimOff >= 0x1E000 && swimOff < 0x20000) {
            write8(addr + 1, uint8_t(v));
            return;
        }
    }
    write8(addr, uint8_t(v >> 8));
    write8(addr + 1, uint8_t(v));
}

uint8_t CentrisMemory::peek8(uint32_t addr) const {
    if (addr < 0x40000000) {
        if (overlay_) return rom_[addr & (kRomSize - 1)];
        if (addr < totalRam_) return ram_[addr];
        return 0xFF;
    }
    if (addr < 0x50000000) return rom_[addr & (kRomSize - 1)];
    if (addr >= 0xF9000000 && addr < 0xF9200000)
        return vram_[(addr - 0xF9000000) & (kVramSize - 1)];
    if ((addr & 0x0FFF0000) == 0x0FFF0000 && addr >= 0x5FFF0000 && addr < 0x60000000)
        return uint8_t(kBoxId >> (8 * (3 - (addr & 3))));
    return 0xFF;
}

void CentrisMemory::tick(int cpuCycles) {
    // VIA1 φ2 is the board's fixed 783.36 kHz E clock, not a divisor of
    // the CPU — an integer ratio is an approximation here (ViaEClock.h).
    const int viaCycles = viaEClock_.advance(cpuCycles, cpuHz_);
    if (viaCycles && via1_.tick(viaCycles)) updateIrq();

    adbVia_.tick(cpuCycles);
    if (cpu_) adbVia_.syncTo(cpu_->machineClock());

    sccDebt_ += cpuCycles;
    if (scc_.cyclesToNextEvent() <= sccDebt_) flushScc();

    ascCycAcc_ += int64_t(cpuCycles) * AscIosb::kCpuHz;
    int ascCyc = int(ascCycAcc_ / cpuHz_);
    ascCycAcc_ -= int64_t(ascCyc) * cpuHz_;
    asc_.tick(ascCyc);
    syncSwimFromCpu();
    drive0_.tick(cpuCycles);
    drive1_.tick(cpuCycles);

    if (scsi_.irq() != ((pvIfr_ & 0x08) != 0)) scsiPoll_();
    scsiDebt_ += cpuCycles;
    if (scsi_.cyclesToNextEvent() <= scsiDebt_) flushScsi();

    // 60.15 Hz CA1 tick (iosb 6015_timer)
    tickAcc_ += int64_t(cpuCycles) * 6015;
    if (tickAcc_ >= cpuHz_ * 100) {
        tickAcc_ -= cpuHz_ * 100;
        via1_.raiseCa1();
        updateIrq();
    }

    // 1 Hz RTC heartbeat → VIA1 CA2 (rtc cko), and advance the clock.
    secAcc_ += cpuCycles;
    if (secAcc_ >= cpuHz_) {
        secAcc_ -= cpuHz_;
        rtc_.tickSecond();
        via1_.raiseCa2();
        updateIrq();
    }

    dafbCell_.tick(cpuCycles);
}

int CentrisMemory::cyclesToNextEvent() const {
    int best = adbVia_.cyclesToNextEvent();
    best = std::min(best, viaEClock_.cyclesToNext(cpuHz_));
    auto debtBound = [](int next, int64_t debt) {
        if (next == 0x7fffffff) return next;
        return int(std::max<int64_t>(1, int64_t(next) - debt));
    };
    best = std::min(best, debtBound(scc_.cyclesToNextEvent(), sccDebt_));
    best = std::min(best, debtBound(scsi_.cyclesToNextEvent(), scsiDebt_));
    auto bridge = [](int deviceCycles, int64_t acc, int64_t deviceHz,
                     int64_t machineHz) {
        if (deviceCycles == 0x7fffffff) return deviceCycles;
        const int64_t need = int64_t(deviceCycles) * machineHz - acc;
        if (need <= 0) return 1;
        return int((need + deviceHz - 1) / deviceHz);
    };
    best = std::min(best, bridge(asc_.cyclesToNextEvent(), ascCycAcc_,
                                 AscIosb::kCpuHz, cpuHz_));
    best = std::min(best, bridge(swim_.cyclesToNextEvent(), swimCycAcc_,
                                 AscIosb::kCpuHz, cpuHz_));
    best = std::min(best, int(std::max<int64_t>(1,
        (cpuHz_ * 100 - tickAcc_ + 6014) / 6015)));
    best = std::min(best, int(std::max<int64_t>(1, cpuHz_ - secAcc_)));
    best = std::min(best, dafbCell_.cyclesToNextEvent());
    return std::max(best, 1);
}

void CentrisMemory::flushScc() {
    while (sccDebt_ > 0) {
        const int step = int(std::min<int64_t>(sccDebt_, 0x7fffffff));
        sccDebt_ -= step;
        scc_.tick(step);
    }
    if (sccIrq_ != scc_.irqAsserted()) {
        sccIrq_ = scc_.irqAsserted();
        updateIrq();
    }
}

void CentrisMemory::flushScsi() {
    while (scsiDebt_ > 0) {
        const int step = int(std::min<int64_t>(scsiDebt_, 0x7fffffff));
        scsiDebt_ -= step;
        scsi_.tick(step);
    }
    if (scsi_.irq() != ((pvIfr_ & 0x08) != 0)) scsiPoll_();
}
