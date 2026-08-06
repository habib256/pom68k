// POM68K — MSC MAME-parity unit gate (PowerBook Duo platform), no ROMs
// needed. Covers two fixes:
//  A. SOUND_BUSY is bit 6 of the MSC sound-ctrl reg (msc.cpp:24), set by
//     any ASC write (msc.cpp:130-134 install_write_tap) and cleared by
//     reading pseudo-VIA offset $22 (msc_pseudovia_r:225-238) — the old
//     model used bit 0, so the PMU idle/sleep logic always saw "sound
//     inactive".
//  B. The PMU releasing /RESET on its port E bit 2 rising edge re-arms
//     the ROM overlay and resets the 68030 (macpwrbkmsc.cpp
//     pmu_porte_w:431-441 → msc.cpp pmu_reset_w:363-378) — the old model
//     let a BORG-commanded reboot resume stale state. Part B drives the
//     real M68hc05Pge core with a synthetic 512-byte boot ROM that
//     toggles port E, so the edge detection itself is under test.
//  C. Port H reads return the write latch, which starts at $00 — bit 0
//     (DFAC reset) reading 0 is a precondition for the boot ROM's DFAC
//     config (macpwrbkmsc.cpp:129 m_last_porth, pmu_porth_r:543-546) —
//     the old model returned open-bus $FF.
//  D. The PMU RTC is seeded from host time at machine construction
//     (MAME m68hc05pge.cpp:185-187 device_start) and survives reset;
//     unseeded the Duo guest clock sat at the 1904 epoch.
//  E. The two PG&E cosmetics kept on purpose (MAME-parity audit §2.12,
//     2026-08-06): ports J/K/L unwired inputs read $FF where MAME reads 0
//     — the residue of C, since MAME binds no read handler for those three
//     and its latch rule does not transfer — and STOP ($8E) approximated
//     as WAIT, where MAME fatalerrors. Pinned so a parity diff cannot flip
//     them silently; see src/PgePmu.cpp and src/M68hc05Pge.cpp.
//  F. PG&E NVRAM persistence: MscMemory::loadPram/savePram round trip in
//     MAME's layout, including the $91 power-flag scrub on load
//     (m68hc05pge.cpp:955-975). The pair is machine-side only — the Duo
//     has no kProfiles row, so nothing in main.cpp calls it yet.
// Registered in CMakeLists.txt (msc_parity_test, links pom68k_core).

#include "MscCpu.h"
#include "MscMemory.h"
#include "PgePmu.h"
#include "Via6522.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) failures++;
}

// ── A. SOUND_BUSY semantics on the full MscMemory decode ───────────────
static void testSoundBusy() {
    MscMemory mem(8u << 20, MscMemory::kCpuHz230, MscMemory::kIdDuo230);
    const uint32_t kSndCtrl = 0x50F26022;    // pseudo-VIA MSC block reg 2
    const uint32_t kAsc = 0x50F15000;        // inside the $14000-$15FFF tap

    check(mem.read8(kSndCtrl) == 0, "sound ctrl starts clear");

    mem.write8(kAsc, 0x55);                  // any ASC write width marks busy
    uint8_t r = mem.read8(kSndCtrl);
    check((r & 0x40) != 0, "ASC byte write sets SOUND_BUSY bit 6");
    check((r & 0x01) == 0, "bit 0 stays clear (old wrong bit)");
    check((mem.read8(kSndCtrl) & 0x40) == 0, "read clears SOUND_BUSY");

    mem.write16(kAsc, 0x1234);               // word path decomposes to write8
    check((mem.read8(kSndCtrl) & 0x40) != 0, "ASC word write sets bit 6");
    mem.read8(kSndCtrl);                     // drain

    // A host write to reg 2 stores as-is; only the read clears bit 6
    // (msc_pseudovia_w:248-251).
    mem.write8(kSndCtrl, 0xFF);
    check(mem.read8(kSndCtrl) == 0xFF, "reg-2 write read back whole");
    check(mem.read8(kSndCtrl) == 0xBF, "read-clear removed only bit 6");
}

// ── B. Port E bit 2 edge → onCpuReset, on the real 68HC05 core ─────────
// Synthetic 512-B boot ROM @ $FE00 (reset vector at $FFFE):
//   LDA #$FF / STA $21   DDR E all-output (drives port E = $00: the bit-2
//                        fall drops the power-on hold, no reset callback)
//   LDA #$04 / STA $20   bit 2 rise #1 → onCpuReset
//   LDA #$00 / STA $20   bit 2 fall → held again (level)
//   LDA #$04 / STA $20   bit 2 rise #2 → onCpuReset (the "reboot")
//   BRA *
static void testPorteReset() {
    std::vector<uint8_t> fw(512, 0x9D);      // NOP filler
    const uint8_t prog[] = { 0xA6, 0xFF, 0xB7, 0x21,
                             0xA6, 0x04, 0xB7, 0x20,
                             0xA6, 0x00, 0xB7, 0x20,
                             0xA6, 0x04, 0xB7, 0x20,
                             0x20, 0xFE };
    std::copy(std::begin(prog), std::end(prog), fw.begin());
    fw[0x1FE] = 0xFE; fw[0x1FF] = 0x00;      // reset vector → $FE00

    const auto path = std::filesystem::temp_directory_path()
                    / "pom68k_msc_parity_pge.bin";
    { std::ofstream out(path, std::ios::binary);
      out.write(reinterpret_cast<const char*>(fw.data()),
                std::streamsize(fw.size())); }

    Via6522 via;
    PgePmu pmu(via, MscMemory::kCpuHz230);
    check(pmu.loadBootRom(path.string()), "synthetic PG&E boot ROM loads");
    int resets = 0;
    pmu.onCpuReset = [&] { resets++; };
    pmu.reset();
    check(pmu.cpuHeld(), "power-on: 68030 held");
    pmu.tick(200000);                        // ~12.7k MCU cycles, plenty
    check(!pmu.cpuHeld(), "final bit-2 high releases the CPU");
    check(resets == 2, "onCpuReset fired on each rising edge (2)");
    std::filesystem::remove(path);
}

// ── B2. Machine side: onCpuReset re-arms the overlay + latches the CPU
// reset for the next run boundary (same action as the port G wake path).
static void testMachineWiring() {
    MscMemory mem(8u << 20, MscMemory::kCpuHz230, MscMemory::kIdDuo230);
    check(static_cast<bool>(mem.pmu().onCpuReset),
          "MscMemory wires pmu.onCpuReset");
    mem.consumeWakeReset();                  // drain any power-on latch
    mem.read8(0x40000000);                   // first ROM-space read drops
    check(!mem.overlay(), "overlay off after ROM-space access");
    if (mem.pmu().onCpuReset) mem.pmu().onCpuReset();
    check(mem.overlay(), "onCpuReset re-arms the ROM overlay");
    check(mem.consumeWakeReset(), "onCpuReset latches the CPU reset");
    check(!mem.consumeWakeReset(), "wake-reset latch is one-shot");
}

// ── C. Port H read-back latch, on the real 68HC05 core ─────────────────
// Synthetic 512-B boot ROM (port H = $26, DDRH = $27, RAM at $40+):
//   LDA $26 / STA $40    reset-state read → RAM[0]; MAME latch = $00
//                        (open-bus $FF here was finding #48)
//   LDA #$FF / STA $27   DDRH all-output (drives $00: ACK bit 6 falls)
//   LDA #$40 / STA $26   drive /PMU_ACK high → latch = $40
//   LDA $26 / STA $41    read-back → RAM[1] = $40
//   LDA #$A5 / STA $42   execution marker
//   BRA *
static void testPortHLatch() {
    std::vector<uint8_t> fw(512, 0x9D);              // NOP filler
    const uint8_t prog[] = { 0xB6, 0x26, 0xB7, 0x40,
                             0xA6, 0xFF, 0xB7, 0x27,
                             0xA6, 0x40, 0xB7, 0x26,
                             0xB6, 0x26, 0xB7, 0x41,
                             0xA6, 0xA5, 0xB7, 0x42,
                             0x20, 0xFE };
    std::copy(std::begin(prog), std::end(prog), fw.begin());
    fw[0x1FE] = 0xFE; fw[0x1FF] = 0x00;              // reset vector → $FE00

    const auto path = std::filesystem::temp_directory_path()
                    / "pom68k_msc_parity_porth.bin";
    { std::ofstream out(path, std::ios::binary);
      out.write(reinterpret_cast<const char*>(fw.data()),
                std::streamsize(fw.size())); }

    Via6522 via;
    PgePmu pmu(via, MscMemory::kCpuHz230);
    check(pmu.loadBootRom(path.string()), "port-H boot ROM loads");
    pmu.reset();
    pmu.tick(200000);
    check(pmu.mcu().ramByte(2) == 0xA5, "port-H program ran to completion");
    check(pmu.mcu().ramByte(0) == 0x00,
          "port H reads the $00 latch at reset, not open-bus $FF");
    check(pmu.mcu().ramByte(1) == 0x40, "port H reads back the driven value");
    check(pmu.pmuAck(), "/PMU_ACK level follows the bit-6 write");
    check(pmu.pmuAckEdges == 2, "ACK edges: fall on DDR drive, rise on $40");
    std::filesystem::remove(path);
}

// ── D. PMU RTC seeded from host time at machine construction ───────────
static void testRtcSeed() {
    MscMemory mem(8u << 20, MscMemory::kCpuHz230, MscMemory::kIdDuo230);
    const uint32_t s = mem.pmu().mcu().rtc();
    // Mac epoch (1904) + 1970 offset 2 082 844 800; 2020-01-01 local =
    // 3 660 681 600. Any host clock this test runs on is past that, and
    // the u32 rolls over in 2040 — "plausible epoch" is the window.
    check(s != 0, "PMU RTC is seeded (not the 1904 epoch)");
    check(s >= 3660681600u, "PMU RTC is past 2020 (host-time seed)");
    mem.reset();
    check(mem.pmu().mcu().rtc() == s, "seed survives machine reset");
    mem.setRtcSeconds(0xA1B2C3D4);                   // GUI re-seed forwarder
    check(mem.pmu().mcu().rtc() == 0xA1B2C3D4, "setRtcSeconds forwards");
}

// ── E. The two remaining PG&E cosmetics, PINNED not fixed ──────────────
// MAME-parity audit §2.12, DOCUMENT-SKIP 2026-08-06. Nothing here asserts
// MAME parity; it asserts the divergences POM68K deliberately keeps, so a
// future parity diff cannot flip them by accident and call it a cleanup.
//
//  E1. Unwired inputs read $FF, MAME reads 0. Part C fixed the ONE port
//      where MAME binds a read handler (port H → m_last_porth, and its $00
//      start value is load-bearing). Ports J/K/L are the residue: MAME
//      binds no pmu_portj_r / pmu_portk_r / pmu_portl_r at all, so its
//      unbound `m_read_p(*this, 0)` default (m68hc05pge.cpp:115) reads them
//      as literal 0 — the port-H RULE does NOT transfer, there is no latch
//      to read back. Reachable, not theoretical: the real boot ROM
//      (roms/pge/pge_boot.bin) writes DDRJ = $D0 and DDRL = $07, leaving
//      J bits 5,3-0 and L bits 7-3 as inputs, and then does BSET/BCLR on
//      port J, which reads them back. Left at $FF because the only
//      coverage is a full cold boot (duo230_boot_etalon) and $FF is what
//      the Duo boots on today. The mixed read below is the discriminator:
//      $FF here, $D0 under MAME's rule.
//  E2. STOP ($8E) is approximated as WAIT — core idle, on-chip timers
//      still counting. MAME has no behaviour to match: both its stop and
//      wait handlers are fatalerror (6805ops.hxx:527-539).
//
// Synthetic 512-B boot ROM (PORTJ $28, DDRJ $29, PORTL $2A, PORTK $2C,
// RAM at $40+):
//   LDA $28/$2C/$2A → RAM[0..2]   reset-state reads, DDR = 0 (all input)
//   LDA #$D0 / STA $29            DDRJ as the real boot ROM sets it
//   LDA #$FF / STA $28            drive the J output latch
//   LDA $28 / STA $43             mixed output-latch + input read → RAM[3]
//   LDA #$A5 / STA $44            execution marker → RAM[4]
//   STOP                          must park, not fall through
//   LDA #$5A / STA $45            unreachable → RAM[5] stays 0
static void testPortDefaultsAndStop() {
    std::vector<uint8_t> fw(512, 0x9D);              // NOP filler
    const uint8_t prog[] = { 0xB6, 0x28, 0xB7, 0x40,
                             0xB6, 0x2C, 0xB7, 0x41,
                             0xB6, 0x2A, 0xB7, 0x42,
                             0xA6, 0xD0, 0xB7, 0x29,
                             0xA6, 0xFF, 0xB7, 0x28,
                             0xB6, 0x28, 0xB7, 0x43,
                             0xA6, 0xA5, 0xB7, 0x44,
                             0x8E,
                             0xA6, 0x5A, 0xB7, 0x45,
                             0x20, 0xFE };
    std::copy(std::begin(prog), std::end(prog), fw.begin());
    fw[0x1FE] = 0xFE; fw[0x1FF] = 0x00;              // reset vector → $FE00

    const auto path = std::filesystem::temp_directory_path()
                    / "pom68k_msc_parity_portjkl.bin";
    { std::ofstream out(path, std::ios::binary);
      out.write(reinterpret_cast<const char*>(fw.data()),
                std::streamsize(fw.size())); }

    Via6522 via;
    PgePmu pmu(via, MscMemory::kCpuHz230);
    check(pmu.loadBootRom(path.string()), "port J/K/L boot ROM loads");
    pmu.reset();
    pmu.tick(200000);
    check(pmu.mcu().ramByte(4) == 0xA5, "port J/K/L program reached the marker");
    check(pmu.mcu().ramByte(0) == 0xFF, "port J unwired inputs read $FF (MAME: $00)");
    check(pmu.mcu().ramByte(1) == 0xFF, "port K unwired inputs read $FF (MAME: $00)");
    check(pmu.mcu().ramByte(2) == 0xFF, "port L unwired inputs read $FF (MAME: $00)");
    check(pmu.mcu().ramByte(3) == 0xFF,
          "DDRJ=$D0 mixed read is $FF here, $D0 under MAME's rule");
    check(!pmu.mcu().illegal(), "STOP ($8E) is a decoded opcode, not undefined");
    check(pmu.mcu().waiting(), "STOP parks the core in the WAIT state");
    check(pmu.mcu().ramByte(5) == 0x00, "STOP did not fall through");
    std::filesystem::remove(path);
}

// ── F. PG&E NVRAM persistence round trip (MscMemory::load/savePram) ─────
// The Duo's PRAM is the PG&E's internal RAM + SRAM; the battery file is
// MAME's layout (m68hc05pge.cpp:966-974: $3C0 internal RAM then $8000
// SRAM) plus POM68K's 4-byte big-endian RTC-seconds tail (the Egret
// battery-file convention). What this pins:
//   - the bytes survive a save → mutate → load cycle, at both ends of
//     both ranges;
//   - the power flag at MCU $91 reads 0 AFTER a load even though it was
//     non-zero when saved (m68hc05pge.cpp:959 "clear power flag so the
//     boot ROM does a cold boot") — and the scrub is on LOAD, not save:
//     the saved file still carries the non-zero byte;
//   - a missing or short file returns false and leaves the live PG&E
//     untouched (CentrisMemory::loadPram's `b.size() < N` rule).
static void testPramRoundTrip() {
    MscMemory mem(8u << 20, MscMemory::kCpuHz230, MscMemory::kIdDuo230);
    M68hc05Pge& mcu = mem.pmu().mcu();
    const int kFlag = M68hc05Pge::kPowerFlagAddr - 0x40;   // $91 → ram_ index

    // Recognisable pattern at both ends of both ranges + the power flag.
    mcu.setRamByte(0, 0xA5);
    mcu.setRamByte(M68hc05Pge::kRamSize - 1, 0x5A);
    mcu.setRamByte(kFlag, 0xC3);                     // non-zero at save time
    mcu.setSramByte(0, 0x11);
    mcu.setSramByte(0x1234, 0x77);
    mcu.setSramByte(M68hc05Pge::kSramSize - 1, 0xEE);
    mem.setRtcSeconds(0x0BADF00D);

    const auto path = std::filesystem::temp_directory_path()
                    / "pom68k_msc_parity.pram";
    std::filesystem::remove(path);
    mem.savePram(path.string());
    const auto sz = std::filesystem::file_size(path);
    check(sz == std::uintmax_t(M68hc05Pge::kRamSize)
               + std::uintmax_t(M68hc05Pge::kSramSize) + 4,
          "PRAM file = MAME layout ($3C0 + $8000) + 4-byte seconds tail");

    // The scrub is a LOAD-side rule: the file keeps the live power flag.
    {
        std::ifstream f(path, std::ios::binary);
        f.seekg(kFlag);
        char c = 0; f.read(&c, 1);
        check(uint8_t(c) == 0xC3, "saved image keeps the power flag as-is");
    }

    // Mutate everything the load must put back.
    mcu.setRamByte(0, 0x00);
    mcu.setRamByte(M68hc05Pge::kRamSize - 1, 0x00);
    mcu.setRamByte(kFlag, 0xFF);
    mcu.setSramByte(0, 0x00);
    mcu.setSramByte(0x1234, 0x00);
    mcu.setSramByte(M68hc05Pge::kSramSize - 1, 0x00);
    mem.setRtcSeconds(1);

    check(mem.loadPram(path.string()), "loadPram accepts the saved image");
    check(mcu.ramByte(0) == 0xA5, "internal RAM first byte restored");
    check(mcu.ramByte(M68hc05Pge::kRamSize - 1) == 0x5A,
          "internal RAM last byte ($3FF) restored");
    check(mcu.sramByte(0) == 0x11, "SRAM first byte restored");
    check(mcu.sramByte(0x1234) == 0x77, "SRAM interior byte restored");
    check(mcu.sramByte(M68hc05Pge::kSramSize - 1) == 0xEE,
          "SRAM last byte ($FFFF) restored");
    check(mcu.ramByte(kFlag) == 0x00,
          "power flag $91 scrubbed to 0 on load (MAME m68hc05pge.cpp:959)");
    check(mcu.rtc() == 0x0BADF00D, "RTC seconds tail restored over the seed");

    // A MAME-written image stops at $83C0: it must still load, and leave
    // the clock alone (the tail is POM68K's optional extension).
    const auto mamePath = std::filesystem::temp_directory_path()
                        / "pom68k_msc_parity_mame.pram";
    std::filesystem::copy_file(path, mamePath,
                               std::filesystem::copy_options::overwrite_existing);
    std::filesystem::resize_file(mamePath,
                                 std::uintmax_t(M68hc05Pge::kRamSize)
                               + std::uintmax_t(M68hc05Pge::kSramSize));
    mem.setRtcSeconds(0x12345678);
    check(mem.loadPram(mamePath.string()), "tail-less (MAME) image loads");
    check(mcu.ramByte(0) == 0xA5 && mcu.sramByte(M68hc05Pge::kSramSize - 1) == 0xEE,
          "tail-less image restores both ranges");
    check(mcu.rtc() == 0x12345678, "no tail → the clock is left as it was");
    std::filesystem::remove(mamePath);
    mem.setRtcSeconds(0x0BADF00D);                   // back to the pinned value

    // Missing file → false, live state untouched.
    const auto absent = std::filesystem::temp_directory_path()
                      / "pom68k_msc_parity_absent.pram";
    std::filesystem::remove(absent);
    check(!mem.loadPram(absent.string()), "loadPram(missing) returns false");
    check(mcu.ramByte(0) == 0xA5 && mcu.sramByte(0x1234) == 0x77
          && mcu.rtc() == 0x0BADF00D,
          "loadPram(missing) left the PG&E untouched");

    // Short file → false, live state untouched (the `b.size() < N` rule).
    const auto shortPath = std::filesystem::temp_directory_path()
                         / "pom68k_msc_parity_short.pram";
    { std::ofstream out(shortPath, std::ios::binary | std::ios::trunc);
      std::vector<char> junk(100, 0x7E);
      out.write(junk.data(), std::streamsize(junk.size())); }
    check(!mem.loadPram(shortPath.string()), "loadPram(short file) returns false");
    check(mcu.ramByte(0) == 0xA5 && mcu.sramByte(0) == 0x11
          && mcu.rtc() == 0x0BADF00D,
          "loadPram(short file) left the PG&E untouched");

    std::filesystem::remove(path);
    std::filesystem::remove(shortPath);
}

int main() {
    testSoundBusy();
    testPorteReset();
    testMachineWiring();
    testPortHLatch();
    testRtcSeed();
    testPortDefaultsAndStop();
    testPramRoundTrip();
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
