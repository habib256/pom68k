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
#include <algorithm>
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

    // ── Stacked CC bits 7-5 (MAME-parity audit §2.10, POM68K is MORE
    // faithful — pinned 2026-08-06). The 6805 CCR is five bits wide and
    // its top three read as 1 on silicon (MC68HC05 family manual,
    // CCR = 1 1 1 H I N Z C), so the interrupt sequence stacks a byte with
    // $E0 set; MAME pushes its raw m_cc (m6805.cpp:554/561), whose bits
    // 7-5 are 0. A future parity diff must not "fix" cc()'s `| 0xE0` —
    // the stacked byte is what RTI pops back and what a firmware walking
    // its own stack frame sees. Runs on a synthetic ROM so it holds with
    // or without the Cuda dumps below.
    {
        std::vector<uint8_t> synth(0x1100, 0x9D);          // NOP fill
        auto put = [&](uint16_t addr, std::initializer_list<uint8_t> bytes) {
            size_t o = size_t(addr) - 0x0F00;
            for (uint8_t v : bytes) synth[o++] = v;
        };
        put(0x1000, { 0x9A, 0x20, 0xFE });                 // CLI ; BRA *
        put(0x1010, { 0x20, 0xFE });                       // IRQ handler: BRA *
        put(0x1FFA, { 0x10, 0x10 });                       // IRQ vector   $1010
        put(0x1FFE, { 0x10, 0x00 });                       // reset vector $1000
        M68hc05 m;
        check(m.loadRom(synth), "synthetic 0x1100 ROM loads at $0F00");
        m.reset();
        check(m.pc() == 0x1000, "synthetic reset vector taken");
        m.run(40);                                         // CLI, then spin
        check((m.cc() & 0xE0) == 0xE0, "live CC reads 111HINZC");
        const uint8_t sp0 = m.sp();                        // $FF, nothing pushed
        m.setIrqLine(true);
        m.run(40);
        check(m.pc() == 0x1010, "external /IRQ vectors through $1FFA");
        // pushState order is PCL, PCH, X, A, CC and push8 writes to
        // $00C0 | (sp & $3F) before decrementing, so CC lands at sp0-4.
        const uint8_t stacked = m.ramByte(0x00C0 | ((sp0 - 4) & 0x3F));
        check((stacked & 0xE0) == 0xE0, "stacked CC forces bits 7-5 to 1");
    }

    std::string path = findAsset("roms/cuda/341s0788.bin");
    if (path.empty()) {
        // The synthetic-ROM block above is dump-free, so it must still be
        // able to fail the gate when the Cuda firmware is absent.
        std::printf("SKIP: needs roms/cuda/341s0788.bin\n");
        return gFails ? 1 : 0;
    }
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

    // onesec_w re-arms the 1 Hz phase on EVERY write (m68hc05e1.cpp:201 —
    // an unconditional m_timer->adjust(from_seconds(1))). A loop that keeps
    // rewriting $12 therefore keeps pushing the next CPI tick a second
    // away: it never lands. (MAME-parity audit #47.)
    {
        std::vector<uint8_t> rom47(0x1100, 0x9D);            // NOP-filled
        const uint8_t code[] = { 0xA6, 0x10,                 // LDA #$10 (CPI en)
                                 0xB7, 0x12,                 // STA $12 (arm)
                                 0x9A,                       // CLI
                                 0xB7, 0x12,                 // loop: STA $12
                                 0x20, 0xFC };               // BRA loop
        std::copy(std::begin(code), std::end(code), rom47.begin());
        rom47[0x0080] = 0x3C; rom47[0x0081] = 0x90;          // CPI: INC $90
        rom47[0x0082] = 0x80;                                //      RTI
        rom47[0x10F6] = 0x0F; rom47[0x10F7] = 0x80;          // CPI  -> $0F80
        rom47[0x10FE] = 0x0F; rom47[0x10FF] = 0x00;          // RESET -> $0F00
        M68hc05 m47;
        check(m47.loadRom(rom47), "onesec re-arm ROM loads");
        m47.reset();
        m47.run(2097152 * 5 / 2);                            // 2.5 s of cycles
        check(m47.ramByte(0x90) == 0,
              "constant onesec_w rewrites hold off the 1 Hz tick (:201)");
    }

    // ...and armed once (firmware-style: the CPI handler re-writes $12 to
    // ack), the cadence stays one tick per second.
    {
        std::vector<uint8_t> rom1s(0x1100, 0x9D);
        const uint8_t code[] = { 0xA6, 0x10,                 // LDA #$10
                                 0xB7, 0x12,                 // STA $12 (arm)
                                 0x9A,                       // CLI
                                 0x20, 0xFE };               // spin
        std::copy(std::begin(code), std::end(code), rom1s.begin());
        rom1s[0x0080] = 0x3C; rom1s[0x0081] = 0x90;          // CPI: INC $90
        rom1s[0x0082] = 0xA6; rom1s[0x0083] = 0x10;          //      LDA #$10
        rom1s[0x0084] = 0xB7; rom1s[0x0085] = 0x12;          //      STA $12 (ack)
        rom1s[0x0086] = 0x80;                                //      RTI
        rom1s[0x10F6] = 0x0F; rom1s[0x10F7] = 0x80;          // CPI  -> $0F80
        rom1s[0x10FE] = 0x0F; rom1s[0x10FF] = 0x00;          // RESET -> $0F00
        M68hc05 m1s;
        check(m1s.loadRom(rom1s), "1 Hz cadence ROM loads");
        m1s.reset();
        m1s.run(2097152 * 5 / 2);                            // 2.5 s of cycles
        check(m1s.ramByte(0x90) == 2,
              "armed once, CPI ticks land at 1 s and 2 s");
    }

    // TOF acked while I is still masked WITHDRAWS the pending request —
    // level semantics (request = flag AND enable), per MAME's detailed
    // HC05 model (m68hc05.cpp:313-318 clears M68HC05_INT_TIMER on ack);
    // the e1 device's keep-until-taken is the generic m6805 external-pin
    // latch (m6805.cpp:541-546) defeating its own set_input_line(CLEAR).
    // (MAME-parity audit #46, refuted: POM68K is correct.)
    {
        std::vector<uint8_t> rom46(0x1100, 0x9D);            // NOP sled
        const uint8_t setup[] = { 0xA6, 0x20,                // LDA #$20 (TOIE)
                                  0xB7, 0x08,                // STA $08
                                  0xA6, 0x01,                // LDA #$01
                                  0xB7, 0x07 };              // STA $07 (arm PLL)
        std::copy(std::begin(setup), std::end(setup), rom46.begin());
        // 264 NOPs ($0F08-$100F) = 528 cycles: TOF sets mid-sled, I masked.
        rom46[0x0110] = 0xB6; rom46[0x0111] = 0x08;          // LDA $08
        rom46[0x0112] = 0xB7; rom46[0x0113] = 0x92;          // STA $92 (record)
        rom46[0x0114] = 0xA6; rom46[0x0115] = 0x20;          // LDA #$20
        rom46[0x0116] = 0xB7; rom46[0x0117] = 0x08;          // STA $08 (ack TOF)
        rom46[0x0118] = 0x9A;                                // CLI
        rom46[0x0119] = 0x20; rom46[0x011A] = 0xFE;          // spin
        rom46[0x0180] = 0x3C; rom46[0x0181] = 0x91;          // TIMER: INC $91
        rom46[0x0182] = 0x80;                                //        RTI
        rom46[0x10F8] = 0x10; rom46[0x10F9] = 0x80;          // TIMER -> $1080
        rom46[0x10FE] = 0x0F; rom46[0x10FF] = 0x00;          // RESET -> $0F00
        M68hc05 m46;
        check(m46.loadRom(rom46), "TOF withdraw ROM loads");
        m46.reset();
        m46.run(700);   // next overflow at ~1036 — out of reach
        check(m46.ramByte(0x92) == 0xA0,
              "TOF was set and observed before the ack");
        check(m46.ramByte(0x91) == 0,
              "acked TOF must not deliver a vector after CLI (level irq)");
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
