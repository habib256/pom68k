// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// O6.5 gate — LC II SCSI through the V8 bus: 53C80 registers at $F10000
// (stride $10), the pseudo-DMA read alias (reg 6 @ +$260), the wide
// DRQ-handshaked window at $F06000/$F12000, and the DRQ-gated /DSACK
// timeout that raises a BUS ERROR — the mechanism the SCSI Manager's
// blind MOVE.L loops rely on to terminate (macscsi.cpp:5-52;
// maclc.cpp:222-266). Soft-skips without hdv/HD20SC.vhd.
// Exit 0 = pass, 1 = fail.

#include "V8Memory.h"
#include "Cpu030.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {
int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}

std::string find(const char* rel) {
    for (const std::string base : { std::string(), std::string("../") }) {
        std::string p = base + rel;
        if (std::ifstream(p, std::ios::binary)) return p;
    }
    return {};
}

// 53C80 register access through the LC II map
constexpr uint32_t kRegBase = 0xF10000;
uint8_t rd(V8Memory& m, int reg) { return m.read8(kRegBase + uint32_t(reg) * 0x10); }
void wr(V8Memory& m, int reg, uint8_t v) { m.write8(kRegBase + uint32_t(reg) * 0x10, v); }
} // namespace

int main() {
    std::printf("scsi_pdma_test — V8 SCSI pseudo-DMA + BERR timeout (O6.5)\n");
    std::string img = find("hdv/HD20SC.vhd");
    if (img.empty()) { std::printf("SKIP: hdv/HD20SC.vhd not found\n"); return 0; }

    V8Memory mem;
    Cpu030 cpu(mem);
    mem.setCpu(&cpu);
    check(mem.attachScsi(img), "SCSI disk attached");

    // Arbitration + selection of target 0 (SCSI Manager sequence)
    wr(mem, Ncr5380::R_DATA, 0x80);
    wr(mem, Ncr5380::R_MODE, Ncr5380::MODE_ARBITRATE);
    wr(mem, Ncr5380::R_DATA, 0x81);
    wr(mem, Ncr5380::R_MODE, 0);
    wr(mem, Ncr5380::R_ICR, Ncr5380::ICR_SEL);
    check(rd(mem, Ncr5380::R_CSR) & Ncr5380::CBS_REQ, "REQ after selection");

    // READ(6) of block 0, one block, sent with ACK pulses
    const uint8_t cdb[6] = { 0x08, 0, 0, 0, 1, 0 };
    for (uint8_t b : cdb) {
        wr(mem, Ncr5380::R_DATA, b);
        wr(mem, Ncr5380::R_ICR, Ncr5380::ICR_ACK);
        wr(mem, Ncr5380::R_ICR, 0);
    }

    // Blind transfer: MODE_DMA + the handshaked window
    wr(mem, Ncr5380::R_MODE, Ncr5380::MODE_DMA);
    check((mem.pseudoVia().read(3) & 0x01) != 0, "DRQ visible in pseudo-VIA IFR bit 0");

    std::vector<uint8_t> block;
    block.push_back(mem.read8(0xF06000));            // narrow window
    block.push_back(uint8_t(mem.read16(0xF06000) >> 8));  // wide window
    block.push_back(uint8_t(mem.read16(0xF06000)));
    block.push_back(mem.read8(0xF12000));            // window alias
    while (block.size() < 512)
        block.push_back(mem.read8(0xF10260));        // pdma reg-6 alias
    check(block[0] == 0x45 && block[1] == 0x52,
          "block 0 starts with the 'ER' Driver Descriptor Map");

    // The phase change ends the blind transfer: DRQ drops and the next
    // window access BUS-ERRORS — this is how the SCSI Manager's MOVE.L
    // loop terminates (macscsi.cpp:5-52)
    bool berr = false;
    try { (void)mem.read8(0xF06000); } catch (moira::MmuBusError&) { berr = true; }
    check(berr, "window read past the data phase raises a bus error");
    berr = false;
    try { mem.write8(0xF06000, 0); } catch (moira::MmuBusError&) { berr = true; }
    check(berr, "window write without DRQ raises a bus error");

    // STATUS + MESSAGE via the register path (ACK pulses), then bus free
    wr(mem, Ncr5380::R_MODE, 0);
    auto ackByte = [&] {
        uint8_t b = rd(mem, Ncr5380::R_DATA);
        wr(mem, Ncr5380::R_ICR, Ncr5380::ICR_ACK);
        wr(mem, Ncr5380::R_ICR, 0);
        return b;
    };
    uint8_t status = ackByte();
    uint8_t msg = ackByte();
    check(status == 0x00 && msg == 0x00, "GOOD status + COMMAND COMPLETE");
    check(!(rd(mem, Ncr5380::R_CSR) & Ncr5380::CBS_BSY), "bus free after transfer");

    std::printf("%s\n", gFails ? "FAILED" : "PASSED");
    return gFails ? 1 : 0;
}
