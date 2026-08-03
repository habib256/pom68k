// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// O6.6 gate — ASC-V8 semantics against MAME asc.cpp (asc_v8_device,
// hardware-tested via ASCTester on a real LC): version $E8, constant
// regs, mono FIFO A (writes to B ignored), FIFO status bits ($804:
// bit 0 = half-empty asserting a LEVEL IRQ, bit 1 = empty/full), drain
// at 22 257 Hz, and the boot-critical "fill until full" exit that the
// LC II ROM's beep code spins on ($A45F26-$A45F34).
// Exit 0 = pass, 1 = fail.

#include "Asc.h"

#include <cstdio>

namespace {
int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}
} // namespace

int main() {
    std::printf("asc_test — ASC-V8 FIFO/IRQ semantics (O6.6)\n");

    AscV8 asc;
    bool irqLine = false;
    asc.onIrq = [&](bool s) { irqLine = s; };
    asc.reset();

    // Constant registers (asc.cpp:845-882)
    check(asc.read(0x800) == 0xE8, "version reads $E8");
    check(asc.read(0x801) == 1 && asc.read(0x802) == 1 && asc.read(0x803) == 1,
          "mode/control/fifo-mode read 1 (forced FIFO mode)");
    check(asc.read(0x805) == 0 && asc.read(0x807) == 0,
          "wavetable/clock read 0");
    check(asc.read(0x804) == 0x02, "reset: FIFO A empty (status bit 1)");

    // Fill FIFO A: half point clears half-empty, $3FF sets full
    for (int i = 0; i < 0x200; i++) asc.write(uint32_t(i & 0x3FF), 0x80);
    check((asc.read(0x804) & 0x01) == 0, "at $200 bytes: half-empty clears");
    for (int i = 0; i < 0x1FF; i++) asc.write(0, 0x80);
    check((asc.read(0x804) & 0x02) == 0x02, "at $3FF bytes: FULL bit set (ROM beep exit)");

    // FIFO B writes are ignored on V8 (mono)
    AscV8 ascB;
    ascB.reset();
    for (int i = 0; i < 0x400; i++) ascB.write(0x400 + uint32_t(i & 0x3FF), 0x80);
    check(ascB.read(0x804) == 0x02, "FIFO B writes ignored (still empty)");

    // Drain: 22 257 samples per emulated second
    check(asc.fifoCap() == 0x3FF, "cap $3FF before drain");
    asc.tick(704 * 100);                     // ≈100 samples
    check(asc.fifoCap() < 0x3FF && asc.fifoCap() > 0x300,
          "drain consumes ≈1 byte per 704 CPU cycles");

    // Drain below half → LEVEL IRQ asserts; status read does NOT clear it
    asc.tick(704 * 0x300);
    check((asc.read(0x804) & 0x01) == 0x01, "below half: half-empty set");
    check(irqLine, "below half: IRQ line asserted");
    (void)asc.read(0x804);
    check(irqLine, "status read does NOT clear the IRQ while half-empty (level)");

    // Refill past half → IRQ clears on the next status read
    for (int i = 0; i < 0x300; i++) asc.write(0, 0x80);
    (void)asc.read(0x804);
    check(!irqLine, "refilled past half: status read clears the IRQ");

    // Empty drain floor: empty bit returns, output keeps flowing silently
    AscV8 quiet;
    quiet.reset();
    quiet.tick(704 * 50);
    check((quiet.read(0x804) & 0x03) == 0x03, "empty FIFO: empty + half-empty set");
    check(quiet.available() >= 49, "silence samples still produced for the host");

    // ── Classic ASC ($00, Mac II) — MAME asc_device + QEMU empty-cycle ──
    std::printf("asc_test — classic ASC ($00) Mac II semantics\n");
    AscV8 classic(0x00);
    bool cIrq = false;
    classic.onIrq = [&](bool s) { cIrq = s; };
    classic.reset();
    check(classic.read(0x800) == 0x00, "classic version $00");
    check(classic.read(0x804) == 0x00 && !cIrq, "classic idle: status $00, no IRQ");
    classic.write(0x801, 0x18);              // high bits must be masked
    check(classic.read(0x801) == 0x00, "MODE write $18 masks to $00");
    check(!cIrq, "masked-off MODE stays quiet");

    classic.write(0x801, 1);                 // FIFO mode
    for (int i = 0; i < 0x200; i++) classic.write(0, 0x80);
    classic.tick(704 * 2);                   // cross half → edge IRQ
    check(cIrq, "classic half-cross asserts IRQ");
    (void)classic.read(0x804);
    check(!cIrq && classic.read(0x804) == 0x00,
          "classic status read clears IRQ and status");

    // Empty-cycle: FIFO mode left running, no samples → periodic re-IRQ
    classic.write(0x801, 0);
    classic.write(0x801, 1);                 // enter FIFO mode with empty FIFOs
    cIrq = false;
    classic.tick(704 * 0x400);
    check(cIrq, "classic empty-cycle re-asserts IRQ (Sound Manager)");
    uint8_t st = classic.read(0x804);
    check((st & 0x03) == 0x03, "empty-cycle status has A half+empty");
    check(!cIrq, "status read clears the empty-cycle IRQ");

    // ── $807 CLOCK RATE drives the drain cadence (LLE_VS_HLE §1.7) ──
    // The rate was pinned at 22 257 Hz whatever the guest programmed.
    // Measured through the OBSERVABLE the register changes: how many CPU
    // cycles it takes to drain a fixed number of FIFO bytes. Asserting on
    // `drainHz()` directly would only prove the switch statement compiles.
    {
        auto cyclesToDrain = [](uint8_t clockReg, int samples) {
            AscV8 a(0x00);                   // classic: $807 is writable here
            a.reset();
            a.write(0x807, clockReg);
            a.write(0x801, 1);               // FIFO mode
            for (int i = 0; i < 0x400; i++) a.write(0, 0x80);
            int cyc = 0;
            while (a.available() < samples && cyc < 40'000'000) {
                a.tick(64);
                cyc += 64;
            }
            return cyc;
        };
        const int base = cyclesToDrain(0, 400);      // 22 257 Hz
        const int r22050 = cyclesToDrain(2, 400);
        const int r44100 = cyclesToDrain(3, 400);
        // 22 050 is 0.93 % slower than 22 257 → very slightly more cycles.
        check(r22050 > base, "$807 = 2 (22 050 Hz) drains slower than the Mac rate");
        // 44 100 is very nearly double → about half the cycles.
        const double ratio = double(base) / double(r44100);
        check(ratio > 1.9 && ratio < 2.1,
              "$807 = 3 (44 100 Hz) drains at ~2x the Mac rate");
        // Code 1 is undefined; MAME leaves it so and we keep the Mac rate
        // rather than inventing one.
        check(cyclesToDrain(1, 400) == base, "$807 = 1 (undefined) keeps the Mac rate");

        // On the integrated flavours the register is not writable — reads
        // back 0 and the cadence must not move. That is what makes this
        // change free on every machine that boots today.
        AscV8 v8(0xE8);
        v8.reset();
        v8.write(0x807, 3);
        check(v8.read(0x807) == 0x00, "V8 integration: $807 not writable, reads 0");
    }

    std::printf("%s\n", gFails ? "FAILED" : "PASSED");
    return gFails ? 1 : 0;
}
