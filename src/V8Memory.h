// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Mac LC II memory map + V8 gate array core ──
// V8 decodes A31 + A23-A0 only (MAME maclc.cpp:181 masks $80FFFFFF).
// RAM $000000-$9FFFFF per the config register (SIMM bank first, then
// motherboard; the first 2 MB of motherboard RAM are ALWAYS aliased at
// $800000). ROM 512 KB at $A00000 mirrored ×2 — any read there clears
// the boot overlay (address-triggered, no VIA bit). I/O at $F00000+:
// VIA1, SCC, SCSI, ASC, SWIM, Ariel, pseudo-VIA, VRAM. Unmapped I/O in
// $F00000+ and the absent PDS slot (A31 set) BUS-ERROR — the ROM's
// address-map probe relies on it (asc.cpp:766-770 AddrMapFlags).
// Source of truth: MAME v8.cpp + maclc.cpp (master 2026-07-15), pinned
// with line numbers in docs/LCII_HARDWARE.md.
// Gates: tests/v8_ramsize.cpp, tests/pseudovia_test.cpp.

#pragma once
#include "Via6522.h"
#include "PseudoVia.h"
#include "Ariel.h"
#include "Egret.h"
#include "AdbBus.h"
#include "Asc.h"
#include "Ncr5380.h"
#include "ScsiDisk.h"
#include "Iwm.h"
#include "SonyDrive.h"
#include "Scc8530.h"
#include <cstdint>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

class Cpu030;

class V8Memory {
public:
    static constexpr uint32_t kRomSize = 0x80000;    // 512 KB
    static constexpr uint32_t kVramSize = 0x80000;   // 512 KB window, fully populated
    static constexpr uint32_t kMbRamSize = 0x400000; // 4 MB soldered (baseIs4M)
    static constexpr int64_t  kCpuHz = 15667200;     // C32M/2

    // totalRam: 4, 6, 8 or 10 MB (motherboard 4 MB + SIMM pair);
    // 10 MB is the V8 hard limit (12 MB installed, 2 MB wasted).
    explicit V8Memory(uint32_t totalRam = 0xA00000);

    bool loadRom(const std::vector<uint8_t>& data);  // 512 KB flat image
    void reset();                                    // overlay on, V8 regs cleared

    uint8_t  read8(uint32_t addr);
    uint16_t read16(uint32_t addr);
    void     write8(uint32_t addr, uint8_t v);
    void     write16(uint32_t addr, uint16_t v);

    // Side-effect-free inspection (video scanout, tests, debugger) —
    // never clears the overlay, never bus-errors, never stalls.
    uint8_t peek8(uint32_t addr) const;

    // Wire-back to the CPU (POMIIGS setCpu pattern): IPL recompute on any
    // IFR/IER change, extBusError() on unmapped I/O, E-clock stalls.
    void setCpu(Cpu030* cpu) { cpu_ = cpu; }
    void updateIrq();
    // V8 interrupt priority resolver (v8.cpp:287-315): SCC=4 > VIA2=2 > VIA1=1
    int iplLevel() const;

    // Called from Cpu030::sync with elapsed CPU cycles: VIA1 timers
    // (φ2 = CPU/20 = 783.36 kHz) + the free-running 60.15 Hz CA1 tick.
    void tick(int cpuCycles);

    Via6522& via1() { return via_; }
    PseudoVia& pseudoVia() { return pvia_; }
    Ariel& ariel() { return ariel_; }
    Egret& egret() { return egret_; }
    AdbBus& adb() { return adb_; }
    AscV8& asc() { return asc_; }
    Ncr5380& scsi() { return scsi_; }
    ScsiDisk& scsiDisk() { return scsiDisk_; }
    bool attachScsi(const std::string& path, bool writeBack = false) {
        if (!scsiDisk_.open(path, writeBack)) return false;
        scsi_.attach(&scsiDisk_);
        return true;
    }
    // SWIM1 comes up IWM-compatible (GCR); the Plus IWM + Sony drive
    // serve the 800K path, ISM/MFM (1.44M) is deferred (O6.7)
    Iwm& iwm() { return iwm_; }
    bool insertDisk(const std::string& path) { return drive_.insert(path); }
    bool cpuHeld() const { return egret_.cpuHeld(); }  // power-on reset hold
    bool overlay() const { return overlay_; }
    uint8_t ramConfig() const { return config_; }
    const uint8_t* vram() const { return vram_.data(); }
    uint8_t videoConfig() const { return videoConfig_; }  // depth bits 0-2
    uint8_t monitorSense() const { return montype_; }
    // Switch the monitor sense (= plug in a different display). Takes a Mac
    // reset to matter (the ROM reads it at boot). On a real Mac each monitor
    // keeps its own bit-depth; our sPRAM models one shared video block
    // ($58-$5A: depth + mode), so booting a second monitor would overwrite
    // the first's color choice. Emulate the per-monitor behaviour by parking
    // the outgoing monitor's video sPRAM and restoring the incoming one's,
    // so alternating resolutions never clobbers the other's depth.
    void setMonitorSense(uint8_t m) {
        if (m == montype_) return;
        for (int i = 0; i < 3; i++) vidSpram_[montype_ & 7][i] = egret_.pram(0x58 + i);
        vidSpramSaved_[montype_ & 7] = true;
        montype_ = m;
        if (vidSpramSaved_[m & 7])
            for (int i = 0; i < 3; i++) egret_.setPram(0x58 + i, vidSpram_[m & 7][i]);
        // else: a monitor never configured this session — let the ROM/System
        // set it up (it comes up B&W until "256 couleurs" + restart).
    }

    // Device lines into the pseudo-VIA (SCSI lands in O6.5, ASC in O6.6)
    void ascIrq(bool s)  { pvia_.ascIrq(s);  updateIrq(); }
    void scsiIrq(bool s) { pvia_.scsiIrq(s); updateIrq(); }
    void scsiDrq(bool s) { pvia_.scsiDrq(s); updateIrq(); }
    void vblIrq(bool s)  { pvia_.slotIrq(PseudoVia::VBL, s); updateIrq(); }
    void sccIrqLine(bool s) { sccIrq_ = s; updateIrq(); }
    Scc8530& scc() { return scc_; }

    // Debug hook (lcii_trace): (address, isWrite, value) per SCC access
    std::function<void(uint32_t, bool, uint8_t)> onSccAccess;

private:
    void applyRamConfig(uint8_t config);
    // Byte index into ram_ for a RAM-space address, or $FFFFFFFF when
    // the address falls in a hole (open bus). Priority mirrors MAME's
    // install order: $800000 alias, then motherboard window, then SIMM.
    // Inline (POM68K perf 2026-07-17): 1.4G calls/10s at the Finder.
    uint32_t ramIndex(uint32_t addr) const {
        if (overlay_) return 0xFFFFFFFF;
        if (addr >= 0x800000)                // fixed 2 MB alias (v8.cpp:33-35)
            return addr & 0x1FFFFF;
        if (mbMapped_ && addr >= mbLoc_ && addr < mbLoc_ + mbSize_)
            return addr - mbLoc_;
        if (simmMapped_ && addr < simmPhys_)
            return simmOff_ + addr;
        return 0xFFFFFFFF;
    }
    uint8_t viaAccess8(uint32_t addr, bool write, uint8_t v);
    [[noreturn]] void busError() const;
    void viaSync();                          // E-clock stall (v8.cpp:462-483)

    std::vector<uint8_t> ram_, rom_, vram_;
    Via6522 via_;
    PseudoVia pvia_;
    Ariel ariel_;
    Egret egret_{via_};
    AdbBus adb_;
    AscV8 asc_;
    Ncr5380 scsi_;
    ScsiDisk scsiDisk_;
    Iwm iwm_;
    SonyDrive drive_;
    Cpu030* cpu_ = nullptr;

    uint32_t totalRam_;
    // RAM banks per config (MAME v8.cpp ram_size, byte offsets into ram_):
    // SIMM at $000000 when enabled, motherboard after it; alias fixed.
    bool simmMapped_ = false;
    uint32_t simmOff_ = 0, simmPhys_ = 0;    // physical SIMM bank
    uint32_t mbLoc_ = 0, mbSize_ = 0;        // motherboard window ($FFFFFFFF = none)
    bool mbMapped_ = false;

    Scc8530 scc_;                            // Z8530 (O6.10: LAP ext ints)
    void localTalkWatchdog(int cpuCycles);   // O6.11 HLE LAP unwedge
    int64_t lapHeldCycles_ = 0;              // cycles the LAP mutex held
    bool lapWatchdog_ = true;                // POM68K_NO_LTALK_WD disables
    uint8_t config_ = 0;                     // pseudo-VIA reg 1 (RAM size)
    uint8_t videoConfig_ = 0;                // pseudo-VIA reg $10 bits 0-2
    uint8_t montype_ = 2;                    // 512×384 12" RGB
    uint8_t vidSpram_[8][3] = {};            // parked video sPRAM per sense
    bool vidSpramSaved_[8] = {};
    bool overlay_ = true;
    bool sccIrq_ = false;
    int viaPhase_ = 0;                       // CPU-cycle remainder for ÷20
    int64_t tickAcc_ = 0;                    // 60.15 Hz Bresenham accumulator
    // 512×384 12" RGB frame: dot clock = CPU clock, 640×407 total dots
    // (v8.cpp:717) — VBL asserted during lines 384-406
    int64_t framePos_ = 0;
    bool vblState_ = false;
    uint8_t scsiDma_();                      // DRQ-gated window byte read
    void scsiDmaW_(uint8_t v);
};
