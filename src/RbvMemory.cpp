// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "RbvMemory.h"
#include "FirmwareChoice.h"
#include "RbvCpu.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

RbvMemory::RbvMemory(const pom68k::CoreConfig& coreConfig,
                     uint32_t totalRam, int64_t cpuHz, bool iici)
    : ram_(totalRam, 0), rom_(kRomSize, 0xFF),
      egret_(via_, false, int(cpuHz)),
      egretLle_(via_, cpuHz, CudaLle::Flavor::Egret),
      totalRam_(totalRam), cpuHz_(cpuHz), iici_(iici) {
    via_.configureTrace(coreConfig.peripherals.adbLleTrace);
    egret_.configure(coreConfig.peripherals.appleTalkPram,
                     coreConfig.peripherals.egretCommandTrace);
    egretLle_.configure(coreConfig.peripherals);
    adbVia_.configure(coreConfig.firmware, coreConfig.peripherals);
    rtc_.configure(coreConfig.peripherals.appleTalkPram,
                   coreConfig.peripherals.rtcTrace);
    drive_.configureFluxJitter(coreConfig.storage.fluxJitterPercent);
    scc_.configureTrace(coreConfig.peripherals.sccTrace);
    for (ScsiDisk& disk : scsiDisks_) disk.configure(coreConfig.storage);
    egret_.setAdbBus(&adb_);
    // RBV pseudo-VIA video hooks (rbv.cpp:181-189): the config read
    // returns the monitor type on bits 3-5, the write latches
    // depth (bits 0-2) + video-off (bit 6).
    pvia_.onVideoRead = [this] { return uint8_t((montype_ << 3) & 0x38); };
    pvia_.onVideoWrite = [this](uint8_t v) { videoConfig_ = v; };
    // Discrete ASC IRQ → pseudo-VIA IFR bit 4 (maciici.cpp:562 irqf →
    // rbv asc_irq_w → pseudovia).
    asc_.onIrq = [this](bool s) { pvia_.ascIrq(s); updateIrq(); };
    if (iici_) {
        // IIci: PIC1654S ADB modem on VIA1 CB1/CB2 (firmware LLE loaded from
        // roms/adbmodem/342s0440-b.bin inside AdbVia::reset) + discrete RTC.
        adbVia_.attach(via_, adb_, cpuHz_);
        rtc_.factoryDefaults();
        rtc_.setXpram(0x8A, uint8_t(rtc_.xpram(0x8A) | 0x05));   // 32-bit clean
    } else {
        // IIsi: Egret firmware LLE — the 344S0100 (maciici.cpp:666
        // set_default_bios_tag), the LC III's 341S0851/0850 as fallbacks.
        pom68k::fw::Request req{pom68k::lle::HleEgretCuda,
                                pom68k::FirmwareTarget::Egret};
        req.name = "Egret — MCU ADB / PRAM / horloge";
        req.enableKnob = "POM68K_EGRET_LLE";
        req.pathKnob = "POM68K_CUDA_FW";
        req.logTag = "Rbv";
        req.enabled = coreConfig.firmware.egretLle;
        req.forcedPath = coreConfig.firmware.egretPath.value_or(std::string());
        req.candidates = {
            "roms/egret/344s0100.bin", "../roms/egret/344s0100.bin",
            "roms/egret/341s0851.bin", "../roms/egret/341s0851.bin",
            "roms/egret/341s0850.bin", "../roms/egret/341s0850.bin" };
        egretLleOn_ = pom68k::fw::select(req, [this](const std::vector<uint8_t>& fw) {
            return egretLle_.loadFirmware(fw);
        });
    }
    // Firmware RESET_SYSTEM ($11) — the Finder's "Restart". DEFERRED: this
    // fires from inside viaWrite(), under the CPU, and reset() would reset
    // the very MCU that is mid-instruction issuing it. Latch here, act at a
    // run boundary in the CPU wrapper. Only the address map comes back; the
    // devices, the PRAM and the MCU keep running, which is what the /RESET
    // line does on the board (the gate array and the CPU sit on it; the
    // Egret/Cuda is the one pulling it).
    egretLle_.onCpuReset = [this] {
        overlay_ = true;
        jitMapChanged();
        restartPending_ = true;
    };
    reset();
}

// IIci: PRAM is the discrete RTC's 256-byte XPRAM; IIsi: the Egret's.
bool RbvMemory::loadPram(const std::string& path) {
    if (iici_) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        if (b.size() < 256) return false;
        for (int i = 0; i < 256; i++) rtc_.setXpram(uint8_t(i), b[i]);
        return true;
    }
    bool ok = egret_.loadPram(path);
    if (egretLleOn_)
        for (int i = 0; i < 256; i++) egretLle_.setPram(i, egret_.pram(i));
    return ok;
}

void RbvMemory::savePram(const std::string& path) {
    if (iici_) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) return;
        for (int i = 0; i < 256; i++) {
            uint8_t v = rtc_.xpram(uint8_t(i));
            out.write(reinterpret_cast<const char*>(&v), 1);
        }
        return;
    }
    if (egretLleOn_)
        for (int i = 0; i < 256; i++) egret_.setPram(i, egretLle_.pram(i));
    egret_.savePram(path);
}

bool RbvMemory::loadRom(const std::vector<uint8_t>& data) {
    if (data.size() != kRomSize) return false;
    rom_ = data;
    uint32_t stored = uint32_t(rom_[0]) << 24 | uint32_t(rom_[1]) << 16
                    | uint32_t(rom_[2]) << 8 | rom_[3];
    uint32_t sum = 0;
    for (size_t i = 4; i + 1 < rom_.size(); i += 2)
        sum += uint32_t(rom_[i] << 8 | rom_[i + 1]);
    if (sum != stored)
        std::fprintf(stderr, "RbvMemory: ROM checksum $%08X != header $%08X\n",
                     sum, stored);
    return true;
}

// VBL geometry per monitor (rbv.cpp device_reset): 640×480 = 864×525 @
// 30.24 MHz, 512×384 = 640×407 @ C15M, 640×870 portrait = 832×918 @
// 57.2832 MHz. VBL = the lines past the active area.
void RbvMemory::recalcFrame() {
    int htot, vtot, vres;
    int64_t dot;
    switch (montype_ & 7) {
    case 1:  htot = 832; vtot = 918; vres = 870; dot = 57283200; break;
    case 2:  htot = 640; vtot = 407; vres = 384; dot = 15667200; break;
    case 6:
    default: htot = 864; vtot = 525; vres = 480; dot = 30240000; break;
    }
    frameCycles_ = int64_t(htot) * vtot * cpuHz_ / dot;
    vblStart_ = int64_t(htot) * vres * cpuHz_ / dot;
    frameTotalLines_ = int(vtot);
    framePos_ = 0;
    if (vblState_) {
        vblState_ = false;
        pvia_.slotIrq(PseudoVia::VBL, false);
        updateIrq();
    }
}

void RbvMemory::reset() {
    overlay_ = true;
    restartPending_ = false;   // a cold reset supersedes a warm one
    sccIrq_ = false;
    scc_.reset();
    scc_.setClocks(cpuHz_, 7833600);         // SCC85C30 @ C7M (maciici.cpp:544)
    scc_.setAbortIdle(true);       // no hardwired LocalTalk peer — the
                                   // standing abort is a line state, not
                                   // machine config (Scc8530::openLine,
                                   // LLE steps 7+8: virgin line = clean)
    via_.reset();
    pvia_.reset();
    if (iici_) {
        rtc_.reset();
        adbVia_.reset();
    } else {
        egret_.reset();
        egret_.factoryDefaults();
        egretLle_.reset();
        if (egretLleOn_)
            for (int i = 0; i < 256; i++)
                egretLle_.setPram(i, egret_.pram(i));
    }
    adb_.reset();
    dac_.reset();
    asc_.reset();
    scsi_.reset();
    swim_.reset();
    swim_.attachDrive(&drive_, nullptr);
    drive_.setSpinClockHz(15667200);         // drive_.tick unit = C15M cycles
    videoConfig_ = 0;
    viaAcc_ = tickAcc_ = c15Acc_ = secAcc_ = 0;
    vblState_ = false;
    recalcFrame();
    if (iici_) {
        // IIci VIA1 PA = $C6 | BIT(config,1) (maciici.cpp via_in_a). The
        // config port defaults to "diagnostic disabled" (bit 1 = 1), so PA0
        // reads 1 → $C7. Feeding a bare $C6 (PA0=0) makes the ROM take the
        // diagnostic burn-in path and spin in the VIA-T2 calibration loop
        // forever. PB from the RTC data + /ADB IRQ, refreshed live.
        via_.setInA(0xC7);
        refreshVia1PortB();
    } else {
        // IIsi VIA1 port A reads $97 (maciici.cpp via_in_a_iisi: $96 | PA0 =
        // diagnostic-mode disabled). Port B: PB3 = Egret XCVR_SESSION (live);
        // PB4/PB5 (VIA_FULL/SYS_SESSION) are host-driven and idle LOW (the V8
        // phantom-edge lesson); the other input bits read pulled-up ($C7 —
        // the V8Memory/SonoraMemory composition). MAME's via_in_b_iisi
        // returns a bare session bit, but our Egret HLE/LLE handshake is
        // edge-triggered on PB and needs the same undriven-high bits every
        // other V8 machine feeds it, or the transport wedges after byte 1.
        via_.setInA(0x97);
        via_.setInB(uint8_t(0xC7 | (xcvrSession_() << 3)));
    }
}

void RbvMemory::busError() const {
    if (cpu_) cpu_->extBusError();
    throw moira::MmuBusError{};
}

int RbvMemory::iplLevel() const {
    if (sccIrq_) return 4;                   // maciici.cpp field_interrupts
    if (pvia_.irqAsserted()) return 2;
    if (via_.irqAsserted()) return 1;
    return 0;
}

void RbvMemory::updateIrq() {
    if (cpu_) cpu_->updateIpl();
}

// maciici.cpp via_sync — stall the CPU to the 783.36 kHz VIA E-clock on
// every VIA1 access (the Sonora formula).
// Machine cycles, not core-clock cycles (SonoraMemory::viaSync).
void RbvMemory::viaSync() {
    if (!cpu_) return;
    int64_t c = cpu_->machineClock();
    int64_t viaCycle = c * kViaHz / cpuHz_;
    int64_t target = (viaCycle * 2 + 3) * cpuHz_ / (2 * kViaHz) + 1;
    if (target > c) cpu_->stall(int(target - c));
}

// IIci VIA1 port B input: PB0 = RTC serial data, PB3 = /ADB IRQ (active
// low), the rest pulled up (maciici.cpp via_in_b).
void RbvMemory::refreshVia1PortB() {
    uint8_t in = uint8_t(rtc_.dataBit() & 1);
    in |= 0xC6;
    if (!adbVia_.irqPending()) in |= 0x08;
    via_.setInB(in);
}

uint8_t RbvMemory::viaAccess8(uint32_t addr, bool write, uint8_t v) {
    if (cpu_) cpu_->flushTicks();
    viaSync();
    if (iici_ && cpu_) adbVia_.syncTo(cpu_->machineClock());
    int reg = (addr >> 9) & 0x0F;            // $200 stride
    if (write) {
        via_.write(reg, v);
        if (iici_) {
            // ADB modem clock/data on CB1/CB2 + PB4/PB5; RTC on PB0-2/CA2
            // (maciici.cpp via_out_b: adbmodem set_via_state + rtc lines).
            if (reg == Via6522::ORB || reg == Via6522::DDRB
                || reg == Via6522::SR || reg == Via6522::ACR) {
                adbVia_.sync();
                rtc_.setLines(!(via_.portB() & 0x04),        // PB2 = /CE
                              (via_.portB() & 0x02) != 0,     // PB1 = clock
                              (via_.portB() & 0x01) != 0);    // PB0 = data
                refreshVia1PortB();
            }
        } else if (reg == Via6522::ORB || reg == Via6522::DDRB) {
            if (egretLleOn_) egretLle_.portBChanged(via_.portB());
            else             egret_.portBChanged(via_.portB());
        }
        updateIrq();
        return 0;
    }
    if (reg == Via6522::ORB) {
        if (iici_) refreshVia1PortB();
        else       via_.setInB(uint8_t(0xC7 | (xcvrSession_() << 3)));
    }
    uint8_t d = via_.read(reg);
    updateIrq();
    return d;
}

uint8_t RbvMemory::scsiDma_() {
    if (!scsi_.drqActive()) busError();      // macscsi.cpp timeout → /BERR
    uint8_t d = scsi_.dmaRead();
    scsiDrq(scsi_.drqActive());
    return d;
}

void RbvMemory::scsiDmaW_(uint8_t v) {
    if (!scsi_.drqActive()) busError();
    scsi_.dmaWrite(v);
    scsiDrq(scsi_.drqActive());
}

const uint8_t* RbvMemory::codeSpan(uint32_t phys, uint32_t& len) const {
    len = 0;
    if (phys < 0x40000000) {
        if (overlay_) return nullptr;        // ROM mirror until first ROM read
        if (phys >= totalRam_) return nullptr;   // open bus, not memory
        len = totalRam_ - phys;
        return ram_.data() + phys;
    }
    if (phys < 0x50000000) {
        if (overlay_) return nullptr;        // reading here would clear it
        const uint32_t o = phys & (kRomSize - 1);
        len = kRomSize - o;                  // stop at the mirror seam
        return rom_.data() + o;
    }
    return nullptr;                          // I/O, VRAM, machine ID
}

uint8_t* RbvMemory::dataSpan(uint32_t phys, uint32_t& len, bool write) {
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

void RbvMemory::jitMapChanged() {
    if (jitGuard_) jitGuard_->invalidate();
    if (cpu_) cpu_->pomJitDtlbFlush();
}

uint8_t RbvMemory::read8(uint32_t addr) {
    if (addr < 0x40000000) {                 // RAM (ROM mirror under overlay)
        if (overlay_) return addr < kRomSize ? rom_[addr] : 0xFF;
        return addr < totalRam_ ? ram_[addr] : 0xFF;
    }
    if (addr < 0x50000000) {                 // ROM ×32 (rom_switch_r)
        if (overlay_) { overlay_ = false; jitMapChanged(); }      // any read clears the overlay
        return rom_[addr & (kRomSize - 1)];
    }
    if (addr >= 0x51000000) return 0x00;     // MAME-unmapped

    // ── RBV I/O page $50xxxxxx (windows mirror $00F00000) ──
    const uint32_t sub = addr & 0xFFFFFF;
    if ((sub & 0x3FFFF) < 0x2000)            // VIA1 at +$00000 and +$40000
        return viaAccess8(sub, false, 0);
    if (cpu_) cpu_->flushTicks();
    const uint32_t low = sub & 0xFFFFF;
    if (low >= 0x04000 && low < 0x06000) {   // SCC, dc_ab decode
        int ch = (low >> 1) & 1;
        uint8_t d = ((low >> 2) & 1) ? scc_.readData(ch) : scc_.readCtl(ch);
        sccIrqLine(scc_.irqAsserted());
        return d;
    }
    if ((low >= 0x06000 && low < 0x08000) ||
        (low >= 0x12000 && low < 0x14000)) return scsiDma_();
    if (low >= 0x10000 && low < 0x12000) {   // 53C80, stride $10
        int reg = (low >> 4) & 7;
        if (reg == 6 && (low & 0xFFF) == 0x260) return scsiDma_();
        uint8_t d = scsi_.read(reg);
        scsiDrq(scsi_.drqActive());
        return d;
    }
    if (low >= 0x14000 && low < 0x16000) return asc_.read(low - 0x14000);
    if (low >= 0x16000 && low < 0x18000) {   // SWIM1, +5 wait states
        if (cpu_) cpu_->stall(5);
        swim_.setSel((via_.portA() & 0x20) != 0);
        return swim_.read((low >> 9) & 0xF);
    }
    if (low >= 0x24000 && low < 0x24010) {
        // Bt478 DAC: MSB byte lane of each longword (rbv.cpp map
        // umask32 ff000000) — regs at +0/+4/+8/+$C only.
        if ((low & 3) == 0) return dac_.read((low >> 2) & 3);
        return 0x00;
    }
    if (low >= 0x26000 && low < 0x28000) {
        uint8_t d = pvia_.read(low - 0x26000);
        updateIrq();
        return d;
    }
    return 0x00;                             // MAME-unmapped reads 0
}

void RbvMemory::write8(uint32_t addr, uint8_t v) {
    if (addr < 0x40000000) {
        if (overlay_) return;
        if (jitGuard_) jitGuard_->note(addr, 1);
        if (addr < totalRam_) ram_[addr] = v;
        return;
    }
    if (addr < 0x50000000) return;           // ROM
    if (addr >= 0x51000000) return;

    const uint32_t sub = addr & 0xFFFFFF;
    if ((sub & 0x3FFFF) < 0x2000) { viaAccess8(sub, true, v); return; }
    if (cpu_) cpu_->flushTicks();
    const uint32_t low = sub & 0xFFFFF;
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
        if (reg == 0 && (low & 0xFFF) == 0x200) { scsiDmaW_(v); return; }
        scsi_.write(reg, v);
        scsiDrq(scsi_.drqActive());
        return;
    }
    if (low >= 0x14000 && low < 0x16000) { asc_.write(low - 0x14000, v); return; }
    if (low >= 0x16000 && low < 0x18000) {
        if (cpu_) cpu_->stall(5);
        swim_.setSel((via_.portA() & 0x20) != 0);
        swim_.write((low >> 9) & 0xF, v);
        return;
    }
    if (low >= 0x24000 && low < 0x24010) {
        if ((low & 3) == 0) dac_.write((low >> 2) & 3, v);
        return;
    }
    if (low >= 0x26000 && low < 0x28000) {
        pvia_.write(low - 0x26000, v);
        updateIrq();
        return;
    }
}

uint16_t RbvMemory::read16(uint32_t addr) {
    // Word fast paths for the flat regions (the V8 read16 lesson: device
    // space goes byte-by-byte, sequenced).
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
        if (overlay_) overlay_ = false;
        uint32_t o = addr & (kRomSize - 1);
        return uint16_t(rom_[o] << 8 | rom_[(o + 1) & (kRomSize - 1)]);
    }
    // VIA1 word reads mirror the byte on both lanes (maciici.cpp via_r).
    if (addr < 0x51000000 && ((addr & 0xFFFFFF) & 0x3E000) == 0) {
        uint16_t d = viaAccess8(addr & 0xFFFFFF, false, 0);
        return uint16_t(d | (d << 8));
    }
    // SCC word fast path: one side-effect, byte mirrored (scc_r).
    if (addr < 0x51000000) {
        const uint32_t low = addr & 0xFFFFF;
        if (low >= 0x04000 && low < 0x06000) {
            if (cpu_) cpu_->flushTicks();
            int ch = (low >> 1) & 1;
            uint8_t d = ((low >> 2) & 1) ? scc_.readData(ch) : scc_.readCtl(ch);
            sccIrqLine(scc_.irqAsserted());
            return uint16_t(d | (d << 8));
        }
    }
    const uint16_t hi = read8(addr);         // sequenced (SCSI pseudo-DMA)
    return uint16_t(hi << 8) | read8(addr + 1);
}

void RbvMemory::write16(uint32_t addr, uint16_t v) {
    if (addr < 0x40000000) [[likely]] {
        if (overlay_) return;
        if (jitGuard_) jitGuard_->note(addr, 2);
        if (addr + 1 < totalRam_) {
            ram_[addr] = uint8_t(v >> 8);
            ram_[addr + 1] = uint8_t(v);
        }
        return;
    }
    if (addr >= 0x50000000 && addr < 0x51000000
        && ((addr & 0xFFFFFF) & 0x3E000) == 0) {
        // VIA1 word writes: low lane first (maciici.cpp via_w)
        viaAccess8(addr & 0xFFFFFF, true, uint8_t(v));
        viaAccess8(addr & 0xFFFFFF, true, uint8_t(v >> 8));
        return;
    }
    if (addr >= 0x50000000 && addr < 0x51000000) {
        const uint32_t low = addr & 0xFFFFF;
        if (low >= 0x04000 && low < 0x06000) {
            if (cpu_) cpu_->flushTicks();
            int ch = (low >> 1) & 1;
            uint8_t b = uint8_t(v >> 8);
            if ((low >> 2) & 1) scc_.writeData(ch, b);
            else                scc_.writeCtl(ch, b);
            sccIrqLine(scc_.irqAsserted());
            return;
        }
    }
    write8(addr, uint8_t(v >> 8));
    write8(addr + 1, uint8_t(v));
}

uint8_t RbvMemory::peek8(uint32_t addr) const {
    if (addr < 0x40000000) {
        if (overlay_) return addr < kRomSize ? rom_[addr] : 0xFF;
        return addr < totalRam_ ? ram_[addr] : 0xFF;
    }
    if (addr < 0x50000000) return rom_[addr & (kRomSize - 1)];
    return 0x00;
}

void RbvMemory::tick(int cpuCycles) {
    // VIA1 timers at 783.36 kHz (Bresenham on cpuHz_).
    viaAcc_ += int64_t(cpuCycles) * kViaHz;
    int viaCycles = int(viaAcc_ / cpuHz_);
    viaAcc_ -= int64_t(viaCycles) * cpuHz_;
    if (viaCycles && via_.tick(viaCycles)) updateIrq();

    // Free-running 60.15 Hz tick timer → CA1 (rbv.cpp mac_6015_tick).
    tickAcc_ += int64_t(cpuCycles) * 1203;
    if (tickAcc_ >= cpuHz_ * 20) {
        tickAcc_ -= cpuHz_ * 20;
        via_.raiseCa1();
    }

    // Monitor-driven VBL → pseudo-VIA slot bit $40 (rbv.cpp screen_vblank).
    if (frameCycles_) {
        framePos_ += cpuCycles;
        // Completed frames, for the raster beam (VideoBeam::setPos).
        if (framePos_ >= frameCycles_) {
            frameCount_ += uint64_t(framePos_ / frameCycles_);
            framePos_ %= frameCycles_;
        }
        bool vbl = framePos_ >= vblStart_;
        if (vbl != vblState_) {
            vblState_ = vbl;
            pvia_.slotIrq(PseudoVia::VBL, vbl);
        }
    }

    if (iici_) {
        adbVia_.tick(cpuCycles);
        if (cpu_) adbVia_.syncTo(cpu_->machineClock());
        refreshVia1PortB();                  // /ADB IRQ level → PB3
        updateIrq();
        // Discrete RTC 1 Hz: the 343-0042 advances its counter and its CKO
        // output lands on VIA1 CA2.  Advancing only the private counter is
        // insufficient: the System updates low-memory Time from this edge.
        secAcc_ += cpuCycles;
        if (secAcc_ >= cpuHz_) {
            secAcc_ -= cpuHz_;
            rtc_.tickSecond();
            via_.raiseCa2();
            updateIrq();
        }
    } else if (egretLleOn_) {
        egretLle_.tick(cpuCycles);
    } else {
        egret_.tick(cpuCycles);
    }

    // ASC drain + SWIM1 cells + floppy spin run in the C15M domain;
    // convert 20/25 MHz RBV cycles.
    c15Acc_ += int64_t(cpuCycles) * 15667200;
    int c15 = int(c15Acc_ / cpuHz_);
    c15Acc_ -= int64_t(c15) * cpuHz_;
    if (c15) {
        asc_.tick(c15);
        swim_.tick(c15);
        drive_.tick(c15);
    }
    scc_.tick(cpuCycles);
    sccIrq_ = scc_.irqAsserted();
    updateIrq();
}
