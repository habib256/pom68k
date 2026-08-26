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
//   iisi   — RBV, Egret 344s0100 @ 20 MHz (screen-observed, see below)
//   q900   — Eclipse tower, Egret 341s0851 @ 25 MHz (68040, Mac OS 8.1)
// Method = q605_cudalle_mouse/key_etalon: boot System 7.5, inject deltas
// into the bit-serial AdbLine and require the input to arrive. The whole
// chain runs: AdbLine wire → MCU firmware autopoll → VIA1 SR → ADB
// Manager → drivers. Soft-skips without the assets, and SKIPs (not
// fails) when the machine fell back to the HLE (no dump) — the gate
// exists to pin the FIRMWARE path.
//
// TWO ways to observe the arrival, because one does not fit every machine:
//  • **Low-memory globals** (Mouse $0830, KeyMap $0174-$017B) — precise,
//    and they prove the driver chain right down to jCrsrTask. Valid only
//    where the System's logical low memory IS the physical low memory our
//    peek8() reads.
//  • **On-screen pixels** — the only honest option on a RAM-BASED-VIDEO
//    machine (RBV: IIsi/IIci). There, physical low RAM *is* the
//    framebuffer and the ROM uses the PMMU to put the System's logical
//    low memory elsewhere, so peeking "$0830" returns screen pixels.
//    Reading globals there is how this gate spent two rounds "finding" a
//    dead ADB stack that was never dead: the $55555555 in "ADBBase" was
//    the 50%-gray desktop pattern (2026-07-29). The cursor-motion check
//    below is MMU-independent: idle frames must be identical, and
//    injected motion must change pixels.

#include "AssetFingerprint.h"
#include "Cpu030.h"
#include "RbvCpu.h"
#include "RbvMemory.h"
#include "RbvVideo.h"
#include "Q700Cpu.h"
#include "Q700Memory.h"
#include "SonoraCpu.h"
#include "SonoraMemory.h"
#include "VaspCpu.h"
#include "VaspMemory.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <fstream>
#include <string>
#include <vector>

namespace {
std::string find(const char* rel) {
    return testasset::find(rel);
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

// Boot blind, then drive the wire. `snapshot` is non-null only on
// RAM-based-video machines, where low-memory peeks read the framebuffer
// instead of the System's globals (see the header): there the mouse is
// asserted on SCREEN PIXELS instead. Returns the exit code.
// `keyHold` is the number of frames a key stays down: the 8.1 image used by
// the 040 towers ships with Slow Keys ON and rejects a tap under ~2 s, so a
// gate booting that volume has to hold longer than the 7.5 families do.
template <class M, class C>
int runInput(M& mem, C& cpu, int64_t kFrame, const char* name,
             std::function<void(std::vector<uint32_t>&)> snapshot = nullptr,
             long bootFrames = 12000, int keyHold = 120) {
    while (mem.cpuHeld()) mem.tick(1000);
    const long kBootFrames = bootFrames;     // Finder well before this on
                                             // every family boot etalon
    for (long f = 0; f < kBootFrames && !cpu.isHalted(); f++)
        cpu.runCycles(kFrame);
    if (cpu.isHalted()) { std::fprintf(stderr, "FAIL: CPU halted during boot\n"); return 1; }

    if (snapshot) {
        // Cursor-motion check: an idle Finder must be pixel-stable, and
        // injected motion must repaint the cursor. MMU-independent, so it
        // is the only sound observation on this machine class.
        std::vector<uint32_t> a, b, c;
        snapshot(a);
        for (int f = 0; f < 90 && !cpu.isHalted(); f++) cpu.runCycles(kFrame);
        snapshot(b);
        long idle = 0;
        for (size_t i = 0; i < a.size() && i < b.size(); i++) if (a[i] != b[i]) idle++;
        for (int f = 0; f < 300 && !cpu.isHalted(); f++) {
            mem.mouseMove(4, 3);
            cpu.runCycles(kFrame);
        }
        snapshot(c);
        long moved = 0;
        for (size_t i = 0; i < b.size() && i < c.size(); i++) if (b[i] != c[i]) moved++;
        std::printf("cursor: idle diff %ld px, after motion %ld px\n", idle, moved);
        bool ok = moved > idle + 20 && !cpu.isHalted();
        std::printf("%s — %s mouse on the MCU firmware wire (screen-observed)\n",
                    ok ? "PASSED" : "FAILED", name);
        return ok ? 0 : 1;
    }

    auto rd16 = [&](uint32_t a) {
        return int16_t(uint16_t(mem.peek8(a) << 8 | mem.peek8(a + 1)));
    };
    // ADBBase ($0CF8) is the ADB Manager's globals pointer — non-zero and
    // in RAM once the System has initialized ADB. Only meaningful on the
    // machines reached by this path (see the header note on RBV).
    uint32_t adbBase = uint32_t(mem.peek8(0x0CF8)) << 24
                     | uint32_t(mem.peek8(0x0CF9)) << 16
                     | uint32_t(mem.peek8(0x0CFA)) << 8 | mem.peek8(0x0CFB);
    std::printf("ADBBase=$%08X %s\n", adbBase,
                (adbBase && adbBase < 0x800000) ? "(ADB Manager up)"
                                                : "(ADB NOT INITIALIZED)");

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
    // KeyMap is EXACTLY 8 bytes ($0174-$017B). Scanning 16 reached into
    // KeypadMap and its neighbours, where an unrelated non-zero byte
    // ($017D = $41) read as a keystroke — that false positive is what
    // masked the IIsi having no working keyboard either (2026-07-29).
    bool keySeen = false;
    mem.keyEvent(0x00, true);
    for (int f = 0; f < keyHold && !keySeen && !cpu.isHalted(); f++) {
        cpu.runCycles(kFrame);
        for (int i = 0; i < 8; i++)
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
        testasset::report({ rom, img });
        std::vector<uint8_t> romData = loadRomFile(rom);
        SonoraMemory mem(pom68k::defaultCoreConfig(), 0x800000,
                         SonoraMemory::kCpuHz,
                         aio ? SonoraMemory::kIdLc520 : SonoraMemory::kIdLc3,
                         /*cudaAdb=*/aio);
        if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
        mem.setMonitorSense(aio ? 6 : 2);
        if (!mem.egretLleActive() && !getenv("POM68K_INPUT_ANYPATH")) { std::printf("SKIP: needs the MCU dump (firmware path)\n"); return 0; }
        SonoraCpu cpu(mem, jit::defaultResolvedConfig(),
                      pom68k::defaultCoreConfig().cpu, /*withFpu=*/true);
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
        VaspMemory mem(pom68k::defaultCoreConfig(), 0x800000);
        if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
        mem.setMonitorSense(6);
        if (!mem.egretLleActive() && !getenv("POM68K_INPUT_ANYPATH")) { std::printf("SKIP: needs the MCU dump (firmware path)\n"); return 0; }
        VaspCpu cpu(mem, jit::defaultResolvedConfig(),
                    pom68k::defaultCoreConfig().cpu, /*withFpu=*/true);
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
        RbvMemory mem(pom68k::defaultCoreConfig(), 0x800000);
        if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
        if (!mem.egretLleActive() && !getenv("POM68K_INPUT_ANYPATH")) { std::printf("SKIP: needs the MCU dump (firmware path)\n"); return 0; }
        RbvCpu cpu(mem, jit::defaultResolvedConfig(),
                   pom68k::defaultCoreConfig().cpu, /*withFpu=*/true);
        mem.setCpu(&cpu);
        cpu.hardReset();
        if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk\n"); return 1; }
        ensureBootDriverType(mem.scsiDisk().image());
        // RAM-based video: assert on the screen, not on low memory.
        RbvVideo video(mem);
        return runInput(mem, cpu, RbvMemory::kCpuHz / 60,
                        "Mac IIsi (Egret 344s0100, RBV)",
                        [&video](std::vector<uint32_t>& fb) { video.decode(fb); });
    }
    // Quadra 900 "Eclipse": the tower's Egret 341S0851 firmware LLE, wired
    // 2026-08-14. The Eclipse is the one board where the ADB devices could
    // hang off either transport — the Egret's own bit-serial line or the SWIM
    // IOP's bit-banged wire — and this gate is what finally asked. The answer
    // was the IOP: through the Egret ADBBase comes up and NOTHING arrives;
    // through the IOP the cursor crosses the screen. The dead Egret A/B route
    // was retired after this result. The tower had had no working input since
    // the profile landed on 2026-08-02, because it had no input gate. It boots
    // Mac OS 8.1, not the 7.5 volume the 030 families use, so it gets the
    // longer key hold (that image has Slow Keys on).
    if (which == "q900" || which == "q950") {
        const bool q950 = which == "q950";
        std::string rom = q950
            ? find("roms/1MB ROMs/1992-03 - 3DC27823 - Quadra 950.ROM")
            : find("roms/1MB ROMs/1991-10 - 420DBFF3 - Quadra 700&900 & PB140&170.ROM");
        if (rom.empty()) rom = find(q950 ? "roms/quadra950.rom" : "roms/quadra700.rom");
        std::string img = find("hdv/MacOS-8.1-boot.vhd");
        if (img.empty()) img = find("hdv/boot.vhd");
        if (img.empty()) img = find("hdv/GISTPERSO-boot.vhd");
        if (rom.empty() || img.empty()) { std::printf("SKIP: needs the 1 MB ROM + a bootable image\n"); return 0; }
        testasset::report({ rom, img });
        std::vector<uint8_t> romData = loadRomFile(rom);
        const int64_t hz = q950 ? Q700Memory::kCpuHzQ950 : Q700Memory::kCpuHz;
        Q700Memory mem(pom68k::defaultCoreConfig(), 32u << 20, hz,
                       q950 ? Q700Memory::Model::Q950 : Q700Memory::Model::Q900);
        if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
        if (!mem.egretLleActive() && !getenv("POM68K_INPUT_ANYPATH")) { std::printf("SKIP: needs roms/egret/341s0851.bin (firmware path)\n"); return 0; }
        Q700Cpu cpu(mem, jit::defaultResolvedConfig(),
                    pom68k::defaultCoreConfig().cpu);
        mem.setCpu(&cpu);
        cpu.hardReset();
        if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk\n"); return 1; }
        ensureBootDriverType(mem.scsiDisk().image());
        const int rc = runInput(mem, cpu, hz / 60,
                                q950 ? "Quadra 950 (Egret 341s0851, Eclipse)"
                                     : "Quadra 900 (Egret 341s0851, Eclipse)",
                                nullptr, /*bootFrames=*/16000, /*keyHold=*/240);
        // Which transport did the ROM actually drive? `adbHostEdges` counts
        // the SWIM IOP's gpout0 transitions on its own wire; the Egret's MCU
        // instruction count says its firmware ran. Both are printed because
        // the answer decides where input has to be injected on this board.
        std::printf("transport: IOP gpout0 edges=%ld, Egret MCU instr=%ld\n",
                    mem.adbHostEdges(), mem.egretLle().mcu().instructions);
        return rc;
    }
    std::fprintf(stderr, "usage: %s lc3|lc520|iivx|iisi|q900|q950\n", argv[0]);
    return 2;
}
