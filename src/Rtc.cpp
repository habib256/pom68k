// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "Rtc.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

void Rtc::reset() {
    phase_ = CMD; bitCnt_ = 0; shift_ = 0; cmd_ = 0; out_ = 1;
    enabled_ = false; lastClk_ = false;
    // seconds_ / pram_ survive reset (battery-backed) — and so does
    // writeProtect_. RICHER THAN MAME (audit § 2.2): device_reset() clears
    // m_write_protect (macrtc.cpp:113-121), but the WP latch lives on the
    // same battery domain as the clock and the PRAM it guards; a /RESET
    // pulse to the host cannot unlock the chip on silicon. Do not "fix"
    // this back to MAME — a reset that dropped WP would let a crashing
    // guest scribble over locked PRAM.
}

void Rtc::factoryDefaults() {
    // RICHER THAN MAME (audit § 2.2): MAME's nvram_default() is a bare
    // memset(m_pram, 0, 0x100) (macrtc.cpp:397-400) — a cold MAME Mac boots
    // on an invalid PRAM and lets the ROM re-initialize whatever it feels
    // like. POM68K seeds a known-good, byte-for-byte deterministic image so
    // every etalon starts from the same guest configuration. Keep.
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
    // it ACTIVE so headless LLAP tests skip the Chooser toggle. "=0" is the
    // global AppleTalk-off switch, so it must NOT seed active.
    const char* atalk = std::getenv("POM68K_APPLETALK");
    pram_[0x13] = (atalk && std::strcmp(atalk, "0") != 0) ? 0x21 : 0x22;
}

// The battery file. Short or missing → false, and the caller keeps the
// factory image it already seeded; a partial file is never half-applied.
bool Rtc::loadPram(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    if (b.size() < sizeof pram_) return false;
    std::memcpy(pram_, b.data(), sizeof pram_);
    return true;
}

void Rtc::savePram(const std::string& path) const {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return;
    out.write(reinterpret_cast<const char*>(pram_), sizeof pram_);
}

uint8_t Rtc::readReg(uint8_t cmd) const {
    uint8_t addr = (cmd >> 2) & 0x1F;
    if (addr < 8)   return uint8_t(seconds_ >> (8 * (addr & 3)));
    if (addr < 12)  return pram_[addr];        // XPRAM $08-$0B
    // Regs 12 (test) / 13 (write-protect) / 14-15 (reserved) are write-only:
    // the read decoder selects nothing. Aligned on MAME (audit § 2.2), whose
    // read switch has no case for them and falls through to
    // `m_data_byte = 0` (macrtc.cpp:380-383) — same class as the
    // undecoded-read findings #45/#59, where the house answer is 0 and not
    // open-bus $FF. Cold path either way: no shipped ROM reads these, and
    // only 12/13 can even arrive — a read of 14/15 would be command $B8/$BC,
    // which (cmd & $78) == $38 claims for the extended-XPRAM sequence in
    // setLines() before this decode ever runs (same overlap in MAME).
    if (addr < 16)  return 0x00;
    return pram_[addr];                        // XPRAM $10-$1F
}

void Rtc::writeReg(uint8_t cmd, uint8_t v) {
    uint8_t addr = (cmd >> 2) & 0x1F;
    if (addr == 13) { writeProtect_ = (v & 0x80) != 0; return; }
    // Test register: the chip would reset the seconds counter and clock it
    // at the raw 32768 Hz. MAME latches bit 7 into m_test_mode and admits
    // "(not implemented)" (macrtc.cpp:298-303) — a saved field no code ever
    // reads. NOT ALIGNED (audit § 2.2, inert on both sides): mirroring it
    // would add a member to visit<Ar>(), i.e. a save-state layout change,
    // to model exactly nothing. Reopen with the 32768 Hz behaviour itself.
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
                // RICHER THAN MAME (audit § 2.2): the WP gate covers the
                // EXTENDED write too. MAME checks m_write_protect only on
                // the classic register path (macrtc.cpp:279-283) and its
                // RTC_STATE_XPWRITE arm stores unconditionally (:267-272) —
                // so a MAME guest can write locked XPRAM through the
                // extended command. Keep the gate on both doors.
                if (!writeProtect_) pram_[xpAddr_] = shift_;
                shift_ = 0; phase_ = DONE;
            } else {                           // WRITE_DATA (classic)
                writeReg(cmd_, shift_); shift_ = 0; phase_ = DONE;
            }
            break;
        case READ_DATA:                        // one bit per falling edge, MSB first
            // RICHER THAN MAME (audit § 2.2): the 9th and later clocks of a
            // read end the transaction cleanly. MAME shifts unguarded —
            // `m_data_out = (m_data_byte >> --m_bit_count) & 0x01` with
            // m_bit_count a u8 (macrtc.cpp:223, macrtc.h:73) — so a host
            // that overclocks the byte underflows the counter to 255 and
            // shifts by 255. Keep the guard.
            if (bitCnt_ > 0) out_ = (outData_ >> --bitCnt_) & 1;
            else { phase_ = DONE; out_ = 1; }
            break;
        case DONE:
            break;
    }
}
