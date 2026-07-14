// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Mac Plus memory map (24-bit) ──
// RAM $000000-$3FFFFF (up to 4 MB), ROM $400000 (128 KB, mirrored), SCSI
// $580000, SCC read $9xxxxx / write $Bxxxxx, IWM $Dxxxxx, VIA $Exxxxx.
// Boot overlay maps ROM at $000000 and RAM at $600000 until the ROM clears
// VIA PA4. Video framebuffer: main = ramSize-0x5900 (512×342, 1 bpp).
// Source of truth: Guide to the Macintosh Family Hardware; MAME mac.cpp
// (pending web-research pinning — see TODO.md § M2).
// Gate: tests/cpu_smoke.cpp.

#pragma once
#include "Via6522.h"
#include "Rtc.h"
#include "Iwm.h"
#include "SonyDrive.h"
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>

class Cpu68k;

class MacMemory {
public:
    static constexpr uint32_t kRamSize = 0x400000;   // 4 MB (Mac Plus max)
    static constexpr uint32_t kRomSize = 0x20000;    // 128 KB

    MacMemory();
    bool loadRom(const std::vector<uint8_t>& data);
    void installRom(const uint8_t* data, size_t n);  // built-in demo/test ROM
    void reset();                                    // asserts the boot overlay

    uint8_t  read8(uint32_t addr);
    uint16_t read16(uint32_t addr);
    void     write8(uint32_t addr, uint8_t v);
    void     write16(uint32_t addr, uint16_t v);

    // Screen buffer bases, selected by VIA PA6 (1 = main, 0 = alternate).
    // GttMFH; MAME MAC_MAIN_SCREEN_BUF_OFFSET; Mini vMac kMain_Offset.
    uint32_t mainScreenBase() const { return kRamSize - 0x5900; }
    uint32_t altScreenBase()  const { return kRamSize - 0xD900; }
    uint32_t screenBase() const {
        return (via_.portA() & 0x40) ? mainScreenBase() : altScreenBase();
    }

    const uint8_t* ram() const { return ram_.data(); }
    Via6522& via() { return via_; }
    bool overlay() const { return overlay_; }

    // Wire-back to the CPU: the IPL line is level-sensitive, so it must be
    // recomputed whenever a VIA access changes IFR/IER (POMIIGS setCpu pattern).
    void setCpu(Cpu68k* cpu) { cpu_ = cpu; }
    void updateIrq();          // raise/lower IPL from VIA state

    // Called from Cpu68k::sync with elapsed CPU cycles: advances the VIA
    // timers (φ2 = CPU/10) and raises IRQs on underflow.
    void tick(int cpuCycles);
    // Called once per emulated second: RTC seconds + CA2 interrupt.
    void tickOneSecond();
    Rtc& rtc() { return rtc_; }
    Iwm& iwm() { return iwm_; }
    SonyDrive& internalDrive() { return drive_; }
    bool insertDisk(const std::string& path) { return drive_.insert(path); }

private:
    uint8_t viaAccess(uint32_t addr, bool write, uint8_t v);
    void refreshPortBInputs();

    std::vector<uint8_t> ram_, rom_;
    Via6522 via_;
    Rtc rtc_;
    Iwm iwm_;
    SonyDrive drive_;                // internal drive; external = M5.1
    Cpu68k* cpu_ = nullptr;
    int viaPhase_ = 0;         // CPU-cycle remainder for the ÷10 VIA clock
    bool overlay_ = true;
};
