// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "MacMemory.h"
#include "Cpu68k.h"

MacMemory::MacMemory() : ram_(kRamSize, 0), rom_(kRomSize, 0xFF) {}

bool MacMemory::loadRom(const std::vector<uint8_t>& data) {
    if (data.empty() || data.size() > kRomSize) return false;
    installRom(data.data(), data.size());
    return true;
}

void MacMemory::installRom(const uint8_t* data, size_t n) {
    rom_.assign(kRomSize, 0xFF);
    for (size_t i = 0; i < kRomSize; i++) rom_[i] = data[i % n];  // mirror
}

void MacMemory::reset() {
    via_.reset();
    rtc_.reset();                    // shifter only; seconds/PRAM are battery-backed
    viaPhase_ = 0;
    overlay_ = true;                 // hardware reset asserts the overlay
}

void MacMemory::updateIrq() {
    if (cpu_) cpu_->updateIpl();
}

void MacMemory::tick(int cpuCycles) {
    viaPhase_ += cpuCycles;
    int t = viaPhase_ / 10;          // φ2 = 7.8336 MHz / 10 = 783.36 kHz
    viaPhase_ %= 10;
    if (t && via_.tick(t)) updateIrq();
}

void MacMemory::tickOneSecond() {
    rtc_.tickSecond();
    via_.raiseCa2();
    updateIrq();
}

// Port B inputs (DEV.md § VIA): PB0 = RTC data, PB3 = mouse button
// (1 = up), PB6 = H4 horizontal blanking (1 = in hblank), PB4/5 mouse
// quadrature (idle high). Derived fresh before every ORB read.
void MacMemory::refreshPortBInputs() {
    uint8_t in = 0xFF & uint8_t(~0x01);
    in |= rtc_.dataBit() & 1;
    if (cpu_) {
        long f = long(cpu_->getClock() % 130240);
        if (f % 352 < 256) in &= uint8_t(~0x40);   // beam in display portion
    }
    via_.setInB(in);
}

// VIA registers are selected by address bits 12..9 ($EFE1FE + reg*$200).
// Any access may change IFR/IER (reads clear flags), so the level-sensitive
// IPL line is recomputed after every one.
uint8_t MacMemory::viaAccess(uint32_t addr, bool write, uint8_t v) {
    int reg = (addr >> 9) & 0xF;
    if (!write) {
        if (reg == Via6522::ORB) refreshPortBInputs();
        uint8_t r = via_.read(reg);
        updateIrq();
        return r;
    }
    via_.write(reg, v);
    if (reg == Via6522::ORA || reg == Via6522::ORA_NH || reg == Via6522::DDRA)
        overlay_ = (via_.portA() & 0x10) != 0;   // PA4 = ROM overlay
    if (reg == Via6522::ORB || reg == Via6522::DDRB) {
        uint8_t pb = via_.portB();               // drive the RTC serial lines
        rtc_.setLines(!(pb & 0x04), (pb & 0x02) != 0, (pb & 0x01) != 0);
    }
    updateIrq();
    return 0;
}

uint8_t MacMemory::read8(uint32_t addr) {
    addr &= 0xFFFFFF;
    switch (addr >> 20) {
        case 0x0: case 0x1: case 0x2: case 0x3:              // RAM (or ROM w/ overlay)
            if (overlay_) return rom_[addr & (kRomSize - 1)];
            return ram_[addr & (kRamSize - 1)];
        case 0x4: case 0x5:
            if (addr >= 0x580000) return 0x00;               // SCSI 5380 (stub)
            return rom_[addr & (kRomSize - 1)];              // ROM
        case 0x6: case 0x7:                                  // RAM while overlay on
            return ram_[addr & (kRamSize - 1)];
        case 0x8: case 0x9:                                  // SCC read, even bytes (stub — M7)
            return 0x00;                                     // MAME mac128.cpp; odd read = SCC reset quirk
        case 0xA: case 0xB:                                  // SCC write side (stub — M7)
            return 0x00;
        case 0xC: case 0xD:                                  // IWM, odd bytes (stub — M5)
            return 0x1F;                                     // $1F needed to reach the blinking-? (BMOW Plus Too)
        case 0xE: case 0xF:                                  // VIA
            if (addr >= 0xE80000) return viaAccess(addr, false, 0);
            return 0xFF;
    }
    return 0xFF;                                             // open bus
}

void MacMemory::write8(uint32_t addr, uint8_t v) {
    addr &= 0xFFFFFF;
    switch (addr >> 20) {
        case 0x0: case 0x1: case 0x2: case 0x3:
            if (!overlay_) ram_[addr & (kRamSize - 1)] = v;  // ROM under overlay: ignored
            return;
        case 0x6: case 0x7:
            ram_[addr & (kRamSize - 1)] = v;
            return;
        case 0xE: case 0xF:
            if (addr >= 0xE80000) viaAccess(addr, true, v);
            return;
        default:                                             // SCC/IWM/SCSI stubs
            return;
    }
}

uint16_t MacMemory::read16(uint32_t addr) {
    return uint16_t((read8(addr) << 8) | read8(addr + 1));   // 68000 big-endian
}

void MacMemory::write16(uint32_t addr, uint16_t v) {
    write8(addr, uint8_t(v >> 8));
    write8(addr + 1, uint8_t(v));
}
