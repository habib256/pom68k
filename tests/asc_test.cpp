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

    std::printf("%s\n", gFails ? "FAILED" : "PASSED");
    return gFails ? 1 : 0;
}
