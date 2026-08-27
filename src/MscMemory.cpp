// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "MscMemory.h"
#include "MscCpu.h"
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iterator>

#ifdef _WIN32
#define timegm _mkgmtime
// localtime_s takes its two arguments in the opposite order to localtime_r.
static inline std::tm* localtime_r(const std::time_t* t, std::tm* out) {
    return localtime_s(out, t) == 0 ? out : nullptr;
}
#endif

// Host local wall time as Mac-epoch seconds (1904→1970 = 2 082 844 800 s) —
// the main.cpp hostMacSeconds() convention: re-read the LOCAL broken-down
// time as if it were UTC, which IS the wall clock the RTC keeps.
static uint32_t hostMacSecondsMsc() {
    std::time_t now = std::time(nullptr);
    std::tm lt{};
    localtime_r(&now, &lt);
    return uint32_t(uint64_t(int64_t(timegm(&lt))) + 2082844800ULL);
}

MscMemory::MscMemory(const pom68k::CoreConfig& coreConfig,
                     uint32_t totalRam, int64_t cpuHz, uint32_t machineId)
    : ram_(totalRam, 0), rom_(kRomSize, 0xFF), vram_(kVramSize, 0),
      pmu_(via_, cpuHz, coreConfig.peripherals),
      totalRam_(totalRam), cpuHz_(cpuHz), machineId_(machineId) {
    lle_ = coreConfig.firmware.registry;
    via_.configureTrace(coreConfig.peripherals.adbLleTrace);
    scc_.configureTrace(coreConfig.peripherals.sccTrace);
    for (ScsiDisk& disk : scsiDisks_) disk.configure(coreConfig.storage);
    asc_.onIrq = [this](bool s) { pvia_.ascIrq(s); updateIrq(); };
    via_.setMscShiftQuirk(true);     // mscvia: SR mode 000 = ext shift-in
    // msc_config rides the pseudo-VIA video hook (msc.cpp:63-64) — the
    // MSC has no monitor sense, the LCD is fixed; bit 1 of a config write
    // halves the CPU clock on real hardware (msc_config_w) — logged, not
    // modelled, until power management is real.
    pvia_.onVideoRead = [this] { return mscConfig_; };
    pvia_.onVideoWrite = [this](uint8_t v) {
        if ((v ^ mscConfig_) & 2)
            std::fprintf(stderr, "msc: config clock-divide bit -> %d "
                         "(not modelled)\n", (v >> 1) & 1);
        mscConfig_ = v;
    };
    // PMU handshake on pseudo-VIA2 port B: bit 1 = /PMU_ACK (in, from the
    // PG&E port H), bit 2 = /PMU_REQ (host out, PMU reads it on port F) —
    // msc.cpp via2_in_b / via2_out_b.
    pvia_.onPortBRead = [this] {
        return uint8_t((pmu_.pmuAck() ? 0x02 : 0x00) | (pmuReq_ ? 0x04 : 0x00));
    };
    pvia_.onPortB = [this](uint8_t v) {
        pmuReq_ = (v & 0x04) != 0;
        pmu_.setPmuReq(pmuReq_);
        // MAME stalls the 68030 80 µs on a REQ edge so the PMU can react
        // before the host samples ACK (msc.cpp via2_out_b). stall() feeds
        // peripheral time, so the PMU actually runs during the wait.
        if (cpu_) if (int s = pmu_.takeHostSpin()) cpu_->stall(s);
    };
    // MSC block ($20-$2F inside the pseudo-VIA window): reg 1 clock ctrl,
    // reg 2 sound ctrl with a read-clearing SOUND_BUSY bit 6 (msc.cpp:24
    // SOUND_BUSY=6, msc_pseudovia_r/w:225-262).
    pvia_.onMscRead = [this](int off) -> uint8_t {
        if (off == 1) return mscClockCtrl_;
        if (off == 2) {
            const uint8_t r = mscSoundCtrl_;
            mscSoundCtrl_ &= uint8_t(~0x40);
            return r;
        }
        return 0;
    };
    pvia_.onMscWrite = [this](int off, uint8_t v) {
        if (off == 1) mscClockCtrl_ = v;
        else if (off == 2) mscSoundCtrl_ = v;
    };
    pmu_.onWake = [this] {
        // pmu_portg_w wake: re-arm the overlay (msc pmu_reset_w) and reset
        // the 68030 at the next safe run boundary.
        overlay_ = true;
        jitMapChanged();
        wakeReset_ = true;
        sleeping_ = false;           // a full power_cycle_w sleep ends here
    };
    // pmu_porte_w:431-441 → msc.cpp pmu_reset_w:363-378: raising port E
    // bit 2 releases /RESET with the overlay re-armed — the same machine
    // action as wake, so a BORG-commanded reboot restarts from the ROM.
    // (At the power-on release the CPU has not run yet: the extra reset()
    // re-fetches the same vectors, a no-op.)
    pmu_.onCpuReset = pmu_.onWake;
    // PG&E boot mask ROM — user-provided, never committed (roms/README).
    for (const char* p : { "roms/pge/pge_boot.bin", "../roms/pge/pge_boot.bin" })
        if (pmu_.loadBootRom(p)) break;
    for (const char* p : { "roms/pge/duobatid.bin", "../roms/pge/duobatid.bin" })
        if (pmu_.loadBatteryId(p)) break;
    if (!pmu_.active())
        std::fprintf(stderr, "Msc: no roms/pge/pge_boot.bin — NO power "
                     "manager; the ROM will stall at the PMU handshake "
                     "(docs/DUO_BRINGUP.md milestone 2)\n");
    // The Duo's clock/PRAM live inside the PMU, and MAME seeds the PGE's
    // RTC from host time at device_start (m68hc05pge.cpp:185-187) — so the
    // seed belongs here, at machine construction, not in the GUI like the
    // platforms with a discrete Rtc. Unseeded, the guest clock sits at the
    // 1904 epoch. Survives reset(): M68hc05Pge::reset() keeps rtc_, as
    // MAME's device_reset keeps m_rtc.
    pmu_.setSeconds(hostMacSecondsMsc());
    reset();
}

// ── PRAM / NVRAM persistence ───────────────────────────────────────────
// On a Duo there is no discrete RTC chip and no Egret/Cuda: the clock and
// the parameter RAM live inside the PG&E power manager, so the battery
// file is the PG&E's non-volatile store. Layout, byte for byte MAME's
// (m68hc05pge.cpp:966-974 nvram_read/nvram_write):
//   $0000  $03C0  PGE internal RAM  (MCU $40-$3FF)
//   $03C0  $8000  PGE SRAM          (MCU $8000-$FFFF)
//   $83C0  4      RTC seconds, big-endian — POM68K's own tail, the same
//                 convention as the Egret battery file (Egret.cpp:38-45).
//                 Optional on read, so a MAME-written .nv loads here and a
//                 file written here loads in MAME (which stops at $83C0).
// The SRAM half is not dead weight: it holds the MAIN firmware the system
// ROM uploads over SPI *and* the PMU-side PRAM the protocol reads/writes.
//
// The $91 scrub is the whole reason a naive restore would wedge: MAME
// clears the power flag on every load (m68hc05pge.cpp:959, "clear power
// flag so the boot ROM does a cold boot"). Restored non-zero, the PG&E
// mask ROM resumes a warm/sleep path against an SRAM firmware image that
// the host has not re-uploaded yet. Copied exactly.
//
// RTC policy: a restored tail OVERRIDES the ctor's host-time seed — a
// restored clock is the point of the file, and the seed is what applies
// when there is no file (loadPram returns false without touching a byte).
// `runDuo` follows main.cpp's per-platform pattern — loadPram, then
// setRtcSeconds(hostMacSeconds()) — so the host wall clock lands back on
// top, exactly as on every other machine; that ordering is main.cpp's
// call, not this file's, and it is why the tail is harmless there.
//
// Gate: tests/msc_parity_test.cpp § F.
bool MscMemory::loadPram(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    constexpr size_t kRam = size_t(M68hc05Pge::kRamSize);
    constexpr size_t kSram = size_t(M68hc05Pge::kSramSize);
    if (b.size() < kRam + kSram) return false;       // CentrisMemory rule
    M68hc05Pge& mcu = pmu_.mcu();
    for (size_t i = 0; i < kRam; i++) mcu.setRamByte(int(i), b[i]);
    for (size_t i = 0; i < kSram; i++) mcu.setSramByte(int(i), b[kRam + i]);
    // Cold boot, always (m68hc05pge.cpp:959).
    mcu.setRamByte(M68hc05Pge::kPowerFlagAddr - 0x40, 0);
    if (b.size() >= kRam + kSram + 4) {
        const uint8_t* s = &b[kRam + kSram];
        mcu.setRtc(uint32_t(s[0]) << 24 | uint32_t(s[1]) << 16
                 | uint32_t(s[2]) << 8 | uint32_t(s[3]));
    }
    return true;
}

void MscMemory::savePram(const std::string& path) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return;
    M68hc05Pge& mcu = pmu_.mcu();
    std::vector<uint8_t> b;
    b.reserve(size_t(M68hc05Pge::kRamSize) + size_t(M68hc05Pge::kSramSize) + 4);
    for (int i = 0; i < M68hc05Pge::kRamSize; i++) b.push_back(mcu.ramByte(i));
    for (int i = 0; i < M68hc05Pge::kSramSize; i++) b.push_back(mcu.sramByte(i));
    const uint32_t s = mcu.rtc();                    // the PMU keeps the clock
    b.push_back(uint8_t(s >> 24)); b.push_back(uint8_t(s >> 16));
    b.push_back(uint8_t(s >> 8));  b.push_back(uint8_t(s));
    out.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
}

bool MscMemory::loadRom(const std::vector<uint8_t>& data) {
    if (data.size() != kRomSize) return false;
    rom_ = data;
    uint32_t stored = uint32_t(rom_[0]) << 24 | uint32_t(rom_[1]) << 16
                    | uint32_t(rom_[2]) << 8 | rom_[3];
    uint32_t sum = 0;
    for (size_t i = 4; i + 1 < rom_.size(); i += 2)
        sum += uint32_t(rom_[i] << 8 | rom_[i + 1]);
    if (sum != stored)
        std::fprintf(stderr, "MscMemory: ROM checksum $%08X != header $%08X\n",
                     sum, stored);
    return true;
}

void MscMemory::reset() {
    overlay_ = true;
    sccIrq_ = false;
    scc_.reset();
    scc_.setClocks(cpuHz_, 7833600);
    scc_.setAbortIdle(true);
    via_.reset();
    pvia_.reset();
    pmu_.reset();
    wakeReset_ = false;
    powerCycle_ = 0;
    sleeping_ = false;
    pmuReq_ = true;
    asc_.reset();
    scsi_.reset();
    mscConfig_ = mscClockCtrl_ = mscSoundCtrl_ = 0;
    for (auto& r : gscRegs_) r = 0;
    viaAcc_ = tickAcc_ = c15Acc_ = 0;
    framePos_ = 0;
    vblState_ = false;
    // msc.cpp via_in_a returns $07, via_in_b returns 0.
    via_.setInA(0x07);
    via_.setInB(0x00);
}

void MscMemory::busError() const {
    if (cpu_) cpu_->extBusError();
    throw moira::MmuBusError{};
}

int MscMemory::iplLevel() const {
    if (sccIrq_) return 4;                   // msc.cpp field_interrupts
    if (pvia_.irqAsserted()) return 2;
    if (via_.irqAsserted()) return 1;
    return 0;
}

void MscMemory::updateIrq() {
    if (cpu_) cpu_->updateIpl();
}

// msc.cpp via_sync — the Sonora/VASP formula, machine cycles.
void MscMemory::viaSync() {
    if (!cpu_) return;
    int64_t c = cpu_->machineClock();
    int64_t viaCycle = c * kViaHz / cpuHz_;
    int64_t target = (viaCycle * 2 + 3) * cpuHz_ / (2 * kViaHz) + 1;
    if (target > c) cpu_->stall(int(target - c));
}

uint8_t MscMemory::viaAccess8(uint32_t addr, bool write, uint8_t v) {
    if (cpu_) cpu_->flushTicks();
    viaSync();
    int reg = (addr >> 9) & 0x0F;            // $200 stride (msc via_r)
    if (write) {
        via_.write(reg, v);
        updateIrq();
        return 0;
    }
    uint8_t d = via_.read(reg);
    updateIrq();
    return d;
}

uint8_t MscMemory::scsiDma_() {
    if (!scsi_.drqActive()) busError();      // scsi_berr_w parity
    uint8_t d = scsi_.dmaRead();
    scsiDrq(scsi_.drqActive());
    return d;
}

void MscMemory::scsiDmaW_(uint8_t v) {
    if (!scsi_.drqActive()) busError();
    scsi_.dmaWrite(v);
    scsiDrq(scsi_.drqActive());
}

const uint8_t* MscMemory::codeSpan(uint32_t phys, uint32_t& len) const {
    len = 0;
    if (phys < 0x40000000) {
        if (overlay_) return nullptr;
        if (phys >= totalRam_) return nullptr;
        len = totalRam_ - phys;
        return ram_.data() + phys;
    }
    if (phys < 0x50000000) {
        if (overlay_) return nullptr;        // reading here would clear it
        const uint32_t o = phys & (kRomSize - 1);
        len = kRomSize - o;
        return rom_.data() + o;
    }
    return nullptr;
}

uint8_t* MscMemory::dataSpan(uint32_t phys, uint32_t& len, bool write) {
    len = 0;
    if (phys < 0x40000000) {
        if (overlay_) return nullptr;
        if (phys >= totalRam_) return nullptr;
        len = totalRam_ - phys;
        return ram_.data() + phys;
    }
    if (!write && phys < 0x50000000) {
        if (overlay_) return nullptr;
        const uint32_t o = phys & (kRomSize - 1);
        len = kRomSize - o;
        return rom_.data() + o;
    }
    return nullptr;
}

void MscMemory::jitMapChanged() {
    if (jitGuard_) jitGuard_->invalidate();
    if (cpu_) cpu_->pomJitDtlbFlush();
}

uint8_t MscMemory::read8(uint32_t addr) {
    if (addr < 0x40000000) {                 // RAM (ROM mirror under overlay)
        if (overlay_) return addr < kRomSize ? rom_[addr] : 0xFF;
        return addr < totalRam_ ? ram_[addr] : 0xFF;
    }
    if (addr < 0x50000000) {                 // ROM ×16 (msc rom_switch_r)
        if (overlay_) { overlay_ = false; jitMapChanged(); }
        return rom_[addr & (kRomSize - 1)];
    }
    if (addr >= 0x60000000) {
        // GSC VRAM, 128 KB mirrored across the $6xxxxxxx select
        // (gsc.cpp map: mirror $0FFE0000).
        if (addr < 0x70000000) return vram_[addr & (kVramSize - 1)];
        return 0x00;
    }
    if (addr >= 0x5FFFFFFC)                  // box ID
        return uint8_t(machineId_ >> ((3 - (addr & 3)) * 8));

    // ── I/O page $50Fxxxxx ──
    if ((addr & 0xFFF00000) != 0x50F00000) return 0x00;   // GSC-only space
    const uint32_t low = addr & 0xFFFFF;
    if (onIoAccess) onIoAccess(addr, false, 0);
    if (low < 0x2000)                        // VIA1
        return viaAccess8(low, false, 0);
    if (cpu_) cpu_->flushTicks();
    if (low >= 0x04000 && low < 0x06000) {   // SCC, dc_ab decode
        int ch = (low >> 1) & 1;
        uint8_t d = ((low >> 2) & 1) ? scc_.readData(ch) : scc_.readCtl(ch);
        sccIrqLine(scc_.irqAsserted());
        return d;
    }
    if ((low >= 0x06000 && low < 0x08000) ||
        (low >= 0x12000 && low < 0x14000)) return scsiDma_();
    if (low >= 0x10000 && low < 0x12000) {   // NCR 5380, $10 stride
        int reg = (low >> 4) & 7;
        if (reg == 6 && (low & 0xFFF) >= 0x260) return scsiDma_();
        uint8_t d = scsi_.read(reg);
        scsiDrq(scsi_.drqActive());
        return d;
    }
    if (low >= 0x14000 && low < 0x16000) return asc_.read(low - 0x14000);
    if (low >= 0x20000 && low < 0x22000) {   // GSC regs (gsc.cpp gsc_r)
        const uint32_t off = (low - 0x20000) & 0x1F;
        if (off == 1) return 6;              // panel ID 6 = DBLite 640×400
        return gscRegs_[off];
    }
    if (low >= 0x26000 && low < 0x28000) {
        // msc.cpp via2_r does 4× via_sync before the pseudo-VIA read.
        viaSync(); viaSync(); viaSync(); viaSync();
        uint8_t d = pvia_.read(low - 0x26000);
        updateIrq();
        return d;
    }
    return 0x00;                             // unmapped I/O = 0 (MAME parity)
}

void MscMemory::write8(uint32_t addr, uint8_t v) {
    if (addr < 0x40000000) {
        if (overlay_) return;
        if (jitGuard_) jitGuard_->note(addr, 1);
        if (addr < totalRam_) ram_[addr] = v;
        return;
    }
    if (addr < 0x50000000) return;           // ROM
    if (addr >= 0x60000000) {
        if (addr < 0x70000000) vram_[addr & (kVramSize - 1)] = v;
        return;
    }
    if ((addr & 0xFFF00000) != 0x50F00000) return;
    const uint32_t low = addr & 0xFFFFF;
    if (onIoAccess) onIoAccess(addr, true, v);
    if (low < 0x2000) { viaAccess8(low, true, v); return; }
    if (cpu_) cpu_->flushTicks();
    if (low >= 0x04000 && low < 0x06000) {
        int ch = (low >> 1) & 1;
        if ((low >> 2) & 1) scc_.writeData(ch, v);
        else scc_.writeCtl(ch, v);
        sccIrqLine(scc_.irqAsserted());
        return;
    }
    if ((low >= 0x06000 && low < 0x08000) ||
        (low >= 0x12000 && low < 0x14000)) { scsiDmaW_(v); return; }
    if (low >= 0x10000 && low < 0x12000) {
        int reg = (low >> 4) & 7;
        if (reg == 0 && (low & 0xFFF) >= 0x100) { scsiDmaW_(v); return; }
        scsi_.write(reg, v);
        scsiDrq(scsi_.drqActive());
        return;
    }
    if (low >= 0x14000 && low < 0x16000) {
        // Any ASC write marks sound busy for the sleep logic
        // (msc.cpp:130-134 install_write_tap sets bit 6, SOUND_BUSY=6).
        mscSoundCtrl_ |= 0x40;
        asc_.write(low - 0x14000, v);
        return;
    }
    if (low >= 0x20000 && low < 0x22000) {   // GSC regs (gsc.cpp gsc_w)
        gscRegs_[(low - 0x20000) & 0x1F] = v;
        return;
    }
    if (low >= 0x26000 && low < 0x28000) {
        viaSync(); viaSync(); viaSync(); viaSync();
        pvia_.write(low - 0x26000, v);
        updateIrq();
        return;
    }
    if (low >= 0xA0000 && low < 0xA0004) {
        // ── power_cycle_w (msc.cpp:191-206) ────────────────────────────
        // The 68030 asking to be powered down. MAME: 0 and $5A000000 mean
        // "reset me and resume" — PowerBook processor cycling, the idle
        // path — and any other value is a full system sleep that halts the
        // CPU until the PMU wakes it. The guest writes this and then spins
        // in `bra.b *` at $x88B14 with the interrupt mask at 7: the reset
        // IS the return path, there is no other way out.
        //
        // Until 2026-08-13 this was a milestone-1 fprintf, so that spin
        // never ended. The Duo froze 58 s into an idle Finder — reported
        // by duo_soak_etalon as a Mac clock stuck at 0 s over 180 s, and
        // first mis-read as "no one-second source is wired from the PG&E".
        // The one-second source was fine: nothing was running to receive
        // it. The screen keeps showing the desktop the machine painted
        // before it died, which is why the boot gate never noticed.
        //
        // The reset does NOT re-arm the ROM overlay — unlike the PMU's own
        // reset (pmu_reset_w), which does. MAME pulses the CPU's reset
        // line alone, so the vectors come from RAM at $0, where the System
        // parked its resume path.
        powerCycle_ = (powerCycle_ << 8) | v;
        if ((addr & 3) != 3) return;             // wait for the full long
        const uint32_t word = powerCycle_;
        powerCycle_ = 0;
        if (word == 0 || word == 0x5A000000) wakeReset_ = true;
        else sleeping_ = true;                   // until the PMU wakes us
        return;
    }
}

uint16_t MscMemory::read16(uint32_t addr) {
    if (addr < 0x40000000) [[likely]] {
        if (!overlay_) {
            if (addr + 1 < totalRam_)
                return uint16_t(ram_[addr] << 8 | ram_[addr + 1]);
            return 0xFFFF;
        }
        if (addr + 1 < kRomSize)
            return uint16_t(rom_[addr] << 8 | rom_[addr + 1]);
        return 0xFFFF;
    }
    if (addr < 0x50000000) {
        if (overlay_) { overlay_ = false; jitMapChanged(); }
        uint32_t o = addr & (kRomSize - 1);
        return uint16_t(rom_[o] << 8 | rom_[(o + 1) & (kRomSize - 1)]);
    }
    if (addr >= 0x60000000 && addr < 0x70000000 - 1) {
        uint32_t o = addr & (kVramSize - 1);
        return uint16_t(vram_[o] << 8 | vram_[(o + 1) & (kVramSize - 1)]);
    }
    if (addr >= 0x50000000 && addr < 0x60000000 &&
        (addr & 0xFFF00000) == 0x50F00000) {
        const uint32_t low = addr & 0xFFFFF;
        // VIA1 word read: data on D15-D8 (msc via_r returns read()<<8).
        if (low < 0x2000) {
            uint16_t d = viaAccess8(low, false, 0);
            return uint16_t(d << 8);
        }
        if (low >= 0x04000 && low < 0x06000) {   // scc_r: (r<<8)|r
            if (cpu_) cpu_->flushTicks();
            int ch = (low >> 1) & 1;
            uint8_t d = ((low >> 2) & 1) ? scc_.readData(ch) : scc_.readCtl(ch);
            sccIrqLine(scc_.irqAsserted());
            return uint16_t(d | (d << 8));
        }
        if (low >= 0x10000 && low < 0x12000) {   // scsi_r: byte on D15-D8
            return uint16_t(read8(addr) << 8);
        }
    }
    const uint16_t hi = read8(addr);
    return uint16_t(hi << 8) | read8(addr + 1);
}

void MscMemory::write16(uint32_t addr, uint16_t v) {
    if (addr < 0x40000000) [[likely]] {
        if (overlay_) return;
        if (jitGuard_) jitGuard_->note(addr, 2);
        if (addr + 1 < totalRam_) {
            ram_[addr] = uint8_t(v >> 8);
            ram_[addr + 1] = uint8_t(v);
        }
        return;
    }
    if (addr >= 0x60000000 && addr < 0x70000000 - 1) {
        uint32_t o = addr & (kVramSize - 1);
        vram_[o] = uint8_t(v >> 8);
        vram_[(o + 1) & (kVramSize - 1)] = uint8_t(v);
        return;
    }
    if (addr >= 0x50000000 && addr < 0x60000000 &&
        (addr & 0xFFF00000) == 0x50F00000) {
        const uint32_t low = addr & 0xFFFFF;
        if (low < 0x2000) {                  // msc via_w: data >>= 8
            viaAccess8(low, true, uint8_t(v >> 8));
            return;
        }
        if (low >= 0x04000 && low < 0x06000) {   // scc_w: data >> 8
            if (cpu_) cpu_->flushTicks();
            int ch = (low >> 1) & 1;
            uint8_t b = uint8_t(v >> 8);
            if ((low >> 2) & 1) scc_.writeData(ch, b);
            else                scc_.writeCtl(ch, b);
            sccIrqLine(scc_.irqAsserted());
            return;
        }
        if (low >= 0x10000 && low < 0x12000) {   // scsi_w: data >> 8
            write8(addr, uint8_t(v >> 8));
            return;
        }
    }
    write8(addr, uint8_t(v >> 8));
    write8(addr + 1, uint8_t(v));
}

uint8_t MscMemory::peek8(uint32_t addr) const {
    if (addr < 0x40000000) {
        if (overlay_) return addr < kRomSize ? rom_[addr] : 0xFF;
        return addr < totalRam_ ? ram_[addr] : 0xFF;
    }
    if (addr < 0x50000000) return rom_[addr & (kRomSize - 1)];
    if (addr >= 0x60000000 && addr < 0x70000000)
        return vram_[addr & (kVramSize - 1)];
    if (addr >= 0x5FFFFFFC && addr < 0x60000000)
        return uint8_t(machineId_ >> ((3 - (addr & 3)) * 8));
    return 0xFF;
}

int MscMemory::cyclesToNextEvent() const {
    int best = 0x7fffffff;
    auto tighten = [&best](int64_t cycles) {
        if (cycles < 1) cycles = 1;
        if (cycles < best) best = int(cycles);
    };
    // Bridge a device-domain bound (cycles on `deviceHz`, with `acc` already
    // banked in the fractional accumulator) back to machine cycles.
    auto bridge = [&](int deviceCycles, int64_t acc, int64_t deviceHz) {
        if (deviceCycles == 0x7fffffff) return;
        const int64_t need = int64_t(deviceCycles) * cpuHz_ - acc;
        tighten(need <= 0 ? 1 : (need + deviceHz - 1) / deviceHz);
    };

    // The PMU's own 68HC05 — the binding source on this board.
    tighten(pmu_.cyclesToNextEvent());
    // VIA1 timers and shift register, on the 783.36 kHz φ2.
    bridge(via_.cyclesToNextEvent(), viaAcc_, kViaHz);
    // Sound drain and the SCC's live countdowns, both C15M-paced for the
    // ASC and machine-paced for the SCC (see tick()).
    bridge(asc_.cyclesToNextEvent(), c15Acc_, kC15M);
    tighten(scc_.cyclesToNextEvent());
    // The board's two frame timers: 60.15 Hz into CA1, and the LCD VBL edge.
    tighten((cpuHz_ * 20 - tickAcc_ + 1202) / 1203);
    const int64_t frame = cpuHz_ / 60;
    const int64_t vblAt = frame * 400 / 445;
    tighten(framePos_ < vblAt ? vblAt - framePos_ : frame - framePos_);

    // NOT bounded: the SCSI controller and the drives — pure state between
    // accesses, and every access forces a flush. The wrapper's cap at the
    // historical batch holds them to their former cadence regardless.
    return std::max(best, 1);
}

void MscMemory::tick(int cpuCycles) {
    // VIA1 timers at 783.36 kHz (Bresenham on cpuHz_).
    viaAcc_ += int64_t(cpuCycles) * kViaHz;
    int viaCycles = int(viaAcc_ / cpuHz_);
    viaAcc_ -= int64_t(viaCycles) * cpuHz_;
    if (viaCycles && via_.tick(viaCycles)) updateIrq();

    // Free-running 60.15 Hz tick → CA1 (msc_6015_tick).
    tickAcc_ += int64_t(cpuCycles) * 1203;
    if (tickAcc_ >= cpuHz_ * 20) {
        tickAcc_ -= cpuHz_ * 20;
        via_.raiseCa1();
    }

    // 60 Hz LCD VBL → pseudo-VIA slot bit $40 (msc lcd_irq_w).
    framePos_ += cpuCycles;
    const int64_t frame = cpuHz_ / 60;
    framePos_ %= frame;
    bool vbl = framePos_ >= frame * 400 / 445;
    if (vbl != vblState_) {
        vblState_ = vbl;
        pvia_.slotIrq(PseudoVia::VBL, vbl);
    }

    // ASC + SCC run in the C15M domain.
    c15Acc_ += int64_t(cpuCycles) * kC15M;
    int c15 = int(c15Acc_ / cpuHz_);
    c15Acc_ -= int64_t(c15) * cpuHz_;
    if (c15) asc_.tick(c15);
    scc_.tick(cpuCycles);
    sccIrq_ = scc_.irqAsserted();

    pmu_.tick(cpuCycles);            // PG&E (debt-carried MCU clock)
    updateIrq();
}
