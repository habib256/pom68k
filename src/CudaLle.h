// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Cuda firmware LLE: M68HC05E1 core wired to the machine (step 2) ──
// The real 341S0788 (Cuda 2.37) firmware talking to the host VIA and the
// bit-serial ADB line — the blueprint's step 2 (TODO "Egret/Cuda firmware
// LLE"), behind POM68K_CUDA_LLE=1; the Egret HLE stays the default until
// this path passes the boot etalons.
//
// Signal map (MAME mame/apple/cuda.cpp pa_r/pa_w/pb_r/pb_w/pc_w +
// macquadra605.cpp:214-233 wiring):
//   PA7 out = ADB drive (1 = release)   PA6 in = ADB line level
//   PB1 out = /TREQ (1 = idle) → host VIA1 PB3
//   PB4 out = via_clock → VIA1 CB1 (external shift clock)
//   PB5 out/in = via_data ↔ VIA1 CB2 (SR MSB when the host shifts out)
//   PB2 in = BYTEACK ← host VIA1 PB4   PB3 in = TIP ← host VIA1 PB5
//   PC3 out = 680x0 reset release; PRAM (256 B) is installed into the
//   E1's internal RAM at $0100-$01FF on that rising edge (pc_w :117-131)
// The VIA side reuses Via6522::extShiftCB1/extShiftCB2Out — the external
// shift path built for the Mac II ADB PIC1654S (the rollout template).
//
// Gate: tests/cuda_lle_test.cpp (firmware releases the host, TREQ idles,
// staged PRAM lands in MCU RAM).

#pragma once
#include "CoreConfig.h"
#include "M68hc05.h"
#include "AdbLine.h"
#include <cstdint>
#include <functional>
#include <vector>

class Via6522;

class CudaLle {
public:
    // The Egret is the same customized 68HC05E1 at the same 4.19 MHz with
    // the same VIA/ADB port assignments (MAME egret.cpp vs cuda.cpp); the
    // flavor differences are the idle input levels (PB bit7/PC power sense
    // absent, PA bare), the pull-up set (PB6 only, no PFW tap needed) and
    // the host-reset edge (PC3 FALLING releases + installs PRAM, where the
    // Cuda uses the rising edge).
    enum class Flavor { Cuda, Egret };
    explicit CudaLle(Via6522& via, int64_t cpuHz = 25000000,
                     Flavor flavor = Flavor::Cuda);
    void configure(const pom68k::CorePeripheralConfig& peripherals) {
        trace_ = peripherals.adbLleTrace;
        adb_.configure(peripherals.adbKeyboardHandlerId, trace_);
    }

    bool loadFirmware(const std::vector<uint8_t>& rom);  // 0x1100 E1 image
    bool firmwareLoaded() const { return fwLoaded_; }
    void reset();

    // The firmware holds the 680x0 in reset until its PC3 rising edge.
    bool cpuHeld() const { return held_; }

    // Firmware-driven RESET_SYSTEM ($11) — the Finder's "Restart". The MCU
    // pulses the same PC3 line a SECOND time, on a machine that has already
    // booted; the power-on release is filtered out for you (see the handler,
    // CudaLle.cpp). The PG&E twin is PgePmu::onCpuReset, bound by MscMemory
    // to onWake (MscMemory.cpp:88), which is why the Duo restarts and no
    // Egret/Cuda machine does.
    //
    // UNBOUND on every platform today, so behaviour is unchanged. Binding it
    // is NOT a one-liner and must not be done casually: this fires from
    // inside viaWrite(), and every platform's reset() calls egret_.reset() /
    // cuda_.reset() (V8Memory.cpp:259, SonoraMemory.cpp:109,
    // Q605Memory.cpp:134) — a direct binding re-enters and resets the MCU
    // while it is executing. It needs a DEFERRED reset (latch here, act at
    // the next tick boundary), one binding per Egret/Cuda platform, and a
    // gate; TODO § 4 carries it. (2026-08-12)
    std::function<void()> onCpuReset;

    // The action a firmware RESET_SYSTEM performs — pull the host /RESET
    // line. The PC3 handler calls exactly this once it has decided the
    // release is a restart and not the power-on hold coming off, so a
    // caller here exercises the shipped path rather than a replica.
    void hostReset();

    // Host VIA1 port B outputs (machine calls on ORB/DDRB writes):
    // PB4 = BYTEACK, PB5 = TIP (both active low on a Cuda).
    void portBChanged(uint8_t pb);
    // → host VIA1 PB3 input: the MCU's /TREQ (1 = idle).
    uint8_t xcvrSession() const { return treq_; }

    void tick(int cpuCycles);            // machine cycles → MCU + ADB clocks
    // Earliest machine-cycle distance at which the MCU can execute another
    // cycle after accounting for the fractional clock bridge and run()
    // overshoot debt. Used by event-deadline peripheral scheduling.
    int cyclesToNextEvent() const;

    // PRAM staging: contents installed into MCU RAM $0100-$01FF when the
    // firmware releases the host (MAME m_pram_loaded copy). Reads come
    // back from staging before release, from live MCU RAM after.
    uint8_t pram(int i) const;
    // Host wall clock -> the firmware's own seconds counter (MCU RAM $AB-$AE,
    // cuda.cpp:226-229). Staged like PRAM and installed on reset release.
    void setSeconds(uint32_t s);
    void setPram(int i, uint8_t v);

    // Minimal I2C slave on the Cuda's PB7 (SCL) / PB6 (SDA): the DFAC2
    // audio chip at address $6F (MAME dfac2.cpp i2c_hle_interface). Enable
    // per machine — MAME wires one on the Color Classic (maclc.cpp:505),
    // the Sonora AIOs (maclc3.cpp:403) and the Quadra 630
    // (macquadra630.cpp:196); the Q605 bus is empty and the Mac TV has no
    // DFAC at all. Without the ACK the factory Color Classic Cuda 2.35
    // (341S0417) takes its DFAC-error path after ONE aborted probe and
    // never completes the next host VIA session — the "0417 wedge" was
    // this missing device, not an M68hc05 core bug (2026-07-29). Write
    // bytes are accepted and discarded (register semantics are still
    // being reverse-engineered upstream); a data READ from the device is
    // not modeled and returns all-ones.
    void setI2cDfac(bool on) { i2cDfac_ = on; }

    // The bus is a BUS: MAME merges the Cuda, the Valkyrie and the DFAC2
    // onto one wired-AND SDA on the Quadra 630 / LC 580
    // (`macquadra630.cpp:187-199`). Installing this callback adds the
    // **Valkyrie clock generator at address $28** as a second slave, and
    // unlike the DFAC2 its payload is load-bearing: the i2c_hle framing is
    // address → sub-address (a register pointer) → data bytes at an
    // auto-incrementing offset (`i2chle.cpp:176-196`), and registers 1/2/3
    // are the M/N/P divisors of the pixel clock. Left unset, address $28
    // is simply absent from the bus and NACKs, which is what every other
    // Cuda machine wants.
    std::function<void(uint8_t /*reg*/, uint8_t /*val*/)> onI2cValkyrie;

    AdbLine& adbLine() { return adb_; }  // input events (key/mouse) land here
    M68hc05& mcu() { return mcu_; }      // gate/debug access

    // ── Save states (SaveState.h) ───────────────────────────────────────
    // The MCU and the ADB wire nest in (each has its own visit()), then the
    // glue: the two clock-domain accumulators plus `mcuDebt_`. That debt is
    // the run() overshoot carried between ticks, and it is not cosmetic —
    // dropping it is what once overclocked the MCU ~37 % and drifted the Mac
    // clock (CHANGELOG, `lcii_soak_etalon`). A restore must resume mid-debt.
    //
    // The half-transferred host handshake travels too (`treq_`, `byteack_`,
    // `tip_`, `lastViaClock_`, the I2C shift state): a snapshot can land in
    // the middle of a byte, and the transport does not tolerate being
    // re-synchronized underneath the firmware.
    //
    // Out: `via_` (reference), `cpuHz_`/`flavor_`/`i2cDfac_`/`fwLoaded_`
    // (board identity, set at construction), `onMcuPortWrite` (re-bound).
    template <class Ar> void visit(Ar& ar) {
        ar(mcu_, adb_);
        ar(mcuAcc_, mcuDebt_, adbAcc_,
           held_, treq_, byteack_, tip_, lastViaClock_, resetLine_,
           i2cScl_, i2cSda_, i2cActive_, i2cAddressed_, i2cBit_,
           i2cShift_, i2cDriveLow_, i2cSlave_, i2cByteIdx_, i2cOffset_,
           stagedPram_, pramInstalled_, stagedSeconds_,
           traceSessionClocks_, traceByte_, traceBits_);
    }
    // Debug hook (cuda_lle wire tracing): fires on every MCU port write
    // (port, value) — the Egret::onEdge equivalent for the firmware path.
    std::function<void(int, uint8_t)> onMcuPortWrite;

private:
    bool trace_ = false;
    uint8_t mcuPortRead(int p);
    void mcuPortWrite(int p, uint8_t v);
    void i2cWire(bool scl, bool sda);    // DFAC2 slave bus tracking

    Via6522& via_;
    M68hc05 mcu_;
    AdbLine adb_;
    const int64_t cpuHz_;                // machine cycles per second
    const Flavor flavor_;
    static constexpr int64_t kMcuHz = 2097152;    // 4.194304 MHz XTAL / 2
    static constexpr int64_t kAdbHz = 15667200;   // AdbLine's cycle domain
    int64_t mcuAcc_ = 0;                 // machine cycles → MCU cycles
    int mcuDebt_ = 0;                    // run() overshoot carried forward
    int64_t adbAcc_ = 0;                 // MCU cycles → ADB cycles (slaved
                                         // wire, mcu_.onCycles hook)

    bool fwLoaded_ = false;
    bool held_ = true;
    uint8_t treq_ = 1;
    int traceSessionClocks_ = 0;      // diag only (POM68K_ADB_LLE_TRACE)
    uint8_t traceByte_ = 0;
    int traceBits_ = 0;
    bool byteack_ = true, tip_ = true;   // host levels as the MCU reads them
    bool lastViaClock_ = true;
    // ── I2C bus state (setI2cDfac / onI2cValkyrie) ──
    bool i2cDfac_ = false;
    bool i2cScl_ = true, i2cSda_ = true; // last pin levels the MCU drove
    bool i2cActive_ = false;             // between START and STOP
    bool i2cAddressed_ = false;          // a slave on the bus answered
    int i2cBit_ = 0;                     // SCL rising edges in current byte
    uint8_t i2cShift_ = 0;
    bool i2cDriveLow_ = false;           // slave holds SDA low (ACK slot)
    uint8_t i2cSlave_ = 0;               // 7-bit address the transfer opened at
    uint8_t i2cByteIdx_ = 0;             // 0 = address, 1 = sub-address, 2+ = data
    uint8_t i2cOffset_ = 0;              // auto-incrementing register pointer
    bool resetLine_ = false;             // PC3 latch (rising edge releases)

    uint8_t stagedPram_[256] = {};
    bool pramInstalled_ = false;
    uint32_t stagedSeconds_ = 0;
    void writeRtcSeconds();
};
