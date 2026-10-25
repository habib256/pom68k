// POM68K — Mac II family with a Display Card 8•24 in slot 9 → System 7 Finder.
// The card's own 341-0868 declaration ROM and driver program the JMFB,
// CRTC, clock synthesizer and RAMDAC; nothing here sets a register. Soft-
// skips without the board ROM, the 341-0868 dump and a System 7.0/7.1 disk.
//
//   --monitor <name>   the sense code the card reports: hires (640×480,
//                      the default), rgb12 (512×384), rgb16 (832×624) or
//                      rgb21 (1152×870) — DisplayCard824::monitorFromName
//   --depth 8          start in 256 colours: the slot-9 PRAM record names
//                      the card's 8-bit mode, as the Monitors control panel
//                      leaves it. The record was read back from a boot on
//                      this card: XPRAM $46 = boardID $0027, $48 = mode
//                      sRsrc ($80 one bit … $83 eight bits), $49-$4C the
//                      driver's own bytes. A record the driver rejects is
//                      rewritten to $80, which the depth check would see.
//   --iicx             the 68030 IIcx on the II FDHD/IIx/IIcx ROM instead
//                      of the 68020 Mac II on its own
//
// The verdict is the Finder signature (FinderSignature.h) on the card's
// geometry and depth, the settled screen pinned by its pixels (PixelPin.h)
// under `macii_824_etalon/<variant>`, and a cursor that follows the mouse.

#include "AssetFingerprint.h"
#include "Cpu020.h"
#include "DisplayCard824.h"
#include "FinderSignature.h"
#include "MacIIMemory.h"
#include "PixelPin.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    std::string monitorName = "hires";
    int depth = 1;
    bool iicx = false;
    for (int i = 1; i < argc; i++) {
        const std::string opt = argv[i];
        if (opt == "--iicx") { iicx = true; continue; }
        if (opt == "--monitor" && i + 1 < argc) { monitorName = argv[++i]; continue; }
        if (opt == "--depth" && i + 1 < argc) { depth = std::atoi(argv[++i]); continue; }
        std::fprintf(stderr, "usage: %s [--monitor name] [--depth 1|8] [--iicx]\n", argv[0]);
        return 2;
    }
    uint8_t monitor = 0;
    if (!DisplayCard824::monitorFromName(monitorName, monitor) || (depth != 1 && depth != 8)) {
        std::fprintf(stderr, "FAIL: unknown monitor '%s' or depth %d\n",
                     monitorName.c_str(), depth);
        return 2;
    }

    const std::string rom = testasset::find(
        iicx ? "roms/256KB ROMs/1988-09 - 97221136 - Mac II FDHD & IIx & IIcx.ROM"
             : "roms/256KB ROMs/1987-12 - 9779D2C4 - MacII (800k v2).ROM");
    std::string decl;
    for (const char* rel : DisplayCard824::kDeclRomCandidates)
        if (decl.empty()) decl = testasset::find(rel);
    std::string img = testasset::find("hdv/System 7.0 HD.dsk");
    if (img.empty()) img = testasset::find("hdv/System 7.1 HD.dsk");
    if (rom.empty() || decl.empty() || img.empty()) {
        std::printf("SKIP: needs the %s ROM, the 8•24 341-0868 dump and "
                    "hdv/System 7.0|7.1 HD.dsk\n", iicx ? "IIcx" : "Mac II");
        return 0;
    }
    testasset::report({ { "rom", rom }, { "declrom", decl }, { "disk", img } });

    std::ifstream rin(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(rin)), {});
    MacIIMemory mem(pom68k::defaultCoreConfig(), 0x800000,
                    iicx ? MacIIMemory::Model::IIcx : MacIIMemory::Model::MacII);
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    if (!mem.installDisplayCard824(decl, monitor)) {
        std::fprintf(stderr, "FAIL: 8•24 declaration ROM refused\n");
        return 1;
    }
    Cpu020 cpu(mem, jit::defaultResolvedConfig(),
               pom68k::defaultCoreConfig().cpu, /*withFpu=*/true, /*is030=*/iicx);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (depth == 8) {
        const uint8_t record[] = {0x00, 0x27, 0x83, 0xA6, 0xA6, 0x00, 0xFF};
        for (uint8_t i = 0; i < sizeof record; i++)
            mem.rtc().setXpram(uint8_t(0x46 + i), record[i]);
    }
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk\n"); return 1; }

    // macii_sys7_boot_etalon's driver: the AppleTalk CautionAlerts are
    // dismissed with real ADB Return keystrokes when a modal sits on a
    // stalled boot; the machine itself never touches guest state.
    auto p32 = [&](uint32_t a) {
        return uint32_t(mem.peek8(a)) << 24 | uint32_t(mem.peek8(a + 1)) << 16
             | uint32_t(mem.peek8(a + 2)) << 8 | mem.peek8(a + 3);
    };
    const int64_t kFrame = MacIIMemory::kCpuHz / 60;
    const long kFrames = 25000;
    long stall = 0, lastCmds = -1;
    int posts = 0, cool = 0, keyUpIn = 0;
    for (long f = 0; f < kFrames && !cpu.isHalted(); f++) {
        cpu.runCycles(kFrame);
        if (keyUpIn && !--keyUpIn) mem.keyEvent(0x24, false);
        if (cool > 0) { cool--; continue; }
        const long cmds = mem.scsi().commands;
        stall = (cmds == lastCmds && cmds > 200) ? stall + 1 : 0;
        lastCmds = cmds;
        const bool modal = (p32(0xA64) & 0x80000000u) != 0;     // CurActivate
        if (modal && stall >= 45 && posts < 6) {
            mem.keyEvent(0x24, true);
            keyUpIn = 6;
            posts++; cool = 90; stall = 0;
        }
    }
    if (cpu.isHalted()) { std::fprintf(stderr, "FAIL: CPU halted\n"); return 1; }

    DisplayCard824* card = mem.card824();
    std::vector<uint32_t> fb;
    card->decode(fb);
    const int W = card->width(), H = card->height();
    const int menuRun = findersig::menuBarRun(fb, W, H);
    const std::string app = findersig::curApName(mem);
    std::printf("8•24 on the %s, %s monitor: %dx%d %d bpp, scanning %d, VBL %s; "
                "menu run %d, CurApName \"%s\", SCSI commands %ld\n",
                iicx ? "IIcx" : "Mac II", monitorName.c_str(), W, H,
                card->bitsPerPixel(), card->scanning() ? 1 : 0,
                card->vblEnabled() ? "on" : "off", menuRun, app.c_str(),
                mem.scsi().commands);

    const std::string variant = (iicx ? "iicx-" : "") + monitorName +
                                (depth == 8 ? "-8bit" : "");
    const pixelpin::Result pin = pixelpin::settleAndHash(
        [&](std::vector<uint32_t>& out) { card->decode(out); },
        [&](long frames) {
            for (long f = 0; f < frames && !cpu.isHalted(); f++)
                cpu.runCycles(kFrame);
        },
        [&] { return mem.scsi().commands; },
        W, H, /*menuRows=*/20);
    const bool pinOk = pixelpin::check("macii_824_etalon/" + variant, pin);

    // The cursor rides the main screen's slot VBL (VIA2 CA1 → jCrsrTask,
    // macii_mouse_trace): ADB motion must reach RawMouse ($82C). On System 7
    // that needs the rewritten slot-interrupt tables MacIIMemory reads, and
    // on this card the edge a held VBL line was denied before its handler
    // existed (MacIIMemory::tick).
    auto rd16 = [&](uint32_t at) { return uint16_t(mem.peek8(at) << 8 | mem.peek8(at + 1)); };
    const uint16_t mouseX = rd16(0x82E), mouseY = rd16(0x82C);
    for (int f = 0; f < 120; ++f) { mem.mouseMove(3, 2); cpu.runCycles(kFrame); }
    const bool mouseOk = rd16(0x82E) != mouseX && rd16(0x82C) != mouseY;
    std::printf("mouse (%u,%u) -> (%u,%u)\n", mouseX, mouseY, rd16(0x82E), rd16(0x82C));

    struct Geometry { const char* name; int w, h; };
    static constexpr Geometry kGeometry[] = {
        {"hires", 640, 480}, {"rgb12", 512, 384}, {"rgb16", 832, 624},
        {"rgb21", 1152, 870},
    };
    bool geometryOk = false;
    for (const Geometry& g : kGeometry)
        if (monitorName == g.name) geometryOk = W == g.w && H == g.h;

    const bool ok = card->configured() && geometryOk && card->scanning() &&
                    card->vblEnabled() && card->bitsPerPixel() == depth &&
                    menuRun > findersig::menuBarRunFloor(W) && app == "Finder" &&
                    pinOk && mouseOk;
    std::printf("%s\n", ok ? "PASSED — System 7 Finder on the 8•24" : "FAILED");
    return ok ? 0 : 1;
}
