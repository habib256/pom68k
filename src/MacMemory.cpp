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
    iwm_.reset();
    iwm_.attachDrive(&drive_, nullptr);
    drive_.reset();
    scc_.reset();
    kbd_.reset();
    kbdPhase_ = KBD_IDLE;
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
    iwm_.tick(cpuCycles);
    drive_.tick(cpuCycles);

    // M0110 transaction (DEV.md § Input): the command byte finishes
    // shifting out (SR interrupt #1); once the driver flips the ACR to
    // shift-in, the response byte lands (SR interrupt #2). ~3 ms per phase.
    if (kbdPhase_ == KBD_SHIFT_OUT || kbdPhase_ == KBD_SHIFT_IN) {
        kbdTimer_ -= cpuCycles;
        if (kbdTimer_ <= 0) {
            if (kbdPhase_ == KBD_SHIFT_OUT) {
                kbdResp_ = kbd_.respond(kbdCmd_);
                kbdPhase_ = KBD_AWAIT_IN;
                via_.raiseShift();                       // command sent
                if (((via_.acr() >> 2) & 7) == 3) {      // already in shift-in
                    kbdPhase_ = KBD_SHIFT_IN;
                    kbdTimer_ = 23500;
                }
            } else {
                kbdPhase_ = KBD_IDLE;
                via_.loadSR(kbdResp_);                   // response arrived
            }
            updateIrq();
        }
    }

    int stepped = mouse_.tick(cpuCycles);     // quadrature steps
    if (stepped) {
        if (stepped & 1) scc_.setDcd(1, mouse_.x1);   // channel A = X
        if (stepped & 2) scc_.setDcd(0, mouse_.y1);   // channel B = Y
        updateIrq();
    }
}

void MacMemory::tickOneSecond() {
    rtc_.tickSecond();
    via_.raiseCa2();
    updateIrq();
}

// Port B inputs (DEV.md § VIA): PB0 = RTC data, PB3 = mouse button
// (0 = down), PB4/PB5 = mouse quadrature X2/Y2, PB6 = H4 horizontal
// blanking (1 = in hblank). Derived fresh before every ORB read.
void MacMemory::refreshPortBInputs() {
    uint8_t in = 0xFF & uint8_t(~0x01);
    in |= rtc_.dataBit() & 1;
    if (mouse_.button()) in &= uint8_t(~0x08);     // PB3: 0 = pressed
    if (!mouse_.x2) in &= uint8_t(~0x10);          // PB4 = X2
    if (!mouse_.y2) in &= uint8_t(~0x20);          // PB5 = Y2
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
    if (reg == Via6522::ORA || reg == Via6522::ORA_NH || reg == Via6522::DDRA) {
        overlay_ = (via_.portA() & 0x10) != 0;   // PA4 = ROM overlay
        iwm_.setSel((via_.portA() & 0x20) != 0); // PA5 = drive SEL line
    }
    if (reg == Via6522::SR && ((via_.acr() >> 2) & 7) == 7) {
        // shift-out under external clock = keyboard command byte
        kbdCmd_ = v;
        kbdPhase_ = KBD_SHIFT_OUT;
        kbdTimer_ = 23500;                       // ~3 ms at 7.8336 MHz
    }
    if (reg == Via6522::ACR && kbdPhase_ == KBD_AWAIT_IN && ((v >> 2) & 7) == 3) {
        kbdPhase_ = KBD_SHIFT_IN;                // driver ready for the response
        kbdTimer_ = 23500;
    }
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
        case 0x8: case 0x9:                                  // SCC read, even bytes
            // A1 = channel (0 = B, 1 = A), A2 = ctl/data
            if (addr & 4) return scc_.readData((addr >> 1) & 1);
            return scc_.readCtl((addr >> 1) & 1);
        case 0xA: case 0xB:                                  // SCC write side (reads: 0)
            return 0x00;
        case 0xC: case 0xD:                                  // IWM, odd bytes, reg = A9-A12
            return iwm_.read((addr >> 9) & 0xF);
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
        case 0xA: case 0xB: {                                // SCC write, odd bytes
            bool wasIrq = scc_.irqAsserted();
            if (addr & 4) scc_.writeData((addr >> 1) & 1, v);
            else scc_.writeCtl((addr >> 1) & 1, v);
            if (wasIrq != scc_.irqAsserted()) updateIrq();
            return;
        }
        case 0xC: case 0xD:                                  // IWM
            iwm_.write((addr >> 9) & 0xF, v);
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
