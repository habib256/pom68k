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

    // ── Audit #42: F09/F29 FIFO IRQ-control reads ──
    // Classic reads 0 (MAME master asc_device::read asc.cpp:657-667, the
    // real-IIci dump asc.cpp:621); V8 reads 0 per the real-LC dump
    // (asc.cpp:767) — deliberately NOT MAME's V8 value of 1 (asc.cpp:867-869).
    {
        AscV8 c(0x00), v8(0xE8);
        c.reset(); v8.reset();
        check(c.read(0xF09) == 0 && c.read(0xF29) == 0,
              "classic F09/F29 read 0 (IIci dump / MAME master fix)");
        check(v8.read(0xF09) == 0 && v8.read(0xF29) == 0,
              "V8 F09/F29 read 0 (real-LC dump; MAME's 1 is wrong)");
    }

    // ── Audit #43: classic FIFO B write obeys the CONTROL stereo gate ──
    // (MAME asc_device::write asc.cpp:704-739)
    {
        AscV8 c(0x00);
        bool irq = false;
        c.onIrq = [&](bool s) { irq = s; };
        c.reset();
        c.write(0x801, 1);                   // FIFO mode, CONTROL still 0
        for (int i = 0; i < 0x400; i++) c.write(0x400, 0x80);
        check(c.fifoCapB() == 0 && !irq,
              "classic mono: FIFO B writes dropped, no data, no IRQ");
        check((c.read(0x804) & 0x0C) == 0,
              "classic mono: FIFO B status bits untouched");
        c.write(0x802, 0x02);                // CONTROL_STEREO (bit 1) on
        for (int i = 0; i < 0x3FF; i++) c.write(0x400, 0x80);
        check(c.fifoCapB() == 0x3FF, "classic stereo: FIFO B accepts data");
        const bool irqAtFull = irq;          // $804 read clears it (classic)
        check((c.read(0x804) & 0x08) == 0x08 && irqAtFull,
              "classic stereo: FIFO B full sets bit 3 + IRQ");
        // Wavetable/idle addressed poke (mode != 1) is not stereo-gated.
        AscV8 wt(0x00);
        wt.reset();
        wt.write(0x400 + 5, 0xAA);
        check(wt.read(0x400 + 5) == 0xAA,
              "classic mode 0: addressed FIFO B poke still lands");
    }

    // ── Audit #44: V8 record mode (R_PLAYRECA $80A bit 0) freezes the ──
    // FIFO A status bits on write (asc_base_device::write asc.cpp:386-404
    // via asc_v8_device::write asc.cpp:878-902). The classic override
    // (asc.cpp:669-703) has no such gate: its status still moves.
    {
        AscV8 v8(0xE8);
        v8.reset();
        v8.write(0x80A, 1);                  // record mode
        v8.write(0, 0x80);                   // 1 byte: ungated would clear bit 1
        check(v8.fifoCap() == 1, "V8 record mode: data still enters FIFO A");
        check(v8.read(0x804) == 0x02,
              "V8 record mode: status frozen (empty bit stays despite data)");
        v8.write(0x80A, 0);                  // back to playback
        v8.write(0, 0x80);                   // cap 2 < $200: gate open, bit 1 clears
        check(v8.read(0x804) == 0x00,
              "V8 playback restored: write clears the stale empty bit");

        AscV8 c(0x00);
        bool cIrq2 = false;
        c.onIrq = [&](bool s) { cIrq2 = s; };
        c.reset();
        c.write(0x801, 1);                   // FIFO mode
        c.write(0x80A, 1);                   // record mode: NO gate on classic
        for (int i = 0; i < 0x3FF; i++) c.write(0, 0x80);
        const bool cIrqAtFull = cIrq2;       // $804 read clears it (classic)
        check((c.read(0x804) & 0x02) == 0x02 && cIrqAtFull,
              "classic ignores $80A: FIFO A full still edges status + IRQ");
    }

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

    // ── SIMPLIFICATIONS_REVIEW F5: four-voice wavetable mode ────────────
    // MODE = 2, unique to the first-generation ASC (asc.cpp:19, engine at
    // :248-281). Each voice advances a 24-bit phase by its increment and
    // takes bits 23-15 as a 512-byte index; voices 0/1 read FIFO A, 2/3
    // read FIFO B, and odd voices take the upper half (+$200) of theirs.
    // Samples are offset binary. Registers: phase at $811/$819/$821/$829,
    // increment at $815/$81D/$825/$82D, 24-bit big-endian.
    {
        // Voice n's phase register base; increment sits 4 bytes later.
        auto phaseReg = [](int v) { return uint32_t(0x811 + 8 * v); };
        auto incrReg  = [](int v) { return uint32_t(0x815 + 8 * v); };
        auto write24  = [](AscV8& a, uint32_t reg, uint32_t v) {
            a.write(reg,     uint8_t(v >> 16));
            a.write(reg + 1, uint8_t(v >> 8));
            a.write(reg + 2, uint8_t(v));
        };
        auto read24 = [](AscV8& a, uint32_t reg) {
            return uint32_t(a.read(reg)) << 16 | uint32_t(a.read(reg + 1)) << 8
                 | a.read(reg + 2);
        };
        // Drain n samples and return the loudest absolute output seen.
        auto peak = [](AscV8& a, int samples) {
            a.tick(704 * samples);
            int pk = 0;
            while (a.available()) {
                int s = a.pop();
                if (s < 0) s = -s;
                if (s > pk) pk = s;
            }
            return pk;
        };

        AscV8 wt(0x00);
        bool wtIrq = false;
        wt.onIrq = [&](bool s) { wtIrq = s; };
        wt.reset();
        wt.write(0x801, 2);                   // MODE = wavetable
        check(wt.read(0x801) == 2, "wavetable: MODE 2 accepted");

        // Registers are live oscillators, not a register-file echo: the
        // two upper voices live past the 32-byte block entirely.
        write24(wt, phaseReg(3), 0x123456);
        write24(wt, incrReg(3), 0x00ABCD);
        check(read24(wt, phaseReg(3)) == 0x123456,
              "wavetable: voice 3 phase reads back (past regs_)");
        check(read24(wt, incrReg(3)) == 0x00ABCD,
              "wavetable: voice 3 increment reads back");

        // A silent table ($80 = offset-binary zero) stays silent even with
        // every oscillator running — this is the control for the next check.
        for (uint32_t i = 0; i < 0x400; i++) {
            wt.write(i, 0x80);                // FIFO A as wavetable RAM
            wt.write(0x400 + i, 0x80);        // FIFO B
        }
        for (int v = 0; v < 4; v++) {
            write24(wt, phaseReg(v), 0);
            write24(wt, incrReg(v), 0x008000);
        }
        check(peak(wt, 200) == 0, "wavetable: a centred table is silent");

        // Now make voice 0's table a square wave. The engine must produce
        // real audio — the whole point of F5, where the old stub emitted
        // zeroes whatever the guest programmed.
        for (uint32_t i = 0; i < 0x200; i++)
            wt.write(i, i < 0x100 ? 0xFF : 0x00);
        const int loud = peak(wt, 400);
        check(loud > 4000, "wavetable: a square-wave table is audible");
        check(loud <= 32767, "wavetable: output stays inside int16");

        // The phase free-runs: it must have advanced by increment × samples.
        AscV8 ph(0x00);
        ph.reset();
        ph.write(0x801, 2);
        write24(ph, phaseReg(0), 0);
        write24(ph, incrReg(0), 0x000100);
        ph.tick(704 * 16);                    // 16 samples
        const uint32_t advanced = read24(ph, phaseReg(0));
        check(advanced == 0x001000,
              "wavetable: phase advances by increment per sample");
        check(read24(ph, phaseReg(1)) == 0,
              "wavetable: a zero-increment voice does not drift");

        // Voice/table routing. One voice moves at a time, and the loud
        // region sits in the SECOND half of a table so the three parked
        // voices — which all sit at index 0 — read centred silence: a
        // parked voice on a loud sample is a DC offset that would make
        // every "silent" assertion below pass or fail for the wrong reason.
        // Increment $8000 advances the index by exactly one per sample, so
        // a sweep needs >256 samples to reach a region at index $100.
        auto routingRig = [&](AscV8& a, bool loudInB) {
            a.reset();
            a.write(0x801, 2);
            for (uint32_t i = 0; i < 0x400; i++) {
                a.write(i, 0x80);                   // FIFO A: all centred
                a.write(0x400 + i, 0x80);           // FIFO B: all centred
            }
            // Square wave over indices $100-$1FF of the chosen FIFO's UPPER
            // half — i.e. bytes $300-$3FF of that FIFO's own 1 KB, which is
            // write offset $300 for FIFO A and $700 for FIFO B (the B
            // window starts at $400).
            const uint32_t base = loudInB ? 0x700 : 0x300;
            for (uint32_t i = 0; i < 0x100; i++)
                a.write(base + i, i < 0x80 ? 0xFF : 0x00);
            for (int v = 0; v < 4; v++) {
                write24(a, phaseReg(v), 0);
                write24(a, incrReg(v), 0);
            }
        };
        // Park every voice back at index 0 — a voice left where the previous
        // sweep stopped can sit ON the loud region and contribute a DC
        // offset, which is exactly what made the first version of this
        // block report routing failures that were its own bookkeeping.
        auto sweepOnly = [&](AscV8& a, int voice) {
            for (int v = 0; v < 4; v++) {
                write24(a, incrReg(v), 0);
                write24(a, phaseReg(v), 0);
            }
            write24(a, incrReg(voice), 0x008000);
            return peak(a, 400);
        };

        // Loud region in FIFO A's UPPER half → only voice 1 reaches it.
        AscV8 rtA(0x00);
        routingRig(rtA, /*loudInB=*/false);
        check(sweepOnly(rtA, 1) > 4000, "wavetable: voice 1 reads FIFO A's upper half");
        check(sweepOnly(rtA, 0) == 0, "wavetable: voice 0 stays in A's lower half");
        check(sweepOnly(rtA, 2) == 0, "wavetable: voice 2 reads FIFO B, not A");

        // Loud region in FIFO B's UPPER half → only voice 3 reaches it.
        AscV8 rtB(0x00);
        routingRig(rtB, /*loudInB=*/true);
        check(sweepOnly(rtB, 3) > 4000, "wavetable: voice 3 reads FIFO B's upper half");
        check(sweepOnly(rtB, 2) == 0, "wavetable: voice 2 stays in B's lower half");
        check(sweepOnly(rtB, 1) == 0, "wavetable: voice 1 reads FIFO A, not B");

        // Wavetable mode raises no FIFO interrupt (MAME's case 2 has no
        // set_irq_line): a guest driving it open-loop must not be woken.
        check(!wtIrq, "wavetable: no FIFO IRQ in wavetable mode");

        // And the mode is classic-only: a V8 integration forces FIFO mode,
        // so the same programming must not turn into wavetable playback.
        AscV8 v8wt(0xE8);
        v8wt.reset();
        v8wt.write(0x801, 2);
        check(v8wt.read(0x801) == 1, "V8: MODE stays forced to FIFO, no wavetable");
    }

    std::printf("%s\n", gFails ? "FAILED" : "PASSED");
    return gFails ? 1 : 0;
}
