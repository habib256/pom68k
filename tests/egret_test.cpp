// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// O6.3 gate — Egret HLE transport + command set, driven exactly like the
// LC II ROM's own drivers (the acceptance oracle for the protocol):
// host raises SYS_SESSION, clocks bytes through the VIA1 shift register
// with VIA_FULL pulses, drops the session; Egret asserts XCVR_SESSION,
// replies [sync, status, status, cmdEcho, data…] and deasserts XCVR
// with the last byte. Also covers RTC/PRAM round-trips, the periodic
// timer packets (via-cuda.c: "Egret sends these periodically") and the
// abort of an unacknowledged Egret-initiated packet.
// Exit 0 = pass, 1 = fail.

#include "Via6522.h"
#include "Egret.h"
#include "AdbBus.h"

#include <cstdio>
#include <vector>

namespace {
int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}

struct Host {                                // the ROM-side handshake
    Via6522 via;
    Egret egret{via};
    uint8_t pb = 0;

    Host() {
        egret.reset();
        while (egret.cpuHeld()) egret.tick(1000);
    }
    void setPb(uint8_t v) { pb = v; egret.portBChanged(pb); }
    void tick(int n) { egret.tick(n); }

    void sendCommand(const std::vector<uint8_t>& bytes) {
        setPb(pb | 0x20);                    // SYS_SESSION up
        for (uint8_t b : bytes) {
            via.write(Via6522::SR, b);
            setPb(pb | 0x10);                // VIA_FULL rise: byte ready
            (void)via.read(Via6522::SR);     // host reads back, clears SHIFT
            setPb(pb & ~0x10);
        }
        setPb(pb & ~0x20);                   // session drop: Egret processes
    }

    // Reads a reply the way the ROM's $A4A2C4 driver does: wait for
    // XCVR + sync, join with SYS_SESSION, ack each byte, stop when
    // XCVR_SESSION deasserts with the byte just read.
    std::vector<uint8_t> readReply(int maxBytes = 300) {
        std::vector<uint8_t> r;
        for (int w = 0; w < 100 && !via.shiftPending(); w++) tick(100);
        if (!via.shiftPending()) return r;
        r.push_back(via.read(Via6522::SR));  // sync byte, no ack
        setPb(pb | 0x20);                    // join the read session
        tick(400);
        while (int(r.size()) < maxBytes) {
            for (int w = 0; w < 100 && !via.shiftPending(); w++) tick(100);
            if (!via.shiftPending()) break;
            bool last = egret.xcvrSession() != 0;  // dropped with this byte
            setPb(pb | 0x10);                // ack rise (before the read)
            r.push_back(via.read(Via6522::SR));
            setPb(pb & ~0x10);               // fall: clock the next byte
            tick(400);
            if (last) break;
        }
        setPb(pb & ~0x20);
        return r;
    }
};
} // namespace

int main() {
    std::printf("egret_test — Egret HLE transport + commands (O6.3)\n");

    {
        Host h;
        check(!h.egret.cpuHeld(), "power-on reset hold releases the CPU");

        // GET_TIME round-trip
        h.egret.setSeconds(0xA1B2C3D4);
        h.sendCommand({ 0x01, 0x03 });
        auto r = h.readReply();
        check(r.size() == 8, "GET_TIME reply is 8 bytes (sync+hdr+4)");
        check(r.size() == 8 && r[1] == 0x00 && r[2] == 0x00 && r[3] == 0x03,
              "GET_TIME header = [attn, status0(0), status1(0), cmd echo]");
        check(r.size() == 8 && r[4] == 0xA1 && r[5] == 0xB2 && r[6] == 0xC3 && r[7] == 0xD4,
              "GET_TIME returns the RTC seconds");
        check(h.egret.xcvrSession() == 1, "XCVR_SESSION idle after the reply");

        // SET_TIME
        h.sendCommand({ 0x01, 0x09, 0x11, 0x22, 0x33, 0x44 });
        (void)h.readReply();
        check(h.egret.seconds() == 0x11223344, "SET_TIME programs the clock");

        // PRAM round-trip ([1,$C,hi,lo,value] / [1,7,hi,lo])
        h.sendCommand({ 0x01, 0x0C, 0x00, 0xF9, 0x5A });
        auto ra = h.readReply();
        check(ra.size() == 4 && ra[3] == 0x0C, "SET_PRAM acks with the cmd echo last");
        h.sendCommand({ 0x01, 0x07, 0x00, 0xF9 });
        auto rp = h.readReply(5);            // stream: take 1 byte, drop
        check(rp.size() == 5 && rp[3] == 0x07 && rp[4] == 0x5A,
              "GET_PRAM returns the written byte");

        // ReadXPram (Egret cmd $02): [1, 2, 1, addr] → a byte STREAM with
        // no length on the wire (O6.11: the ROM's 'NuMc' check reads 4
        // bytes, its boot-flag read at $A0CC64 reads 1, both send the
        // same command shape). The HOST terminates by dropping
        // SYS_SESSION after its count; Egret must then release XCVR.
        h.sendCommand({ 0x01, 0x0C, 0x00, 0x8A, 0x00 });   // pram[$8A] = 0
        (void)h.readReply();
        h.sendCommand({ 0x01, 0x0C, 0x00, 0x8B, 0x42 });
        (void)h.readReply();
        h.sendCommand({ 0x01, 0x02, 0x01, 0x8A });
        auto rx = h.readReply(6);            // take sync+hdr+2 bytes only
        check(rx.size() == 6 && rx[3] == 0x02 && rx[4] == 0x00 && rx[5] == 0x42,
              "ReadXPram streams bytes from the given offset");
        check(h.egret.xcvrSession() == 1,
              "host session drop mid-stream releases XCVR_SESSION");

        // WriteXPram (Egret cmd $08): [1, 8, 1, addr, data…] — the data
        // length IS the count (the ROM's 'NuMc' signature + cold-PRAM
        // re-init go through this; unhandled, every boot re-inits).
        h.sendCommand({ 0x01, 0x08, 0x01, 0x8A, 0x4E, 0x75, 0x4D, 0x63 });
        auto rw = h.readReply();
        check(rw.size() == 4 && rw[3] == 0x08, "WriteXPram acks");
        h.sendCommand({ 0x01, 0x02, 0x01, 0x8A });
        auto rv = h.readReply(8);
        check(rv.size() == 8 && rv[4] == 0x4E && rv[5] == 0x75
              && rv[6] == 0x4D && rv[7] == 0x63,
              "WriteXPram stored the block (read back via the stream)");

        // ADB Talk on an idle keyboard: no-response still shapes a reply
        h.sendCommand({ 0x00, 0x2C });       // kbd addr 2, talk reg 0
        auto rk = h.readReply();
        check(rk.size() >= 4 && rk[3] == 0x2C, "ADB talk echoes the ADB command");
    }

    // Periodic timer packets: Egret-initiated, governed by pseudo cmd
    // $1B (one-second mode — captured from both the LC II ROM, $1B 00,
    // and Sys 7.5 / Mac OS 8.1, $1B 03). Power-on default is mode 1:
    // every tick is the full 10-byte boot heartbeat (the ROM's D1=8
    // reader at $A15376). After a mode change the FIRST packet is still
    // the full form (ERS), then the mode's own shape — mode 3 = the
    // short [sync, TIMER(3), seconds] form.
    {
        Host h;
        h.tick(15667200 + 200000);           // one emulated second + margin
        check(h.egret.xcvrSession() == 0, "timer tick: Egret asserts XCVR");
        check(h.via.shiftPending(), "timer tick: sync byte clocked");
        auto r = h.readReply();
        check(r.size() == 10 && r[0] == 0x01 && r[1] == 0x03,
              "mode-1 tick = 10-byte boot heartbeat, type 3");
        h.sendCommand({ 0x01, 0x1B, 0x03 });  // one-second mode 3
        (void)h.readReply();
        h.tick(15667200 + 300000);
        auto r2 = h.readReply();
        check(r2.size() == 10 && r2[0] == 0x01 && r2[1] == 0x03,
              "first tick after $1B is still the full form");
        h.tick(15667200 + 300000);
        auto r3 = h.readReply();
        check(r3.size() == 3 && r3[0] == 0x01 && r3[1] == 0x03,
              "mode-3 ticks = short [sync, TIMER(3), seconds]");
        h.sendCommand({ 0x01, 0x1B, 0x00 });  // off
        (void)h.readReply();
        h.tick(2 * 15667200 + 300000);
        check(h.egret.xcvrSession() == 1 && !h.via.shiftPending(),
              "mode 0 = no timer packets at all");
    }

    // An initiated packet is COMMITTED once its sync byte is on the wire
    // (2026-07-17): the sync sits in the VIA SR with the host's level-1
    // interrupt in flight, so retracting manufactures a "ghost" 1-byte
    // session when the host services it late (SC2K's per-VBL redraw
    // preempts the ROM byte loop 300K+ cycles; the driver computes the
    // ADB record length as received-4 = -3 and dbra-copies 64KB over
    // the stack — the "coprocesseur absent" bomb). The real Egret's
    // handshake is synchronous and host-clocked: it WAITS. A slow host
    // must still find XCVR asserted and the packet intact.
    {
        Host h;
        h.tick(15667200 + 200000);
        check(h.egret.xcvrSession() == 0, "unacked packet: XCVR asserted");
        h.tick(400000);                      // host preempted a long time
        check(h.egret.xcvrSession() == 0,
              "unacked packet stays committed (no mid-flight retraction)");
        auto r = h.readReply();              // late host still gets it whole
        check(r.size() == 10 && r[0] == 0x01 && r[1] == 0x03,
              "late host reads the full packet");
    }

    std::printf("%s\n", gFails ? "FAILED" : "PASSED");
    return gFails ? 1 : 0;
}
