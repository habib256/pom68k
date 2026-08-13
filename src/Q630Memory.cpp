// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Q630Memory.h"
#include "LleSession.h"
#include "Q630Cpu.h"
#include "Moira.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

Q630Memory::Q630Memory(uint32_t totalRam)
    : totalRam_(totalRam)
{
    // The ROM's bank prober sizes RAM by ALIASING (write a pattern,
    // find where it reappears): the size must be a power of two and
    // the whole $0-$3FFFFFFF window must mirror modulo the size, like
    // undecoded address lines on a real bank. Round down to a power of
    // two.
    while (totalRam_ & (totalRam_ - 1)) totalRam_ &= totalRam_ - 1;
    ram_.assign(totalRam_, 0);
    rom_.assign(kRomSize, 0xFF);
    vram_.assign(kVramSize, 0);
    cuda_.setAdbBus(&adb_);
    // Valkyrie VBL → PrimeTime II via2_irq_w<0x40> (nubus bit 6)
    // (macquadra630.cpp:172).
    video_.onIrq = [this](bool s) { vblIrq(s); };
    // EASC half-empty IRQ → pseudo-VIA2 bit 4 (iosb.cpp:358-361).
    asc_.onIrq = [this](bool s) { ascIrq(s); };
    // Two MFD-75W SuperDrives behind PrimeTime's SWIM2.
    drive0_.setSuperDrive(true);
    drive1_.setSuperDrive(true);
    // machineTick spins the drives in 25 MHz machine cycles — declare the
    // clock so the raw-cell rotation angle (SWIM2 read/write resync) is
    // in real time.
    drive0_.setSpinClockHz(kCpuHz);
    drive1_.setSpinClockHz(kCpuHz);
    swim_.attachDrive(&drive0_, &drive1_);
    // A cold (unsigned) XPRAM makes the ROM run its LONG full-RAM
    // burn-in on every boot and boots B&W — seed the Basilisk-verified
    // 'NuMc' defaults (docs: macemu main.cpp:106-141)
    cuda_.factoryDefaults();
    // XPRAM $78/$7A: boot drive/driver 0 (Basilisk defaults) already 0;
    // $8A |= $05 = 32-bit mode (Mac OS 8 requires 32-bit clean)
    cuda_.setPram(0x8A, uint8_t(cuda_.pram(0x8A) | 0x05));
    // SCSI bus-service latency (Q6.5b diagnostics → LLE step 9 default-on):
    // cycles between a command and its interrupt. Default is the MAME-derived
    // delay model (-1: ncr53c90.cpp arbitrate/settle chain + sync_period per
    // byte at the 40 MHz chip clock — Ncr53c96::selectionDelayCpu_/
    // xferDelayCpu_). POM68K_SCSI_LAT=0 forces the historical instant
    // behaviour, =N a flat N-cycle deferral (diagnostics).
    {
        const char* e = std::getenv("POM68K_SCSI_LAT");
        scsi_.setLatency(e ? std::atoi(e) : -1);
    }
    if (const char* id = std::getenv("POM68K_Q630_ID"))
        machineId_ = uint32_t(std::strtoul(id, nullptr, 16));
    // Cuda firmware LLE — the DEFAULT whenever the real dump is present
    // (blueprint step 4, the POM68K_ADB_LLE rollout pattern);
    // POM68K_CUDA_LLE=0 forces the Egret HLE, a missing dump falls back
    // silently. The staged PRAM mirrors the Egret HLE's factory seed so
    // both paths boot from the same battery contents.
    // The F108 board hangs TWO slaves on the Cuda's I2C, on one wired-AND
    // SDA (macquadra630.cpp:187-199): the DFAC2 at $6F (ACK only — its
    // payload is oracle-discarded) and the **Valkyrie clock generator at
    // $28**, whose payload sets the video pixel clock. Framing lives in
    // `CudaLle::i2cWire`; only the second one carries data worth keeping.
    cudaLle_.setI2cDfac(true);
    cudaLle_.onI2cValkyrie = [this](uint8_t reg, uint8_t v) {
        video_.i2cWrite(reg, v);
    };
    {
        const char* e = std::getenv("POM68K_CUDA_LLE");
        const bool want = !e || std::atoi(e) != 0;
        if (want) {
            // macquadra630.cpp:175 set_default_bios_tag("341s0060") — the
            // same Cuda 2.40 the LC 520 family runs.
            for (const char* p : { "roms/cuda/341s0060.bin",
                                   "../roms/cuda/341s0060.bin" }) {
                std::ifstream in(p, std::ios::binary);
                if (!in) continue;
                std::vector<uint8_t> fw((std::istreambuf_iterator<char>(in)),
                                        std::istreambuf_iterator<char>());
                if (cudaLle_.loadFirmware(fw)) { cudaLleOn_ = true; break; }
            }
            if (cudaLleOn_)
                for (int i = 0; i < 256; i++)
                    cudaLle_.setPram(i, cuda_.pram(i));
            else
                std::fprintf(stderr, "Q630: no roms/cuda/341s0060.bin — "
                             "running the NON-CONFORMANT HLE ADB substitute "
                             "(docs/LLE_VS_HLE.md §2)\n");
        } else {
            std::fprintf(stderr, "Q630: POM68K_CUDA_LLE=0 — NON-CONFORMANT "
                         "HLE ADB substitute forced\n");
        }
        if (!cudaLleOn_) pom68k::lle::activateHle(pom68k::lle::HleEgretCuda);
    }
    // Firmware RESET_SYSTEM ($11) — the Finder's "Restart". DEFERRED: this
    // fires from inside viaWrite(), under the CPU, and reset() would reset
    // the very MCU that is mid-instruction issuing it. Latch here, act at a
    // run boundary in the CPU wrapper. Only the address map comes back; the
    // devices, the PRAM and the MCU keep running, which is what the /RESET
    // line does on the board (the gate array and the CPU sit on it; the
    // Egret/Cuda is the one pulling it).
    cudaLle_.onCpuReset = [this] {
        overlay_ = true;
        jitMapChanged();
        restartPending_ = true;
    };
}

bool Q630Memory::loadRom(const std::vector<uint8_t>& data) {
    if (data.size() != kRomSize) return false;
    std::memcpy(rom_.data(), data.data(), kRomSize);
    jitMapChanged();
    return true;
}


void Q630Memory::reset() {
    overlay_ = true;
    restartPending_ = false;   // a cold reset supersedes a warm one
    jitMapChanged();
    pvIfr_ = pvIer_ = pvPortB_ = 0;
    nubusIrqs_ = 0xFF;
    std::memset(iosbRegs_, 0, sizeof iosbRegs_);
    ataIrq_ = false;
    video_.reset();
    via1_.reset();
    cuda_.reset();
    cudaLle_.reset();
    scc_.reset();
    scsi_.reset();
    sccDebt_ = scsiDebt_ = 0;
    asc_.reset();
    swim_.reset();
    swim_.attachDrive(&drive0_, &drive1_);
    drive0_.reset();
    drive1_.reset();
    ascLine_ = false;
    scsiReadCycles_ = scsiWriteCycles_ = 3;      // iosb.cpp:144-148 defaults
    scsiDmaReadCycles_ = scsiDmaWriteCycles_ = 3;
    ascCycAcc_ = 0;
    swimLastCpu_ = -1;
    swimCycAcc_ = 0;
    scc_.setClocks(kCpuHz, 7833600);   // SCC85C30 @ C7M (f108.cpp:76)
    scc_.setCtsHigh(false);        // no serial debugger attached (POST check)
    scc_.setAbortIdle(true);       // no *hardwired* LocalTalk peer. The abort
                                   // is a LINE state, not machine config: a
                                   // virgin line reads clean (OT binds .MPP),
                                   // the standing abort exists once the line
                                   // has carried a frame, and a live peer
                                   // suppresses it — Scc8530::openLine
                                   // (LLE steps 7+8, docs/LLE_VS_HLE.md §1.10).
    viaEClock_ = {};
    tickAcc_ = 0;
    sccIrq_ = false;
}

void Q630Memory::busError(uint32_t addr, bool write) const {
    if (onBusError) onBusError(addr, write);
    if (cpu_) cpu_->extBusError040();
    throw moira::MmuBusError{};
}

int Q630Memory::iplLevel() const {
    if (sccIrq_) return 4;                   // iosb field_interrupts
    if (via2IrqAsserted()) return 2;
    if (via1_.irqAsserted()) return 1;
    return 0;
}

void Q630Memory::updateIrq() {
    if (cpu_) cpu_->updateIpl();
}

void Q630Memory::via2Recalc() {
    if (ascLine_) pvIfr_ |= 0x10;
    if (via2IrqAsserted()) pvIfr_ |= 0x80; else pvIfr_ &= ~0x80;
    updateIrq();
}

void Q630Memory::vblIrq(bool s) {
    // MEMCjr video IRQ -> IOSB via2_irq_w<0x40>: nubus bit 6 (active
    // low) + the pseudo-VIA slot-summary IFR bit 1
    if (s) nubusIrqs_ &= ~0x40; else nubusIrqs_ |= 0x40;
    if ((nubusIrqs_ & 0x79) != 0x79) pvIfr_ |= 0x02;
    else                             pvIfr_ &= ~0x02;
    via2Recalc();
}

void Q630Memory::scsiIrq(bool s) {
    if (s) pvIfr_ |= 0x08;                   // CB2 bit (pseudovia.cpp:148)
    else   pvIfr_ &= ~0x08;
    via2Recalc();
}

// MAME swim2_device::read/write begin with sync(). Drive SWIM from the CPU
// clock so sync-on-access and the batched machine tick share one timeline.
// Moira's clock runs cacheBoost_× ahead of machine time; convert deltas
// before the C15M ratio so SWIM stays locked to VIA/ASC/SCSI under boost.
void Q630Memory::syncSwimFromCpu() {
    if (!cpu_) return;
    const int64_t now = int64_t(cpu_->getClock());
    if (swimLastCpu_ < 0) { swimLastCpu_ = now; return; }
    const int64_t delta = now - swimLastCpu_;
    if (delta <= 0) return;
    swimLastCpu_ = now;
    const int boost = std::max(1, cpu_->cacheBoost());
    swimCycAcc_ += (delta * AscIosb::kCpuHz) / boost;
    const int cyc = int(swimCycAcc_ / kCpuHz);
    swimCycAcc_ -= int64_t(cyc) * kCpuHz;
    if (cyc) swim_.tick(cyc);
}

void Q630Memory::ascIrq(bool s) {
    ascLine_ = s;
    if (s) pvIfr_ |= 0x10;                   // EASC bit 4 (IFR/IER mask $1B)
    else   pvIfr_ &= ~0x10;
    via2Recalc();
}

// VIA1 E-clock sync (iosb via_sync, same arithmetic as the LC II). VIA1 runs
// at a FIXED 783.36 kHz — iosb.cpp:74 is R65NC22(config, m_via1, C7M / 10)
// with C7M = 7 833 600 — so the divider follows the MACHINE clock, it is not
// a constant: 25 MHz / 783.36 kHz ≈ 32 (the Q605 value this file was copied
// from), but this board runs at 33 MHz, where it is ≈ 42. Work in
// machine-cycle space so the alignment is invariant under
// POM68K_Q605_CACHE_BOOST.
void Q630Memory::viaSync() {
    if (!cpu_) return;
    const int boost = std::max(1, cpu_->cacheBoost());
    int64_t c = int64_t(cpu_->getClock()) / boost;
    const int64_t target = via_eclock::syncTarget(c, kCpuHz);
    if (target > c) cpu_->stall(int(target - c));
}

uint8_t Q630Memory::viaAccess8(uint32_t addr, bool write, uint8_t v) {
    if (cpu_) cpu_->flushTicks();
    viaSync();
    int reg = (addr >> 9) & 0x0F;            // $200 stride
    if (write) {
        via1_.write(reg, v);
        // Port B outputs carry the Cuda handshake (PB4 BYTEACK/VIA_FULL,
        // PB5 TIP/SYS_SESSION — macquadra605.cpp:230-233)
        if (reg == Via6522::ORB || reg == Via6522::DDRB) {
            if (cudaLleOn_) cudaLle_.portBChanged(via1_.portB());
            else            cuda_.portBChanged(via1_.portB());
        }
        updateIrq();
        return 0;
    }
    if (reg == Via6522::ORB)                 // PB3 = TREQ, live
        via1_.setInB(uint8_t(0xC7 | ((cudaLleOn_ ? cudaLle_.xcvrSession()
                                                 : cuda_.xcvrSession()) << 3)));
    uint8_t d = via1_.read(reg);
    updateIrq();
    return d;
}

uint8_t Q630Memory::via2Access8(uint32_t addr, bool write, uint8_t v) {
    if (cpu_) cpu_->flushTicks();
    flushScsi();
    viaSync();
    int reg = (addr >> 9) & 0x0F;            // pseudovia.cpp quadra flavor
    if (write) {
        switch (reg) {
            case 0:  pvPortB_ = v; break;    // DFAC latches — ignored
            case 1:  break;                  // port A is input-only here
            case 13: pvIfr_ &= ~(v & 0x1B); via2Recalc(); break;
            case 14:
                if (v & 0x80) pvIer_ |= v & 0x1B;
                else          pvIer_ &= ~(v & 0x1B);
                via2Recalc();
                break;
            default: break;
        }
        return 0;
    }
    switch (reg) {
        case 0:  return pvPortB_;
        case 1: case 15: return nubusIrqs_;
        // IFR bit0 reflects the live SCSI DRQ line (pseudovia.cpp:162
        // scsi_drq_w sets reg[3] |= 0x01). The Mac OS 8.1 SCSI driver polls
        // this bit ($408D1FA2) between the 53C96's S_TC0 and its 16-byte
        // pseudo-DMA window burst.
        case 13: return (pvIfr_ & ~0x01) | (scsi_.drq() ? 0x01 : 0);
        case 14: return pvIer_;
        default: return 0;
    }
}

// ── Valkyrie register cell (MAME valkyrie.cpp) ──
// Two windows inside PrimeTime's I/O block: byte registers at +$2A000 and
// a u32 RAMDAC at +$24000 whose payload rides the TOP byte. No holding
// register games (that was MEMCjr's 12-bit DAFB port) — F108 passes the
// bus straight through.

uint8_t Q630Memory::ioRead8(uint32_t addr) {
    uint32_t sub = addr & 0x0FFFFFFF;

    if (onIoAccess) onIoAccess(addr, false, 0xFFFFFFFF);   // pre-access probe log

    // MAME (iosb.cpp:64-65, inherited by primetimeii) installs $A55A2BAD
    // across the whole $0FFF0000-$0FFFFFFF window and the machine driver
    // overrides only the final longword. Answering just the last longword left
    // 64 KB reading 0 = "no IOSB/PrimeTime present". CentrisMemory already
    // decodes the full window; this is the same family.
    if (sub >= 0x0FFF0000) {
        uint32_t id = (sub >= 0x0FFFFFFC) ? machineId_ : 0xA55A2BADu;
        return uint8_t(id >> (8 * (3 - (sub & 3))));
    }

    uint32_t base = sub & 0x0003FFFF;            // pre-mirror window

    if (base < 0x02000) return viaAccess8(base, false, 0);
    if ((sub & ~0xF00000u) >= 0x02000 && (sub & ~0xF00000u) < 0x04000)
        return via2Access8(sub & 0x1FFF, false, 0);
    if (base >= 0x0C000 && base < 0x0E000) {     // SCC, byte on D8-15
        flushScc();
        int ch = (base >> 1) & 1;
        uint8_t d = ((base >> 2) & 1) ? scc_.readData(ch) : scc_.readCtl(ch);
        sccIrqLine(scc_.irqAsserted());          // reading RR0 / data can clear
        return d;                                // or (re-)assert the level-4 line
    }
    if (base >= 0x10000 && base < 0x10100) {     // TurboSCSI 53C96 regs — Q6
        flushScsi();
        // reg select = (addr>>4)&0xF (iosb.cpp:58-59 turboscsi_r reads
        // m_ncr->read(offset>>4)); absolute reg N at PrimeTime+$10000+N*$10.
        // Every register access costs 3 CPU cycles (iosb.cpp:486 turboscsi_r
        // adjust_icount, step 9 wait-state cell).
        if (cpu_) cpu_->stall(scsiReadCycles_);
        uint8_t d = scsi_.read((base >> 4) & 0xF);
        scsiPoll_();
        return d;
    }
    if (base >= 0x10100 && base < 0x10104) {     // TurboSCSI pseudo-DMA — Q6
        // The waitstated alias (byte-address bit 19 — MAME iosb.cpp:507
        // BIT(offset<<1,18) under .select(0xfc0000)) inserts the IOSB-reg-2
        // programmed wait states; the plain window does not.
        if (cpu_ && ((sub >> 19) & 1)) cpu_->stall(scsiDmaReadCycles_);
        return scsiDmaRead_();
    }
    if ((sub & ~0xF00000u) >= 0x14000 && (sub & ~0xF00000u) < 0x15000)
        return asc_.read(addr & 0xFFF);          // PrimeTime/IOSB ASC ($BB)
    if ((sub & ~0xF00000u) >= 0x18000 && (sub & ~0xF00000u) < 0x1A000) {
        uint32_t reg = ((sub & 0x1FFF) >> 8) & 0x1F;
        uint32_t byteInWord = sub & 1;
        return uint8_t(iosbRegs_[reg] >> (8 * (1 - byteInWord)));
    }
    if ((sub & ~0xF00000u) >= 0x1A000 && (sub & ~0xF00000u) < 0x1A100) {
        // F108 ATA/IDE port (f108.cpp:38-41). No drive is modelled, so the
        // bus reads back 0 — the ROM's IDE probe then finds no device and
        // falls through to SCSI, which is where POM68K's disks live.
        return 0;
    }
    if ((sub & ~0xF00000u) >= 0x1A100 && (sub & ~0xF00000u) < 0x1A110) {
        // PrimeTime II special interrupt status (iosb.cpp:662-675):
        // bit 6 = VBL IRQ (nubus bit 6, active low), bit 5 = ATA IRQ.
        uint8_t st = uint8_t(((nubusIrqs_ ^ 0xFF) & 0x40) | (ataIrq_ ? 0x20 : 0));
        return ((sub & ~0xF00000u) == 0x1A101) ? st : 0;
    }
    if ((sub & ~0xF00000u) >= 0x24000 && (sub & ~0xF00000u) < 0x26000) {
        // readRamdac32 mutates state (palIdx_ advances), so it must fire once
        // per access, not once per byte lane — a 16-bit read desynced the
        // R/G/B read phase for good. The write path already guards this way.
        if ((sub & 3) != 0) return 0;
        uint32_t v = video_.readRamdac32(sub & 0x1FFF);   // payload in bits 31-24
        return uint8_t(v >> 24);
    }
    if ((sub & ~0xF00000u) >= 0x2A000 && (sub & ~0xF00000u) < 0x2C000)
        return video_.readReg8(sub & 0x1FFF);

    if ((sub & ~0xF00000u) >= 0x1E000 && (sub & ~0xF00000u) < 0x20000) {
        if (cpu_) cpu_->stall(5);                 // iosb.cpp swim_r wait states
        syncSwimFromCpu();                        // MAME swim2::read → sync()
        return (addr & 1) ? 0 : swim_.read((sub >> 9) & 0x0F);
    }

    // Unmapped PrimeTime/IOSB I/O reads back 0, not /BERR: MAME's iosb_base::map
    // (iosb.cpp:54-65) installs only VIA1/VIA2/TurboSCSI/ASC/iosb_regs/SWIM/ID
    // and has no catch-all, so the space's unmap value is 0 (same rule as the
    // Sonora map — see SonoraMemory.cpp). The desktop LC 475 / Quadra 605 path
    // tolerates a /BERR here, but the all-in-one LC 575 ($A55A222E) ProductInfo
    // probes an un-emulated window and a /BERR drops it into the ROM serial
    // debugger; reading 0 lets the probe fall through, as on real hardware.
    return 0x00;
}

void Q630Memory::ioWrite8(uint32_t addr, uint8_t v) {
    uint32_t sub = addr & 0x0FFFFFFF;

    if (onIoAccess) onIoAccess(addr, true, v);

    if (sub >= 0x0FFFFFFC) return;               // ID is read-only

    uint32_t base = sub & 0x0003FFFF;

    if (base < 0x02000) { viaAccess8(base, true, v); return; }
    if ((sub & ~0xF00000u) >= 0x02000 && (sub & ~0xF00000u) < 0x04000) {
        via2Access8(sub & 0x1FFF, true, v);
        return;
    }
    if (base >= 0x0C000 && base < 0x0E000) {
        flushScc();
        int ch = (base >> 1) & 1;
        if ((base >> 2) & 1) scc_.writeData(ch, v);
        else scc_.writeCtl(ch, v);
        sccIrqLine(scc_.irqAsserted());          // WR1/WR9/WR15 + Reset Ext/Status
        return;                                  // change the pending interrupt
    }
    if (base >= 0x10000 && base < 0x10100) {     // TurboSCSI 53C96 regs — Q6
        flushScsi();
        if (cpu_) cpu_->stall(scsiWriteCycles_); // iosb.cpp:494 turboscsi_w
        scsi_.write((base >> 4) & 0xF, v);
        scsiPoll_();
        return;
    }
    if (base >= 0x10100 && base < 0x10104) {     // TurboSCSI pseudo-DMA — Q6
        // Waitstated alias — see ioRead8; write side iosb.cpp:552.
        if (cpu_ && ((sub >> 19) & 1)) cpu_->stall(scsiDmaWriteCycles_);
        scsiDmaWrite_(v);
        return;
    }
    if ((sub & ~0xF00000u) >= 0x14000 && (sub & ~0xF00000u) < 0x15000) {
        asc_.write(addr & 0xFFF, v);                 // PrimeTime/IOSB ASC
        return;
    }
    if ((sub & ~0xF00000u) >= 0x18000 && (sub & ~0xF00000u) < 0x1A000) {
        uint32_t reg = ((sub & 0x1FFF) >> 8) & 0x1F;
        if (sub & 1) iosbRegs_[reg] = uint16_t((iosbRegs_[reg] & 0xFF00) | v);
        else         iosbRegs_[reg] = uint16_t((iosbRegs_[reg] & 0x00FF) | (v << 8));
        if (reg == 2) {
            // IOSB reg 2 programs the pseudo-DMA wait states (iosb.cpp:610-617
            // iosb_regs_w): bits 8-9 → DMA read, bits 11-12 → DMA write,
            // through times[4] = {5,5,4,3} CPU cycles per access.
            static constexpr int times[4] = { 5, 5, 4, 3 };
            scsiDmaReadCycles_  = times[(iosbRegs_[2] >> 8) & 3];
            scsiDmaWriteCycles_ = times[(iosbRegs_[2] >> 11) & 3];
        }
        return;
    }
    if ((sub & ~0xF00000u) >= 0x1E000 && (sub & ~0xF00000u) < 0x20000) {
        if (cpu_) cpu_->stall(5);                 // iosb.cpp swim_w wait states
        syncSwimFromCpu();                        // MAME swim2::write → sync()
        swim_.write((sub >> 9) & 0x0F, v);
        return;
    }
    if ((sub & ~0xF00000u) >= 0x1A000 && (sub & ~0xF00000u) < 0x1A110)
        return;                                   // ATA port: no drive
    if ((sub & ~0xF00000u) >= 0x24000 && (sub & ~0xF00000u) < 0x26000) {
        // RAMDAC: the payload is the TOP byte of the u32 (valkyrie.cpp
        // ramdac_w does `data >>= 24`), so commit on lane 0 only.
        if ((sub & 3) == 0) video_.writeRamdac32(sub & 0x1FFF, uint32_t(v) << 24);
        return;
    }
    if ((sub & ~0xF00000u) >= 0x2A000 && (sub & ~0xF00000u) < 0x2C000) {
        video_.writeReg8(sub & 0x1FFF, v);
        return;
    }

    // Unmapped PrimeTime/IOSB I/O writes are ignored, not /BERR (MAME
    // iosb.cpp:54-65 — no catch-all handler); symmetric with ioRead8.
    return;
}

// ── TurboSCSI pseudo-DMA (PrimeTime + $10100) ──
// On a CPU access to the DMA window the PrimeTime/IOSB holds off /DTACK
// while !DRQ (MAME iosb.cpp:498-591 turboscsi_dma_r/w spin on the DRQ line).
// Our 53C96 changes DRQ only on CPU-driven accesses — nothing can assert it
// while the CPU is held — so a !DRQ access can never be released and the
// observable equivalent of the eventual bus timeout is an immediate /BERR,
// which the SCSI Manager's blind-transfer loops catch to terminate (the LC II
// V8 path behaves the same). With a proper transfer count the 53C96 keeps
// DRQ asserted until the payload is drained. The waitstated-alias cycle
// costs are charged at the ioRead8/ioWrite8 call sites (step 9 cell).
// macquadra605.cpp:206 drq_handler -> primetime scsi_drq_w -> via2.
uint8_t Q630Memory::scsiDmaRead_() {
    flushScsi();
    if (!scsi_.drq()) busError(0x50010100, false);
    uint8_t d = scsi_.dmaRead();
    scsiPoll_();
    return d;
}

void Q630Memory::scsiDmaWrite_(uint8_t v) {
    flushScsi();
    if (!scsi_.drq()) busError(0x50010100, true);
    scsi_.dmaWrite(v);
    scsiPoll_();
}

// Level-sensitive IRQ/DRQ from the 53C96 into the Quadra pseudo-VIA2:
// macquadra605.cpp:204-206 wires ncr1->irq_handler_cb -> primetime
// scsi_irq_w -> via2 (pseudovia.cpp:148); IntStatus-read clears the IRQ.
void Q630Memory::scsiPoll_() {
    scsiIrq(scsi_.irq());
}

// POM68K JIT: the address map itself moved (overlay flip, ROM reload).
// No byte was written, so the write guard cannot see it — say so directly.
void Q630Memory::jitMapChanged() {
    if (jitGuard_) jitGuard_->invalidate();
    // J3: the interpreter's data window bypasses this map entirely, so a
    // remap the MMU cannot see (the boot overlay flip writes no ATC) has
    // to reach the CPU's DTLB directly — the guard above is only serviced
    // by the JIT engine's own loop, which the interpreter never enters.
    if (cpu_) cpu_->pomJitDtlbFlush();
}

const uint8_t* Q630Memory::codeSpan(uint32_t phys, uint32_t& len) const {
    // Exactly two regions may be handed out, and only with the overlay down.
    len = 0;
    if (phys < 0x40000000) {
        // Under the overlay this gigabyte is the ROM mirrored modulo 1 MB,
        // and the mapping flips out from under the caller the moment
        // read8/read16 touches $40000000. Refuse until it has flipped: the
        // boot ROM then runs interpreted, which costs about a second once.
        if (overlay_) return nullptr;
        // RAM is FLAT here, not mirrored — above the bank read8 returns
        // open bus 0xFF, which is not memory and must never be a span.
        if (phys >= totalRam_) return nullptr;
        len = totalRam_ - phys;
        return ram_.data() + phys;
    }
    if (phys < 0x50000000) {
        // ROM window. Reading it clears the overlay (read8), a side effect
        // a const probe cannot perform — so refuse while it is still set.
        if (overlay_) return nullptr;
        const uint32_t o = phys & (kRomSize - 1);
        len = kRomSize - o;              // stop at the 1 MB mirror seam
        return rom_.data() + o;
    }
    // $50000000-$5FFFFFFF I/O (VIA IFR clears, SCC RR0, 53C96 registers and
    // pseudo-DMA pops that can throw), $F9xxxxxx VRAM and the video cell
    // registers (reads clear interrupts and auto-increment the RAMDAC), and
    // every unmapped address (those bus-error). None of them is code.
    return nullptr;
}

uint8_t* Q630Memory::dataSpan(uint32_t phys, uint32_t& len, bool write) {
    len = 0;
    if (phys < 0x40000000) {
        if (overlay_) return nullptr;          // this gigabyte is ROM for now
        if (phys >= totalRam_) return nullptr; // open bus, not memory
        // The write-watch hook has to see every store that reaches RAM.
        if (write && ramWatch_ && onRamWrite) return nullptr;
        len = totalRam_ - phys;
        return ram_.data() + phys;
    }
    if (phys < 0x50000000) {
        // The ROM window: readable, and a store there is dropped by the
        // hardware rather than stored, which an inline store cannot model.
        if (write || overlay_) return nullptr;
        const uint32_t o = phys & (kRomSize - 1);
        len = kRomSize - o;
        return rom_.data() + o;
    }
    // The framebuffer. QuickDraw drawing a 640x480x8 desktop is a very
    // large number of stores, and leaving them on the slow path made the
    // code generator SLOWER than the interpreter for the whole Finder
    // phase — each store paid a TLB probe, a remembered refusal and a call,
    // to reach what is a plain array write. read8/read16/write8/write16 all
    // treat this window as bytes and nothing else: no latch, no
    // auto-increment, no dirty tracking. (The video CELL registers at
    // $F98000xx are a different window and stay out.)
    //
    // The offset is taken modulo the framebuffer size and the span stops at
    // that seam, because two of the four maps mirror this window.
    if (phys >= 0xF9000000 && phys < 0xF9000000 + kVramSize) {
        const uint32_t o = (phys - 0xF9000000) & (kVramSize - 1);
        len = kVramSize - o;
        return vram_.data() + o;
    }

    // I/O and the video cell registers stay on the slow path: reads there
    // latch and auto-increment.
    return nullptr;
}

uint8_t Q630Memory::read8(uint32_t addr) {
    if (addr < 0x40000000) {
        if (overlay_) return rom_[addr & (kRomSize - 1)];
        if (addr < totalRam_) return ram_[addr];
        return 0xFF;        // open bus above the installed bank(s): the
                            // sizer must find NO alias there or it
                            // invents phantom banks (5 x mirror) whose
                            // burn-in self-corrupts
    }
    if (addr < 0x50000000) {                     // ROM window
        if (overlay_) { overlay_ = false; jitMapChanged(); }          // djmemc rom_switch_r
        return rom_[addr & (kRomSize - 1)];
    }
    if (addr < 0x60000000) return ioRead8(addr);
    if (addr >= 0xF9000000 && addr < 0xF9000000 + kVramSize)
        return vram_[addr - 0xF9000000];
    busError(addr, false);
}

uint16_t Q630Memory::read16(uint32_t addr) {
    // RAM/ROM fast paths (aligned by Moira's splitting)
    if (addr < 0x40000000) {
        if (overlay_) {
            uint32_t o = addr & (kRomSize - 1);
            return uint16_t(rom_[o & (kRomSize - 1)] << 8 | rom_[(o + 1) & (kRomSize - 1)]);
        }
        if (addr + 1 < totalRam_)
            return uint16_t(ram_[addr] << 8 | ram_[addr + 1]);
        return 0xFFFF;
    }
    if (addr < 0x50000000) {
        if (overlay_) { overlay_ = false; jitMapChanged(); }
        uint32_t o = addr & (kRomSize - 1);
        return uint16_t(rom_[o & (kRomSize - 1)] << 8 | rom_[(o + 1) & (kRomSize - 1)]);
    }
    if (addr >= 0xF9000000 && addr + 1 < 0xF9000000 + kVramSize) {
        uint32_t o = addr - 0xF9000000;
        return uint16_t(vram_[o] << 8 | vram_[o + 1]);
    }
    if (addr >= 0x50000000 && addr < 0x60000000) {
        uint32_t swimOff = (addr & 0x0FFFFFFF) & ~0xF00000u;
        if (swimOff >= 0x1E000 && swimOff < 0x20000)
            return uint16_t(read8(addr) << 8);    // IOSB result on D15-D8
    }
    return uint16_t(read8(addr) << 8 | read8(addr + 1));
}

void Q630Memory::write8(uint32_t addr, uint8_t v) {
    if (addr < 0x40000000) {
        if (overlay_) return;                    // ROM mirror: writes drop
        if (ramWatch_ && onRamWrite && addr == ramWatch_) onRamWrite(addr, 1, v);
        if (jitGuard_) jitGuard_->note(addr, 1);
        if (addr < totalRam_) ram_[addr] = v;
        return;
    }
    if (addr < 0x50000000) return;               // ROM window (nopw)
    if (addr < 0x60000000) { ioWrite8(addr, v); return; }
    if (addr >= 0xF9000000 && addr < 0xF9000000 + kVramSize) {
        vram_[addr - 0xF9000000] = v;
        return;
    }
    busError(addr, true);
}

void Q630Memory::write16(uint32_t addr, uint16_t v) {
    if (addr < 0x40000000 && !overlay_) {
        if (ramWatch_ && onRamWrite && (addr == ramWatch_ || addr + 1 == ramWatch_))
            onRamWrite(addr, 2, v);
        if (addr + 1 < totalRam_) {
            if (jitGuard_) jitGuard_->note(addr, 2);
            ram_[addr] = uint8_t(v >> 8);
            ram_[addr + 1] = uint8_t(v);
        }
        return;
    }
    if (addr >= 0xF9000000 && addr + 1 < 0xF9000000 + kVramSize) {
        uint32_t o = addr - 0xF9000000;
        vram_[o] = uint8_t(v >> 8);
        vram_[o + 1] = uint8_t(v);
        return;
    }
    if (addr >= 0x50000000 && addr < 0x60000000) {
        uint32_t swimOff = (addr & 0x0FFFFFFF) & ~0xF00000u;
        if (swimOff >= 0x1E000 && swimOff < 0x20000) {
            write8(addr + 1, uint8_t(v));         // IOSB accepts low byte
            return;
        }
    }
    write8(addr, uint8_t(v >> 8));
    write8(addr + 1, uint8_t(v));
}

uint8_t Q630Memory::peek8(uint32_t addr) const {
    if (addr < 0x40000000) {
        if (overlay_) return rom_[addr & (kRomSize - 1)];
        if (addr < totalRam_) return ram_[addr];
        return 0xFF;
    }
    if (addr < 0x50000000) return rom_[addr & (kRomSize - 1)];
    if (addr >= 0xF9000000 && addr < 0xF9000000 + kVramSize)
        return vram_[addr - 0xF9000000];
    if (addr >= 0x5FFFFFFC && addr < 0x60000000)                   // board ID (mirrors read8)
        return uint8_t(machineId_ >> (8 * (3 - (addr & 3))));
    return 0xFF;
}

void Q630Memory::tick(int cpuCycles) {
    // VIA1 φ2 = 783.36 kHz, fixed (iosb.cpp:74) — see viaSync above for why
    // the divider tracks kCpuHz instead of being the Q605's constant 32.
    // VIA1 φ2 is the board's fixed 783.36 kHz E clock, not a divisor of
    // the CPU — an integer ratio is an approximation here (ViaEClock.h).
    const int viaCycles = viaEClock_.advance(cpuCycles, kCpuHz);
    if (viaCycles && via1_.tick(viaCycles)) updateIrq();

    if (cudaLleOn_) cudaLle_.tick(cpuCycles);
    else            cuda_.tick(cpuCycles);

    // SCC open-line Break/Abort stream (Q6.6): on a BARE line (no peer) the
    // SDLC receiver keeps detecting aborts, so the level-4 ext/status
    // interrupt re-asserts periodically — the carrier-sense signal OS 8.1's
    // .MPP LAP sleeps on. A real LToUDP peer transmitting drops the stream
    // (Scc8530::openLine, LLE step 8). A de-asserted SCC must also lower the
    // line.
    sccDebt_ += cpuCycles;
    if (scc_.cyclesToNextEvent() <= sccDebt_) flushScc();

    // IOSB ASC and SWIM2 run on C15M (15.6672 MHz),
    // independent of the 25 MHz CPU — convert cpuCycles into ASC-clock ticks
    // via a fractional accumulator so the 22 257 Hz sample rate stays exact.
    ascCycAcc_ += int64_t(cpuCycles) * AscIosb::kCpuHz;
    int ascCyc = int(ascCycAcc_ / kCpuHz);
    ascCycAcc_ -= int64_t(ascCyc) * kCpuHz;
    asc_.tick(ascCyc);
    syncSwimFromCpu();
    drive0_.tick(cpuCycles);
    drive1_.tick(cpuCycles);

    // SCSI bus-service latency countdown (Q6.5b) → reflect the deferred IRQ
    // into the pseudo-VIA2 line when it lands.
    if (scsi_.irq() != ((pvIfr_ & 0x08) != 0)) scsiPoll_();
    scsiDebt_ += cpuCycles;
    if (scsi_.cyclesToNextEvent() <= scsiDebt_) flushScsi();

    // 60.15 Hz CA1 tick (iosb 6015_timer)
    tickAcc_ += int64_t(cpuCycles) * 6015;
    if (tickAcc_ >= kCpuHz * 100) {
        tickAcc_ -= kCpuHz * 100;
        via1_.raiseCa1();
        updateIrq();
    }

    // Valkyrie frame clock (VBL interrupt) — Valkyrie::tick.
    video_.tick(cpuCycles);
}

int Q630Memory::cyclesToNextEvent() const {
    int best = cudaLleOn_ ? cudaLle_.cyclesToNextEvent() : 1;
    best = std::min(best, viaEClock_.cyclesToNext(kCpuHz));
    auto debtBound = [](int next, int64_t debt) {
        if (next == 0x7fffffff) return next;
        return int(std::max<int64_t>(1, int64_t(next) - debt));
    };
    best = std::min(best, debtBound(scc_.cyclesToNextEvent(), sccDebt_));
    best = std::min(best, debtBound(scsi_.cyclesToNextEvent(), scsiDebt_));
    auto bridge = [](int deviceCycles, int64_t acc) {
        if (deviceCycles == 0x7fffffff) return deviceCycles;
        const int64_t need = int64_t(deviceCycles) * kCpuHz - acc;
        if (need <= 0) return 1;
        return int((need + AscIosb::kCpuHz - 1) / AscIosb::kCpuHz);
    };
    best = std::min(best, bridge(asc_.cyclesToNextEvent(), ascCycAcc_));
    best = std::min(best, bridge(swim_.cyclesToNextEvent(), swimCycAcc_));
    best = std::min(best, int(std::max<int64_t>(1,
        (kCpuHz * 100 - tickAcc_ + 6014) / 6015)));
    best = std::min(best, video_.cyclesToNextEvent());
    return std::max(best, 1);
}

void Q630Memory::flushScc() {
    while (sccDebt_ > 0) {
        const int step = int(std::min<int64_t>(sccDebt_, 0x7fffffff));
        sccDebt_ -= step;
        scc_.tick(step);
    }
    if (sccIrq_ != scc_.irqAsserted()) {
        sccIrq_ = scc_.irqAsserted();
        updateIrq();
    }
}

void Q630Memory::flushScsi() {
    while (scsiDebt_ > 0) {
        const int step = int(std::min<int64_t>(scsiDebt_, 0x7fffffff));
        scsiDebt_ -= step;
        scsi_.tick(step);
    }
    if (scsi_.irq() != ((pvIfr_ & 0x08) != 0)) scsiPoll_();
}
