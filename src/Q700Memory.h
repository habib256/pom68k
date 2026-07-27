// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Macintosh Quadra 700 ("Spike") memory map ──
// The FIRST Quadra: a 68040 desktop still built from DISCRETE chips, before
// djMEMC/IOSB condensed them (MAME macquadra700.cpp, `spike_state`). That
// makes it a recombination of two machines POM68K already has — the Mac II's
// VIA1+VIA2 / discrete 343-0042 RTC / PIC1654S ADB transceiver front end, on
// the Quadra-era I/O block (DAFB video, 53C96 TurboSCSI, SWIM1, EASC, SCC,
// SONIC) at the $5000xxxx layout the Centris/Quadra 800 also use.
//
//   $00000000-  RAM (flat), ROM mirror under the boot overlay
//   $40000000-  ROM 1 MB (mirrored; the first read clears the overlay —
//               quadrax00_state::rom_switch_r)
//   $50000000-  I/O (quadra700_map): +$00000 VIA1, +$02000 VIA2 (a REAL
//               6522, unlike the Centris pseudo-VIA), +$08000 ethernet
//               address ROM, +$0A000 SONIC (unmapped 0), +$0C000 SCC,
//               +$0E000 "Orwell" memory controls (unmapped 0),
//               +$0F000 TurboSCSI 53C96 registers, +$0F100 TurboSCSI
//               pseudo-DMA, +$14000 EASC, +$1E000 SWIM1
//   $F9000000-  VRAM (2 MB), $F9800000- DAFB register cell
//
// Two things differ from the Centris beyond the layout:
//  * SCSI lives behind **DAFB's** TurboSCSI cell, not IOSB's — DAFB register
//    $24 carries the wait-state bits and reads back the DRQ status in bit 9
//    (dafb.cpp:480-530). This is the cell `docs/LLE_VS_HLE.md` §3 recorded as
//    "only matters for a future Q700/Q950-class profile".
//  * VIA2 is a real 6522 with the Mac II's interrupt fan-in: slot/DAFB IRQs
//    on CA1 + port A, ASC on CB1, SCSI on CB2, SONIC on PA0 (all active low).
// Unmapped I/O reads back 0 (the family rule; MAME maps nothing there).
// Gate: tests/q700_boot_etalon.cpp.

#pragma once
#include "jit/JitGuard.h"
#include "Via6522.h"
#include "Rtc.h"
#include "AdbVia.h"
#include "AdbBus.h"
#include "Scc8530.h"
#include "Ncr53c96.h"
#include "ScsiDisk.h"
#include "Asc.h"
#include "Swim1.h"
#include "Dafb.h"
#include "SonyDrive.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class Q700Cpu;

class Q700Memory {
public:
    static constexpr uint32_t kRomSize = 0x100000;    // 1 MB
    static constexpr uint32_t kVramSize = 0x200000;   // 2 MB (macqd700)
    int64_t cpuHz() const { return cpuHz_; }          // 60 Hz quantum for the shell
    static constexpr int64_t  kCpuHz = 25000000;      // 68040, 50 MHz XTAL / 2
    // Ethernet address ROM (the Q800 shape: 6 MAC bytes + inverted XOR).
    static constexpr uint8_t kMacAddr[6] = { 0x08, 0x00, 0x07, 0x70, 0x30, 0x30 };

    explicit Q700Memory(uint32_t totalRam = 32u << 20, int64_t cpuHz = kCpuHz);

    bool loadRom(const std::vector<uint8_t>& data);
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
    // Attaches the JIT's write guard; nullptr detaches it. When null, every
    // write path costs one always-predicted branch.
    void setJitGuard(jit::CodeGuard* g) { jitGuard_ = g; }
    uint32_t ramBytes() const { return totalRam_; }

    void setCpu(Q700Cpu* cpu) { cpu_ = cpu; }
    void updateIrq();
    int iplLevel() const;      // SCC=4 > VIA2=2 > VIA1=1 (field_interrupts)

    void tick(int cpuCycles);

    Via6522& via1() { return via1_; }
    Via6522& via2() { return via2_; }
    Rtc& rtc() { return rtc_; }
    AdbVia& adbVia() { return adbVia_; }
    AdbBus& adb() { return adb_; }
    Scc8530& scc() { return scc_; }
    AscSonora& asc() { return asc_; }
    Swim1& swim() { return swim_; }
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
    void attachDriveSounds(FloppySoundSink* floppy, FloppySoundSink* hdd) {
        drive0_.setSoundSink(floppy);
        drive1_.setSoundSink(floppy);
        for (ScsiDisk& d : scsiDisks_) d.setSoundSink(hdd);
    }
    bool cpuHeld() const { return false; }        // no reset-holding MCU
    bool adbLleActive() const { return adbVia_.lle(); }

    bool loadPram(const std::string& path);
    void savePram(const std::string& path);

    void keyEvent(uint8_t code, bool down) { adbVia_.keyEvent(code, down); }
    void mouseMove(int dx, int dy) { adbVia_.mouseMove(dx, dy); }
    void mouseButton(bool down) { adbVia_.mouseButton(down); }
    bool overlay() const { return overlay_; }
    const uint8_t* vram() const { return vram_.data(); }

    Dafb& dafb() { return dafbCell_; }
    const uint8_t (*clut() const)[3] { return dafbCell_.clut(); }
    uint32_t dafbStride() const { return dafbCell_.stride(); }
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

private:
    uint8_t viaAccess8(uint32_t addr, bool write, uint8_t v);
    uint8_t via2Access8(uint32_t addr, bool write, uint8_t v);
    void viaSync();
    void refreshVia1PortB();
    void refreshVia2PortA();
    [[noreturn]] void busError(uint32_t addr, bool write) const;
    uint8_t ioRead8(uint32_t addr);
    void ioWrite8(uint32_t addr, uint8_t v);
    uint8_t scsiDmaRead_();
    void    scsiDmaWrite_(uint8_t v);
    void    scsiPoll_();
    void syncSwimFromCpu();
    uint8_t dafbRead8(uint32_t addr);
    void dafbWrite8(uint32_t addr, uint8_t v);

    std::vector<uint8_t> ram_, rom_, vram_;
    Via6522 via1_, via2_;
    Rtc rtc_;
    AdbBus adb_;
    AdbVia adbVia_;
    Scc8530 scc_;
    AscSonora asc_;
    int64_t ascCycAcc_ = 0;
    Swim1 swim_;
    SonyDrive drive0_, drive1_;
    int64_t swimLastCpu_ = -1;
    int64_t swimCycAcc_ = 0;
    Ncr53c96 scsi_;
    ScsiDisk scsiDisks_[7];
    Q700Cpu* cpu_ = nullptr;

    void jitMapChanged();
    jit::CodeGuard* jitGuard_ = nullptr;   // JIT code invalidation

    uint32_t totalRam_;
    int64_t  cpuHz_;
    bool overlay_ = true;
    bool sccIrq_ = false;
    uint8_t nubusIrqs_ = 0xFF;         // active low, bit 6 = DAFB (slot $F)

    // DAFB TurboSCSI cell (dafb.cpp:480-530): register $24 latches the
    // wait-state selection bits and reads back with the live DRQ in bit 9.
    uint16_t scsiCtrl_ = 0;
    int scsiReadCycles_ = 3, scsiWriteCycles_ = 3;
    int scsiDmaReadCycles_ = 3, scsiDmaWriteCycles_ = 3;

    Dafb dafbCell_;

    int viaPhase_ = 0;
    int64_t tickAcc_ = 0;
    int64_t secAcc_ = 0;
};
