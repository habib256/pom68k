// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Cuda firmware-LLE glue — see CudaLle.h for the signal map and oracle
// references (MAME mame/apple/cuda.cpp + m68hc05e1.cpp).

#include "CudaLle.h"
#include "Via6522.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

CudaLle::CudaLle(Via6522& via, int64_t cpuHz, Flavor flavor)
    : via_(via), cpuHz_(cpuHz), flavor_(flavor)
{
    mcu_.readPort = [this](int p) { return mcuPortRead(p); };
    mcu_.writePort = [this](int p, uint8_t v) { mcuPortWrite(p, v); };
    // Event-driven wire (TODO step 6): AdbLine's clock is SLAVED to the
    // MCU's instruction stream — every instruction advances the wire by
    // the equivalent ADB-domain cycles, so the firmware's bit-banged
    // receive loop samples device edges at exact instruction boundaries.
    // The batch-frozen wire quantized the 35/65 µs cells to the machine
    // tick (~8 µs); the Cuda ROM's receive tolerated the skew, the Egret
    // ROM's mis-heard device bytes as zeros (~1.5% mouse delivery).
    // Crucially the MCU's own scheduling vs the host VIA is UNCHANGED —
    // the boot-time PC3/VIA lockstep phase proved load-bearing when the
    // two slicing experiments (uniform 2 µs interleave, busy-gated)
    // moved it and crashed the guest / silenced the mouse.
    mcu_.onCycles = [this](int cyc) {
        adbAcc_ += int64_t(cyc) * kAdbHz;
        const int adbCyc = int(adbAcc_ / kMcuHz);
        adbAcc_ -= int64_t(adbCyc) * kMcuHz;
        if (adbCyc) adb_.tick(adbCyc);
    };
    if (flavor_ == Flavor::Cuda) {
        mcu_.setPullups(1, 0xC0);        // PB6/7 I2C pull-ups (cuda.cpp:88)
        mcu_.setPullups(2, 0x04);        // PC2 NMI pull-up (cuda.cpp:89)
        // The real Cuda's PA0 (PFW) is hard-wired as an input — the
        // firmware sets its DDR bit as output, which on a stock E1 would
        // drive PFW low and read back "power failing" (the boot then parks
        // in the shutdown wait, ADB line low). MAME's "cudapfw" write tap,
        // cuda.cpp:146-152.
        mcu_.setForcedInputs(0, 0x01);
    } else {
        mcu_.setPullups(1, 0x40);        // PB6 pull-up only (egret.cpp:90)
    }
}

bool CudaLle::loadFirmware(const std::vector<uint8_t>& rom) {
    fwLoaded_ = mcu_.loadRom(rom);
    return fwLoaded_;
}

void CudaLle::reset() {
    // PRAM is battery-backed: snapshot the LIVE window back into the staging
    // copy BEFORE dropping the flag, else a warm reset reverted every XPRAM
    // byte the guest wrote since boot (monitor depth, volume, startup disk,
    // date/time) to the boot-time copy — and the next PC3 release re-installed
    // that stale copy over the firmware's. Egret::reset() already preserves
    // its PRAM for exactly this reason, and MAME's cuda device_reset() never
    // touches m_pram_loaded.
    if (pramInstalled_)
        for (int i = 0; i < 256; i++)
            stagedPram_[i] = mcu_.ramByte(0x100 + i);
    mcu_.reset();
    adb_.reset();
    held_ = true;
    treq_ = 1;
    byteack_ = tip_ = true;
    lastViaClock_ = true;
    resetLine_ = false;
    pramInstalled_ = false;
    mcuAcc_ = adbAcc_ = 0;
    mcuDebt_ = 0;
    i2cScl_ = i2cSda_ = true;            // I2C bus idles released
    i2cActive_ = i2cAddressed_ = i2cDriveLow_ = false;
    i2cBit_ = 0;
    i2cShift_ = 0;
}

void CudaLle::writeRtcSeconds() {
    mcu_.setRamByte(0xAB, uint8_t(stagedSeconds_ >> 24));
    mcu_.setRamByte(0xAC, uint8_t(stagedSeconds_ >> 16));
    mcu_.setRamByte(0xAD, uint8_t(stagedSeconds_ >> 8));
    mcu_.setRamByte(0xAE, uint8_t(stagedSeconds_));
}

void CudaLle::setSeconds(uint32_t s) {
    stagedSeconds_ = s;
    if (pramInstalled_) writeRtcSeconds();   // live after release, like PRAM
}

// ── PRAM staging (cuda.cpp pc_w :117-131: install on reset release) ────
uint8_t CudaLle::pram(int i) const {
    if (pramInstalled_)
        return const_cast<M68hc05&>(mcu_).ramByte(0x100 + (i & 0xFF));
    return stagedPram_[i & 0xFF];
}

void CudaLle::setPram(int i, uint8_t v) {
    stagedPram_[i & 0xFF] = v;
    if (pramInstalled_)                  // live after release, like NVRAM
        mcu_.setRamByte(0x100 + (i & 0xFF), v);
}

// ── MCU port callbacks (cuda.cpp / egret.cpp pa_r/pb_r/pc_r) ───────────
uint8_t CudaLle::mcuPortRead(int p) {
    const bool cuda = flavor_ == Flavor::Cuda;
    switch (p) {
        case 0:                          // PA: ADB line + power sense
            // Cuda: pull-up|PFW|ADB-power-off (cuda.cpp:244-260); Egret:
            // bare ADB line, control-panel bit left floating (egret.cpp).
            return uint8_t((cuda ? 0x07 : 0x00) | (adb_.line() ? 0x40 : 0));
        case 1:                          // PB: +5v | BYTEACK/VIA_FULL |
                                         // TIP/SYS_SESSION | via_data
            // PB6 = IIC SDA: pulled up unless the DFAC2 slave holds the
            // ACK slot low (MAME pb_r :271 m_iic_sda). Egret keeps its
            // bare PB6 pull-up (no I2C on that flavor).
            return uint8_t(0x01
                           | (byteack_ ? 0x04 : 0)
                           | (tip_ ? 0x08 : 0)
                           | (via_.extShiftCB2Out() ? 0x20 : 0)
                           | ((i2cDfac_ && i2cDriveLow_) ? 0x00 : 0x40)
                           | (cuda ? 0x80 : 0x00));
        default:                         // PC: power sense (Cuda only)
            return cuda ? 0x03 : 0x00;
    }
}

// ── I2C bus (setI2cDfac / onI2cValkyrie) ───────────────────────────────
// The bus master is the Cuda firmware bit-banging PB7 (SCL) / PB6 (SDA);
// pin levels arrive through the port/DDR mixing (open-drain release reads
// back 1 via the PB6/PB7 pull-ups, M68hc05::sendPort). The frame is MAME's
// i2c_hle_interface (`i2chle.cpp:108-200`): START, an address byte
// (7 bits + R/W), then a **sub-address** that seeds an auto-incrementing
// register pointer, then data bytes — each acknowledged by the slave
// holding SDA low from the 8th data bit until the ACK clock completes.
//
// Two slaves live here, and they are NOT symmetric:
//   * **DFAC2 at $6F** (`dfac2.cpp`) — payload accepted and DISCARDED,
//     which is oracle parity: MAME's own `write_data` only logs, its
//     registers still being reverse-engineered upstream. What matters is
//     the ACK. The factory Color Classic Cuda 2.35 (341S0417) takes its
//     DFAC-error path after ONE aborted probe and never completes the next
//     host VIA session — the "0417 wedge" was this missing device, not an
//     M68hc05 core bug (2026-07-29).
//   * **Valkyrie at $28** (`valkyrie.cpp:542-573`, Q630/LC 580 only) —
//     payload LOAD-BEARING: registers 1/2/3 are the M/N/P divisors of the
//     video pixel clock. Present only when `onI2cValkyrie` is installed.
//
// A data READ from either device is not modeled (no Mac firmware reads
// them back); the bus would float to all-ones.
void CudaLle::i2cWire(bool scl, bool sda) {
    if (scl && i2cScl_) {                // SCL high steady: START / STOP
        if (i2cSda_ && !sda) {           // START (repeated START re-opens)
            i2cActive_ = true;
            i2cAddressed_ = false;
            i2cSlave_ = 0;
            i2cByteIdx_ = 0;
            i2cOffset_ = 0;
            i2cBit_ = 0;
            i2cShift_ = 0;
            i2cDriveLow_ = false;
        } else if (!i2cSda_ && sda) {    // STOP
            i2cActive_ = false;
            i2cDriveLow_ = false;
        }
    } else if (scl && !i2cScl_ && i2cActive_) {      // SCL rising edge
        if (i2cBit_ < 8) {
            i2cShift_ = uint8_t((i2cShift_ << 1) | (sda ? 1 : 0));
            if (++i2cBit_ == 8) {
                if (i2cByteIdx_ == 0) {
                    // Address + R/W. A slave answers for both directions;
                    // an address nobody carries is NACKed and the rest of
                    // the transfer is ignored (i2cAddressed_ stays false).
                    const uint8_t addr = uint8_t(i2cShift_ >> 1);
                    if (i2cDfac_ && addr == 0x6F) i2cSlave_ = 0x6F;
                    else if (onI2cValkyrie && addr == 0x28) i2cSlave_ = 0x28;
                    else i2cSlave_ = 0;
                    i2cAddressed_ = i2cSlave_ != 0;
                } else if (i2cAddressed_) {
                    if (i2cByteIdx_ == 1) {
                        i2cOffset_ = i2cShift_;       // register pointer
                    } else {
                        if (i2cSlave_ == 0x28 && onI2cValkyrie)
                            onI2cValkyrie(i2cOffset_, i2cShift_);
                        i2cOffset_++;                 // auto-increment
                    }
                }
                if (i2cByteIdx_ < 255) i2cByteIdx_++;
                i2cDriveLow_ = i2cAddressed_;
                static const bool trace =
                    std::getenv("POM68K_ADB_LLE_TRACE") != nullptr;
                if (trace)
                    std::fprintf(stderr, "cudalle: i2c byte %02X %s (slave %02X)\n",
                                 i2cShift_, i2cAddressed_ ? "ACK" : "NACK",
                                 i2cSlave_);
            }
        } else {
            i2cBit_ = 9;                 // ACK clock high phase
        }
    } else if (!scl && i2cScl_ && i2cActive_ && i2cBit_ == 9) {
        i2cDriveLow_ = false;            // ACK clock done: release SDA
        i2cBit_ = 0;
        i2cShift_ = 0;
    }
    i2cScl_ = scl;
    i2cSda_ = sda;
}

void CudaLle::mcuPortWrite(int p, uint8_t v) {
    if (onMcuPortWrite) onMcuPortWrite(p, v);
    switch (p) {
        case 0:                          // PA7 = ADB drive (pa_w :96-121)
            // The ADB output stage inverts: PA7=1 pulls the line LOW.
            // MAME encodes it as write_linechange((bit7>>7)^1) and macadb
            // echoes that level back into PA6 (adb_linechange_w:955) — so
            // the electrical line is !PA7 and PA6 senses the line direct.
            // Getting this backwards made AdbLine see idle-low/attention-
            // high and never decode a single autopoll command.
            adb_.setHostDrive(!(v & 0x80));
            break;
        case 1: {                        // PB (pb_w :123-137)
            const uint8_t newTreq = uint8_t((v >> 1) & 1);
            if (newTreq != treq_ && !newTreq) {
                static const bool trace =
                    std::getenv("POM68K_ADB_LLE_TRACE") != nullptr;
                if (trace) std::fprintf(stderr, "cudalle: TREQ fall\n");
            }
            treq_ = newTreq;
            const bool clock = (v & 0x10) != 0;
            const bool data = (v & 0x20) != 0;
            if (clock != lastViaClock_) {
                // via_clock drives the host VIA SR's external shift clock
                // (CB1); via_data rides CB2 — the PIC1654S wire pattern.
                via_.extShiftCB1(clock, data);
                traceSessionClocks_++;
                if (!clock) {                        // sample on falling edge
                    traceByte_ = uint8_t((traceByte_ << 1) | (data ? 1 : 0));
                    if (++traceBits_ == 8) {
                        traceBits_ = 0;
                        static const bool trace =
                            std::getenv("POM68K_ADB_LLE_TRACE") != nullptr;
                        if (trace)
                            std::fprintf(stderr,
                                         "cudalle: byte = %02X (tip=%d treq=%d)\n",
                                         traceByte_, tip_ ? 1 : 0, treq_);
                    }
                }
                lastViaClock_ = clock;
            }
            // PB7 = IIC SCL, PB6 = IIC SDA (cuda.cpp pb_w :198-199) — the
            // DFAC2 slave listens when the machine carries one.
            if (i2cDfac_) i2cWire((v & 0x80) != 0, (v & 0x40) != 0);
            break;
        }
        case 2: {                        // PC3 = host reset
            // Cuda: RISING edge pulses the host reset and installs the
            // staged PRAM (cuda.cpp pc_w :139-160). Egret: the FALLING
            // edge does (egret.cpp pc_w :240-262).
            const bool level = (v & 0x08) != 0;
            const bool release = (flavor_ == Flavor::Cuda)
                ? (!resetLine_ && level)
                : (resetLine_ && !level);
            if (release) {
                held_ = false;
                if (!pramInstalled_) {   // slap staged PRAM into live RAM
                    for (int i = 0; i < 256; i++)
                        mcu_.setRamByte(0x100 + i, stagedPram_[i]);
                    // ...and seed the firmware's own RTC counter, exactly as
                    // MAME does next to m_pram_loaded (cuda.cpp:226-229): the
                    // 32-bit seconds live in MCU RAM at $AB(MSB)..$AE(LSB).
                    // Without this the host wall clock went to the INACTIVE
                    // HLE object and every firmware-LLE machine booted at the
                    // 1904 epoch.
                    writeRtcSeconds();
                    pramInstalled_ = true;
                }
            }
            resetLine_ = level;
            break;
        }
    }
}

// ── Host-side wiring ───────────────────────────────────────────────────
void CudaLle::portBChanged(uint8_t pb) {
    // macquadra605.cpp:230-233: VIA1 PB4 → BYTEACK, PB5 → TIP (active low).
    const bool newAck = (pb & 0x10) != 0;
    if (newAck != byteack_) {
        static const bool trace = std::getenv("POM68K_ADB_LLE_TRACE") != nullptr;
        if (trace) std::fprintf(stderr, "cudalle: BYTEACK %d\n", newAck ? 1 : 0);
    }
    byteack_ = newAck;
    const bool newTip = (pb & 0x20) != 0;
    static const bool trace = std::getenv("POM68K_ADB_LLE_TRACE") != nullptr;
    if (newTip != tip_ && trace) {
        if (!newTip) {
            traceSessionClocks_ = 0;
            std::fprintf(stderr, "cudalle: TIP fall (session start)\n");
        } else {
            std::fprintf(stderr, "cudalle: TIP rise after %d clock edges\n",
                         traceSessionClocks_);
        }
    }
    tip_ = newTip;
}

void CudaLle::tick(int cpuCycles) {
    // Machine cycles → the E1's 2.097 MHz cycle domain. The ADB wire is
    // slaved to the MCU's instruction stream via mcu_.onCycles (see the
    // constructor) — it advances INSIDE run(), not here, so the firmware
    // hears device edges at instruction resolution while the MCU/machine
    // lockstep phase stays exactly as before.
    mcuAcc_ += int64_t(cpuCycles) * kMcuHz;
    int mcuCyc = int(mcuAcc_ / cpuHz_);
    mcuAcc_ -= int64_t(mcuCyc) * cpuHz_;
    // run() finishes its last instruction past the budget; carry that
    // overshoot as a debt against the next slice, or the MCU clock gains
    // ~37% under instruction-sized quanta — the LC II soak gate caught the
    // Mac wall clock running 247 s in 180 s of machine time (the HLE path,
    // which is time-based, kept exact time on the same feed).
    mcuCyc -= mcuDebt_;
    if (mcuCyc > 0) {
        int used = mcu_.run(mcuCyc);
        mcuDebt_ = used > mcuCyc ? used - mcuCyc : 0;
    } else {
        mcuDebt_ = -mcuCyc;
    }
}
