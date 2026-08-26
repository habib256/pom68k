// POM68K — Mac IIfx input gate (docs/IOP_BRINGUP.md milestone 6).
//
// Proves the platform-#12 ADB path DELIVERS, not just that it boots: the
// chain under test is unique in the tree — the System's ADB Manager talks
// to the SWIM IOP through its mailbox, the IOP's own 65C02 firmware
// bit-bangs the wire, and `AdbLine`'s LLE keyboard+mouse answer. Nothing
// is HLE'd, so a regression anywhere in that chain shows up here.
//
// TWO observations, both chosen to survive the traps this project has
// already paid for:
//  • **Mouse → screen pixels.** MMU-independent (`pom68k-peek-is-physical-rbv`):
//    an idle Finder must be pixel-stable, and injected motion must repaint.
//    This is the observation closest to "does the cursor move?".
//  • **Keyboard → KeyMap ($0174, EXACTLY 8 bytes).** A wider window is a
//    false green (`pom68k-false-green-wide-assert`, $017D=$41). The 030's
//    PMMU IS on here (TC=$80F05750) and the read is still sound — see the
//    justification at the assertion: unlike the RBV machines, the IIfx has
//    no built-in video, and the measured 0 → 1 → 0 signature proved both
//    sensitivity and silence before the observable was trusted.
//
// Soft-skips without the IIfx ROM ($4147DD77) + a bootable System ≤ 7.6.

#include "AssetFingerprint.h"
#include "IIfxMemory.h"
#include "IIfxCpu.h"
#include "TobyVideo.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::string find(const char* rel) {
    return testasset::find(rel);
}

int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-56s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}

}  // namespace

int main() {
    std::string rom = find("roms/512KB ROMs/1990-03 - 4147DD77 - Mac IIfx.ROM");
    std::string toby = find("roms/342-0008-a.bin");
    if (toby.empty())
        toby = find("roms/archive/macroms/Misc/Video cards/Apple Macintosh II Video Card/342-0008-a.bin");
    if (toby.empty())
        toby = find("roms/archive/macroms/68k/256k/Macintosh II/342-0008-a.bin");
    std::string img = find("hdv/GISTPERSO-boot.vhd");
    if (img.empty()) img = find("hdv/MacOS-7.6-boot.vhd");
    if (img.empty()) img = find("hdv/boot.vhd");
    if (rom.empty() || toby.empty() || img.empty()) {
        std::printf("SKIP: needs the Mac IIfx ROM ($4147DD77), Toby 342-0008-a, and a bootable image\n");
        return 0;
    }
    testasset::report({ { "rom", rom }, { "declrom", toby }, { "disk", img } });

    std::ifstream rin(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(rin)), {});
    if (romData.size() != IIfxMemory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM size\n");
        return 1;
    }

    IIfxMemory mem(pom68k::defaultCoreConfig());
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    if (!mem.installTobyVideo(toby)) {
        std::fprintf(stderr, "FAIL: bad Toby declaration ROM\n");
        return 1;
    }
    IIfxCpu cpu(mem, jit::defaultResolvedConfig(), /*withFpu=*/true);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk\n"); return 1; }

    const int64_t kFrame = 40000000LL * 100 / 6015;
    const int W = TobyVideo::W, H = TobyVideo::H;
    std::vector<uint32_t> fb;
    auto snapshot = [&](std::vector<uint32_t>& out) {
        mem.toby()->decode(out);
    };
    auto desktopBlack = [&]() {
        mem.toby()->decode(fb);
        long black = 0;
        for (int y = 40; y < H - 40; y++)
            for (int x = W / 2; x < W; x++)
                if ((fb[size_t(y) * W + x] & 0xFF) < 0x80) black++;
        return double(black) / double(W - W / 2) / double(H - 80);
    };

    // Boot to the Finder (the boot etalon lands at ~1560 frames; give it
    // room, then confirm the desktop is actually painted before testing
    // input — a black screen would make every pixel diff meaningless).
    const long kBootFrames = std::getenv("POM68K_FRAMES")
        ? std::atol(std::getenv("POM68K_FRAMES")) : 2400;
    for (long f = 0; f < kBootFrames && !cpu.isHalted(); f++)
        cpu.runCycles(kFrame);
    if (cpu.isHalted()) { std::fprintf(stderr, "FAIL: CPU halted during boot\n"); return 1; }
    const double desk = desktopBlack();
    check(desk > 0.20 && desk < 0.90, "boot: the Finder desktop is painted");
    check(mem.scsi().commands > 500, "boot: the System loaded off SCSI");

    // ── Mouse: idle must be still, motion must repaint ───────────────────
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

    std::printf("  idle diff %ld px, after motion %ld px, ADB host edges %ld\n",
                idle, moved, mem.adbHostEdges());
    // The observable must demonstrate SILENCE without stimulus before its
    // signal means anything (the methodology note in TODO §1). Measured
    // 2026-08-01: idle 0 px, motion 43 px — the same class as the RBV
    // family's 46 px, so the threshold is `family_input_etalon`'s
    // `moved > idle + 20`, not an invented one.
    check(idle < 20, "mouse: an idle Finder is pixel-stable");
    check(moved > idle + 20, "mouse: injected motion repaints the cursor");
    check(mem.adbHostEdges() > 10000, "ADB: the IOP firmware is driving the wire");

    // ── Keyboard: KeyMap, exactly 8 bytes, PMMU-guarded ──────────────────
    const uint32_t tc = uint32_t(cpu.getTC());
    const bool pmmuOn = (tc & 0x80000000u) != 0;   // reported, see below
    auto keyMapBits = [&]() {
        int bits = 0;
        for (int i = 0; i < 8; i++) {          // $0174..$017B — no wider
            uint8_t v = mem.peek8(0x174 + uint32_t(i));
            for (int k = 0; k < 8; k++) if (v & (1u << k)) bits++;
        }
        return bits;
    };
    const int idleBits = keyMapBits();
    // 'A' = ADB $00 (M0110 $01 >> 1). Hold it well past any Slow-Keys
    // filter (`pom68k-81-image-slow-keys`), then release.
    for (int f = 0; f < 150 && !cpu.isHalted(); f++) {
        if (f == 0) mem.keyEvent(0x00, true);
        cpu.runCycles(kFrame);
    }
    const int heldBits = keyMapBits();
    mem.keyEvent(0x00, false);
    for (int f = 0; f < 60 && !cpu.isHalted(); f++) cpu.runCycles(kFrame);
    const int releasedBits = keyMapBits();

    std::printf("  KeyMap bits idle=%d held=%d released=%d (TC=%08X, PMMU %s)\n",
                idleBits, heldBits, releasedBits, tc, pmmuOn ? "ON" : "off");
    // The PMMU IS on here (measured TC=$80F05750) and the reads are still
    // sound — unlike the RBV machines, the IIfx has no built-in video, so
    // physical low RAM is ordinary RAM and the ROM's mapping leaves the
    // System's low globals identity-mapped. That is not assumed: the
    // measured signature is 0 → 1 → 0 bits, i.e. the observable proved
    // BOTH sensitivity and silence-without-stimulus before being trusted
    // (TODO §1 methodology). If a future System/ROM moves them, this gate
    // fails loudly with the bit counts above — which is the point.
    check(idleBits == 0, "keyboard: KeyMap is clear with no key down");
    check(heldBits > 0, "keyboard: the held key reaches KeyMap");
    check(releasedBits == 0, "keyboard: the release clears KeyMap");

    if (gFails) {
        std::printf("FAILED — %d check(s)\n", gFails);
        return 1;
    }
    std::printf("PASSED — ADB input delivered through the IOP firmware\n");
    return 0;
}
