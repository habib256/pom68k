// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "MacIIMemory.h"
#include "Cpu020.h"
#include <cstdio>
#include <cstring>

MacIIMemory::~MacIIMemory() { delete toby_; }

MacIIMemory::MacIIMemory(uint32_t ramSize)
    : ram_(ramSize, 0), rom_(kRomSize, 0xFF), ramSize_(ramSize) {
    adbVia_.attach(via1_, adb_);
    // MAME mac_asc_irq: VIA2 CB1 = !asc_irq (active-low into the 6522).
    asc_.onIrq = [this](bool s) {
        via2_.setCb1(!s);
        via2Irq_ = via2_.irqAsserted();
        updateIrq();
    };
    nubus_.setIrqCallback([this](int slot, bool active) { nubusSlotIrq(slot, active); });
}

bool MacIIMemory::loadRom(const std::vector<uint8_t>& data) {
    if (data.size() != kRomSize) return false;
    rom_ = data;
    return true;
}

bool MacIIMemory::installTobyVideo(const std::string& declRomPath) {
    if (toby_) return true;
    toby_ = new TobyVideo(nubus_, 9);
    std::vector<uint8_t> decl;
    if (!declRomPath.empty())
        decl = DeclRom::loadTobyRaw(declRomPath);
    if (decl.empty()) {
        static const char* paths[] = {
            "tests/data/342-0008-a.bin",
            "roms/342-0008-a.bin",
        };
        for (const char* p : paths) {
            decl = DeclRom::loadTobyRaw(p);
            if (!decl.empty()) break;
        }
    }
    if (decl.empty()) {
        std::fprintf(stderr, "MacIIMemory: no Toby decl ROM — using synthetic\n");
        auto syn = DeclRom::buildSynthetic(nubus_.slotBase(9));
        decl = DeclRom::installRaw(syn.data(), syn.size());
    }
    nubus_.installCard(9, toby_, decl);
    return true;
}

void MacIIMemory::reset() {
    overlay_ = true;
    // MAME m_glue_ram_size starts 0; overlay-clear calls via2_out_a(0x3f)
    // which stores (0x3f & 0xc0) == 0 → bank B at 1 MiB.
    glueRamSize_ = 0x00;
    nubusIrqState_ = 0x3F;
    sccIrq_ = false;
    via2Irq_ = false;
    via2Pb7_ = true;
    viaPhase_ = 0;
    tickAcc_ = 0;
    secAcc_ = 0;
    via1_.reset();
    via2_.reset();
    rtc_.reset();
    rtc_.factoryDefaults();
    adb_.reset();
    adbVia_.reset();
    asc_.reset();
    scsi_.reset();
    iwm_.reset();
    iwm_.attachDrive(&drive_, nullptr);
    drive_.reset();
    scc_.reset();
    // Mac II POST probes RR0 &$70 (Sync/CTS/TxU); CTS high looks like a
    // serial debugger (same Q5 Quadra note) — pull it low like the LC 475.
    scc_.setCtsHigh(false);
    // No standing Rx: D7=$…0005 in the diagnostic console means the RAM
    // sizing probe failed first; fixing glue/mirrors exits that path.
    if (toby_) toby_->reset();
    via1_.setInA(0x81);
    via1_.setInB(0xCF);
    via2_.setInB(0xCF);                      // MAME via2_in_b
    via2_.setCb1(true);
    via2_.setCb2(true);
    refreshVia2PortA();
}

void MacIIMemory::applyRamBank() {
    // GLUE PA6-7 select bank-B window (macii.cpp via2_out_a). Physical RAM
    // stays contiguous at $0; mirrors beyond ramSize_ are handled in ramAt().
}

// MAME via2_out_a for 8 MiB: asize=4 MiB mirrored at [memsize, memsize+asize).
uint8_t* MacIIMemory::ramAt(uint32_t addr) {
    if (addr < ramSize_) return &ram_[addr];
    if (ramSize_ == 0x800000 && addr < 0x00C00000u)
        return &ram_[addr - 0x800000];       // 8–12 MiB → bank A[0–4 MiB)
    return nullptr;
}

const uint8_t* MacIIMemory::ramAt(uint32_t addr) const {
    if (addr < ramSize_) return &ram_[addr];
    if (ramSize_ == 0x800000 && addr < 0x00C00000u)
        return &ram_[addr - 0x800000];
    return nullptr;
}

void MacIIMemory::nubusSlotIrq(int slot, bool active) {
    static const uint8_t masks[] = {0x1, 0x2, 0x4, 0x8, 0x10, 0x20};
    int idx = slot - 9;
    if (idx < 0 || idx > 5) return;
    if (active) nubusIrqState_ &= uint8_t(~masks[idx]);
    else nubusIrqState_ |= masks[idx];
    refreshVia2PortA();
    if ((nubusIrqState_ & 0x3F) != 0x3F) {
        via2_.raiseCa1();
        via2Irq_ = true;
    } else {
        via2Irq_ = via2_.irqAsserted();
    }
    updateIrq();
}

void MacIIMemory::refreshVia2PortA() {
    via2_.setInA(uint8_t(glueRamSize_ | nubusIrqState_));
}

void MacIIMemory::refreshVia1PortB() {
    // MAME macii via_in_b: PB0 = RTC data, PB3 = /ADB IRQ (active low).
    uint8_t in = rtc_.dataBit() & 1;
    in |= 0xC6;                                   // unused pulled up
    if (!adbVia_.irqPending()) in |= 0x08;
    via1_.setInB(in);
}

void MacIIMemory::busError() const {
    if (cpu_) cpu_->extBusError();
    throw moira::MmuBusError{};
}

int MacIIMemory::iplLevel() const {
    if (sccIrq_) return 4;
    if (via2Irq_ || via2_.irqAsserted()) return 2;
    if (via1_.irqAsserted()) return 1;
    return 0;
}

void MacIIMemory::updateIrq() {
    if (cpu_) cpu_->updateIpl();
}

void MacIIMemory::viaSync() {
    if (!cpu_) return;
    int64_t c = cpu_->getClock();
    int64_t viaCycle = c / 20;
    int64_t target = (viaCycle * 2 + 3) * 10 + 1;
    if (target > c) cpu_->stall(int(target - c));
}

bool MacIIMemory::isIo(uint32_t addr, uint32_t& off) const {
    // MAME macii_map: I/O @$50000000 with .mirror(0x00F00000) — ROM uses
    // $50F0xxxx. Strip those mirror bits before decoding the 128 KB window.
    if ((addr & 0xFF000000u) != 0x50000000u) return false;
    off = (addr & ~0x00F00000u) - 0x50000000u;
    return true;
}

uint16_t MacIIMemory::viaAccess(Via6522& via, uint32_t addr, bool write, uint16_t v,
                                bool isVia1) {
    viaSync();
    // MAME via_r: word_offset >> 8 ≡ byte_offset >> 9 ($200 register stride,
    // same as Mac Plus / V8). Using >> 8 on byte offsets mis-routed ORA_NH
    // ($1E00) into IER and left ROM overlay stuck on.
    int reg = (addr >> 9) & 0xF;
    if (write) {
        if (reg == Via6522::ORA || reg == Via6522::ORA_NH || reg == Via6522::DDRA) {
            via.write(reg, uint8_t(v & 0xFF));
            if (isVia1) {
                // MAME via_out_a(data): overlay from the PA write data bit,
                // not floating inputs (DDRA may still mark the pin as input).
                if (reg == Via6522::ORA || reg == Via6522::ORA_NH)
                    overlay_ = (v & 0x10) != 0;
                else
                    overlay_ = (via.portA() & 0x10) != 0;
                iwm_.setSel((reg == Via6522::ORA || reg == Via6522::ORA_NH)
                            ? (v & 0x20) != 0
                            : (via.portA() & 0x20) != 0);
                // MAME set_memory_overlay(0): via2_out_a(0x3f) → glue &= $C0 == 0.
                if (!overlay_) {
                    glueRamSize_ = 0x00;
                    refreshVia2PortA();
                }
            } else if (reg == Via6522::ORA || reg == Via6522::ORA_NH) {
                // MAME via2_out_a(data): glue from the written byte, not portA().
                glueRamSize_ = uint8_t(v & 0xC0);
                applyRamBank();
                refreshVia2PortA();
            } else {
                glueRamSize_ = uint8_t(via.portA() & 0xC0);
                applyRamBank();
                refreshVia2PortA();
            }
        } else {
            via.write(reg, uint8_t(v & 0xFF));
            if ((v >> 8) & 0xFF) via.write(reg, uint8_t(v >> 8));
        }
        if (isVia1) {
            if (reg == Via6522::ORB || reg == Via6522::DDRB || reg == Via6522::SR
                || reg == Via6522::ACR)
                adbVia_.sync();
            rtc_.setLines((via.portB() & 0x04) != 0,
                          (via.portB() & 0x02) != 0,
                          (via.portB() & 0x01) != 0);
        } else if (reg == Via6522::ORB || reg == Via6522::DDRB) {
            // MAME via2_out_b: PB7 level chains into VIA1 CA1 (60.15 Hz).
            const bool pb7 = (via.portB() & 0x80) != 0;
            if (pb7 != via2Pb7_) {
                via2Pb7_ = pb7;
                if (!pb7) via1_.raiseCa1();
                updateIrq();
            }
        }
        if (!isVia1 && reg == Via6522::IFR)
            via2Irq_ = via.irqAsserted();
        updateIrq();
        return 0;
    }
    if (isVia1 && reg == Via6522::ORB) refreshVia1PortB();
    if (!isVia1 && reg == Via6522::ORA) refreshVia2PortA();
    uint8_t lo = via.read(reg);
    updateIrq();
    if (!isVia1 && reg == Via6522::IFR)
        via2Irq_ = via.irqAsserted();
    return uint16_t(lo) | (uint16_t(lo) << 8);
}

uint8_t MacIIMemory::scsiDma() {
    if (!scsi_.drqActive()) busError();
    return scsi_.dmaRead();
}

void MacIIMemory::scsiDmaW(uint8_t v) {
    if (!scsi_.drqActive()) busError();
    scsi_.dmaWrite(v);
}

uint8_t MacIIMemory::read8(uint32_t addr) {
    if (addr < 0x40000000u) {
        if (overlay_) {
            if (addr < kRomSize) return rom_[addr];
            return 0xFF;
        }
        if (const uint8_t* p = ramAt(addr)) return *p;
        return 0xFF;
    }
    if (addr >= 0x40000000u && addr < 0x50000000u)
        return rom_[(addr - 0x40000000u) & (kRomSize - 1)];

    uint32_t ioOff = 0;
    if (isIo(addr, ioOff)) {
        if (cpu_) cpu_->flushTicks();
        if (ioOff < 0x2000 || (ioOff >= 0x40000 && ioOff < 0x42000)) {
            uint32_t voff = ioOff & 0x1FFF;
            uint16_t w = viaAccess(via1_, voff, false, 0, true);
            return uint8_t(w >> 8);
        }
        if (ioOff < 0x4000) {
            uint16_t w = viaAccess(via2_, ioOff - 0x2000, false, 0, false);
            return uint8_t(w >> 8);
        }
        if (ioOff < 0x6000) {
            int ch = (ioOff >> 1) & 1;
            uint8_t d = ((ioOff >> 2) & 1) ? scc_.readData(ch) : scc_.readCtl(ch);
            sccIrq_ = scc_.irqAsserted();
            updateIrq();
            return d;
        }
        if (ioOff == 0x6000 || ioOff == 0x6060 || ioOff == 0x12000)
            return scsiDma();
        if (ioOff >= 0x10000 && ioOff < 0x12000) {
            // MAME scsi_r: reg = (word_offset >> 3) & 0xf ≡ byte_off >> 4.
            const uint32_t rel = ioOff - 0x10000;
            int reg = (rel >> 4) & 7;
            // pseudo-DMA read: word offset $130 → byte $260 into the window
            if (reg == 6 && rel == 0x260) return scsiDma();
            return scsi_.read(reg);
        }
        if (ioOff >= 0x14000 && ioOff < 0x16000)
            return asc_.read(ioOff - 0x14000);
        if (ioOff >= 0x16000 && ioOff < 0x18000) {
            if (cpu_) cpu_->stall(5);
            return iwm_.read((ioOff >> 9) & 0xF);
        }
        return 0xFF;                             // open bus (map probe)
    }

    if (addr >= 0x90000000u && addr < 0xFF000000u)
        return nubus_.read8(addr);
    // 24-bit NuBus slot window $s00000 (s=$9..$E)
    if (addr >= 0x00900000u && addr < 0x00F00000u)
        return nubus_.read8(addr);
    return 0xFF;                                 // unmapped: open bus
}

uint16_t MacIIMemory::read16(uint32_t addr) {
    if (addr >= 0x50000000u && addr < 0x60000000u) {
        uint32_t ioOff = 0;
        if (isIo(addr, ioOff)) {
            if (ioOff < 0x2000 || (ioOff >= 0x40000 && ioOff < 0x42000))
                return viaAccess(via1_, ioOff & 0x1FFF, false, 0, true);
            if (ioOff < 0x4000)
                return viaAccess(via2_, ioOff - 0x2000, false, 0, false);
            if (ioOff < 0x6000) {
                uint8_t d = read8(addr);
                return uint16_t(d) | (uint16_t(d) << 8);
            }
        }
    }
    return uint16_t(read8(addr) << 8) | read8(addr + 1);
}

void MacIIMemory::write8(uint32_t addr, uint8_t v) {
    if (addr < 0x40000000u) {
        if (overlay_) return;
        if (uint8_t* p = ramAt(addr)) *p = v;
        return;
    }
    if (addr >= 0x40000000u && addr < 0x50000000u) return;

    uint32_t ioOff = 0;
    if (isIo(addr, ioOff)) {
        if (cpu_) cpu_->flushTicks();
        if (ioOff < 0x2000 || (ioOff >= 0x40000 && ioOff < 0x42000)) {
            viaAccess(via1_, ioOff & 0x1FFF, true, v, true);
            return;
        }
        if (ioOff < 0x4000) {
            viaAccess(via2_, ioOff - 0x2000, true, v, false);
            return;
        }
        if (ioOff < 0x6000) {
            int ch = (ioOff >> 1) & 1;
            if ((ioOff >> 2) & 1) scc_.writeData(ch, v);
            else scc_.writeCtl(ch, v);
            sccIrq_ = scc_.irqAsserted();
            updateIrq();
            return;
        }
        if (ioOff == 0x6000 || ioOff == 0x6060 || ioOff == 0x12000) {
            scsiDmaW(v); return;
        }
        if (ioOff >= 0x10000 && ioOff < 0x12000) {
            const uint32_t rel = ioOff - 0x10000;
            int reg = (rel >> 4) & 7;
            // pseudo-DMA write: word offset $100 → byte $200 into the window
            if (reg == 0 && rel == 0x200) { scsiDmaW(v); return; }
            scsi_.write(reg, v);
            return;
        }
        if (ioOff >= 0x14000 && ioOff < 0x16000) {
            asc_.write(ioOff - 0x14000, v); return;
        }
        if (ioOff >= 0x16000 && ioOff < 0x18000) {
            if (cpu_) cpu_->stall(5);
            iwm_.write((ioOff >> 9) & 0xF, v);
            return;
        }
        return;                                    // open bus
    }

    if (addr >= 0x90000000u && addr < 0xFF000000u) {
        nubus_.write8(addr, v);
        return;
    }
    if (addr >= 0x00900000u && addr < 0x00F00000u) {
        nubus_.write8(addr, v);
        return;
    }
}

void MacIIMemory::write16(uint32_t addr, uint16_t v) {
    if (addr >= 0x50000000u && addr < 0x60000000u) {
        uint32_t ioOff = 0;
        if (isIo(addr, ioOff)) {
            if (ioOff < 0x2000 || (ioOff >= 0x40000 && ioOff < 0x42000)) {
                viaAccess(via1_, ioOff & 0x1FFF, true, v, true);
                return;
            }
            if (ioOff < 0x4000) {
                viaAccess(via2_, ioOff - 0x2000, true, v, false);
                return;
            }
        }
    }
    write8(addr, uint8_t(v >> 8));
    write8(addr + 1, uint8_t(v));
}

uint8_t MacIIMemory::peek8(uint32_t addr) const {
    if (!overlay_) {
        if (const uint8_t* p = ramAt(addr)) return *p;
    }
    if (addr >= 0x40000000u && addr < 0x50000000u)
        return rom_[(addr - 0x40000000u) & (kRomSize - 1)];
    return 0xFF;
}

void MacIIMemory::tick(int cpuCycles) {
    tickCalls_++;
    viaPhase_ += cpuCycles;
    int t = viaPhase_ / 20;
    viaPhase_ %= 20;
    if (t) {
        if (via1_.tick(t)) updateIrq();
        via2_.tick(t);
        // Only IER-enabled IFR bits raise IPL (MAME via6522). A sticky
        // via2Irq_=true on any T1 underflow with IER empty pinned IPL at 2
        // and starved the VIA1 VBL path the $6DD8 wait needs.
        via2Irq_ = via2_.irqAsserted();
        if (via2Irq_) updateIrq();
    }
    adbVia_.tick(cpuCycles);
    asc_.tick(cpuCycles);
    // MAME scsi_irq → VIA2 CB2 active-low (write_cb2(state ^ 1)).
    via2_.setCb2(!scsi_.irqAsserted());
    via2Irq_ = via2_.irqAsserted();
    updateIrq();
    iwm_.tick(cpuCycles);
    drive_.tick(cpuCycles);
    int stepped = mouse_.tick(cpuCycles);
    if (stepped) {
        if (stepped & 1) scc_.setDcd(1, mouse_.x1);
        if (stepped & 2) scc_.setDcd(0, mouse_.y1);
        sccIrq_ = scc_.irqAsserted();
        updateIrq();
    }
    nubus_.tick(cpuCycles);

    tickAcc_ += cpuCycles;
    if (tickAcc_ >= kCpuHz / 60) {
        tickAcc_ -= kCpuHz / 60;
        // Prefer VIA2 PB7→VIA1 CA1 when the ROM has armed that path; otherwise
        // pulse CA1 directly so VBL still arrives (MAME scanline / via2_out_b).
        via1_.raiseCa1();
        vblPulses_++;
        if (!via1_.irqAsserted()) ++vblPulseNoIrq_;
        updateIrq();
    }
    secAcc_ += cpuCycles;
    if (secAcc_ >= kCpuHz) {
        secAcc_ -= kCpuHz;
        rtc_.tickSecond();
        via1_.raiseCa2();
        updateIrq();
    }
}
