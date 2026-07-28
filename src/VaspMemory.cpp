// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "VaspMemory.h"
#include "VaspCpu.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

VaspMemory::VaspMemory(uint32_t totalRam, int64_t cpuHz, uint32_t machineId)
    : ram_(totalRam, 0), rom_(kRomSize, 0xFF), vram_(kVramSize, 0),
      egret_(via_, false, int(cpuHz)),
      egretLle_(via_, cpuHz, CudaLle::Flavor::Egret),
      totalRam_(totalRam), cpuHz_(cpuHz), machineId_(machineId) {
    egret_.setAdbBus(&adb_);
    // V8-style pseudo-VIA video hooks (vasp.cpp:308-315): the config read
    // returns the monitor sense on bits 3-5, the write latches depth.
    pvia_.onVideoRead = [this] { return uint8_t((montype_ << 3) & 0x38); };
    pvia_.onVideoWrite = [this](uint8_t v) { videoConfig_ = v; };
    asc_.onIrq = [this](bool s) { pvia_.ascIrq(s); updateIrq(); };
    // Egret firmware LLE — the IIvx/IIvi carry the LC III's 341S0851
    // (maciivx.cpp:407 set_default_bios_tag), 341S0850 as fallback.
    {
        const char* e = std::getenv("POM68K_EGRET_LLE");
        const bool want = !e || std::atoi(e) != 0;
        if (want) {
            for (const char* p : { "roms/egret/341s0851.bin",
                                   "../roms/egret/341s0851.bin",
                                   "roms/egret/341s0850.bin",
                                   "../roms/egret/341s0850.bin" }) {
                std::ifstream in(p, std::ios::binary);
                if (!in) continue;
                std::vector<uint8_t> fw((std::istreambuf_iterator<char>(in)),
                                        std::istreambuf_iterator<char>());
                if (egretLle_.loadFirmware(fw)) { egretLleOn_ = true; break; }
            }
            if (!egretLleOn_ && e)
                std::fprintf(stderr, "Vasp: POM68K_EGRET_LLE set but no "
                             "roms/egret/341s085x.bin — Egret HLE fallback\n");
        }
    }
    reset();
}

bool VaspMemory::loadRom(const std::vector<uint8_t>& data) {
    if (data.size() != kRomSize) return false;
    rom_ = data;
    uint32_t stored = uint32_t(rom_[0]) << 24 | uint32_t(rom_[1]) << 16
                    | uint32_t(rom_[2]) << 8 | rom_[3];
    uint32_t sum = 0;
    for (size_t i = 4; i + 1 < rom_.size(); i += 2)
        sum += uint32_t(rom_[i] << 8 | rom_[i + 1]);
    if (sum != stored)
        std::fprintf(stderr, "VaspMemory: ROM checksum $%08X != header $%08X\n",
                     sum, stored);
    return true;
}

void VaspMemory::reset() {
    overlay_ = true;
    sccIrq_ = false;
    scc_.reset();
    scc_.setClocks(cpuHz_, 7833600);         // SCC85C30 @ C7M
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
    ariel_.reset();
    asc_.reset();
    scsi_.reset();
    swim_.reset();
    swim_.attachDrive(&drive_, nullptr);
    drive_.setSpinClockHz(kCpuHzVi);         // drive_.tick unit = C15M cycles
    videoConfig_ = 0;
    viaAcc_ = tickAcc_ = c15Acc_ = 0;
    framePos_ = 0;
    vblState_ = false;
    // VIA1 port A reads $D5 (vasp.cpp via_in_a); port B carries only the
    // Egret XCVR_SESSION on PB3 (vasp.cpp via_in_b — MAME parity, no
    // pull-up composition).
    via_.setInA(0xD5);
    via_.setInB(uint8_t(xcvrSession_() << 3));
}

void VaspMemory::busError() const {
    if (cpu_) cpu_->extBusError();
    throw moira::MmuBusError{};
}

int VaspMemory::iplLevel() const {
    if (sccIrq_) return 4;                   // vasp.cpp field_interrupts
    if (pvia_.irqAsserted()) return 2;
    if (via_.irqAsserted()) return 1;
    return 0;
}

void VaspMemory::updateIrq() {
    if (cpu_) cpu_->updateIpl();
}

// vasp.cpp via_sync — stall the CPU to the 783.36 kHz VIA E-clock on
// every VIA1 access (the Sonora formula).
// Machine cycles, not core-clock cycles (SonoraMemory::viaSync).
void VaspMemory::viaSync() {
    if (!cpu_) return;
    int64_t c = cpu_->machineClock();
    int64_t viaCycle = c * kViaHz / cpuHz_;
    int64_t target = (viaCycle * 2 + 3) * cpuHz_ / (2 * kViaHz) + 1;
    if (target > c) cpu_->stall(int(target - c));
}

uint8_t VaspMemory::viaAccess8(uint32_t addr, bool write, uint8_t v) {
    if (cpu_) cpu_->flushTicks();
    viaSync();
    int reg = (addr >> 9) & 0x0F;            // $200 stride (vasp mac_via_r)
    if (write) {
        via_.write(reg, v);
        if (reg == Via6522::ORB || reg == Via6522::DDRB) {
            if (egretLleOn_) egretLle_.portBChanged(via_.portB());
            else             egret_.portBChanged(via_.portB());
        }
        if (reg == Via6522::ORA || reg == Via6522::DDRA)
            swim_.setSel((via_.portA() & 0x20) != 0);   // hdsel (via_out_a)
        updateIrq();
        return 0;
    }
    if (reg == Via6522::ORB)
        via_.setInB(uint8_t(xcvrSession_() << 3));
    uint8_t d = via_.read(reg);
    updateIrq();
    return d;
}

uint8_t VaspMemory::scsiDma_() {
    if (!scsi_.drqActive()) busError();      // macscsi.cpp timeout → /BERR
    uint8_t d = scsi_.dmaRead();
    scsiDrq(scsi_.drqActive());
    return d;
}

void VaspMemory::scsiDmaW_(uint8_t v) {
    if (!scsi_.drqActive()) busError();
    scsi_.dmaWrite(v);
    scsiDrq(scsi_.drqActive());
}

const uint8_t* VaspMemory::codeSpan(uint32_t phys, uint32_t& len) const {
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

uint8_t* VaspMemory::dataSpan(uint32_t phys, uint32_t& len, bool write) {
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

void VaspMemory::jitMapChanged() {
    if (jitGuard_) jitGuard_->invalidate();
    if (cpu_) cpu_->pomJitDtlbFlush();
}

uint8_t VaspMemory::read8(uint32_t addr) {
    if (addr < 0x40000000) {                 // RAM (ROM mirror under overlay)
        if (overlay_) return addr < kRomSize ? rom_[addr] : 0xFF;
        return addr < totalRam_ ? ram_[addr] : 0xFF;
    }
    if (addr < 0x50000000) {                 // ROM ×16 (vasp.cpp map)
        if (overlay_) { overlay_ = false; jitMapChanged(); }      // rom_switch_r: any read clears
        return rom_[addr & (kRomSize - 1)];
    }
    if (addr >= 0x60000000) {
        // VRAM, 1 MB mirrored across the $6xxxxxxx select (vasp.cpp:63).
        if (addr < 0x70000000) return vram_[addr & (kVramSize - 1)];
        return 0x00;                         // NuBus space, empty (MAME unmap)
    }
    if (addr >= 0x5FFFFFFC)                  // machine ID (maciivx.cpp:171)
        return uint8_t(machineId_ >> ((3 - (addr & 3)) * 8));

    // ── VASP I/O page $50xxxxxx (device windows mirror $F00000) ──
    const uint32_t low = addr & 0xFFFFF;
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
    if (low >= 0x10000 && low < 0x12000) {   // 53C80, stride $10
        int reg = (low >> 4) & 7;
        if (reg == 6 && (low & 0xFFF) == 0x260) return scsiDma_();
        uint8_t d = scsi_.read(reg);
        scsiDrq(scsi_.drqActive());
        return d;
    }
    if (low >= 0x14000 && low < 0x16000) return asc_.read(low - 0x14000);
    if (low >= 0x16000 && low < 0x18000) {   // SWIM1
        if (cpu_) cpu_->stall(5);
        swim_.setSel((via_.portA() & 0x20) != 0);
        return swim_.read((low >> 9) & 0xF);
    }
    if (low >= 0x24000 && low < 0x26000)     // Ariel-style DAC (vasp dac_r)
        return ariel_.read(low & 3);
    if (low >= 0x26000 && low < 0x28000) {
        uint8_t d = pvia_.read(low - 0x26000);
        updateIrq();
        return d;
    }
    return 0x00;                             // unmapped I/O = 0 (MAME parity)
}

void VaspMemory::write8(uint32_t addr, uint8_t v) {
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
    const uint32_t low = addr & 0xFFFFF;
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
    if (low >= 0x24000 && low < 0x26000) { ariel_.write(low & 3, v); return; }
    if (low >= 0x26000 && low < 0x28000) {
        pvia_.write(low - 0x26000, v);
        updateIrq();
        return;
    }
}

uint16_t VaspMemory::read16(uint32_t addr) {
    // Word fast paths for the flat regions (the V8 read16 lesson: device
    // space must go byte-by-byte, sequenced).
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
    // VIA1 word reads mirror the byte on both lanes (vasp mac_via_r).
    if (addr >= 0x50000000 && addr < 0x60000000) {
        const uint32_t low = addr & 0xFFFFF;
        if (low < 0x2000) {
            uint16_t d = viaAccess8(low, false, 0);
            return uint16_t(d | (d << 8));
        }
        if (low >= 0x04000 && low < 0x06000) {   // SCC: one side-effect
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

void VaspMemory::write16(uint32_t addr, uint16_t v) {
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
    if (addr >= 0x50000000 && addr < 0x60000000) {
        const uint32_t low = addr & 0xFFFFF;
        if (low < 0x2000) {
            // VIA1 word writes: low lane first (vasp mac_via_w)
            viaAccess8(low, true, uint8_t(v));
            viaAccess8(low, true, uint8_t(v >> 8));
            return;
        }
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

uint8_t VaspMemory::peek8(uint32_t addr) const {
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

void VaspMemory::tick(int cpuCycles) {
    // VIA1 timers at 783.36 kHz (Bresenham on cpuHz_).
    viaAcc_ += int64_t(cpuCycles) * kViaHz;
    int viaCycles = int(viaAcc_ / cpuHz_);
    viaAcc_ -= int64_t(viaCycles) * cpuHz_;
    if (viaCycles && via_.tick(viaCycles)) updateIrq();

    // Free-running 60.15 Hz tick timer → CA1 (vasp mac_6015_tick).
    tickAcc_ += int64_t(cpuCycles) * 1203;
    if (tickAcc_ >= cpuHz_ * 20) {
        tickAcc_ -= cpuHz_ * 20;
        via_.raiseCa1();
    }

    // Fixed 60 Hz VBL → pseudo-VIA slot bit $40 (vasp screen 800×525 @
    // 25.175 MHz; VBL covers the last 45 of 525 lines).
    framePos_ += cpuCycles;
    const int64_t frame = cpuHz_ / 60;
    framePos_ %= frame;
    bool vbl = framePos_ >= frame * 480 / 525;
    if (vbl != vblState_) {
        vblState_ = vbl;
        pvia_.slotIrq(PseudoVia::VBL, vbl);
    }

    if (egretLleOn_) egretLle_.tick(cpuCycles);
    else             egret_.tick(cpuCycles);

    // ASC drain + SWIM1 cells + floppy spin all run in the C15M domain the
    // V8 machines feed them natively; convert 31.3344 MHz IIvx cycles
    // (IIvi is 1:1).
    c15Acc_ += int64_t(cpuCycles) * kCpuHzVi;
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
