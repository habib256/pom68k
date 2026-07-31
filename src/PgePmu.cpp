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
    lastPortE_ = lastPortF_ = lastPortG_ = 0xFF;
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
        via_.extCb1Int(level);       // stock write_cb1's interrupt half
        // MISO is driven ONLY while the VIA shifts OUT: MAME wires the
        // VIA's cb2 OUTPUT callback to spi_miso_w, and stock 6522
        // shift_out() is the only caller (6522via.cpp:446). Feeding the SR
        // MSB back in shift-IN mode makes the PMU read its own bytes one
        // exchange later — the echo pattern ("D9 out, D9 in") that had the
        // firmware talking to itself instead of to the host.
        if (((via_.acr() >> 2) & 7) == 7)
            mcu_->spiMisoIn(via_.extShiftCB2Out());
    };

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
            // Row select == 0: the power-key pseudo-row (pmu_porta_r);
            // otherwise keyboard matrix columns X0-X7. No key pressed
            // reads all-ones (active low). Matrix input is milestone 4.
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
