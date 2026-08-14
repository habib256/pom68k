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
// ── Quadra 900 "Eclipse" / 950 "Zydeco" (Model::Q900 / Q950) ──
// The tower siblings are this very board with the IIfx's I/O front end
// grafted on (MAME `eclipse_state`, `quadra900_map`) — which is why they
// live here rather than in a platform of their own:
//   * SCC and SWIM move BEHIND the two Apple PIC IOPs: the SCC PIC host
//     window replaces the SCC at +$0C000 (now $1000 wide) and the SWIM PIC
//     replaces the SWIM at +$1E000. The IOP firmware is downloaded by the
//     ROM, exactly as on the IIfx (`docs/IOP_BRINGUP.md`).
//   * **ADB is bit-banged by the SWIM IOP** (gpout0 → the wire, inverted;
//     gpin reads it back) against `AdbLine` — no PIC1654S transceiver, and
//     not the Egret either: MAME feeds the device line to both listeners but
//     only the IOP drives it. Measured 2026-08-14 (`q900_input_etalon`),
//     which is when the default stopped routing input at the Egret, where
//     nothing had ever arrived.
//   * The discrete 343-0042 RTC is gone: an **Egret** (341S0851) on VIA1
//     CB1/CB2 owns clock, PRAM and power ("The Quadra 900 replaced the
//     real-time clock with a compatible variant of Egret"). Since
//     2026-08-14 that Egret is the **real firmware on a real 68HC05**
//     (`CudaLle`, `Flavor::Egret`, MAME `macquadra700.cpp:886-894`
//     `set_default_bios_tag("341s0851")`) whenever the dump is present —
//     the Eclipse was the last machine in the tree still running the
//     command-level `Egret` HLE unconditionally. `POM68K_EGRET_LLE=0` or a
//     missing dump falls back to it, loudly (docs/LLE_VS_HLE.md § 2).
//   * A **second** 53C96 SCSI bus at +$0F400 (registers) / +$0F502 (DMA).
//   * VIA1 PA identity: $D0|bit0 (Q900) or $90|bit0 (Q950) vs the Q700's
//     $C0|bit0; VIA2 port B carries no DFAC on these boards.
//   * 5 NuBus slots instead of 2; Q950 runs at 33.333 MHz (Q900 at 25).
// The Q950's DAFB II reports version 3 with the AC842a RAMDAC (PCBR1 ID
// $01); the Q700/Q900 chip reports version 1 with the plain AC842 — the
// ctor passes both to the `Dafb` cell (dafb.cpp:84,426-427,1105-1131).
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
#include "ViaEClock.h"
#include "Rtc.h"
#include "AdbVia.h"
#include "AdbBus.h"
#include "AdbLine.h"
#include "ApplePic.h"
#include "Egret.h"
#include "CudaLle.h"
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
    static constexpr int64_t  kCpuHzQ950 = 33333333;  // Zydeco (33.333 MHz XTAL)
    // Ethernet address ROM (the Q800 shape: 6 MAC bytes + inverted XOR).
    static constexpr uint8_t kMacAddr[6] = { 0x08, 0x00, 0x07, 0x70, 0x30, 0x30 };

    // Spike = the Quadra 700; Q900/Q950 = the "Eclipse"/"Zydeco" towers,
    // the same board with the IIfx's IOP + Egret front end (see the
    // header note). `eclipse()` is the one predicate the code branches on.
    enum class Model { Spike, Q900, Q950 };

    explicit Q700Memory(uint32_t totalRam = 32u << 20, int64_t cpuHz = kCpuHz,
                        Model model = Model::Spike);
    Model model() const { return model_; }
    bool eclipse() const { return model_ != Model::Spike; }

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
    // dataSpan() is codeSpan's write-aware twin, for the JIT data TLB: a
    // host pointer to bytes a generated load or store may touch directly.
    // Same refusals, plus everything a STORE cannot simply land in — the
    // ROM window, and any map with a diagnostic write-watch armed.
    uint8_t* dataSpan(uint32_t phys, uint32_t& len, bool write);
    // Attaches the JIT's write guard; nullptr detaches it. When null, every
    // write path costs one always-predicted branch.
    void setJitGuard(jit::CodeGuard* g) { jitGuard_ = g; }
    uint32_t ramBytes() const { return totalRam_; }

    void setCpu(Q700Cpu* cpu) { cpu_ = cpu; }
    void updateIrq();
    int iplLevel() const;      // SCC=4 > VIA2=2 > VIA1=1 (field_interrupts)

    void tick(int cpuCycles);
    int cyclesToNextEvent() const;

    Via6522& via1() { return via1_; }
    Via6522& via2() { return via2_; }
    Rtc& rtc() { return rtc_; }
    AdbVia& adbVia() { return adbVia_; }
    AdbBus& adb() { return adb_; }
    Scc8530& scc() { flushScc(); return scc_; }
    int64_t deferredSccCycles() const { return sccDebt_; }
    AscEasc& asc() { return asc_; }
    Swim1& swim() { return swim_; }
    // Eclipse-only front end (unused on the Spike; see the header note).
    ApplePic& sccPic() { return sccPic_; }
    ApplePic& swimPic() { return swimPic_; }
    AdbLine& adbLine() { return adbLine_; }
    Egret& egret() { return egret_; }
    CudaLle& egretLle() { return egretLle_; }
    bool egretLleActive() const { return egretLleOn_; }
    Ncr53c96& scsi2() { flushScsi2(); return scsi2_; }
    int64_t deferredScsi2Cycles() const { return scsi2Debt_; }
    long adbHostEdges() const { return adbHostEdges_; }
    SonyDrive& internalDrive() { return drive0_; }
    SonyDrive& externalDrive() { return drive1_; }
    bool insertDisk(const std::string& path) { return drive0_.insert(path); }
    void ejectDisk() { drive0_.eject(); }
    Ncr53c96& scsi() { flushScsi(); return scsi_; }
    int64_t deferredScsiCycles() const { return scsiDebt_; }
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
    void attachDriveSounds(FloppySoundSink* floppy, FloppySoundSink* hdd) {
        drive0_.setSoundSink(floppy);
        drive1_.setSoundSink(floppy);
        for (ScsiDisk& d : scsiDisks_) d.setSoundSink(hdd);
    }
    // The Spike has no reset-holding MCU; the Eclipse's Egret does hold it
    // (the firmware's own PC3 edge under the LLE, a timer under the HLE).
    bool cpuHeld() const {
        if (!eclipse()) return false;
        return egretLleOn_ ? egretLle_.cpuHeld() : egret_.cpuHeld();
    }
    // "The machine's ADB transport is the real firmware": the PIC1654S on
    // the Spike, the Egret's own 68HC05 on the Eclipse. Product mode reads
    // this (main.cpp qualifyFullLleAarch64).
    bool adbLleActive() const {
        return eclipse() ? egretLleOn_ : adbVia_.lle();
    }

    // Firmware RESET_SYSTEM ($11) — the Finder's "Restart". The CPU wrapper
    // consumes the latch at a run boundary (Q700Cpu::runCycles); the MCU
    // must never be reset from inside the memory callback that raised it.
    bool consumeRestart() {
        bool r = restartPending_;
        restartPending_ = false;
        return r;
    }

    bool loadPram(const std::string& path);
    void savePram(const std::string& path);
    // Host wall clock → whichever store keeps time on this board. Both Egret
    // objects are seeded: the HLE counts seconds itself, the LLE stages them
    // for the firmware's own counter (Sonora's lesson — seeding one left
    // every default boot at the 1904 epoch).
    void setEgretSeconds(uint32_t s) {
        egret_.setSeconds(s);
        egretLle_.setSeconds(s);
    }

    // Input reaches the guest over whichever ADB transport this board has:
    // the PIC1654S transceiver (Spike), or on the Eclipse the SWIM IOP's
    // bit-banged wire — the IIfx path, and the one the ROM actually drives
    // (measured 2026-08-14, `q900_input_etalon`). `POM68K_Q900_ADB=egret`
    // routes to the Egret instead, where the firmware LLE puts the devices
    // on the MCU's own bit-serial line and the HLE on the command-level
    // AdbBus; neither delivers, which is the point of keeping the knob.
    void keyEvent(uint8_t code, bool down) {
        if (!eclipse()) { adbVia_.keyEvent(code, down); return; }
        if (!egretAdb_) { adbLine_.keyEvent(code, down); return; }
        if (egretLleOn_) egretLle_.adbLine().keyEvent(code, down);
        else adb_.keyEvent(code, down);
    }
    void mouseMove(int dx, int dy) {
        if (!eclipse()) { adbVia_.mouseMove(dx, dy); return; }
        if (!egretAdb_) { adbLine_.mouseMove(dx, dy); return; }
        if (egretLleOn_) egretLle_.adbLine().mouseMove(dx, dy);
        else adb_.mouseMove(dx, dy);
    }
    void mouseButton(bool down, int button = 0) {
        if (!eclipse()) { adbVia_.mouseButton(down, button); return; }
        if (!egretAdb_) { adbLine_.mouseButton(down, button); return; }
        if (egretLleOn_) egretLle_.adbLine().mouseButton(down, button);
        else if (button == 0) adb_.mouseButton(down);
    }
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

    // A Mac ROM's first longword IS its checksum (V8Memory pattern).
    uint32_t romChecksum() const {
        if (rom_.size() < 4) return 0;
        return uint32_t(rom_[0]) << 24 | uint32_t(rom_[1]) << 16
             | uint32_t(rom_[2]) << 8  | uint32_t(rom_[3]);
    }

    // ── Save states: the machine chunk (V8Memory pattern) ───────────────
    // Two real VIAs (the discrete-040 front end) + RTC + PIC ADB + the
    // DAFB cell with its TurboSCSI control latch. Out: rom_, cpuHz_
    // (profile), cpu_/jitGuard_.
    template <class Ar> void visit(Ar& ar) {
        ar.blob(ram_);
        ar.blob(vram_);
        ar(via1_, via2_, rtc_, adb_, adbVia_, scc_, asc_, swim_,
           drive0_, drive1_, scsi_, dafbCell_);
        for (auto& d : scsiDisks_) ar(d);
        ar(totalRam_, overlay_, sccIrq_, nubusIrqs_, scsiCtrl_,
           scsiReadCycles_, scsiWriteCycles_,
           scsiDmaReadCycles_, scsiDmaWriteCycles_,
           ascCycAcc_, swimLastCpu_, swimCycAcc_,
           viaEClock_, tickAcc_, secAcc_, sccDebt_, scsiDebt_);
        // Eclipse-only tail: the header pins the profile, so the Quadra
        // 700's chunk layout (and its existing snapshots) is untouched.
        // Each ApplePic carries its 32 KB of host-uploaded firmware — a
        // restore that dropped it would wake IOPs with no program.
        // `egretLle_` nests its 68HC05, its ADB wire and the half-transferred
        // host handshake (CudaLle::visit). It travels next to the HLE object
        // rather than instead of it: the header pins the profile, not the MCU
        // flavour, so one chunk layout serves both paths.
        if (eclipse()) {
            ar(sccPic_, swimPic_, adbLine_, egret_, egretLle_, scsi2_,
               scsiCtrl2_, scsi2ReadCycles_, scsi2WriteCycles_,
               scsi2DmaReadCycles_, scsi2DmaWriteCycles_, scsi2Debt_,
               eclipseC15Acc_);
        }
        if constexpr (Ar::loading) {
            if (jitGuard_) jitGuard_->invalidate();
        }
    }

private:
    void iopTrace(bool write, char which, uint32_t base, uint8_t v);
    // Bring-up: the last host-window touches, dumped when an IOP panics.
    // A 65C02 that returned through a smashed stack frame was smashed by
    // SOMEONE, and the host writes the same RAM the IOP runs from.
    struct IopEvent { uint32_t pc; uint16_t base, ramAddr; uint8_t off, v; char which, rw; };
    static constexpr int kIopRing = 64;
    IopEvent iopRing_[kIopRing] = {};
    int iopRingIdx_ = 0;
    void dumpIopRing();
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
    void    scsiHoldDtack_(bool write);
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
    int64_t sccDebt_ = 0;          // Spike only; Eclipse SCC is IOP-coupled
    void flushScc();
    // Discrete EASC $B0 (macquadra700.cpp:805 wires ASC_EASC on every
    // quadra_base machine) — not the Sonora $BC cell this board shipped
    // with before the MAME-parity audit (finding #1).
    AscEasc asc_;
    int64_t ascCycAcc_ = 0;
    Swim1 swim_;
    SonyDrive drive0_, drive1_;
    int64_t swimLastCpu_ = -1;
    int64_t swimCycAcc_ = 0;
    Ncr53c96 scsi_;
    int64_t scsiDebt_ = 0;
    void flushScsi();
    ScsiDisk scsiDisks_[7];
    // Eclipse front end (constructed always, wired only when eclipse()).
    ApplePic sccPic_, swimPic_;
    AdbLine adbLine_;
    Egret egret_;                      // command-level fallback (LLE_VS_HLE §2)
    CudaLle egretLle_;                 // Egret firmware LLE (341S0851)
    bool egretLleOn_ = false;
    bool restartPending_ = false;      // firmware RESET_SYSTEM, deferred
    Ncr53c96 scsi2_;                   // the tower's second SCSI bus
    int64_t scsi2Debt_ = 0;
    int64_t eclipseC15Acc_ = 0;        // CPU→C15M fractional phase
    void flushScsi2();
    bool egretAdb_ = false;            // Egret serves ADB (vs the IOP wire,
                                       // the measured default since 2026-08-14)
    long adbHostEdges_ = 0;
    Q700Cpu* cpu_ = nullptr;

    void jitMapChanged();
    jit::CodeGuard* jitGuard_ = nullptr;   // JIT code invalidation

    uint32_t totalRam_;
    int64_t  cpuHz_;
    Model model_ = Model::Spike;
    bool overlay_ = true;
    bool sccIrq_ = false;
    uint8_t nubusIrqs_ = 0xFF;         // active low, bit 6 = DAFB (slot $F)

    // DAFB TurboSCSI cell (dafb.cpp:480-530): register $24 latches the
    // wait-state selection bits and reads back with the live DRQ in bit 9.
    uint16_t scsiCtrl_ = 0;
    int scsiReadCycles_ = 3, scsiWriteCycles_ = 3;
    int scsiDmaReadCycles_ = 3, scsiDmaWriteCycles_ = 3;
    // SCSI bus 2, register $28 — same decode (dafb.cpp:424,533-576). The
    // latch exists on every flavour; only the Eclipse wires a second 53C96
    // behind it, so on the Spike bit 9 (live DRQ) simply never rises.
    uint16_t scsiCtrl2_ = 0;
    int scsi2ReadCycles_ = 3, scsi2WriteCycles_ = 3;
    int scsi2DmaReadCycles_ = 3, scsi2DmaWriteCycles_ = 3;

    Dafb dafbCell_;

    via_eclock::Ticker viaEClock_;   // exact 783.36 kHz (ViaEClock.h)
    int64_t tickAcc_ = 0;
    int64_t secAcc_ = 0;
};
