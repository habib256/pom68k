// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Mac Plus memory map (24-bit) ──
// RAM $000000-$3FFFFF (up to 4 MB), ROM $400000 (128 KB, mirrored), SCSI
// $580000, SCC read $9xxxxx / write $Bxxxxx, IWM $Dxxxxx, VIA $Exxxxx.
// Boot overlay maps ROM at $000000 and RAM at $600000 until the ROM clears
// VIA PA4. Video framebuffer: main = ramSize-0x5900 (512×342, 1 bpp).
// Source of truth: Guide to the Macintosh Family Hardware; MAME mac128.cpp.
// Gates: tests/cpu_smoke.cpp, tests/storage_profile_test.cpp.

#pragma once
#include "DeviceSnapshot.h"
#include "CoreConfig.h"
#include "DaynaPortBus.h"
#include "Via6522.h"
#include "Rtc.h"
#include "Swim1.h"
#include "SonyDrive.h"
#include "Scc8530.h"
#include "MacInput.h"
#include "AdbVia.h"
#include "AdbBus.h"
#include "Ncr5380.h"
#include "CdAudioPump.h"
#include "ScsiDisk.h"
#include "jit/JitGuard.h"
#include <array>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>

class Cpu68k;

class MacMemory {
public:
    // The registry this machine reports its LLE/HLE outcomes into.
    // Injected with CoreConfig so a session owns it instead of the
    // process (2026-08-27); save states and the Périphériques window
    // read it back from the machine rather than from a global.
    pom68k::lle::Registry& lleRegistry() const { return *lle_; }

    // Ceiling of the family, not the size of every member: ramSize() and
    // romSize() are the live values (the 128K/512K are smaller on both).
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
    //  * no mouse quadrature on PB4/PB5 (the mouse is an ADB device);
    //  * SWIM + SuperDrive on the SE FDHD and Classic; Plus and original SE
    //    keep the IWM-compatible personality and 800K-only mechanism.
    //
    // The two machines BELOW the Plus are the same board with less of it:
    // a 64 KB ROM, 128 KB / 512 KB of RAM, the M0110 keyboard and quadrature
    // mouse the Plus uses, the single-sided 400K mechanism — and no SCSI at
    // all, which is why `hasScsi()` exists (mac128.cpp macplus_map adds the
    // 5380 that mac128_map does not have).
    enum class Model { Plus, SE, SEFDHD, Classic, Mac128, Mac512, Mac512e };

    explicit MacMemory(
        const pom68k::CoreConfig& coreConfig, Model model = Model::Plus);
    // Re-profile before loadRom(): main() constructs the machine before it has
    // read the ROM, and the compact models are told apart by its checksum.
    void setModel(Model m);
    Model model() const { return model_; }
    bool isAdb() const {
        return model_ == Model::SE || model_ == Model::SEFDHD ||
               model_ == Model::Classic;
    }
    // The SCSI bus arrived with the Plus. On the 128K/512K nothing decodes
    // $580000-$5FFFFF and the quarter answers address-dependent open bus.
    bool hasScsi() const {
        return model_ != Model::Mac128 && model_ != Model::Mac512 && model_ != Model::Mac512e;
    }
    // The 400K mechanism whose spindle speed the board commands by PWM, and
    // which the 64K ROM calibrates against the tachometer. The Plus's 800K
    // drive regulates itself, so its ROM never runs that calibration and
    // never reads the odd sound bytes back — see SonyDrive::pwmPush.
    // One scan line's speaker fetch: the buffer's even byte, and the VIA
    // bits that shape it at that moment — PA2-0 volume, PB7 /enable.
    struct SoundLine { uint8_t sample = 0x80, via = 0x80; };
    static constexpr int kSoundLines = 370;
    const SoundLine& soundLine(int line) const { return soundLine_[size_t(line)]; }
    bool hasPwmSpindle() const {
        return model_ == Model::Mac128 || model_ == Model::Mac512;
    }
    uint32_t romSize() const { return romSize_; }
    // Physical RAM, a PROFILE fact on this board rather than a constant:
    // 128 KB on the Mac 128K, 512 KB on the 512K, 4 MB on the Plus and the
    // ADB compacts. The map mirrors it through $3FFFFF (MAME `offset &
    // ram_mask`), so every RAM index below masks with ramSize_-1 — and the
    // screen and sound buffers, which are quoted from the TOP of RAM, move
    // with it.
    uint32_t ramSize() const { return ramSize_; }
    AdbVia& adbVia() { return adbVia_; }
    AdbBus& adb() { return adb_; }
    bool adbLleActive() const { return adbVia_.lle(); }
    // Uniform MachineHost input surface. ADB compacts consume native ADB key
    // codes; the Plus hands the same code to the M0110A model, which frames
    // it (one byte for the main block, $79-prefixed for the keypad and the
    // arrows — MacInput.cpp). The GUI therefore queues one command format
    // for every platform.
    void keyEvent(uint8_t code, bool down) {
        if (isAdb()) adbVia_.keyEvent(code, down);
        else kbd_.keyEvent(code, down);
    }
    void mouseMove(int dx, int dy) {
        if (isAdb()) adbVia_.mouseMove(dx, dy);
        else mouse_.move(dx, dy);
    }
    void mouseButton(bool down, int button = 0) {
        if (isAdb()) adbVia_.mouseButton(down, button);
        else if (button == 0) mouse_.setButton(down);
    }
    void adbMouseMove(int dx, int dy) { adbVia_.mouseMove(dx, dy); }
    void adbMouseButton(bool down, int button = 0) { adbVia_.mouseButton(down, button); }

    bool loadRom(const std::vector<uint8_t>& data);
    void installRom(const uint8_t* data, size_t n);  // built-in demo/test ROM
    void reset();                                    // asserts the boot overlay

    uint8_t  read8(uint32_t addr);
    uint16_t read16(uint32_t addr);
    void     write8(uint32_t addr, uint8_t v);
    void     write16(uint32_t addr, uint16_t v);

    // Side-effect-free read, the counterpart every other machine map already
    // carries (V8Memory, MacIIMemory, …). read8() cannot be used for
    // inspection: on this map it clears VIA interrupt flags, advances the
    // IWM state machine and hands the SCC a status latch. Plain memory only;
    // everything with a read side effect answers $FF rather than being
    // touched. Used by `jit_lockstep_68000_test` to diff guest RAM between
    // two machines without perturbing either.
    uint8_t  peek8(uint32_t addr) const;

    // Screen buffer bases, selected by VIA PA6 (1 = main, 0 = alternate).
    // GttMFH; MAME MAC_MAIN_SCREEN_BUF_OFFSET; Mini vMac kMain_Offset.
    uint32_t mainScreenBase() const { return ramSize_ - 0x5900; }
    uint32_t altScreenBase()  const { return ramSize_ - 0xD900; }
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
    // Battery file (Rtc.h): the compacts keep their Control Panel settings
    // between sessions like every other platform. The 343-0040 part has
    // only 20 bytes of NVRAM on silicon, but POM68K runs the -0042
    // superset (Rtc.h header) — the file is the same flat 256 bytes
    // everywhere, and the compact ROMs only ever touch the low end.
    bool loadPram(const std::string& path) { return rtc_.loadPram(path); }
    void savePram(const std::string& path) { rtc_.savePram(path); }
    Iwm& iwm() { return swim_.iwm(); }
    Swim1& swim() { return swim_; }
    bool hasSuperDrive() const {
        return model_ == Model::SEFDHD || model_ == Model::Classic;
    }
    SonyDrive& internalDrive() { return drive_; }
    SonyDrive& externalDrive() { return externalDrive_; }
    // The internal-connector line (Iwm.h): `drive_` answers VIA1 PA4 low.
    // The dual-floppy SE and SE FDHD fit a second mechanism on PA4 high
    // (CoreStorageConfig::secondInternalFloppy); the Classic's ROM drives
    // the same line, but its board has no second internal connector.
    bool hasInternalSelectLine() const { return isAdb(); }
    bool hasSecondInternalDrive() const {
        return secondInternalFitted_ &&
               (model_ == Model::SE || model_ == Model::SEFDHD);
    }
    SonyDrive& secondInternalDrive() { return secondInternalDrive_; }
    // The boards whose second internal connector exists (fitted or not):
    // what the Disques window offers to fit at the next boot.
    bool canFitSecondInternalDrive() const {
        return model_ == Model::SE || model_ == Model::SEFDHD;
    }
    bool insertSecondInternalDisk(const std::string& path) {
        return hasSecondInternalDrive() && secondInternalDrive_.insert(path);
    }
    void ejectSecondInternalDisk() { secondInternalDrive_.eject(); }
    // Every compact has the DB-19 external drive port (MAME mac128.cpp
    // connects both drives by default).
    static constexpr bool externalFloppyPort() { return true; }
    bool insertDisk(const std::string& path) { return drive_.insert(path); }
    void ejectDisk() { drive_.eject(); }
    bool insertExternalDisk(const std::string& path) {
        return externalDrive_.insert(path);
    }
    void ejectExternalDisk() { externalDrive_.eject(); }
    Scc8530& scc() { return scc_; }
    MacMouse& mouse() { return mouse_; }
    MacKeyboard& keyboard() { return kbd_; }
    bool sccIrq() const { return scc_.irqAsserted(); }
    Ncr5380& scsi() { return scsi_; }
    ScsiDisk& scsiDisk() { return scsiDisks_[0]; }
    // The DaynaPort SCSI/Link, if POM68K_DAYNAPORT put one on the bus
    // (DaynaPortBus.h); AtalkHub wires it to the in-process NAT.
    DaynaPort& daynaPort() { return dayna_; }
    bool attachScsi(const std::string& path, bool writeBack = false,
                    int id = 0) {
        if (id < 0 || id > 6 || !scsiDisks_[id].open(path, writeBack))
            return false;
        scsi_.attach(&scsiDisks_[id], id);
        return true;
    }
    // The reverse of attachScsi, for a FIXED disk the guest has let go of
    // (docs/SCSI_HOTPLUG.md § 7): the target leaves the bus between two
    // quanta and the image is dropped. The host decides WHEN — only once
    // the guest's VCB queue no longer holds a volume on that bay
    // (GuestScsiView); this call does not look. False and nothing changed
    // when no fixed disk sits there, or while the controller has a
    // session open on it (`scsi().sessionOn(id)`; MachineHost retries).
    // The CD bays keep their drive: that is ejectBayMedia.
    bool detachScsi(int id) {
        if (id < 1 || id > 6 || scsiDisks_[id].cdrom() || !scsiDisks_[id].present())
            return false;
        if (!scsi().detach(id)) return false;
        scsiDisks_[id].close();
        return true;
    }
    bool attachCdrom(const std::string& path, int id = 3) {
        if (id < 0 || id > 6 || !scsiDisks_[id].openCdrom(path)) return false;
        scsi_.attach(&scsiDisks_[id], id);
        return true;
    }
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
    bool insertBayMedia(int id, const std::string& path) {
        return bayIsCdrom(id) && scsiDisks_[id].openCdrom(path);
    }
    void ejectBayMedia(int id) {
        if (bayIsCdrom(id)) scsiDisks_[id].eject();
    }
    // Mechanical drive sounds (GUI only; headless leaves sinks null).
    void attachDriveSounds(FloppySoundSink* floppy, FloppySoundSink* hdd) {
        drive_.setSoundSink(floppy);
        externalDrive_.setSoundSink(floppy);
        secondInternalDrive_.setSoundSink(floppy);
        for (ScsiDisk& disk : scsiDisks_) disk.setSoundSink(hdd);
    }
    // The CD-audio lead. A playing disc is decoded by the drive and
    // mixed as analog on a real machine, so its samples go to the host
    // beside the mechanisms, never through the sound chip
    // (CdAudioSink.h).
    void attachCdAudioSink(CdAudioSink* cd) {
        for (ScsiDisk& d : scsiDisks_) d.setCdAudioSink(cd);
    }

    // ── JIT memory hooks (src/jit/POM68K_JIT.md § 4) ────────────────────
    // The compacts' map is flat and 24-bit, so the address the window probe
    // reports IS the bus address (pomJitProbeCode refuses anything above
    // $FFFFFF for exactly that reason) — no remap to reconcile, unlike the
    // GLUE and V8 boards.
    //
    // Handed out: RAM and the ROM window. Refused: every I/O quarter, the
    // $600000 RAM alias (it exists only while the overlay is up, and it is
    // a SECOND name for bytes the window already reaches at $000000 — one
    // alias the guard would have to mirror for nothing), and the whole map
    // while the overlay is up on the ADB compacts, where the overlay drops
    // on the first low-RAM WRITE (mac128.cpp ram_w_se) and the window has
    // no way to see it coming.
    const uint8_t* codeSpan(uint32_t phys, uint32_t& len) const;
    uint8_t* dataSpan(uint32_t phys, uint32_t& len, bool write);
    void setJitGuard(jit::CodeGuard* g) { jitGuard_ = g; }
    void jitMapChanged();

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
    // cpu_ and jitGuard_ (pointers the machine owns).
    // ── Debugger (DeviceSnapshot.h): this board's devices, read from their
    // members — VIA, SCC, floppy, SCSI, ADB and video, never a bus access.
    void debugDevices(std::vector<pom68k::dev::Snapshot>& out) const {
        using pom68k::dev::add;
        add(out, "VIA", "VIA", via_);
        add(out, "SCC", "Z8530", scc_);
        add(out, "Floppy", model_ == Model::SEFDHD || model_ == Model::Classic ? "SWIM" : "IWM", swim_);
        add(out, "SCSI", "5380", scsi_);
        if (isAdb()) {
            add(out, "ADB", "ADB transceiver", adbVia_);
            add(out, "ADB", "ADB bus", adb_);
        }
        {
            pom68k::dev::Snapshot v;
            v.kind = "Video";
            v.name = "1 bpp 512x342";
            const std::uint32_t base = screenBase();
            const bool main = (via_.portA() & 0x40) != 0;
            POM_DEVICE_FIELDS(v.fields, base, main);
            out.push_back(std::move(v));
        }
    }
    template <class Ar> void visit(Ar& ar) {
        ar.blob(ram_);
        // The second internal mechanism travels on every compact so the
        // layout does not depend on the profile (SaveState.h v27).
        ar(via_, adb_, adbVia_, rtc_, swim_, drive_, externalDrive_,
           secondInternalDrive_, scc_,
           scsi_, kbd_, mouse_, dayna_);
        for (ScsiDisk& disk : scsiDisks_) ar(disk);
        ar(kbdPhase_, kbdCmd_, kbdResp_, kbdTimer_, kbdInquiryHold_,
           viaPhase_, secAcc_, overlay_, pwmPhase_, pwmLine_);
        if constexpr (Ar::loading) {
            // RAM and the overlay state just changed wholesale — no write
            // can express that (JitGuard.h § invalidate).
            if (jitGuard_) jitGuard_->invalidate();
        }
    }

private:
    pom68k::lle::Registry* lle_ = &pom68k::lle::processRegistry();

    bool seViaTrace_ = false;
    bool secondInternalFitted_ = false;      // board configuration
    uint8_t viaAccess(uint32_t addr, bool write, uint8_t v);
    void refreshPortBInputs();

    std::vector<uint8_t> ram_, rom_;
    // The 512Ke decodes repeated Plus ROMs through $4FFFFF. Its ROM uses
    // that equality to detect the absent SCSI board (MAME mac512ke_map).
    uint32_t romWindowEnd() const { return model_ == Model::Mac512e ? 0x500000 : 0x400000 + romSize_; }
    Model model_ = Model::Plus;
    uint32_t romSize_ = kRomSize;
    uint32_t ramSize_ = kRamSize;
    // Scan-line phase of the sound/PWM word fetch (tick()), and what each
    // line of the current frame fetched for the speaker. The latches are
    // host-audio staging, not guest state: they stay out of the snapshot.
    int pwmPhase_ = 0, pwmLine_ = 0;
    std::array<SoundLine, kSoundLines> soundLine_{};
    Via6522 via_;
    AdbBus adb_;
    AdbVia adbVia_;
    Rtc rtc_;
    Swim1 swim_;
    SonyDrive drive_;                // internal mechanism
    SonyDrive externalDrive_;        // second / external mechanism
    SonyDrive secondInternalDrive_;  // SE PA4-high internal mechanism
    Scc8530 scc_;
    Ncr5380 scsi_;
    ScsiDisk scsiDisks_[7];
    CdAudioPump cdPump_;      // see CdAudioPump.h: 1 ms grain
    DaynaPort dayna_;              // opt-in Ethernet target (DaynaPortBus.h)
    MacKeyboard kbd_;
    MacMouse mouse_;
    // M0110 transaction pacing: two SR interrupts ~3 ms apart (Snow model)
    enum { KBD_IDLE, KBD_SHIFT_OUT, KBD_AWAIT_IN, KBD_SHIFT_IN } kbdPhase_ = KBD_IDLE;
    uint8_t kbdCmd_ = 0, kbdResp_ = 0;
    int kbdTimer_ = 0;
    Cpu68k* cpu_ = nullptr;
    jit::CodeGuard* jitGuard_ = nullptr;   // not serialized: machine wiring
    int viaPhase_ = 0;         // CPU-cycle remainder for the ÷10 VIA clock
    int64_t secAcc_ = 0;       // CPU-cycle accumulator for the RTC 1 Hz tick
    bool    kbdInquiryHold_ = false;   // Inquiry waiting out its ~1/4 s window
    // The real M0110 answers an Inquiry only on a key transition, or with Null
    // after roughly 250 ms; that hold is what paces the Mac's poll loop.
    static constexpr int kInquiryHoldCycles = 1958400;   // 250 ms @ 7.8336 MHz
    bool overlay_ = true;
};
