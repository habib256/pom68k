// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Gate — DAFB TurboSCSI PDMA gating (MAME-parity finding #17).
// MAME dafb.cpp:996-1082 (turboscsi_dma_r/w): a PDMA access with !DRQ is
// NEVER an immediate bus error. With the DRQ-check bit of scsiCtrl clear
// (reset state) the access goes through blindly; with it set (bit 7 reads,
// bit 8 writes — dafb.cpp:484-486) the DAFB holds off /DTACK until DRQ,
// and only the hold-off TIMEOUT raises /BERR (dafb.cpp:485).
// Exit 0 = pass, 1 = fail.

#include "Q700Memory.h"
#include "Moira.h"

#include <cstdio>

namespace {
int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}

// Program a 32-bit DAFB register byte by byte; dafbWrite8 commits the
// merged longword on the (addr & 3) == 3 lane.
void writeDafb(Q700Memory& mem, uint32_t off, uint32_t v) {
    for (int i = 0; i < 4; i++)
        mem.write8(0xF9800000 + off + i, uint8_t(v >> (8 * (3 - i))));
}
uint32_t readDafb(Q700Memory& mem, uint32_t off) {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++)
        v = (v << 8) | mem.read8(0xF9800000 + off + i);
    return v;
}
void writeScsiCtrl(Q700Memory& mem, uint32_t v) { writeDafb(mem, 0x24, v); }
} // namespace

int main() {
    std::printf("q700_turboscsi_test — PDMA DRQ gating (dafb.cpp:996-1082)\n");

    Q700Memory mem(8u << 20);
    mem.reset();
    int berrs = 0;
    uint32_t berrAddr = 0;
    mem.onBusError = [&](uint32_t a, bool) { berrs++; berrAddr = a; };

    // ── DRQ-check clear (reset scsiCtrl = 0): blind access, no /BERR ────
    bool threw = false;
    uint8_t d = 0xEE;
    try { d = mem.read8(0x5000F100); } catch (moira::MmuBusError&) { threw = true; }
    check(!threw && berrs == 0, "PDMA read, check bit clear: no bus error");
    check(d == 0x00, "blind pop of an idle 53C96 reads 0");
    try { mem.write8(0x5000F100, 0xAA); } catch (moira::MmuBusError&) { threw = true; }
    check(!threw && berrs == 0, "PDMA write, check bit clear: no bus error");

    // ── DRQ Check Read (bit 7): hold-off, then timeout /BERR ────────────
    writeScsiCtrl(mem, 0x080);
    check(mem.read8(0xF9800027) == 0x80 && !(mem.read8(0xF9800026) & 0x02),
          "scsiCtrl readback: bit 7 latched, live DRQ bit 9 low");
    threw = false;
    try { (void)mem.read8(0x5000F100); } catch (moira::MmuBusError&) { threw = true; }
    check(threw && berrs == 1 && berrAddr == 0x5000F100,
          "PDMA read, check bit set, DRQ never: timeout bus error");

    // ── DRQ Check Write (bit 8) ─────────────────────────────────────────
    writeScsiCtrl(mem, 0x100);
    threw = false;
    try { mem.write8(0x5000F100, 0x55); } catch (moira::MmuBusError&) { threw = true; }
    check(threw && berrs == 2, "PDMA write, check bit set: timeout bus error");
    // ...and the read side must now be blind again (its bit is clear).
    threw = false;
    try { (void)mem.read8(0x5000F100); } catch (moira::MmuBusError&) { threw = true; }
    check(!threw && berrs == 2, "read gate independent of the write gate");

    // ── Register PIO path is never gated (dafb.cpp:972-991) ─────────────
    threw = false;
    try { (void)mem.read8(0x5000F000); } catch (moira::MmuBusError&) { threw = true; }
    check(!threw, "53C96 register read unaffected by the DRQ gates");

    // ── Finding #55: registers below $200 are 12-bit (dafb.cpp:435,625) ──
    writeScsiCtrl(mem, 0xFFFFFFFF);
    check(readDafb(mem, 0x24) == 0xFFF,
          "scsiCtrl write is clamped to 12 bits before latching");
    writeDafb(mem, 0x148, 0xFFF38);               // Swatch HPIX
    check(readDafb(mem, 0x148) == 0xF38,
          "Swatch register write is clamped to 12 bits");
    writeScsiCtrl(mem, 0);

    // ── Finding #50: SCSI bus 2 register $28 (dafb.cpp:424,533-576) ─────
    writeDafb(mem, 0x28, 0x0A5);
    check(readDafb(mem, 0x28) == 0x0A5,
          "bus-2 ctrl latches; live DRQ bit 9 low with no device");
    check(readDafb(mem, 0x24) == 0, "bus-2 ctrl is independent of bus 1");
    writeDafb(mem, 0x28, 0);

    // ── Finding #51/#52: discrete DAFB = version 1, plain AC842 ─────────
    check(((readDafb(mem, 0x2C) >> 9) & 7) == 1,
          "Q700 DAFB reports version 1 in $2C bits 11-9 (dafb.cpp:84)");
    writeDafb(mem, 0x220, 0x06);                  // the PCBR1 dance…
    writeDafb(mem, 0x200, 1);
    writeDafb(mem, 0x220, 0xC0);
    writeDafb(mem, 0x200, 0);
    writeDafb(mem, 0x220, 0x06);
    check(mem.dafbMode() != 5, "AC842 has no x555 mode (dafb.cpp:792-820)");
    writeDafb(mem, 0x200, 1);
    check(readDafb(mem, 0x220) == 0x06,
          "AC842 $220 always reads PCBR0, never a PCBR1");

    // ── Q950: DAFB II — version 3, AC842a with PCBR1 ID $01 ─────────────
    {
        Q700Memory q950(8u << 20, Q700Memory::kCpuHzQ950,
                        Q700Memory::Model::Q950);
        q950.reset();
        check(((readDafb(q950, 0x2C) >> 9) & 7) == 3,
              "Q950 DAFB II reports version 3 (dafb.cpp:1105)");
        writeDafb(q950, 0x220, 0x06);
        writeDafb(q950, 0x200, 1);
        writeDafb(q950, 0x220, 0xC0);             // PCBR1
        writeDafb(q950, 0x200, 0);
        writeDafb(q950, 0x220, 0x06);
        check(q950.dafbMode() == 5, "AC842a x555 engages on the Q950");
        writeDafb(q950, 0x200, 1);
        check(readDafb(q950, 0x220) == 0xC1,
              "AC842a PCBR1 carries version ID $01 (dafb.cpp:1131)");
        // Bus 2 exists for real here (second 53C96, still idle → DRQ low).
        writeDafb(q950, 0x28, 0x0A5);
        check(readDafb(q950, 0x28) == 0x0A5,
              "Eclipse bus-2 ctrl latches with live DRQ low");
    }

    std::printf("%s\n", gFails ? "FAILED" : "PASSED");
    return gFails ? 1 : 0;
}
