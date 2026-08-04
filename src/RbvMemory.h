// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Mac IIsi / IIci memory map + RBV (RAM-Based Video) ──
// RBV is the direct ancestor of the V8/VASP/Sonora line (MAME rbv.cpp:6-9)
// — it originated the pseudo-VIA and the video-out-of-system-RAM those
// chips inherited. The IIsi (MAME maciici.cpp "Erickson/Rafiki/Hobie Cat")
// is the IIci cost-reduced: 68030 @ 20 MHz, Egret ADB (344S0100) instead
// of the PIC ADB modem + discrete RTC, same RBV/MDU core. Map (maciici.cpp
// maciici_map, all windows mirror $00F00000): contiguous RAM at
// $00000000, the 512 KB ROM at $40000000 (mirrored; any read clears the
// boot overlay, rom_switch_r), VIA1 at +$00000 AND +$40000, SCC +$04000,
// SCSI 5380 +$10000 with pseudo-DMA at +$06000/+$12000, discrete ASC
// (version $00, the Mac II cell) +$14000, SWIM1 +$16000, RBV +$24000 =
// Bt478 DAC (MSB byte lane of each longword — the Ariel register model)
// and +$26000 = pseudo-VIA. There is NO machine-ID longword: the ROM
// reads VIA1 PA = $97 (via_in_a_iisi $96 | diag-disabled bit 0) and the
// monitor sense through the pseudo-VIA video-config hook (montype << 3).
// The framebuffer is the START of system RAM (rbv.cpp set_ram_info).
// Unmapped I/O reads back 0 (MAME parity — the LC III+ lesson).
//
// The **Mac IIci** (`iici=true`) is the same RBV map with a different ADB +
// clock front end (MAME `maciici.cpp maciici`): instead of the IIsi's Egret
// it carries the **ADB modem** — a PIC1654S transceiver (`AdbVia`, firmware
// `roms/adbmodem/342s0440-b.bin`) on VIA1 CB1/CB2 + PB4/PB5 — and a
// **discrete RTC** (343-0042) on VIA1 PB0-2/CA2, exactly the Centris/Mac II
// wiring. VIA1 PA reads `$C7` — MAME's `0xC6 | BIT(config,1)` with the
// diagnostic disabled, so PA0 = 1; reading back `$C6` puts the ROM in its
// VIA-T2 burn-in loop forever (RbvMemory.cpp:168). PB reads the RTC
// serial data (PB0) + /ADB-IRQ
// (PB3); there is no MCU reset-hold, so the 68030 runs from power-on. Its
// three NuBus slots read 0 (empty, MAME-unmapped parity). 68030 @ 25 MHz,
// ROM `$368CADFE`.
// Sources: MAME maciici.cpp + rbv.cpp + adbmodem.cpp (fetched 2026-07-25).
// Gates: tests/iisi_boot_etalon.cpp, tests/iici_boot_etalon.cpp.

#pragma once
#include "jit/JitGuard.h"
#include "Via6522.h"
#include "PseudoVia.h"
#include "Egret.h"
#include "CudaLle.h"
#include "AdbBus.h"
#include "AdbVia.h"
#include "Rtc.h"
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

class RbvCpu;

class RbvMemory {
public:
    static constexpr uint32_t kRomSize = 0x80000;    // 512 KB
    static constexpr int64_t  kCpuHz = 20000000;     // IIsi (maciici.cpp:654)
    static constexpr int64_t  kCpuHzCi = 25000000;   // IIci (maciici.cpp:528)
    static constexpr int64_t  kViaHz = 783360;       // C7M / 10

    // iici: the ADB-modem + discrete-RTC front end (no Egret, no reset hold);
    // otherwise the IIsi's Egret 344S0100.
    explicit RbvMemory(uint32_t totalRam = 0x800000, int64_t cpuHz = kCpuHz,
                       bool iici = false);
    int64_t cpuHz() const { return cpuHz_; }
    bool isIIci() const { return iici_; }

    bool loadRom(const std::vector<uint8_t>& data);  // 512 KB flat image
    void reset();

    uint8_t  read8(uint32_t addr);
    uint16_t read16(uint32_t addr);
    void     write8(uint32_t addr, uint8_t v);
    void     write16(uint32_t addr, uint16_t v);
    uint8_t  peek8(uint32_t addr) const;             // side-effect-free

    void setCpu(RbvCpu* cpu) { cpu_ = cpu; }

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
    int iplLevel() const;            // SCC=4 > RBV pseudo-VIA=2 > VIA1=1

    void tick(int cpuCycles);        // VIA timers, 60.15 Hz CA1, VBL, MCU…
    // Firmware-MCU deadline on the IIsi; the IIci's PIC ADB modem has no
    // deadline API yet, so the wrapper's batch cap is its bound.
    int cyclesToNextEvent() const {
        return (!iici_ && egretLleOn_) ? egretLle_.cyclesToNextEvent()
                                       : 0x7fffffff;
    }

    Via6522& via1() { return via_; }
    PseudoVia& pseudoVia() { return pvia_; }
    Egret& egret() { return egret_; }
    AdbBus& adb() { return adb_; }
    AdbVia& adbVia() { return adbVia_; }   // IIci PIC1654S ADB modem
    Rtc& rtc() { return rtc_; }            // IIci discrete 343-0042
    Ariel& dac() { return dac_; }    // Bt478 — the Ariel register model
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
    Swim1& swim() { return swim_; }
    SonyDrive& internalDrive() { return drive_; }
    bool insertDisk(const std::string& path) { return drive_.insert(path); }
    void ejectDisk() { drive_.eject(); }
    void attachDriveSounds(FloppySoundSink* floppy, FloppySoundSink* hdd) {
        drive_.setSoundSink(floppy);
        for (ScsiDisk& d : scsiDisks_) d.setSoundSink(hdd);
    }
    bool cpuHeld() const {
        // The IIci's ADB modem does not gate the CPU — it runs from power-on
        // (MAME machine_reset only holds when an Egret is present).
        if (iici_) return false;
        return egretLleOn_ ? egretLle_.cpuHeld() : egret_.cpuHeld();
    }
    bool egretLleActive() const { return !iici_ && egretLleOn_; }
    bool adbLleActive() const { return iici_ && adbVia_.lle(); }
    CudaLle& egretLle() { return egretLle_; }
    uint8_t pramByte(int i) const {
        if (iici_) return rtc_.xpram(uint8_t(i));
        return egretLleOn_ ? egretLle_.pram(i) : egret_.pram(i);
    }
    void setPramByte(int i, uint8_t v) {
        if (iici_)            rtc_.setXpram(uint8_t(i), v);
        else if (egretLleOn_) egretLle_.setPram(i, v);
        else                  egret_.setPram(i, v);
    }
    // Host wall clock. MUST branch on the LLE flag like loadPram/keyEvent do:
    // seeding only the HLE object left the firmware MCU's own seconds counter
    // untouched, so every default (firmware-LLE) boot started at the 1904
    // epoch and wrote that back to the battery file.
    void setRtcSeconds(uint32_t s) {
        egret_.setSeconds(s);
        egretLle_.setSeconds(s);
    }
    bool loadPram(const std::string& path);
    void savePram(const std::string& path);
    void keyEvent(uint8_t code, bool down) {
        if (iici_)            adbVia_.keyEvent(code, down);
        else if (egretLleOn_) egretLle_.adbLine().keyEvent(code, down);
        else                  adb_.keyEvent(code, down);
    }
    void mouseMove(int dx, int dy) {
        if (iici_)            adbVia_.mouseMove(dx, dy);
        else if (egretLleOn_) egretLle_.adbLine().mouseMove(dx, dy);
        else                  adb_.mouseMove(dx, dy);
    }
    void mouseButton(bool down, int button = 0) {
        if (iici_)            adbVia_.mouseButton(down, button);
        else if (egretLleOn_) egretLle_.adbLine().mouseButton(down, button);
        else if (button == 0) adb_.mouseButton(down);
    }
    bool overlay() const { return overlay_; }
    Scc8530& scc() { return scc_; }
    void sccIrqLine(bool s) { sccIrq_ = s; updateIrq(); }
    void ascIrq(bool s)  { pvia_.ascIrq(s);  updateIrq(); }
    void scsiIrq(bool s) { pvia_.scsiIrq(s); updateIrq(); }
    void scsiDrq(bool s) { pvia_.scsiDrq(s); updateIrq(); }

    // ── RBV video state (decoder in RbvVideo.h) ──
    // The framebuffer is system RAM from offset 0 (rbv.cpp screen_update
    // reads m_ram_ptr); rows are packed at hres × bpp / 8.
    const uint8_t* framebuffer() const { return ram_.data(); }
    uint8_t videoConfig() const { return videoConfig_; }    // bit 6 = off
    uint8_t videoDepth() const { return uint8_t(videoConfig_ & 7); }
    // ── Raster geometry (VideoBeam.h) ───────────────────────────────────
    // The SAME accumulator that generates the VBL — the beam is a pure
    // function of it, never a second clock.
    int64_t framePos() const { return framePos_; }
    int64_t frameCycles() const { return frameCycles_; }
    int64_t frameActiveCycles() const { return vblStart_; }
    int     frameTotalLines() const { return frameTotalLines_; }
    uint64_t frameCount() const { return frameCount_; }

    uint8_t monitorSense() const { return montype_; }
    // Plug a different monitor (1 = 640×870 portrait mono, 2 = 512×384
    // 12" RGB, 6 = 640×480 13" RGB — rbv.cpp MONTYPE); read back through
    // the pseudo-VIA video hook as montype << 3.
    void setMonitorSense(uint8_t m) { montype_ = m; recalcFrame(); }

    // A Mac ROM's first longword IS its checksum (V8Memory pattern).
    uint32_t romChecksum() const {
        if (rom_.size() < 4) return 0;
        return uint32_t(rom_[0]) << 24 | uint32_t(rom_[1]) << 16
             | uint32_t(rom_[2]) << 8  | uint32_t(rom_[3]);
    }

    // ── Save states: the machine chunk (V8Memory pattern) ───────────────
    // No VRAM chunk: on RBV the framebuffer IS system RAM. Both ADB front
    // ends travel (Egret HLE+LLE and the IIci's AdbVia/PIC + discrete RTC)
    // — the unused set is idle and costs bytes, not correctness. Out:
    // rom_, cpuHz_/iici_/egretLleOn_ (construction), cpu_/jitGuard_.
    template <class Ar> void visit(Ar& ar) {
        ar.blob(ram_);
        ar(via_, pvia_, egret_, egretLle_, adbVia_, rtc_, adb_, dac_,
           asc_, scsi_, swim_, drive_, scc_);
        for (auto& d : scsiDisks_) ar(d);
        ar(totalRam_, overlay_, sccIrq_, videoConfig_, montype_);
        ar(viaAcc_, tickAcc_, c15Acc_, secAcc_, framePos_, frameCycles_,
           vblStart_, vblState_, frameTotalLines_, frameCount_);
        if constexpr (Ar::loading) {
            if (jitGuard_) jitGuard_->invalidate();
        }
    }

private:
    uint8_t viaAccess8(uint32_t addr, bool write, uint8_t v);
    void viaSync();                  // E-clock stall (maciici.cpp via_sync)
    void recalcFrame();              // VBL geometry from the monitor type
    void refreshVia1PortB();         // IIci: RTC data (PB0) + /ADB IRQ (PB3)
    uint8_t scsiDma_();
    void scsiDmaW_(uint8_t v);
    [[noreturn]] void busError() const;

    std::vector<uint8_t> ram_, rom_;
    Via6522 via_;
    // rbv.cpp:66 wires APPLE_PSEUDOVIA — the base device: the ASC IRQ is
    // edge-latched and the guest's IFR ack sticks.
    PseudoVia pvia_{PseudoVia::Flavour::Base};
    Egret egret_;
    CudaLle egretLle_;               // Egret firmware LLE (344S0100) — IIsi
    bool egretLleOn_ = false;
    const bool iici_;                // IIci: ADB modem + discrete RTC front end
    uint8_t xcvrSession_() const {
        return egretLleOn_ ? egretLle_.xcvrSession() : egret_.xcvrSession();
    }
    AdbBus adb_;
    AdbVia adbVia_;                  // IIci PIC1654S ADB modem (firmware LLE)
    Rtc rtc_;                        // IIci discrete 343-0042 (VIA1 PB0-2/CA2)
    Ariel dac_;                      // Bt478 CLUT (same register model)
    AscV8 asc_{0x00};                // discrete ASC, version $00 (Mac II cell)
    Ncr5380 scsi_;
    ScsiDisk scsiDisks_[7];
    Swim1 swim_;
    SonyDrive drive_;
    Scc8530 scc_;
    RbvCpu* cpu_ = nullptr;

    uint32_t totalRam_;
    int64_t  cpuHz_;
    jit::CodeGuard* jitGuard_ = nullptr;   // JIT code invalidation
    bool overlay_ = true;
    bool sccIrq_ = false;
    uint8_t videoConfig_ = 0;
    uint8_t montype_ = 6;            // 640×480 13" RGB

    // Clock dividers: VIA φ2, 60.15 Hz CA1, ASC drain + SWIM1 C15M cells,
    // montype-driven VBL.
    int64_t viaAcc_ = 0;
    int64_t tickAcc_ = 0;
    int64_t c15Acc_ = 0;
    int64_t secAcc_ = 0;             // IIci RTC 1 Hz accumulator
    int64_t framePos_ = 0;
    int64_t frameCycles_ = 0, vblStart_ = 0;
    int     frameTotalLines_ = 0;    // vtot of the sensed monitor
    uint64_t frameCount_ = 0;        // completed frames (raster beam seq)
    bool vblState_ = false;
};
