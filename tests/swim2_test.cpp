// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Q8 SWIM2 slice-1 gate: register file, two-entry FIFO, mode/setup/params,
// error and PrimeTime 16-bit bus-lane mapping. Media/flux comes next.

#include "Q605Memory.h"
#include "Swim2.h"
#include "Cpu040.h"

#include <cstdio>

namespace {
int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-62s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}
} // namespace

int main() {
    std::printf("swim2_test — SWIM2 register/FIFO core (Q8)\n");

    Swim2 swim;
    swim.reset();
    check(swim.read(6) == 0x40 && swim.read(5) == 0, "reset mode/setup");

    swim.write(3, 0x11); swim.write(3, 0x22);
    swim.write(3, 0x33); swim.write(3, 0x44);
    check(swim.read(3) == 0x11 && swim.read(3) == 0x22 &&
          swim.read(3) == 0x33 && swim.read(3) == 0x44,
          "four timing parameters rotate");

    swim.write(4, 0xA5);
    swim.write(5, 0x46);
    check(swim.read(4) == 0xA5 && swim.read(5) == 0x46, "phases/setup round-trip");

    swim.write(0, 0x12);
    swim.write(1, 0x34);
    check(swim.fifoCount() == 2 && (swim.read(7) & 0xC0) == 0xC0,
          "read-mode handshake reports two queued entries");
    check(swim.read(0) == 0x12, "data register pops FIFO head");
    check(swim.read(0) == 0x34 && swim.read(2) == 0x02,
          "data read of a mark reports mark error");

    swim.write(0, 1); swim.write(0, 2); swim.write(0, 3);
    check(swim.read(2) == 0x04, "third FIFO push reports overflow");
    swim.write(7, 0x01);                       // mode bit 0 clears FIFO
    check(swim.fifoCount() == 0, "mode CLEAR bit flushes FIFO");
    swim.write(6, 0x01);
    check((swim.mode() & 0x40) != 0 && (swim.mode() & 1) == 0,
          "mode-clear preserves mandatory bit 6");

    swim.reset();
    swim.write(7, 0x98);                       // motor + write + action
    check((swim.read(7) & 0xC0) == 0xC0, "write-mode empty FIFO reports room");
    swim.write(0, 0x12); swim.write(0, 0x34);
    swim.tick(128);
    check(swim.fifoCount() == 1 && (swim.read(7) & 0x80),
          "write engine drains one queued byte and exposes FIFO room");

    swim.reset();
    swim.write(7, 0x08);                       // enter read/action mode
    swim.tick(128);
    check(swim.fifoCount() == 1 && (swim.read(7) & 0x80),
          "no-media read amplifier clocks an idle $FF byte");
    check(swim.read(0) == 0xFF, "idle read byte is $FF");

    Q605Memory mem(1u << 20);
    mem.reset();
    Cpu040 cpu(mem);
    mem.setCpu(&cpu);
    constexpr uint32_t kSwim = 0x5001E000;
    auto before = cpu.getClock();
    check(mem.read16(kSwim + 6 * 0x200) == 0x4000,
          "PrimeTime word read returns SWIM byte on D15-D8");
    check(cpu.getClock() - before == 5, "PrimeTime SWIM access costs five cycles");
    mem.write16(kSwim + 7 * 0x200, 0x0080);
    check((mem.swim().mode() & 0x80) != 0,
          "PrimeTime word write accepts the low data byte once");

    std::printf("%s\n", gFails ? "FAILED" : "PASSED");
    return gFails ? 1 : 0;
}
