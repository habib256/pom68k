// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "IIfxMemory.h"
#include "IIfxCpu.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

// Diagnostic tracer (POM68K_IIFX_IO_TRACE=1): unknown I/O touches, PIC
// /RSTPIC edges, OSS writes — the bring-up eyes (docs/IOP_BRINGUP.md M3).
static bool iifxIoTrace() {
    static const bool t = std::getenv("POM68K_IIFX_IO_TRACE") != nullptr;
    return t;
}

IIfxMemory::~IIfxMemory() { delete toby_; }

IIfxMemory::IIfxMemory(uint32_t ramSize)
    : ram_(ramSize, 0), rom_(kRomSize, 0xFF), ramSize_(ramSize) {
    // ASC IRQ → OSS input 8 (`maciifx.cpp:458`).
    asc_.onIrq = [this](bool s) { ossSetInput(8, s); };
    // The two IOPs' host interrupts → OSS inputs 7 (SCC) and 6 (SWIM)
    // (`maciifx.cpp:473,482`).
    sccPic_.hostInt = [this](bool s) { ossSetInput(7, s); };
    swimPic_.hostInt = [this](bool s) { ossSetInput(6, s); };
    // SCC behind the SCC PIC: prd/pwr use the z80scc "universal bus"
    // decode — offset bit0 = channel (1 = A), bit1 = data/control
    // (`z80scc.cpp:788-801`; POM68K channel index 1 IS channel A).
    sccPic_.readPeriph = [this](int r) -> uint8_t {
        const int ch = r & 1;
        return (r & 2) ? scc_.readData(ch) : scc_.readCtl(ch);
    };
    sccPic_.writePeriph = [this](int r, uint8_t v) {
        const int ch = r & 1;
        if (r & 2) scc_.writeData(ch, v);
        else scc_.writeCtl(ch, v);
        sccPic_.pintW(scc_.irqAsserted());
    };
    // SWIM behind the SWIM PIC (`maciifx.cpp:479-486`).
    swimPic_.readPeriph = [this](int r) -> uint8_t { return swim_.read(r); };
    swimPic_.writePeriph = [this](int r, uint8_t v) { swim_.write(r, v); };
    // DAT1BYTE → the IOP's DMA channel A only (maciifx.cpp:486; the Quadra
    // 900/950 append channel B, this board does not). The line is level:
    // "the ISM FIFO can take/give a byte now".
    swim_.onDat1Byte = [this](bool s) { swimPic_.reqaW(s); };
    // ADB on the SWIM PIC's GPIO (M5): the IOP firmware bit-bangs the
    // wire, `AdbLine` answers as keyboard+mouse — LLE on both ends.
    // gpout0 is inverted on the board (MAME `maciifx.cpp:483` .invert()):
    // dpll bit1 = 0 at reset → line released (idle high). gpin bit 0
    // reads the combined wired-AND level back.
    swimPic_.gpOut = [this](int pin, bool level) {
        if (pin != 0) return;
        adbHostEdges_++;
        static const bool trace = std::getenv("POM68K_IIFX_ADB_TRACE") != nullptr;
        const int stBefore = adbLine_.dbgLinestate();
        adbLine_.setHostDrive(!level);
        if (trace) {
            static int n = 0;
            const int stAfter = adbLine_.dbgLinestate();
            if (stAfter != stBefore && stAfter >= 10 && n++ < 300)
                std::fprintf(stderr,
                             "iifx-adb: st %d->%d cmd=%02X dsz=%d @pic=%lld\n",
                             stBefore, stAfter, adbLine_.dbgCommand(),
                             adbLine_.dbgDatasize(),
                             (long long)swimPic_.cpu().cycleCount());
        }
    };
    swimPic_.gpIn = [this]() -> uint8_t {
        return adbLine_.line() ? 0x01 : 0x00;
    };
    // NuBus slot IRQs 9-E → OSS inputs 0-5 (`maciifx.cpp:501-506`).
    nubus_.setIrqCallback([this](int slot, bool active) {
        if (slot >= 9 && slot <= 14) ossSetInput(slot - 9, active);
    });
}

bool IIfxMemory::loadRom(const std::vector<uint8_t>& data) {
    if (data.size() != kRomSize) return false;
    rom_ = data;
    return true;
}

bool IIfxMemory::installTobyVideo(const std::string& declRomPath) {
    if (toby_) return true;
    toby_ = new TobyVideo(nubus_, 9);
    std::vector<uint8_t> decl;
    if (!declRomPath.empty())
        decl = DeclRom::loadTobyRaw(declRomPath);
    if (decl.empty()) {
        static const char* paths[] = {
            "tests/data/342-0008-a.bin",
            "../tests/data/342-0008-a.bin",
            "roms/342-0008-a.bin",
            "../roms/342-0008-a.bin",
        };
        for (const char* p : paths) {
            decl = DeclRom::loadTobyRaw(p);
            if (!decl.empty()) break;
        }
    }
    if (decl.empty()) {
        std::fprintf(stderr, "IIfxMemory: no Toby decl ROM — using synthetic\n");
        decl = DeclRom::buildSynthetic(nubus_.slotBase(9));
    }
    nubus_.installCard(9, toby_, decl);
    return true;
}

void IIfxMemory::reset() {
    overlay_ = true;
    scsiDmaCtl_ = 0;
    scsiDmaCount_ = 0;
    scsiDmaAddr_ = 0;
    for (uint8_t& r : ossRegs_) r = 0;
    viaPhase_ = 0;
    c15Acc_ = 0;
    tickAcc_ = 0;
    secAcc_ = 0;
    via1_.reset();
    rtc_.reset();
    rtc_.factoryDefaults();
    asc_.reset();
    sccPic_.reset();
    swimPic_.reset();
    scsi_.reset();
    adbLine_.reset();
    swim_.reset();
    swim_.attachDrive(&drive_, nullptr);
    drive_.reset();
    drive_.setSpinClockHz(kC15MHz);
    scc_.reset();
    // SCC ticks in the C15M domain; PCLK = C7M (the II-class wiring).
    scc_.setClocks(kC15MHz, 7833600);
    scc_.setCtsHigh(false);
    if (toby_) toby_->reset();
    // Machine ID on VIA1 port A: PA6|PA4|PA1 (`maciifx.cpp:259-262`).
    via1_.setInA(0xD3);
    refreshVia1PortB();
}

void IIfxMemory::busError() const {
    if (cpu_) cpu_->extBusError();
    throw moira::MmuBusError{};
}

// ── OSS (`maciifx.cpp:329-398`) ───────────────────────────────────────────

void IIfxMemory::ossSetInput(int n, bool state) {
    const int reg = (n >= 8) ? 0x202 : 0x203;
    const uint8_t bit = uint8_t(1u << (n & 7));
    const uint8_t before = ossRegs_[reg];
    if (state) ossRegs_[reg] |= bit;
    else       ossRegs_[reg] &= uint8_t(~bit);
    if (ossRegs_[reg] != before) updateIrq();
}

int IIfxMemory::iplLevel() const {
    // Highest requested level over all pending inputs; regs[0..7] hold
    // inputs 0-7's levels, regs[8..15] inputs 8-15's.
    int take = 0;
    for (int n = 0; n < 8; n++) {
        if ((ossRegs_[0x203] >> n) & 1)
            take = std::max(take, int(ossRegs_[n]));
        if ((ossRegs_[0x202] >> n) & 1)
            take = std::max(take, int(ossRegs_[8 + n]));
    }
    return take;
}

uint8_t IIfxMemory::ossRead(uint32_t off) {
    return off < 0x400 ? ossRegs_[off] : 0;
}

void IIfxMemory::ossWrite(uint32_t off, uint8_t v) {
    if (off == 0x207) {
        // Ack the 60.15 Hz tick (`maciifx.cpp:388-393`).
        ossSetInput(10, false);
        return;
    }
    if (off < 0x400) {
        if (iifxIoTrace() && off < 0x10)
            std::fprintf(stderr, "iifx: OSS pri[%u]=%d pc=%08X\n",
                         off, v, cpu_ ? unsigned(cpu_->getPC()) : 0u);
        ossRegs_[off] = v;
        updateIrq();   // priority rewrites can change the level
    }
}

void IIfxMemory::updateIrq() {
    // regs[$200] bit 7 = an interrupt is being taken (`maciifx.cpp:358-366`).
    if (iplLevel() > 0) ossRegs_[0x200] |= 0x80;
    else                ossRegs_[0x200] &= 0x7F;
    if (cpu_) cpu_->updateIpl();
}

// ── VIA1 (R65NC22 at C7M/10 = 783.36 kHz) ────────────────────────────────

void IIfxMemory::refreshVia1PortB() {
    // MAME via_in_b returns only the RTC data bit (`maciifx.cpp:264-267`).
    via1_.setInB(uint8_t(rtc_.dataBit() & 1));
}

void IIfxMemory::viaSync() {
    // MAME maciifx via_sync (`maciifx.cpp:208-229`) in integer clocks:
    // align the access on the 783.36 kHz VIA clock.
    if (!cpu_) return;
    const int64_t c = cpu_->machineClock();
    const int64_t viaHz = 783360;
    const int64_t viaCycle = c * viaHz / kCpuHz;
    const int64_t target = (viaCycle * 2 + 3) * kCpuHz / (2 * viaHz) + 1;
    if (target > c) cpu_->stall(int(target - c));
}

uint16_t IIfxMemory::viaAccess(uint32_t addr, bool write, uint16_t v) {
    if (cpu_) cpu_->flushTicks();
    viaSync();
    const int reg = (addr >> 9) & 0xF;
    if (write) {
        via1_.write(reg, uint8_t(v & 0xFF));
        if (reg == Via6522::ORB || reg == Via6522::DDRB) {
            // RTC on port B: bit 0 data, bit 1 clock, bit 2 /enable
            // (`maciifx.cpp:273-278`; active-low select, the Mac II wiring).
            rtc_.setLines(!(via1_.portB() & 0x04),
                          (via1_.portB() & 0x02) != 0,
                          (via1_.portB() & 0x01) != 0);
        }
        ossSetInput(11, via1_.irqAsserted());
        return 0;
    }
    if (reg == Via6522::ORB) refreshVia1PortB();
    const uint8_t lo = via1_.read(reg);
    ossSetInput(11, via1_.irqAsserted());
    return uint16_t(lo) | (uint16_t(lo) << 8);
}

// ── SCSIDMA (M3 subset: bare 53C80 + soft handshake; `scsidma.cpp`) ──────

// Diagnostic (POM68K_IIFX_SCSI_TRACE=1): every 5380/SCSIDMA register
// touch with PC — the POST self-test sequence decoder.
static bool iifxScsiTrace() {
    static const bool t = std::getenv("POM68K_IIFX_SCSI_TRACE") != nullptr;
    return t;
}

uint8_t IIfxMemory::scsiDmaRead(uint32_t off) {
    if (iifxScsiTrace()) {
        static int n = 0;
        const unsigned pc = cpu_ ? unsigned(cpu_->getPC()) : 0u;
        const bool inTest = pc >= 0x40807000u && pc < 0x40808000u;
        if (inTest && n++ < 300 && !(off >= 0x40 && off < 0x50))
            std::fprintf(stderr, "iifx-scsi: rd %03X pc=%08X\n", off, pc);
    }
    if (off < 0x80) {
        // $00-$03 handshake data port: single-byte non-handshake reads with
        // no DRQ fall through to the 5380's CSD — reg 0 (`scsidma.cpp:262-269`).
        // Swallowing these instead ate the boot scan's ODR traffic (the
        // 2026-08-01 selection bug).
        if (off < 0x04)
            return scsi_.drqActive() ? scsi_.dmaRead() : scsi_.read(0);
        // $60-$63 handshake read: DRQ ? dma_r : read(6) (`scsidma.cpp:213`);
        // the restartable CTRL_HNDSHK stall is M4.
        if (off >= 0x60 && off < 0x64)
            return scsi_.drqActive() ? scsi_.dmaRead() : scsi_.read(6);
        return scsi_.read((off >> 4) & 7);
    }
    // 32-bit control regs, byte-composed: $80 control, $C0 count, $100 address.
    uint32_t reg = 0;
    if ((off & ~3u) == 0x80) reg = scsiDmaCtl_ | (scsi_.irqAsserted() ? kScsiScIrq : 0);
    else if ((off & ~3u) == 0xC0) reg = scsiDmaCount_;
    else if ((off & ~3u) == 0x100) reg = scsiDmaAddr_;
    return uint8_t(reg >> (8 * (3 - (off & 3))));
}

void IIfxMemory::scsiDmaWrite(uint32_t off, uint8_t v) {
    if (iifxScsiTrace()) {
        static int n = 0;
        const unsigned pc = cpu_ ? unsigned(cpu_->getPC()) : 0u;
        if (n++ < 300 && pc >= 0x40807000u && pc < 0x40808000u)
            std::fprintf(stderr, "iifx-scsi: wr %03X=%02X pc=%08X\n", off, v, pc);
    }
    if (off < 0x80) {
        if (off < 0x04) {
            // Single-byte write, no DRQ → the 5380's ODR (reg 0) — MAME
            // `scsidma.cpp:300-310`. This is how the boot scan puts the
            // target ID on the bus.
            if (scsi_.drqActive()) scsi_.dmaWrite(v);
            else scsi_.write(0, v);
            return;
        }
        scsi_.write((off >> 4) & 7, v);
        return;
    }
    auto setByte = [&](uint32_t& reg) {
        const int sh = 8 * (3 - (off & 3));
        reg = (reg & ~(0xFFu << sh)) | (uint32_t(v) << sh);
    };
    if ((off & ~3u) == 0x80) setByte(scsiDmaCtl_);
    else if ((off & ~3u) == 0xC0) setByte(scsiDmaCount_);
    else if ((off & ~3u) == 0x100) setByte(scsiDmaAddr_);
}

// ── Decode ────────────────────────────────────────────────────────────────

bool IIfxMemory::isIo(uint32_t addr, uint32_t& off) const {
    if ((addr & 0xFF000000u) != 0x50000000u) return false;
    off = (addr & ~0x00F00000u) - 0x50000000u;
    return true;
}

// ── JIT memory hooks (src/jit/POM68K_JIT.md § 4) ────────────────────────
// Mirrors read8Decoded()'s plain-memory cases. No address to reconcile: the
// IIfx has no HMMU and no GLUE remap, so what the probe reports is what the
// bus decodes (IIfxMemory.h § JIT memory hooks).
const uint8_t* IIfxMemory::codeSpan(uint32_t phys, uint32_t& len) const {
    len = 0;
    // The overlay is dropped by a ROM-region READ (rom_switch_r). A windowed
    // fetch performs no read, so while it is up the window must serve
    // NOTHING — not the ROM region whose read would drop it, and not low
    // memory, which is that same ROM until it does.
    if (overlay_) return nullptr;
    if (phys < 0x40000000u) {
        if (phys >= ramSize_) return nullptr;     // above the SIMMs: open bus
        len = ramSize_ - phys;
        return ram_.data() + phys;
    }
    if (phys < 0x50000000u) {                     // ROM, mirrored every 512 KB
        const uint32_t o = (phys - 0x40000000u) & (kRomSize - 1);
        len = kRomSize - o;
        return rom_.data() + o;
    }
    return nullptr;                               // OSS/VIA/PIC I/O, NuBus
}

uint8_t* IIfxMemory::dataSpan(uint32_t phys, uint32_t& len, bool write) {
    len = 0;
    if (overlay_) return nullptr;
    if (phys < 0x40000000u) {
        if (phys >= ramSize_) return nullptr;
        len = ramSize_ - phys;
        return ram_.data() + phys;
    }
    if (!write && phys < 0x50000000u) {
        const uint32_t o = (phys - 0x40000000u) & (kRomSize - 1);
        len = kRomSize - o;
        return rom_.data() + o;
    }
    return nullptr;
}

void IIfxMemory::jitMapChanged() {
    if (jitGuard_) jitGuard_->invalidate();
    if (cpu_) cpu_->pomJitDtlbFlush();
}

uint8_t IIfxMemory::read8(uint32_t addr) { return read8Decoded(addr); }

uint8_t IIfxMemory::read8Decoded(uint32_t addr) {
    if (addr < 0x40000000u) {
        if (overlay_)
            return addr < 0x04000000u ? rom_[addr & (kRomSize - 1)] : 0xFF;
        if (addr < ramSize_) return ram_[addr];
        return 0xFF;
    }
    if (addr < 0x50000000u) {
        // Any ROM-region read drops the overlay (rom_switch_r,
        // `maciifx.cpp:169-186`) — the very first instruction fetch after
        // reset already lands here. This is a READ side effect, so it is
        // also the one map move a JIT write guard can never see: say it.
        if (overlay_) { overlay_ = false; jitMapChanged(); }
        return rom_[(addr - 0x40000000u) & (kRomSize - 1)];
    }

    uint32_t off = 0;
    if (isIo(addr, off)) {
        if (cpu_) cpu_->flushTicks();
        if (off < 0x2000 || (off >= 0x40000 && off < 0x42000))
            return uint8_t(viaAccess(off & 0x1FFF, false, 0) >> 8);
        if (off >= 0x4000 && off < 0x6000)
            return sccPic_.hostRead((off >> 1) & 0x1F);
        if (off >= 0x8000 && off < 0xA000)
            return scsiDmaRead(off & 0x1FF);
        if (off >= 0x10000 && off < 0x12000)
            return asc_.read(off - 0x10000);
        if (off >= 0x12000 && off < 0x14000)
            return swimPic_.hostRead((off >> 1) & 0x1F);
        if (off >= 0x18000 && off < 0x1A000)
            return 0;                            // BIU (`maciifx.cpp:320-327`)
        if (off >= 0x1A000 && off < 0x1C000)
            return ossRead(off - 0x1A000);
        if (off >= 0x24000 && off < 0x28000)
            busError();                          // the FMC probe (`:204`)
        if (iifxIoTrace()) {
            static int n = 0;
            if (n++ < 200)
                std::fprintf(stderr, "iifx: rd unknown io off=%05X pc=%08X\n",
                             off, cpu_ ? unsigned(cpu_->getPC()) : 0u);
        }
        return 0xFF;                             // open bus (map probe)
    }

    if (addr >= 0x90000000u && addr < 0xFF000000u)
        return nubus_.read8(addr);
    return 0xFF;
}

uint16_t IIfxMemory::read16(uint32_t addr) {
    if (addr >= 0x50000000u && addr < 0x60000000u) {
        uint32_t off = 0;
        if (isIo(addr, off)) {
            if (off < 0x2000 || (off >= 0x40000 && off < 0x42000))
                return viaAccess(off & 0x1FFF, false, 0);
        }
    }
    return uint16_t(read8Decoded(addr) << 8) | read8Decoded(addr + 1);
}

void IIfxMemory::write8(uint32_t addr, uint8_t v) { write8Decoded(addr, v); }

void IIfxMemory::write8Decoded(uint32_t addr, uint8_t v) {
    if (addr < 0x40000000u) {
        if (overlay_) return;                    // ROM under the overlay
        if (addr < ramSize_) {
            // One name per byte on this board — no $800000 alias to mirror
            // the way the GLUE and V8 maps need (IIfxMemory.h § JIT hooks).
            if (jitGuard_) jitGuard_->note(addr, 1);
            ram_[addr] = v;
        }
        return;
    }
    if (addr < 0x50000000u) return;              // ROM

    uint32_t off = 0;
    if (isIo(addr, off)) {
        if (cpu_) cpu_->flushTicks();
        if (off < 0x2000 || (off >= 0x40000 && off < 0x42000)) {
            viaAccess(off & 0x1FFF, true, v);
            return;
        }
        if (off >= 0x4000 && off < 0x6000) {
            sccPic_.hostWrite((off >> 1) & 0x1F, v);
            return;
        }
        if (off >= 0x8000 && off < 0xA000) {
            scsiDmaWrite(off & 0x1FF, v);
            return;
        }
        if (off >= 0x10000 && off < 0x12000) {
            asc_.write(off - 0x10000, v);
            return;
        }
        if (off >= 0x12000 && off < 0x14000) {
            swimPic_.hostWrite((off >> 1) & 0x1F, v);
            return;
        }
        if (off >= 0x18000 && off < 0x1A000) return;   // BIU
        if (off >= 0x1A000 && off < 0x1C000) {
            ossWrite(off - 0x1A000, v);
            return;
        }
        if (off >= 0x24000 && off < 0x28000)
            busError();
        if (iifxIoTrace()) {
            static int n = 0;
            if (n++ < 200)
                std::fprintf(stderr, "iifx: wr unknown io off=%05X v=%02X pc=%08X\n",
                             off, v, cpu_ ? unsigned(cpu_->getPC()) : 0u);
        }
        return;
    }

    if (addr >= 0x90000000u && addr < 0xFF000000u)
        nubus_.write8(addr, v);
}

void IIfxMemory::write16(uint32_t addr, uint16_t v) {
    if (addr >= 0x50000000u && addr < 0x60000000u) {
        uint32_t off = 0;
        if (isIo(addr, off)) {
            if (off < 0x2000 || (off >= 0x40000 && off < 0x42000)) {
                viaAccess(off & 0x1FFF, true, v);
                return;
            }
        }
    }
    write8Decoded(addr, uint8_t(v >> 8));
    write8Decoded(addr + 1, uint8_t(v));
}

uint8_t IIfxMemory::peek8(uint32_t addr) const {
    // Side-effect-free (disassembler / gates): no overlay clearing.
    if (addr < 0x40000000u) {
        if (overlay_)
            return addr < 0x04000000u ? rom_[addr & (kRomSize - 1)] : 0xFF;
        if (addr < ramSize_) return ram_[addr];
        return 0xFF;
    }
    if (addr < 0x50000000u)
        return rom_[(addr - 0x40000000u) & (kRomSize - 1)];
    return 0xFF;
}

// ── Time ──────────────────────────────────────────────────────────────────

void IIfxMemory::tick(int cpuCycles) {
    // 40 MHz → C15M: ×15667200/40000000 = ×9792/25000, remainder carried.
    c15Acc_ += int64_t(cpuCycles) * 9792;
    const int c15 = int(c15Acc_ / 25000);
    c15Acc_ %= 25000;
    if (!c15) return;

    // VIA at C15M/20 = 783.36 kHz (the Mac II divisor).
    viaPhase_ += c15;
    const int t = viaPhase_ / 20;
    viaPhase_ %= 20;
    if (t) {
        via1_.tick(t);
        ossSetInput(11, via1_.irqAsserted());
    }

    // Both IOPs run on C15M (65C02 at C15M/8 inside ApplePic). The ADB
    // line's device timers live in the same domain (AdbLine was rebased
    // onto Mac II cycles = C15M). Batch granularity here is ≤25 C15M
    // cycles per flush — 1.6 % of a 100 µs ADB bit cell; if the IOP
    // firmware turns out phase-sensitive, slave the wire to the 65C02's
    // instruction stream like `CudaLle` does (`pom68k-mactv-gate-broken`).
    sccPic_.tick(c15);
    swimPic_.tick(c15);
    adbLine_.tick(c15);

    asc_.tick(c15);
    swim_.tick(c15);
    drive_.tick(c15);
    if (scc_.tick(c15))
        sccPic_.pintW(scc_.irqAsserted());
    nubus_.tick(c15);

    // SCSIDMA IRQ → OSS input 9, gated by CTRL_IRQEN (`scsidma.cpp:353-363`).
    ossSetInput(9, scsi_.irqAsserted() && (scsiDmaCtl_ & kScsiIrqEn));

    tickAcc_ += c15;
    // 60.15 Hz tick: VIA1 CA1 pulse + OSS input 10, held until the $207
    // ack (`maciifx.cpp:369-374`).
    if (tickAcc_ >= (kC15MHz * 100) / 6015) {
        tickAcc_ -= (kC15MHz * 100) / 6015;
        via1_.raiseCa1();
        vblPulses_++;
        ossSetInput(10, true);
        ossSetInput(11, via1_.irqAsserted());
    }
    secAcc_ += c15;
    if (secAcc_ >= kC15MHz) {
        secAcc_ -= kC15MHz;
        rtc_.tickSecond();
        via1_.raiseCa2();   // RTC CKO → CA2 (`maciifx.cpp:424-425`)
        ossSetInput(11, via1_.irqAsserted());
    }
}
