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

    if (failures == 0)
        std::printf("PASS: llap loop (ENQ both ways, addr filter, broadcast, "
                    "abort, carrier sense)\n");
    return failures ? 1 : 0;
}
