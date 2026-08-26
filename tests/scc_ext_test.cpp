// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// O6.10 gate — SCC external/status interrupt for AppleTalk carrier sense.
// The LC II has no LocalTalk peer; the SDLC receiver sees a standing
// Break/Abort on the open line (RR0 bit 7). The System's LAP manager
// arms WR15 bit 7 (Break/Abort IE) and sleeps until the resulting
// external/status interrupt tells it the wire is idle. Without it,
// GISTPERSO's boot hangs at the "Bienvenue" progress bar (Scc8530.h,
// CHANGELOG 2026-07-15).
//
// Checks: (1) open-line RR0 carries Break/Abort; (2) arming WR15 bit 7
// with abort-idle latches an ext interrupt and asserts IRQ once WR9 MIE
// is set; (3) Reset Ext/Status clears it; (4) WR2/WR9 are chip-global.
// Exit 0 = pass, 1 = fail.

#include "Scc8530.h"
#include "V8Memory.h"

#include <cstdio>
#include <vector>

namespace {
int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-56s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}
// Point WR0 at register r on channel ch, then write v to it.
void writeReg(Scc8530& scc, int ch, int r, uint8_t v) {
    scc.writeCtl(ch, uint8_t(r & 7) | ((r & 8) ? 0x08 : 0));  // WR0 pointer
    scc.writeCtl(ch, v);
}
} // namespace

int main() {
    std::printf("scc_ext_test — LAP carrier-sense ext interrupt (O6.10)\n");

    // Channel A = index 1 (the LC II runs LocalTalk on port B = index 0,
    // but the LAP manager arms both; test channel B).
    constexpr int B = 0;

    // ── 1. open line: RR0 Break/Abort standing (SDLC hunt only) ────────
    {
        Scc8530 scc; scc.reset();
        check(!(scc.readCtl(B) & 0x80), "closed line: RR0 bit 7 clear");
        scc.setAbortIdle(true);
        // In async modes continuous marks are normal idle, NOT a break —
        // the abort condition exists only while hunting in SDLC mode
        // (WR4 bits 5-4 = 10), which is what the LAP programs.
        check(!(scc.readCtl(B) & 0x80), "open line, async mode: no Break/Abort");
        writeReg(scc, B, 4, 0x20);              // WR4: SDLC mode (LAP init)
        // LLE step 7 (§1.10): a VIRGIN line — never driven since reset —
        // reads CLEAN even on the open no-peer connector: FM0 gives the
        // DPLL no edge, so there is no recovered clock to sample an abort
        // with. This is the line state OT's .MPP bind waits for.
        check(!(scc.readCtl(B) & 0x80), "virgin open line: still no Break/Abort");
        // The first transmitted frame drives the line (the LLAP trailer
        // ends in a real abort, then the driver releases it mid-mark) —
        // WR0 $C0 opens an SDLC frame, the idle shifter underruns and the
        // tail (CRC+flag) drains in 3 byte times: EOM = line driven.
        scc.writeCtl(B, 0xC0);                  // Reset Tx Underrun/EOM latch
        scc.tick(3 * 544 + 100);                // drain the SDLC tail
        check((scc.readCtl(B) & 0x80) != 0, "driven open line: RR0 bit 7 (Break/Abort) set");
    }

    // ── 2. arm WR15 bit 7 → ext int latched; WR9 MIE gates IRQ ─────────
    {
        Scc8530 scc; scc.reset();
        scc.setAbortIdle(true);
        writeReg(scc, B, 4, 0x20);              // WR4: SDLC mode (LAP init)
        writeReg(scc, B, 1, 0x01);              // WR1: ext int enable
        writeReg(scc, B, 15, 0x80);             // WR15: Break/Abort IE
        check(!scc.irqAsserted(), "armed on a virgin line: nothing to latch");
        // Drive the line: the first frame's trailer abort is what starts
        // the LAP's abort stream on a previously-virgin line (EOM latch
        // presents Break/Abort when only WR15 bit 7 is armed).
        scc.writeCtl(B, 0xC0);
        scc.tick(3 * 544 + 100);
        check(!scc.irqAsserted(), "ext latched but WR9 MIE off: no IRQ yet");
        writeReg(scc, B, 9, 0x08);              // WR9: master int enable
        check(scc.irqAsserted(), "WR9 MIE on: IRQ asserted");

        // RR0 read returns the value latched at interrupt time
        check((scc.readCtl(B) & 0x80) != 0, "RR0 latch carries Break/Abort");

        // ── 3. Reset Ext/Status clears the latch ───────────────────────
        scc.writeCtl(B, 0x10);                  // WR0 cmd: Reset Ext/Status
        check(!scc.irqAsserted(), "Reset Ext/Status: IRQ cleared");

        // ── 3b. servicing re-arms the standing abort (event-driven) ────
        check(!scc.tick(1000), "abort not re-presented before ~130 µs");
        check(scc.tick(1500), "serviced abort re-presents after ~130 µs");
        check(scc.irqAsserted(), "re-presented abort asserts IRQ");
        // No storm: an armed-but-unserviced channel latches exactly once
        check(!scc.tick(10000), "no re-latch without a Reset Ext/Status");
    }

    // ── 2b. WR1 bit 0 (Ext Int Enable) gates the abort latch ───────────
    {
        Scc8530 scc; scc.reset();
        scc.setAbortIdle(true);
        writeReg(scc, B, 4, 0x20);              // SDLC mode
        writeReg(scc, B, 15, 0x80);             // Break/Abort IE, WR1.0 off
        writeReg(scc, B, 9, 0x08);
        scc.writeCtl(B, 0xC0);                  // drive the line (EOM tail)
        scc.tick(3 * 544 + 100);
        check(!scc.irqAsserted(), "WR15 bit 7 without WR1 bit 0: no IRQ");
    }

    // ── 5. TxIP is edge-triggered (became-empty), not level ────────────
    // Since the Tx-engine LLE the buffer hands its byte to a PACED shifter
    // (one character time each) and only under WR5 bit 3 Tx Enable — the
    // real driver enables Tx before writing, so the tests do too.
    {
        Scc8530 scc; scc.reset();
        writeReg(scc, B, 9, 0x08);              // MIE
        writeReg(scc, B, 5, 0x68);              // Tx 8-bit + Tx Enable
        writeReg(scc, B, 1, 0x02);              // Tx Int Enable, no data yet
        check(!scc.irqAsserted(), "Tx IE on a never-filled buffer: no IRQ");
        scc.writeData(B, 0x55);                 // buffer → idle shifter at once
        check(scc.irqAsserted(), "data write with Tx IE: TxIP latches");
        scc.writeCtl(B, 0x28);                  // Reset Tx Int Pending
        check(!scc.irqAsserted(), "Reset Tx Int Pending clears TxIP");
        writeReg(scc, B, 1, 0x02);              // re-enable, still no new data
        check(!scc.irqAsserted(), "re-enabling Tx IE alone does not re-latch");
        scc.writeData(B, 0xAA);                 // shifter busy: byte parks
        check(!scc.irqAsserted(), "byte parked behind the shifter: no TxIP yet");
        check(!(scc.readCtl(B) & 0x04), "RR0 TBE low while the buffer is full");
        scc.tick(600);                          // one character time (544)
        check(scc.irqAsserted(), "shifter reload latches TxIP again");
        check((scc.readCtl(B) & 0x04) != 0, "RR0 TBE back up after the reload");
    }

    // ── 5c. WR5 bit 3 gates the transmitter ────────────────────────────
    {
        Scc8530 scc; scc.reset();
        writeReg(scc, B, 9, 0x08);              // MIE
        writeReg(scc, B, 1, 0x02);              // Tx IE — but Tx DISABLED
        scc.writeData(B, 0x55);
        check(!scc.irqAsserted(), "Tx disabled: byte waits, no TxIP");
        check(!(scc.readCtl(B) & 0x04), "Tx disabled: TBE stays low");
        scc.tick(5000);
        check(!scc.irqAsserted(), "Tx disabled: shifter never takes the byte");
        writeReg(scc, B, 5, 0x68);              // Tx Enable flushes the buffer
        check(scc.irqAsserted(), "enabling Tx hands the parked byte over");
    }

    // ── 5b. SDLC Tx Underrun/EOM latch (frame-complete interrupt) ──────
    {
        Scc8530 scc; scc.reset();
        writeReg(scc, B, 4, 0x20);              // SDLC mode
        writeReg(scc, B, 9, 0x08);              // MIE
        writeReg(scc, B, 5, 0x68);              // Tx 8-bit + Tx Enable
        writeReg(scc, B, 1, 0x01);              // ext int enable
        writeReg(scc, B, 15, 0x40);             // Tx Underrun/EOM IE
        check((scc.readCtl(B) & 0x40) != 0, "idle transmitter: RR0 bit 6 set");
        scc.writeCtl(B, 0xC0);                  // WR0: Reset Tx Underrun/EOM
        check(!(scc.readCtl(B) & 0x40), "frame open: latch reset");
        scc.writeData(B, 0x55);                 // frame byte
        check(!scc.irqAsserted(), "mid-frame: no EOM interrupt yet");
        scc.tick(2500);                         // byte (544) + CRC/flag (1632)
        check((scc.readCtl(B) & 0x40) != 0, "underrun: latch sets again");
        check(scc.irqAsserted(), "EOM latch 0->1 raises ext/status IRQ");
        scc.writeCtl(B, 0x10);                  // Reset Ext/Status
        check(!scc.irqAsserted(), "Reset Ext/Status clears EOM interrupt");
    }

    // ── 4. WR2/WR9 are chip-global (written via B, read state via A) ───
    {
        Scc8530 scc; scc.reset();
        writeReg(scc, B, 2, 0x18);              // vector, through channel B
        writeReg(scc, B, 9, 0x08);              // MIE, through channel B
        check(scc.wr(1, 2) == 0x18, "WR2 mirrored to channel A");
        check(scc.wr(1, 9) == 0x08, "WR9 mirrored to channel A");
    }

    // ── 6. V8 word access must not double-advance the register pointer ─
    {
        V8Memory mem(pom68k::defaultCoreConfig(), 4u << 20);
        std::vector<uint8_t> rom(V8Memory::kRomSize, 0xFF);
        rom[0] = 0x35; rom[1] = 0xC2; rom[2] = 0x8F; rom[3] = 0x5F;
        mem.loadRom(rom);
        mem.reset();
        // Word read: one side-effect, byte mirrored on both lanes.
        mem.write16(0xF04000, 0x0000);          // WR0 = 0 (point at RR0)
        uint16_t w1 = mem.read16(0xF04000);
        check(((w1 >> 8) & 0xFF) == (w1 & 0xFF), "SCC word read mirrors both lanes");
        // Word write after an explicit pointer: high byte only, once.
        mem.write8(0xF04000, 9);                // WR0 pointer = WR9
        mem.write16(0xF04000, 0x0800);          // WR9 = $08 (MIE)
        check(mem.scc().wr(0, 9) == 0x08, "SCC word write hits WR9 once");
        check(mem.scc().wr(1, 9) == 0x08, "SCC WR9 still chip-global after word write");
    }

    std::printf("%s\n", gFails ? "FAILED" : "PASSED");
    return gFails ? 1 : 0;
}
