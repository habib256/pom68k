// POM68K — gate `llap_loop_test`: two Scc8530 instances wired as a virtual
// LLAP cable (onTxFrame → injectRxFrame both ways). Models the LocalTalk
// address-acquisition dialogue: a node probes its tentative ID with ENQ
// frames (dst = src = ID); a node already holding that ID sees them (SDLC
// address match) — a node with another ID must NOT (hardware filter).
// Pins: SDLC Tx frame capture (underrun = frame end, Send Abort discards),
// Rx FIFO pacing at wire speed, Hunt exit/re-entry (carrier sense), per-byte
// Rx interrupts, End-of-Frame + FCS tail in RR1, broadcast $FF.

#include "Scc8530.h"
#include <cstdio>
#include <cstring>
#include <vector>

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { \
    std::printf("FAIL: %s\n", msg); failures++; } } while (0)

namespace {
constexpr int kB = 0;                  // channel B = LocalTalk (printer port)
constexpr int kByteCyc = 544;          // 230.4 kbit/s @ 15.6672 MHz

void wr(Scc8530& s, int reg, uint8_t v) {
    s.writeCtl(kB, uint8_t(reg));
    s.writeCtl(kB, v);
}

// Program channel B the way the LAP Manager arms LocalTalk receive:
// SDLC mode, Rx 8 bits + enable + hunt + address search, node ID in WR6,
// ext + Rx-int-all enabled, MIE on.
void lapArm(Scc8530& s, uint8_t nodeId) {
    wr(s, 9, 0x08);                    // MIE
    wr(s, 4, 0x20);                    // SDLC
    wr(s, 6, nodeId);                  // SDLC address
    wr(s, 3, 0xD5);                    // Rx 8-bit, hunt, addr search, RxEN
    wr(s, 15, 0x10);                   // Sync/Hunt IE (carrier sense)
    wr(s, 1, 0x11);                    // Rx int on all chars + ext IE
}

// Transmit one LLAP frame the way the driver does: Reset Tx Underrun/EOM
// (frame start), stream the bytes, let the shifter drain (underrun = CRC +
// closing flag on the real wire).
void lapSend(Scc8530& s, const uint8_t* d, size_t n) {
    s.writeCtl(kB, 0xC0);              // WR0: Reset Tx Underrun/EOM latch
    for (size_t i = 0; i < n; i++) s.writeData(kB, d[i]);
    s.tick(2000);                      // > kUnderrunDelay: frame completes
}

// Same, but without advancing time — the caller owns the clock (used by the
// RTS/CTS dialogue where both ends are co-stepped).
void lapSendNoWait(Scc8530& s, const uint8_t* d, size_t n) {
    s.writeCtl(kB, 0xC0);
    for (size_t i = 0; i < n; i++) s.writeData(kB, d[i]);
}

// Drain B's Rx FIFO while pacing the wire; returns the delivered bytes and
// the RR1 status of the last one.
std::vector<uint8_t> lapDrain(Scc8530& s, uint8_t& lastRr1, int maxByteTimes = 64) {
    std::vector<uint8_t> got;
    for (int t = 0; t < maxByteTimes; t++) {
        s.tick(kByteCyc);
        s.writeCtl(kB, 0x10);                  // service ext/status like the
                                               // real LAP ISR (Reset Ext/St)
        while (true) {
            s.writeCtl(kB, 0);                 // point at RR0
            if (!(s.readCtl(kB) & 0x01)) break; // Rx char available?
            s.writeCtl(kB, 1);                 // RR1 of the FIFO-top byte
            lastRr1 = s.readCtl(kB);
            got.push_back(s.readData(kB));
        }
    }
    return got;
}
} // namespace

int main() {
    // ── Two nodes probing the SAME tentative ID: each sees the other's ENQ ──
    {
        Scc8530 a, b;
        a.reset(); b.reset();
        lapArm(a, 42); lapArm(b, 42);
        a.onTxFrame = [&](int ch, const uint8_t* d, size_t n) {
            if (ch == kB) b.injectRxFrame(kB, d, n);
        };
        b.onTxFrame = [&](int ch, const uint8_t* d, size_t n) {
            if (ch == kB) a.injectRxFrame(kB, d, n);
        };

        const uint8_t enq[3] = {42, 42, 0x81};  // dst, src, lapENQ
        lapSend(a, enq, 3);

        uint8_t rr1 = 0;
        std::vector<uint8_t> got = lapDrain(b, rr1);
        CHECK(got.size() == 5, "B receives ENQ + 2-byte FCS");
        CHECK(got.size() >= 3 && !std::memcmp(got.data(), enq, 3),
              "B sees A's ENQ intact");
        CHECK(rr1 & 0x80, "End-of-Frame status on the last byte");
        CHECK(!(rr1 & 0x40), "FCS good (no CRC error)");
        b.writeCtl(kB, 0);
        CHECK(b.readCtl(kB) & 0x10, "B back in hunt after the frame");

        // And the reverse direction over the same cable.
        const uint8_t enqB[3] = {42, 42, 0x81};
        lapSend(b, enqB, 3);
        std::vector<uint8_t> gotA = lapDrain(a, rr1);
        CHECK(gotA.size() == 5 && !std::memcmp(gotA.data(), enqB, 3),
              "A sees B's ENQ over the same cable");
    }

    // ── Address filter: a node holding ANOTHER ID must not hear the probe ──
    {
        Scc8530 a, c;
        a.reset(); c.reset();
        lapArm(a, 42); lapArm(c, 7);
        a.onTxFrame = [&](int ch, const uint8_t* d, size_t n) {
            if (ch == kB) c.injectRxFrame(kB, d, n);
        };
        const uint8_t enq[3] = {42, 42, 0x81};
        lapSend(a, enq, 3);
        uint8_t rr1 = 0;
        CHECK(lapDrain(c, rr1).empty(), "addr-search drops a foreign ENQ");

        // Broadcast ($FF) passes every filter.
        const uint8_t bc[3] = {0xFF, 42, 0x81};
        lapSend(a, bc, 3);
        std::vector<uint8_t> got = lapDrain(c, rr1);
        CHECK(got.size() == 5 && got[0] == 0xFF, "broadcast reaches node 7");
    }

    // ── Send Abort discards the pending frame ──
    {
        Scc8530 a;
        a.reset();
        lapArm(a, 42);
        long frames = 0;
        a.onTxFrame = [&](int, const uint8_t*, size_t) { frames++; };
        a.writeCtl(kB, 0xC0);
        a.writeData(kB, 42); a.writeData(kB, 42);
        a.writeCtl(kB, 0x18);          // WR0: Send Abort
        a.tick(4000);
        CHECK(frames == 0, "aborted frame never reaches the wire");
    }

    // ── Directed-frame dialogue: lapRTS → lapCTS → DATA under 200 µs ──
    // The receiver's "driver" answers the RTS with a CTS as soon as its Rx
    // completes; the sender must see the CTS within the LLAP inter-frame
    // window (IFG 200 µs = ~3130 cycles) of its RTS completing on the wire,
    // then ships the data frame. Pins the wire's end-to-end latency budget.
    {
        Scc8530 a, b;
        a.reset(); b.reset();
        lapArm(a, 42); lapArm(b, 7);
        std::vector<uint8_t> bGot;
        long ctsAtTick = -1, rtsDoneTick = -1;
        a.onTxFrame = [&](int ch, const uint8_t* d, size_t n) {
            if (ch == kB) b.injectRxFrame(kB, d, n);
        };
        b.onTxFrame = [&](int ch, const uint8_t* d, size_t n) {
            if (ch == kB) a.injectRxFrame(kB, d, n);
        };

        const uint8_t rts[3] = {7, 42, 0x84};    // dst 7, src 42, lapRTS
        lapSend(a, rts, 3);
        // Tick both sides in 1/4-byte steps; B's driver replies CTS the
        // moment its frame completes (EOF byte drained).
        long t = 0;
        bool bSawRts = false;
        uint8_t rr1 = 0;
        std::vector<uint8_t> aGot;
        for (; t < 400; t++) {
            a.tick(kByteCyc / 4);
            b.tick(kByteCyc / 4);
            // B side: drain FIFO, on EOF reply CTS.
            while (true) {
                b.writeCtl(kB, 0);
                if (!(b.readCtl(kB) & 0x01)) break;
                b.writeCtl(kB, 1);
                uint8_t st = b.readCtl(kB);
                bGot.push_back(b.readData(kB));
                if (st & 0x80) {                 // EOF: whole RTS is in
                    bSawRts = bGot.size() >= 3 && bGot[2] == 0x84;
                    if (rtsDoneTick < 0) rtsDoneTick = t;
                    const uint8_t cts[3] = {42, 7, 0x85};
                    lapSendNoWait(b, cts, 3);
                }
            }
            // A side: watch for the CTS.
            while (true) {
                a.writeCtl(kB, 0);
                if (!(a.readCtl(kB) & 0x01)) break;
                a.writeCtl(kB, 1);
                uint8_t st = a.readCtl(kB);
                aGot.push_back(a.readData(kB));
                if ((st & 0x80) && aGot.size() >= 3 && aGot[2] == 0x85
                    && ctsAtTick < 0)
                    ctsAtTick = t;
            }
            if (ctsAtTick >= 0) break;
        }
        CHECK(bSawRts, "B received the directed RTS");
        CHECK(ctsAtTick >= 0, "A received the CTS reply");
        // Sender-side budget: CTS fully received within IFG(200 µs) + the
        // CTS frame's own wire time (5 bytes) of the RTS completing.
        const long budget = (3130 + 5 * kByteCyc) / (kByteCyc / 4) + 1;
        CHECK(rtsDoneTick >= 0 && ctsAtTick - rtsDoneTick <= budget,
              "CTS lands inside the LLAP inter-frame window");
        // And the data frame follows on the open dialogue.
        const uint8_t data[6] = {7, 42, 0x01, 0xAA, 0xBB, 0xCC};
        lapSend(a, data, 6);
        bGot.clear();
        std::vector<uint8_t> gotData = lapDrain(b, rr1);
        CHECK(gotData.size() == 8 && gotData[2] == 0x01 && gotData[5] == 0xCC,
              "directed DATA frame delivered after the handshake");
    }

    // ── Cable-synthesized express CTS across the half-duplex Rx-off window ──
    // Pins the driver sequence captured on the LToUDP cable (SCCDBG,
    // 2026-07-22): the LAP sender disables Rx around its directed RTS, sees
    // the EOM, drops RTS/TxEnable, writes WR3 again with Rx still off, then
    // re-arms Rx and waits for the CTS carrier. The cable's CTS must
    // (a) queue through the Rx-off window, (b) survive the intermediate
    // Rx-off WR3 write (the queue is the WIRE, not the chip), and (c) start
    // only after an inter-frame gap so the re-armed receiver catches every
    // byte — instant delivery played the CTS into a closed ear and the
    // Chooser retried its RTS forever.
    {
        Scc8530 s;
        s.reset();
        lapArm(s, 1);                            // node 1, as in the capture
        s.onTxFrame = [&s](int ch, const uint8_t* d, size_t n) {
            if (ch != kB) return;
            if (n == 3 && d[2] == 0x84 && d[0] != 0xFF) {
                const uint8_t cts[3] = { d[1], d[0], 0x85 };
                s.injectRxFrame(kB, cts, 3, true);   // express (cable CTS)
            }
        };
        wr(s, 5, 0x6B);                          // TxEnable + RTS
        wr(s, 3, 0xD0);                          // Rx OFF (half-duplex Tx)
        const uint8_t rts[3] = {0xFE, 1, 0x84};  // directed lapRTS
        for (uint8_t byte : rts) s.writeData(kB, byte);
        s.writeCtl(kB, 0xC0);                    // Reset Tx Underrun/EOM
        int t = 0;                               // poll RR0 for the EOM…
        for (; t < 1000; t++) {
            s.tick(8);                           // instruction-grained time
            s.writeCtl(kB, 0);
            if (s.readCtl(kB) & 0x40) break;
        }
        CHECK(t < 1000, "directed RTS completes (EOM latch sets)");
        s.writeCtl(kB, 0);
        CHECK(s.readCtl(kB) & 0x10, "line still idle at EOM (CTS gap)");
        wr(s, 5, 0x60);                          // drop TxEnable + RTS
        wr(s, 3, 0xC0);                          // WR3 with Rx still off
        s.writeCtl(kB, 0x30);                    // Error Reset
        s.writeCtl(kB, 0x10);                    // Reset Ext/Status
        wr(s, 3, 0xDD);                          // re-arm Rx + hunt + search
        uint8_t rr1 = 0;
        std::vector<uint8_t> got = lapDrain(s, rr1);
        CHECK(got.size() == 5 && got[0] == 1 && got[1] == 0xFE
                  && got[2] == 0x85,
              "re-armed receiver catches the whole synthesized CTS");
        CHECK((rr1 & 0x80) && !(rr1 & 0x40), "CTS EOF with good FCS");
    }

    // ── Hunt exit is an ext/status event (carrier sense for the LAP) ──
    {
        Scc8530 a, b;
        a.reset(); b.reset();
        lapArm(a, 42); lapArm(b, 42);
        a.onTxFrame = [&](int ch, const uint8_t* d, size_t n) {
            if (ch == kB) b.injectRxFrame(kB, d, n);
        };
        const uint8_t enq[3] = {42, 42, 0x81};
        lapSend(a, enq, 3);
        b.tick(kByteCyc);              // opening flag: hunt clears
        CHECK(b.irqAsserted(), "hunt-exit ext/status raises the IRQ");
        b.writeCtl(kB, 0);
        CHECK(!(b.readCtl(kB) & 0x10), "Sync/Hunt low while carrier present");
    }

    // ── A mid-frame Enter-Hunt must NOT truncate the frame in flight ──
    // The LLAP driver re-arms the receiver by writing WR3 with the Enter
    // Hunt bit (0x10) the instant it finishes the previous frame — which
    // on the byte-paced wire lands while the NEXT frame is already being
    // clocked in. Clearing the in-flight frame there truncated every long
    // directed frame to its first 2-3 bytes: the 44-byte NBP LkUpReply
    // lost its DDP payload and the AppleShare server never populated the
    // guest's Chooser even though the reply reached the node on the wire
    // (2026-07-22, live GISTPERSO capture). Enter Hunt while synced onto a
    // frame is ignored; the frame finishes and re-enters hunt at its EOF.
    {
        Scc8530 a, b;
        a.reset(); b.reset();
        lapArm(a, 1); lapArm(b, 1);
        a.onTxFrame = [&](int ch, const uint8_t* d, size_t n) {
            if (ch == kB) b.injectRxFrame(kB, d, n);
        };
        // A 44-byte directed long-DDP frame to node 1 (dst,src,type=$02…),
        // like an NBP LkUpReply.
        uint8_t frame[44];
        frame[0] = 1; frame[1] = 0xFE; frame[2] = 0x02;
        for (int i = 3; i < 44; i++) frame[i] = uint8_t(0x40 + i);
        lapSend(a, frame, sizeof frame);

        // Drain, but re-arm with WR3 Enter Hunt (0xD5 has bit 4 set) after
        // the first two bytes — exactly what the driver does mid-frame.
        std::vector<uint8_t> got;
        uint8_t rr1 = 0;
        for (int t = 0; t < 80; t++) {
            b.tick(kByteCyc);
            b.writeCtl(kB, 0x10);                 // Reset Ext/Status
            while (true) {
                b.writeCtl(kB, 0);
                if (!(b.readCtl(kB) & 0x01)) break;
                b.writeCtl(kB, 1);
                rr1 = b.readCtl(kB);
                got.push_back(b.readData(kB));
                if (got.size() == 2) wr(b, 3, 0xD5);  // mid-frame re-arm
            }
        }
        CHECK(got.size() == sizeof frame + 2,
              "long directed frame survives a mid-frame Enter Hunt (+FCS)");
        CHECK(got.size() >= 44 && !std::memcmp(got.data(), frame, 44),
              "the whole LkUpReply-sized frame is delivered intact");
        CHECK((rr1 & 0x80) && !(rr1 & 0x40), "EOF + good FCS on the last byte");
    }

    // ── The open-line standing abort is transport-driven, not a constant ──
    // setAbortIdle(true) marks a machine with no hardwired peer: its idle
    // SDLC line reads a standing Break/Abort (RR0 bit 7), the carrier-sense
    // signal the LAP times out on when solo. But the instant a REAL peer
    // transmits (a non-express injected frame — an LToUDP multicast frame,
    // not the cable's own synthesized CTS) the line becomes a live,
    // terminated network and the abort drops for a hold window; it returns
    // only once the peer goes quiet (LLE_VS_HLE §1.8 / step 8).
    {
        Scc8530 s;
        s.reset();
        s.setAbortIdle(true);              // LC II/Q605: no hardwired peer
        lapArm(s, 1);
        s.writeCtl(kB, 0);
        CHECK(s.readCtl(kB) & 0x80, "solo open line shows the standing abort");

        // A real peer's frame (express=false) → line live, abort drops.
        // Drain it so rxCur is empty: the drop is then the peer state, not
        // the frame-in-flight mask.
        const uint8_t enq[3] = {1, 2, 0x81};
        s.injectRxFrame(kB, enq, 3, false);
        uint8_t rr1 = 0;
        (void)lapDrain(s, rr1);
        s.writeCtl(kB, 0);
        CHECK(!(s.readCtl(kB) & 0x80),
              "a real peer's frame drops the standing abort (line is live)");

        // Peer goes quiet past the hold window → open line returns.
        s.tick(31000000);                  // > kPeerHold
        s.writeCtl(kB, 0);
        CHECK(s.readCtl(kB) & 0x80, "abort returns after the peer goes quiet");

        // The cable's own synthesized CTS (express=true) is NOT a peer.
        const uint8_t cts[3] = {1, 2, 0x85};
        s.injectRxFrame(kB, cts, 3, true);
        (void)lapDrain(s, rr1);
        s.writeCtl(kB, 0);
        CHECK(s.readCtl(kB) & 0x80,
              "a synthesized (express) frame does not mark a peer present");
    }

    // ── Back-to-back injected frames both survive (empty-Chooser bug) ──
    // The router delivers a LkUp broadcast and afpd's LkUpReply in one poll,
    // so two non-express frames land in the Rx queue together. The second
    // must NOT start the instant the first ends (its head would fall into
    // the still-closing FIFO): the inter-dialog gap is measured from the
    // FIRST frame's END (rxIdle), not from injection. Both must be delivered
    // in full — the live GISTPERSO capture showed the guest re-sending the
    // AFPServer lookup forever because the reply (frame 2) was lost.
    {
        Scc8530 s;
        s.reset();
        lapArm(s, 1);
        const uint8_t f1[4] = {1, 2, 0x01, 0xAA};    // "broadcast LkUp"
        const uint8_t f2[4] = {1, 2, 0x01, 0xBB};    // "LkUpReply" — the one lost
        s.injectRxFrame(kB, f1, 4, false);
        s.injectRxFrame(kB, f2, 4, false);           // same poll, back-to-back
        uint8_t rr1 = 0;
        auto got = lapDrain(s, rr1, 96);
        bool sawA = false, sawB = false;
        for (uint8_t b : got) { if (b == 0xAA) sawA = true; if (b == 0xBB) sawB = true; }
        CHECK(sawA, "back-to-back: the first frame is delivered");
        CHECK(sawB, "back-to-back: the SECOND frame (LkUpReply) is delivered too");
    }

    // ── FCS residue must not surface at the head of the NEXT frame ──
    // The real LAP driver (unlike lapDrain, which reads every byte) reads a
    // frame by its DDP length and re-enters hunt WITHOUT reading the trailing
    // FCS — it trusts the hardware CRC (RR1 bit 6). Those unread FCS bytes
    // must be flushed at the frame boundary: left in the 3-deep FIFO the
    // previous frame's crc_hi (its EOF byte) surfaced as a phantom EOF-flagged
    // byte at the HEAD of the next frame, desyncing the 44-byte NBP LkUpReply
    // so the AFPServer entity never populated the Chooser (empty server list,
    // 2026-07-22 GISTPERSO capture). Here we read a frame by its length,
    // leave the 2 FCS bytes unread, re-arm hunt, then verify the NEXT frame
    // is delivered starting on its OWN first byte with no phantom leader.
    {
        Scc8530 s;
        s.reset();
        lapArm(s, 1);
        const uint8_t f1[6] = {1, 2, 0x01, 0xAA, 0xBB, 0xCC};   // 6 + 2 FCS
        s.injectRxFrame(kB, f1, sizeof f1, false);
        // Read exactly the 6 payload bytes (as the driver does by DDP length).
        std::vector<uint8_t> g1;
        uint8_t rr1 = 0;
        for (int t = 0; t < 40 && g1.size() < 6; t++) {
            s.tick(kByteCyc);
            s.writeCtl(kB, 0x10);
            while (g1.size() < 6) {
                s.writeCtl(kB, 0);
                if (!(s.readCtl(kB) & 0x01)) break;
                s.writeCtl(kB, 1); rr1 = s.readCtl(kB);
                g1.push_back(s.readData(kB));
            }
        }
        CHECK(g1.size() == 6 && g1[0] == 1 && g1[5] == 0xCC,
              "frame read by DDP length, FCS left unread");
        // Clock the 2 unread FCS bytes onto the FIFO (crc_lo, crc_hi=EOF)
        // without reading them — the residue the real driver skips.
        s.tick(kByteCyc); s.tick(kByteCyc);
        s.writeCtl(kB, 0);
        CHECK(s.readCtl(kB) & 0x01, "unread FCS residue sits in the FIFO");
        // Frame boundary: driver re-arms with WR3 Enter Hunt (0xDD, bit 4).
        // This must flush the EOF-flagged residue.
        wr(s, 3, 0xDD);
        s.writeCtl(kB, 0);
        CHECK(!(s.readCtl(kB) & 0x01),
              "Enter Hunt at the frame boundary flushes the FCS residue");
        // The LkUpReply-analogue now arrives; it must start on its own byte.
        const uint8_t f2[6] = {1, 2, 0x01, 0x11, 0x22, 0x33};
        s.injectRxFrame(kB, f2, sizeof f2, false);
        std::vector<uint8_t> g2 = lapDrain(s, rr1);
        CHECK(!g2.empty() && g2[0] == 1,
              "next frame starts on its own first byte, not a phantom EOF");
        CHECK(g2.size() == 8 && g2[2] == 0x01 && g2[5] == 0x33,
              "the second frame is delivered intact (no stale leading byte)");
        CHECK((rr1 & 0x80) && !(rr1 & 0x40), "clean EOF + good FCS on frame 2");
    }

    // ── FCS residue that arrives AFTER the re-arm (the real boot timing) ──
    // In the live LC II boot the driver reads the frame by length and re-arms
    // hunt (WR3 Enter Hunt) BEFORE the wire has clocked the trailing crc_hi
    // into the FIFO — rxCur is still non-empty at the re-arm, so the
    // Enter-Hunt flush cannot fire. crc_hi lands a byte-time later and lingers
    // as a phantom EOF byte until the NEXT frame. rxStartFrame must drop it so
    // the next frame (the NBP LkUpReply) opens on its own first byte. This is
    // the ordering the running Enter-Hunt-only fix failed to clear (the phantom
    // survived in a live SCCDBG capture), so it is pinned separately.
    {
        Scc8530 s;
        s.reset();
        lapArm(s, 1);
        const uint8_t f1[6] = {1, 2, 0x01, 0xAA, 0xBB, 0xCC};   // 6 + 2 FCS
        s.injectRxFrame(kB, f1, sizeof f1, false);
        std::vector<uint8_t> g1;
        uint8_t rr1 = 0;
        for (int t = 0; t < 40 && g1.size() < 6; t++) {
            s.tick(kByteCyc);
            s.writeCtl(kB, 0x10);
            while (g1.size() < 6) {
                s.writeCtl(kB, 0);
                if (!(s.readCtl(kB) & 0x01)) break;
                s.writeCtl(kB, 1); rr1 = s.readCtl(kB);
                g1.push_back(s.readData(kB));
            }
        }
        CHECK(g1.size() == 6, "frame read by length; FCS still on the wire");
        wr(s, 3, 0xDD);                      // re-arm BEFORE crc_hi hits the FIFO
        s.tick(kByteCyc); s.tick(kByteCyc);  // the FCS now paces in as a phantom
        const uint8_t f2[6] = {1, 2, 0x01, 0x11, 0x22, 0x33};
        s.injectRxFrame(kB, f2, sizeof f2, false);
        // The real LAP driver does NOT poll the idle inter-dialog gap — it
        // waits on the carrier-sense (hunt-exit) interrupt, so it reads only
        // once the next frame opens. Model that: advance WITHOUT reading until
        // hunt clears (rxStartFrame ran and flushed the phantom), then drain.
        for (int t = 0; t < 40; t++) {
            s.tick(kByteCyc);
            s.writeCtl(kB, 0);
            if (!(s.readCtl(kB) & 0x10)) break;   // hunt cleared → f2 opened
        }
        std::vector<uint8_t> g2 = lapDrain(s, rr1);
        CHECK(!g2.empty() && g2[0] == 1,
              "phantom FCS arriving after re-arm is flushed when the next frame opens");
        CHECK(g2.size() == 8 && g2[2] == 0x01 && g2[5] == 0x33,
              "the LkUpReply-analogue opens on its own first byte, intact");
    }

    if (failures == 0)
        std::printf("PASS: llap loop (ENQ both ways, addr filter, broadcast, "
                    "abort, RTS/CTS dialogue in-window, express CTS across "
                    "Rx-off, carrier sense)\n");
    return failures ? 1 : 0;
}
