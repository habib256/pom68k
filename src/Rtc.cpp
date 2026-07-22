// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Rtc.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

void Rtc::reset() {
    phase_ = CMD; bitCnt_ = 0; shift_ = 0; cmd_ = 0; out_ = 1;
    enabled_ = false; lastClk_ = false;
    // seconds_ / pram_ survive reset (battery-backed)
}

void Rtc::factoryDefaults() {
    // Basilisk II XPRAMInit (main.cpp) verbatim. Always (re)seed
    // AppleTalk-inactive SPConfig ($13) even when 'NuMc' is already
    // present — AppleTalk 57.x self-heals 0/$F → active, and a prior boot
    // may have left it on.
    const bool hadSig = pram_[0x0C] == 0x4E && pram_[0x0D] == 0x75
                     && pram_[0x0E] == 0x4D && pram_[0x0F] == 0x63;
    if (!hadSig) {
        std::memset(pram_, 0, sizeof pram_);
        pram_[0x0C] = 0x4E; pram_[0x0D] = 0x75;   // 'NuMc' validity
        pram_[0x0E] = 0x4D; pram_[0x0F] = 0x63;
        pram_[0x01] = 0x80;                        // InternalWaitFlags = DynWait
        pram_[0x08] = 0x13; pram_[0x09] = 0x88;   // classic PRAM 16-19
        pram_[0x0A] = 0x00; pram_[0x0B] = 0xCC;
        pram_[0x10] = 0xA8; pram_[0x11] = 0x00;   // SysParam (classic PRAM 0-7)
        pram_[0x12] = 0x00; pram_[0x13] = 0x22;   // SPConfig: both ports async
        pram_[0x14] = 0xCC; pram_[0x15] = 0x0A;
        pram_[0x16] = 0xCC; pram_[0x17] = 0x0A;
        pram_[0x1C] = 0x00; pram_[0x1D] = 0x02;
        pram_[0x1E] = 0x63; pram_[0x1F] = 0x00;
        pram_[0x76] = 0x00;                        // OSDefault = MacOS
        pram_[0x77] = 0x01;                        // StartBoot wantType source:
                                                   // ddType 1 → Apple_HFS hunt
        // $78-$7B (default startup drive/driver) stay 0 → ROM scans SCSI
    }
    // SPConfig low nibble = printer port use: 2 = async (AppleTalk OFF,
    // the deterministic default), 1 = AppleTalk. POM68K_APPLETALK=1 seeds
    // it ACTIVE so headless LLAP tests skip the Chooser toggle.
    pram_[0x13] = std::getenv("POM68K_APPLETALK") ? 0x21 : 0x22;
}

uint8_t Rtc::readReg(uint8_t cmd) const {
    uint8_t addr = (cmd >> 2) & 0x1F;
    if (addr < 8)   return uint8_t(seconds_ >> (8 * (addr & 3)));
    if (addr < 12)  return pram_[addr];        // XPRAM $08-$0B
    if (addr < 16)  return 0xFF;               // test / write-protect: write-only
    return pram_[addr];                        // XPRAM $10-$1F
}

void Rtc::writeReg(uint8_t cmd, uint8_t v) {
    uint8_t addr = (cmd >> 2) & 0x1F;
    if (addr == 13) { writeProtect_ = (v & 0x80) != 0; return; }
    if (addr == 12) return;                    // test register: ignored
    if (writeProtect_) return;
    if (addr < 8)       seconds_ = (seconds_ & ~(0xFFu << (8 * (addr & 3))))
                                 | (uint32_t(v) << (8 * (addr & 3)));
    else if (addr < 12) pram_[addr] = v;       // XPRAM $08-$0B
    else if (addr < 16) return;                // 14/15 reserved: drop
    else                pram_[addr] = v;       // XPRAM $10-$1F
}

void Rtc::setLines(bool enable, bool clock, bool dataOut) {
    if (!enable) {                             // /enable high = idle: reset shifter
        enabled_ = false; phase_ = CMD; bitCnt_ = 0; shift_ = 0; out_ = 1;
        lastClk_ = clock;
        return;
    }
    if (!enabled_) { enabled_ = true; phase_ = CMD; bitCnt_ = 0; shift_ = 0; }

    // Bits move on the clock's HIGH→LOW transition (MAME macrtc
    // rtc_shift_data: the chip updates data out / samples data in on the
    // falling edge; the host then reads or changes the line while the
    // clock is low and raises it again). The old rising-edge model was
    // half a cycle early: the Plus ROM tolerated it, but the Mac II ROM's
    // XPRAM validity read came back bit-shifted, so it re-initialized
    // PRAM on every boot and boot-device defaults never survived.
    bool falling = !clock && lastClk_;
    lastClk_ = clock;
    if (!falling) return;

    switch (phase_) {
        case CMD:
        case WRITE_DATA:
        case XP_ADDR:
        case XP_WRITE:
            shift_ = uint8_t((shift_ << 1) | (dataOut ? 1 : 0));
            if (++bitCnt_ < 8) break;
            bitCnt_ = 0;
            if (phase_ == CMD) {
                cmd_ = shift_; shift_ = 0;
                if (getenv("RTCDBG")) fprintf(stderr, "[rtc] cmd %02X\n", cmd_);
                if ((cmd_ & 0x78) == 0x38)     // extended XPRAM sequence
                    phase_ = XP_ADDR;
                else if (cmd_ & 0x80) {
                    // Do NOT present bit 7 yet: the chip drives each data
                    // bit on the NEXT falling edge (MAME rtc_shift_data
                    // "--m_bit_count"). Presenting it on the command's own
                    // completion edge made the host sample bits 6..0 plus
                    // a trailing idle 1 — every read byte came back as
                    // (v << 1) | 1 (Mac II GetDefaultStartup $DF → $BF).
                    phase_ = READ_DATA;
                    outData_ = readReg(cmd_);
                    bitCnt_ = 8;
                    if (getenv("RTCDBG")) fprintf(stderr, "[rtc] read    reg %2d -> %02X\n", (cmd_ >> 2) & 0x1F, outData_);
                } else
                    phase_ = WRITE_DATA;
            } else if (phase_ == XP_ADDR) {
                // addr = first byte bits 2-0 (hi) + second byte bits 6-2 (lo)
                xpAddr_ = uint8_t(((cmd_ & 7) << 5) | ((shift_ & 0x7C) >> 2));
                shift_ = 0;
                if (cmd_ & 0x80) {             // extended read (bit 7 on next edge)
                    phase_ = READ_DATA;
                    outData_ = pram_[xpAddr_];
                    bitCnt_ = 8;
                    if (getenv("RTCDBG")) fprintf(stderr, "[rtc] xpread  $%02X -> %02X\n", xpAddr_, outData_);
                } else
                    phase_ = XP_WRITE;
            } else if (phase_ == XP_WRITE) {
                if (getenv("RTCDBG")) fprintf(stderr, "[rtc] xpwrite $%02X <- %02X%s\n", xpAddr_, shift_, writeProtect_ ? " (WP!)" : "");
                if (!writeProtect_) pram_[xpAddr_] = shift_;
                shift_ = 0; phase_ = DONE;
            } else {                           // WRITE_DATA (classic)
                writeReg(cmd_, shift_); shift_ = 0; phase_ = DONE;
            }
            break;
        case READ_DATA:                        // one bit per falling edge, MSB first
            if (bitCnt_ > 0) out_ = (outData_ >> --bitCnt_) & 1;
            else { phase_ = DONE; out_ = 1; }
            break;
        case DONE:
            break;
    }
}
