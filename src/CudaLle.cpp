// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Cuda firmware-LLE glue — see CudaLle.h for the signal map and oracle
// references (MAME mame/apple/cuda.cpp + m68hc05e1.cpp).

#include "CudaLle.h"
#include "Via6522.h"
#include <cstring>

CudaLle::CudaLle(Via6522& via, int64_t cpuHz)
    : via_(via), cpuHz_(cpuHz)
{
    mcu_.readPort = [this](int p) { return mcuPortRead(p); };
    mcu_.writePort = [this](int p, uint8_t v) { mcuPortWrite(p, v); };
    mcu_.setPullups(1, 0xC0);            // PB6/7 I2C pull-ups (cuda.cpp:88)
    mcu_.setPullups(2, 0x04);            // PC2 NMI pull-up (cuda.cpp:89)
}

bool CudaLle::loadFirmware(const std::vector<uint8_t>& rom) {
    fwLoaded_ = mcu_.loadRom(rom);
    return fwLoaded_;
}

void CudaLle::reset() {
    mcu_.reset();
    adb_.reset();
    held_ = true;
    treq_ = 1;
    byteack_ = tip_ = true;
    lastViaClock_ = true;
    resetLine_ = false;
    pramInstalled_ = false;
    mcuAcc_ = adbAcc_ = 0;
}

// ── PRAM staging (cuda.cpp pc_w :117-131: install on reset release) ────
uint8_t CudaLle::pram(int i) const {
    if (pramInstalled_)
        return const_cast<M68hc05&>(mcu_).ramByte(0x100 + (i & 0xFF));
    return stagedPram_[i & 0xFF];
}

void CudaLle::setPram(int i, uint8_t v) {
    stagedPram_[i & 0xFF] = v;
    if (pramInstalled_)                  // live after release, like NVRAM
        mcu_.setRamByte(0x100 + (i & 0xFF), v);
}

// ── MCU port callbacks (cuda.cpp pa_r/pb_r/pc_r) ───────────────────────
uint8_t CudaLle::mcuPortRead(int p) {
    switch (p) {
        case 0:                          // PA: pull-up|PFW|ADB-power-off
            return uint8_t(0x02 | 0x01 | 0x04 | (adb_.line() ? 0x40 : 0));
        case 1:                          // PB: +5v | BYTEACK | TIP | via_data
            return uint8_t(0x01
                           | (byteack_ ? 0x04 : 0)
                           | (tip_ ? 0x08 : 0)
                           | (via_.extShiftCB2Out() ? 0x20 : 0)
                           | 0x40 | 0x80);
        default:                         // PC: trickle sense + pull-up
            return 0x03;
    }
}

void CudaLle::mcuPortWrite(int p, uint8_t v) {
    switch (p) {
        case 0:                          // PA7 = ADB drive (pa_w :96-121)
            adb_.setHostDrive((v & 0x80) != 0);
            break;
        case 1: {                        // PB (pb_w :123-137)
            treq_ = uint8_t((v >> 1) & 1);
            const bool clock = (v & 0x10) != 0;
            const bool data = (v & 0x20) != 0;
            if (clock != lastViaClock_) {
                // via_clock drives the host VIA SR's external shift clock
                // (CB1); via_data rides CB2 — the PIC1654S wire pattern.
                via_.extShiftCB1(clock, data);
                lastViaClock_ = clock;
            }
            break;
        }
        case 2:                          // PC3 = host reset (pc_w :139-160)
            if (!resetLine_ && (v & 0x08)) {
                held_ = false;
                if (!pramInstalled_) {   // slap staged PRAM into live RAM
                    for (int i = 0; i < 256; i++)
                        mcu_.setRamByte(0x100 + i, stagedPram_[i]);
                    pramInstalled_ = true;
                }
            }
            resetLine_ = (v & 0x08) != 0;
            break;
    }
}

// ── Host-side wiring ───────────────────────────────────────────────────
void CudaLle::portBChanged(uint8_t pb) {
    // macquadra605.cpp:230-233: VIA1 PB4 → BYTEACK, PB5 → TIP (active low).
    byteack_ = (pb & 0x10) != 0;
    tip_ = (pb & 0x20) != 0;
}

void CudaLle::tick(int cpuCycles) {
    // Machine cycles → the E1's 2.097 MHz cycle domain.
    mcuAcc_ += int64_t(cpuCycles) * kMcuHz;
    int mcuCyc = int(mcuAcc_ / cpuHz_);
    mcuAcc_ -= int64_t(mcuCyc) * cpuHz_;
    if (mcuCyc) mcu_.run(mcuCyc);
    // AdbLine's timers run in the Mac II 15.6672 MHz cycle domain.
    adbAcc_ += int64_t(cpuCycles) * kAdbHz;
    int adbCyc = int(adbAcc_ / cpuHz_);
    adbAcc_ -= int64_t(adbCyc) * cpuHz_;
    if (adbCyc) adb_.tick(adbCyc);
}
