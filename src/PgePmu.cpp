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

// ── The Duo's built-in keyboard matrix ──────────────────────────────────
// One entry per key the PowerBook Duo physically has, transcribed from
// MAME's Y0-Y7 / keyb_special port tables (macpwrbkmsc.cpp:647-757). The
// index is the Mac VIRTUAL key code — the code every POM68K machine takes
// from the host — and the value is the column bit inside its row. The Duo
// keyboard has no F1-F3 and no keypad beyond Enter; codes with no cell here
// are dropped rather than mapped to a neighbour.
namespace {

struct DuoKey { uint8_t vk; uint8_t row; uint8_t bit; };

constexpr DuoKey kDuoMatrix[] = {
    // Y0
    { 0x30, 0, 0 }, { 0x0D, 0, 1 }, { 0x0F, 0, 2 }, { 0x10, 0, 3 },
    { 0x22, 0, 4 }, { 0x23, 0, 6 }, { 0x1E, 0, 7 }, { 0x7E, 0, 9 },
    // Y1
    { 0x07, 1, 1 }, { 0x02, 1, 2 }, { 0x05, 1, 3 }, { 0x26, 1, 4 },
    { 0x25, 1, 5 }, { 0x27, 1, 7 }, { 0x7D, 1, 9 },
    // Y2 — bits 1 and 3 are the brightness keys (F4/F5 on MAME's layout)
    { 0x35, 2, 0 }, { 0x76, 2, 1 }, { 0x60, 2, 3 }, { 0x09, 2, 4 },
    { 0x2E, 2, 7 }, { 0x4C, 2, 8 }, { 0x31, 2, 9 },
    // Y3 — bits 1 and 3 are the contrast keys (F6/F7)
    { 0x61, 3, 1 }, { 0x08, 3, 2 }, { 0x62, 3, 3 }, { 0x0B, 3, 5 },
    { 0x2D, 3, 6 }, { 0x2B, 3, 7 }, { 0x7B, 3, 9 },
    // Y4
    { 0x00, 4, 0 }, { 0x01, 4, 1 }, { 0x03, 4, 3 }, { 0x04, 4, 4 },
    { 0x28, 4, 5 }, { 0x29, 4, 6 }, { 0x2F, 4, 7 }, { 0x2C, 4, 8 },
    { 0x7C, 4, 9 },
    // Y5
    { 0x0C, 5, 0 }, { 0x13, 5, 1 }, { 0x15, 5, 2 }, { 0x16, 5, 3 },
    { 0x1C, 5, 5 }, { 0x1D, 5, 6 }, { 0x18, 5, 8 },
    // Y6
    { 0x06, 6, 0 }, { 0x32, 6, 1 }, { 0x0E, 6, 2 }, { 0x11, 6, 3 },
    { 0x20, 6, 4 }, { 0x1F, 6, 5 }, { 0x21, 6, 6 }, { 0x2A, 6, 8 },
    { 0x24, 6, 9 },
    // Y7
    { 0x12, 7, 1 }, { 0x14, 7, 2 }, { 0x17, 7, 3 }, { 0x1A, 7, 4 },
    { 0x19, 7, 5 }, { 0x1B, 7, 6 }, { 0x33, 7, 8 },
};

// keyb_special, the five modifiers the scanner reads on port B bits 3-7.
constexpr DuoKey kDuoModifiers[] = {
    { 0x37, 0, 3 },              // Command
    { 0x3B, 0, 4 },              // Control
    { 0x38, 0, 5 },              // Shift
    { 0x3A, 0, 6 },              // Option
    { 0x39, 0, 7 },              // Caps Lock
};

}  // namespace

void PgePmu::keyEvent(uint8_t code, bool down) {
    if (code == 0x7F) { powerKey_ = down; return; }   // ADB power key
    for (const DuoKey& k : kDuoModifiers) {
        if (k.vk != code) continue;
        if (down) modifiers_ |= uint8_t(1u << k.bit);
        else modifiers_ &= uint8_t(~(1u << k.bit));
        return;
    }
    for (const DuoKey& k : kDuoMatrix) {
        if (k.vk != code) continue;
        if (down) matrix_[k.row] |= uint16_t(1u << k.bit);
        else matrix_[k.row] &= uint16_t(~(1u << k.bit));
        return;
    }
    // Not on this keyboard. An external Duo keyboard would come down the
    // ADB cell instead, which is where this used to go unconditionally —
    // and where nothing ever arrived, because the cell enumerates no
    // devices (M68hc05Pge's adbCommand note).
}

// ── The trackball ───────────────────────────────────────────────────────
// Same split as the keyboard: the Duo's built-in pointer is wired to the
// PG&E's quadrature decoder, not to the ADB bus, so motion has to arrive
// through TBCS/TBX/TBY ($14-$16) for the firmware to see it at all. Sent
// down the ADB modem cell instead — which is where it went until
// 2026-08-14 — the guest's own Mouse global never moves once.
// Screen convention throughout: +x right, +y down, exactly what the
// counters carry (measured on the guest's pointer, all four directions).
static bool pgeAdbMouse() {
    const char* e = std::getenv("POM68K_PGE_ADBMOUSE");
    return e && e[0] != '0';
}

void PgePmu::mouseMove(int dx, int dy) {
    if (pgeAdbMouse()) { adb_.mouseMove(dx, dy); return; }
    tbAccX_ += dx;
    tbAccY_ += dy;                   // +y is DOWN, both here and on screen
}

// One frame's worth of accumulated motion becomes what the firmware reads,
// and stays that for the whole frame.
void PgePmu::tbLatch() {
    auto take = [](int& acc) {
        int d = acc < -127 ? -127 : (acc > 127 ? 127 : acc);
        acc -= d;
        return uint8_t(int8_t(d));
    };
    tbRegX_ = take(tbAccX_);
    tbRegY_ = take(tbAccY_);
}

void PgePmu::mouseButton(bool down) {
    if (pgeAdbMouse()) { adb_.mouseButton(down); return; }
    tbButton_ = down;
}

void PgePmu::reset() {
    mcu_->reset();
    // A reset here is the machine being POWERED ON, and the PG&E's own RAM
    // survives it — which is the whole difficulty. $91 is the power flag,
    // and the mask ROM's first decision (LDA $91 / CMP #$62 at $FE28) is
    // which way to go on it: cold boot, or resume. Left at $62 by the
    // session that just ended, the ROM takes the RESUME path, STOPs at
    // $FE0D waiting for a wake event a reset never sends, and the 68030 is
    // never released — measured, 400 000 ticks still held, PG&E pc=$FE0E,
    // waiting=1. MAME clears the same byte for the same reason every time
    // it restores this MCU's NVRAM (m68hc05pge.cpp:959, "clear power flag
    // so the boot ROM does a cold boot"), and MscMemory::loadPram already
    // copies that rule; a reset needs it exactly as much.
    mcu_->setRamByte(M68hc05Pge::kPowerFlagAddr - 0x40, 0);
    for (auto& r : matrix_) r = 0;
    modifiers_ = 0;
    powerKey_ = false;
    tbAccX_ = tbAccY_ = 0;
    tbRegX_ = tbRegY_ = 0;
    tbAcc_ = 0;
    tbButton_ = false;
    clamshellOpen_ = true;
    mcuAcc_ = 0;
    mcuDebt_ = 0;
    held_ = true;
    porteBit2_ = false;
    ackLevel_ = true;
    reqLevel_ = true;
    lastPortE_ = lastPortF_ = lastPortG_ = lastPortC_ = 0xFF;
    lastPortH_ = 0x00;       // DFAC-reset bit must start low (mac...:129)
    adb_.reset();
    ds2400_.reset();
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
    // MAME's 80 µs host stall (via2_out_b). POM68K_PGE_SPINUS is the bisect
    // knob — a stall that long could equally make the host sleep straight
    // through the PMU's ACK window, so the length is settable and 0 drops it
    // entirely. (An earlier POM68K_PGE_NOSPIN=1 was documented here long
    // after it stopped being read by anything. 2026-08-12)
    // Measured 2026-07-31: WITHOUT it the machine does not boot at all
    // (ADBReInit #1 never completes, 0 SCSI selects) — MAME's stall is
    // load-bearing, exactly as the Cuda transport experience predicts.
    // POM68K_PGE_SPINUS overrides the length for phase experiments; 0
    // disables it.
    static const int spinUs = std::getenv("POM68K_PGE_SPINUS")
                            ? atoi(std::getenv("POM68K_PGE_SPINUS")) : 80;
    if (level != reqLevel_ && spinUs > 0)
        hostSpin_ = int(cpuHz_ * spinUs / 1000000);
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
    // Command level is honest here: the cell IS a hardware transceiver, so
    // the wire lives inside the PG&E's silicon and the firmware only ever
    // exchanges bytes with it. MAME's m68hc05pge leaves the cell inert
    // (TDRE/TC only, never RDRF), so its ADBReInit cannot enumerate at all.
    // POM68K_PGE_ADB=0 disables it for A/B.
    if (!std::getenv("POM68K_PGE_ADB") ||
        std::getenv("POM68K_PGE_ADB")[0] != '0')
        m.adbCommand = [this](uint8_t cmd, const std::vector<uint8_t>& d) {
            return adb_.command(cmd, d);
        };

    // Trackball quadrature counters ($14-$16): the LATCHED registers, held
    // steady for a whole 60 Hz frame — never a live drain, see tbLatch().
    m.tbX = [this] { return tbRegX_; };
    m.tbY = [this] { return tbRegY_; };
    m.tbButton = [this] { return tbButton_; };

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
            // it is the released state.
            if (matrixRow() < 0)
                return uint8_t(0xDF | (powerKey_ ? 0x00 : 0x20));
            return uint8_t(~uint8_t(matrix_[matrixRow()] & 0xFF));
        case M68hc05Pge::B:
            // Modifiers on bits 3-7, matrix columns X8-X10 on bits 0-2
            // (pmu_portb_r). Both active low.
            return uint8_t((~modifiers_ & 0xF8) |
                           (matrixRow() < 0
                                ? 0x07
                                : uint8_t(~(matrix_[matrixRow()] >> 8) & 0x07)));
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
            // (bit 3), /PMU_REQ mirrored on bit 6 (pmu_portf_r). MAME
            // hard-wires the lid open; here it is a line the host can
            // drive, and driving it is the sleep milestone's first
            // instrument. What it does TODAY, measured 2026-08-14:
            // closing it holds the 68030 within 5 s — so the firmware
            // does act on the switch — but the System never runs its
            // sleep procs first (the volume's dirty bit is untouched and
            // not one write reaches the disk), and re-opening does not
            // bring the machine back. Both halves belong to
            // docs/DUO_BRINGUP.md's sleep/wake milestone; the default is
            // open, so nothing reaches this path unless a caller asks.
            return uint8_t(0x06 | (clamshellOpen_ ? 0x08 : 0x00) |
                           (reqLevel_ ? 0x40 : 0x00));
        case M68hc05Pge::G: {
            // bit 6 = charger present (MAME pmu_portg_r returns 1). The
            // BORG firmware's charge-management pass against our CONSTANT
            // ADC readings is under investigation as the ADB-starvation
            // culprit; POM68K_PGE_CHARGER=0 unplugs the charger for A/B.
            static const bool charger = !std::getenv("POM68K_PGE_CHARGER")
                || std::getenv("POM68K_PGE_CHARGER")[0] != '0';
            return uint8_t((charger ? 0x40 : 0x00) | 0x08);
        }
        case M68hc05Pge::H:
            // Read-back of the write latch, NOT open-bus: MAME pmu_porth_r
            // returns m_last_porth (macpwrbkmsc.cpp:543-546), and its $00
            // start value is a precondition for the boot ROM's DFAC config
            // (bit 0 = DFAC reset, :129). $FF here reads as "DFAC already
            // out of reset" and the config pass is skipped.
            return lastPortH_;
        case M68hc05Pge::J:
        case M68hc05Pge::K:
        case M68hc05Pge::L:
        default:
            // MAME-parity audit §2.12 (cosmetic, DOCUMENT-SKIP 2026-08-06;
            // this is the RESIDUE of "unwired inputs read $FF vs 0" after
            // the port-H fix). MAME reads these as 0, not $FF: the PG&E's
            // port callbacks default to `m_read_p(*this, 0)`
            // (m68hc05pge.cpp:115) and macpwrbkmsc.cpp binds NO pmu_portj_r
            // / pmu_portk_r / pmu_portl_r — only the J and L writes
            // (:557-567). ports_r then keeps the latch for output bits and
            // takes the callback's 0 for input bits (:251-260).
            //
            // Not aligned, but NOT because it is unreachable — measured on
            // roms/pge/pge_boot.bin, the boot ROM writes DDRJ = $D0 (at
            // $051-$054) and DDRL = $07 (at $055-$058), leaving port J bits
            // 5 and 3-0 and port L bits 7-3 configured as INPUTS, and it
            // then does BSET/BCLR 7,$28 and 6,$28 ($15B-$172), which are
            // read-modify-writes that read those input bits back. The
            // difference stays inside `ports_[J]` (sendPort masks with the
            // DDR, so no pin output changes) unless the uploaded BORG
            // firmware branches on a port J/K/L input bit — which is
            // exactly the shape of the port-H bug that wave 2 fixed.
            //
            // It is left open because the only coverage of this path is
            // `duo230_boot_etalon` (a full cold boot), the port-H incident
            // proved these defaults are load-bearing and cheap to get
            // wrong, and $FF is the value the Duo currently boots on. Note
            // that the port-H RULE does not transfer: port H reads back its
            // write latch because MAME binds pmu_porth_r to m_last_porth;
            // J/K/L have no read handler at all, so aligning means literal
            // 0x00 for input bits, not a latch.
            // Reopening condition: flip these to 0x00, run
            // duo230_boot_etalon, and keep it only if it stays green — do
            // it when the keyboard matrix / power key wiring lands
            // (docs/DUO_BRINGUP.md, "Next: input through the PMU"), since
            // that pass touches these ports anyway.
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
            if (((v ^ lastPortE_) & 0x04) != 0) {
                held_ = false;
                // Rising edge = /RESET released: MAME calls pmu_reset_w(0)
                // which re-arms the ROM overlay and restarts the CPU from
                // the reset vector (msc.cpp pmu_reset_w:363-378) — without
                // it a BORG-commanded reboot resumes stale state.
                if ((v & 0x04) && onCpuReset) onCpuReset();
            }
            porteBit2_ = (v & 0x04) != 0;
            if (((v ^ lastPortE_) & 0x02) != 0 && onDisplayBlank)
                onDisplayBlank((v & 0x02) != 0);
            // bit 7 = 1-Wire master drive → the DS2400 slave.
            if (((v ^ lastPortE_) & 0x80) != 0)
                ds2400_.write((v & 0x80) != 0, mcu_->cycleCount());
            lastPortE_ = v;
            break;
        case M68hc05Pge::F:
            // bit 2 = /PMU_INT to the host — a LEVEL into the MSC VIA1's
            // customized INT_CB1 (msc.h:19-26 pmu_int), not just a falling
            // edge. The PMU driver masks IER.CB1 and acks stale flags
            // around every PmgrOp; a cause asserted inside that window
            // must still be pending when IER returns, which only level
            // semantics provide (the edge-only model deadlocked the
            // System-era ADB SendReset against the CPI second-tick).
            if (((lastPortF_ ^ v) & 0x04) != 0) {
                if (!(v & 0x04)) pmuIntEdges++;
                via_.pmuIntLevel(!(v & 0x04));
            }
            {
                static const bool ht =
                    std::getenv("POM68K_PGE_HSHAKE") != nullptr;
                if (ht && ((lastPortF_ ^ v) & 0x04)) {
                    static long n = 0;
                    if (n++ < 100000)
                        std::fprintf(stderr, "hs: INT line -> %d cyc=%lld "
                                     "pc=$%04X\n", (v >> 2) & 1,
                                     (long long)mcu_->cycleCount(),
                                     mcu_->pc());
                }
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
            lastPortH_ = v;                          // pmu_porth_w:548-553
            break;
        default:
            break;                                   // J/L: DFAC + rails
        }
    };
}

int PgePmu::cyclesToNextEvent() const {
    const int64_t need = (int64_t(mcuDebt_) + 1) * cpuHz_ - mcuAcc_;
    if (need <= 0) return 1;
    const int64_t cycles = (need + kMcuHz - 1) / kMcuHz;
    return int(cycles > 0 ? cycles : 1);
}

void PgePmu::tick(int cpuCycles) {
    // Trackball registers refresh at the board's 60 Hz, the cadence MAME
    // recomputes them on (macpwrbkmsc.cpp vbl_w) — see tbLatch().
    tbAcc_ += int64_t(cpuCycles) * 60;
    if (tbAcc_ >= cpuHz_) {
        tbAcc_ %= cpuHz_;
        tbLatch();
    }
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
