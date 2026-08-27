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
#include "CoreConfig.h"
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
#include "jit/JitGuard.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class IIfxCpu;

class IIfxMemory {
public:
    // The registry this machine reports its LLE/HLE outcomes into.
    // Injected with CoreConfig so a session owns it instead of the
    // process (2026-08-27); save states and the Périphériques window
    // read it back from the machine rather than from a global.
    pom68k::lle::Registry& lleRegistry() const { return *lle_; }

    static constexpr uint32_t kRomSize = 0x80000;    // 512 KB
    static constexpr int64_t  kCpuHz   = 40000000;
    static constexpr int64_t  kC15MHz  = 15667200;
    int64_t cpuHz() const { return kCpuHz; }

    explicit IIfxMemory(
        const pom68k::CoreConfig& coreConfig, uint32_t ramSize = 0x800000);
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

    // ── JIT memory hooks (src/jit/POM68K_JIT.md § 4) ────────────────────
    // The simplest map in the tree for a code window: the IIfx is 32-bit
    // clean, with no HMMU and no GLUE 24-bit remap (24-bit compatibility is
    // the 030 PMMU's job), so what pomJitProbeCode reports IS the bus
    // address and there is nothing to reconcile — unlike the GLUE and V8
    // boards, which both had to refuse their own remapped aliases.
    //
    // Refused: the whole map while the overlay is up. That is not caution,
    // it is the same rule V8Memory follows for the same reason — on this
    // machine a ROM-region READ is what drops the overlay (rom_switch_r,
    // maciifx.cpp:169-186), and a windowed fetch performs no read, so
    // serving it would leave the map latched in its boot state forever.
    // Also refused: all of $50xxxxxx I/O and NuBus.
    const uint8_t* codeSpan(uint32_t phys, uint32_t& len) const;
    uint8_t* dataSpan(uint32_t phys, uint32_t& len, bool write);
    void setJitGuard(jit::CodeGuard* g) { jitGuard_ = g; }
    // The overlay drop, which is a read side effect and therefore the one
    // map move no write guard could ever see.
    void jitMapChanged();

    void updateIrq();
    int  iplLevel() const;
    void tick(int cpuCycles);

    Via6522& via1() { return via1_; }
    Rtc& rtc() { return rtc_; }
    // Battery file (Rtc.h). The IIfx keeps a discrete RTC despite its two
    // IOPs — the PICs carry the ADB and serial firmware, not the PRAM.
    bool loadPram(const std::string& path) { return rtc_.loadPram(path); }
    void savePram(const std::string& path) { rtc_.savePram(path); }
    NuBus& nubus() { return nubus_; }
    TobyVideo* toby() { return toby_; }
    AscV8& asc() { return asc_; }
    ApplePic& sccPic() { return sccPic_; }
    ApplePic& swimPic() { return swimPic_; }
    Scc8530& scc() { return scc_; }
    Swim1& swim() { return swim_; }
    Ncr5380& scsi() { return scsi_; }
    // The boot disk's image, as the sibling memories expose it (the beyond
    // gate samples the HFS catalog through it).
    ScsiDisk& scsiDisk(int id = 0) { return scsiDisks_[id]; }
    AdbLine& adbLine() { return adbLine_; }
    SonyDrive& internalDrive() { return drive_; }

    // Input events (UI thread → machine): the ADB devices hang off the
    // SWIM PIC's bit-banged line, LLE on both ends of the wire.
    void keyEvent(uint8_t code, bool down) { adbLine_.keyEvent(code, down); }
    void mouseMove(int dx, int dy) { adbLine_.mouseMove(dx, dy); }
    void mouseButton(bool down, int button = 0) { adbLine_.mouseButton(down, button); }
    bool insertDisk(const std::string& path) { return drive_.insert(path); }
    void ejectDisk() { drive_.eject(); }
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
    // An empty CD drive on the bus at boot: the ROM's probe sees it, the
    // driver polls it, and media hot-inserted later mounts without a
    // reboot (ScsiDisk raises UNIT ATTENTION / $28 on the change).
    bool attachCdromEmpty(int id) {
        if (id < 1 || id > 6) return false;
        scsiDisks_[id].attachCdromEmpty();
        scsi_.attach(&scsiDisks_[id], id);
        return true;
    }
    bool bayIsCdrom(int id) const {
        return id >= 1 && id <= 6 && scsiDisks_[id].cdrom()
            && scsiDisks_[id].present();
    }
    // Media in/out of an existing CD bay, mid-run (machine thread only).
    bool insertBayMedia(int id, const std::string& path) {
        return bayIsCdrom(id) && scsiDisks_[id].openCdrom(path);
    }
    void ejectBayMedia(int id) {
        if (bayIsCdrom(id)) scsiDisks_[id].eject();
    }
    bool attachScsi(const std::string& path, bool writeBack = false, int id = 0) {
        if (id < 0 || id > 6 || !scsiDisks_[id].open(path, writeBack)) return false;
        // One ID, one target. The MacIIMemory all-ID mirror was copied here
        // during bring-up, but the IIfx path does NOT dedupe the way the
        // Mac II ROM does ($B2E): System 7.6.1 installed seven drivers
        // (refNums -33..-39) and mounted the SAME volume seven times —
        // seven VCBs writing one store corrupted it (POM68KProber report,
        // 2026-08-04). The empty-ID selection timeouts the mirror avoided
        // are the price of a truthful bus.
        scsi_.attach(&scsiDisks_[id], id);
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
        if constexpr (Ar::loading) {
            // RAM and the overlay state just changed wholesale, which no
            // write can express (JitGuard.h § invalidate).
            if (jitGuard_) jitGuard_->invalidate();
        }
    }

private:
    pom68k::lle::Registry* lle_ = &pom68k::lle::processRegistry();

    bool ioTrace_ = false;
    bool adbTrace_ = false;
    bool scsiTrace_ = false;
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
    jit::CodeGuard* jitGuard_ = nullptr;   // not serialized: machine wiring

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
