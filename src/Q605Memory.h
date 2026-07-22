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
//               +$1E000 SWIM2 (register/FIFO + SuperDrive media)
//   $5FFFFFFC   machine ID $A55A2221 (LC 475)
//   $F9000000-  VRAM (1 MB modelled)
//   $F9800000-  DAFB II register cell (Dafb.h/.cpp; the MEMCjr 6+6-bit
//               holding split over the 12-bit window stays here)
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
#include "Asc.h"
#include "Swim2.h"
#include "Dafb.h"
#include "SonyDrive.h"
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
    AscIosb& asc() { return asc_; }
    Swim2& swim() { return swim_; }
    SonyDrive& internalDrive() { return drive0_; }
    SonyDrive& externalDrive() { return drive1_; }
    bool insertDisk(const std::string& path) { return drive0_.insert(path); }
    void ejectDisk() { drive0_.eject(); }
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
    // DAFB cell accessors (forwarders; see Dafb.h).
    Dafb& dafb() { return dafbCell_; }
    const uint32_t* dafbRegs() const { return dafbCell_.regs(); }
    const uint8_t (*clut() const)[3] { return dafbCell_.clut(); }
    uint32_t dafbStride() const { return dafbCell_.stride(); }
    uint32_t dafbBase() const { return dafbCell_.base(); }
    uint8_t dafbMode() const { return dafbCell_.mode(); }
    uint8_t dafbDepth() const { return dafbCell_.depth(); }
    uint32_t dafbHres() const { return dafbCell_.hres(); }
    uint32_t dafbVres() const { return dafbCell_.vres(); }
    uint32_t dafbPixelClock() const { return dafbCell_.pixelClock(); }
    bool dafbBlanked() const { return dafbCell_.blanked(); }
    void setDafbMonitor(uint8_t code) { dafbCell_.setMonitor(code); }

    // VIA2 IFR device lines (Quadra pseudo-VIA: CA1=slot/VBL summary,
    // bit encodings identical to a real VIA's IFR)
    void vblIrq(bool s);
    void scsiIrq(bool s);
    void ascIrq(bool s);                         // EASC half-empty → IFR bit 4
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
    uint8_t via2Ifr() const { return pvIfr_; }   // Q6.5b diag
    uint8_t via2Ier() const { return pvIer_; }

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

    std::vector<uint8_t> ram_, rom_, vram_;
    Via6522 via1_;
    // Cuda flavor: TIP/BYTEACK active low; 25 MHz machine clock for the
    // µs per-byte pacing and the RTC seconds heartbeat
    Egret cuda_{via1_, true, 25000000};
    AdbBus adb_;
    Scc8530 scc_;
    // PrimeTime's IOSB audio cell at $50014000: $BB version, stereo FIFO A/B,
    // 22.257 kHz at C15M (15.6672 MHz), level IRQ on pseudo-VIA2 bit 4.
    // This is the IOSB ASC verified by ASCTester on an LC 475, not the $B0
    // discrete EASC despite MAME iosb.cpp's historical ASC_EASC wiring.
    AscIosb asc_;
    int64_t ascCycAcc_ = 0;        // 25 MHz CPU → 15.6672 MHz ASC clock bridge
    Swim2 swim_;                   // PrimeTime SWIM2 at $5001E000
    SonyDrive drive0_;             // internal SuperDrive (SWIM2 soft-select A)
    SonyDrive drive1_;             // external SuperDrive (soft-select B)
    // MAME swim2_device::read/write call sync() on every access so the
    // FIFO drains between a write-ACTION and the handshake poll. Batching
    // only from Cpu040::catchUp left the ROM spinning on bit 7 forever.
    void syncSwimFromCpu();
    int64_t swimLastCpu_ = -1;     // <0: latch on first sync
    int64_t swimCycAcc_ = 0;       // CPU→C15M fractional bridge (shared timeline)
    Ncr53c96 scsi_;                // TurboSCSI 53C96 (Q6)
    ScsiDisk scsiDisks_[7];        // by SCSI ID; [0] = boot drive
    Cpu040* cpu_ = nullptr;

    uint32_t totalRam_;
    // $5FFFFFFC board ID (MAME macquadra605.cpp): LC 475 $A55A2221,
    // Quadra 605 $A55A2225, LC 575 $A55A222E. POM68K_Q605_ID overrides
    // (hex) for machine-identity experiments.
    uint32_t machineId_ = 0xA55A2221u;
    bool overlay_ = true;
    bool sccIrq_ = false;

    // Quadra pseudo-VIA registers
    uint8_t pvIfr_ = 0, pvIer_ = 0, pvPortB_ = 0;
    uint8_t nubusIrqs_ = 0xFF;     // active low; bit 6 = VBL (MEMCjr video)
    bool ascLine_ = false;          // live level, re-sampled after IFR ack

    // MEMCjr state
    uint32_t memcjr_[0x20] = {};   // $5000E000, u32 every 4 (only $7C used)
    uint16_t dafbHolding_ = 0;     // MEMCjr 6-bit DAFB bus holding register
    uint16_t iosbRegs_[0x20] = {}; // $50018000, u16 every $100

    // DAFB cell ($F9800000 window) + MEMCjr 6+6-bit holding wrappers.
    Dafb dafbCell_{kCpuHz};
    uint32_t dafbRegRead(uint32_t off);          // holding split (read)
    void     dafbRegWrite(uint32_t off, uint32_t v);   // holding merge (write)

    // 60.15 Hz CA1 tick, derived from CPU cycles
    int viaPhase_ = 0;
    int64_t tickAcc_ = 0;
};
