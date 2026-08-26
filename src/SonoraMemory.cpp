// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "SonoraMemory.h"
#include "FirmwareChoice.h"
#include "SonoraCpu.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

// MAME mv_sonora.cpp:20-26 — the five Sonora modelines. The LC III
// drives 512×384 (sense 2) and 640×480 13" (sense 6) here; the other
// three are kept for ROM probing completeness.
// The trailing pair is MAME's own {supports16bpp, monochrome}: only the
// 640×870 15" Portrait (sense $01) is a mono display.
static const SonoraMemory::Modeline kModelines[] = {
    { 0x02, 15667200,  640, 16, 32,  80,  407,  1, 3, 19, true,  false },
    { 0x06, 31334400,  896, 80, 64, 112,  525,  3, 3, 39, true,  false },
    { 0x01, 57283200,  832, 32, 80,  80,  918,  3, 3, 42, false, true  },
    { 0x09, 57283200, 1152, 32, 64, 224,  667,  1, 3, 39, false, false },
    { 0x0B, 25175000,  800, 16, 96,  48,  525, 10, 2, 33, false, false },
};

const SonoraMemory::Modeline* SonoraMemory::modeline(uint8_t id) {
    for (const Modeline& m : kModelines)
        if (m.id == (id & 0x1F)) return &m;
    return nullptr;
}

SonoraMemory::SonoraMemory(const pom68k::CoreConfig& coreConfig,
                           uint32_t totalRam, int64_t cpuHz,
                           uint32_t machineId, bool cudaAdb)
    : ram_(totalRam, 0), rom_(kRomSize, 0xFF), vram_(kVramSize, 0),
      egret_(via_, cudaAdb, int(cpuHz)),
      egretLle_(via_, cpuHz, cudaAdb ? CudaLle::Flavor::Cuda
                                     : CudaLle::Flavor::Egret),
      asc_(cpuHz),
      totalRam_(totalRam), cpuHz_(cpuHz), machineId_(machineId) {
    via_.configureTrace(coreConfig.peripherals.adbLleTrace);
    egret_.configure(coreConfig.peripherals.appleTalkPram,
                     coreConfig.peripherals.egretCommandTrace);
    egretLle_.configure(coreConfig.peripherals);
    drive_.configureFluxJitter(coreConfig.storage.fluxJitterPercent);
    scc_.configureTrace(coreConfig.peripherals.sccTrace);
    for (ScsiDisk& disk : scsiDisks_) disk.configure(coreConfig.storage);
    egret_.setAdbBus(&adb_);
    // The Cuda AIOs (LC 520/550/CC II) carry a DFAC2 on the Cuda's I2C
    // (maclc3.cpp:403) — the slave ACK lives in CudaLle (setI2cDfac).
    if (cudaAdb) egretLle_.setI2cDfac(true);
    // Sonora EASC IRQ is level-triggered into pseudo-VIA IFR bit 4
    // (sonora.cpp:86 irqf → pseudovia asc_irq_w).
    asc_.onIrq = [this](bool s) { pvia_.ascIrq(s); updateIrq(); };
    // ADB MCU firmware LLE — LC III: Egret 341S0851 (maclc3.cpp:341
    // set_default_bios_tag), the LC II's 341S0850 as fallback. LC 520/550
    // AIO family: Cuda 341S0060 (Cuda 2.40, maclc3.cpp:380), the Q605's
    // proven 341S0788 as fallback. Same POM68K_EGRET_LLE rollout as V8Memory.
    {
        pom68k::fw::Request req{
            pom68k::lle::HleEgretCuda,
            cudaAdb ? pom68k::FirmwareTarget::Cuda
                    : pom68k::FirmwareTarget::Egret};
        req.name = cudaAdb ? "Cuda — MCU ADB / PRAM / horloge"
                           : "Egret — MCU ADB / PRAM / horloge";
        req.enableKnob = cudaAdb ? "POM68K_CUDA_LLE"
                                 : "POM68K_EGRET_LLE";
        req.pathKnob = "POM68K_CUDA_FW";
        req.logTag = "Sonora";
        req.enabled = cudaAdb ? coreConfig.firmware.cudaLle
                              : coreConfig.firmware.egretLle;
        req.forcedPath = (cudaAdb ? coreConfig.firmware.cudaPath
                                  : coreConfig.firmware.egretPath)
                             .value_or(std::string());
        req.candidates = cudaAdb
            ? std::vector<std::string>{
                  "roms/cuda/341s0060.bin", "../roms/cuda/341s0060.bin",
                  "roms/cuda/341s0788.bin", "../roms/cuda/341s0788.bin" }
            : std::vector<std::string>{
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

bool SonoraMemory::loadRom(const std::vector<uint8_t>& data) {
    if (data.size() != kRomSize) return false;
    rom_ = data;
    uint32_t stored = uint32_t(rom_[0]) << 24 | uint32_t(rom_[1]) << 16
                    | uint32_t(rom_[2]) << 8 | rom_[3];
    uint32_t sum = 0;
    for (size_t i = 4; i + 1 < rom_.size(); i += 2)
        sum += uint32_t(rom_[i] << 8 | rom_[i + 1]);
    if (sum != stored)
        std::fprintf(stderr, "SonoraMemory: ROM checksum $%08X != header $%08X\n",
                     sum, stored);
    return true;
}

void SonoraMemory::reset() {
    overlay_ = true;
    restartPending_ = false;   // a cold reset supersedes a warm one
    sccIrq_ = false;
    scc_.reset();
    scc_.setClocks(cpuHz_, 7833600);         // SCC85C30 @ C7M (maclc3.cpp:297)
    scc_.setAbortIdle(true);       // no hardwired LocalTalk peer — the
                                   // standing abort is a line state, not
                                   // machine config (Scc8530::openLine,
                                   // LLE steps 7+8: virgin line = clean)
    via_.reset();
    pvia_.reset();
    egret_.reset();
    egret_.factoryDefaults();
    egretLle_.reset();
    if (egretLleOn_)
        for (int i = 0; i < 256; i++)
            egretLle_.setPram(i, egret_.pram(i));
    adb_.reset();
    asc_.reset();
    scsi_.reset();
    swim_.reset();
    swim_.attachDrive(&drive_, nullptr);
    drive_.setSpinClockHz(cpuHz_);           // drive_.tick unit = CPU cycles
    // mv_sonora device_reset: blanked, no modeline, sense drive released.
    for (uint32_t& p : pens_) p = 0;
    palAddr_ = palIdx_ = palControl_ = palColkey_ = 0;
    vidMode_ = 0x9F;
    vidDepth_ = 0;
    vidMonId_ = 8;
    vidVtest_ = 0;
    mode_ = nullptr;
    viaAcc_ = tickAcc_ = swimAcc_ = 0;
    framePos_ = frameCycles_ = vblStart_ = 0;
    vblState_ = false;
    // VIA1 port A reads $80 (sonora.cpp:212-215 — no V8-style machine ID;
    // the model comes from the $5FFFFFFC longword). Port B: PB3 = MCU
    // XCVR_SESSION/TREQ (sonora.cpp:217-220), PB4/PB5 host outputs idle
    // LOW (the V8 phantom-edge lesson), the other input bits pulled HIGH
    // ($C7 — the V8Memory/Q605Memory composition).
    via_.setInA(0x80);
    via_.setInB(uint8_t(0xC7 | (xcvrSession_() << 3)));
}

void SonoraMemory::busError() const {
    if (cpu_) cpu_->extBusError();
    throw moira::MmuBusError{};
}

int SonoraMemory::iplLevel() const {
    if (sccIrq_) return 4;                   // sonora.cpp:258-286
    if (pvia_.irqAsserted()) return 2;
    if (via_.irqAsserted()) return 1;
    return 0;
}

void SonoraMemory::updateIrq() {
    if (cpu_) cpu_->updateIpl();
}

// sonora.cpp:347-368 — stall the CPU to the 783.36 kHz VIA E-clock on
// every VIA1 access (generic two-clock formula; 25 MHz / 783 360 is not
// an integer ratio, unlike the V8's ÷20).
// In MACHINE cycles, not core-clock cycles: under the i-cache throughput
// overlay the core clock runs cacheBoost_× fast, and aligning to a
// boost-fast phantom E-clock shrinks every VIA-paced pulse by the same
// factor — which is what starved the Egret transport on the 25/33 MHz
// Sonora machines (CHANGELOG 2026-07-25).
void SonoraMemory::viaSync() {
    if (!cpu_) return;
    int64_t c = cpu_->machineClock();
    int64_t viaCycle = c * kViaHz / cpuHz_;
    int64_t target = (viaCycle * 2 + 3) * cpuHz_ / (2 * kViaHz) + 1;
    if (target > c) cpu_->stall(int(target - c));
}

uint8_t SonoraMemory::viaAccess8(uint32_t addr, bool write, uint8_t v) {
    if (cpu_) cpu_->flushTicks();
    viaSync();
    int reg = (addr >> 9) & 0x0F;            // $200 stride (sonora.cpp:319-345)
    if (write) {
        via_.write(reg, v);
        if (reg == Via6522::ORB || reg == Via6522::DDRB) {
            if (egretLleOn_) egretLle_.portBChanged(via_.portB());
            else             egret_.portBChanged(via_.portB());
        }
        updateIrq();
        return 0;
    }
    if (reg == Via6522::ORB)
        via_.setInB(uint8_t(0xC7 | (xcvrSession_() << 3)));
    uint8_t d = via_.read(reg);
    updateIrq();
    return d;
}

uint8_t SonoraMemory::vctrlRead(int reg) {
    switch (reg) {
    case 0: return vidMode_;
    case 1: return vidDepth_;
    case 2: {
        // Monitor sense (mv_sonora.cpp:267-292): 3 pulled-up lines the
        // system can drive through vidMonId_; simple monitors ground a
        // code, extended ones tie lines together ($40 flag).
        uint8_t mon = montype_;
        uint8_t res;
        if (mon & 0x40) {
            res = 7;
            if (!(vidMonId_ & 0xC))
                res &= uint8_t(4 | (((mon >> 5) & 1) << 1) | ((mon >> 4) & 1));
            if (!(vidMonId_ & 0xA))
                res &= uint8_t((((mon >> 3) & 1) << 2) | 2 | ((mon >> 2) & 1));
            if (!(vidMonId_ & 0x9))
                res &= uint8_t((((mon >> 1) & 1) << 2) | ((mon & 1) << 1) | 1);
        } else {
            res = mon;
            if (!(vidMonId_ & 8)) res &= vidMonId_ & 7;
        }
        return uint8_t(vidMonId_ | (res << 4));
    }
    case 3: return vidVtest_;
    case 4: case 5: case 6: case 7: {
        // Beam position from the frame phase (hpos/vpos, mv_sonora
        // vctrl_r cases 4-7).
        if (!mode_ || !frameCycles_) return 0;
        int64_t dots = framePos_ * mode_->dot / cpuHz_;
        int hpos = int(dots % mode_->htot), vpos = int(dots / mode_->htot);
        switch (reg) {
        case 4: return uint8_t((hpos >> 8) & 7);
        case 5: return uint8_t(hpos & 0xFF);
        case 6: return uint8_t((vpos >> 8) & 3);
        default: return uint8_t(vpos & 0xFF);
        }
    }
    }
    return 0;
}

void SonoraMemory::vctrlWrite(int reg, uint8_t v) {
    switch (reg) {
    case 0: {
        vidMode_ = v & 0x9F;
        const Modeline* m = modeline(vidMode_);
        if (m != mode_) {
            mode_ = m;
            framePos_ = 0;
            if (vblState_) {
                vblState_ = false;
                pvia_.slotIrq(PseudoVia::VBL, false);
                updateIrq();
            }
            if (m) {
                frameCycles_ = int64_t(m->htot) * m->vtot * cpuHz_ / m->dot;
                vblStart_ = int64_t(m->htot) * m->vres() * cpuHz_ / m->dot;
                frameTotalLines_ = int(m->vtot);
            } else {
                frameCycles_ = vblStart_ = 0;
                frameTotalLines_ = 0;
            }
        }
        break;
    }
    case 1: vidDepth_ = v & 7; break;
    case 2: vidMonId_ = v & 0xF; break;
    case 3: vidVtest_ = v & 1; break;
    default: break;
    }
}

uint8_t SonoraMemory::dacRead(int reg) const {
    return reg == 2 ? palControl_ : 0;       // mv_sonora dac_r
}

void SonoraMemory::dacWrite(int reg, uint8_t v) {
    switch (reg) {                           // mv_sonora dac_w
    case 0: palAddr_ = v; palIdx_ = 0; break;
    case 1:
        palRgb_[palIdx_++] = v;
        if (palIdx_ == 3) {
            // A monochrome modeline (only the 640×870 15" Portrait, sense
            // $01) wires the blue DAC to the video amplifier, so the R and
            // G bytes are dropped and blue drives all three primaries
            // (mv_sonora.cpp:373-388). The LC III's own two monitors are
            // RGB, so this never fires on a boot gate — it is the same
            // rule the RBV portrait display gets (RbvVideo.h pen()) and
            // the DAFB (Dafb::write32 $210), applied here at the write as
            // MAME does, because pens_[] is what the decoder reads.
            const bool mono = mode_ && mode_->monochrome;
            pens_[palAddr_] = mono
                ? (uint32_t(v) << 16 | uint32_t(v) << 8 | v)
                : (uint32_t(palRgb_[0]) << 16
                   | uint32_t(palRgb_[1]) << 8 | palRgb_[2]);
            palIdx_ = 0;
            palAddr_++;
        }
        break;
    case 2: palControl_ = v; break;
    case 3: palColkey_ = v; break;
    }
}

uint8_t SonoraMemory::scsiDma_() {
    if (!scsi_.drqActive()) busError();      // macscsi.cpp timeout → /BERR
    uint8_t d = scsi_.dmaRead();
    scsiDrq(scsi_.drqActive());
    return d;
}

void SonoraMemory::scsiDmaW_(uint8_t v) {
    if (!scsi_.drqActive()) busError();
    scsi_.dmaWrite(v);
    scsiDrq(scsi_.drqActive());
}

const uint8_t* SonoraMemory::codeSpan(uint32_t phys, uint32_t& len) const {
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

uint8_t* SonoraMemory::dataSpan(uint32_t phys, uint32_t& len, bool write) {
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

void SonoraMemory::jitMapChanged() {
    if (jitGuard_) jitGuard_->invalidate();
    if (cpu_) cpu_->pomJitDtlbFlush();
}

uint8_t SonoraMemory::read8(uint32_t addr) {
    if (addr < 0x40000000) {                 // RAM (ROM mirror under overlay)
        if (overlay_) return addr < kRomSize ? rom_[addr] : 0xFF;
        return addr < totalRam_ ? ram_[addr] : 0xFF;
    }
    if (addr < 0x50000000) {                 // ROM ×16 (sonora.cpp:51)
        if (overlay_) { overlay_ = false; jitMapChanged(); }      // rom_switch_r: any read clears
        return rom_[addr & (kRomSize - 1)];
    }
    if (addr >= 0x60000000) {
        // VRAM, 1 MB mirrored across the whole $6xxxxxxx select
        // (sonora.cpp:60 mirror $0FF00000).
        if (addr < 0x70000000) return vram_[addr & (kVramSize - 1)];
        return 0xFF;
    }
    if (addr >= 0x5FFFFFFC) {                // machine ID (maclc3.cpp:161)
        return uint8_t(machineId_ >> ((3 - (addr & 3)) * 8));
    }
    // $51000000-$5FFFFFFB: unmapped in MAME too (sonora.cpp:49-61 — every
    // window mirrors at most $00FC0000, inside $50xxxxxx; maclc3.cpp:147-161)
    // and the space has no unmap_value_high, so it reads 0 — same rule as
    // the in-page unmapped-I/O return below, not open-bus $FF.
    if (addr >= 0x51000000) return 0x00;

    // ── Sonora I/O page $50xxxxxx ──
    const uint32_t sub = addr & 0xFFFFFF;
    if (sub >= 0xF24000 && sub < 0xF24004) return dacRead(sub & 3);
    if (sub >= 0xF28000 && sub < 0xF28008) return vctrlRead(sub & 7);
    if ((sub & 0x3FFFF) < 0x2000)            // VIA1, mirror $FC0000
        return viaAccess8(sub, false, 0);
    if (cpu_) cpu_->flushTicks();
    const uint32_t low = sub & 0xFFFFF;      // device windows mirror $F00000
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
    if (low >= 0x16000 && low < 0x18000) {   // SWIM2, +5 wait states
        if (cpu_) cpu_->stall(5);
        return swim_.read((low >> 9) & 0xF);
    }
    if (low >= 0x26000 && low < 0x28000) {
        uint8_t d = pvia_.read(low - 0x26000);
        updateIrq();
        return d;
    }
    // Unmapped Sonora I/O reads back 0, not open-bus $FF: MAME's sonora map
    // has no catch-all (no BERR) and the space's unmap value is 0. The LC III+
    // ProductInfo ($A55A0003) pokes an un-emulated device at $50F0A000 and
    // spins on `btst #3` of the readback (RAM routine at $441A); $FF would
    // keep bit 3 set forever, 0 lets the poll fall through — the LC III path
    // never touches it, so this only unblocks the LC III+.
    return 0x00;
}

void SonoraMemory::write8(uint32_t addr, uint8_t v) {
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
    if (addr >= 0x51000000) return;

    const uint32_t sub = addr & 0xFFFFFF;
    if (sub >= 0xF24000 && sub < 0xF24004) { dacWrite(sub & 3, v); return; }
    if (sub >= 0xF28000 && sub < 0xF28008) { vctrlWrite(sub & 7, v); return; }
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
        swim_.write((low >> 9) & 0xF, v);
        return;
    }
    if (low >= 0x26000 && low < 0x28000) {
        pvia_.write(low - 0x26000, v);
        updateIrq();
        return;
    }
}

uint16_t SonoraMemory::read16(uint32_t addr) {
    // Word fast paths for the three flat regions (the V8 read16 lesson:
    // device space must go byte-by-byte, sequenced).
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
    if (addr >= 0x60000000 && addr < 0x70000000 - 1) {
        uint32_t o = addr & (kVramSize - 1);
        return uint16_t(vram_[o] << 8 | vram_[(o + 1) & (kVramSize - 1)]);
    }
    // VIA1 word reads mirror the byte on both lanes (sonora.cpp:319-332).
    // The DAC/vctrl windows don't collide: their A13-A17 bits are set.
    if (addr < 0x51000000 && (addr & 0x3E000) == 0) {
        uint16_t d = viaAccess8(addr & 0xFFFFFF, false, 0);
        return uint16_t(d | (d << 8));
    }
    // SCC word fast path: one side-effect, byte mirrored (V8 rule).
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

void SonoraMemory::write16(uint32_t addr, uint16_t v) {
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
    if (addr >= 0x50000000 && addr < 0x51000000 && (addr & 0x3E000) == 0) {
        // VIA1 word writes: low lane first (sonora.cpp:334-345)
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

uint8_t SonoraMemory::peek8(uint32_t addr) const {
    if (addr < 0x40000000) {
        if (overlay_) return addr < kRomSize ? rom_[addr] : 0xFF;
        return addr < totalRam_ ? ram_[addr] : 0xFF;
    }
    if (addr < 0x50000000) return rom_[addr & (kRomSize - 1)];
    if (addr >= 0x60000000 && addr < 0x70000000)
        return vram_[addr & (kVramSize - 1)];
    if (addr >= 0x5FFFFFFC && addr < 0x60000000)                   // model longword (mirrors read8)
        return uint8_t(machineId_ >> ((3 - (addr & 3)) * 8));
    return 0xFF;
}

void SonoraMemory::tick(int cpuCycles) {
    // VIA1 timers at 783.36 kHz (25 MHz / 31.914 — Bresenham).
    viaAcc_ += int64_t(cpuCycles) * kViaHz;
    int viaCycles = int(viaAcc_ / cpuHz_);
    viaAcc_ -= int64_t(viaCycles) * cpuHz_;
    if (viaCycles && via_.tick(viaCycles)) updateIrq();

    // Free-running 60.15 Hz tick timer → CA1 (sonora.cpp:206-210).
    tickAcc_ += int64_t(cpuCycles) * 1203;
    if (tickAcc_ >= cpuHz_ * 20) {
        tickAcc_ -= cpuHz_ * 20;
        via_.raiseCa1();
    }

    // Modeline-driven VBL → pseudo-VIA slot bit $40 (sonora.cpp:70).
    if (frameCycles_) {
        framePos_ += cpuCycles;
        // Completed frames, for the raster beam (VideoBeam::setPos): the
        // position alone is modulo, so a decoder sampling once per frame at
        // a fixed phase could not tell a whole frame from no time at all.
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

    if (egretLleOn_) egretLle_.tick(cpuCycles);
    else             egret_.tick(cpuCycles);
    asc_.tick(cpuCycles);                    // 22 257 Hz drain (25 MHz base)
    // SWIM2 cell engine runs at C15M; convert 25 MHz machine cycles.
    swimAcc_ += int64_t(cpuCycles) * 15667200;
    int swimCyc = int(swimAcc_ / cpuHz_);
    swimAcc_ -= int64_t(swimCyc) * cpuHz_;
    if (swimCyc) swim_.tick(swimCyc);
    drive_.tick(cpuCycles);
    scc_.tick(cpuCycles);
    sccIrq_ = scc_.irqAsserted();
    updateIrq();
}
