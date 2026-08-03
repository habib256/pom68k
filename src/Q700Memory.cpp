// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Q700Memory.h"
#include "Q700Cpu.h"
#include "Moira.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

Q700Memory::Q700Memory(uint32_t totalRam, int64_t cpuHz, Model model)
    // Discrete DAFB (macquadra700.cpp:729 instantiates plain dafb_device):
    // the DP8531 clock chip, not the MEMCjr's Gazelle (dafb.cpp:884).
    : asc_(cpuHz), egret_(via1_, /*cudaPolarity=*/false, int(cpuHz)),
      totalRam_(totalRam), cpuHz_(cpuHz), model_(model),
      dafbCell_(cpuHz, Dafb::Clockgen::Dp8531) {
    while (totalRam_ & (totalRam_ - 1)) totalRam_ &= totalRam_ - 1;   // pow2
    ram_.assign(totalRam_, 0);
    rom_.assign(kRomSize, 0xFF);
    vram_.assign(kVramSize, 0);
    adbVia_.attach(via1_, adb_, cpuHz_);
    if (eclipse()) {
        // The IIfx front end (docs/IOP_BRINGUP.md), grafted on the Spike
        // board. SCC behind its IOP: the z80scc "universal bus" decode —
        // offset bit0 = channel (1 = A), bit1 = data/control.
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
        // The SCC IOP's host interrupt IS the board's SCC interrupt line
        // (macquadra700.cpp:867 hint → scc_irq_w).
        sccPic_.hostInt = [this](bool s) { sccIrqLine(s); };
        swimPic_.readPeriph = [this](int r) -> uint8_t { return swim_.read(r); };
        swimPic_.writePeriph = [this](int r, uint8_t v) { swim_.write(r, v); };
        // DAT1BYTE → BOTH of the IOP's DMA request channels
        // (macquadra700.cpp:879-880 sets reqa and appends reqb). The line
        // says "the ISM FIFO can take/give a byte now"; whichever channel
        // the firmware has enabled is the one that moves.
        swim_.onDat1Byte = [this](bool s) {
            swimPic_.reqaW(s);
            swimPic_.reqbW(s);
        };
        // The SWIM IOP's host interrupt lands on VIA2 CA2, INVERTED
        // (macquadra700.cpp:877). The 6522 latches that line on its edge,
        // so an assertion (line → 0) is the flag-setting transition;
        // `raiseCa2` is that edge (`Via6522` models CA2 edge-only).
        swimPic_.hostInt = [this](bool s) {
            if (s) { via2_.raiseCa2(); updateIrq(); }
        };
        // ADB on the SWIM IOP's GPIO: gpout0 drives the wire (inverted on
        // the board), gpin reads the wired-AND level back.
        swimPic_.gpOut = [this](int pin, bool level) {
            if (pin != 0) return;
            adbHostEdges_++;
            adbLine_.setHostDrive(!level);
        };
        swimPic_.gpIn = [this]() -> uint8_t {
            return adbLine_.line() ? 0x01 : 0x00;
        };
        // MAME wires the ADB devices' data line to BOTH the IOP's gpin and
        // the Egret (`macadb->adb_data_callback().append(m_egret,
        // set_adb_line)`), so which side actually serves the guest is a
        // question of what the ROM drives — measured, not assumed
        // (POM68K_Q900_ADB=iop forces the IIfx-style IOP-only wire).
        const char* who = std::getenv("POM68K_Q900_ADB");
        egretAdb_ = !(who && std::string(who) == "iop");
        if (egretAdb_) egret_.setAdbBus(&adb_);

        // POM68K_Q900_IOPBRK=1: both IOP firmwares end in a BRK panic
        // handler when they lose their way, and the only useful evidence
        // is where they came FROM — dump the 65C02 PC trail on the first
        // `$00` executed (docs/IOP_BRINGUP.md § M7).
        if (std::getenv("POM68K_Q900_IOPBRK")) {
            sccPic_.cpu().setTrace(true);
            swimPic_.cpu().setTrace(true);
            auto trap = [this](const char* who2, R65c02& c) {
                return [this, who2, &c](uint16_t pc) {
                    static int once = 0;
                    if (once++) return;
                    std::fprintf(stderr, "[IOP-BRK] %s executed $00 at $%04X; trail:",
                                 who2, pc);
                    const std::vector<uint16_t> t = c.pcTrail();
                    const std::vector<uint8_t> s = c.spTrail();
                    int k = 0;
                    for (uint16_t p : t) {
                        std::fprintf(stderr, "%s$%04X/%02X", (k % 8) ? " " : "\n  ",
                                     p, k < int(s.size()) ? s[k] : 0);
                        k++;
                    }
                    std::fprintf(stderr, "\n  A=%02X X=%02X Y=%02X SP=%02X P=%02X\n",
                                 c.getAccumulator(), c.getXRegister(),
                                 c.getYRegister(), c.getStackPointer(),
                                 c.getStatusRegister());
                    dumpIopRing();
                };
            };
            sccPic_.cpu().onBrk = trap("sccPIC", sccPic_.cpu());
            swimPic_.cpu().onBrk = trap("swimPIC", swimPic_.cpu());
        }
        // POM68K_Q900_IOPWATCH=<hex>: name every writer of one IOP RAM byte
        // (65C02 / host window / DMA). The SWIM IOP returned through a
        // corrupted stack frame, and only three things can corrupt it.
        if (const char* w = std::getenv("POM68K_Q900_IOPWATCH"))
            swimPic_.watch = int(strtoul(w, nullptr, 16));
    }
    dafbCell_.onIrq = [this](bool s) { vblIrq(s); };
    asc_.onIrq = [this](bool s) { ascIrq(s); };
    drive0_.setSuperDrive(true);
    drive1_.setSuperDrive(true);
    drive0_.setSpinClockHz(15667200);        // SWIM1 cell domain = C15M
    drive1_.setSpinClockHz(15667200);
    swim_.attachDrive(&drive0_, &drive1_);
    rtc_.factoryDefaults();
    rtc_.setXpram(0x8A, uint8_t(rtc_.xpram(0x8A) | 0x05));   // 32-bit clean
    // Eclipse: same seeding, in the Egret's PRAM — a cold (all-zero) store
    // sends the ROM down its long full-RAM burn-in on every boot.
    if (eclipse()) {
        egret_.factoryDefaults();
        egret_.setPram(0x8A, uint8_t(egret_.pram(0x8A) | 0x05));
    }
    {
        const char* e = std::getenv("POM68K_SCSI_LAT");
        scsi_.setLatency(e ? std::atoi(e) : -1);
    }
}

bool Q700Memory::loadRom(const std::vector<uint8_t>& data) {
    if (data.size() != kRomSize) return false;
    std::memcpy(rom_.data(), data.data(), kRomSize);
    jitMapChanged();
    return true;
}

// On the Eclipse the battery-backed store lives in the Egret, not in a
// discrete RTC — route there so a tower keeps its clock and startup disk.
bool Q700Memory::loadPram(const std::string& path) {
    if (eclipse()) return egret_.loadPram(path);
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    if (b.size() < 256) return false;
    for (int i = 0; i < 256; i++) rtc_.setXpram(uint8_t(i), b[i]);
    return true;
}

void Q700Memory::savePram(const std::string& path) {
    if (eclipse()) { egret_.savePram(path); return; }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return;
    for (int i = 0; i < 256; i++) {
        uint8_t v = rtc_.xpram(uint8_t(i));
        out.write(reinterpret_cast<const char*>(&v), 1);
    }
}

void Q700Memory::reset() {
    overlay_ = true;
    jitMapChanged();
    nubusIrqs_ = 0xFF;
    scsiCtrl_ = 0;
    scsiReadCycles_ = scsiWriteCycles_ = 3;
    scsiDmaReadCycles_ = scsiDmaWriteCycles_ = 3;
    dafbCell_.reset();
    via1_.reset();
    via2_.reset();
    rtc_.reset();
    adbVia_.reset();
    if (eclipse()) {
        sccPic_.reset();
        swimPic_.reset();
        adbLine_.reset();
        egret_.reset();          // holds the 68040 until the MCU releases it
        scsi2_.reset();
        adbHostEdges_ = 0;
    }
    scc_.reset();
    scsi_.reset();
    asc_.reset();
    swim_.reset();
    swim_.attachDrive(&drive0_, &drive1_);
    drive0_.reset();
    drive1_.reset();
    ascCycAcc_ = 0;
    swimLastCpu_ = -1;
    swimCycAcc_ = 0;
    scc_.setClocks(cpuHz_, 7833600);
    scc_.setCtsHigh(false);
    scc_.setAbortIdle(true);       // no hardwired LocalTalk peer — the
                                   // standing abort is a line state, not
                                   // machine config (Scc8530::openLine,
                                   // LLE steps 7+8: virgin line = clean)
    viaEClock_ = {};
    tickAcc_ = 0;
    secAcc_ = 0;
    sccIrq_ = false;
    // VIA1 PA identity | diagnostic-disabled bit 0 — the IIci lesson:
    // feeding PA0 = 0 sends the ROM down the burn-in path. $C0 = Spike
    // (`spike_state::via_in_a`), $D0 = Q900, $90 = Q950
    // (`eclipse_state::via_in_a` / `via_in_a_q950`).
    via1_.setInA(model_ == Model::Q900 ? 0xD1
               : model_ == Model::Q950 ? 0x91 : 0xC1);
    refreshVia1PortB();
    // VIA2 PB = $CF (via2_in_b: "no NuBus transaction error").
    via2_.setInB(0xCF);
    refreshVia2PortA();
}

void Q700Memory::busError(uint32_t addr, bool write) const {
    if (onBusError) onBusError(addr, write);
    if (cpu_) cpu_->extBusError040();
    throw moira::MmuBusError{};
}

int Q700Memory::iplLevel() const {
    if (sccIrq_) return 4;
    if (via2_.irqAsserted()) return 2;
    if (via1_.irqAsserted()) return 1;
    return 0;
}

void Q700Memory::updateIrq() {
    if (cpu_) cpu_->updateIpl();
}

// Slot interrupts (quadrax00_state::nubus_slot_interrupt): active-low bit per
// slot in VIA2 port A, plus a CA1 edge. The DAFB is slot $F → bit 6.
void Q700Memory::refreshVia2PortA() {
    via2_.setInA(uint8_t(0x80 | nubusIrqs_));
}

void Q700Memory::vblIrq(bool s) {
    uint8_t before = nubusIrqs_;
    if (s) nubusIrqs_ &= uint8_t(~0x40); else nubusIrqs_ |= 0x40;
    refreshVia2PortA();
    if (before != nubusIrqs_ && s) via2_.raiseCa1();
    updateIrq();
}

void Q700Memory::scsiIrq(bool s) {
    via2_.setCb2(!s);                 // active low (irq_handler_cb .invert())
    updateIrq();
}

void Q700Memory::ascIrq(bool s) {
    via2_.setCb1(!s);                 // active low (irqf_callback .invert())
    updateIrq();
}

void Q700Memory::syncSwimFromCpu() {
    if (!cpu_) return;
    const int64_t now = int64_t(cpu_->machineClock());
    if (swimLastCpu_ < 0) { swimLastCpu_ = now; return; }
    const int64_t delta = now - swimLastCpu_;
    if (delta <= 0) return;
    swimLastCpu_ = now;
    swimCycAcc_ += delta * 15667200;              // SWIM1 cells run at C15M
    const int cyc = int(swimCycAcc_ / cpuHz_);
    swimCycAcc_ -= int64_t(cyc) * cpuHz_;
    if (cyc) swim_.tick(cyc);
}

// Eclipse bring-up eyes (POM68K_Q900_IOP_TRACE=1): every host-window touch
// on either IOP, with the decoded 5-bit offset and the CPU's PC.
void Q700Memory::iopTrace(bool write, char which, uint32_t base, uint8_t v) {
    // POM68K_Q900_IOP_TRACE=<max lines> (1 = the default 600). The data
    // port floods, so a bare `=1` is useless past the first upload —
    // raise it, or grep the control writes (off=02) that gate /RSTPIC.
    static const long cap = [] {
        const char* e = std::getenv("POM68K_Q900_IOP_TRACE");
        if (!e) return 0L;
        long n = std::atol(e);
        return n > 1 ? n : 600L;
    }();
    // Ring first (always armed on the Eclipse — it is what a panic dump
    // reads), then the streaming form if the env asked for it.
    ApplePic& pic = (which == 'w') ? swimPic_ : sccPic_;
    iopRing_[iopRingIdx_] = { cpu_ ? unsigned(cpu_->getPC()) : 0u,
                              uint16_t(base), pic.ramAddr(),
                              uint8_t((base >> 1) & 0x1F), v, which,
                              char(write ? 'W' : 'R') };
    iopRingIdx_ = (iopRingIdx_ + 1) % kIopRing;
    if (!cap) return;
    static long n = 0;
    if (n++ >= cap) return;
    std::fprintf(stderr, "iop-%c: %s base=%05X off=%02X v=%02X pc=%08X\n",
                 which, write ? "wr" : "rd", base, (base >> 1) & 0x1F, v,
                 cpu_ ? unsigned(cpu_->getPC()) : 0u);
}

// The last kIopRing host-window touches, oldest first: which IOP, the
// decoded 5-bit register offset, the shared-RAM pointer AT that moment and
// the 68k PC that issued it.
void Q700Memory::dumpIopRing() {
    std::fprintf(stderr, "  host-window trail (oldest first):\n");
    for (int i = 0; i < kIopRing; i++) {
        const IopEvent& e = iopRing_[(iopRingIdx_ + i) % kIopRing];
        if (!e.pc && !e.base) continue;
        std::fprintf(stderr, "    iop-%c %c off=%02X v=%02X ramAddr=$%04X pc=$%08X\n",
                     e.which, e.rw, e.off, e.v, e.ramAddr, e.pc);
    }
}

// VIA E-clock alignment, in MACHINE cycles (CHANGELOG 2026-07-25): the
// 25 MHz / 783.36 kHz ratio is the Centris's 32:1 approximation.
void Q700Memory::viaSync() {
    if (!cpu_) return;
    int64_t c = int64_t(cpu_->machineClock());
    const int64_t target = via_eclock::syncTarget(c, cpuHz_);
    if (target > c) cpu_->stall(int(target - c));
}

// VIA1 PB. Spike (`spike_state::via_in_b`): PB0 = RTC serial data,
// PB3 = /ADB IRQ. Eclipse (`eclipse_state::via_in_b`): the discrete RTC is
// gone — the Egret owns clock/PRAM/power and the only input is its
// XCVR_SESSION on PB3.
void Q700Memory::refreshVia1PortB() {
    if (eclipse()) {
        via1_.setInB(uint8_t(egret_.xcvrSession() << 3));
        return;
    }
    uint8_t in = rtc_.dataBit() & 1;
    in |= 0xC6;
    if (!adbVia_.irqPending()) in |= 0x08;
    via1_.setInB(in);
}

uint8_t Q700Memory::viaAccess8(uint32_t addr, bool write, uint8_t v) {
    if (cpu_) cpu_->flushTicks();
    viaSync();
    if (!eclipse() && cpu_) adbVia_.syncTo(cpu_->machineClock());
    int reg = (addr >> 9) & 0x0F;
    if (write) {
        via1_.write(reg, v);
        if (eclipse()) {
            // The Egret transport: port B carries VIA_FULL (PB4) and
            // SYS_SESSION (PB5); no RTC lines, and the SWIM's head-select
            // is the IOP's business (`eclipse_state::via_out_a` is empty).
            if (reg == Via6522::ORB || reg == Via6522::DDRB) {
                egret_.portBChanged(via1_.portB());
                refreshVia1PortB();
            }
            updateIrq();
            return 0;
        }
        // PA5 = floppy head-select (macquadra700.cpp:614-625
        // spike_state::via_out_a → m_cur_floppy->ss_w). SWIM1's IWM
        // personality also multiplexes half its sense registers on this line
        // (Iwm.cpp:23 senseAddr), so leaving it undriven pinned the Quadra 700
        // to side 0. q700_boot_etalon boots from SCSI and never saw it.
        if (reg == Via6522::ORA || reg == Via6522::ORA_NH
            || reg == Via6522::DDRA)
            swim_.setSel((via1_.portA() & 0x20) != 0);
        if (reg == Via6522::ORB || reg == Via6522::DDRB || reg == Via6522::SR
            || reg == Via6522::ACR) {
            adbVia_.sync();
            // spike_state::via_out_b — RTC /ce = PB2, clock = PB1, data = PB0;
            // the ADB ST lines are PB4/PB5 (AdbVia reads them itself).
            rtc_.setLines(!(via1_.portB() & 0x04),
                          (via1_.portB() & 0x02) != 0,
                          (via1_.portB() & 0x01) != 0);
            refreshVia1PortB();
        }
        updateIrq();
        return 0;
    }
    if (reg == Via6522::ORB) refreshVia1PortB();
    uint8_t d = via1_.read(reg);
    updateIrq();
    return d;
}

uint8_t Q700Memory::via2Access8(uint32_t addr, bool write, uint8_t v) {
    if (cpu_) cpu_->flushTicks();
    viaSync();
    int reg = (addr >> 9) & 0x0F;
    if (write) {
        via2_.write(reg, v);
        updateIrq();
        return 0;
    }
    if (reg == Via6522::ORA || reg == Via6522::ORA_NH) refreshVia2PortA();
    uint8_t d = via2_.read(reg);
    updateIrq();
    return d;
}

uint8_t Q700Memory::dafbRead8(uint32_t addr) {
    if (onIoAccess) onIoAccess(0xF9800000 + (addr & 0x3FF), false, 0xFFFFFFFF);
    // TurboSCSI bus-1 control/status (dafb.cpp:421): the latched wait-state
    // bits plus the LIVE DRQ in bit 9 — how the driver polls the FIFO.
    if ((addr & 0x3FC) == 0x24) {
        uint32_t val = uint32_t(scsiCtrl_) | (scsi_.drq() ? 0x200u : 0u);
        return uint8_t(val >> (8 * (3 - (addr & 3))));
    }
    if ((addr & 0x3FC) == 0x210 && (addr & 3) != 3) return 0;
    uint32_t val = dafbCell_.read32(addr & ~3u);
    return uint8_t(val >> (8 * (3 - (addr & 3))));
}

void Q700Memory::dafbWrite8(uint32_t addr, uint8_t v) {
    if (onIoAccess) onIoAccess(0xF9800000 + (addr & 0x3FF), true, v);
    if ((addr & 0x300) == 0x300) {
        dafbCell_.clockgenWrite8(addr & 0x3FF, v);
        return;
    }
    int sh = 8 * (3 - (addr & 3));
    uint32_t merged = (dafbCell_.rawReg(addr) & ~(0xFFu << sh))
                    | (uint32_t(v) << sh);
    if ((addr & 3) != 3) { dafbCell_.setRawReg(addr, merged); return; }
    if ((addr & 0x3FC) == 0x24) {
        // dafb.cpp:490-530 — one register, four wait-state selections.
        scsiCtrl_ = uint16_t(merged);
        scsiReadCycles_  = (merged & 1) ? 6 : ((merged & 2) ? 4 : 3);
        scsiWriteCycles_ = (merged & 4) ? 3 : 4;
        scsiDmaReadCycles_ = (merged & 8) ? 3 : 4;
        if (merged & 0x10)      scsiDmaWriteCycles_ = 5;
        else if (merged & 0x20) scsiDmaWriteCycles_ = 3;
        return;
    }
    dafbCell_.write32(addr & ~3u, merged);
}

uint8_t Q700Memory::ioRead8(uint32_t addr) {
    uint32_t sub = addr & 0x00FFFFFF;             // .mirror(0x00fc0000)
    if (onIoAccess) onIoAccess(addr, false, 0xFFFFFFFF);
    uint32_t base = sub & 0x0003FFFF;

    if (base < 0x02000) return viaAccess8(base, false, 0);
    if (base >= 0x02000 && base < 0x04000) return via2Access8(base, false, 0);
    if (base >= 0x08000 && base < 0x08008) {      // ethernet address ROM
        int off = int(base & 7);
        if (off < 6) return kMacAddr[off];
        if (off == 7) {
            uint8_t x = 0;
            for (uint8_t b : kMacAddr) x ^= b;
            return uint8_t(x ^ 0xFF);
        }
        return 0;
    }
    if (base >= 0x0C000 && base < 0x0E000) {
        // Eclipse: the SCC IOP's host window ($1000 wide) replaces the SCC.
        if (eclipse()) {
            uint8_t d = sccPic_.hostRead((base >> 1) & 0x1F);
            iopTrace(false, 's', base, d);
            return d;
        }
        int ch = (base >> 1) & 1;
        uint8_t d = ((base >> 2) & 1) ? scc_.readData(ch) : scc_.readCtl(ch);
        sccIrqLine(scc_.irqAsserted());
        return d;
    }
    // Eclipse only: the tower's SECOND 53C96 bus (registers + pseudo-DMA).
    if (eclipse() && base >= 0x0F400 && base < 0x0F500)
        return scsi2_.read((base >> 4) & 0xF);
    if (eclipse() && base >= 0x0F500 && base < 0x0F504)
        return scsi2_.drq() ? scsi2_.dmaRead() : 0xFF;
    if (base >= 0x0F000 && base < 0x0F100) {      // TurboSCSI 53C96 registers
        if (cpu_) cpu_->stall(scsiReadCycles_);
        uint8_t d = scsi_.read((base >> 4) & 0xF);
        scsiPoll_();
        return d;
    }
    if (base >= 0x0F100 && base < 0x0F104) {      // TurboSCSI pseudo-DMA
        if (cpu_ && ((sub >> 18) & 1)) cpu_->stall(scsiDmaReadCycles_);
        return scsiDmaRead_();
    }
    if (base >= 0x14000 && base < 0x16000) return asc_.read(addr & 0xFFF);
    if (base >= 0x1E000 && base < 0x20000) {
        // Eclipse: the SWIM IOP's host window replaces the SWIM.
        if (eclipse()) {
            uint8_t d = swimPic_.hostRead((base >> 1) & 0x1F);
            iopTrace(false, 'w', base, d);
            return d;
        }
        if (cpu_) cpu_->stall(5);
        syncSwimFromCpu();
        return (addr & 1) ? 0 : swim_.read((base >> 9) & 0x0F);
    }
    return 0x00;                                  // SONIC / Orwell / unmapped
}

void Q700Memory::ioWrite8(uint32_t addr, uint8_t v) {
    uint32_t sub = addr & 0x00FFFFFF;
    if (onIoAccess) onIoAccess(addr, true, v);
    uint32_t base = sub & 0x0003FFFF;

    if (base < 0x02000) { viaAccess8(base, true, v); return; }
    if (base >= 0x02000 && base < 0x04000) { via2Access8(base, true, v); return; }
    if (base >= 0x0C000 && base < 0x0E000) {
        if (eclipse()) {
            iopTrace(true, 's', base, v);
            sccPic_.hostWrite((base >> 1) & 0x1F, v);
            return;
        }
        int ch = (base >> 1) & 1;
        if ((base >> 2) & 1) scc_.writeData(ch, v);
        else scc_.writeCtl(ch, v);
        sccIrqLine(scc_.irqAsserted());
        return;
    }
    if (eclipse() && base >= 0x0F400 && base < 0x0F500) {
        scsi2_.write((base >> 4) & 0xF, v);
        return;
    }
    if (eclipse() && base >= 0x0F500 && base < 0x0F504) {
        if (scsi2_.drq()) scsi2_.dmaWrite(v);
        return;
    }
    if (base >= 0x0F000 && base < 0x0F100) {
        if (cpu_) cpu_->stall(scsiWriteCycles_);
        scsi_.write((base >> 4) & 0xF, v);
        scsiPoll_();
        return;
    }
    if (base >= 0x0F100 && base < 0x0F104) {
        if (cpu_ && ((sub >> 18) & 1)) cpu_->stall(scsiDmaWriteCycles_);
        scsiDmaWrite_(v);
        return;
    }
    if (base >= 0x14000 && base < 0x16000) { asc_.write(addr & 0xFFF, v); return; }
    if (base >= 0x1E000 && base < 0x20000) {
        if (eclipse()) {
            iopTrace(true, 'w', base, v);
            swimPic_.hostWrite((base >> 1) & 0x1F, v);
            return;
        }
        if (cpu_) cpu_->stall(5);
        syncSwimFromCpu();
        swim_.write((base >> 9) & 0x0F, v);
        return;
    }
}

uint8_t Q700Memory::scsiDmaRead_() {
    if (!scsi_.drq()) busError(0x5000F100, false);
    uint8_t d = scsi_.dmaRead();
    scsiPoll_();
    return d;
}

void Q700Memory::scsiDmaWrite_(uint8_t v) {
    if (!scsi_.drq()) busError(0x5000F100, true);
    scsi_.dmaWrite(v);
    scsiPoll_();
}

void Q700Memory::scsiPoll_() {
    scsiIrq(scsi_.irq());
}

// POM68K JIT: the address map itself moved (overlay flip, ROM reload).
// No byte was written, so the write guard cannot see it — say so directly.
void Q700Memory::jitMapChanged() {
    if (jitGuard_) jitGuard_->invalidate();
    // J3: the interpreter's data window bypasses this map entirely, so a
    // remap the MMU cannot see (the boot overlay flip writes no ATC) has
    // to reach the CPU's DTLB directly — the guard above is only serviced
    // by the JIT engine's own loop, which the interpreter never enters.
    if (cpu_) cpu_->pomJitDtlbFlush();
}

const uint8_t* Q700Memory::codeSpan(uint32_t phys, uint32_t& len) const {
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

uint8_t* Q700Memory::dataSpan(uint32_t phys, uint32_t& len, bool write) {
    len = 0;
    if (phys < 0x40000000) {
        if (overlay_) return nullptr;          // this gigabyte is ROM for now
        if (phys >= totalRam_) return nullptr; // open bus, not memory
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

uint8_t Q700Memory::read8(uint32_t addr) {
    if (addr < 0x40000000) {
        if (overlay_) return rom_[addr & (kRomSize - 1)];
        if (addr < totalRam_) return ram_[addr];
        return 0xFF;
    }
    if (addr < 0x50000000) {
        if (overlay_) { overlay_ = false; jitMapChanged(); }           // rom_switch_r
        return rom_[addr & (kRomSize - 1)];
    }
    if (addr < 0x60000000) return ioRead8(addr);
    if (addr >= 0xF9000000 && addr < 0xF9200000)
        return vram_[(addr - 0xF9000000) & (kVramSize - 1)];
    if (addr >= 0xF9800000 && addr < 0xF9800400)
        return dafbRead8(addr - 0xF9800000);
    busError(addr, false);
}

uint16_t Q700Memory::read16(uint32_t addr) {
    if (addr < 0x40000000) {
        if (overlay_) {
            uint32_t o = addr & (kRomSize - 1);
            return uint16_t(rom_[o] << 8 | rom_[(o + 1) & (kRomSize - 1)]);
        }
        if (addr + 1 < totalRam_)
            return uint16_t(ram_[addr] << 8 | ram_[addr + 1]);
        return 0xFFFF;
    }
    if (addr < 0x50000000) {
        if (overlay_) { overlay_ = false; jitMapChanged(); }
        uint32_t o = addr & (kRomSize - 1);
        return uint16_t(rom_[o] << 8 | rom_[(o + 1) & (kRomSize - 1)]);
    }
    if (addr >= 0xF9000000 && addr < 0xF9200000 - 1) {
        uint32_t o = (addr - 0xF9000000) & (kVramSize - 1);
        return uint16_t(vram_[o] << 8 | vram_[(o + 1) & (kVramSize - 1)]);
    }
    if (addr >= 0x50000000 && addr < 0x60000000) {
        uint32_t base = (addr & 0x00FFFFFF) & 0x0003FFFF;
        // The Spike's SWIM1 hangs off the ODD byte lane, so a word read
        // there yields only its high half. On the Eclipse that window is
        // the SWIM IOP's host window, and MAME installs its handler on
        // BOTH lanes (`macquadra700.cpp:590-591`, umask $FF00FF00 AND
        // $00FF00FF) — the mirror of the write-side rule below. Keeping
        // the Spike rule here made `move.w (a2),d3` at ROM $408050CA read
        // the IOP's 16-bit shared-RAM address register as `hi<<8`: the ROM
        // read $0203 as $0200, decremented to $01FF and wrote its byte
        // into the IOP's STACK page, smashing the return address of the
        // routine at $53ED. The 65C02 then RTS'd to $0042 and BRK'd.
        if (!eclipse() && base >= 0x1E000 && base < 0x20000)
            return uint16_t(read8(addr) << 8);
    }
    return uint16_t(read8(addr) << 8 | read8(addr + 1));
}

void Q700Memory::write8(uint32_t addr, uint8_t v) {
    if (addr < 0x40000000) {
        if (overlay_) return;
        if (jitGuard_) jitGuard_->note(addr, 1);
        if (addr < totalRam_) ram_[addr] = v;
        return;
    }
    if (addr < 0x50000000) return;
    if (addr < 0x60000000) { ioWrite8(addr, v); return; }
    if (addr >= 0xF9000000 && addr < 0xF9200000) {
        vram_[(addr - 0xF9000000) & (kVramSize - 1)] = v;
        return;
    }
    if (addr >= 0xF9800000 && addr < 0xF9800400) {
        dafbWrite8(addr - 0xF9800000, v);
        return;
    }
    busError(addr, true);
}

void Q700Memory::write16(uint32_t addr, uint16_t v) {
    if (addr < 0x40000000 && !overlay_) {
        if (addr + 1 < totalRam_) {
            if (jitGuard_) jitGuard_->note(addr, 2);
            ram_[addr] = uint8_t(v >> 8);
            ram_[addr + 1] = uint8_t(v);
        }
        return;
    }
    if (addr >= 0xF9000000 && addr < 0xF9200000 - 1) {
        uint32_t o = (addr - 0xF9000000) & (kVramSize - 1);
        vram_[o] = uint8_t(v >> 8);
        vram_[(o + 1) & (kVramSize - 1)] = uint8_t(v);
        return;
    }
    if (addr >= 0x50000000 && addr < 0x60000000) {
        uint32_t base = (addr & 0x00FFFFFF) & 0x0003FFFF;
        // The Quadra 700's SWIM1 hangs off the ODD byte lane, so a word
        // write there delivers only its low half. On the Eclipse that same
        // window is the SWIM IOP's HOST window, whose 16-bit RAM-address
        // register the ROM writes with a single `move.w d0,(a2)`
        // (a2 = $5001E001, ROM $40804D38): high half → offset 0, low half →
        // offset 1. Applying the Spike quirk there dropped the high byte,
        // so every upload landed in the low 256 bytes, the ROM's 32 KB RAM
        // test failed, and the IOP was never released from /RSTPIC —
        // the boot then waited forever for ADB.
        if (!eclipse() && base >= 0x1E000 && base < 0x20000) {
            write8(addr + 1, uint8_t(v));
            return;
        }
    }
    write8(addr, uint8_t(v >> 8));
    write8(addr + 1, uint8_t(v));
}

uint8_t Q700Memory::peek8(uint32_t addr) const {
    if (addr < 0x40000000) {
        if (overlay_) return rom_[addr & (kRomSize - 1)];
        if (addr < totalRam_) return ram_[addr];
        return 0xFF;
    }
    if (addr < 0x50000000) return rom_[addr & (kRomSize - 1)];
    if (addr >= 0xF9000000 && addr < 0xF9200000)
        return vram_[(addr - 0xF9000000) & (kVramSize - 1)];
    return 0xFF;
}

void Q700Memory::tick(int cpuCycles) {
    // VIA1 φ2 is the board's fixed 783.36 kHz E clock, not a divisor of
    // the CPU — an integer ratio is an approximation here (ViaEClock.h).
    const int viaCycles = viaEClock_.advance(cpuCycles, cpuHz_);
    if (viaCycles) {
        bool irq = via1_.tick(viaCycles);
        irq |= via2_.tick(viaCycles);
        if (irq) updateIrq();
    }

    if (eclipse()) {
        // Both IOPs run on C15M; AdbLine's device timers were rebased onto
        // that same domain. The Egret keeps the CPU clock (µs pacing).
        const int c15 = int(int64_t(cpuCycles) * 15667200 / cpuHz_);
        sccPic_.tick(c15);
        swimPic_.tick(c15);
        adbLine_.tick(c15);
        egret_.tick(cpuCycles);
        refreshVia1PortB();
    } else {
        adbVia_.tick(cpuCycles);
        if (cpu_) adbVia_.syncTo(cpu_->machineClock());
    }

    scc_.tick(cpuCycles);
    if (eclipse()) {
        // On the Eclipse the board's SCC interrupt line belongs to the SCC
        // IOP's HOST interrupt, not to the SCC chip: the chip's /INT is a
        // PERIPHERAL interrupt into the IOP (macquadra700.cpp:867-870).
        // Letting the Q700's line survive here re-derived `sccIrq_` from
        // the raw chip every tick and wiped the IOP's interrupt the moment
        // it was raised — the boot then waited forever on an
        // `_IOPMsgRequest` that had, in fact, been answered.
        sccPic_.pintW(scc_.irqAsserted());
    } else if (sccIrq_ != scc_.irqAsserted()) {
        sccIrq_ = scc_.irqAsserted();
        updateIrq();
    }

    asc_.tick(cpuCycles);
    syncSwimFromCpu();
    drive0_.tick(cpuCycles);
    drive1_.tick(cpuCycles);

    scsi_.tick(cpuCycles);
    scsiPoll_();

    // 60.15 Hz to VIA1 CA1. On real hardware VIA2's T1 drives PB7 and the
    // board chains PB7 → VIA1 CA1 (via2_out_b "chain 60.15 Hz to VIA1");
    // POM68K generates the tick directly, as it does on the IOSB machines
    // (LLE-simplified: the rate is right, the chain is not modelled).
    tickAcc_ += int64_t(cpuCycles) * 6015;
    if (tickAcc_ >= cpuHz_ * 100) {
        tickAcc_ -= cpuHz_ * 100;
        via1_.raiseCa1();
        updateIrq();
    }

    secAcc_ += cpuCycles;
    if (secAcc_ >= cpuHz_) {
        secAcc_ -= cpuHz_;
        // Eclipse has no discrete RTC — the Egret keeps time (it runs its
        // own second counter in tick()) and there is no CA2 tick to chain.
        if (!eclipse()) {
            rtc_.tickSecond();
            via1_.raiseCa2();
            updateIrq();
        }
    }

    dafbCell_.tick(cpuCycles);
}
