// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Mac LC 475 / Quadra 605 memory map: MEMCjr + PrimeTime (Q5) ──
// The 68LC040 sees a 32-bit clean map (MAME macquadra605.cpp:123-137,
// djmemc.cpp, iosb.cpp — refs in ~/src/refs/mame-apple, pinned lines in
// the comments below):
//   $00000000-  RAM (flat; MEMCjr banking is not modelled — MAME ditto),
//               ROM mirror while the boot overlay is on
//   $40000000-  ROM 1 MB, mirrored across $4FFFFFFF; the first read
//               anywhere in the window clears the overlay
//               (djmemc.cpp rom_switch_r)
//   $50000000-  PrimeTime I/O (iosb_base::map):
//               +$00000 VIA1 (real 6522, reg every $200, 60.15 Hz CA1)
//               +$02000 VIA2 (Quadra pseudo-VIA: real-VIA layout,
//                        no timers/SR)
//               +$0C000 SCC (word access, high byte)
//               +$0E000 MEMCjr regs (DAFB 6-bit holding at +$7C)
//               +$10000 TurboSCSI 53C96 regs (reg = A4-A7) — Q6
//               +$10100 TurboSCSI DMA port — Q6
//               +$14000 ASC (EASC-like)
//               +$18000 IOSB regs (u16 every $100)
//               +$1E000 SWIM2 (stub: SCSI boot only, like early LC II)
//   $5FFFFFFC   machine ID $A55A2221 (LC 475)
//   $F9000000-  VRAM (1 MB modelled)
//   $F9800000-  DAFB II registers (HLE stub grown by the boot trace)
// Anything else in I/O space bus-errors — the ROM's address-map probe
// relies on it (same discipline as the LC II V8).
// Cuda replaces Egret+RTC: VIA1 PB3=TREQ in, PB4=BYTEACK, PB5=TIP,
// CB1/CB2 = Cuda clock/data (macquadra605.cpp:214-224). The wire
// protocol is Egret-compatible; POM68K reuses the Egret HLE until the
// boot trace demands Cuda-specific commands.

#pragma once
#include "Via6522.h"
#include "Egret.h"
#include "AdbBus.h"
#include "Scc8530.h"
#include "Ncr53c96.h"
#include "ScsiDisk.h"
#include <cstdint>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

class Cpu040;

class Q605Memory {
public:
    static constexpr uint32_t kRomSize = 0x100000;   // 1 MB
    static constexpr uint32_t kVramSize = 0x100000;  // 1 MB window
    static constexpr int64_t  kCpuHz = 25000000;     // 25 MHz 68LC040

    explicit Q605Memory(uint32_t totalRam = 36u << 20);

    bool loadRom(const std::vector<uint8_t>& data);  // 1 MB flat image
    void reset();

    uint8_t  read8(uint32_t addr);
    uint16_t read16(uint32_t addr);
    void     write8(uint32_t addr, uint8_t v);
    void     write16(uint32_t addr, uint16_t v);
    uint8_t  peek8(uint32_t addr) const;             // side-effect free

    void setCpu(Cpu040* cpu) { cpu_ = cpu; }
    void updateIrq();
    int iplLevel() const;    // SCC=4 > VIA2=2 > VIA1=1 (iosb field_interrupts)

    // Called from Cpu040::sync with elapsed CPU cycles: VIA1 timers
    // (783.36 kHz) + the 60.15 Hz CA1 tick + the DAFB VBL.
    void tick(int cpuCycles);

    Via6522& via1() { return via1_; }
    Egret& cuda() { return cuda_; }
    AdbBus& adb() { return adb_; }
    Scc8530& scc() { return scc_; }
    Ncr53c96& scsi() { return scsi_; }
    ScsiDisk& scsiDisk() { return scsiDisks_[0]; }  // boot drive (tests poke it)

    // Attach a SCSI target from a backing image (boot drive = ID 0).
    bool attachScsi(const std::string& path, bool writeBack = false, int id = 0) {
        if (id < 0 || id > 6 || !scsiDisks_[id].open(path, writeBack)) return false;
        scsi_.attach(&scsiDisks_[id], id);
        return true;
    }
    bool cpuHeld() const { return cuda_.cpuHeld(); }
    bool overlay() const { return overlay_; }
    const uint8_t* vram() const { return vram_.data(); }
    const uint32_t* dafbRegs() const { return dafb_; }

    // VIA2 IFR device lines (Quadra pseudo-VIA: CA1=slot/VBL summary,
    // bit encodings identical to a real VIA's IFR)
    void vblIrq(bool s);
    void scsiIrq(bool s);
    void sccIrqLine(bool s) { sccIrq_ = s; updateIrq(); }

    // Debug hooks (q605_trace)
    std::function<void(uint32_t, bool, uint32_t)> onIoAccess;   // addr, isWrite, value
    std::function<void(uint32_t, bool)> onBusError;             // addr, isWrite
    // RAM write-watch: fires onRamWrite(addr,size,value) when a write's
    // byte range covers ramWatch_ (0 = disabled). Diagnostic only.
    uint32_t ramWatch_ = 0;
    std::function<void(uint32_t, int, uint32_t)> onRamWrite;

    // Quadra pseudo-VIA (VIA2) state — MAME devices/machine/pseudovia.cpp
    // quadra flavor: reg 0 = port B, 1/15 = port A (nubus IRQs, active
    // low, bit 6 = VBL), 13 = IFR, 14 = IER; IFR/IER mask $1B =
    // DRQ(0) | slots(1) | SCSI(3) | ASC(4).
    bool via2IrqAsserted() const { return (pvIfr_ & pvIer_ & 0x1B) != 0; }

private:
    uint8_t viaAccess8(uint32_t addr, bool write, uint8_t v);
    uint8_t via2Access8(uint32_t addr, bool write, uint8_t v);
    void via2Recalc();
    void viaSync();
    [[noreturn]] void busError(uint32_t addr, bool write) const;
    uint8_t ioRead8(uint32_t addr);
    void ioWrite8(uint32_t addr, uint8_t v);
    uint8_t scsiDmaRead_();                  // DRQ-gated pseudo-DMA byte in
    void    scsiDmaWrite_(uint8_t v);        // DRQ-gated pseudo-DMA byte out
    void    scsiPoll_();                      // feed 53C96 irq()/drq() to VIA2
    uint8_t dafbRead8(uint32_t addr);
    void dafbWrite8(uint32_t addr, uint8_t v);
    uint32_t dafbRegReadRaw(uint32_t off);   // pre-holding-split register value

    std::vector<uint8_t> ram_, rom_, vram_;
    Via6522 via1_;
    Egret cuda_{via1_, true};      // Cuda flavor: TIP/BYTEACK active low
    AdbBus adb_;
    Scc8530 scc_;
    Ncr53c96 scsi_;                // TurboSCSI 53C96 (Q6)
    ScsiDisk scsiDisks_[7];        // by SCSI ID; [0] = boot drive
    Cpu040* cpu_ = nullptr;

    uint32_t totalRam_;
    bool overlay_ = true;
    bool sccIrq_ = false;

    // Quadra pseudo-VIA registers
    uint8_t pvIfr_ = 0, pvIer_ = 0, pvPortB_ = 0;
    uint8_t nubusIrqs_ = 0xFF;     // active low; bit 6 = VBL (MEMCjr video)

    // MEMCjr / DAFB HLE state
    uint32_t memcjr_[0x20] = {};   // $5000E000, u32 every 4 (only $7C used)
    uint16_t dafbHolding_ = 0;     // MEMCjr 6-bit DAFB bus holding register
    uint32_t dafb_[0x100] = {};    // $F9800000 register file (u32 index)
    uint16_t iosbRegs_[0x20] = {}; // $50018000, u16 every $100

    // DAFB HLE (MEMCjr integrated cell, version 3) — MAME dafb.cpp.
    // The raw dafb_[] file backs unknown registers; the fields below
    // implement the ones the ROM's video driver depends on.
    uint32_t dafbRegRead(uint32_t off);          // u32 semantics
    void     dafbRegWrite(uint32_t off, uint32_t v);
    void     dafbRecalcIrq();
    uint8_t  dafbIntStatus_ = 0;   // bit 0 = VBL, bit 2 = cursor scanline
    uint32_t swatchIntEnable_ = 0; // $104: bit 0 VBL, bit 2 cursor line
    uint32_t dafbCursorLine_ = 0;  // $118
    uint8_t  palAddress_ = 0, palIdx_ = 0;       // Antelope RAMDAC
    uint8_t  ac842Pbctrl_ = 0, pcbr1_ = 0;
    uint8_t  clut_[256][3] = {};
    int      prevLine_ = 0;        // scanline edge detect (525-line frame)

    // 60.15 Hz tick + VBL (DAFB "Swatch"): both derived from CPU cycles
    int viaPhase_ = 0;
    int64_t tickAcc_ = 0;
    int64_t framePos_ = 0;
    bool vblState_ = false;
};
