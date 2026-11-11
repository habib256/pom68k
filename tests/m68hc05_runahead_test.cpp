// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// M68hc05 run-ahead gate (M68hc05::setRunAhead / nextIsInternal, used by
// CudaLle::tick). The claim under test: letting the MCU execute early the
// instructions that touch neither a port nor a DDR nor the /IRQ pin moves
// no port access in time and changes no value the MCU reads or drives.
//
// Three integrators run the same randomly generated firmware on the debt
// pattern of CudaLle::tick, against a port whose input level is a function
// of integrator time:
//   * the reference — run-ahead off, one cycle per tick (the finest
//     slicing there is);
//   * run-ahead with the slice chosen at the integrator's event bound
//     (debt + 1), as a board does;
//   * run-ahead with random slices at or below that bound.
// Every port write and DDR write (pin levels) and every port read must
// land on the same integrator cycle with the same value in all three, the
// two run-ahead integrators must serialize byte-identically at every
// comparison point (slicing independence), and the reference, once it has
// caught up on the MCU cycle count, must serialize identically too.
//
// Synthetic ROMs only: no firmware dump is needed.

#include "M68hc05.h"
#include "SaveState.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <vector>

namespace {
int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-62s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}

struct Rng {
    uint64_t s;
    uint32_t next() {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        return uint32_t(s >> 33);
    }
    uint32_t below(uint32_t n) { return next() % n; }
};

// One random straight-line program at $1000 that loops for ever, a
// subroutine at $1F00 and a timer handler at $1F10. Branch displacements
// are all zero and X only ever holds a value from `xs`, so the stream
// cannot leave the program whatever the port reads return; no store can
// reach the timer control register, so a WAIT always wakes.
std::vector<uint8_t> makeRom(uint64_t seed) {
    std::vector<uint8_t> rom(0x1100, 0x9D);                  // NOP fill
    size_t o = 0x1000 - 0x0F00;
    auto put = [&](std::initializer_list<uint8_t> bytes) {
        for (uint8_t v : bytes) rom[o++] = v;
    };
    Rng r { seed };
    static const uint8_t xs[] = { 0x00, 0x01, 0x02, 0x09, 0x12,
                                  0x90, 0x95, 0xA0, 0xB0 };
    auto ram = [&] { return uint8_t(0x90 + r.below(0x30)); };

    put({ 0xA6, 0xFF, 0xB7, 0x04, 0xB7, 0x06 });             // PA, PC outputs
    put({ 0xA6, 0x0F, 0xB7, 0x05 });                         // PB low nibble out
    put({ 0xA6, 0x03, 0xB7, 0x07 });                         // PLL: arm timer
    put({ 0xA6, 0x20, 0xB7, 0x08, 0x9A });                   // TOF int on, CLI
    const size_t loop = o;
    for (int i = 0; i < 400; i++) {
        switch (r.below(28)) {
            case 0:  put({ 0xA6, uint8_t(r.next()) }); break;        // LDA #
            case 1:  put({ 0xAB, uint8_t(r.next()) }); break;        // ADD #
            case 2:  put({ 0x4C }); break;                           // INCA
            case 3:  put({ 0xAE, xs[r.below(sizeof xs)] }); break;   // LDX #
            case 4:  put({ 0xB7, ram() }); break;                    // STA ram
            case 5:  put({ 0xB6, ram() }); break;                    // LDA ram
            case 6:  put({ 0x3C, ram() }); break;                    // INC ram
            case 7:  put({ 0xB6, 0x01 }); break;                     // LDA PB
            case 8:  put({ 0xB7, 0x00 }); break;                     // STA PA
            case 9:  put({ 0xB7, 0x02 }); break;                     // STA PC
            case 10: put({ 0x16, 0x02 }); break;                     // BSET 3,PC
            case 11: put({ 0x01, 0x01, 0x00 }); break;               // BRCLR 0,PB
            case 12: put({ 0x03, ram(), 0x00 }); break;              // BRCLR 1,ram
            case 13: put({ 0xB6, 0x09 }); break;                     // LDA timer
            case 14: put({ 0xF7 }); break;                           // STA ,X
            case 15: put({ 0xF6 }); break;                           // LDA ,X
            case 16: put({ 0xE7, uint8_t(r.below(4)) }); break;      // STA n,X
            case 17: put({ 0xC6, 0x00, 0x01 }); break;               // LDA >PB
            case 18: put({ 0xC6, 0x10, uint8_t(r.next()) }); break;  // LDA >rom
            case 19: put({ 0xC7, 0x00, 0x00 }); break;               // STA >PA
            case 20: put({ 0xD6, 0x00, 0x00 }); break;               // LDA 0,X
            case 21: put({ 0xCD, 0x1F, 0x00 }); break;               // JSR sub
            case 22: if (r.below(4) == 0) put({ 0x8F }); break;      // WAIT
            case 23: put({ r.below(2) ? uint8_t(0x2E) : uint8_t(0x2F),
                           0x00 }); break;                           // BIL/BIH
            case 24: put({ 0x27, 0x00 }); break;                     // BEQ
            case 25: put({ 0x15, 0x05 }); break;                     // BCLR 2,DDRB
            case 26: put({ 0x14, 0x05 }); break;                     // BSET 2,DDRB
            default: put({ uint8_t(i % 3 == 0 ? 0x9B : i % 3 == 1 ? 0x9A
                                                        : 0x99) }); break; // SEI/CLI/SEC
        }
    }
    put({ 0xCC, uint8_t(0x0F + (loop >> 8)), uint8_t(loop) });       // JMP loop
    o = 0x1F00 - 0x0F00; put({ 0x3C, 0xA1, 0xB6, 0x01, 0x81 });      // sub
    o = 0x1F10 - 0x0F00;                                             // timer
    put({ 0xB6, 0x01, 0x1F, 0x08, 0x3C, 0xA2, 0xB7, 0x00, 0x80 });   // PB first
    o = 0x1FF8 - 0x0F00; put({ 0x1F, 0x10 });
    o = 0x1FFE - 0x0F00; put({ 0x10, 0x00 });
    return rom;
}

struct Event {
    int64_t t; int kind; int port; uint8_t value;
    bool operator==(const Event&) const = default;
};

// The debt pattern of CudaLle::tick, with the MCU cycle as integrator time.
struct Rig {
    M68hc05 m;
    int64_t t = 0;
    int debt = 0, maxDebt = 0;
    long ticks = 0;
    std::vector<Event> log;

    Rig(const std::vector<uint8_t>& rom, int runAhead) {
        m.loadRom(rom);
        m.setRunAhead(runAhead);
        m.setPullups(1, 0xC0);
        m.readPort = [this](int p) {
            const uint8_t v = uint8_t((uint64_t(t) * 2654435761u) >> (8 + p));
            log.push_back({ t, 0, p, v });
            return v;
        };
        m.writePort = [this](int p, uint8_t v) { log.push_back({ t, 1, p, v }); };
        m.reset();
    }
    int bound() const { return debt + 1; }
    void tick(int n) {
        t += n;
        ticks++;
        const int c = n - debt;
        const int used = m.run(c);
        debt = used > c ? used - c : 0;
        if (debt > maxDebt) maxDebt = debt;
    }
    std::vector<sav::u8> bytes() {
        std::vector<sav::u8> out;
        sav::Writer ar(out);
        ar(m);
        return out;
    }
};

bool runProgram(uint64_t seed, int cap, long* refTicks, long* boundTicks,
                int* maxDebt) {
    const std::vector<uint8_t> rom = makeRom(seed);
    Rig ref(rom, 0), atBound(rom, cap), random(rom, cap);
    Rng r { seed ^ 0x9E3779B97F4A7C15ull };
    bool ok = true;
    const int64_t kEnd = 60000;
    for (int64_t stop = 2000; stop <= kEnd && ok; stop += 2000) {
        while (ref.t < stop) ref.tick(1);
        while (atBound.t < stop)
            atBound.tick(int(std::min<int64_t>(atBound.bound(), stop - atBound.t)));
        while (random.t < stop) {
            const int most = int(std::min<int64_t>(random.bound(), stop - random.t));
            random.tick(1 + int(r.below(uint32_t(most))));
        }
        ok = ok && atBound.bytes() == random.bytes();
    }
    ok = ok && ref.log.size() > 1000;
    // The run-ahead integrators stand ahead of the reference: their logs
    // may hold nothing the reference has not reached, and the reference
    // catches up on the MCU cycle count without another port access.
    ok = ok && atBound.log == random.log && ref.log == atBound.log;
    const int64_t ahead = atBound.m.cycleCount();
    while (ok && ref.m.cycleCount() < ahead) ref.tick(1);
    ok = ok && ref.log == atBound.log && ref.bytes() == atBound.bytes();
    *refTicks += ref.ticks;
    *boundTicks += atBound.ticks;
    if (atBound.maxDebt > *maxDebt) *maxDebt = atBound.maxDebt;
    return ok;
}
} // namespace

int main() {
    std::printf("m68hc05_runahead_test — internal instructions run early, "
                "port accesses do not move\n");
    for (int cap : { 512, 37, 1 }) {
        bool ok = true;
        long refTicks = 0, boundTicks = 0;
        int maxDebt = 0;
        for (uint64_t seed = 1; seed <= 40; seed++)
            ok = runProgram(seed * 7919 + uint64_t(cap), cap, &refTicks,
                            &boundTicks, &maxDebt) && ok;
        char what[96];
        std::snprintf(what, sizeof what,
                      "cap %3d: 40 programs, logs and state identical", cap);
        check(ok, what);
        std::printf("    integrator wake-ups %ld, max debt %d MCU cycles\n",
                    boundTicks, maxDebt);
        if (cap == 512) {
            // Not vacuous: the bound really reaches past single
            // instructions (the longest one costs 11 cycles).
            check(maxDebt > 40, "the debt reaches past one instruction");
        }
    }
    // Off means off: a standalone core stops at the first instruction
    // boundary at or past its budget, and does nothing on a spent budget.
    {
        const std::vector<uint8_t> rom = makeRom(5);
        M68hc05 m;
        m.loadRom(rom);
        m.reset();
        check(m.run(0) == 0 && m.run(-5) == 0 && m.cycleCount() == 0,
              "run-ahead off: a spent budget executes nothing");
        const int used = m.run(1);
        check(used >= 2 && used <= 11 && m.cycleCount() == used,
              "run-ahead off: one instruction for a budget of one");
    }
    std::printf(gFails ? "FAILED (%d)\n" : "PASS\n", gFails);
    return gFails ? 1 : 0;
}
