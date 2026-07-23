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
    mcu_.reset();
    adb_.reset();
    held_ = true;
    treq_ = 1;
    byteack_ = tip_ = true;
    lastViaClock_ = true;
    resetLine_ = false;
    pramInstalled_ = false;
    mcuAcc_ = adbAcc_ = 0;
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
            return uint8_t(0x01
                           | (byteack_ ? 0x04 : 0)
                           | (tip_ ? 0x08 : 0)
                           | (via_.extShiftCB2Out() ? 0x20 : 0)
                           | 0x40 | (cuda ? 0x80 : 0x00));
        default:                         // PC: power sense (Cuda only)
            return cuda ? 0x03 : 0x00;
    }
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
    // NOTE (2026-07-23, TODO step 6): this batch order — the MCU runs its
    // whole slice against a FROZEN wire, then the wire advances — is why
    // the Egret ROM's ADB receive mis-hears device bytes as zeros (the
    // SR-byte diagnostics show well-formed [00 40 00 00] autopoll packets
    // whose data the firmware never heard; the Cuda ROM's receive loop
    // tolerates the same skew). Two slicing experiments failed: uniform
    // 2 µs interleave breaks the boot-time PC3/VIA dance (guest crash),
    // busy-gated slicing kills the mouse entirely — the lockstep phase is
    // load-bearing in both directions. The fix is an event-driven wire
    // (MAME attotime-style: the MCU consumes line-edge timestamps), not a
    // slicing heuristic.
    // Machine cycles → the E1's 2.097 MHz cycle domain.
    mcuAcc_ += int64_t(cpuCycles) * kMcuHz;
    int mcuCyc = int(mcuAcc_ / cpuHz_);
    mcuAcc_ -= int64_t(mcuCyc) * cpuHz_;
    if (mcuCyc) mcu_.run(mcuCyc);
    // AdbLine's timers run in the Mac II 15.6672 MHz cycle domain.
    adbAcc_ += int64_t(cpuCycles) * kAdbHz;
    int adbCyc = int(adbAcc_ / cpuHz_);
    adbAcc_ -= int64_t(adbCyc) * cpuHz_;
    if (adbCyc) adb_.tick(adbCyc);
}
