// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "PgePmu.h"
#include "Via6522.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

static constexpr int64_t kMcuHz = 2097152;           // 4.194304 MHz / 2

PgePmu::PgePmu(Via6522& via1, int64_t cpuHz)
    : mcu_(std::make_unique<M68hc05Pge>()), via_(via1), cpuHz_(cpuHz) {
    wirePorts();
}

bool PgePmu::loadBootRom(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::vector<uint8_t> fw((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
    bootLoaded_ = mcu_->loadBootRom(fw);
    return bootLoaded_;
}

bool PgePmu::loadBatteryId(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    in.read(reinterpret_cast<char*>(ds2400_.data), 8);
    ds2400_.loaded = in.gcount() == 8;
    return ds2400_.loaded;
}

// ── DS2400 1-Wire slave (MAME ds2401.cpp, cycles @2.097152/µs) ─────────
static constexpr int64_t kUs = 2;                    // ≈2.097 cycles per µs
static constexpr int64_t T_SAMP = 30 * kUs, T_RDV = 30 * kUs;
static constexpr int64_t T_RSTL = 480 * kUs, T_PDH = 30 * kUs, T_PDL = 120 * kUs;

void PgePmu::Ds2400::write(bool level, int64_t now) {
    if (!level && rx) {                              // falling edge
        switch (state) {
        case Command:
            mainAt = now + T_SAMP;                   // sample the slot later
            break;
        case ReadRom:
            if (!bit) shift = data[7 - byte];        // MAME byte order
            tx = shift & 1;                          // LSB first; 0 holds low
            shift >>= 1;
            if (++bit == 8) { bit = 0; byte++; }
            mainAt = now + T_RDV;                    // release after 30 µs
            break;
        default: break;
        }
        resetAt = now + T_RSTL;                      // arm reset detect
    } else if (level && !rx) {                       // rising edge
        if (state == Reset) {
            state = Reset1;
            mainAt = now + T_PDH;
        }
        resetAt = -1;
    }
    rx = level;
}

void PgePmu::Ds2400::tick(int64_t now) {
    if (resetAt >= 0 && now >= resetAt) {            // line low ≥ 480 µs
        state = Reset;
        resetAt = -1;
    }
    if (mainAt < 0 || now < mainAt) return;
    mainAt = -1;
    switch (state) {
    case Reset1:                                     // presence pulse start
        tx = false;
        state = Reset2;
        mainAt = now + T_PDL;
        break;
    case Reset2:                                     // presence done
        tx = true;
        bit = 0; shift = 0; byte = 0;
        state = Command;
        break;
    case Command:
        shift >>= 1;
        if (rx) shift |= 0x80;                       // sample line at +30 µs
        if (++bit == 8) {
            if (shift == 0x33 || shift == 0x0F) {    // READ ROM
                bit = 0; byte = 0;
                state = ReadRom;
            } else {
                state = Idle;
            }
        }
        break;
    case ReadRom:
        tx = true;                                   // read-slot window ends
        if (byte == 8) state = Idle;
        break;
    default: break;
    }
}

void PgePmu::reset() {
    mcu_->reset();
    mcuAcc_ = 0;
    mcuDebt_ = 0;
    held_ = true;
    porteBit2_ = false;
    ackLevel_ = true;
    reqLevel_ = true;
    lastPortE_ = lastPortF_ = lastPortG_ = lastPortC_ = 0xFF;
    adb_.reset();
    lastMosi_ = false;
}

// Host side of the handshake: the guest writes /PMU_REQ on pseudo-VIA2
// port B bit 2 (msc.cpp via2_out_b). MAME also spins the 68030 80 µs on
// each edge as transport pacing — deliberately not modelled; the SPI
// completion IRQ is what the protocol really waits on.
void PgePmu::setPmuReq(bool level) {
    static const bool trace = std::getenv("POM68K_PGE_TRACE") != nullptr;
    static const bool hshake = std::getenv("POM68K_PGE_HSHAKE") != nullptr;
    if ((trace || hshake) && level != reqLevel_) {
        static long n = 0;
        if (n++ < 100000)
            std::fprintf(stderr, "hs: REQ -> %d (ack=%d mcuPc=$%04X cyc=%lld)\n",
                         level ? 1 : 0, ackLevel_ ? 1 : 0, mcu_->pc(),
                         (long long)mcu_->cycleCount());
    }
    if (level != reqLevel_)                          // 80 µs of machine time
        hostSpin_ = int(cpuHz_ * 80 / 1000000);
    reqLevel_ = level;
}

void PgePmu::wirePorts() {
    M68hc05Pge& m = *mcu_;
    m.setPullups(M68hc05Pge::C, 0xFF);               // driver :788
    m.setPullups(M68hc05Pge::E, 0x80);               // bit 7 = 1-Wire bus

    // The DS2400 timers run on the MCU's own instruction clock — the
    // event-driven-wire pattern (M68hc05::onCycles) the Egret ADB uses.
    m.onCycles = [this](int) { ds2400_.tick(mcu_->cycleCount()); };

    // SPI master → the MSC VIA1 shifter (SCK on CB1, MOSI on CB2, MISO
    // from the SR MSB). Clock edges must NOT raise IFR.CB1 — that is the
    // mscvia write_cb1_noint contract, and extShiftCB1 already honours it
    // (only IFR.SHIFT fires, on the 8th bit).
    m.spiMosi = [this](bool b) { lastMosi_ = b; };
    m.spiClock = [this](bool level) {
        via_.extShiftCB1(level, lastMosi_);
        // NO CB1 interrupt here — this is msc.h's `write_cb1_noint`
        // contract (msc.h:34-48), and it is load-bearing. MAME's
        // macpwrbkmsc binds the SPI clock to the INTERRUPTING `cb1_w`, but
        // with that wiring every shift edge sets IFR.CB1, the driver
        // re-enables IER at the end of each transaction with the flag
        // still pending, and its ISR issues readINT ($78) forever: the
        // machine burns 100 % of its time in PMU comms and never selects a
        // SCSI target. Measured, same build, 5 G cycles: interrupt half ON
        // → 176 back-to-back $78 and 0 SCSI selects; OFF → 1122 selects,
        // 555 commands, and the boot proceeds. The PMU's real interrupt is
        // its port F bit 2 (pmu_int → INT_CB1), which msc.h models as a
        // separate path precisely because the shift clock must not do it.
        // POM68K_PGE_CB1INT=1 restores the MAME-literal wiring for A/B.
        static const bool cb1Int = std::getenv("POM68K_PGE_CB1INT") != nullptr;
        if (cb1Int) via_.extCb1Int(level);
        // POM68K_PGE_CB1BYTE=1: third option under test — one CB1 interrupt
        // per completed BYTE (the SR interrupt re-pointed at the bit the
        // driver actually enables) instead of one per edge or none.
        static const bool cb1Byte = std::getenv("POM68K_PGE_CB1BYTE") != nullptr;
        if (cb1Byte && via_.takeShiftDone()) via_.raiseCb1();
        // MISO is driven ONLY while the VIA shifts OUT: MAME wires the
        // VIA's cb2 OUTPUT callback to spi_miso_w, and stock 6522
        // shift_out() is the only caller (6522via.cpp:446). Feeding the SR
        // MSB back in shift-IN mode makes the PMU read its own bytes one
        // exchange later — the echo pattern ("D9 out, D9 in") that had the
        // firmware talking to itself instead of to the host.
        if (((via_.acr() >> 2) & 7) == 7)
            mcu_->spiMisoIn(via_.extShiftCB2Out());
    };

    // ADB modem cell → the command-level bus (keyboard $2, mouse $3).
    // OFF by default, POM68K_PGE_ADB=1 to arm. The System's ADBReInit
    // cannot enumerate anything with an inert cell (that is where the boot
    // currently stops, docs/DUO_BRINGUP.md), but this first attempt at
    // answering is WORSE, not better: it regresses the boot from 1122 SCSI
    // selects to zero, i.e. the ROM's own early ADBReInit now goes wrong
    // too. The byte framing between ADBDR/RDRF and a command-level bus is
    // the thing to get right — how many reply bytes the firmware expects
    // per Talk, and how Listen data bytes are sequenced — so the knob
    // stays here as the experiment's handle rather than a default.
    if (std::getenv("POM68K_PGE_ADB"))
        m.adbCommand = [this](uint8_t cmd) { return adb_.command(cmd, {}); };

    m.adcIn = [](int ch) -> uint8_t {
        // Fixed board values (macpwrbkmsc.cpp:473-496): healthy battery,
        // ~24 °C. ch0 bat-low, ch1 bat-high, ch2 current, ch3/4 temps.
        switch (ch) {
            case 0: return 0xFF;
            case 1: return 0x7F;
            case 2: return 0x40;
            case 3: case 4: return 131;
            default: return 0xFF;
        }
    };

    // POM68K_PGE_TRACE=1: dump every port access with the stub's PC —
    // the tool that shows what the boot ROM actually polls.
    static const bool trace = std::getenv("POM68K_PGE_TRACE") != nullptr;

    m.readPort = [this](int p) -> uint8_t {
        M68hc05Pge& mc = *mcu_;
        if (trace && p != M68hc05Pge::A && p != M68hc05Pge::B) {
            static long n = 0;
            static const char* names = "ABCDEFGHJKL";
            if (n++ < 2000)
                std::fprintf(stderr, "pge: rd P%c pc=$%04X (req=%d)\n",
                             names[p], mc.pc(), reqLevel_ ? 1 : 0);
        }
        switch (p) {
        case M68hc05Pge::A:
            // With NO matrix row selected (port C latch all-ones) port A is
            // the POWER-KEY pseudo-row: `0xdf | (powerKey << 5)`
            // (macpwrbkmsc.cpp pmu_porta_r). MEASURED: bit 5 must read 1
            // (all-ones) for the key to count as UP — returning the literal
            // $DF instead hangs the boot dead at $408B98F2 with Ticks
            // frozen at 0, i.e. the firmware reads $DF as "power key held"
            // and never starts the machine. So $FF here is not laziness;
            // it is the released state. Matrix columns X0-X7 are likewise
            // active low, no key = all ones (real input is milestone 4).
            return 0xFF;
        case M68hc05Pge::B:
            return 0xFF;                             // modifiers + X8-X10
        case M68hc05Pge::C:
            return 0xFF;                             // row select readback
        case M68hc05Pge::D:
            // bit 7 = 2nd mouse button up, bit 6 = dock ABSENT (no dock
            // hardware yet), bit 4 = US keyboard (pmu_portd_r).
            return 0x80 | 0x40 | 0x10;
        case M68hc05Pge::E:
            // bit 7 = 1-Wire read: DS2400 battery serial (wired-AND with
            // the master's own drive, ds2401.cpp read()).
            return uint8_t((lastPortE_ & 0x7F) |
                           (ds2400_.read() ? 0x80 : 0x00));
        case M68hc05Pge::F:
            // +5V present (bit 2), PFW ok (bit 1), clamshell open
            // (bit 3), /PMU_REQ mirrored on bit 6 (pmu_portf_r).
            return uint8_t(0x0E | (reqLevel_ ? 0x40 : 0x00));
        case M68hc05Pge::G:
            return 0x40 | 0x08;                      // charger + dock power
        case M68hc05Pge::H:
        case M68hc05Pge::J:
        case M68hc05Pge::K:
        case M68hc05Pge::L:
        default:
            return 0xFF;
        }
        (void)mc;
    };

    m.writePort = [this](int p, uint8_t v) {
        if (trace && p != M68hc05Pge::C) {
            static long n = 0;
            static const char* names = "ABCDEFGHJKL";
            if (n++ < 2000)
                std::fprintf(stderr, "pge: wr P%c=$%02X pc=$%04X\n",
                             names[p], v, mcu_->pc());
        }
        switch (p) {
        case M68hc05Pge::C:
            lastPortC_ = v;          // row select; $FF = power-key row
            break;
        case M68hc05Pge::E:
            // bit 2 = MSC /reset: the PMU releasing the 68030. The first
            // change also drops the power-on HALT (pmu_porte_w:433-441).
            if (((v ^ lastPortE_) & 0x04) != 0) held_ = false;
            porteBit2_ = (v & 0x04) != 0;
            if (((v ^ lastPortE_) & 0x02) != 0 && onDisplayBlank)
                onDisplayBlank((v & 0x02) != 0);
            // bit 7 = 1-Wire master drive → the DS2400 slave.
            if (((v ^ lastPortE_) & 0x80) != 0)
                ds2400_.write((v & 0x80) != 0, mcu_->cycleCount());
            lastPortE_ = v;
            break;
        case M68hc05Pge::F:
            // bit 2 falling = PMU interrupt to the host: the MSC VIA1's
            // customized INT_CB1 (msc.h:19-26 pmu_int).
            if ((lastPortF_ & 0x04) && !(v & 0x04)) {
                pmuIntEdges++;
                via_.raiseCb1();
            }
            lastPortF_ = v;
            break;
        case M68hc05Pge::G:
            // bit 5: 1 = master clock off (sleep); 1→0 = wake — the
            // machine clears HALT, pulses the CPU reset and re-arms the
            // ROM overlay (pmu_portg_w + msc pmu_reset_w).
            if ((lastPortG_ & 0x20) && !(v & 0x20) && onWake) onWake();
            lastPortG_ = v;
            break;
        case M68hc05Pge::H:
            // bit 6 = /PMU_ACK → pseudo-VIA2 port B bit 1.
            if (((v >> 6) & 1) != (ackLevel_ ? 1 : 0)) {
                pmuAckEdges++;
                mcu_->spinCycles(20 * 2097152 / 1000000);   // 20 µs (MAME)
                static const bool ht = std::getenv("POM68K_PGE_HSHAKE") != nullptr;
                if (ht) {
                    static long n = 0;
                    if (n++ < 100000)
                        std::fprintf(stderr, "hs: ACK -> %d (req=%d mcuPc=$%04X "
                                     "cyc=%lld)\n", (v >> 6) & 1, reqLevel_ ? 1 : 0,
                                     mcu_->pc(), (long long)mcu_->cycleCount());
                }
            }
            ackLevel_ = (v & 0x40) != 0;
            break;
        default:
            break;                                   // J/L: DFAC + rails
        }
    };
}

void PgePmu::tick(int cpuCycles) {
    // Machine cycles → 2.097 MHz MCU cycles, carrying run()'s overshoot
    // as debt (pom68k-mcu-lle-clock-drift: without it the MCU overclocks
    // ~37 % and the RTC drifts).
    mcuAcc_ += int64_t(cpuCycles) * kMcuHz;
    int mcuCyc = int(mcuAcc_ / cpuHz_);
    mcuAcc_ -= int64_t(mcuCyc) * cpuHz_;
    mcuCyc -= mcuDebt_;
    if (mcuCyc > 0) {
        int used = mcu_->run(mcuCyc);
        mcuDebt_ = used > mcuCyc ? used - mcuCyc : 0;
    } else {
        mcuDebt_ = -mcuCyc;
    }
}
