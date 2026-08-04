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
#include "Se30Video.h"
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
    int64_t cpuHz() const { return kCpuHz; }         // LocalTalk pace / 60 Hz quantum

    // Same GLUE board, ROM-sharing models (MAME macii.cpp): the plain
    // **Mac II** / II FDHD (68020 + HMMU), and the 68030 variants **IIx**,
    // **IIcx** and **SE/30** — distinguished only by the VIA1 PA / VIA2 PB
    // machine-ID pins (iix_via2_in_b $87; iicx_via_in_a $C1; the SE/30 shows
    // both). All boot the mac2fdhd ROM. The SE/30 is the compact IIx: no
    // NuBus slots, internal 512×342 video on pseudo-slot $E (Se30Video).
    enum class Model { MacII, IIx, IIcx, SE30 };

    explicit MacIIMemory(uint32_t ramSize = 0x800000, Model model = Model::MacII);
    Model model() const { return model_; }
    bool is030() const { return model_ != Model::MacII; }
    ~MacIIMemory();

    bool loadRom(const std::vector<uint8_t>& data);
    void reset();
    bool installTobyVideo(const std::string& declRomPath = {});
    // SE/30 internal video on pseudo-slot $E; the 8 KB se30vrom.uk6 dump is
    // its declaration ROM (linear, byteLanes $0F — no Toby descrambling).
    bool installSe30Video(const std::string& vromPath = {});

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
    Se30Video* se30() { return se30_; }

    // ── Raster geometry (VideoBeam.h) ───────────────────────────────────
    // The SE/30's internal video has no CRTC of its own, so its beam rides
    // the SAME 60 Hz accumulator that raises VIA1 CA1 and the slot-$E VBL —
    // one source of frame time, never a second clock. (The Toby card on the
    // Mac II proper owns its own CRTC clock and does not use these.)
    int64_t framePos() const { return tickAcc_; }
    int64_t frameCycles() const { return kCpuHz / 60; }
    uint64_t frameCount() const { return frameCount_; }
    AdbBus& adb() { return adb_; }
    AdbVia& adbVia() { return adbVia_; }
    AscV8& asc() { return asc_; }
    Ncr5380& scsi() { return scsi_; }
    ScsiDisk& scsiDisk() { return scsiDisks_[0]; }
    // Mount a CD image at a SCSI ID. Call AFTER attachScsi: the disk is
    // mirrored across every ID below (a boot-scan workaround), so the CD
    // must overwrite its own slot afterwards to be seen as a CD.
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
        // Mirror on every ID so StartBoot's 7→0 scan does not burn multi-
        // second BSY waits on empty targets ($40826CC0). Deduping is left
        // to the ROM ($B2E); we still prefer a single open image.
        for (int i = 0; i < 7; i++)
            scsi_.attach(&scsiDisks_[id], i);
        return true;
    }
    Iwm& iwm() { return iwm_; }
    SonyDrive& internalDrive() { return drive_; }
    // Mechanical drive sounds (GUI only; headless leaves sinks null).
    void attachDriveSounds(FloppySoundSink* floppy, FloppySoundSink* hdd) {
        drive_.setSoundSink(floppy);
        for (ScsiDisk& d : scsiDisks_) d.setSoundSink(hdd);
    }
    Scc8530& scc() { return scc_; }
    Rtc& rtc() { return rtc_; }
    bool insertDisk(const std::string& path) { return drive_.insert(path); }
    void ejectDisk() { drive_.eject(); }
    bool overlay() const { return overlay_; }
    bool hmmu24() const { return hmmu24_; }
    uint8_t nubusIrqState() const { return nubusIrqState_; }
    long vblPulses() const { return vblPulses_; }
    long tickCalls() const { return tickCalls_; }
    long vblPulseNoIrq() const { return vblPulseNoIrq_; }

    void keyEvent(uint8_t code, bool down) { adbVia_.keyEvent(code, down); }
    void mouseMove(int dx, int dy) { adbVia_.mouseMove(dx, dy); }
    void mouseButton(bool down, int button = 0) { adbVia_.mouseButton(down, button); }

    // Soft-post keyDown Return into EvQ (ADB modem may be wedged). Used to
    // clear Sys7 AppleTalk CautionAlerts when EtherTalk is selected but no
    // NuBus ethernet is present.

    // ── Save states (SaveState.h) ───────────────────────────────────────
    uint32_t ramBytes() const { return ramSize_; }
    // A Mac ROM's first longword IS its checksum (V8Memory pattern).
    uint32_t romChecksum() const {
        if (rom_.size() < 4) return 0;
        return uint32_t(rom_[0]) << 24 | uint32_t(rom_[1]) << 16
             | uint32_t(rom_[2]) << 8  | uint32_t(rom_[3]);
    }
    // The machine chunk. The Toby card is optional wiring (installTobyVideo),
    // so its presence is recorded and must match on restore — a snapshot
    // taken with a card cannot load into a machine without one. Out:
    // rom_/model_ (profile identity), cpu_ (pointer), the vbl debug longs.
    template <class Ar> void visit(Ar& ar) {
        ar.blob(ram_);
        ar(via1_, via2_, rtc_, nubus_, adbVia_, adb_, asc_,
           scsi_, iwm_, drive_, scc_, mouse_);
        for (auto& d : scsiDisks_) ar(d);
        std::uint8_t hasToby = toby_ != nullptr;
        ar(hasToby);
        if constexpr (Ar::loading) {
            if ((toby_ != nullptr) != (hasToby != 0)) { ar.fail(); return; }
        }
        if (toby_) ar(*toby_);
        ar(ramSize_, overlay_, glueRamSize_, nubusIrqState_, sccIrq_,
           via2Irq_, via2Pb7_, hmmu24_, viaPhase_, tickAcc_, secAcc_,
           frameCount_);
        // SE/30-only tail: the header pins the profile, so the II/IIx/IIcx
        // chunk layout (and their existing snapshots) is untouched.
        if (model_ == Model::SE30) {
            if constexpr (Ar::loading) {
                if (!se30_) { ar.fail(); return; }
            }
            if (se30_) ar(*se30_);
            ar(se30VblEnable_, se30VblPhase_);
        }
    }

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
    // True when Slot Manager has a CA1 ($D04) task for this VIA2 PA bit —
    // raising VIA2 CA1 with an empty queue is SysError(51) at ROM $408062DC.
    bool via2Ca1SlotTaskArmed(int bit) const;
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
    Se30Video* se30_ = nullptr;
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
    Model model_ = Model::MacII;
    bool overlay_ = true;
    uint8_t glueRamSize_ = 0x00;             // MAME: via2_out_a(0x3f) → 0
    uint8_t nubusIrqState_ = 0x3F;
    bool sccIrq_ = false;
    bool via2Irq_ = false;
    bool via2Pb7_ = true;                    // last VIA2 PB7 level (→ VIA1 CA1)
    bool hmmu24_ = false;                    // VIA2 PB3=0 → M68K_HMMU_ENABLE_II
    // SE/30 VBL as pseudo-slot $E (MAME se30_via_out_b / vblank_irq): VIA1
    // PB6=0 enables; the phase toggle asserts every other frame, the driver
    // clears by flipping PB6.
    bool se30VblEnable_ = false;
    bool se30VblPhase_ = false;
    int viaPhase_ = 0;
    int64_t tickAcc_ = 0;
    uint64_t frameCount_ = 0;        // completed 60 Hz frames (raster seq)
    int64_t secAcc_ = 0;
    long vblPulses_ = 0;
    long tickCalls_ = 0;
    long vblPulseNoIrq_ = 0;
};
