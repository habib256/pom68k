// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Apple 343S1021 PIC body — see ApplePic.h for the register map and the
// timing contract. Oracle: MAME `machine/applepic.cpp` (cited per
// function); the one structural difference is the clock plumbing — MAME
// schedules attotime timers, POM68K counts input clocks slaved to the
// 65C02's instruction stream (debt-carried, `pom68k-mcu-lle-clock-drift`).

#include "ApplePic.h"

ApplePic::ApplePic()
{
    cpu_.read8 = [this](uint16_t a) { return read8(a); };
    cpu_.write8 = [this](uint16_t a, uint8_t v) { write8(a, v); };
}

void ApplePic::reset()
{
    // MAME device_reset (`applepic.cpp:108-123`): keep bits 7,6,1,0 of
    // the status register (mask $C3) — bit 2 clearing is what holds the
    // 65C02 in reset until the host writes /RSTPIC=1.
    statusReg_ &= 0xC3;
    intMask_ = 0;
    if (hostInt) hostInt(false);
    for (DmaChannel& ch : dma_)
        ch.control = 0;
    updateIrqLine();
}

// ── Internal 65C02 space ──────────────────────────────────────────────────

uint8_t ApplePic::read8(uint16_t a)
{
    // $F000-$F7FF is the register hole; everything else is the 32 KB RAM
    // through its mirrors ($0000-$6FFF ≡ $8000-$EFFF, $7800-$7FFF ≡
    // $F800-$FFFF — the vectors the host uploads land at $7Fxx).
    if ((a & 0xF800) == 0xF000)
        return regRead(a);
    return ram_[a & 0x7FFF];
}

void ApplePic::write8(uint16_t a, uint8_t v)
{
    if ((a & 0xF800) == 0xF000) {
        regWrite(a, v);
        return;
    }
    ram_[a & 0x7FFF] = v;
}

// ── Timer (`applepic.cpp:265-322`) ────────────────────────────────────────

uint16_t ApplePic::timerCount() const
{
    if (timerArmed_) {
        // MAME get_timer_count: (remaining_clocks - 4) / 8. We can be
        // late by part of an instruction; clamp at 0 rather than wrap.
        const int64_t remaining = timerExpiry_ - clockNow_;
        if (remaining <= 4) return 0;
        return static_cast<uint16_t>((remaining - 4) / 8);
    }
    // Expired: free-running down-count from $FFFF.
    return static_cast<uint16_t>(
        0xFFFF - static_cast<uint16_t>(((clockNow_ - timerLastExpired_) + 4) / 8));
}

void ApplePic::runTimer()
{
    while (timerArmed_ && clockNow_ >= timerExpiry_) {
        setInterrupt(kIrqTimer);
        if (dpllCtl_ & 0x01) {
            // Continuous mode: re-arm from the SCHEDULED expiry, not from
            // "now" — we fire with instruction granularity and the cadence
            // must not accumulate that lateness (MAME fires exactly, so
            // its adjust-from-callback is the same schedule).
            timerExpiry_ += (timerLatch_ + 2) * 8;
        } else {
            timerLastExpired_ = timerExpiry_;
            timerArmed_ = false;
        }
    }
}

// ── DMA (`applepic.cpp:386-422`) — one byte per channel per 8 clocks ─────

void ApplePic::dmaTick()
{
    for (int ch = 0; ch < 2; ch++) {
        DmaChannel& channel = dma_[ch];
        DmaChannel& other = dma_[ch ^ 1];

        if ((channel.control & kDmaEnable) && channel.req && channel.tc > 0) {
            const int ioReg = channel.control >> 4;
            if (channel.control & kDmaDir) {
                const uint8_t xfer = readPeriph ? readPeriph(ioReg) : 0xFF;
                write8(channel.map, xfer);
            } else {
                const uint8_t xfer = read8(channel.map);
                if (writePeriph) writePeriph(ioReg, xfer);
            }

            channel.map++;
            channel.tc--;

            if (channel.tc == 0) {
                channel.control &= ~kDmaEnable;
                // DEN1ON2/DEN2ON1 alternating-buffer chaining.
                if (other.control & kDmaChain)
                    other.control |= kDmaEnable;
                setInterrupt(kIrqDma1 + ch);
            }
        }
    }
}

// ── Interrupt unit (`applepic.cpp:464-535`) ───────────────────────────────

void ApplePic::updateIrqLine()
{
    cpu_.setIrqLine(0, (intReg_ & intMask_) != 0);
}

void ApplePic::setInterrupt(int which)
{
    intReg_ |= static_cast<uint8_t>(1u << which);
    updateIrqLine();
}

void ApplePic::resetInterrupt(int which)
{
    intReg_ &= static_cast<uint8_t>(~(1u << which));
    updateIrqLine();
}

// ── Internal registers ($F0xx, 6502 + host-window sides) ─────────────────

uint8_t ApplePic::regRead(uint16_t a)
{
    switch (a) {
    case 0xF010: case 0xF011: case 0xF012: case 0xF013: {
        // offset bit1 = latch vs count, bit0 = hi vs lo byte; reading the
        // count's low byte acks the timer interrupt (`applepic.cpp:265-280`).
        const uint16_t reg = (a & 2) ? timerLatch_ : timerCount();
        if (a & 1)
            return static_cast<uint8_t>(reg >> 8);
        if (a == 0xF010)
            resetInterrupt(kIrqTimer);
        return static_cast<uint8_t>(reg);
    }

    case 0xF020: case 0xF021: case 0xF022: case 0xF023: case 0xF024:
    case 0xF028: case 0xF029: case 0xF02A: case 0xF02B: case 0xF02C: {
        const DmaChannel& channel = dma_[(a >> 3) & 1];
        switch (a & 7) {
        case 0: return channel.control;
        case 1: return static_cast<uint8_t>(channel.map);
        case 2: return static_cast<uint8_t>(channel.map >> 8);
        case 3: return static_cast<uint8_t>(channel.tc);
        case 4: return static_cast<uint8_t>((channel.tc & 0x700) >> 8);
        }
        return 0;
    }

    case 0xF030: return sccCtl_;
    case 0xF031: return ioCtl_;
    case 0xF032: return static_cast<uint8_t>(dpllCtl_ | ((gpIn ? gpIn() : 0) & 3) << 2);
    case 0xF033: return intMask_;
    // Masked so the firmware never tries to service a disabled source
    // (MAME's own warning, `applepic.cpp:484-488`).
    case 0xF034: return intReg_ & intMask_;
    case 0xF035: return static_cast<uint8_t>((statusReg_ & 0x30) >> 2);

    default:
        if ((a & 0xFFF0) == 0xF040) {
            // Device registers reach the peripheral in NON-bypass mode
            // only (`applepic.cpp:552-566`).
            if (sccCtl_ & 0x01) return 0;
            return readPeriph ? readPeriph(a & 0x0F) : 0xFF;
        }
        return 0xFF;   // open bus in the unmapped register hole
    }
}

void ApplePic::regWrite(uint16_t a, uint8_t v)
{
    switch (a) {
    case 0xF010: case 0xF011: case 0xF012: case 0xF013:
        // bit0 = latch hi byte; writing $F011 also acks + arms the
        // one-shot for latch*8+12 clocks (`applepic.cpp:282-297`).
        if (a & 1) {
            timerLatch_ = static_cast<uint16_t>((v << 8) | (timerLatch_ & 0x00FF));
            if (a == 0xF011) {
                resetInterrupt(kIrqTimer);
                timerArmed_ = true;
                timerExpiry_ = clockNow_ + timerLatch_ * 8 + 12;
            }
        } else {
            timerLatch_ = static_cast<uint16_t>((timerLatch_ & 0xFF00) | v);
        }
        return;

    case 0xF020: case 0xF021: case 0xF022: case 0xF023: case 0xF024:
    case 0xF028: case 0xF029: case 0xF02A: case 0xF02B: case 0xF02C: {
        DmaChannel& channel = dma_[(a >> 3) & 1];
        switch (a & 7) {
        case 0:
            // DREQ (bit 1) reflects the request wire, not the write.
            channel.control = static_cast<uint8_t>((v & ~kDmaReq) |
                                                   (channel.control & kDmaReq));
            break;
        case 1: channel.map = static_cast<uint16_t>((channel.map & 0xFF00) | v); break;
        case 2: channel.map = static_cast<uint16_t>((v << 8) | (channel.map & 0x00FF)); break;
        case 3: channel.tc = static_cast<uint16_t>((channel.tc & 0x700) | v); break;
        case 4: channel.tc = static_cast<uint16_t>(((v & 0x07) << 8) | (channel.tc & 0x0FF)); break;
        }
        return;
    }

    case 0xF030:
        // bit 0 = host bypass. Leaving bypass with the peripheral /INT
        // pending raises the 6502's peripheral interrupt
        // (`applepic.cpp:429-441`); bit 7 drives gpout1.
        sccCtl_ = v;
        if (!(v & 0x01) && (statusReg_ & 0x40))
            setInterrupt(kIrqPeripheral);
        else
            resetInterrupt(kIrqPeripheral);
        if (gpOut) gpOut(1, (v & 0x80) != 0);
        return;

    case 0xF031:
        ioCtl_ = v;
        return;

    case 0xF032:
        // Writable bits $53 (MAME keeps $A0); bit 1 drives gpout0 — the
        // ADB output line on the IIfx's SWIM PIC.
        dpllCtl_ = static_cast<uint8_t>((dpllCtl_ & 0xA0) | (v & 0x53));
        if (gpOut) gpOut(0, (v & 0x02) != 0);
        return;

    case 0xF033:
        intMask_ = v & 0x3E;
        updateIrqLine();
        return;

    case 0xF034:
        // Write-1-to-clear.
        intReg_ &= static_cast<uint8_t>(~v);
        updateIrqLine();
        return;

    case 0xF035:
        // INTHST0/1 requests → status bits 4/5 → host interrupt line
        // (`applepic.cpp:542-550`).
        statusReg_ |= static_cast<uint8_t>((v & 0x0C) << 2);
        if ((v & 0x0C) && hostInt) hostInt(true);
        return;

    default:
        if ((a & 0xFFF0) == 0xF040) {
            if (sccCtl_ & 0x01) return;   // blocked in bypass mode
            if (writePeriph) writePeriph(a & 0x0F, v);
        }
        return;
    }
}

// ── Host window (`applepic.cpp:125-217`) ──────────────────────────────────

uint8_t ApplePic::hostRead(uint32_t offset)
{
    if (offset & 0x10) {
        // Direct peripheral access — bypass mode only.
        if (sccCtl_ & 0x01)
            return readPeriph ? readPeriph(offset & 0x0F) : 0xFF;
        return 0;
    }
    if (offset & 0x04) {
        // Shared-RAM data port: full internal decode, registers included
        // (MAME reads through the 6502's address space).
        const uint8_t data = read8(ramAddr_);
        if (statusReg_ & 0x02)
            ++ramAddr_;
        return data;
    }
    if (offset & 0x02) {
        // PINT (D6) and /REQ (D7) are meaningful in bypass mode only.
        return (sccCtl_ & 0x01) ? static_cast<uint8_t>(statusReg_ | 0x01)
                                : static_cast<uint8_t>((statusReg_ & 0x3F) | 0x80);
    }
    if (offset & 0x01)
        return static_cast<uint8_t>(ramAddr_);
    return static_cast<uint8_t>(ramAddr_ >> 8);
}

void ApplePic::hostWrite(uint32_t offset, uint8_t data)
{
    if (offset & 0x10) {
        if (sccCtl_ & 0x01) {
            if (writePeriph) writePeriph(offset & 0x0F, data);
        }
        return;
    }
    if (offset & 0x04) {
        write8(ramAddr_, data);
        if (statusReg_ & 0x02)
            ++ramAddr_;
        return;
    }
    if (offset & 0x02) {
        // bit2 = /RSTPIC. The release edge starts the 65C02's reset
        // sequence — vectors through the RAM the host just filled.
        if (((statusReg_ ^ data) & 0x04) && (data & 0x04))
            cpu_.softReset();
        if (data & 0x08)
            setInterrupt(kIrqHost);
        if ((statusReg_ & data & 0x30) != 0) {
            statusReg_ &= static_cast<uint8_t>(~(data & 0x30));
            if ((statusReg_ & 0x30) == 0 && hostInt)
                hostInt(false);   // all host interrupts acknowledged
        }
        statusReg_ = static_cast<uint8_t>((data & 0x06) | (statusReg_ & 0xF0));
        return;
    }
    if (offset & 0x01) {
        ramAddr_ = static_cast<uint16_t>((ramAddr_ & 0xFF00) | data);
        return;
    }
    ramAddr_ = static_cast<uint16_t>((data << 8) | (ramAddr_ & 0x00FF));
}

// ── Peripheral wires (`applepic.cpp:219-263`) ─────────────────────────────

void ApplePic::pintW(bool state)
{
    if (state) {
        statusReg_ |= 0x40;
        if (!(sccCtl_ & 0x01))
            setInterrupt(kIrqPeripheral);
    } else {
        statusReg_ &= 0xBF;
        if (!(sccCtl_ & 0x01))
            resetInterrupt(kIrqPeripheral);
    }
}

void ApplePic::reqaW(bool state)
{
    dma_[0].req = state;
    if (state) dma_[0].control |= kDmaReq;
    else       dma_[0].control &= ~kDmaReq;
}

void ApplePic::reqbW(bool state)
{
    dma_[1].req = state;
    if (state) dma_[1].control |= kDmaReq;
    else       dma_[1].control &= ~kDmaReq;
}

// ── Clock ─────────────────────────────────────────────────────────────────

void ApplePic::tick(int clocks)
{
    budget_ += clocks;
    while (budget_ > 0) {
        // One 65C02 instruction per iteration (one idle cycle when the
        // host still holds /RSTPIC); DMA and the timer advance with the
        // same instruction granularity, slaved to the CPU's cycle count.
        int cpuCycles = 1;
        if (!cpuHeld()) {
            cpu_.step();
            cpuCycles = cpu_.getCurrentInstructionCycles();
        }
        const int used = cpuCycles * 8;
        clockNow_ += used;
        budget_ -= used;

        dmaPhase_ += used;
        while (dmaPhase_ >= 8) {
            dmaPhase_ -= 8;
            dmaTick();
        }
        runTimer();
    }
}
