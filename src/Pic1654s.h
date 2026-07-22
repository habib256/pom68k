// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── PIC1654S microcontroller (LLE) ──
// Microchip / General Instrument PIC1654S 12-bit MCU — the exact part Apple
// used as the Mac II / SE ADB transceiver "ADB Modem 342S0440-B". MAME
// emulates the ADB this way: by running the real PIC firmware ROM. The whole
// NEW/EVEN/ODD/IDLE byte handshake, the ADB bit timing and the device
// timeouts live *inside* that firmware, not in host C++. Feed this core the
// dumped 342s0440-b.bin and it reproduces the transceiver bit-for-bit.
//
// This is the PIC16C54 core (9-bit ROM = 512 words, 5-bit data = 32 regs,
// 2 ports) with the GI/NMOS "0x1654" quirks that MAME models:
//   • /8 clock divider (3.6864 MHz XTAL → 460.8 kHz instruction rate),
//   • OPTION / SLEEP / CLRWDT / TRIS decode as NOP (main table routes the
//     0x000-0x00F control group to nop/illegal — those instructions do not
//     exist on this mask-ROM part),
//   • port read = external pins AND the output latch (no TRIS mux),
//   • STATUS writes force the unused top bits to 1 (status mask 0x07),
//   • TMR0/RTCC counts external RTCC-pin edges (OPTION fixed at 0x3F).
//
// Source: MAME src/devices/cpu/pic16c5x/pic16c5x.cpp (Tony La Porta,
// BSD-3-Clause — GPLv3-compatible; header preserved) + adbmodem.cpp wiring.

#pragma once
#include <cstdint>
#include <cstddef>
#include <functional>
#include <vector>

class Pic1654s {
public:
    // Port I/O hooks (set by the owner, e.g. AdbVia). Reads return the pin
    // levels driven onto the input pins by the outside world (the core ANDs
    // them with the output latch, the GI/NMOS quasi-bidirectional model).
    // Writes receive the full output latch byte; the wiring decides pins.
    std::function<uint8_t()>        readA;    // PORTA pins (RA0..RA3)
    std::function<uint8_t()>        readB;    // PORTB pins (RB0..RB7)
    std::function<void(uint8_t)>    writeA;   // PORTA output latch
    std::function<void(uint8_t)>    writeB;   // PORTB output latch

    // Load mask-ROM firmware (raw dump, two bytes per 12-bit word).
    bool loadRom(const uint8_t* data, size_t bytes, bool bigEndian = false);
    bool romLoaded() const { return !prog_.empty(); }

    void reset();

    // Drive the RTCC / T0CKI pin (wired to ADB-in). TMR0 counts falling
    // edges (OPTION T0SE=1 at reset). Call whenever the ADB line changes.
    void setRtcc(bool level);

    // Execute up to `cycles` instruction cycles. Returns the number spent
    // (branches/skips cost 2).
    int run(int cycles);

    // Debug / test accessors.
    uint8_t  w() const { return w_; }
    uint16_t pc() const { return pc_; }
    uint8_t  status() const { return status_; }
    uint8_t  reg(uint8_t f) const { return f < ram_.size() ? ram_[f] : 0; }

private:
    enum : uint8_t {                         // file-register indices
        F_INDF = 0x00, F_TMR0 = 0x01, F_PCL = 0x02, F_STATUS = 0x03,
        F_FSR = 0x04, F_PORTA = 0x05, F_PORTB = 0x06,
    };
    enum : uint8_t {                         // STATUS bits
        ST_C = 0x01, ST_DC = 0x02, ST_Z = 0x04, ST_PD = 0x08, ST_TO = 0x10,
        ST_PA = 0xE0,                        // program page preselect (cosmetic on 512-word part)
    };
    static constexpr uint8_t kStatusHi = uint8_t(~0x07);   // NMOS: unused bits read/forced 1

    uint8_t  readReg(uint8_t f);
    void     writeReg(uint8_t f, uint8_t v);
    void     setZ(uint8_t v) { if (v) status_ &= ~ST_Z; else status_ |= ST_Z; }
    void     push(uint16_t addr);
    uint16_t pop();

    std::vector<uint16_t> prog_;             // program memory, 12-bit words
    uint16_t progMask_ = 0;                  // size-1 (power-of-two wrap)
    std::vector<uint8_t>  ram_;              // 32 file registers

    uint8_t  w_ = 0;
    uint16_t pc_ = 0;
    uint8_t  status_ = 0;
    uint8_t  fsr_ = 0;
    uint8_t  option_ = 0x3F;                 // fixed (OPTION instr is a NOP here)
    uint8_t  portA_ = 0, portB_ = 0;         // output latches
    uint16_t stack_[2] = { 0, 0 };
    bool     pclWritten_ = false;            // computed-goto cost tracking
    bool     rtcc_ = true;                   // last RTCC pin level (pulled up)
};
