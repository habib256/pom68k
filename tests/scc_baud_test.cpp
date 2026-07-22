// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// SCC async-baud LLE gate — the guest-programmed serial timing derivation,
// pinned against MAME z80scc.cpp:
//   get_clock_mode (:1157)  WR4 bits 7-6 → ×1/16/32/64
//   get_brg_rate   (:2476)  rate = src / (2+(WR13<<8|WR12)) / (2·mode),
//                           src = WR14 bit 1 ? PCLK : RTxC (3.6864 MHz)
//   update_serial  (:2565)  WR11 bits 4-3 route the Tx clock
// and the Mac wiring facts: RTxC = 3.6864 MHz on every machine (MAME
// configure_channels; LCII_HARDWARE.md), so SDLC/LocalTalk = RTxC/16 =
// 230 400 bit/s — which must derive EXACTLY the historical fixed constants
// (272 @ 7.8336 MHz, 544 @ 15.6672 MHz, 868 @ 25 MHz): the LLAP wire keeps
// its timing when the fixed byteCycles_ gives way to the derivation.

#include "Scc8530.h"
#include <cstdio>

namespace {
int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-62s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}
void wrReg(Scc8530& s, int ch, int reg, uint8_t v) {
    s.writeCtl(ch, uint8_t(reg));
    s.writeCtl(ch, v);
}
} // namespace

int main() {
    std::printf("scc_baud_test — WR4/WR11/WR12-14 baud machinery (SCC LLE)\n");

    {   // Bare chip, no machine clocks: legacy fixed pacing only.
        Scc8530 s;
        s.reset();
        check(s.paceCycles(0) == 544, "no clocks: legacy 544-cycle fallback");
        s.setByteCycles(272);
        check(s.paceCycles(0) == 272, "setByteCycles still overrides the fallback");
        wrReg(s, 0, 4, 0x20);                     // SDLC — still no clocks
        check(s.paceCycles(0) == 272, "programming without clocks keeps legacy pace");
    }

    {   // SDLC derives the exact historical LocalTalk constants.
        Scc8530 s; s.reset();
        s.setClocks(15667200, 7833600);           // LC II / Mac II
        check(s.paceCycles(0) == 544, "reset regs (sync, non-SDLC) fall back to 544");
        wrReg(s, 0, 4, 0x20);                     // SDLC mode
        check(s.paceCycles(0) == 544, "SDLC @ 15.6672 MHz derives 544 (RTxC/16)");
        Scc8530 q; q.reset(); q.setClocks(25000000, 7833600);
        wrReg(q, 0, 4, 0x20);
        check(q.paceCycles(0) == 868, "SDLC @ 25 MHz derives 868");
        Scc8530 p; p.reset(); p.setClocks(7833600, 3916800);
        wrReg(p, 0, 4, 0x20);
        check(p.paceCycles(0) == 272, "SDLC @ 7.8336 MHz derives 272");
    }

    {   // Async via the BRG — the Mac serial-driver programming shape.
        Scc8530 s; s.reset();
        s.setClocks(15667200, 7833600);
        wrReg(s, 1, 5, 0x60);                     // 8 data bits (Tx)
        wrReg(s, 1, 4, 0x44);                     // X16, 1 stop, no parity
        wrReg(s, 1, 12, 10);                      // BRG constant 10 → 9600
        wrReg(s, 1, 13, 0);
        wrReg(s, 1, 11, 0x10);                    // Tx clock = BRG
        wrReg(s, 1, 14, 0x01);                    // BRG enable, source RTxC
        // 3.6864e6/(10+2)/(2*16) = 9600 baud; 10 bits/char @ 15.6672 MHz
        check(s.paceCycles(1) == 16320, "9600 8N1 via BRG(RTxC): 16320 cycles/byte");
        check(s.paceCycles(0) == 544, "channel B is untouched by channel A's regs");
        wrReg(s, 1, 4, 0x45);                     // + parity bit (8E1-ish)
        check(s.paceCycles(1) == 17952, "parity adds one bit time (11 bits/char)");
        wrReg(s, 1, 4, 0x4C);                     // 2 stop bits, no parity
        check(s.paceCycles(1) == 17952, "2 stop bits add one bit time too");
        wrReg(s, 1, 4, 0x44);
        wrReg(s, 1, 14, 0x03);                    // BRG source = PCLK
        // 7.8336e6/12/32 = 20400 baud → 15667200*10/20400 = 7680
        check(s.paceCycles(1) == 7680, "BRG source PCLK follows WR14 bit 1");
    }

    {   // Async clocked straight off RTxC (WR11 source 00).
        Scc8530 s; s.reset();
        s.setClocks(15667200, 7833600);
        wrReg(s, 1, 5, 0x60);
        wrReg(s, 1, 11, 0x00);                    // Tx clock = RTxC pin
        wrReg(s, 1, 4, 0x44);                     // X16 → 230 400 baud
        check(s.paceCycles(1) == 680, "X16 direct RTxC: 230.4 kbaud, 680 cycles");
        wrReg(s, 1, 4, 0x84);                     // X32
        check(s.paceCycles(1) == 1360, "X32 halves the bit rate");
        wrReg(s, 1, 11, 0x08);                    // TRxC pin: not modelled
        check(s.paceCycles(1) == 544, "unmodelled clock source falls back");
    }

    if (gFails) { std::printf("FAILED (%d)\n", gFails); return 1; }
    std::printf("PASS\n");
    return 0;
}
