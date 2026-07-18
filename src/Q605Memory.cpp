// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Q605Memory.h"
#include "Cpu040.h"
#include "Moira.h"
#include <cstring>

Q605Memory::Q605Memory(uint32_t totalRam)
    : totalRam_(totalRam)
{
    ram_.assign(totalRam_, 0);
    rom_.assign(kRomSize, 0xFF);
    vram_.assign(kVramSize, 0);
    cuda_.setAdbBus(&adb_);
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
    dafbHolding_ = 0;
    via1_.reset();
    cuda_.reset();
    scc_.reset();
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

// VIA1 E-clock sync (iosb via_sync, same arithmetic as the LC II):
// cpuClk/viaClk = 25 MHz / 783.36 kHz ≈ 31.91 — use the same integer
// scheme with a 32:1 approximation.
void Q605Memory::viaSync() {
    if (!cpu_) return;
    int64_t c = cpu_->getClock();
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
        case 13: return pvIfr_;
        case 14: return pvIer_;
        default: return 0;
    }
}

uint8_t Q605Memory::dafbRead8(uint32_t addr) {
    // HLE register file: reads return what was written (the 6-bit
    // holding-register dance of the real MEMCjr is not modelled — the
    // ROM reads back what it programmed). Grown by the boot trace.
    uint32_t idx = (addr >> 2) & 0xFF;
    uint32_t val = dafb_[idx];
    return uint8_t(val >> (8 * (3 - (addr & 3))));
}

void Q605Memory::dafbWrite8(uint32_t addr, uint8_t v) {
    uint32_t idx = (addr >> 2) & 0xFF;
    int sh = 8 * (3 - (addr & 3));
    dafb_[idx] = (dafb_[idx] & ~(0xFFu << sh)) | (uint32_t(v) << sh);
}

uint8_t Q605Memory::ioRead8(uint32_t addr) {
    uint32_t sub = addr & 0x0FFFFFFF;

    if (sub >= 0x0FFFFFFC)                       // machine ID (LC 475)
        return uint8_t(0xA55A2221u >> (8 * (3 - (sub & 3))));

    uint32_t base = sub & 0x0003FFFF;            // pre-mirror window

    if (base < 0x02000) return viaAccess8(base, false, 0);
    if ((sub & ~0xF00000u) >= 0x02000 && (sub & ~0xF00000u) < 0x04000)
        return via2Access8(sub & 0x1FFF, false, 0);
    if (base >= 0x0C000 && base < 0x0E000) {     // SCC, byte on D8-15
        int ch = (base >> 1) & 1;
        uint8_t d = ((base >> 2) & 1) ? scc_.readData(ch) : scc_.readCtl(ch);
        if (onIoAccess) onIoAccess(addr, false, d);
        return d;
    }
    if (base >= 0x0E000 && base < 0x10000) {     // MEMCjr regs
        uint32_t idx = (base & 0x7F) >> 2;
        return uint8_t(memcjr_[idx] >> (8 * (3 - (base & 3))));
    }
    if (base >= 0x10000 && base < 0x10100)       // TurboSCSI regs — Q6
        return 0;
    if (base >= 0x10100 && base < 0x10104)       // TurboSCSI DMA — Q6
        return 0;
    if ((sub & ~0xF00000u) >= 0x14000 && (sub & ~0xF00000u) < 0x15000)
        return 0;                                // ASC — Q8 (stub)
    if ((sub & ~0xF00000u) >= 0x18000 && (sub & ~0xF00000u) < 0x1A000) {
        uint32_t reg = ((sub & 0x1FFF) >> 8) & 0x1F;
        uint32_t byteInWord = sub & 1;
        return uint8_t(iosbRegs_[reg] >> (8 * (1 - byteInWord)));
    }
    if ((sub & ~0xF00000u) >= 0x1E000 && (sub & ~0xF00000u) < 0x20000)
        return 0xFF;                             // SWIM2 stub (no floppy)

    busError(addr, false);
}

void Q605Memory::ioWrite8(uint32_t addr, uint8_t v) {
    uint32_t sub = addr & 0x0FFFFFFF;
    if (sub >= 0x0FFFFFFC) return;               // ID is read-only

    uint32_t base = sub & 0x0003FFFF;

    if (base < 0x02000) { viaAccess8(base, true, v); return; }
    if ((sub & ~0xF00000u) >= 0x02000 && (sub & ~0xF00000u) < 0x04000) {
        via2Access8(sub & 0x1FFF, true, v);
        return;
    }
    if (base >= 0x0C000 && base < 0x0E000) {
        int ch = (base >> 1) & 1;
        if (onIoAccess) onIoAccess(addr, true, v);
        if ((base >> 2) & 1) scc_.writeData(ch, v);
        else scc_.writeCtl(ch, v);
        return;
    }
    if (base >= 0x0E000 && base < 0x10000) {
        uint32_t idx = (base & 0x7F) >> 2;
        int sh = 8 * (3 - (base & 3));
        memcjr_[idx] = (memcjr_[idx] & ~(0xFFu << sh)) | (uint32_t(v) << sh);
        return;
    }
    if (base >= 0x10000 && base < 0x10104) return;   // SCSI — Q6
    if ((sub & ~0xF00000u) >= 0x14000 && (sub & ~0xF00000u) < 0x15000)
        return;                                      // ASC stub
    if ((sub & ~0xF00000u) >= 0x18000 && (sub & ~0xF00000u) < 0x1A000) {
        uint32_t reg = ((sub & 0x1FFF) >> 8) & 0x1F;
        if (sub & 1) iosbRegs_[reg] = uint16_t((iosbRegs_[reg] & 0xFF00) | v);
        else         iosbRegs_[reg] = uint16_t((iosbRegs_[reg] & 0x00FF) | (v << 8));
        return;
    }
    if ((sub & ~0xF00000u) >= 0x1E000 && (sub & ~0xF00000u) < 0x20000)
        return;                                      // SWIM2 stub

    busError(addr, true);
}

uint8_t Q605Memory::read8(uint32_t addr) {
    if (addr < 0x40000000) {
        if (overlay_) return rom_[addr & (kRomSize - 1)];
        if (addr < totalRam_) return ram_[addr];
        busError(addr, false);
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
            return uint16_t(rom_[o] << 8 | rom_[o + 1]);
        }
        if (addr + 1 < totalRam_)
            return uint16_t(ram_[addr] << 8 | ram_[addr + 1]);
        busError(addr, false);
    }
    if (addr < 0x50000000) {
        if (overlay_) overlay_ = false;
        uint32_t o = addr & (kRomSize - 1);
        return uint16_t(rom_[o] << 8 | rom_[o + 1]);
    }
    if (addr >= 0xF9000000 && addr + 1 < 0xF9000000 + kVramSize) {
        uint32_t o = addr - 0xF9000000;
        return uint16_t(vram_[o] << 8 | vram_[o + 1]);
    }
    return uint16_t(read8(addr) << 8 | read8(addr + 1));
}

void Q605Memory::write8(uint32_t addr, uint8_t v) {
    if (addr < 0x40000000) {
        if (overlay_) return;                    // ROM mirror: writes drop
        if (addr < totalRam_) { ram_[addr] = v; return; }
        busError(addr, true);
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
    if (addr < 0x40000000 && !overlay_ && addr + 1 < totalRam_) {
        ram_[addr] = uint8_t(v >> 8);
        ram_[addr + 1] = uint8_t(v);
        return;
    }
    if (addr >= 0xF9000000 && addr + 1 < 0xF9000000 + kVramSize) {
        uint32_t o = addr - 0xF9000000;
        vram_[o] = uint8_t(v >> 8);
        vram_[o + 1] = uint8_t(v);
        return;
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

    // 60.15 Hz CA1 tick (iosb 6015_timer)
    tickAcc_ += int64_t(cpuCycles) * 6015;
    if (tickAcc_ >= kCpuHz * 100) {
        tickAcc_ -= kCpuHz * 100;
        via1_.raiseCa1();
        updateIrq();
    }

    // DAFB VBL — placeholder timing (640×480@60: blank ≈ the last 5% of
    // the frame) until the Swatch registers drive it (Q5 follow-up)
    framePos_ += cpuCycles;
    int64_t frameLen = kCpuHz / 60;
    framePos_ %= frameLen;
    bool vbl = framePos_ >= frameLen * 95 / 100;
    if (vbl != vblState_) {
        vblState_ = vbl;
        vblIrq(vbl);
    }
}
