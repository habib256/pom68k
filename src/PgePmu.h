// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── PG&E power manager, board-level integration (MSC PowerBooks) ──
// Owns the M68hc05Pge chip and wires it the way macpwrbkmsc.cpp does
// (:421-585 port handlers, :773-796 bindings):
//   SPI    SCK → VIA1 CB1 (ext shift, no CB1 IFR), MOSI → VIA1 CB2,
//          MISO ← VIA1 SR bit 7 (extShiftCB2Out) — the host uploads the
//          PMU firmware and talks the PMU protocol through this shifter.
//   Port A/B/C  keyboard matrix (rows on C, columns A + B bits 0-2,
//               modifiers B bits 3-7); power key row when C == 0
//   Port D      config: bit 7 = 2nd mouse button (1 = up), bit 6 = dock
//               absent, bit 4 = US keyboard
//   Port E      bit 1 display blank, bit 2 = MSC /reset (releases the
//               68030 — the PMU boots FIRST), bit 7 = 1-Wire (DS2400
//               battery ID — stubbed idle-high, logged)
//   Port F      read: +5V ok, clamshell open, /PMU_REQ on bit 6;
//               write bit 2 = PMU interrupt to the host (VIA1 CB1 IFR)
//   Port G      read: charger present, dock powered; write bit 5 =
//               sleep (1 = master clock off; 1→0 = wake + CPU reset)
//   Port H      write bit 6 = /PMU_ACK (pseudo-VIA2 PB1); reads return
//               the write latch, which STARTS AT $00 — bit 0 (DFAC reset)
//               must read 0 for the boot ROM to configure the DFAC
//               (macpwrbkmsc.cpp:129, pmu_porth_r:543-546)
//   Port J/L    DFAC / power rails — latched, logged
//   ADC         ch0 bat-low $FF, ch1 bat-high $7F, ch2 current $40,
//               ch3/4 temps 131 (~24 °C) — the MAME fixed board values
// Boot flow: 512 B mask ROM runs first, host uploads the main firmware
// over SPI into the PGE's SRAM, PMU releases the 68030 via port E bit 2.
// The 68030 is HELD until then (cpuHeld — msc.cpp:151).
// Gates: tests/msc_parity_test.cpp, tests/duo230_boot_etalon.cpp;
// blueprint docs/DUO_BRINGUP.md.

#pragma once
#include "AdbBus.h"
#include "M68hc05Pge.h"
#include "SaveState.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

class Via6522;

class PgePmu {
public:
    // cpuHz = the host machine clock feeding tick().
    PgePmu(Via6522& via1, int64_t cpuHz);

    bool loadBootRom(const std::string& path);       // roms/pge/pge_boot.bin
    bool loadBatteryId(const std::string& path);     // roms/pge/duobatid.bin
    bool active() const { return mcu_ != nullptr && bootLoaded_; }
    void reset();
    void tick(int cpuCycles);                        // CudaLle debt pattern

    // ── Machine-facing lines ──
    bool cpuHeld() const { return held_ || !porteBit2_; }
    bool pmuAck() const { return ackLevel_; }        // pseudo-VIA2 PB1
    void setPmuReq(bool level);                      // pseudo-VIA2 PB2 write
    // Machine cycles the 68030 must burn after a /PMU_REQ edge, so the PMU
    // gets to react before the host samples ACK (MAME via2_out_b:
    // spin_until_time(80 µs)). 0 when the edge did not change the line.
    int takeHostSpin() { int c = hostSpin_; hostSpin_ = 0; return c; }
    // port G bit 5 falling edge = wake: the machine must clear HALT,
    // pulse the CPU reset and re-arm the ROM overlay (pmu_portg_w +
    // msc.cpp pmu_reset_w).
    std::function<void()> onWake;
    // port E bit 2 rising edge = the PMU releasing the 68030's /RESET:
    // the machine re-arms the ROM overlay and resets the CPU
    // (pmu_porte_w:431-441 → msc.cpp pmu_reset_w:363-378). The low LEVEL
    // is the hold, reported by cpuHeld().
    std::function<void()> onCpuReset;
    std::function<void(bool)> onDisplayBlank;        // port E bit 1

    // RTC seed (Mac epoch) — PRAM/clock live inside the PMU on Duos.
    void setSeconds(uint32_t s) { if (mcu_) mcu_->setRtc(s); }

    M68hc05Pge& mcu() { return *mcu_; }
    AdbBus& adb() { return adb_; }
    void keyEvent(uint8_t code, bool down) { adb_.keyEvent(code, down); }
    void mouseMove(int dx, int dy) { adb_.mouseMove(dx, dy); }
    void mouseButton(bool down) { adb_.mouseButton(down); }
    long pmuIntEdges = 0;                            // port F bit 2 falls
    long pmuAckEdges = 0;                            // port H bit 6 changes

    // ── Save states ──
    template <class Ar> void visit(Ar& ar) {
        if (mcu_) ar(*mcu_);
        ar(mcuAcc_, mcuDebt_, held_, porteBit2_, ackLevel_, reqLevel_,
           lastPortE_, lastPortF_, lastPortG_, lastPortC_, lastPortH_,
           lastMosi_);
        ar(ds2400_, adb_);
    }

private:
    void wirePorts();

    // Heap-allocated: the PGE carries a 32 KB SRAM; keep machine objects
    // that embed a PgePmu small.
    std::unique_ptr<M68hc05Pge> mcu_;
    Via6522& via_;
    int64_t cpuHz_;
    bool bootLoaded_ = false;

    int64_t mcuAcc_ = 0;
    int mcuDebt_ = 0;

    bool held_ = true;                               // 68030 held at power-on
    bool porteBit2_ = false;                         // MSC /reset (1 = run)
    bool ackLevel_ = true;                           // /PMU_ACK idle
    bool reqLevel_ = true;                           // /PMU_REQ idle
    uint8_t lastPortE_ = 0xFF, lastPortF_ = 0xFF, lastPortG_ = 0xFF;
    uint8_t lastPortC_ = 0xFF;                       // matrix row select
    // Port H read-back latch. $00 at power-on is load-bearing: bit 0 is
    // the DFAC reset and the PG&E boot ROM only configures the DFAC when
    // it starts low (MAME macpwrbkmsc.cpp:129 m_last_porth, :543-546).
    uint8_t lastPortH_ = 0x00;
    bool lastMosi_ = false;
    int hostSpin_ = 0;                               // machine cycles owed
    // The ADB bus behind the PG&E's modem cell. Command-level is honest
    // here: the cell IS a hardware transceiver, so the wire lives inside
    // the PG&E's silicon and the firmware only ever sees bytes.
    AdbBus adb_;

    // ── DS2400 battery serial, 1-Wire slave on port E bit 7 ─────────────
    // MAME machine/ds2401.cpp state machine on the MCU cycle clock
    // (2.097152 cycles/µs): reset ≥480 µs low → presence 30+120 µs, then
    // LSB-first bytes data[7]…data[0]; write-slots sampled +30 µs after
    // the falling edge, read-slot zero holds the line 30 µs.
    struct Ds2400 {
        enum State { Idle, Reset, Reset1, Reset2, Command, ReadRom };
        uint8_t data[8] = {};
        bool loaded = false;
        int state = Idle;
        int bit = 0, byte = 0;
        uint8_t shift = 0;
        bool rx = true, tx = true;
        int64_t mainAt = -1, resetAt = -1;           // mcu-cycle deadlines
        void write(bool level, int64_t now);         // master line drive
        void tick(int64_t now);                      // deadline pump
        bool read() const { return tx && rx; }
        template <class Ar> void visit(Ar& ar) {
            ar(state, bit, byte, shift, rx, tx, mainAt, resetAt);
        }
    } ds2400_;
};
