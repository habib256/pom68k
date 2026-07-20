// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Mac II GLUE map + Apple HMMU (VIA2 PB3): RAM/overlay, ROM @$40000000,
// I/O @$50xxxxxx, NuBus. Source: MAME macii.cpp / m68kmmu.h (2026-07-20).

#pragma once
#include "Via6522.h"
#include "Rtc.h"
#include "NuBus.h"
#include "TobyVideo.h"
#include "DeclRom.h"
#include "AdbVia.h"
#include "AdbBus.h"
#include "Asc.h"
#include "Ncr5380.h"
#include "ScsiDisk.h"
#include "Iwm.h"
#include "SonyDrive.h"
#include "Scc8530.h"
#include "MacInput.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class Cpu020;

class MacIIMemory {
public:
    static constexpr uint32_t kRomSize = 0x40000;    // 256 KB
    static constexpr int64_t  kCpuHz   = 15667200;

    explicit MacIIMemory(uint32_t ramSize = 0x800000);
    ~MacIIMemory();

    bool loadRom(const std::vector<uint8_t>& data);
    void reset();
    bool installTobyVideo(const std::string& declRomPath = {});

    uint8_t  read8(uint32_t addr);
    uint16_t read16(uint32_t addr);
    void     write8(uint32_t addr, uint8_t v);
    void     write16(uint32_t addr, uint16_t v);
    uint8_t  peek8(uint32_t addr) const;

    void setCpu(Cpu020* cpu) { cpu_ = cpu; }
    void updateIrq();
    int  iplLevel() const;
    void tick(int cpuCycles);

    Via6522& via1() { return via1_; }
    Via6522& via2() { return via2_; }
    NuBus& nubus() { return nubus_; }
    TobyVideo* toby() { return toby_; }
    AdbBus& adb() { return adb_; }
    AdbVia& adbVia() { return adbVia_; }
    AscV8& asc() { return asc_; }
    Ncr5380& scsi() { return scsi_; }
    ScsiDisk& scsiDisk() { return scsiDisks_[0]; }
    bool attachScsi(const std::string& path, bool writeBack = false, int id = 0) {
        if (id < 0 || id > 6 || !scsiDisks_[id].open(path, writeBack)) return false;
        // Mirror on every ID so StartBoot's 7→0 scan does not burn multi-
        // second BSY waits on empty targets ($40826CC0). Deduping is left
        // to the ROM ($B2E); we still prefer a single open image.
        for (int i = 0; i < 7; i++)
            scsi_.attach(&scsiDisks_[id], i);
        return true;
    }
    Iwm& iwm() { return iwm_; }
    bool insertDisk(const std::string& path) { return drive_.insert(path); }
    bool overlay() const { return overlay_; }
    bool hmmu24() const { return hmmu24_; }
    uint8_t nubusIrqState() const { return nubusIrqState_; }
    long vblPulses() const { return vblPulses_; }
    long tickCalls() const { return tickCalls_; }
    long vblPulseNoIrq() const { return vblPulseNoIrq_; }

    void keyEvent(uint8_t code, bool down) { adbVia_.keyEvent(code, down); }
    void mouseMove(int dx, int dy) { adbVia_.mouseMove(dx, dy); }
    void mouseButton(bool down) { adbVia_.mouseButton(down); }

    // Soft-post keyDown Return into EvQ (ADB modem may be wedged). Used to
    // clear Sys7 AppleTalk CautionAlerts when EtherTalk is selected but no
    // NuBus ethernet is present.
    void postKeyReturn();
    void maybeDismissBootAlerts();

private:
    bool isIo(uint32_t addr, uint32_t& off) const;
    void viaSync();
    uint16_t viaAccess(Via6522& via, uint32_t addr, bool write, uint16_t v,
                       bool isVia1);
    void refreshVia1PortB();
    void refreshVia2PortA();
    void updateHmmuFromVia2();
    uint32_t physAddr(uint32_t addr) const;
    uint8_t read8Decoded(uint32_t addr);
    void write8Decoded(uint32_t addr, uint8_t v);
    void nubusSlotIrq(int slot, bool active);
    // True when Slot Manager has a CA1 ($D04) task — raising VIA2 CA1 with
    // an empty queue is SysError(51) at ROM $408062DC.
    bool via2Ca1SlotTaskArmed() const;
    uint32_t peek32(uint32_t addr) const {
        return (uint32_t(peek8(addr) << 24) | (uint32_t(peek8(addr + 1)) << 16) |
                (uint32_t(peek8(addr + 2)) << 8) | peek8(addr + 3));
    }
    void applyRamBank();
    uint8_t* ramAt(uint32_t addr);
    const uint8_t* ramAt(uint32_t addr) const;
    [[noreturn]] void busError() const;
    uint8_t scsiDma(bool berIfNoDrq = true);
    void scsiDmaW(uint8_t v, bool berIfNoDrq = true);

    std::vector<uint8_t> ram_, rom_;
    Via6522 via1_, via2_;
    Rtc rtc_;
    NuBus nubus_;
    TobyVideo* toby_ = nullptr;
    AdbVia adbVia_;
    AdbBus adb_;
    AscV8 asc_{0x00};   // Mac II discrete ASC (version $00), not V8
    Ncr5380 scsi_;
    ScsiDisk scsiDisks_[7];
    Iwm iwm_;
    SonyDrive drive_;
    Scc8530 scc_;
    MacMouse mouse_;
    Cpu020* cpu_ = nullptr;

    uint32_t ramSize_;
    bool overlay_ = true;
    uint8_t glueRamSize_ = 0x00;             // MAME: via2_out_a(0x3f) → 0
    uint8_t nubusIrqState_ = 0x3F;
    bool sccIrq_ = false;
    bool via2Irq_ = false;
    bool via2Pb7_ = true;                    // last VIA2 PB7 level (→ VIA1 CA1)
    bool hmmu24_ = false;                    // VIA2 PB3=0 → M68K_HMMU_ENABLE_II
    int viaPhase_ = 0;
    int64_t tickAcc_ = 0;
    int64_t secAcc_ = 0;
    long vblPulses_ = 0;
    long tickCalls_ = 0;
    long vblPulseNoIrq_ = 0;

    // Sys7 AppleTalk modal dismiss (see maybeDismissBootAlerts)
    long scsiStallFrames_ = 0;
    long lastScsiCmds_ = -1;
    int alertDismissPosts_ = 0;
    int alertDismissCool_ = 0;
    int alertEvSlot_ = 0;
};
