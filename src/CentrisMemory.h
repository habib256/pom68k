// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Mac Centris/Quadra 610/650 memory map: djMEMC + IOSB ──
// The 680x0-class desktop that condensed the Quadra 700's discrete chips
// into two ASICs (MAME macquadra800.cpp, djmemc.cpp, iosb.cpp — refs in
// ~/src/refs/mame*, pinned lines in the comments). Architecturally it is
// the Q605 (djMEMC = the same family as MEMCjr, IOSB = the PrimeTime
// sibling) with the LC 475's Cuda replaced by the Mac II's discrete
// pair — a real 343-0042 RTC bit-banged on VIA1 PB0-2/CA2 and a PIC1654S
// ADB transceiver on VIA1 PB3-5 + CB1/CB2 (AdbVia, firmware LLE). So the
// map, DAFB, TurboSCSI 53C96, SWIM2, ASC and pseudo-VIA2 are the Q605's
// verbatim; only VIA1's peripherals, the model ID and the boot-hold差 (none —
// no reset-holding MCU, the CPU runs from power-on) differ:
//   $00000000-  RAM (flat; djMEMC banking not modelled), ROM mirror under
//               the boot overlay
//   $40000000-  ROM 1 MB ×16 (first read clears the overlay)
//   $50000000-  IOSB I/O (iosb_base::map): +$00000 VIA1, +$02000 VIA2
//               (Quadra pseudo-VIA), +$08000 ethernet MAC (stub 0),
//               +$0A000 SONIC (stub 0), +$0C000 SCC, +$0E000 djMEMC regs,
//               +$10000 TurboSCSI 53C96, +$10100 TurboSCSI DMA,
//               +$14000 ASC, +$18000 IOSB regs, +$1E000 SWIM2
//   $5FFF0000-  machine ID $A55A2BAD (the "IOSB is always $2BAD"; the
//               model — Centris 610 $40 / 650 $46, Quadra 610 $44 / 650
//               $52 — comes from VIA1 port A pins 1/2/4/6)
//   $F9000000-  VRAM (1 MB), $F9800000- DAFB II register cell
// Unmapped I/O reads back 0 (MAME iosb has no catch-all /BERR).

#pragma once
#include "jit/JitGuard.h"
#include "Via6522.h"
#include "ViaEClock.h"
#include "Rtc.h"
#include "AdbVia.h"
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

class CentrisCpu;

class CentrisMemory {
public:
    static constexpr uint32_t kRomSize = 0x100000;   // 1 MB
    static constexpr uint32_t kVramSize = 0x100000;  // 1 MB window
    int64_t cpuHz() const { return cpuHz_; }         // 60 Hz quantum for the shell
    static constexpr int64_t  kCpuHz650 = 25000000;  // 68LC040 (Centris 650)
    static constexpr int64_t  kCpuHz610 = 20000000;  // 68LC040 (Centris 610)
    static constexpr int64_t  kCpuHzQ650 = 33333333; // 68040 (Quadra 650)
    static constexpr int64_t  kCpuHzQ610 = 25000000; // 68040 (Quadra 610)
    // VIA1 port A model pins (iosb_base::via_in_a = pa1<<1|pa2<<2|pa4<<4|pa6<<6).
    static constexpr uint8_t kIdCentris610 = 0x40;   // pa6
    static constexpr uint8_t kIdCentris650 = 0x46;   // pa1|pa2|pa6
    static constexpr uint8_t kIdQuadra610  = 0x44;   // pa2|pa6
    static constexpr uint8_t kIdQuadra650  = 0x52;   // pa1|pa4|pa6
    // Quadra 800 (macquadra800.cpp macqd800): pa1=1, pa2=0, pa4=1, pa6=0 —
    // the only model of the family with pa6 clear.
    static constexpr uint8_t kIdQuadra800  = 0x12;   // pa1|pa4
    // Ethernet address ROM at $50008000 (macquadra800.cpp ethernet_mac_r).
    // Apple OUI 08:00:07 + a fixed host part; the checksum byte is derived.
    static constexpr uint8_t kMacAddr[6] = { 0x08, 0x00, 0x07, 0x50, 0x6D, 0x68 };

    explicit CentrisMemory(uint32_t totalRam = 36u << 20,
                           int64_t cpuHz = kCpuHz650,
                           uint8_t modelPins = kIdCentris650);

    bool loadRom(const std::vector<uint8_t>& data);  // 1 MB flat image
    void reset();

    uint8_t  read8(uint32_t addr);
    uint16_t read16(uint32_t addr);
    void     write8(uint32_t addr, uint8_t v);
    void     write16(uint32_t addr, uint16_t v);
    uint8_t  peek8(uint32_t addr) const;

    // ── JIT hooks (src/jit/POM68K_JIT.md) ──────────────────────────────
    // codeSpan() hands the JIT a raw host pointer to PLAIN memory only.
    // Everything with a read side effect is refused, and that includes the
    // whole map while the boot overlay is still up: clearing the overlay is
    // itself a side effect of a read in the $4xxxxxxx window (see read8),
    // and a const probe cannot perform it.
    const uint8_t* codeSpan(uint32_t phys, uint32_t& len) const;
    // dataSpan() is codeSpan's write-aware twin, for the JIT data TLB: a
    // host pointer to bytes a generated load or store may touch directly.
    // Same refusals, plus everything a STORE cannot simply land in — the
    // ROM window, and any map with a diagnostic write-watch armed.
    uint8_t* dataSpan(uint32_t phys, uint32_t& len, bool write);
    // Attaches the JIT's write guard; nullptr detaches it. When null, every
    // write path costs one always-predicted branch.
    void setJitGuard(jit::CodeGuard* g) { jitGuard_ = g; }
    uint32_t ramBytes() const { return totalRam_; }

    void setCpu(CentrisCpu* cpu) { cpu_ = cpu; }
    void updateIrq();
    int iplLevel() const;    // SCC=4 > VIA2=2 > VIA1=1 (iosb field_interrupts)

    void tick(int cpuCycles);

    Via6522& via1() { return via1_; }
    Rtc& rtc() { return rtc_; }
    AdbVia& adbVia() { return adbVia_; }
    AdbBus& adb() { return adb_; }
    Scc8530& scc() { return scc_; }
    AscIosb& asc() { return asc_; }
    Swim2& swim() { return swim_; }
    SonyDrive& internalDrive() { return drive0_; }
    SonyDrive& externalDrive() { return drive1_; }
    bool insertDisk(const std::string& path) { return drive0_.insert(path); }
    void ejectDisk() { drive0_.eject(); }
    Ncr53c96& scsi() { return scsi_; }
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
    void attachDriveSounds(FloppySoundSink* floppy, FloppySoundSink* hdd) {
        drive0_.setSoundSink(floppy);
        drive1_.setSoundSink(floppy);
        for (ScsiDisk& d : scsiDisks_) d.setSoundSink(hdd);
    }
    // No reset-holding MCU (the adbmodem does not gate the CPU) — the 040
    // runs from power-on, so the machine is never "held".
    bool cpuHeld() const { return false; }
    bool adbLleActive() const { return adbVia_.lle(); }

    // Battery-backed PRAM (the discrete RTC's XPRAM) persistence.
    bool loadPram(const std::string& path);
    void savePram(const std::string& path);

    void keyEvent(uint8_t code, bool down) { adbVia_.keyEvent(code, down); }
    void mouseMove(int dx, int dy) { adbVia_.mouseMove(dx, dy); }
    void mouseButton(bool down, int button = 0) { adbVia_.mouseButton(down, button); }
    bool overlay() const { return overlay_; }
    const uint8_t* vram() const { return vram_.data(); }

    Dafb& dafb() { return dafbCell_; }
    const uint8_t (*clut() const)[3] { return dafbCell_.clut(); }
    uint32_t dafbStride() const { return dafbCell_.stride(); }

    // ── Raster geometry (VideoBeam.h) ───────────────────────────────────
    // Forwarded from the DAFB cell's own frame accumulator — the one that
    // drives its Swatch VBL. One clock, never two.
    int64_t framePos() const { return dafbCell_.framePos(); }
    int64_t frameCycles() const { return dafbCell_.frameCycles(); }
    int64_t frameActiveCycles() const { return dafbCell_.frameActiveCycles(); }
    int     frameTotalLines() const { return dafbCell_.frameTotalLines(); }
    uint64_t frameCount() const { return dafbCell_.frameCount(); }

    uint32_t dafbBase() const { return dafbCell_.base(); }
    uint8_t dafbMode() const { return dafbCell_.mode(); }
    uint8_t dafbDepth() const { return dafbCell_.depth(); }
    uint32_t dafbHres() const { return dafbCell_.hres(); }
    uint32_t dafbVres() const { return dafbCell_.vres(); }
    bool dafbBlanked() const { return dafbCell_.blanked(); }
    void setDafbMonitor(uint8_t code) { dafbCell_.setMonitor(code); }

    void vblIrq(bool s);
    void scsiIrq(bool s);
    void ascIrq(bool s);
    void sccIrqLine(bool s) { sccIrq_ = s; updateIrq(); }

    std::function<void(uint32_t, bool, uint32_t)> onIoAccess;
    std::function<void(uint32_t, bool)> onBusError;

    bool via2IrqAsserted() const { return (pvIfr_ & pvIer_ & 0x1B) != 0; }

    // A Mac ROM's first longword IS its checksum (V8Memory pattern).
    uint32_t romChecksum() const {
        if (rom_.size() < 4) return 0;
        return uint32_t(rom_[0]) << 24 | uint32_t(rom_[1]) << 16
             | uint32_t(rom_[2]) << 8  | uint32_t(rom_[3]);
    }

    // ── Save states: the machine chunk (V8Memory pattern) ───────────────
    // The discrete RTC and the PIC1654S ADB transceiver travel with the
    // machine. Out: rom_, cpuHz_/modelPins_ (profile), cpu_/jitGuard_.
    template <class Ar> void visit(Ar& ar) {
        ar.blob(ram_);
        ar.blob(vram_);
        ar(via1_, rtc_, adb_, adbVia_, scc_, asc_, swim_,
           drive0_, drive1_, scsi_, dafbCell_);
        for (auto& d : scsiDisks_) ar(d);
        ar(totalRam_, overlay_, sccIrq_,
           pvIfr_, pvIer_, pvPortB_, nubusIrqs_, ascLine_,
           memcjr_, dafbHolding_, iosbRegs_,
           scsiReadCycles_, scsiWriteCycles_,
           scsiDmaReadCycles_, scsiDmaWriteCycles_,
           ascCycAcc_, swimLastCpu_, swimCycAcc_,
           viaEClock_, tickAcc_, secAcc_);
        if constexpr (Ar::loading) {
            if (jitGuard_) jitGuard_->invalidate();
        }
    }

private:
    uint8_t viaAccess8(uint32_t addr, bool write, uint8_t v);
    uint8_t via2Access8(uint32_t addr, bool write, uint8_t v);
    void via2Recalc();
    void viaSync();
    void refreshVia1PortB();
    [[noreturn]] void busError(uint32_t addr, bool write) const;
    uint8_t ioRead8(uint32_t addr);
    void ioWrite8(uint32_t addr, uint8_t v);
    uint8_t scsiDmaRead_();
    void    scsiDmaWrite_(uint8_t v);
    void    scsiPoll_();
    void syncSwimFromCpu();
    uint8_t dafbRead8(uint32_t addr);
    void dafbWrite8(uint32_t addr, uint8_t v);
    uint32_t dafbRegRead(uint32_t off);
    void     dafbRegWrite(uint32_t off, uint32_t v);

    std::vector<uint8_t> ram_, rom_, vram_;
    Via6522 via1_;
    Rtc rtc_;                       // discrete 343-0042 on VIA1 PB0-2/CA2
    AdbBus adb_;
    AdbVia adbVia_;                 // PIC1654S ADB transceiver (firmware LLE)
    Scc8530 scc_;
    AscIosb asc_;
    int64_t ascCycAcc_ = 0;
    Swim2 swim_;
    SonyDrive drive0_, drive1_;
    int64_t swimLastCpu_ = -1;
    int64_t swimCycAcc_ = 0;
    Ncr53c96 scsi_;
    ScsiDisk scsiDisks_[7];
    CentrisCpu* cpu_ = nullptr;

    void jitMapChanged();
    jit::CodeGuard* jitGuard_ = nullptr;   // JIT code invalidation

    uint32_t totalRam_;
    int64_t  cpuHz_;
    // VIA1 φ2 is a fixed 783.36 kHz (iosb.cpp:74, R65NC22 at C7M/10), so the
    // CPU:VIA divider is a property of THIS instance's clock, not a constant:
    // 26 at 20 MHz (Centris 610), 32 at 25 MHz (Centris 650 / Quadra 610),
    // 43 at 33.33 MHz (Quadra 650), 42 at 33 MHz (Quadra 800). The hardcoded
    // 32 ran the 610's VIA timers 20 % slow and the Quadra 650's 33 % fast.
    uint8_t  modelPins_;            // VIA1 port A ID (0x40/0x46/0x44/0x52)
    static constexpr uint32_t kBoxId = 0xA55A2BADu;   // $5FFF0000-$5FFFFFFF
    bool overlay_ = true;
    bool sccIrq_ = false;

    uint8_t pvIfr_ = 0, pvIer_ = 0, pvPortB_ = 0;
    uint8_t nubusIrqs_ = 0xFF;
    bool ascLine_ = false;

    uint32_t memcjr_[0x20] = {};
    uint16_t dafbHolding_ = 0;
    uint16_t iosbRegs_[0x20] = {};

    int scsiReadCycles_ = 3, scsiWriteCycles_ = 3;
    int scsiDmaReadCycles_ = 3, scsiDmaWriteCycles_ = 3;

    Dafb dafbCell_;

    via_eclock::Ticker viaEClock_;   // exact 783.36 kHz (ViaEClock.h)
    int64_t tickAcc_ = 0;
    int64_t secAcc_ = 0;
};
