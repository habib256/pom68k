// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Egret/Cuda firmware-LLE gate, blueprint step 1 (TODO / LLE_VS_HLE step
// 10): the M68hc05 core executes the REAL Cuda 2.37 firmware
// (roms/cuda/341s0788.bin, 68HC05E1) from its reset vector with idle port
// levels wired per MAME mame/apple/cuda.cpp (pa_r/pb_r/pc_r) and checks
// the firmware actually runs: no undefined opcode, the PLL gets
// programmed (rate 3 after MAME's :140 cheat), port directions are set,
// RAM/stack traffic happens. Soft-skips without the dump.

#include "M68hc05.h"
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {
int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-62s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}
std::string findAsset(const char* rel) {
    for (const std::string base : { std::string(), std::string("../") }) {
        std::string p = base + rel;
        if (std::ifstream(p, std::ios::binary)) return p;
    }
    return {};
}
} // namespace

int main() {
    std::printf("m68hc05_test — Cuda 2.37 firmware on the M68HC05E1 core\n");

    std::string path = findAsset("roms/cuda/341s0788.bin");
    if (path.empty()) { std::printf("SKIP: needs roms/cuda/341s0788.bin\n"); return 0; }
    std::ifstream in(path, std::ios::binary);
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());

    M68hc05 mcu;
    check(mcu.loadRom(rom), "341s0788.bin is 0x1100 bytes and loads at $0F00");

    // Idle inputs per MAME cuda.cpp: PA = pull-up|PFW|ADB-in-high|ADB-power-
    // off = $47; PB = +5v|BYTEACK|TIP|VIA-data|SDA|bit7 = $ED; PC = $03.
    mcu.readPort = [](int p) -> uint8_t {
        switch (p) { case 0: return 0x47; case 1: return 0xED; default: return 0x03; }
    };
    long pbWrites = 0;
    mcu.writePort = [&](int p, uint8_t) { if (p == 1) pbWrites++; };
    mcu.setPullups(1, 0xC0);                          // cuda.cpp:88-89
    mcu.setPullups(2, 0x04);

    mcu.reset();
    const uint16_t vec = uint16_t(rom[0x10FE] << 8 | rom[0x10FF]);
    check(mcu.pc() == vec, "reset vector fetched from $1FFE");
    check(vec >= 0x0F00 && vec <= 0x1FFF, "reset vector points into ROM");

    // ≈1 second at the post-PLL 2.1 MHz cycle rate.
    mcu.run(2000000);

    if (mcu.illegal())
        std::printf("  ! undefined opcode $%02X at $%04X after %ld instructions\n",
                    mcu.illegalOp(), mcu.illegalPc(), mcu.instructions);
    check(!mcu.illegal(), "no undefined opcode over 2M cycles");
    check(mcu.instructions > 10000, "firmware executes (not stuck in a 2-op loop)");
    check(mcu.pllWrites >= 1 && (mcu.pll() & 3) == 3,
          "firmware programs the PLL (rate 3 via the MAME :140 cheat)");
    check(mcu.ddrWrites >= 1, "firmware sets port directions");
    check(pbWrites >= 1, "firmware drives port B (VIA handshake side)");
    check(mcu.pc() >= 0x0F00, "PC still executing from ROM");

    // Interrupt entry is 11 cycles on the silicon, followed by the first
    // handler opcode in the same run() iteration. A two-cycle NOP therefore
    // consumes exactly 13 cycles after an asserted IRQ. This catches the old
    // zero-cycle timing accommodation independently of any firmware phase.
    {
        std::vector<uint8_t> irqRom(0x1100, 0x9D); // NOP-filled $0F00-$1FFF
        irqRom[0x0000] = 0x9A;                     // reset: CLI
        irqRom[0x10FA] = 0x0F; irqRom[0x10FB] = 0x10; // IRQ -> $0F10
        irqRom[0x10FE] = 0x0F; irqRom[0x10FF] = 0x00; // RESET -> $0F00
        M68hc05 irqMcu;
        check(irqMcu.loadRom(irqRom), "synthetic interrupt timing ROM loads");
        irqMcu.reset();
        irqMcu.run(1);                              // CLI = 2 cycles
        irqMcu.setIrqLine(true);
        const int used = irqMcu.run(1);             // entry 11 + NOP 2
        check(used == 13 && irqMcu.pc() == 0x0F11,
              "IRQ entry charges 11 cycles before handler opcode");
    }

    std::printf("  [%ld instructions, %ld port writes, PC=$%04X%s]\n",
                mcu.instructions, mcu.portWrites, mcu.pc(),
                mcu.waiting() ? " (WAIT)" : "");

    // The other firmware revisions on hand must run clean too.
    for (const char* rel : { "roms/cuda/341s0417.bin",     // Cuda 2.35
                             "roms/cuda/341s0789.bin",     // Cuda 2.38 (Mac TV)
                             "roms/cuda/341s0060.bin" }) { // Cuda 2.40
        std::string p2 = findAsset(rel);
        if (p2.empty()) continue;
        std::ifstream in2(p2, std::ios::binary);
        std::vector<uint8_t> rom2((std::istreambuf_iterator<char>(in2)),
                                  std::istreambuf_iterator<char>());
        M68hc05 m2;
        m2.readPort = mcu.readPort;
        m2.setPullups(1, 0xC0); m2.setPullups(2, 0x04);
        if (!m2.loadRom(rom2)) { check(false, rel); continue; }
        m2.reset();
        m2.run(2000000);
        if (m2.illegal())
            std::printf("  ! %s: opcode $%02X at $%04X\n", rel,
                        m2.illegalOp(), m2.illegalPc());
        check(!m2.illegal() && m2.instructions > 10000 && (m2.pll() & 3) == 3,
              rel);
    }

    if (gFails) { std::printf("FAILED (%d)\n", gFails); return 1; }
    std::printf("PASS\n");
    return 0;
}
