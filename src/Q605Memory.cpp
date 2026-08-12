// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Q605Memory.h"
#include "LleSession.h"
#include "SaveState.h"
#include "Cpu040.h"
#include "Moira.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

Q605Memory::Q605Memory(uint32_t totalRam)
    : totalRam_(totalRam)
{
    if (const char* e = std::getenv("POM68K_Q605_EVENT_SCC"))
        sccEventDriven_ = e[0] != '0';
    if (const char* e = std::getenv("POM68K_Q605_EVENT_SCSI"))
        scsiEventDriven_ = e[0] != '0';
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
    // DAFB interrupt summary → MEMCjr via2_irq_w<0x40> (nubus bit 6).
    dafbCell_.onIrq = [this](bool s) { vblIrq(s); };
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
    // DaynaPort SCSI/Link — Ethernet as a SCSI target, opt-in and OFF by
    // default: a new device answering selection changes what the ROM's bus
    // probe finds, and every boot etalon is calibrated against a bus with
    // only disks on it. POM68K_DAYNAPORT=<id> puts the card at that ID
    // (=1 means "pick the default", ID 3 — where MAME parks the CD-ROM, so
    // choose another if a disc is mounted). `=0` and empty are off.
    // The knob is boolean-shaped at its edges, so `=1` reads as "on, you
    // choose" and NOT as "at ID 1": ID 1 is unreachable through this knob,
    // which is the price of `=1` meaning what everyone types it to mean.
    // Until 2026-08-12 the guard was `id < 1`, so atoi("1") passed straight
    // through and the card landed at ID 1 — the one behaviour no document
    // ever described. (2026-08-12)
    // The card is on the bus regardless of AppleTalk; what it is WIRED to is
    // the in-process NAT, which lives in AtalkHub (see AtalkHub::attach).
    // With POM68K_APPLETALK=0 the guest still sees the card and it carries
    // nothing — a cable-unplugged state, not a missing device.
    if (const char* e = std::getenv("POM68K_DAYNAPORT"); e && e[0] && e[0] != '0') {
        int id = std::atoi(e);
        if (id <= 1 || id > 6) id = 3;
        dayna_.attach();
        scsi_.attach(&dayna_, id);
        std::fprintf(stderr, "DaynaPort SCSI/Link at SCSI ID %d "
                     "(guest needs the SCSI/Link driver + a manual MacTCP "
                     "address in the gateway's subnet)\n", id);
    }
    if (const char* id = std::getenv("POM68K_Q605_ID"))
        machineId_ = uint32_t(std::strtoul(id, nullptr, 16));
    // Cuda firmware LLE — the DEFAULT whenever the real dump is present
    // (blueprint step 4, the POM68K_ADB_LLE rollout pattern);
    // POM68K_CUDA_LLE=0 forces the Egret HLE, a missing dump falls back
    // silently. The staged PRAM mirrors the Egret HLE's factory seed so
    // both paths boot from the same battery contents.
    {
        const char* e = std::getenv("POM68K_CUDA_LLE");
        const bool want = !e || std::atoi(e) != 0;
        if (want) {
            for (const char* p : { "roms/cuda/341s0788.bin",
                                   "../roms/cuda/341s0788.bin" }) {
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
                std::fprintf(stderr, "Q605: no roms/cuda/341s0788.bin — "
                             "running the NON-CONFORMANT HLE ADB substitute "
                             "(docs/LLE_VS_HLE.md §2)\n");
        } else {
            std::fprintf(stderr, "Q605: POM68K_CUDA_LLE=0 — NON-CONFORMANT "
                         "HLE ADB substitute forced\n");
        }
        if (!cudaLleOn_) pom68k::lle::activateHle(pom68k::lle::HleEgretCuda);
    }
}

bool Q605Memory::loadRom(const std::vector<uint8_t>& data) {
    if (data.size() != kRomSize) return false;
    std::memcpy(rom_.data(), data.data(), kRomSize);
    jitMapChanged();
    return true;
}


void Q605Memory::reset() {
    overlay_ = true;
    jitMapChanged();
    pvIfr_ = pvIer_ = pvPortB_ = 0;
    nubusIrqs_ = 0xFF;
    std::memset(memcjr_, 0, sizeof memcjr_);
    std::memset(iosbRegs_, 0, sizeof iosbRegs_);
    dafbHolding_ = 0;
    dafbCell_.reset();
    via1_.reset();
    cuda_.reset();
    cudaLle_.reset();
    scc_.reset();
    sccDebt_ = 0;
    scsi_.reset();
    scsiDebt_ = 0;
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
    scc_.setClocks(kCpuHz, 7833600);   // SCC async-baud LLE: SCC85C30 @ C7M
                                       // (macquadra605.cpp:171)
    scc_.setCtsHigh(false);        // no serial debugger attached (POST check)
    scc_.setAbortIdle(true);       // no *hardwired* LocalTalk peer. The abort
                                   // is a LINE state, not machine config: a
                                   // virgin line reads clean (OT binds .MPP —
                                   // it spins on RR0 bit 7 before bind, the
                                   // §1.10 wedge), the standing abort exists
                                   // once the line has carried a frame, and a
                                   // live peer suppresses it —
                                   // Scc8530::openLine (LLE steps 7+8).
    viaEClock_ = {};
    tickAcc_ = 0;
    sccIrq_ = false;
}

void Q605Memory::busError(uint32_t addr, bool write) const {
    if (onBusError) onBusError(addr, write);
    if (cpu_) cpu_->extBusError040();
    throw moira::MmuBusError{};
}

int Q605Memory::iplLevel() const {
    if (sccIrq_) return 4;                   // iosb field_interrupts
    if (via2IrqAsserted()) return 2;
    if (via1_.irqAsserted()) return 1;
    return 0;
}

void Q605Memory::updateIrq() {
    if (cpu_) cpu_->updateIpl();
}

void Q605Memory::via2Recalc() {
    if (ascLine_) pvIfr_ |= 0x10;
    if (via2IrqAsserted()) pvIfr_ |= 0x80; else pvIfr_ &= ~0x80;
    updateIrq();
}

void Q605Memory::vblIrq(bool s) {
    // MEMCjr video IRQ -> IOSB via2_irq_w<0x40>: nubus bit 6 (active
    // low) + the pseudo-VIA slot-summary IFR bit 1
    if (s) nubusIrqs_ &= ~0x40; else nubusIrqs_ |= 0x40;
    if ((nubusIrqs_ & 0x79) != 0x79) pvIfr_ |= 0x02;
    else                             pvIfr_ &= ~0x02;
    via2Recalc();
}

void Q605Memory::scsiIrq(bool s) {
    if (s) pvIfr_ |= 0x08;                   // CB2 bit (pseudovia.cpp:148)
    else   pvIfr_ &= ~0x08;
    via2Recalc();
}

// MAME swim2_device::read/write begin with sync(). Drive SWIM from the CPU
// clock so sync-on-access and the batched machine tick share one timeline.
// Moira's clock runs cacheBoost_× ahead of machine time; convert deltas
// before the C15M ratio so SWIM stays locked to VIA/ASC/SCSI under boost.
void Q605Memory::syncSwimFromCpu() {
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

void Q605Memory::ascIrq(bool s) {
    ascLine_ = s;
    if (s) pvIfr_ |= 0x10;                   // EASC bit 4 (IFR/IER mask $1B)
    else   pvIfr_ &= ~0x10;
    via2Recalc();
}

// VIA1 E-clock sync (iosb via_sync, same arithmetic as the LC II):
// cpuClk/viaClk = 25 MHz / 783.36 kHz ≈ 31.91 — use the same integer
// scheme with a 32:1 approximation. Work in machine-cycle space so the
// alignment is invariant under POM68K_Q605_CACHE_BOOST.
void Q605Memory::viaSync() {
    if (!cpu_) return;
    const int boost = std::max(1, cpu_->cacheBoost());
    int64_t c = int64_t(cpu_->getClock()) / boost;
    const int64_t target = via_eclock::syncTarget(c, kCpuHz);
    if (target > c) cpu_->stall(int(target - c));
}

uint8_t Q605Memory::viaAccess8(uint32_t addr, bool write, uint8_t v) {
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

uint8_t Q605Memory::via2Access8(uint32_t addr, bool write, uint8_t v) {
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

// ── DAFB HLE (MEMCjr integrated cell) — MAME dafb.cpp ──
// Map (dafb_base::map): +$000 main regs, +$100 Swatch, +$200 RAMDAC
// (Antelope = revised AC842a), +$300 clockgen (Gazelle, reads 0).
// Registers are ≤12 bits, accessed as u32; unknown offsets fall back
// to the raw echo file.

uint32_t Q605Memory::dafbRegRead(uint32_t off) {
    uint32_t full = dafbCell_.read32(off);
    // MEMCjr DAFB bus-holding split (djmemc.cpp:149-165, dafb_holding_r):
    // the $F9800000-$F98001FF window ($000 main + $100 Swatch) is a 12-bit
    // port — a read returns only the low-6 bits and latches the high-6, which
    // the ROM reads back at $50F0E07C (memcjr_r $7C → `holding>>6`) to rebuild
    // the 12-bit value. We keep `dafbHolding_` in bits [11:6] throughout (the
    // memcjr_w/r + DAFB-write convention, djmemc.cpp:172/186/197); MAME's
    // dafb_holding_r stashes it unshifted, an internal asymmetry that would
    // make the high-half read-back return 0 — but this ROM DOES read the high
    // half back after a register read, so we store it shifted so memcjr_r's
    // `>>6` recovers it. Side-effect reads already ran in Dafb::read32 above.
    if ((off & 0x3FC) < 0x200) {
        dafbHolding_ = uint16_t(((full >> 6) & 0x3f) << 6);
        return full & 0x3f;
    }
    return full;
}

void Q605Memory::dafbRegWrite(uint32_t off, uint32_t v) {
    // MEMCjr DAFB bus-holding split (djmemc.cpp:167-178, dafb_holding_w):
    // the $F9800000-$F98001FF window ($000 main + $100 Swatch) is a
    // 12-bit port whose write value is `(this write & 0x3f) | holding`,
    // the high-6 half having been latched by a prior $50F0E07C write;
    // the latch then clears. Outside that window (RAMDAC $200+) the DAFB
    // is written whole.
    if ((off & 0x3FC) < 0x200) {
        v = (v & 0x3f) | uint32_t(dafbHolding_);
        dafbHolding_ = 0;
        v &= 0xFFF;                              // 12-bit DAFB register port
    }
    dafbCell_.write32(off, v);
}

uint8_t Q605Memory::dafbRead8(uint32_t addr) {
    // Byte lanes of the u32 register; side-effectful reads (int clears,
    // pal index) repeat within one long access but are idempotent —
    // except the CLUT data register, served on the low lane only.
    if (onIoAccess) onIoAccess(0xF9800000 + (addr & 0x3FF), false, 0xFFFFFFFF);
    if ((addr & 0x3FC) == 0x210 && (addr & 3) != 3) return 0;
    uint32_t val = dafbRegRead(addr & ~3u);
    return uint8_t(val >> (8 * (3 - (addr & 3))));
}

void Q605Memory::dafbWrite8(uint32_t addr, uint8_t v) {
    // Registers are ≤12 bits: commit semantics once, on the low lane.
    if (onIoAccess) onIoAccess(0xF9800000 + (addr & 0x3FF), true, v);
    if ((addr & 0x300) == 0x300) {               // Gazelle: true byte port
        dafbCell_.clockgenWrite8(addr & 0x3FF, v);
        return;
    }
    int sh = 8 * (3 - (addr & 3));
    uint32_t merged = (dafbCell_.rawReg(addr) & ~(0xFFu << sh))
                    | (uint32_t(v) << sh);
    if ((addr & 3) == 3) dafbRegWrite(addr & ~3u, merged);
    else                 dafbCell_.setRawReg(addr, merged);
}

uint8_t Q605Memory::ioRead8(uint32_t addr) {
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
    if (base >= 0x0E000 && base < 0x10000) {     // MEMCjr regs
        // MAME memcjr_r (djmemc.cpp:181-190): every register reads back 0
        // except $7C, the DAFB bus-holding register, which returns the
        // latched high-6 bits (`m_dafb_holding >> 6`). The high-6 half of
        // a 12-bit DAFB register is transferred through this latch; the
        // low-6 half goes directly at $F9800000 (dafb_holding_r/w).
        uint32_t idx = (base & 0x7F) >> 2;
        if (idx == (0x7C >> 2)) {
            uint32_t hi = uint32_t(dafbHolding_) >> 6;   // 6 bits
            return uint8_t(hi >> (8 * (3 - (base & 3))));
        }
        return 0;
    }
    if (base >= 0x10000 && base < 0x10100) {     // TurboSCSI 53C96 regs — Q6
        // reg select = (addr>>4)&0xF (iosb.cpp:58-59 turboscsi_r reads
        // m_ncr->read(offset>>4)); absolute reg N at PrimeTime+$10000+N*$10.
        // Every register access costs 3 CPU cycles (iosb.cpp:486 turboscsi_r
        // adjust_icount, step 9 wait-state cell).
        if (cpu_) cpu_->stall(scsiReadCycles_);
        flushScsi();
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
    if ((sub & ~0xF00000u) >= 0x1A100 && (sub & ~0xF00000u) < 0x1A110)
        return 0;   // PrimeTime II ATA/status window — ROM probes $1A101

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

void Q605Memory::ioWrite8(uint32_t addr, uint8_t v) {
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
    if (base >= 0x0E000 && base < 0x10000) {
        uint32_t idx = (base & 0x7F) >> 2;
        int sh = 8 * (3 - (base & 3));
        memcjr_[idx] = (memcjr_[idx] & ~(0xFFu << sh)) | (uint32_t(v) << sh);
        // MAME memcjr_w (djmemc.cpp:192-199): a write to $7C latches the
        // high-6 half of the next DAFB register write —
        // `m_dafb_holding = (data & 0x3f) << 6`. The 12-bit DAFB write at
        // $F9800000 then ORs this latch in (dafb_holding_w). Sample the
        // full u32 written to $7C on its low lane ($7F) so all 4 bytes
        // have landed in memcjr_[].
        if (idx == (0x7C >> 2) && (base & 3) == 3)
            dafbHolding_ = uint16_t((memcjr_[idx] & 0x3f) << 6);
        return;
    }
    if (base >= 0x10000 && base < 0x10100) {     // TurboSCSI 53C96 regs — Q6
        if (cpu_) cpu_->stall(scsiWriteCycles_); // iosb.cpp:494 turboscsi_w
        flushScsi();
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
uint8_t Q605Memory::scsiDmaRead_() {
    flushScsi();
    if (!scsi_.drq()) busError(0x50010100, false);
    uint8_t d = scsi_.dmaRead();
    scsiPoll_();
    return d;
}

void Q605Memory::scsiDmaWrite_(uint8_t v) {
    flushScsi();
    if (!scsi_.drq()) busError(0x50010100, true);
    scsi_.dmaWrite(v);
    scsiPoll_();
}

// Level-sensitive IRQ/DRQ from the 53C96 into the Quadra pseudo-VIA2:
// macquadra605.cpp:204-206 wires ncr1->irq_handler_cb -> primetime
// scsi_irq_w -> via2 (pseudovia.cpp:148); IntStatus-read clears the IRQ.
void Q605Memory::scsiPoll_() {
    scsiIrq(scsi_.irq());
}

// POM68K JIT: the address map itself moved (overlay flip, ROM reload).
// No byte was written, so the write guard cannot see it — say so directly.
void Q605Memory::jitMapChanged() {
    if (jitGuard_) jitGuard_->invalidate();
    // J3: the interpreter's data window bypasses this map entirely, so a
    // remap the MMU cannot see (the boot overlay flip writes no ATC) has
    // to reach the CPU's DTLB directly — the guard above is only serviced
    // by the JIT engine's own loop, which the interpreter never enters.
    if (cpu_) cpu_->pomJitDtlbFlush();
}

const uint8_t* Q605Memory::codeSpan(uint32_t phys, uint32_t& len) const {
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

uint8_t* Q605Memory::dataSpan(uint32_t phys, uint32_t& len, bool write) {
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

uint8_t Q605Memory::read8(uint32_t addr) {
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
    if (addr >= 0xF9800000 && addr < 0xF9800400)
        return dafbRead8(addr - 0xF9800000);
    busError(addr, false);
}

uint16_t Q605Memory::read16(uint32_t addr) {
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

void Q605Memory::write8(uint32_t addr, uint8_t v) {
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
    if (addr >= 0xF9800000 && addr < 0xF9800400) {
        dafbWrite8(addr - 0xF9800000, v);
        return;
    }
    busError(addr, true);
}

void Q605Memory::write16(uint32_t addr, uint16_t v) {
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

uint8_t Q605Memory::peek8(uint32_t addr) const {
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

void Q605Memory::tick(int cpuCycles) {
    // VIA1 φ2 = the board's 783.36 kHz E clock, NOT a divisor of the CPU:
    // 25 MHz / 783 360 = 31.914, and the old /32 ran it 0.27 % slow
    // (ViaEClock.h).
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
    // The SCC is event-driven: carry elapsed machine time until its next
    // architectural transition instead of entering the device at every
    // unrelated CUDA/VIA/SCSI wake-up. MMIO and external wire injection
    // call flushScc(), so no observer can see state older than its access.
    if (sccEventDriven_) {
        sccDebt_ += cpuCycles;
        if (scc_.cyclesToNextEvent() <= sccDebt_) flushScc();
    } else {
        scc_.tick(cpuCycles);
        if (sccIrq_ != scc_.irqAsserted()) {
            sccIrq_ = scc_.irqAsserted();
            updateIrq();
        }
    }

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
    if (scsiEventDriven_) {
        scsiDebt_ += cpuCycles;
        if (scsi_.cyclesToNextEvent() <= scsiDebt_) flushScsi();
    } else {
        scsi_.tick(cpuCycles);
        if (scsi_.irq() != ((pvIfr_ & 0x08) != 0)) scsiPoll_();
    }

    // 60.15 Hz CA1 tick (iosb 6015_timer)
    tickAcc_ += int64_t(cpuCycles) * 6015;
    if (tickAcc_ >= kCpuHz * 100) {
        tickAcc_ -= kCpuHz * 100;
        via1_.raiseCa1();
        updateIrq();
    }

    // DAFB frame clock (VBL/cursor interrupts) — Dafb::tick.
    dafbCell_.tick(cpuCycles);
}

int Q605Memory::cyclesToNextEvent() const {
    // Conservative lower bounds: waking early only costs time; waking late
    // changes emulated time. The factory Cuda normally binds at ~12 cycles.
    int best = cudaLleOn_ ? cudaLle_.cyclesToNextEvent() : 1;
    best = std::min(best, viaEClock_.cyclesToNext(kCpuHz));
    if (sccEventDriven_) {
        const int sccNext = scc_.cyclesToNextEvent();
        if (sccNext != 0x7fffffff) {
            const int64_t left = int64_t(sccNext) - sccDebt_;
            best = std::min(best, int(std::max<int64_t>(left, 1)));
        }
    } else best = std::min(best, scc_.cyclesToNextEvent());
    if (scsiEventDriven_) {
        const int scsiNext = scsi_.cyclesToNextEvent();
        if (scsiNext != 0x7fffffff) {
            const int64_t left = int64_t(scsiNext) - scsiDebt_;
            best = std::min(best, int(std::max<int64_t>(left, 1)));
        }
    } else best = std::min(best, scsi_.cyclesToNextEvent());

    auto bridge = [](int deviceCycles, int64_t acc, int64_t deviceHz,
                     int64_t machineHz) {
        if (deviceCycles == 0x7fffffff) return deviceCycles;
        const int64_t need = int64_t(deviceCycles) * machineHz - acc;
        if (need <= 0) return 1;
        return int((need + deviceHz - 1) / deviceHz);
    };
    best = std::min(best, bridge(asc_.cyclesToNextEvent(), ascCycAcc_,
                                 AscIosb::kCpuHz, kCpuHz));
    best = std::min(best, bridge(swim_.cyclesToNextEvent(), swimCycAcc_,
                                 AscIosb::kCpuHz, kCpuHz));

    const int64_t caNeed = kCpuHz * 100 - tickAcc_;
    best = std::min(best, int((caNeed + 6015 - 1) / 6015));
    best = std::min(best, dafbCell_.cyclesToNextEvent());
    return std::max(best, 1);
}

void Q605Memory::flushScc() {
    // Normally bounded by cyclesToNextEvent(). Chunking also makes a long
    // idle snapshot safe: an SCC with no armed event may accumulate more
    // than tick(int) can represent before a later MMIO/wire access.
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

void Q605Memory::flushScsi() {
    while (scsiDebt_ > 0) {
        const int step = int(std::min<int64_t>(scsiDebt_, 0x7fffffff));
        scsiDebt_ -= step;
        scsi_.tick(step);
    }
    if (scsi_.irq() != ((pvIfr_ & 0x08) != 0)) scsiPoll_();
}

namespace {
template <class T> uint64_t lockstepHash(const T& state) {
    std::vector<sav::u8> bytes;
    bytes.reserve(4096);
    sav::Writer ar(bytes);
    // Writer never mutates visited state.  The save-state visitor contract
    // predates const visitors, hence this narrow diagnostic const_cast.
    auto& writable = const_cast<T&>(state);
    ar(writable);
    return sav::hash(bytes.data(), bytes.size());
}
}

Q605Memory::LockstepDebug Q605Memory::lockstepDebug() const {
    LockstepDebug d;
    d.via1 = lockstepHash(via1_);
    d.cuda = lockstepHash(cuda_);
    d.cudaLle = lockstepHash(cudaLle_);
    d.adb = lockstepHash(adb_);
    d.scc = lockstepHash(scc_);
    d.asc = lockstepHash(asc_);
    d.swim = lockstepHash(swim_);
    d.scsi = lockstepHash(scsi_);
    d.dafb = lockstepHash(dafbCell_);

    std::vector<sav::u8> machineBytes;
    sav::Writer machineAr(machineBytes);
    auto overlay = overlay_;
    auto sccIrq = sccIrq_;
    auto ascLine = ascLine_;
    auto pvIfr = pvIfr_, pvIer = pvIer_, pvPortB = pvPortB_, nubusIrqs = nubusIrqs_;
    auto scsiReadCycles = scsiReadCycles_, scsiWriteCycles = scsiWriteCycles_;
    auto scsiDmaReadCycles = scsiDmaReadCycles_;
    auto scsiDmaWriteCycles = scsiDmaWriteCycles_;
    auto ascCycAcc = ascCycAcc_, swimLastCpu = swimLastCpu_;
    auto swimCycAcc = swimCycAcc_, tickAcc = tickAcc_;
    auto sccDebt = sccDebt_, scsiDebt = scsiDebt_;
    auto viaEClock = viaEClock_;
    machineAr(overlay, sccIrq, ascLine, pvIfr, pvIer, pvPortB, nubusIrqs,
              scsiReadCycles, scsiWriteCycles,
              scsiDmaReadCycles, scsiDmaWriteCycles,
              ascCycAcc, swimLastCpu, swimCycAcc, tickAcc, viaEClock,
              sccDebt, scsiDebt);
    d.machine = sav::hash(machineBytes.data(), machineBytes.size());
    d.nextEvent = cyclesToNextEvent();
    d.ipl = iplLevel();
    d.ascCycAcc = ascCycAcc_;
    d.swimLastCpu = swimLastCpu_;
    d.swimCycAcc = swimCycAcc_;
    d.tickAcc = tickAcc_;
    d.pvIfr = pvIfr_;
    d.pvIer = pvIer_;
    d.sccIrq = sccIrq_;
    d.ascLine = ascLine_;
    return d;
}
