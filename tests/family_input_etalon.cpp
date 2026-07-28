// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Beyond-boot input gates for the Phase C 030 families (TODO test-depth
// pass): prove the firmware-LLE ADB path actually DELIVERS mouse and
// keyboard on the Sonora / VASP / RBV machines, not just that they boot.
// One binary, one machine per test (the compact_boot_etalon pattern):
//   lc3    — Sonora, Egret 341s0851 (the LC III wire)
//   lc520  — Sonora AIO, Cuda 341s0060 + the DFAC2 I2C slave
//   iivx   — VASP, Egret 341s0851 @ 31.3 MHz
//   iisi   — RBV, Egret 344s0100 @ 20 MHz (unexercised for input so far)
// Method = q605_cudalle_mouse/key_etalon: boot System 7.5, inject deltas
// into the bit-serial AdbLine, require the low-memory mouse globals
// (Mouse $0830) to move; press a key, require a KeyMap ($0174-$0183)
// bit. The whole chain runs: AdbLine wire → MCU firmware autopoll →
// VIA1 SR → ADB Manager → drivers. Soft-skips without the assets, and
// SKIPs (not fails) when the machine fell back to the HLE (no dump) —
// the gate exists to pin the FIRMWARE path.

#include "Cpu030.h"
#include "RbvCpu.h"
#include "RbvMemory.h"
#include "SonoraCpu.h"
#include "SonoraMemory.h"
#include "VaspCpu.h"
#include "VaspMemory.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {
std::string find(const char* rel) {
    for (const std::string base : { std::string(), std::string("../") }) {
        std::string p = base + rel;
        if (std::ifstream(p, std::ios::binary)) return p;
    }
    return {};
}

// Same DDM ddType $6A fixup as the family boot etalons.
void ensureBootDriverType(std::vector<uint8_t>& img) {
    if (img.size() < 512 || img[0] != 'E' || img[1] != 'R') return;
    int count = (img[0x10] << 8) | img[0x11];
    for (int i = 0; i < count && 0x12 + i * 8 + 8 <= 512; i++) {
        int e = 0x12 + i * 8;
        if (((img[e + 6] << 8) | img[e + 7]) == 0x6A) return;
    }
    if (count >= 1 && 0x12 + count * 8 + 8 <= 512) {
        int src = 0x12, dst = 0x12 + count * 8;
        for (int k = 0; k < 8; k++) img[dst + k] = img[src + k];
        img[dst + 6] = 0x00; img[dst + 7] = 0x6A;
        img[0x10] = uint8_t((count + 1) >> 8);
        img[0x11] = uint8_t(count + 1);
    }
}

std::string sys75Image(const char* preferred) {
    std::string img = find(preferred);
    if (img.empty()) img = find("hdv/lc3-boot.vhd");
    if (img.empty()) img = find("hdv/lcii-boot.vhd");
    if (img.empty()) img = find("hdv/GISTPERSO-boot.vhd");
    if (img.empty()) img = find("hdv/boot.vhd");
    if (img.empty()) img = find("hdv/System 7.5 HD.dsk");
    return img;
}

std::vector<uint8_t> loadRomFile(const std::string& p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
}

// The machine-generic phase: boot blind, then drive the wire and watch
// the low-memory globals. Returns the exit code.
template <class M, class C>
int runInput(M& mem, C& cpu, int64_t kFrame, const char* name) {
    while (mem.cpuHeld()) mem.tick(1000);
    const long kBootFrames = 12000;          // Finder well before this on
                                             // every family boot etalon
    for (long f = 0; f < kBootFrames && !cpu.isHalted(); f++)
        cpu.runCycles(kFrame);
    if (cpu.isHalted()) { std::fprintf(stderr, "FAIL: CPU halted during boot\n"); return 1; }

    auto rd16 = [&](uint32_t a) {
        return int16_t(uint16_t(mem.peek8(a) << 8 | mem.peek8(a + 1)));
    };

    // ── Mouse: inject deltas, require Mouse ($0830 v / $0832 h) to move ──
    // RawMouse ($082C) is printed too: RawMouse moving with Mouse frozen
    // splits "ADB delivery broken" from "cursor task not running".
    int16_t startX = rd16(0x0832), startY = rd16(0x0830);
    int16_t rawX0 = rd16(0x082E), rawY0 = rd16(0x082C);
    std::printf("post-boot: RawMouse=(%d,%d) Mouse=(%d,%d) SCSI=%ld\n",
                rawX0, rawY0, startX, startY, mem.scsi().commands);
    bool moved = false;
    for (long f = 0; f < 4000 && !cpu.isHalted(); f++) {
        mem.mouseMove(3, 2);
        cpu.runCycles(kFrame);
        if (!(f % 120)) {
            moved = rd16(0x0832) != startX || rd16(0x0830) != startY;
            if (moved) break;
        }
    }
    std::printf("mouse: Raw (%d,%d)->(%d,%d); Mouse (%d,%d)->(%d,%d) %s\n",
                rawX0, rawY0, rd16(0x082E), rd16(0x082C),
                startX, startY, rd16(0x0832), rd16(0x0830),
                moved ? "MOVED" : "frozen");
    // Diagnostic (not asserted): does a CLICK reach MBState ($0172)?
    // $FF idle → $00 while pressed. Motion frozen + button live would
    // mean the driver parses reports but drops deltas; both dead means
    // the device's packets never dispatch at all.
    uint8_t mb0 = mem.peek8(0x0172);
    mem.mouseButton(true);
    uint8_t mbDown = mb0;
    for (int f = 0; f < 60 && !cpu.isHalted(); f++) {
        cpu.runCycles(kFrame);
        if (mem.peek8(0x0172) != mb0) { mbDown = mem.peek8(0x0172); break; }
    }
    mem.mouseButton(false);
    for (int f = 0; f < 30 && !cpu.isHalted(); f++) cpu.runCycles(kFrame);
    std::printf("button: MBState %02X -> %02X %s\n", mb0, mbDown,
                mbDown != mb0 ? "(click seen)" : "(dead)");

    // ── Keyboard: press 'a' (code $00), require a KeyMap bit ────────────
    bool keySeen = false;
    mem.keyEvent(0x00, true);
    for (int f = 0; f < 120 && !keySeen && !cpu.isHalted(); f++) {
        cpu.runCycles(kFrame);
        for (int i = 0; i < 16; i++)
            if (mem.peek8(0x0174 + uint32_t(i)) != 0) { keySeen = true; break; }
    }
    mem.keyEvent(0x00, false);
    for (int f = 0; f < 30 && !cpu.isHalted(); f++) cpu.runCycles(kFrame);
    std::printf("keymap: %s\n", keySeen ? "bit seen" : "EMPTY");

    bool ok = moved && keySeen && !cpu.isHalted();
    std::printf("%s — %s mouse+key on the MCU firmware wire\n",
                ok ? "PASSED" : "FAILED", name);
    return ok ? 0 : 1;
}
} // namespace

int main(int argc, char** argv) {
    const std::string which = argc > 1 ? argv[1] : "lc3";

    if (which == "lc3" || which == "lc520") {
        const bool aio = which == "lc520";
        std::string rom = aio
            ? find("roms/1MB ROMs/1993-10 - EDE66CBD - Color Classic II & LC 550 & Performa 275,550,560 & Macintosh TV.ROM")
            : find("roms/1MB ROMs/1993-02 - ECBBC41C - Mac LC III.ROM");
        if (rom.empty()) rom = find(aio ? "roms/maclc520.rom" : "roms/maclc3.rom");
        std::string img = sys75Image("hdv/lc3-boot.vhd");
        if (rom.empty() || img.empty()) { std::printf("SKIP: needs ROM + Sys 7.5 image\n"); return 0; }
        std::vector<uint8_t> romData = loadRomFile(rom);
        SonoraMemory mem(0x800000, SonoraMemory::kCpuHz,
                         aio ? SonoraMemory::kIdLc520 : SonoraMemory::kIdLc3,
                         /*cudaAdb=*/aio);
        if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
        mem.setMonitorSense(aio ? 6 : 2);
        if (!mem.egretLleActive() && !getenv("POM68K_INPUT_ANYPATH")) { std::printf("SKIP: needs the MCU dump (firmware path)\n"); return 0; }
        SonoraCpu cpu(mem, /*withFpu=*/true);
        mem.setCpu(&cpu);
        cpu.hardReset();
        if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk\n"); return 1; }
        ensureBootDriverType(mem.scsiDisk().image());
        return runInput(mem, cpu, SonoraMemory::kCpuHz / 60,
                        aio ? "LC 520 (Cuda 341s0060)" : "LC III (Egret 341s0851)");
    }
    if (which == "iivx") {
        std::string rom = find("roms/1MB ROMs/1992-10 - 4957EB49 - Mac IIvx & IIvi or Performa 600.ROM");
        if (rom.empty()) rom = find("roms/maciivx.rom");
        std::string img = sys75Image("hdv/lc3-boot.vhd");
        if (rom.empty() || img.empty()) { std::printf("SKIP: needs ROM + Sys 7.5 image\n"); return 0; }
        std::vector<uint8_t> romData = loadRomFile(rom);
        VaspMemory mem(0x800000);
        if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
        mem.setMonitorSense(6);
        if (!mem.egretLleActive() && !getenv("POM68K_INPUT_ANYPATH")) { std::printf("SKIP: needs the MCU dump (firmware path)\n"); return 0; }
        VaspCpu cpu(mem, /*withFpu=*/true);
        mem.setCpu(&cpu);
        cpu.hardReset();
        if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk\n"); return 1; }
        ensureBootDriverType(mem.scsiDisk().image());
        return runInput(mem, cpu, VaspMemory::kCpuHzVx / 60,
                        "Mac IIvx (Egret 341s0851, VASP)");
    }
    if (which == "iisi") {
        std::string rom = find("roms/512KB ROMs/1990-10 - 36B7FB6C - Mac IIsi.ROM");
        if (rom.empty()) rom = find("roms/maciisi.rom");
        std::string img = sys75Image("hdv/iisi-boot.vhd");
        if (rom.empty() || img.empty()) { std::printf("SKIP: needs ROM + Sys 7.5 image\n"); return 0; }
        std::vector<uint8_t> romData = loadRomFile(rom);
        RbvMemory mem(0x800000);
        if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
        if (!mem.egretLleActive() && !getenv("POM68K_INPUT_ANYPATH")) { std::printf("SKIP: needs the MCU dump (firmware path)\n"); return 0; }
        RbvCpu cpu(mem, /*withFpu=*/true);
        mem.setCpu(&cpu);
        cpu.hardReset();
        if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk\n"); return 1; }
        ensureBootDriverType(mem.scsiDisk().image());
        return runInput(mem, cpu, RbvMemory::kCpuHz / 60,
                        "Mac IIsi (Egret 344s0100, RBV)");
    }
    std::fprintf(stderr, "usage: %s lc3|lc520|iivx|iisi\n", argv[0]);
    return 2;
}
