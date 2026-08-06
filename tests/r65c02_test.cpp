// R65C02 core gate (docs/IOP_BRINGUP.md milestone 1).
//
// Runs Klaus Dormann's two functional-test images on the vendored
// `R65c02` core over a flat 64 KB RAM bus:
//
//   1. `6502_functional_test.bin` — every official base-6502 instruction,
//      addressing mode and flag behaviour. Success trap $3469.
//      SHA-256 fa12bfc761e6f9057e4cc01a665a7b800ff01ae91f598af1e39a1201d01953fd
//   2. `65C02_extended_opcodes_test.bin` — every 65C02 addition the Apple
//      PIC's core has: STZ/BRA/INA/DEA/PHX/PHY/PLX/PLY, BIT #imm/zp,X/
//      abs,X, TSB/TRB, JMP (abs,X), the (zp) modes, the full Rockwell
//      RMB/SMB/BBR/BBS set, WAI/STP, CMOS decimal-mode flags, CMOS
//      timing. Success trap $24F1.
//      SHA-256 10a2a07fa240666fa610c46accebe8d42b1000feef3aae619da15a8d152869b2
//
// Both images live in tests/assets/ (Klaus's suite is GPL test code, not
// a ROM — committable, unlike everything under roms/). Success addresses
// were derived in POM2 by walking each .lst for the unique non-`trap`
// "jmp *"; the harness convention (trap = JMP-to-self detected as two
// consecutive identical PCs) is inherited from POM2's gates too.
//
// Reference: https://github.com/Klaus2m5/6502_65C02_functional_tests

#include "R65c02.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

namespace {

constexpr size_t kImageSize = 0x10000;
constexpr uint16_t kTestEntry = 0x0400;

// Klaus runs ~96 M cycles clean; a wandering buggy core must still
// terminate before CI gives up.
constexpr long kMaxSteps = 300'000'000L;

bool runImage(const char* path, uint16_t successAddress, const char* label)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        std::fprintf(stderr, "cannot open %s\n", path);
        return false;
    }
    std::vector<uint8_t> ram((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
    if (ram.size() != kImageSize) {
        std::fprintf(stderr, "%s: unexpected image size %zu (expected %zu)\n",
                     path, ram.size(), kImageSize);
        return false;
    }

    R65c02 cpu;
    cpu.read8 = [&ram](uint16_t a) { return ram[a]; };
    cpu.write8 = [&ram](uint16_t a, uint8_t v) { ram[a] = v; };
    // Bypass the reset vector — Klaus's images trap a misfired reset;
    // the documented entry point is $0400.
    cpu.setProgramCounter(kTestEntry);

    long steps = 0;
    uint16_t lastPc = cpu.getProgramCounter();
    int stuckFor = 0;
    while (steps < kMaxSteps) {
        cpu.step();
        ++steps;
        const uint16_t pc = cpu.getProgramCounter();
        if (pc == lastPc) {
            if (++stuckFor >= 2) break;
        } else {
            stuckFor = 0;
            lastPc = pc;
        }
    }

    const uint16_t finalPc = cpu.getProgramCounter();
    std::printf("%s: ended at $%04X after %ld steps (%lld cycles)\n",
                label, finalPc, steps,
                static_cast<long long>(cpu.cycleCount()));

    if (steps >= kMaxSteps) {
        std::fprintf(stderr, "%s: TIMEOUT at $%04X\n", label, finalPc);
        return false;
    }
    if (finalPc != successAddress) {
        std::fprintf(stderr,
            "%s: FAIL — trapped at $%04X (expected $%04X). Cross-reference "
            "the .lst for the test just before this address.\n",
            label, finalPc, successAddress);
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    // Paths resolve against the source dir (the gate's WORKING_DIRECTORY),
    // overridable for by-hand runs.
    const char* base = argc > 1 ? argv[1] : "tests/assets/6502_functional_test.bin";
    const char* ext  = argc > 2 ? argv[2] : "tests/assets/65C02_extended_opcodes_test.bin";

    // $CB / $DB are the WDC WAI/STP, not MAME's 1-cycle NOPs — POM68K is
    // richer than `r65c02` on purpose (MAME-parity audit §2.11, pinned
    // 2026-08-06; the personality question is CLOSED in
    // docs/IOP_BRINGUP.md §4 — a capstone sweep finds zero $CB/$DB in
    // either shipped IOP firmware blob). The Klaus extended image below
    // already exercises them, but it reports one aggregate trap address;
    // this makes the decode itself the observable so a future parity diff
    // that demotes them to NOPs fails HERE, with a legible message.
    bool ok = true;
    {
        std::vector<uint8_t> ram(0x10000, 0xEA);   // NOP fill
        R65c02 cpu;
        cpu.read8 = [&ram](uint16_t a) { return ram[a]; };
        cpu.write8 = [&ram](uint16_t a, uint8_t v) { ram[a] = v; };
        ram[0x0200] = 0xCB;                        // WAI  — falls through
        ram[0x0201] = 0xEA;                        // NOP
        ram[0x0202] = 0xDB;                        // STP  — sticky halt
        cpu.hardReset();
        cpu.setProgramCounter(0x0200);
        cpu.step();
        if (cpu.isHalted()) {
            std::fprintf(stderr, "WAI/STP: $CB must not halt the core\n");
            ok = false;
        }
        cpu.step();                                // NOP
        cpu.step();                                // STP
        if (!cpu.isHalted()) {
            std::fprintf(stderr,
                "WAI/STP: $DB must set the STP halt latch (MAME's r65c02 "
                "decodes it as a NOP — do not import that)\n");
            ok = false;
        }
    }

    ok = runImage(base, 0x3469, "Klaus 6502 functional") && ok;
    ok = runImage(ext, 0x24F1, "Klaus 65C02 extended") && ok;

    std::printf(ok ? "OK\n" : "FAILED\n");
    return ok ? 0 : 1;
}
