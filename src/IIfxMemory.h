// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Mac IIfx map — platform #12: OSS + two Apple PIC IOPs (M3) ───────────
// The fastest 68030 Mac (40 MHz, "F19"): a Mac II-style front end (VIA1 +
// RTC + discrete ASC + SCC + NuBus-only video) where the **OSS interrupt
// controller replaces VIA2**, the SCC and SWIM sit behind two **ApplePic
// IOPs** (host-downloaded 65C02 firmware — ADB is bit-banged on the SWIM
// PIC's GPIO), and SCSI goes through the **SCSIDMA** ASIC (53C80 cell +
// handshake; full DMA is A/UX-only, `scsidma.cpp:12`).
// Oracle: MAME `apple/maciifx.cpp` (R. Belmont) — cited per function.
// Blueprint + milestones: `docs/IOP_BRINGUP.md`.
//
// Address map (`maciifx.cpp:191-206`; I/O mirrors $00F00000):
//   $00000000  RAM (ROM overlay until the first ROM-region read —
//              rom_switch_r; the reset vectors come through the overlay)
//   $40000000  ROM 512 KB, mirrored ($0FF80000)
//   $50000000  VIA1 (783.36 kHz)         $50012000  SWIM PIC host window
//   $50004000  SCC PIC host window       $50018000  BIU (reads 0)
//   $50008000  SCSIDMA                   $5001A000  OSS
//   $50010000  ASC                       $50024000  bus error — the ROM's
//   $50040000  VIA1 mirror                          "am I an FMC?" probe
//   $F9000000+ NuBus (slot 9 = Toby video by default)
//
// OSS inputs (`maciifx.cpp:329-374`): 0-5 = NuBus slots 9-E, 6 = SWIM PIC,
// 7 = SCC PIC, 8 = ASC, 9 = SCSIDMA, 10 = 60.15 Hz tick (also pulses VIA1
// CA1; acked by writing OSS $207), 11 = VIA1. regs[0..15] hold each
// input's IPL level; $202/$203 are the pending flags.
//
// No HMMU and no GLUE 24-bit remap: the IIfx is 32-bit clean, 24-bit
// compatibility is the 030 PMMU's job (Moira translates when TC bit 31
// is set — nothing to do here).
//
// Clock domains: the CPU runs at 40 MHz; every peripheral lives on C15M
// (15.6672 MHz — PIC input clock, ASC, SWIM, SCC pacing) or C15M/20 (the
// VIA). tick() converts with integer accumulators (no drift, remainders
// carried — `pom68k-mcu-lle-clock-drift`).

#pragma once
#include "AdbLine.h"
#include "ApplePic.h"
#include "Asc.h"
#include "DeclRom.h"
#include "NuBus.h"
#include "Ncr5380.h"
#include "Rtc.h"
#include "Scc8530.h"
#include "ScsiDisk.h"
#include "SonyDrive.h"
#include "Swim1.h"
#include "TobyVideo.h"
#include "Via6522.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class IIfxCpu;

class IIfxMemory {
public:
    static constexpr uint32_t kRomSize = 0x80000;    // 512 KB
    static constexpr int64_t  kCpuHz   = 40000000;
    static constexpr int64_t  kC15MHz  = 15667200;
    int64_t cpuHz() const { return kCpuHz; }

    explicit IIfxMemory(uint32_t ramSize = 0x800000);
    ~IIfxMemory();

    bool loadRom(const std::vector<uint8_t>& data);
    void reset();
    bool installTobyVideo(const std::string& declRomPath = {});

    uint8_t  read8(uint32_t addr);
    uint16_t read16(uint32_t addr);
    void     write8(uint32_t addr, uint8_t v);
    void     write16(uint32_t addr, uint16_t v);
    uint8_t  peek8(uint32_t addr) const;

    void setCpu(IIfxCpu* cpu) { cpu_ = cpu; }
    void updateIrq();
    int  iplLevel() const;
    void tick(int cpuCycles);

    Via6522& via1() { return via1_; }
    Rtc& rtc() { return rtc_; }
    NuBus& nubus() { return nubus_; }
    TobyVideo* toby() { return toby_; }
    AscV8& asc() { return asc_; }
    ApplePic& sccPic() { return sccPic_; }
    ApplePic& swimPic() { return swimPic_; }
    Scc8530& scc() { return scc_; }
    Swim1& swim() { return swim_; }
    Ncr5380& scsi() { return scsi_; }
    AdbLine& adbLine() { return adbLine_; }
    SonyDrive& internalDrive() { return drive_; }

    // Input events (UI thread → machine): the ADB devices hang off the
    // SWIM PIC's bit-banged line, LLE on both ends of the wire.
    void keyEvent(uint8_t code, bool down) { adbLine_.keyEvent(code, down); }
    void mouseMove(int dx, int dy) { adbLine_.mouseMove(dx, dy); }
    void mouseButton(bool down) { adbLine_.mouseButton(down); }
    bool insertDisk(const std::string& path) { return drive_.insert(path); }
    // Mechanical drive sounds (GUI only; headless leaves the sinks null).
    void attachDriveSounds(FloppySoundSink* floppy, FloppySoundSink* hdd) {
        drive_.setSoundSink(floppy);
        for (ScsiDisk& d : scsiDisks_) d.setSoundSink(hdd);
    }
    bool overlay() const { return overlay_; }
    uint8_t ossReg(int off) const { return ossRegs_[off & 0x3FF]; }
    long vblPulses() const { return vblPulses_; }
    long adbHostEdges() const { return adbHostEdges_; }

    bool attachCdrom(const std::string& path, int id = 3) {
        if (id < 0 || id > 6 || !scsiDisks_[id].openCdrom(path)) return false;
        scsi_.attach(&scsiDisks_[id], id);
        return true;
    }
    bool attachScsi(const std::string& path, bool writeBack = false, int id = 0) {
        if (id < 0 || id > 6 || !scsiDisks_[id].open(path, writeBack)) return false;
        // Mirror on every ID (the MacIIMemory boot-scan workaround).
        for (int i = 0; i < 7; i++)
            scsi_.attach(&scsiDisks_[id], i);
        return true;
    }

    // ── Save states (SaveState.h) ───────────────────────────────────────
    uint32_t ramBytes() const { return ramSize_; }
    uint32_t romChecksum() const {
        if (rom_.size() < 4) return 0;
        return uint32_t(rom_[0]) << 24 | uint32_t(rom_[1]) << 16
             | uint32_t(rom_[2]) << 8  | uint32_t(rom_[3]);
    }
    template <class Ar> void visit(Ar& ar) {
        ar.blob(ram_);
        ar(via1_, rtc_, nubus_, asc_, sccPic_, swimPic_, scc_, swim_,
           drive_, scsi_, adbLine_);
        for (auto& d : scsiDisks_) ar(d);
        std::uint8_t hasToby = toby_ != nullptr;
        ar(hasToby);
        if constexpr (Ar::loading) {
            if ((toby_ != nullptr) != (hasToby != 0)) { ar.fail(); return; }
        }
        if (toby_) ar(*toby_);
        ar(ossRegs_, ramSize_, overlay_, scsiDmaCtl_, scsiDmaCount_,
           scsiDmaAddr_, viaPhase_, c15Acc_, tickAcc_, secAcc_);
    }

private:
    bool isIo(uint32_t addr, uint32_t& off) const;
    void viaSync();
    uint16_t viaAccess(uint32_t addr, bool write, uint16_t v);
    void refreshVia1PortB();
    uint8_t read8Decoded(uint32_t addr);
    void write8Decoded(uint32_t addr, uint8_t v);
    uint8_t scsiDmaRead(uint32_t off);
    void scsiDmaWrite(uint32_t off, uint8_t v);
    void ossSetInput(int n, bool state);
    uint8_t ossRead(uint32_t off);
    void ossWrite(uint32_t off, uint8_t v);
    [[noreturn]] void busError() const;

    std::vector<uint8_t> ram_, rom_;
    Via6522 via1_;
    Rtc rtc_;
    NuBus nubus_;
    TobyVideo* toby_ = nullptr;
    AscV8 asc_{0x00};                // discrete ASC, like the Mac II
    ApplePic sccPic_, swimPic_;
    AdbLine adbLine_;
    Scc8530 scc_;
    Swim1 swim_;
    SonyDrive drive_;
    Ncr5380 scsi_;
    ScsiDisk scsiDisks_[7];
    IIfxCpu* cpu_ = nullptr;

    uint8_t ossRegs_[0x400] = {};
    uint32_t ramSize_;
    bool overlay_ = true;
    // SCSIDMA control shadow (M3: 5380 + PIO/soft handshake; real DMA and
    // the restartable handshake stall are M4 — `docs/IOP_BRINGUP.md`).
    uint32_t scsiDmaCtl_ = 0;
    uint32_t scsiDmaCount_ = 0;
    uint32_t scsiDmaAddr_ = 0;
    static constexpr uint32_t kScsiIrqEn = 0x0002;   // CTRL_IRQEN
    static constexpr uint32_t kScsiScIrq = 0x0040;   // CTRL_SCIRQEN (status)

    int viaPhase_ = 0;
    int64_t c15Acc_ = 0;             // 40 MHz → C15M remainder
    int64_t tickAcc_ = 0;            // C15M cycles toward the 60.15 Hz tick
    int64_t secAcc_ = 0;             // C15M cycles toward the 1 Hz tick
    long vblPulses_ = 0;
    long adbHostEdges_ = 0;
};
