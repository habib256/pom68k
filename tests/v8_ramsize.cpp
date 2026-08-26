// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// O6.2 gate — V8 memory controller vs the pinned model
// (docs/LCII_HARDWARE.md § RAM controller / § Address map / § Reset):
//   * boot overlay: ROM at 0, cleared by ANY read of $A00000-$AFFFFF,
//     default config $C0 installed on release;
//   * RAM config register: SIMM bank at 0, motherboard after the
//     CONFIGURED SIMM size, first 2 MB of motherboard ALWAYS at $800000,
//     holes read open bus — full table on a 10 MB machine;
//   * ROM mirrored ×2 in $A00000-$AFFFFF;
//   * VIA1 at $F00000 stride $200, byte mirrored on both lanes, E-clock
//     stall (v8.cpp via_sync formula);
//   * unmapped I/O in $F00000+ and the PDS space (A31) bus-error;
//   * V8 interrupt priority resolver (SCC=4 > VIA2=2 > VIA1=1);
//   * a synthetic 512 KB ROM boots through the real reset path.
// Exit 0 = pass, 1 = fail.

#include "V8Memory.h"
#include "Cpu030.h"

#include <cstdio>
#include <vector>

namespace {
int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}

// Synthetic 512 KB ROM: entry vector at +4 → $A00010, a boot stub that
// stores a marker to RAM and stops, and a valid stored checksum.
std::vector<uint8_t> makeRom() {
    std::vector<uint8_t> rom(V8Memory::kRomSize, 0);
    auto w16 = [&](uint32_t a, uint16_t v) { rom[a] = uint8_t(v >> 8); rom[a + 1] = uint8_t(v); };
    auto w32 = [&](uint32_t a, uint32_t v) { w16(a, uint16_t(v >> 16)); w16(a + 2, uint16_t(v)); };

    w32(4, 0x00A00010);                      // reset PC (SP = checksum, unused)
    // $A00010: MOVE.W #$B007, $2000.L ; STOP #$2700
    w16(0x10, 0x33FC); w16(0x12, 0xB007); w32(0x14, 0x00002000);
    w16(0x18, 0x4E72); w16(0x1A, 0x2700);
    for (uint32_t i = 0x20; i < rom.size(); i++) rom[i] = uint8_t(i * 7);

    uint32_t sum = 0;                        // stored checksum (bytes 4…end)
    for (size_t i = 4; i + 1 < rom.size(); i += 2)
        sum += uint32_t(rom[i] << 8 | rom[i + 1]);
    w32(0, sum);
    return rom;
}
} // namespace

int main() {
    std::printf("v8_ramsize — V8 memory controller + overlay (O6.2)\n");
    const auto rom = makeRom();

    // ── Overlay + boot through the real CPU reset path ──────────────────
    {
        V8Memory mem(pom68k::defaultCoreConfig(), 0xA00000);
        check(mem.loadRom(rom), "loadRom accepts a 512 KB image");
        Cpu030 cpu(mem, jit::defaultResolvedConfig(),
                   pom68k::defaultCoreConfig().cpu);
        mem.setCpu(&cpu);

        check(mem.peek8(0) == rom[0], "overlay: ROM visible at $000000");
        cpu.hardReset();                     // vectors from ROM at 0
        cpu.runCycles(400);
        check(!mem.overlay(), "boot: fetch at $A00010 cleared the overlay");
        check(mem.ramConfig() == 0, "boot: config reg still 0 (ROM hasn't sized)");
        check(mem.peek8(0x2000) == 0xB0 && mem.peek8(0x2001) == 0x07,
              "boot: stub stored its marker through the default $C0 map");
    }

    // ── RAM config table (10 MB machine, no CPU needed) ────────────────
    {
        V8Memory mem(pom68k::defaultCoreConfig(), 0xA00000);
        mem.loadRom(rom);
        (void)mem.read8(0xA00000);           // clear overlay → config $C0

        // $C0: SIMM 8 MB at 0 (physical bank at ram+2 MB), motherboard
        // only through the fixed $800000 alias — 10 MB usable
        mem.write8(0x000000, 0x11);
        mem.write8(0x800000, 0x22);
        check(mem.read8(0x000000) == 0x11 && mem.read8(0x800000) == 0x22 &&
              mem.read8(0x000000) != mem.read8(0x800000),
              "$C0: SIMM at 0 and MB alias at $800000 are distinct banks");
        mem.write8(0x7FFFFF, 0x77);
        check(mem.read8(0x7FFFFF) == 0x77, "$C0: SIMM reaches $7FFFFF (8 MB)");

        // $40: SIMM configured 2 MB → MB at $200000; MB byte 0 is the
        // same storage as the $800000 alias byte 0
        mem.write8(0xF26001, 0x40);
        check(mem.read8(0xF26001) == 0x44, "config reg reads back config | $04");
        mem.write8(0x200000, 0x33);
        check(mem.read8(0x800000) == 0x33, "$40: MB window base ≡ $800000 alias");

        // $80: SIMM configured 4 MB → MB at $400000
        mem.write8(0xF26001, 0x80);
        mem.write8(0x400000, 0x44);
        check(mem.read8(0x800000) == 0x44, "$80: MB window at $400000 ≡ alias");
        check(mem.read8(0x000000) == 0x11, "$80: SIMM still at 0");

        // $A0: the PHYSICAL SIMM (8 MB) still fills $000000-$7FFFFF even
        // when the config claims 4 MB (v8.cpp:387-397 installs simm_size)
        mem.write8(0xF26001, 0xA0);
        mem.write8(0x600000, 0x66);
        check(mem.read8(0x600000) == 0x66 && mem.read8(0x800000) != 0x66,
              "$A0: physical SIMM fills to $7FFFFF regardless of config");

        // $00: no SIMM mapped → MB at 0, hole above 4 MB
        mem.write8(0xF26001, 0x00);
        mem.write8(0x000000, 0x55);
        check(mem.read8(0x800000) == 0x55, "$00: MB at 0 ≡ $800000 alias");
        check(mem.read8(0x400000) == 0xFF, "$00: no SIMM → $400000 open bus");

        // MB truncation needs a machine without a SIMM bank: 4 MB total,
        // config $20 forces 2 MB of motherboard → hole at $200000
        V8Memory mem4(pom68k::defaultCoreConfig(), 0x400000);
        mem4.loadRom(rom);
        (void)mem4.read8(0xA00000);
        mem4.write8(0xF26001, 0x20);
        check(mem4.read8(0x200000) == 0xFF,
              "$20 on 4 MB: MB truncated to 2 MB, hole reads open bus");
    }

    // ── Tinker Bell (Mac TV) RAM override ──────────────────────────────
    // MAME v8.cpp:1066-1101 tinkerbell_device::ram_size: motherboard 4 MB
    // ALWAYS at 0, SIMM ALWAYS at $400000, and NO $800000 motherboard
    // image — the $C0 config ("8 MB SIMM, alias only" on V8) means
    // 4 MB motherboard + 4 MB SIMM here. Pure address decode, no CPU.
    {
        V8Memory mem(pom68k::defaultCoreConfig(), 0x800000,
                     V8Memory::Model::MacTv, V8Memory::kCpuHzTv);
        mem.loadRom(rom);
        (void)mem.read8(0xA00000);           // clear overlay → config $C0

        mem.write8(0x000000, 0x11);          // motherboard byte 0
        mem.write8(0x400000, 0x22);          // SIMM byte 0
        check(mem.read8(0x000000) == 0x11 && mem.read8(0x400000) == 0x22,
              "MacTv $C0: MB at 0 and SIMM at $400000, distinct banks");
        mem.write8(0x7FFFFF, 0x33);
        check(mem.read8(0x7FFFFF) == 0x33,
              "MacTv $C0: full 8 MB usable up to $7FFFFF");
        check(mem.read8(0x800000) == 0xFF && mem.read8(0x9FFFFF) == 0xFF,
              "MacTv: no $800000 motherboard image (open bus)");
        mem.write8(0x800000, 0x44);          // must land nowhere
        check(mem.read8(0x000000) == 0x11,
              "MacTv: a $800000 write does not hit motherboard RAM");

        mem.write8(0xF26001, 0x00);          // SIMM bits clear → unmapped
        check(mem.read8(0x000000) == 0x11 && mem.read8(0x400000) == 0xFF,
              "MacTv $00: MB stays at 0, SIMM hole reads open bus");
    }

    // ── ROM window ───────────────────────────────────────────────────
    {
        V8Memory mem(pom68k::defaultCoreConfig());
        mem.loadRom(rom);
        check(mem.read8(0xA00011) == rom[0x11], "ROM readable at $A00000");
        check(mem.read8(0xA80011) == rom[0x11], "ROM mirrored ×2 at $A80000");
        mem.write8(0xA00011, 0x00);
        check(mem.read8(0xA00011) == rom[0x11], "ROM writes ignored");
        check(mem.read8(0xB00000) == 0xFF, "$B00000: open bus, no bus error");
    }

    // ── VIA1 access + E-clock sync ──────────────────────────────────────
    {
        V8Memory mem(pom68k::defaultCoreConfig());
        mem.loadRom(rom);
        Cpu030 cpu(mem, jit::defaultResolvedConfig(),
                   pom68k::defaultCoreConfig().cpu);
        mem.setCpu(&cpu);

        mem.write8(0xF00600, 0xFF);          // DDRA (reg 3, $200 stride)
        mem.write8(0xF00200, 0xA5);          // ORA (reg 1)
        check(mem.via1().portA() == 0xA5, "VIA1: ORA reachable at $F00200");
        uint16_t w = mem.read16(0xF00200);
        check(w == 0xA5A5, "VIA1: word read mirrors the byte on both lanes");

        // via_sync: first access from clock 0 stalls to (0*2+3)*10+1 = 31
        V8Memory mem2(pom68k::defaultCoreConfig(), 0x400000);
        mem2.loadRom(rom);
        Cpu030 cpu2(mem2, jit::defaultResolvedConfig(),
                    pom68k::defaultCoreConfig().cpu);
        mem2.setCpu(&cpu2);
        mem2.read8(0xF00200);
        // In MACHINE cycles: the core clock runs cacheBoost_× fast, and bus
        // time is not accelerated by the i-cache (CHANGELOG 2026-07-25).
        check(cpu2.machineClock() == 31, "VIA1: E-clock sync stalls the CPU (31 cyc @ 0)");
    }

    // ── Bus errors: unmapped I/O + PDS space; VRAM/devices don't ───────
    {
        V8Memory mem(pom68k::defaultCoreConfig());
        mem.loadRom(rom);
        Cpu030 cpu(mem, jit::defaultResolvedConfig(),
                   pom68k::defaultCoreConfig().cpu);
        mem.setCpu(&cpu);

        bool berr = false;
        try { mem.read8(0xF30000); } catch (moira::MmuBusError&) { berr = true; }
        check(berr, "unmapped I/O $F30000 raises a bus error");

        berr = false;
        try { mem.write8(0xF02000, 0); } catch (moira::MmuBusError&) { berr = true; }
        check(berr, "unmapped I/O hole $F02000 raises a bus error");

        berr = false;
        try { mem.read8(0x80001000); } catch (moira::MmuBusError&) { berr = true; }
        check(berr, "PDS slot space (A31 set) raises a bus error");

        mem.write8(0xF40000, 0x99);
        check(mem.read8(0xF40000) == 0x99, "VRAM readable/writable, no bus error");
        check(mem.read8(0xF14800) == 0xE8, "ASC version register reads $E8");

        moira::i64 c0 = cpu.machineClock();  // machine cycles, see above
        (void)mem.read8(0xF16000);           // IWM-compatible reg read
        check(cpu.machineClock() == c0 + 5, "SWIM access eats 5 CPU cycles");
    }

    // ── Ariel palette through the bus ───────────────────────────────────
    {
        V8Memory mem(pom68k::defaultCoreConfig());
        mem.loadRom(rom);
        mem.write8(0xF24000, 1);             // address = 1
        mem.write8(0xF24001, 0x10);          // R
        mem.write8(0xF24001, 0x20);          // G
        mem.write8(0xF24001, 0x30);          // B, then auto-increment
        check(mem.ariel().pen(1) == 0x102030, "Ariel: palette write RGB auto-inc");

        // Reading the address register resets the RGB write phase
        // (Brooktree behaviour, MAME ariel.cpp:96-106).
        mem.write8(0xF24000, 2);             // address = 2, phase 0
        mem.write8(0xF24001, 0x40);          // R (phase → 1)
        (void)mem.read8(0xF24000);           // address read → phase back to 0
        mem.write8(0xF24001, 0x50);          // overwrites R, not G
        mem.write8(0xF24001, 0x60);          // G
        mem.write8(0xF24001, 0x70);          // B
        check(mem.ariel().pen(2) == 0x506070,
              "Ariel: address-register read resets the RGB phase");
    }

    // ── Spice VIA1 port A id ────────────────────────────────────────────
    // The Color Classic reads a plain $82 — no diag-bit OR, spice has no
    // config input port at all (MAME v8.cpp:703-704, 755-758).
    {
        V8Memory cc(pom68k::defaultCoreConfig(), 0xA00000,
                    V8Memory::Model::ColorClassic);
        check(cc.via1().portA() == 0x82, "Spice: VIA1 PA reads plain $82");
    }

    // ── Interrupt priority resolver ─────────────────────────────────────
    {
        V8Memory mem(pom68k::defaultCoreConfig());
        mem.loadRom(rom);
        check(mem.iplLevel() == 0, "resolver: idle = 0");

        mem.write8(0xF26013, 0x88);          // pseudo-VIA IER: enable SCSI IRQ
        mem.scsiIrq(true);
        check(mem.iplLevel() == 2, "resolver: pseudo-VIA → IPL 2");

        mem.via1().write(Via6522::IER, 0x82);  // enable CA1
        mem.via1().raiseCa1();
        check(mem.iplLevel() == 2, "resolver: VIA2 wins over VIA1");

        mem.sccIrqLine(true);
        check(mem.iplLevel() == 4, "resolver: SCC wins at IPL 4");

        mem.sccIrqLine(false);
        mem.scsiIrq(false);
        check(mem.iplLevel() == 1, "resolver: VIA1 alone → IPL 1");
    }

    // ── 60.15 Hz tick timer → VIA1 CA1 ──────────────────────────────────
    {
        V8Memory mem(pom68k::defaultCoreConfig());
        mem.loadRom(rom);
        mem.via1().write(Via6522::IER, 0x82);  // enable CA1
        int ticks = 0;
        // 1 emulated second in 1000-cycle slices
        for (int64_t c = 0; c < V8Memory::kCpuHz; c += 1000) {
            mem.tick(1000);
            if (mem.via1().irqAsserted()) {
                ticks++;
                mem.via1().read(Via6522::IFR);           // peek
                mem.via1().write(Via6522::IFR, 0x02);    // ack CA1
            }
        }
        check(ticks == 60, "tick timer: 60 CA1 pulses in one second (60.15 Hz)");
    }

    // ── Ariel register file: two pins against a future parity diff ──────
    // Audit docs/MAME_PARITY_AUDIT.md § 2.13/§ 2.14, both "POM68K plus
    // riche". The V8 maps the RAMDAC at $F24000 over an 8 KB window
    // (v8.cpp:93, map(0x524000,0x525fff)).
    {
        V8Memory mem(pom68k::defaultCoreConfig());
        mem.loadRom(rom);

        // 1. The key-color register at +3 is REAL here. MAME declares
        // m_key_color, saves it, and then aliases both accessors onto
        // m_control (ariel.cpp:169-177) — so on master a +3 write silently
        // rewrites the depth / master-slave control byte. A plain bug: the
        // file's own header (:13-25) describes +3 as the overlay key.
        mem.write8(0xF24002, 0x0B);          // control: 8 bpp, master
        mem.write8(0xF24003, 0x5A);          // key colour
        check(mem.read8(0xF24002) == 0x0B,
              "Ariel: a key-colour write leaves control alone (MAME aliases)");
        check(mem.read8(0xF24003) == 0x5A,
              "Ariel: the key-colour register reads back its own value");

        // 2. Only A0-A1 are decoded — the four registers mirror over the
        // whole 8 KB select. MAME's device switches on the raw map offset
        // and answers 0 from +4 up (ariel.cpp:62-94); an 8 KB chip select
        // on a 4-register RAMDAC is partial decode on real silicon. Dead
        // path either way (nothing shipped touches +4 and up).
        check(mem.read8(0xF24006) == 0x0B && mem.read8(0xF25FFE) == 0x0B,
              "Ariel: control mirrors across the 8 KB window (partial decode)");
        mem.write8(0xF25FFF, 0xA5);          // write through the mirror…
        check(mem.read8(0xF24003) == 0xA5,   // …lands on the key register
              "Ariel: writes through the mirror reach the same register");

        // The palette phase itself: address, R, G, B, auto-increment.
        mem.write8(0xF24000, 0x20);
        mem.write8(0xF24001, 0x11);
        mem.write8(0xF24001, 0x22);
        mem.write8(0xF24001, 0x33);
        check(mem.read8(0xF24000) == 0x21,
              "Ariel: the address auto-increments after a full RGB triple");
    }

    std::printf("%s\n", gFails ? "FAILED" : "PASSED");
    return gFails ? 1 : 0;
}
