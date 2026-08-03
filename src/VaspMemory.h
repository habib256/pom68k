// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Mac IIvx / IIvi memory map + VASP gate array ──
// VASP is "V8 video on Sonora addressing" (MAME vasp.cpp:35 — like Sonora
// it puts contiguous RAM at $00000000): the 1 MB ROM at $40000000
// (mirrored ×16, any read clears the boot overlay), I/O at $50000000
// (VIA1, SCC, SCSI 5380 + pseudo-DMA, ASC-V8, SWIM1, Ariel DAC at
// +$24000, pseudo-VIA at +$26000 with the V8-style video-config /
// monitor-sense hooks), the machine ID longword at $5FFFFFFC ($A55A2015 =
// IIvx, $A55A2016 = IIvi) and 1 MB VRAM at $60000000 with a FIXED
// 2048-byte row pitch (vasp.cpp screen_update — the V8 uses 1024).
// The ADB MCU is the LC III's Egret 341S0851 (maciivx.cpp:406). CPU:
// 68030 @ 31.3344 MHz (IIvx, C32M) or 15.6672 MHz (IIvi, C15M); VIA at
// 783.36 kHz. The three NuBus slots are empty: their space reads 0 with
// no /BERR, MAME-unmapped parity (maciivx boots that way).
// Sources: MAME vasp.cpp/maciivx.cpp (fetched 2026-07-24).
// Gate: tests/iivx_boot_etalon.cpp.

#pragma once
#include "jit/JitGuard.h"
#include "Via6522.h"
#include "PseudoVia.h"
#include "Egret.h"
#include "CudaLle.h"
#include "AdbBus.h"
#include "Ariel.h"
#include "Asc.h"
#include "Ncr5380.h"
#include "ScsiDisk.h"
#include "Swim1.h"
#include "SonyDrive.h"
#include "Scc8530.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class VaspCpu;

class VaspMemory {
public:
    static constexpr uint32_t kRomSize = 0x100000;   // 1 MB
    static constexpr uint32_t kVramSize = 0x100000;  // 1 MB
    static constexpr int64_t  kCpuHzVx = 31334400;   // C32M (IIvx)
    static constexpr int64_t  kCpuHzVi = 15667200;   // C15M (IIvi)
    static constexpr int64_t  kViaHz = 783360;       // C7M / 10
    static constexpr uint32_t kIdIIvx = 0xA55A2015;  // maciivx.cpp:171
    static constexpr uint32_t kIdIIvi = 0xA55A2016;  // maciivx.cpp:177

    explicit VaspMemory(uint32_t totalRam = 0x800000,
                        int64_t cpuHz = kCpuHzVx,
                        uint32_t machineId = kIdIIvx);
    int64_t cpuHz() const { return cpuHz_; }

    bool loadRom(const std::vector<uint8_t>& data);  // 1 MB flat image
    void reset();

    uint8_t  read8(uint32_t addr);
    uint16_t read16(uint32_t addr);
    void     write8(uint32_t addr, uint8_t v);
    void     write16(uint32_t addr, uint16_t v);
    uint8_t  peek8(uint32_t addr) const;             // side-effect-free

    void setCpu(VaspCpu* cpu) { cpu_ = cpu; }

    // ── JIT hooks (src/jit/POM68K_JIT.md, 030 extension 2026-07-28) ────
    // Same contract as Q605Memory's: codeSpan/dataSpan hand the JIT host
    // pointers to PLAIN memory only — flat RAM below the bank top, ROM once
    // the overlay is down (reading the ROM window clears it, a side effect
    // a const probe cannot perform). I/O and the machine-ID longword are
    // refused; a store to ROM is refused.
    const uint8_t* codeSpan(uint32_t phys, uint32_t& len) const;
    uint8_t* dataSpan(uint32_t phys, uint32_t& len, bool write);
    void setJitGuard(jit::CodeGuard* g) { jitGuard_ = g; }
    uint32_t ramBytes() const { return totalRam_; }
    void jitMapChanged();
    void updateIrq();
    int iplLevel() const;            // SCC=4 > pseudo-VIA=2 > VIA1=1

    void tick(int cpuCycles);        // VIA timers, 60.15 Hz CA1, VBL, MCU…

    Via6522& via1() { return via_; }
    PseudoVia& pseudoVia() { return pvia_; }
    Egret& egret() { return egret_; }
    AdbBus& adb() { return adb_; }
    Ariel& ariel() { return ariel_; }
    AscV8& asc() { return asc_; }
    int ascAvailable() const { return asc_.available(); }
    int16_t ascPop() { return asc_.pop(); }
    Ncr5380& scsi() { return scsi_; }
    ScsiDisk& scsiDisk() { return scsiDisks_[0]; }
    bool attachScsi(const std::string& path, bool writeBack = false, int id = 0) {
        if (id < 0 || id > 6 || !scsiDisks_[id].open(path, writeBack)) return false;
        scsi_.attach(&scsiDisks_[id], id);
        return true;
    }
    // Mount a CD image (.iso/.cdr/.toast) at a SCSI ID. MAME puts the
    // Mac's CD-ROM at ID 3 (maciivx.cpp:323); the target stays present
    // across an eject so the guest sees an empty drive, not a gap.
    bool attachCdrom(const std::string& path, int id = 3) {
        if (id < 0 || id > 6 || !scsiDisks_[id].openCdrom(path)) return false;
        scsi_.attach(&scsiDisks_[id], id);
        return true;
    }
    Swim1& swim() { return swim_; }
    SonyDrive& internalDrive() { return drive_; }
    bool insertDisk(const std::string& path) { return drive_.insert(path); }
    void attachDriveSounds(FloppySoundSink* floppy, FloppySoundSink* hdd) {
        drive_.setSoundSink(floppy);
        for (ScsiDisk& d : scsiDisks_) d.setSoundSink(hdd);
    }
    bool cpuHeld() const {
        return egretLleOn_ ? egretLle_.cpuHeld() : egret_.cpuHeld();
    }
    bool egretLleActive() const { return egretLleOn_; }
    CudaLle& egretLle() { return egretLle_; }
    uint8_t pramByte(int i) const {
        return egretLleOn_ ? egretLle_.pram(i) : egret_.pram(i);
    }
    void setPramByte(int i, uint8_t v) {
        if (egretLleOn_) egretLle_.setPram(i, v);
        else             egret_.setPram(i, v);
    }
    // Host wall clock. MUST branch on the LLE flag like loadPram/keyEvent do:
    // seeding only the HLE object left the firmware MCU's own seconds counter
    // untouched, so every default (firmware-LLE) boot started at the 1904
    // epoch and wrote that back to the battery file.
    void setRtcSeconds(uint32_t s) {
        egret_.setSeconds(s);
        egretLle_.setSeconds(s);
    }
    bool loadPram(const std::string& path) {
        bool ok = egret_.loadPram(path);
        if (egretLleOn_)
            for (int i = 0; i < 256; i++) egretLle_.setPram(i, egret_.pram(i));
        return ok;
    }
    void savePram(const std::string& path) {
        if (egretLleOn_)
            for (int i = 0; i < 256; i++) egret_.setPram(i, egretLle_.pram(i));
        egret_.savePram(path);
    }
    void keyEvent(uint8_t code, bool down) {
        if (egretLleOn_) egretLle_.adbLine().keyEvent(code, down);
        else             adb_.keyEvent(code, down);
    }
    void mouseMove(int dx, int dy) {
        if (egretLleOn_) egretLle_.adbLine().mouseMove(dx, dy);
        else             adb_.mouseMove(dx, dy);
    }
    void mouseButton(bool down) {
        if (egretLleOn_) egretLle_.adbLine().mouseButton(down);
        else             adb_.mouseButton(down);
    }
    bool overlay() const { return overlay_; }
    Scc8530& scc() { return scc_; }
    void sccIrqLine(bool s) { sccIrq_ = s; updateIrq(); }
    void ascIrq(bool s)  { pvia_.ascIrq(s);  updateIrq(); }
    void scsiIrq(bool s) { pvia_.scsiIrq(s); updateIrq(); }
    void scsiDrq(bool s) { pvia_.scsiDrq(s); updateIrq(); }

    // ── VASP video state (decoder in VaspVideo.h) ──
    const uint8_t* vram() const { return vram_.data(); }
    uint8_t videoConfig() const { return videoConfig_; }
    uint8_t videoDepth() const { return uint8_t(videoConfig_ & 7); }
    // ── Raster geometry (VideoBeam.h) ───────────────────────────────────
    // The SAME accumulator that generates the VBL below — the beam is a
    // pure function of it, never a second clock. The VASP screen is a
    // fixed 800×525 @ 25.175 MHz with 480 active lines (vasp.cpp), which
    // reduces to a flat 60 Hz here.
    int64_t framePos() const { return framePos_; }
    int64_t frameCycles() const { return cpuHz_ / 60; }
    int64_t frameActiveCycles() const { return cpuHz_ / 60 * 480 / 525; }
    int     frameTotalLines() const { return 525; }
    uint64_t frameCount() const { return frameCount_; }

    uint8_t monitorSense() const { return montype_; }
    // Plug a different monitor (1 = 640×870 portrait, 2 = 512×384 12",
    // 6 = 640×480 13"); read back through the pseudo-VIA video-config
    // hook (vasp.cpp via2_video_config_r = montype << 3).
    void setMonitorSense(uint8_t m) { montype_ = m; }

    // A Mac ROM's first longword IS its checksum (V8Memory pattern).
    uint32_t romChecksum() const {
        if (rom_.size() < 4) return 0;
        return uint32_t(rom_[0]) << 24 | uint32_t(rom_[1]) << 16
             | uint32_t(rom_[2]) << 8  | uint32_t(rom_[3]);
    }

    // ── Save states: the machine chunk (V8Memory pattern) ───────────────
    // Out: rom_, cpuHz_/machineId_/egretLleOn_ (profile identity and MCU
    // wiring the machine's construction owns), cpu_/jitGuard_ (pointers).
    template <class Ar> void visit(Ar& ar) {
        ar.blob(ram_);
        ar.blob(vram_);
        ar(via_, pvia_, egret_, egretLle_, adb_, ariel_, asc_, scsi_,
           swim_, drive_, scc_);
        for (auto& d : scsiDisks_) ar(d);
        ar(totalRam_, overlay_, sccIrq_, videoConfig_, montype_);
        ar(viaAcc_, tickAcc_, c15Acc_, framePos_, vblState_, frameCount_);
        if constexpr (Ar::loading) {
            if (jitGuard_) jitGuard_->invalidate();
        }
    }

private:
    uint8_t viaAccess8(uint32_t addr, bool write, uint8_t v);
    void viaSync();                  // E-clock stall (vasp.cpp via_sync)
    uint8_t scsiDma_();
    void scsiDmaW_(uint8_t v);
    [[noreturn]] void busError() const;

    std::vector<uint8_t> ram_, rom_, vram_;
    Via6522 via_;
    // vasp.cpp:90 wires APPLE_PSEUDOVIA — the base device, not the V8 one,
    // even though VASP takes the rest of its peripherals from the V8 side.
    // Its AscV8 IRQ line is level-sticky, so the level flavour on top of it
    // would make an enabled ASC interrupt unacknowledgeable.
    PseudoVia pvia_{PseudoVia::Flavour::Base};
    Egret egret_;
    CudaLle egretLle_;               // Egret firmware LLE (341S0851)
    bool egretLleOn_ = false;
    uint8_t xcvrSession_() const {
        return egretLleOn_ ? egretLle_.xcvrSession() : egret_.xcvrSession();
    }
    AdbBus adb_;
    Ariel ariel_;
    AscV8 asc_;
    Ncr5380 scsi_;
    ScsiDisk scsiDisks_[7];
    Swim1 swim_;
    SonyDrive drive_;
    Scc8530 scc_;
    VaspCpu* cpu_ = nullptr;

    uint32_t totalRam_;
    int64_t  cpuHz_;
    uint32_t machineId_;
    jit::CodeGuard* jitGuard_ = nullptr;   // JIT code invalidation
    bool overlay_ = true;
    bool sccIrq_ = false;
    uint8_t videoConfig_ = 0;
    uint8_t montype_ = 6;            // 640×480 13" RGB

    // Clock dividers: VIA φ2, 60.15 Hz CA1, ASC drain + SWIM1 C15M/C7M
    // cells (both fed 15.6672 MHz cycles), fixed-60 Hz VBL.
    int64_t viaAcc_ = 0;
    int64_t tickAcc_ = 0;
    int64_t c15Acc_ = 0;
    int64_t framePos_ = 0;
    uint64_t frameCount_ = 0;        // completed frames (raster beam seq)
    bool vblState_ = false;
};
