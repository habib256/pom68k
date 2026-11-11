// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Gate of Scc8530::tick's quiet short-circuit (Scc8530.h). Two chips take the
// same random stream of register accesses, frame and byte injections, DCD
// edges and ticks of random length. The reference is woken before every tick
// by a mutator that changes nothing (setCtsHigh with the value it already
// holds), so it always runs the complete tick; the subject short-circuits
// whenever it has proved itself idle. After every operation the two must
// agree on everything the save-state visitor serializes, on the tick's
// return value, on the interrupt line and on the next-event bound.
// Exit 0 = pass, 1 = fail.

#include "SaveState.h"
#include "Scc8530.h"

#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

namespace {

std::vector<sav::u8> image(Scc8530& scc) {
    std::vector<sav::u8> out;
    sav::Writer ar(out);
    scc.visit(ar);
    return out;
}

struct Pair {
    Scc8530 ref, sub;
    template <class F> void both(F&& f) { f(ref); f(sub); }
};

} // namespace

int main() {
    std::printf("scc_quiet_tick_test — quiet short-circuit against the complete tick\n");
    long ticks = 0, quietTicks = 0, checks = 0, txFramesRef = 0, txFramesSub = 0;
    int fails = 0;

    for (int board = 0; board < 4; board++) {
        std::mt19937 rng(20261111u + unsigned(board));
        auto pick = [&](int n) { return int(rng() % unsigned(n)); };
        Pair p;
        p.both([&](Scc8530& s) {
            s.reset();
            s.setClocks(board & 1 ? 15667200 : 7833600, 3686400);
            s.setAbortIdle(board >= 2);          // the LC II / Q605 open line
            s.setCtsHigh(true);
        });
        p.ref.onTxFrame = [&](int, const uint8_t*, size_t) { txFramesRef++; };
        p.sub.onTxFrame = [&](int, const uint8_t*, size_t) { txFramesSub++; };

        for (int step = 0; step < 400000 && !fails; step++) {
            const int op = pick(100);
            const int ch = pick(2);
            bool same = true;
            if (op < 70) {
                // Mostly time: short slices (a 68000 bus access) and long ones.
                const int cycles = pick(4) ? 1 + pick(40) : 1 + pick(40000);
                if (p.sub.quiet()) quietTicks++;
                p.ref.setCtsHigh(true);              // wake: complete tick
                const bool a = p.ref.tick(cycles);
                const bool b = p.sub.tick(cycles);
                same = a == b;
                ticks++;
            } else if (op == 70) {
                const uint8_t v = uint8_t(rng());
                p.both([&](Scc8530& s) { s.writeCtl(ch, v); });
            } else if (op == 71 && pick(4) == 0) {
                // A fresh chip now and then: a frame queued on a channel that
                // never reaches SDLC would otherwise keep it busy for good.
                p.both([&](Scc8530& s) { s.reset(); });
            } else if (op == 72 && pick(2) == 0) {
                // The LocalTalk driver's opening, then a peer frame.
                static const uint8_t kOpen[][2] = {
                    {4, 0x20}, {3, 0xDD}, {5, 0x60}, {15, 0x80}, {1, 0x01}, {9, 0x08} };
                uint8_t frame[12];
                for (auto& b : frame) b = uint8_t(rng());
                p.both([&](Scc8530& s) {
                    for (const auto& w : kOpen) {
                        s.writeCtl(ch, uint8_t((w[0] & 7) | ((w[0] & 8) ? 0x08 : 0)));
                        s.writeCtl(ch, w[1]);
                    }
                    s.injectRxFrame(ch, frame, sizeof frame);
                });
            } else if (op < 82) {
                // A register write a driver would make: pointer, then one of
                // the values that select a mode (random bytes almost never
                // reach SDLC with the receiver on, where the line clocks,
                // the queued frames and the abort relatch live).
                static const struct { int reg; uint8_t v[4]; } kWrites[] = {
                    { 4,  { 0x20, 0x20, 0x44, 0x20 } },   // SDLC / async x16
                    { 3,  { 0xC1, 0xD1, 0xDD, 0x00 } },   // Rx enable, hunt
                    { 5,  { 0x68, 0x6A, 0x60, 0x08 } },   // Tx enable, RTS
                    { 1,  { 0x01, 0x11, 0x13, 0x00 } },   // ext / Rx / Tx IE
                    { 15, { 0x80, 0xC0, 0x00, 0x08 } },   // Break/Abort, EOM
                    { 9,  { 0x08, 0x0A, 0x00, 0x08 } },   // MIE
                    { 14, { 0x00, 0x10, 0x01, 0x00 } },   // loopback, BRG
                    { 0,  { 0x10, 0x30, 0x80, 0xC0 } },   // WR0 commands
                };
                const auto& w = kWrites[pick(8)];
                const uint8_t v = w.v[pick(4)];
                p.both([&](Scc8530& s) {
                    if (w.reg) s.writeCtl(ch, uint8_t((w.reg & 7) | ((w.reg & 8) ? 0x08 : 0)));
                    s.writeCtl(ch, v);
                });
            } else if (op < 86) {
                same = p.ref.readCtl(ch) == p.sub.readCtl(ch);
            } else if (op < 88) {
                const uint8_t v = uint8_t(rng());
                p.both([&](Scc8530& s) { s.writeData(ch, v); });
            } else if (op < 97) {
                same = p.ref.readData(ch) == p.sub.readData(ch);
            } else if (op == 97 && pick(4) == 0) {
                uint8_t frame[24];
                const size_t n = 3 + size_t(pick(20));
                for (size_t i = 0; i < n; i++) frame[i] = uint8_t(rng());
                const auto kind = pick(3) == 0 ? Scc8530::RxFrameKind::CtsReply
                                               : Scc8530::RxFrameKind::Peer;
                p.both([&](Scc8530& s) { s.injectRxFrame(ch, frame, n, kind); });
            } else if (op == 98) {
                const uint8_t v = uint8_t(rng());
                p.both([&](Scc8530& s) { s.injectRxByte(ch, v); });
            } else if (op == 99) {
                const bool level = pick(2) != 0;
                p.both([&](Scc8530& s) { s.setDcd(ch, level); });
            }
            checks++;
            if (!same || p.ref.irqAsserted() != p.sub.irqAsserted() ||
                p.ref.cyclesToNextEvent() != p.sub.cyclesToNextEvent() ||
                txFramesRef != txFramesSub || image(p.ref) != image(p.sub)) {
                std::printf("  FAIL board %d step %d op %d: the short-circuit "
                            "diverged from the complete tick\n", board, step, op);
                fails++;
            }
        }
    }
    std::printf("  %ld ticks (%ld short-circuited), %ld compared states, "
                "%ld frames transmitted\n", ticks, quietTicks, checks, txFramesRef);
    if (!fails && (ticks < 1000000 || quietTicks * 10 < ticks || txFramesRef == 0)) {
        std::printf("  FAIL: the stream did not exercise the chip\n");
        fails++;
    }
    std::printf(fails ? "FAILED\n" : "PASSED\n");
    return fails ? 1 : 0;
}
