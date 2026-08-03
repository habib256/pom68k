// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Mac Quadra 630 / LC 580 memory map: F108 + PrimeTime II + Valkyrie ──
// "Show and Tell": the Quadra 605 board cost-reduced twice over — DAFB
// replaced by the fixed-mode **Valkyrie** framebuffer, the SCSI hard disk
// by an **ATA/IDE** port wedged into the chipset, MEMCjr by the **F108**
// memory controller (ROM/RAM switch + ATA + SCC + a "just like a 53C96"
// SCSI cell). The I/O block is otherwise the Quadra 605's PrimeTime, so
// this machine is `Q605Memory` with the video cell swapped (the VASP
// recombination pattern). MAME macquadra630.cpp / f108.cpp / valkyrie.cpp:
//   $00000000-  RAM (flat; MEMCjr banking is not modelled — MAME ditto),
//               ROM mirror while the boot overlay is on
//   $40000000-  ROM 1 MB, mirrored across $4FFFFFFF; the first read
//               anywhere in the window clears the overlay
//               (djmemc.cpp rom_switch_r)
//   $50000000-  PrimeTime I/O (iosb_base::map):
//               +$00000 VIA1 (real 6522, reg every $200, 60.15 Hz CA1)
//               +$02000 VIA2 (Quadra pseudo-VIA: real-VIA layout,
//                        no timers/SR)
//               +$0C000 SCC (word access, high byte)
//               +$0C000 SCC (F108's, same window as the Q605's)
//               +$10000 TurboSCSI 53C96 regs (reg = A4-A7) — Q6
//               +$10100 TurboSCSI DMA port — Q6
//               +$14000 ASC (EASC-like)
//               +$18000 IOSB regs (u16 every $100)
//               +$1A000 F108 ATA/IDE port (cs0/cs1; no drive modelled)
//               +$1A100 PrimeTime II special int status (VBL/ATA)
//               +$1E000 SWIM2 (register/FIFO + SuperDrive media)
//               +$24000 Valkyrie RAMDAC   +$2A000 Valkyrie registers
//   $5FFFFFFC   machine ID $A55A2252 (Quadra 630) / $A55A225A (LC 580)
//   $F9000000-  VRAM (1 MB; frame buffer starts at +$1000)
// Anything else in I/O space bus-errors — the ROM's address-map probe
// relies on it (same discipline as the LC II V8).
// Cuda replaces Egret+RTC: VIA1 PB3=TREQ in, PB4=BYTEACK, PB5=TIP,
// CB1/CB2 = Cuda clock/data (macquadra605.cpp:214-224). The wire
// protocol is Egret-compatible; POM68K reuses the Egret HLE until the
// boot trace demands Cuda-specific commands.

#pragma once
#include "jit/JitGuard.h"
#include "Via6522.h"
#include "ViaEClock.h"
#include "Egret.h"
#include "CudaLle.h"
#include "AdbBus.h"
#include "Scc8530.h"
#include "Ncr53c96.h"
#include "ScsiDisk.h"
#include "Asc.h"
#include "Swim2.h"
#include "Valkyrie.h"
#include "SonyDrive.h"
#include <cstdint>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

class Q630Cpu;

class Q630Memory {
public:
    static constexpr uint32_t kRomSize = 0x100000;   // 1 MB
    static constexpr uint32_t kVramSize = 0x100000;  // 1 MB window
    static constexpr int64_t  kCpuHz = 33000000;     // 33 MHz 68040
    int64_t cpuHz() const { return kCpuHz; }         // 60 Hz quantum for the shell
    // VIA1 φ2 is a fixed 783.36 kHz (iosb.cpp:74, R65NC22 at C7M/10), so the
    // CPU:VIA divider is a property of the machine clock — 42 here, 32 on the
    // 25 MHz Q605 this file was derived from. Rounded, not truncated.

    explicit Q630Memory(uint32_t totalRam = 36u << 20);

    bool loadRom(const std::vector<uint8_t>& data);  // 1 MB flat image
    void reset();

    uint8_t  read8(uint32_t addr);
    uint16_t read16(uint32_t addr);
    void     write8(uint32_t addr, uint8_t v);
    void     write16(uint32_t addr, uint16_t v);
    uint8_t  peek8(uint32_t addr) const;             // side-effect free

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

    void setCpu(Q630Cpu* cpu) { cpu_ = cpu; }
    void updateIrq();
    int iplLevel() const;    // SCC=4 > VIA2=2 > VIA1=1 (iosb field_interrupts)

    // Called from Q630Cpu::sync with elapsed CPU cycles: VIA1 timers
    // (783.36 kHz) + the 60.15 Hz CA1 tick + the DAFB VBL.
    void tick(int cpuCycles);

    Via6522& via1() { return via1_; }
    Egret& cuda() { return cuda_; }
    AdbBus& adb() { return adb_; }
    Scc8530& scc() { return scc_; }
    AscIosb& asc() { return asc_; }
    Swim2& swim() { return swim_; }
    SonyDrive& internalDrive() { return drive0_; }
    SonyDrive& externalDrive() { return drive1_; }
    bool insertDisk(const std::string& path) { return drive0_.insert(path); }
    void ejectDisk() { drive0_.eject(); }
    Ncr53c96& scsi() { return scsi_; }
    ScsiDisk& scsiDisk() { return scsiDisks_[0]; }  // boot drive (tests poke it)

    // Attach a SCSI target from a backing image (boot drive = ID 0).
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
    // Mechanical drive sounds (GUI only; headless leaves sinks null).
    void attachDriveSounds(FloppySoundSink* floppy, FloppySoundSink* hdd) {
        drive0_.setSoundSink(floppy);
        drive1_.setSoundSink(floppy);
        for (ScsiDisk& d : scsiDisks_) d.setSoundSink(hdd);
    }
    bool cpuHeld() const {
        return cudaLleOn_ ? cudaLle_.cpuHeld() : cuda_.cpuHeld();
    }
    // Cuda firmware LLE (POM68K_CUDA_LLE=1 + roms/cuda dump — step 2 of
    // the blueprint; Egret HLE stays the default until etalons pass).
    bool cudaLleActive() const { return cudaLleOn_; }
    CudaLle& cudaLle() { return cudaLle_; }

    // Battery-backed PRAM persistence — the file format and factory
    // fallback stay Egret's; under the firmware LLE the live copy is the
    // MCU's internal RAM, so load re-mirrors into the staging and save
    // harvests the live bytes back first.
    // Host wall clock. MUST branch on the LLE flag like loadPram/keyEvent do:
    // seeding only the HLE object left the firmware MCU's own seconds counter
    // untouched, so every default (firmware-LLE) boot started at the 1904
    // epoch and wrote that back to the battery file.
    void setRtcSeconds(uint32_t s) {
        cuda_.setSeconds(s);
        cudaLle_.setSeconds(s);
    }
    bool loadPram(const std::string& path) {
        bool ok = cuda_.loadPram(path);
        if (cudaLleOn_)
            for (int i = 0; i < 256; i++) cudaLle_.setPram(i, cuda_.pram(i));
        return ok;
    }
    void savePram(const std::string& path) {
        if (cudaLleOn_)
            for (int i = 0; i < 256; i++) cuda_.setPram(i, cudaLle_.pram(i));
        cuda_.savePram(path);
    }

    // Host input events (UI thread → machine) — routed to the firmware's
    // bit-serial AdbLine when the Cuda LLE is active, else to the
    // command-level AdbBus behind the Egret HLE (blueprint step 4).
    void keyEvent(uint8_t code, bool down) {
        if (cudaLleOn_) cudaLle_.adbLine().keyEvent(code, down);
        else            adb_.keyEvent(code, down);
    }
    void mouseMove(int dx, int dy) {
        if (cudaLleOn_) cudaLle_.adbLine().mouseMove(dx, dy);
        else            adb_.mouseMove(dx, dy);
    }
    void mouseButton(bool down) {
        if (cudaLleOn_) cudaLle_.adbLine().mouseButton(down);
        else            adb_.mouseButton(down);
    }
    bool overlay() const { return overlay_; }
    const uint8_t* vram() const { return vram_.data(); }
    // Valkyrie cell accessors (forwarders; see Valkyrie.h).
    Valkyrie& valkyrie() { return video_; }
    const uint8_t (*clut() const)[3] { return video_.clut(); }
    uint32_t videoStride() const { return video_.stride(); }
    uint32_t videoBase() const { return video_.base(); }
    uint8_t videoDepth() const { return video_.depth(); }
    uint32_t videoHres() const { return video_.hres(); }
    uint32_t videoVres() const { return video_.vres(); }
    uint32_t videoPixelClock() const { return video_.pixelClock(); }
    bool videoBlanked() const { return video_.blanked(); }
    // DAFB-shaped aliases: main.cpp's DafbMachine<> thread template reads the
    // frame buffer through these names on every Quadra-class machine.
    uint8_t dafbDepth() const { return video_.depth(); }
    uint32_t dafbStride() const { return video_.stride(); }

    // ── Raster geometry (VideoBeam.h) ───────────────────────────────────
    // Forwarded from Valkyrie's own frame accumulator (the one that fires
    // its VBL). Its unit is pixel-clock × CPU cycles, which is fine: the
    // beam only needs position and frame length to agree with each other.
    int64_t framePos() const { return video_.framePos(); }
    int64_t frameCycles() const { return video_.frameCycles(); }
    int64_t frameActiveCycles() const { return video_.frameActiveCycles(); }
    int     frameTotalLines() const { return video_.frameTotalLines(); }
    uint64_t frameCount() const { return video_.frameCount(); }

    uint32_t dafbBase() const { return video_.base(); }
    void setVideoMonitor(uint8_t code) { video_.setMonitor(code); }

    // VIA2 IFR device lines (Quadra pseudo-VIA: CA1=slot/VBL summary,
    // bit encodings identical to a real VIA's IFR)
    void vblIrq(bool s);
    void scsiIrq(bool s);
    void ascIrq(bool s);                         // EASC half-empty → IFR bit 4
    void sccIrqLine(bool s) { sccIrq_ = s; updateIrq(); }

    // Debug hooks (q605_trace)
    std::function<void(uint32_t, bool, uint32_t)> onIoAccess;   // addr, isWrite, value
    std::function<void(uint32_t, bool)> onBusError;             // addr, isWrite
    // RAM write-watch: fires onRamWrite(addr,size,value) when a write's
    // byte range covers ramWatch_ (0 = disabled). Diagnostic only.
    uint32_t ramWatch_ = 0;
    std::function<void(uint32_t, int, uint32_t)> onRamWrite;

    // Quadra pseudo-VIA (VIA2) state — MAME devices/machine/pseudovia.cpp
    // quadra flavor: reg 0 = port B, 1/15 = port A (nubus IRQs, active
    // low, bit 6 = VBL), 13 = IFR, 14 = IER; IFR/IER mask $1B =
    // DRQ(0) | slots(1) | SCSI(3) | ASC(4).
    bool via2IrqAsserted() const { return (pvIfr_ & pvIer_ & 0x1B) != 0; }
    uint8_t via2Ifr() const { return pvIfr_; }   // Q6.5b diag
    uint8_t via2Ier() const { return pvIer_; }

    // A Mac ROM's first longword IS its checksum (V8Memory pattern).
    uint32_t romChecksum() const {
        if (rom_.size() < 4) return 0;
        return uint32_t(rom_[0]) << 24 | uint32_t(rom_[1]) << 16
             | uint32_t(rom_[2]) << 8  | uint32_t(rom_[3]);
    }

    // ── Save states: the machine chunk (V8Memory pattern) ───────────────
    // Valkyrie replaces DAFB and the F108 ATA IRQ line travels too. Out:
    // rom_, machineId_/cudaLleOn_ (profile + MCU wiring), cpu_/jitGuard_.
    template <class Ar> void visit(Ar& ar) {
        ar.blob(ram_);
        ar.blob(vram_);
        ar(via1_, cuda_, cudaLle_, adb_, scc_, asc_, swim_,
           drive0_, drive1_, scsi_, video_);
        for (auto& d : scsiDisks_) ar(d);
        ar(totalRam_, overlay_, sccIrq_,
           pvIfr_, pvIer_, pvPortB_, nubusIrqs_, ascLine_,
           iosbRegs_, ataIrq_,
           scsiReadCycles_, scsiWriteCycles_,
           scsiDmaReadCycles_, scsiDmaWriteCycles_,
           ascCycAcc_, swimLastCpu_, swimCycAcc_, viaEClock_, tickAcc_);
        if constexpr (Ar::loading) {
            if (jitGuard_) jitGuard_->invalidate();
        }
    }

private:
    uint8_t viaAccess8(uint32_t addr, bool write, uint8_t v);
    uint8_t via2Access8(uint32_t addr, bool write, uint8_t v);
    void via2Recalc();
    void viaSync();
    [[noreturn]] void busError(uint32_t addr, bool write) const;
    uint8_t ioRead8(uint32_t addr);
    void ioWrite8(uint32_t addr, uint8_t v);
    uint8_t scsiDmaRead_();                  // DRQ-gated pseudo-DMA byte in
    void    scsiDmaWrite_(uint8_t v);        // DRQ-gated pseudo-DMA byte out
    void    scsiPoll_();                      // feed 53C96 irq()/drq() to VIA2

    std::vector<uint8_t> ram_, rom_, vram_;
    Via6522 via1_;
    // Cuda flavor: TIP/BYTEACK active low; the machine clock paces the
    // per-byte µs handshake and the RTC seconds heartbeat
    Egret cuda_{via1_, true, kCpuHz};
    CudaLle cudaLle_{via1_, kCpuHz};     // real-firmware path (opt-in)
    bool cudaLleOn_ = false;
    AdbBus adb_;
    Scc8530 scc_;
    // PrimeTime's IOSB audio cell at $50014000: $BB version, stereo FIFO A/B,
    // 22.257 kHz at C15M (15.6672 MHz), level IRQ on pseudo-VIA2 bit 4.
    // This is the IOSB ASC verified by ASCTester on an LC 475, not the $B0
    // discrete EASC despite MAME iosb.cpp's historical ASC_EASC wiring.
    AscIosb asc_;
    int64_t ascCycAcc_ = 0;        // 25 MHz CPU → 15.6672 MHz ASC clock bridge
    Swim2 swim_;                   // PrimeTime SWIM2 at $5001E000
    SonyDrive drive0_;             // internal SuperDrive (SWIM2 soft-select A)
    SonyDrive drive1_;             // external SuperDrive (soft-select B)
    // MAME swim2_device::read/write call sync() on every access so the
    // FIFO drains between a write-ACTION and the handshake poll. Batching
    // only from Q630Cpu::catchUp left the ROM spinning on bit 7 forever.
    void syncSwimFromCpu();
    int64_t swimLastCpu_ = -1;     // <0: latch on first sync
    int64_t swimCycAcc_ = 0;       // CPU→C15M fractional bridge (shared timeline)
    Ncr53c96 scsi_;                // TurboSCSI 53C96 (Q6)
    ScsiDisk scsiDisks_[7];        // by SCSI ID; [0] = boot drive
    Q630Cpu* cpu_ = nullptr;

    void jitMapChanged();
    jit::CodeGuard* jitGuard_ = nullptr;   // JIT code invalidation

    uint32_t totalRam_;
    // $5FFFFFFC board ID (MAME macquadra630.cpp): Quadra 630 $A55A2252,
    // LC/Performa 580 $A55A225A. POM68K_Q630_ID overrides (hex).
    uint32_t machineId_ = 0xA55A2252u;
    bool overlay_ = true;
    bool sccIrq_ = false;

    // Quadra pseudo-VIA registers
    uint8_t pvIfr_ = 0, pvIer_ = 0, pvPortB_ = 0;
    uint8_t nubusIrqs_ = 0xFF;     // active low; bit 6 = VBL (MEMCjr video)
    bool ascLine_ = false;          // live level, re-sampled after IFR ack

    uint16_t iosbRegs_[0x20] = {}; // $50018000, u16 every $100
    bool ataIrq_ = false;          // F108 ATA IRQ → PrimeTime II $1A100 bit 5

    // TurboSCSI wait-state cell (LLE step 9 — MAME iosb.cpp:144-148 defaults,
    // :482-495 register stalls, :498-552 waitstated DMA alias, :606-618
    // programming). Register accesses always cost 3 CPU cycles; the DMA
    // window's waitstated alias (byte-address bit 19, the `.select(0xfc0000)`
    // bit MAME tests via BIT(offset<<1,18)) costs the guest-programmed
    // scsiDma*Cycles_ — IOSB reg 2 bits 8-9 (read) / 11-12 (write) through
    // times[4] = {5,5,4,3}.
    int scsiReadCycles_ = 3, scsiWriteCycles_ = 3;
    int scsiDmaReadCycles_ = 3, scsiDmaWriteCycles_ = 3;

    // Valkyrie video cell ($50F2A000 regs / $50F24000 RAMDAC / VRAM).
    Valkyrie video_{kCpuHz};

    // 60.15 Hz CA1 tick, derived from CPU cycles
    via_eclock::Ticker viaEClock_;   // exact 783.36 kHz (ViaEClock.h)
    int64_t tickAcc_ = 0;
};
