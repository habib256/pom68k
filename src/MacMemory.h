// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Mac Plus memory map (24-bit) ──
// RAM $000000-$3FFFFF (up to 4 MB), ROM $400000 (128 KB, mirrored), SCSI
// $580000, SCC read $9xxxxx / write $Bxxxxx, IWM $Dxxxxx, VIA $Exxxxx.
// Boot overlay maps ROM at $000000 and RAM at $600000 until the ROM clears
// VIA PA4. Video framebuffer: main = ramSize-0x5900 (512×342, 1 bpp).
// Source of truth: Guide to the Macintosh Family Hardware; MAME mac.cpp
// (pending web-research pinning — see TODO.md § M2).
// Gate: tests/cpu_smoke.cpp.

#pragma once
#include "Via6522.h"
#include "Rtc.h"
#include "Iwm.h"
#include "SonyDrive.h"
#include "Scc8530.h"
#include "MacInput.h"
#include "AdbVia.h"
#include "AdbBus.h"
#include "Ncr5380.h"
#include "ScsiDisk.h"
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>

class Cpu68k;

class MacMemory {
public:
    static constexpr uint32_t kRamSize = 0x400000;   // 4 MB (Mac Plus max)
    static constexpr uint32_t kRomSize = 0x20000;    // 128 KB (Plus)
    static constexpr int64_t  kCpuHz   = 7833600;    // 7.8336 MHz
    int64_t cpuHz() const { return kCpuHz; }         // LocalTalk pace / RTC second

    // The compact 68000 family shares this map (MAME mac128.cpp macse_map is
    // the Plus map verbatim). What changes on the SE and the Classic:
    //  * a bigger ROM (256 KB SE / SE FDHD, 512 KB Classic) and the overlay
    //    clearing itself on the first ROM access instead of on VIA PA4
    //    (mac128.cpp ram_w_se) — the Plus needs the explicit PA4 clear;
    //  * ADB instead of the M0110: the SAME PIC1654S transceiver the Mac II
    //    uses (mac128.cpp `m_adbmodem->set_via_state((data & 0x30) >> 4)`),
    //    so `AdbVia` + `AdbLine` run their real firmware here too — VIA PB5/
    //    PB4 = ST, PB3 = /ADB IRQ, CB1/CB2 = the shifter;
    //  * no mouse quadrature on PB4/PB5 (the mouse is an ADB device).
    enum class Model { Plus, SE, SEFDHD, Classic };

    explicit MacMemory(Model model = Model::Plus);
    // Re-profile before loadRom(): main() constructs the machine before it has
    // read the ROM, and the compact models are told apart by its checksum.
    void setModel(Model m);
    Model model() const { return model_; }
    bool isAdb() const { return model_ != Model::Plus; }
    uint32_t romSize() const { return romSize_; }
    AdbVia& adbVia() { return adbVia_; }
    AdbBus& adb() { return adb_; }
    bool adbLleActive() const { return adbVia_.lle(); }
    void keyEvent(uint8_t code, bool down) { adbVia_.keyEvent(code, down); }
    void adbMouseMove(int dx, int dy) { adbVia_.mouseMove(dx, dy); }
    void adbMouseButton(bool down, int button = 0) { adbVia_.mouseButton(down, button); }

    bool loadRom(const std::vector<uint8_t>& data);
    void installRom(const uint8_t* data, size_t n);  // built-in demo/test ROM
    void reset();                                    // asserts the boot overlay

    uint8_t  read8(uint32_t addr);
    uint16_t read16(uint32_t addr);
    void     write8(uint32_t addr, uint8_t v);
    void     write16(uint32_t addr, uint16_t v);

    // Screen buffer bases, selected by VIA PA6 (1 = main, 0 = alternate).
    // GttMFH; MAME MAC_MAIN_SCREEN_BUF_OFFSET; Mini vMac kMain_Offset.
    uint32_t mainScreenBase() const { return kRamSize - 0x5900; }
    uint32_t altScreenBase()  const { return kRamSize - 0xD900; }
    uint32_t screenBase() const {
        return (via_.portA() & 0x40) ? mainScreenBase() : altScreenBase();
    }

    const uint8_t* ram() const { return ram_.data(); }
    Via6522& via() { return via_; }
    bool overlay() const { return overlay_; }

    // Wire-back to the CPU: the IPL line is level-sensitive, so it must be
    // recomputed whenever a VIA access changes IFR/IER (POMIIGS setCpu pattern).
    void setCpu(Cpu68k* cpu) { cpu_ = cpu; }

    // ── Raster geometry (VideoBeam.h) ───────────────────────────────────
    // The Plus's beam is NOT modelled a second time here: this is the same
    // position the VIA PB6 "beam in display portion" bit already reads in
    // readB(), derived from the CPU clock. 370 lines × 352 cycles = 130 240
    // per frame; 342 lines are visible (MacFrame.h). Out of line because
    // Cpu68k is only forward-declared in this header.
    int64_t framePos() const;
    uint64_t frameCount() const;
    static constexpr int64_t frameCycles() { return 130240; }
    static constexpr int64_t frameActiveCycles() { return 342 * 352; }
    static constexpr int frameTotalLines() { return 370; }
    void updateIrq();          // raise/lower IPL from VIA state

    // Called from Cpu68k::sync with elapsed CPU cycles: advances the VIA
    // timers (φ2 = CPU/10) and raises IRQs on underflow.
    void tick(int cpuCycles);
    // Called once per emulated second: RTC seconds + CA2 interrupt.
    void tickOneSecond();
    Rtc& rtc() { return rtc_; }
    Iwm& iwm() { return iwm_; }
    SonyDrive& internalDrive() { return drive_; }
    bool insertDisk(const std::string& path) { return drive_.insert(path); }
    Scc8530& scc() { return scc_; }
    MacMouse& mouse() { return mouse_; }
    MacKeyboard& keyboard() { return kbd_; }
    bool sccIrq() const { return scc_.irqAsserted(); }
    Ncr5380& scsi() { return scsi_; }
    ScsiDisk& scsiDisk() { return scsiDisk_; }
    bool attachScsi(const std::string& path, bool writeBack = false) {
        if (!scsiDisk_.open(path, writeBack)) return false;
        scsi_.attach(&scsiDisk_);
        return true;
    }
    // Mechanical drive sounds (GUI only; headless leaves sinks null).
    void attachDriveSounds(FloppySoundSink* floppy, FloppySoundSink* hdd) {
        drive_.setSoundSink(floppy);
        scsiDisk_.setSoundSink(hdd);
    }

    // ── Save states (SaveState.h) ───────────────────────────────────────
    uint32_t ramBytes() const { return uint32_t(ram_.size()); }
    // A Mac ROM's first longword IS its checksum (V8Memory pattern).
    uint32_t romChecksum() const {
        if (rom_.size() < 4) return 0;
        return uint32_t(rom_[0]) << 24 | uint32_t(rom_[1]) << 16
             | uint32_t(rom_[2]) << 8  | uint32_t(rom_[3]);
    }
    // The machine chunk: RAM + every device + the M0110 keyboard
    // transaction engine. Out: rom_/romSize_/model_ (profile identity),
    // cpu_ (pointer). No JIT guard on the 68000 machine.
    template <class Ar> void visit(Ar& ar) {
        ar.blob(ram_);
        ar(via_, adb_, adbVia_, rtc_, iwm_, drive_, scc_,
           scsi_, scsiDisk_, kbd_, mouse_);
        ar(kbdPhase_, kbdCmd_, kbdResp_, kbdTimer_, kbdInquiryHold_,
           viaPhase_, secAcc_, overlay_);
    }

private:
    uint8_t viaAccess(uint32_t addr, bool write, uint8_t v);
    void refreshPortBInputs();

    std::vector<uint8_t> ram_, rom_;
    Model model_ = Model::Plus;
    uint32_t romSize_ = kRomSize;
    Via6522 via_;
    AdbBus adb_;
    AdbVia adbVia_;
    Rtc rtc_;
    Iwm iwm_;
    SonyDrive drive_;                // internal drive; external = M5.1
    Scc8530 scc_;
    Ncr5380 scsi_;
    ScsiDisk scsiDisk_;
    MacKeyboard kbd_;
    MacMouse mouse_;
    // M0110 transaction pacing: two SR interrupts ~3 ms apart (Snow model)
    enum { KBD_IDLE, KBD_SHIFT_OUT, KBD_AWAIT_IN, KBD_SHIFT_IN } kbdPhase_ = KBD_IDLE;
    uint8_t kbdCmd_ = 0, kbdResp_ = 0;
    int kbdTimer_ = 0;
    Cpu68k* cpu_ = nullptr;
    int viaPhase_ = 0;         // CPU-cycle remainder for the ÷10 VIA clock
    int64_t secAcc_ = 0;       // CPU-cycle accumulator for the RTC 1 Hz tick
    bool    kbdInquiryHold_ = false;   // Inquiry waiting out its ~1/4 s window
    // The real M0110 answers an Inquiry only on a key transition, or with Null
    // after roughly 250 ms; that hold is what paces the Mac's poll loop.
    static constexpr int kInquiryHoldCycles = 1958400;   // 250 ms @ 7.8336 MHz
    bool overlay_ = true;
};
