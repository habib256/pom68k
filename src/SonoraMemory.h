// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Mac LC III ("Vail") memory map + Sonora gate array ──
// Sonora is the evolved V8 for the 32-bit-clean era (MAME sonora.cpp +
// maclc3.cpp, master 2026-07-24): a memory controller with CONTIGUOUS
// RAM at $00000000 (no V8 config-register banking), the 1 MB ROM at
// $40000000 (mirrored ×16, any read there clears the boot overlay),
// I/O at $50000000 (VIA1, SCC, SCSI 5380 + pseudo-DMA, Sonora EASC,
// SWIM2, pseudo-VIA, Sonora video DAC/ctrl), the machine ID longword
// at $5FFFFFFC ($A55A0001 = LC III), and 1 MB VRAM at $60000000. The
// ADB MCU is an Egret (firmware 341S0851; the LC II rollout pattern),
// the video is the mv_sonora cell (5 modelines, CLUT, monitor sense in
// the video control registers — NOT the pseudo-VIA like the V8).
// CPU: 68030 @ 25 MHz (SonoraCpu), VIA at 783.36 kHz (C7M/10).
// Gate: tests/lc3_boot_etalon.cpp.

#pragma once
#include "jit/JitGuard.h"
#include "Via6522.h"
#include "PseudoVia.h"
#include "Egret.h"
#include "CudaLle.h"
#include "AdbBus.h"
#include "Asc.h"
#include "Ncr5380.h"
#include "ScsiDisk.h"
#include "Swim2.h"
#include "SonyDrive.h"
#include "Scc8530.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class SonoraCpu;

class SonoraMemory {
public:
    static constexpr uint32_t kRomSize = 0x100000;   // 1 MB
    static constexpr uint32_t kVramSize = 0x100000;  // 1 MB, fully populated
    static constexpr int64_t  kCpuHz = 25000000;     // 25 MHz XTAL (LC III)
    static constexpr int64_t  kCpuHzPlus = 33333333; // 33.33 MHz (LC III+)
    static constexpr int64_t  kViaHz = 783360;       // C7M / 10 (bus, both)
    static constexpr uint32_t kIdLc3 = 0xA55A0001;   // $5FFFFFFC (maclc3.cpp)
    static constexpr uint32_t kIdLc3Plus = 0xA55A0003;
    // The all-in-one Sonora siblings share the gate array + a separate
    // universal ROM ($EDE66CBD): LC 520 @ 25 MHz and LC 550 @ 33 MHz. The
    // MAME maclc3.cpp ids ARE in this ROM's machine table (two entries each,
    // video type picked by the monitor sense — $32/$4B for $0100, $4A/$4D
    // for $0101); they need cudaAdb=true (Cuda 341S0060 LLE), see
    // docs/LC520_BRINGUP.md.
    static constexpr uint32_t kIdLc520 = 0xA55A0100;
    static constexpr uint32_t kIdLc550 = 0xA55A0101;

    // Sonora video modelines (MAME mv_sonora.cpp:20-26): sense id,
    // dot clock, horizontal total/front/sync/back, vertical ditto.
    struct Modeline {
        uint8_t id; int64_t dot;
        int htot, hfp, hs, hbp, vtot, vfp, vs, vbp;
        bool supports16bpp;
        int hres() const { return htot - hfp - hs - hbp; }
        int vres() const { return vtot - vfp - vs - vbp; }
    };
    static const Modeline* modeline(uint8_t id);     // nullptr = none

    // cpuHz/machineId default to the LC III (25 MHz, $A55A0001); the LC III+
    // passes kCpuHzPlus + kIdLc3Plus. The VIA/bus stays at C7M either way.
    // cudaAdb: the LC 520/550 AIO family carries a Cuda (MAME maclc3.cpp:379
    // CUDA_V2XX 341s0060), not the LC III's Egret — its ROM's reset handshake
    // ($408D1AE6: ByteAck low, wait /TREQ deassert) only a Cuda answers.
    explicit SonoraMemory(uint32_t totalRam = 0x800000,
                          int64_t cpuHz = kCpuHz,
                          uint32_t machineId = kIdLc3,
                          bool cudaAdb = false);
    int64_t cpuHz() const { return cpuHz_; }

    bool loadRom(const std::vector<uint8_t>& data);  // 1 MB flat image
    void reset();

    uint8_t  read8(uint32_t addr);
    uint16_t read16(uint32_t addr);
    void     write8(uint32_t addr, uint8_t v);
    void     write16(uint32_t addr, uint16_t v);
    uint8_t  peek8(uint32_t addr) const;             // side-effect-free

    void setCpu(SonoraCpu* cpu) { cpu_ = cpu; }

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
    // Conservative machine-cycle bound on the next observable transition
    // (the V8Memory pattern): the firmware MCU is the tightest clock; the
    // wrapper caps the result at its historical batch, so anything not
    // bounded here keeps exactly its former cadence.
    int cyclesToNextEvent() const {
        return egretLleOn_ ? egretLle_.cyclesToNextEvent() : 0x7fffffff;
    }

    Via6522& via1() { return via_; }
    PseudoVia& pseudoVia() { return pvia_; }
    Egret& egret() { return egret_; }
    AdbBus& adb() { return adb_; }
    AscSonora& asc() { return asc_; }
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
    Swim2& swim() { return swim_; }
    SonyDrive& internalDrive() { return drive_; }
    bool insertDisk(const std::string& path) { return drive_.insert(path); }
    void ejectDisk() { drive_.eject(); }
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
    void mouseButton(bool down, int button = 0) {
        if (egretLleOn_) egretLle_.adbLine().mouseButton(down, button);
        else if (button == 0) adb_.mouseButton(down);
    }
    bool overlay() const { return overlay_; }
    uint8_t ramConfig() const { return 0; }          // no V8 config register
    Scc8530& scc() { return scc_; }
    void sccIrqLine(bool s) { sccIrq_ = s; updateIrq(); }
    void ascIrq(bool s)  { pvia_.ascIrq(s);  updateIrq(); }
    void scsiIrq(bool s) { pvia_.scsiIrq(s); updateIrq(); }
    void scsiDrq(bool s) { pvia_.scsiDrq(s); updateIrq(); }

    // ── Sonora video state (decoder in SonoraVideo.h) ──
    const uint8_t* vram() const { return vram_.data(); }
    uint32_t pen(int i) const { return pens_[i & 0xFF]; }
    uint8_t videoMode() const { return vidMode_; }   // $9F = blanked/off
    uint8_t videoDepth() const { return vidDepth_; } // 0..4 = 1..16 bpp
    const Modeline* currentModeline() const { return mode_; }

    // ── Raster geometry (VideoBeam.h) ───────────────────────────────────
    // The SAME accumulator that generates the modeline-driven VBL — the
    // beam is a pure function of it, never a second clock.
    int64_t framePos() const { return framePos_; }
    int64_t frameCycles() const { return frameCycles_; }
    int64_t frameActiveCycles() const { return vblStart_; }
    int     frameTotalLines() const { return frameTotalLines_; }
    uint64_t frameCount() const { return frameCount_; }

    uint8_t monitorSense() const { return montype_; }
    // Plug a different monitor (2 = 512×384 12", 6 = 640×480 13"); read
    // by the ROM through video control register 2 at boot.
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
    // mode_ is DERIVED (modeline(vidMode_) is an invariant of vctrlWrite),
    // so it is re-resolved on load rather than serialized.
    template <class Ar> void visit(Ar& ar) {
        ar.blob(ram_);
        ar.blob(vram_);
        ar(via_, pvia_, egret_, egretLle_, adb_, asc_, scsi_,
           swim_, drive_, scc_);
        for (auto& d : scsiDisks_) ar(d);
        ar(totalRam_, overlay_, sccIrq_);
        ar(pens_, palAddr_, palIdx_, palControl_, palColkey_, palRgb_,
           vidMode_, vidDepth_, vidMonId_, vidVtest_, montype_);
        ar(viaAcc_, tickAcc_, swimAcc_, framePos_, frameCycles_,
           vblStart_, vblState_, frameTotalLines_, frameCount_);
        if constexpr (Ar::loading) {
            mode_ = modeline(vidMode_);
            if (jitGuard_) jitGuard_->invalidate();
        }
    }

private:
    uint8_t viaAccess8(uint32_t addr, bool write, uint8_t v);
    void viaSync();                  // E-clock stall (sonora.cpp:347-368)
    uint8_t vctrlRead(int reg);
    void vctrlWrite(int reg, uint8_t v);
    uint8_t dacRead(int reg) const;
    void dacWrite(int reg, uint8_t v);
    uint8_t scsiDma_();
    void scsiDmaW_(uint8_t v);
    [[noreturn]] void busError() const;

    std::vector<uint8_t> ram_, rom_, vram_;
    Via6522 via_;
    PseudoVia pvia_;
    Egret egret_;
    CudaLle egretLle_;               // Egret firmware LLE (341S0851)
    bool egretLleOn_ = false;
    uint8_t xcvrSession_() const {
        return egretLleOn_ ? egretLle_.xcvrSession() : egret_.xcvrSession();
    }
    AdbBus adb_;
    AscSonora asc_{kCpuHz};
    Ncr5380 scsi_;
    ScsiDisk scsiDisks_[7];
    Swim2 swim_;
    SonyDrive drive_;
    Scc8530 scc_;
    SonoraCpu* cpu_ = nullptr;

    uint32_t totalRam_;
    int64_t  cpuHz_ = kCpuHz;         // 25 MHz LC III / 33.33 MHz LC III+
    uint32_t machineId_ = kIdLc3;     // $5FFFFFFC model longword
    jit::CodeGuard* jitGuard_ = nullptr;   // JIT code invalidation
    bool overlay_ = true;
    bool sccIrq_ = false;

    // Sonora video registers (mv_sonora.cpp): CLUT + mode/depth/sense.
    uint32_t pens_[256] = {};
    uint8_t palAddr_ = 0, palIdx_ = 0, palControl_ = 0, palColkey_ = 0;
    uint8_t palRgb_[3] = {};
    uint8_t vidMode_ = 0x9F, vidDepth_ = 0, vidMonId_ = 8, vidVtest_ = 0;
    const Modeline* mode_ = nullptr;
    uint8_t montype_ = 6;            // 640×480 13" RGB (16bpp-capable)

    // Clock dividers (25 MHz CPU): VIA φ2, 60.15 Hz CA1, SWIM2 C15M cell
    // clock, and the modeline-driven VBL.
    int64_t viaAcc_ = 0;
    int64_t tickAcc_ = 0;
    int64_t swimAcc_ = 0;
    int64_t framePos_ = 0;           // CPU cycles into the current frame
    int64_t frameCycles_ = 0, vblStart_ = 0;   // from the active modeline
    int     frameTotalLines_ = 0;    // modeline vtot (blanking included)
    uint64_t frameCount_ = 0;        // completed frames (raster beam seq)
    bool vblState_ = false;
};
