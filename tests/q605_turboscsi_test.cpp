// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// LLE step 9 gate — the PrimeTime TurboSCSI cell and the 53C96 delay model,
// pinned against the MAME oracles (iosb.cpp + ncr53c90.cpp):
//
//  1. Wait states: every 53C96 register access costs 3 CPU cycles
//     (iosb.cpp:144-148/482-495); the pseudo-DMA window's waitstated alias
//     (byte-address bit 19 — BIT(offset<<1,18) under .select(0xfc0000))
//     costs the IOSB-reg-2-programmed count through times[4] = {5,5,4,3}
//     (iosb.cpp:606-618), while the plain window costs nothing.
//  2. Delay model: with the default MAME-derived latency (LLE step 9),
//     SELECT-with-ATN's I_BUS|I_FUNCTION arrives only after the
//     arbitrate/assert/settle chain + per-byte CDB time
//     (ncr53c90.cpp:336-460: 19×conv + 6 clocks, + sync_period per byte),
//     converted 40 MHz SCSI → 25 MHz CPU (×5/8, rounded up); a polled
//     Transfer Information defers its bus-service interrupt by
//     sync_period×bytes + 2 clocks. POM68K_SCSI_LAT=0 (here: setLatency(0))
//     restores the historical instant behaviour.
//
// Runs against a synthetic zero-filled disk image — no ROM, no asset.

#include "Q605Memory.h"
#include "Cpu040.h"
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <vector>

namespace {
int gFails = 0;

void check(bool ok, const char* what) {
    std::printf("  %-62s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}

constexpr uint32_t kScsiReg = 0x50010000;    // + reg*$10
constexpr uint32_t kDmaPlain = 0x50010100;   // no wait states
constexpr uint32_t kDmaWait  = 0x50090100;   // bit-19 waitstated alias
constexpr uint32_t kIosbReg2 = 0x50018200;   // IOSB reg 2 (u16)

// MAME-derived expectations (mirror Ncr53c96::*DelayCpu_; reset defaults
// clockConv=2, syncPeriod=5 — ncr53c90.cpp:251-252).
int toCpu(int clocks) { return (clocks * 5 + 7) / 8; }
int selectionDelay(int bytes) { return toCpu(19 * 2 + 6 + bytes * 5); }
int xferDelay(int bytes) { return toCpu(bytes * 5 + 2); }

// Select target 0 with IDENTIFY + a READ(6) of `blocks` from LBA 0, the way
// the Mac OS 8.1 SCSI Manager programs it (ncr53c96_test pattern).
void selectRead6(Q605Memory& mem, int blocks) {
    mem.write8(kScsiReg + 0x3 * 0x10, 0x01);           // CM_FLUSH_FIFO
    mem.write8(kScsiReg + 0x2 * 0x10, 0xC0);           // IDENTIFY, LUN 0
    const uint8_t cdb[6] = { 0x08, 0, 0, 0, uint8_t(blocks), 0 };
    for (uint8_t b : cdb) mem.write8(kScsiReg + 0x2 * 0x10, b);
    mem.write8(kScsiReg + 0x4 * 0x10, 0x00);           // destination id 0
    mem.write8(kScsiReg + 0x3 * 0x10, 0x42);           // CD_SELECT_ATN
}
} // namespace

int main() {
    std::printf("q605_turboscsi_test — PrimeTime wait states + 53C96 delay model (step 9)\n");

    // Synthetic 64-block zero image (not HFS, so no facade interferes).
    const char* img = "q605_turboscsi_test.tmp.dsk";
    {
        std::ofstream out(img, std::ios::binary | std::ios::trunc);
        std::vector<char> z(64 * 512, 0);
        out.write(z.data(), std::streamsize(z.size()));
    }

    Q605Memory mem(1u << 20);
    Cpu040 cpu(mem);
    mem.setCpu(&cpu);
    mem.reset();
    check(mem.attachScsi(img, false, 0), "synthetic target attached at ID 0");

    // ── 1. Register access wait states ──────────────────────────────────
    // Wait states are MACHINE cycles: the core clock runs cacheBoost_× fast
    // and bus time is not accelerated by the i-cache (CHANGELOG 2026-07-25),
    // so measure with machineClock() to stay boost-invariant.
    auto delta = [&](auto&& fn) {
        cpu.flushTicks();
        moira::i64 c0 = cpu.machineClock();
        fn();
        return int(cpu.machineClock() - c0);
    };
    check(delta([&] { (void)mem.read8(kScsiReg + 0x4 * 0x10); }) == 3,
          "register read stalls 3 cycles");
    check(delta([&] { mem.write8(kScsiReg + 0x0 * 0x10, 0); }) == 3,
          "register write stalls 3 cycles");

    // ── 2. Selection delay (default MAME model) ─────────────────────────
    cpu.flushTicks();
    selectRead6(mem, 1);
    check(!mem.scsi().irq(), "SELECT_ATN interrupt is not instant");
    const int selDelay = selectionDelay(1 + 6);        // IDENTIFY + CDB
    mem.tick(selDelay - 1);
    check(!mem.scsi().irq(), "still pending one cycle before the model delay");
    mem.tick(1);
    check(mem.scsi().irq(), "I_BUS|I_FUNCTION lands at 19*conv+6 + 7*syncPeriod");
    check(mem.read8(kScsiReg + 0x5 * 0x10) == 0x18,    // R_ISTAT read clears
          "interrupt status reads I_BUS|I_FUNCTION");

    // ── 3. Transfer Information delays ──────────────────────────────────
    // Polled (non-DMA) IN moves ONE byte per XFER — MAME raises bus_complete
    // for every received byte (INIT_XFR_WAIT_REQ `fifo_pos == 1`,
    // ncr53c90.cpp:652) — so the deferral is one byte's handshake, NOT the
    // whole remaining payload (that bug wedged the OS 8.1 boot in a 75 ms
    // retry loop per byte-tail XFER).
    cpu.flushTicks();
    mem.write8(kScsiReg + 0x3 * 0x10, 0x10);           // CI_XFER (polled)
    check(!mem.scsi().irq(), "polled XFER bus-service interrupt is not instant");
    const int xfer1 = xferDelay(1);
    mem.tick(xfer1 - 1);
    check(!mem.scsi().irq(), "polled XFER still pending before 1*syncPeriod+2");
    mem.tick(1);
    check(mem.scsi().irq(), "polled XFER completes per byte (MAME non-DMA IN)");
    (void)mem.read8(kScsiReg + 0x5 * 0x10);            // clear

    // A DMA XFER is charged its programmed chunk (TC=64 here) — and a live
    // drain-completion absorbs the schedule (raiseIrq coalesce) instead of
    // firing a phantom interrupt afterwards.
    cpu.flushTicks();
    mem.write8(kScsiReg + 0x0 * 0x10, 64);             // TC = 64
    mem.write8(kScsiReg + 0x1 * 0x10, 0x00);
    mem.write8(kScsiReg + 0xE * 0x10, 0x00);
    mem.write8(kScsiReg + 0x3 * 0x10, 0x90);           // CI_XFER | DMA
    check(!mem.scsi().irq(), "DMA XFER interrupt rides the chunk schedule");
    for (int i = 0; i < 64; i++) (void)mem.read8(kDmaPlain);
    check(mem.scsi().irq(), "drain completion raises bus-service immediately");
    (void)mem.read8(kScsiReg + 0x5 * 0x10);
    mem.tick(xferDelay(64) + 8);
    check(!mem.scsi().irq(),
          "no phantom deferred interrupt after the drain absorbed it");

    // ── 4. Pseudo-DMA window wait states (IOSB reg 2) ───────────────────
    // Re-select and run a DMA Transfer Info so DRQ is up while we probe.
    cpu.flushTicks();
    mem.write8(kScsiReg + 0x3 * 0x10, 0x03);           // CM_RESET_BUS → free
    (void)mem.read8(kScsiReg + 0x5 * 0x10);
    selectRead6(mem, 1);
    mem.tick(selectionDelay(7));
    (void)mem.read8(kScsiReg + 0x5 * 0x10);
    mem.write8(kScsiReg + 0x0 * 0x10, 0x00);           // TC = 512
    mem.write8(kScsiReg + 0x1 * 0x10, 0x02);
    mem.write8(kScsiReg + 0xE * 0x10, 0x00);
    mem.write8(kScsiReg + 0x3 * 0x10, 0x90);           // CI_XFER | DMA
    check(mem.scsi().drq(), "DRQ asserted for the pseudo-DMA drain");

    check(delta([&] { (void)mem.read8(kDmaWait); }) == 3,
          "waitstated DMA alias stalls 3 (power-on default)");
    check(delta([&] { (void)mem.read8(kDmaPlain); }) == 0,
          "plain DMA window inserts no wait states");

    mem.write16(kIosbReg2, uint16_t(0 << 8));          // code 0 → 5 cycles
    check(delta([&] { (void)mem.read8(kDmaWait); }) == 5,
          "IOSB reg 2 code 0 programs 5-cycle DMA reads");
    mem.write16(kIosbReg2, uint16_t(2 << 8));          // code 2 → 4 cycles
    check(delta([&] { (void)mem.read8(kDmaWait); }) == 4,
          "IOSB reg 2 code 2 programs 4-cycle DMA reads");
    mem.write16(kIosbReg2, uint16_t(3 << 8));          // code 3 → 3 cycles
    check(delta([&] { (void)mem.read8(kDmaWait); }) == 3,
          "IOSB reg 2 code 3 programs 3-cycle DMA reads");

    // ── 5. POM68K_SCSI_LAT=0 — historical instant mode ──────────────────
    mem.scsi().setLatency(0);
    mem.write8(kScsiReg + 0x3 * 0x10, 0x02);           // CM_RESET (chip)
    selectRead6(mem, 1);
    check(mem.scsi().irq(), "setLatency(0) restores the instant SELECT interrupt");

    std::remove(img);
    if (gFails) { std::printf("FAILED (%d)\n", gFails); return 1; }
    std::printf("PASS\n");
    return 0;
}
