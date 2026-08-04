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
#include "jit/JitGuard.h" 
#include "Via6522.h"
#include "PseudoVia.h"
#include "Ariel.h"
#include "Egret.h"
#include "CudaLle.h"
#include "AdbBus.h"
#include "Asc.h"
#include "Ncr5380.h"
#include "ScsiDisk.h"
#include "Swim1.h"
#include "Swim2.h"
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
    static constexpr int64_t  kCpuHzTv = 31334400;   // C32M (Mac TV 030)
    static constexpr int64_t  kViaHz = 783360;       // C7M / 10

    // V8-family machine profile (MAME maclc.cpp): the LC II is the
    // reference; the LC is the same board with a 68020 and 2 MB soldered
    // (set_baseram_is_4M(false), maclc.cpp:451); the Classic II swaps
    // the V8 for its EAGLE derivative — mono 512×342 scanned out of MAIN
    // RAM at $1F9A80 (v8.cpp:667-691), no monitor sense, PA id $92. The
    // Color Classic runs the SPICE derivative (v8.cpp:693-929): built-in
    // 512×384 Trinitron (fixed sense 2, 16bpp capable), SWIM2 in the gate
    // array, Sonora-class EASC ($BC), a 1 MB ROM, PA id $82 — and a Cuda
    // MCU instead of the Egret (maclc.cpp:471-517, firmware 341S0417).
    // The Macintosh TV runs TINKER BELL (343S1109), a Spice evolution
    // (v8.cpp:931-1063 + maclc.cpp:519-560 mactv): PA id $84, fixed 13"
    // 640×480 sense ($06 << 3), 68030 @ C32M with no FPU, Cuda MCU
    // (341s0789, Cuda 2.38), 8 MB RAM cap, its own 1 MB ROM ($EAF1678D).
    enum class Model { LcII, Lc, ClassicII, ColorClassic, MacTv };

    // totalRam: 4, 6, 8 or 10 MB (motherboard + SIMM pair);
    // 10 MB is the V8 hard limit (12 MB installed, 2 MB wasted).
    // cpuHz: C15M for everything but the Mac TV's C32M — the gate array
    // stays in the C15M domain, ticks are rescaled (the VASP pattern).
    explicit V8Memory(uint32_t totalRam = 0xA00000, Model model = Model::LcII,
                      int64_t cpuHz = kCpuHz);
    Model model() const { return model_; }
    int64_t cpuHz() const { return cpuHz_; }
    // Spice-class gate arrays (Color Classic Spice + Mac TV Tinker Bell):
    // integrated SWIM2, Sonora-class EASC, Cuda MCU, 1 MB ROM.
    bool spiceClass() const {
        return model_ == Model::ColorClassic || model_ == Model::MacTv;
    }
    // Eagle framebuffer (Classic II): fixed RAM-device offset, MAME
    // v8.cpp:670 m_ram_ptr[0x1f9a80/4] — our ram_ mirrors MAME's device
    // layout, so the same flat offset applies in every RAM config.
    const uint8_t* eagleFrame() const { return ram_.data() + 0x1F9A80; }

    bool loadRom(const std::vector<uint8_t>& data);  // 512 KB (1 MB Spice) flat image
    uint32_t romSize() const { return romSize_; }
    void reset();                                    // overlay on, V8 regs cleared
    int cyclesToNextEvent() const {
        // Firmware LLE is the tightest clock on these boards. Preserve the
        // historical batching only for the explicit HLE fallback.
        return egretLleOn_ ? egretLle_.cyclesToNextEvent() : 128;
    }

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

    // ── JIT hooks (src/jit/POM68K_JIT.md) ──────────────────────────────
    // Same contract as Q605Memory's: codeSpan hands the JIT a host pointer
    // to PLAIN memory only (RAM through the bank remap, ROM once the
    // overlay is down); everything with a read side effect — the whole map
    // while the overlay is up (clearing it IS a read side effect here),
    // VRAM's pull on the video timing, all of $F00000+ — is refused.
    // dataSpan is the write-aware twin; the 030 has no data window today,
    // but the engine's fillDtlb probes through it and simply never fills
    // (pomJitProbeData is 040-only), so it stays cheap and honest.
    const uint8_t* codeSpan(uint32_t phys, uint32_t& len) const;
    uint8_t* dataSpan(uint32_t phys, uint32_t& len, bool write);
    void setJitGuard(jit::CodeGuard* g) { jitGuard_ = g; }
    uint32_t ramBytes() const { return uint32_t(ram_.size()); }
    void jitMapChanged();

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
    AscSonora& ascSonora() { return ascSonora_; }
    // Audio host facade — dispatches to the model's ASC block (Spice =
    // Sonora EASC, others = V8 ASC) so LcMachine::drain stays model-blind.
    int ascAvailable() const {
        return spiceClass() ? ascSonora_.available() : asc_.available();
    }
    int16_t ascPop() {
        return spiceClass() ? ascSonora_.pop() : asc_.pop();
    }
    Ncr5380& scsi() { return scsi_; }
    ScsiDisk& scsiDisk() { return scsiDisks_[0]; }
    ScsiDisk& scsiDiskAt(int id) { return scsiDisks_[id & 7]; }
    // Attach a disk image at a SCSI ID (0 = boot drive, 1-6 = secondary
    // volumes picked up by the System's boot-time bus scan).
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
    // SWIM1 comes up IWM-compatible (GCR, the proven Plus Iwm inside);
    // the ISM personality (1.44 MB MFM) engages on the driver's 1-0-1-1
    // mode-register magic (Swim1.h). The Spice (Color Classic) carries a
    // SWIM2 in the gate array instead (v8.cpp:724-727).
    Swim1& swim() { return swim_; }
    Iwm& iwm() { return swim_.iwm(); }
    Swim2& swim2() { return swim2_; }
    // Cuda-flavored MCU (Color Classic + Mac TV): both the HLE (Egret with
    // the PB4/PB5 polarity inverted) and the LLE firmware follow it.
    bool hasCudaMcu() const { return spiceClass(); }
    SonyDrive& internalDrive() { return drive_; }
    bool insertDisk(const std::string& path) { return drive_.insert(path); }
    void ejectDisk() { drive_.eject(); }
    // Mechanical drive sounds (GUI only; headless leaves sinks null).
    void attachDriveSounds(FloppySoundSink* floppy, FloppySoundSink* hdd) {
        drive_.setSoundSink(floppy);
        for (ScsiDisk& d : scsiDisks_) d.setSoundSink(hdd);
    }
    bool cpuHeld() const {                             // power-on reset hold
        return egretLleOn_ ? egretLle_.cpuHeld() : egret_.cpuHeld();
    }
    // Egret firmware LLE (roms/egret/341s0850.bin + POM68K_EGRET_LLE) —
    // the Q605 CudaLle rollout applied to the LC II's Egret flavor.
    bool egretLleActive() const { return egretLleOn_; }
    CudaLle& egretLle() { return egretLle_; }
    // Live PRAM view: the MCU's internal RAM under the LLE, the HLE's
    // array otherwise (setMonitorSense's video-sPRAM parking uses these).
    uint8_t pramByte(int i) const {
        return egretLleOn_ ? egretLle_.pram(i) : egret_.pram(i);
    }
    void setPramByte(int i, uint8_t v) {
        if (egretLleOn_) egretLle_.setPram(i, v);
        else             egret_.setPram(i, v);
    }
    // Battery persistence — Egret file format, re-mirrored under the LLE.
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
    // Host input events — AdbLine under the firmware LLE, AdbBus otherwise.
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
        for (int i = 0; i < 3; i++) vidSpram_[montype_ & 7][i] = pramByte(0x58 + i);
        vidSpramSaved_[montype_ & 7] = true;
        montype_ = m;
        if (vidSpramSaved_[m & 7])
            for (int i = 0; i < 3; i++) setPramByte(0x58 + i, vidSpram_[m & 7][i]);
        // else: a monitor never configured this session — let the ROM/System
        // set it up (it comes up B&W until "256 couleurs" + restart).
    }

    // ── Raster geometry (VideoBeam.h) ───────────────────────────────────
    // The SAME accumulator that generates the VBL above — the beam is a
    // pure function of it, never a second clock. `frameActiveCycles` is
    // `vblStart_`: the part of the frame the beam spends on visible lines.
    int64_t framePos() const { return framePos_; }
    int64_t frameCycles() const { return frameCycles_; }
    int64_t frameActiveCycles() const { return vblStart_; }
    int     frameTotalLines() const { return frameTotalLines_; }
    uint64_t frameCount() const { return frameCount_; }

    // Device lines into the pseudo-VIA (SCSI lands in O6.5, ASC in O6.6)
    void ascIrq(bool s)  { pvia_.ascIrq(s);  updateIrq(); }
    void scsiIrq(bool s) { pvia_.scsiIrq(s); updateIrq(); }
    void scsiDrq(bool s) { pvia_.scsiDrq(s); updateIrq(); }
    void vblIrq(bool s)  { pvia_.slotIrq(PseudoVia::VBL, s); updateIrq(); }
    void sccIrqLine(bool s) { sccIrq_ = s; updateIrq(); }
    Scc8530& scc() { return scc_; }

    // A Mac ROM's first longword IS its checksum, so this is the cheapest
    // honest identity for a snapshot to pin: restoring against a different
    // ROM would put the guest's cached ROM addresses somewhere else.
    uint32_t romChecksum() const {
        if (rom_.size() < 4) return 0;
        return uint32_t(rom_[0]) << 24 | uint32_t(rom_[1]) << 16
             | uint32_t(rom_[2]) << 8  | uint32_t(rom_[3]);
    }

    // ── Save states: the machine chunk (SaveState.h) ─────────────────────
    // RAM and VRAM go through the zero-run codec; the ROM does not travel at
    // all (it is on disk, and the snapshot header pins its checksum instead).
    //
    // Then every device in a FIXED order — reordering this list silently
    // invalidates existing snapshots, which is why the container is chunked
    // and version-stamped.
    //
    // The gate-array fields at the end are the ones easiest to mistake for
    // configuration. `overlay_` decides whether low memory reads as ROM or
    // RAM, so losing it makes a restored machine boot instead of resume; the
    // SIMM/motherboard window fields are DERIVED from the guest's writes to
    // `config_`, not from the profile, so they are guest state too; and the
    // frame/VIA phase accumulators keep the 60.15 Hz VBL and the 783.36 kHz
    // VIA cadence continuous across the restore.
    //
    // Out: `rom_` and the geometry that describes it, `model_`/`cpuHz_`/
    // `viaDiv_`/`addrMask_`/`mbRam_` (profile identity), `cpu_` and
    // `jitGuard_` (pointers the machine owns), `egretLleOn_` (which MCU path
    // the machine wired up). `totalRam_` IS carried so a size mismatch is
    // detectable rather than silently corrupting the map.
    template <class Ar> void visit(Ar& ar) {
        ar.blob(ram_);
        ar.blob(vram_);

        ar(via_, pvia_, ariel_, egret_, egretLle_, adb_,
           asc_, ascSonora_, scsi_, swim_, swim2_, drive_, scc_);
        for (auto& d : scsiDisks_) ar(d);

        ar(totalRam_, config_, videoConfig_, montype_,
           vidSpram_, vidSpramSaved_, overlay_, sccIrq_);
        ar(simmMapped_, simmOff_, simmPhys_, mbLoc_, mbSize_, mbMapped_);
        ar(viaPhase_, tickAcc_, framePos_, frameCycles_, vblStart_,
           c15Acc_, vblState_, frameCount_);

        if constexpr (Ar::loading) {
            // RAM and the whole address map just changed wholesale. A write
            // cannot express that, so the JIT has to be told explicitly
            // (JitGuard.h § invalidate) or it would replay blocks recorded
            // against the previous session's memory.
            if (jitGuard_) jitGuard_->invalidate();
        }
    }

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
    void holeDump(uint32_t addr) const;   // POM68K_V8_IOHOLE, see .cpp
    void viaSync();                          // E-clock stall (v8.cpp:462-483)

    jit::CodeGuard* jitGuard_ = nullptr;     // JIT code invalidation
    std::vector<uint8_t> ram_, rom_, vram_;
    Via6522 via_;
    PseudoVia pvia_;
    Ariel ariel_;
    // MCU: Egret flavor on LC/LC II/Classic II, Cuda flavor (inverted
    // PB4/PB5 polarity + Cuda firmware) on the Color Classic — both the
    // HLE and the LLE pick the flavor from the model in the constructor.
    Egret egret_;
    CudaLle egretLle_;
    bool egretLleOn_ = false;
    uint8_t xcvrSession_() const {           // → VIA1 PB3 (active path)
        return egretLleOn_ ? egretLle_.xcvrSession() : egret_.xcvrSession();
    }
    AdbBus adb_;
    AscV8 asc_;
    AscSonora ascSonora_;            // Spice/Sonora EASC (Color Classic)
    Ncr5380 scsi_;
    ScsiDisk scsiDisks_[7];          // by SCSI ID; [0] = boot drive
    Swim1 swim_;
    Swim2 swim2_;                    // Spice-integrated SWIM2 (Color Classic)
    SonyDrive drive_;
    Cpu030* cpu_ = nullptr;

    uint32_t totalRam_;
    Model model_ = Model::LcII;
    int64_t cpuHz_ = kCpuHz;                 // C15M; Mac TV = C32M
    int viaDiv_ = 20;                        // cpuHz_ / 783.36 kHz (20 or 40)
    uint32_t romSize_ = kRomSize;            // 512 KB; Spice ROMs are 1 MB
    uint32_t romMask_ = kRomSize - 1;
    uint32_t mbRam_ = kMbRamSize;            // soldered bank: 4 MB, LC = 2 MB
    // Bus decode mask. V8 sees A31 + A23-A0 (maclc.cpp:181). On the LC
    // the 68020's Apple HMMU additionally blanks A31 in 24-bit mode
    // (MAME m68kmmu.h:1357-1361 M68K_HMMU_ENABLE_LC = addr & $FFFFFF),
    // driven by pseudo-VIA PB3 — LOW = 24-bit (maclc.cpp:155-158
    // inverted polarity, v8.cpp:349-352 via2_pb_w). The 030/Eagle models
    // keep the plain V8 mask; their ROMs map 24-bit mode via the PMMU.
    uint32_t addrMask_ = 0x80FFFFFF;
    // RAM banks per config (MAME v8.cpp ram_size, byte offsets into ram_):
    // SIMM at $000000 when enabled, motherboard after it; alias fixed.
    bool simmMapped_ = false;
    uint32_t simmOff_ = 0, simmPhys_ = 0;    // physical SIMM bank
    uint32_t mbLoc_ = 0, mbSize_ = 0;        // motherboard window ($FFFFFFFF = none)
    bool mbMapped_ = false;

    Scc8530 scc_;                            // Z8530 (O6.10: LAP ext ints)
    uint8_t config_ = 0;                     // pseudo-VIA reg 1 (RAM size)
    uint8_t videoConfig_ = 0;                // pseudo-VIA reg $10 bits 0-2
    uint8_t montype_ = 2;                    // 512×384 12" RGB
    uint8_t vidSpram_[8][3] = {};            // parked video sPRAM per sense
    bool vidSpramSaved_[8] = {};
    bool overlay_ = true;
    bool sccIrq_ = false;
    int viaPhase_ = 0;                       // CPU-cycle remainder for ÷20
    int64_t tickAcc_ = 0;                    // 60.15 Hz Bresenham accumulator
    // 512×384 12" RGB frame: dot clock = C15M, 640×407 total dots
    // (v8.cpp:717) — VBL asserted during lines 384-406. Tinker Bell scans
    // 800×525 @ 25.175 MHz instead (v8.cpp:936 set_raw). Both reduced to
    // CPU cycles at reset.
    int64_t framePos_ = 0;
    int64_t frameCycles_ = 640 * 407, vblStart_ = 640 * 384;
    int     frameTotalLines_ = 407;          // blanking included
    uint64_t frameCount_ = 0;                // completed frames (raster seq)
    int64_t c15Acc_ = 0;                     // CPU → C15M device-domain rest
    bool vblState_ = false;
    uint8_t scsiDma_();                      // DRQ-gated window byte read
    void scsiDmaW_(uint8_t v);
};
