// POM68K — Mac IIx boot gate: SCSI → Finder on the 68030 Mac II GLUE board.
// The IIx is the Mac II FDHD with a 68030 (built-in PMMU + 68882) instead of
// the 020 — same GLUE 24-bit remap, same Toby NuBus video, the shared
// mac2fdhd ROM ($97221136). POM68K_IICX=1 selects the IIcx (3 fewer NuBus
// slots + VIA1 PA6 id). Soft-skips without the ROM + a bootable hdv/ image.

#include "AgentBootProbe.h"
#include "AssetFingerprint.h"
#include "FinderSignature.h"
#include "MacIIMemory.h"
#include "TobyVideo.h"
#include "Cpu020.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

static std::string find(const char* rel) {
    return testasset::find(rel);
}

int main() {
    std::string rom = find("roms/256KB ROMs/1988-09 - 97221136 - Mac II FDHD & IIx & IIcx.ROM");
    std::string img = testasset::overrideImage();   // POM68K_BEYOND_IMG, the agent variant's
    if (img.empty()) img = find("hdv/HD20SC.vhd");
    if (img.empty()) img = find("hdv/GISTPERSO-boot.vhd");
    if (img.empty()) img = find("hdv/boot.vhd");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs the mac2fdhd ($97221136) ROM + a bootable hdv/ image\n");
        return 0;
    }
    testasset::report({ rom, img });

    std::ifstream rin(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(rin)), {});
    if (romData.size() != MacIIMemory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM size\n");
        return 1;
    }

    bool iicx = getenv("POM68K_IICX") != nullptr;
    bool force020 = getenv("POM68K_MACII_020") != nullptr;   // isolation knob
    // POM68K_TEST_TOBY_SYNTHETIC=1: boot on the synthetic declaration ROM
    // even when the dump is on hand — the way to prove the fallback itself
    // on a System 7 volume. Since 2026-09-16 the board searches the dump on
    // its own, so the refusal goes through the typed policy the GUI uses.
    const bool forceSynthetic = [] {
        const char* v = getenv("POM68K_TEST_TOBY_SYNTHETIC");
        return v && *v == '1';
    }();
    pom68k::CoreConfig core = pom68k::defaultCoreConfig();
    core.firmware.tobyDeclLle = !forceSynthetic;
    MacIIMemory mem(core, 0x800000,
                    force020 ? MacIIMemory::Model::MacII
                    : iicx ? MacIIMemory::Model::IIcx
                           : MacIIMemory::Model::IIx);
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    // The real Toby declaration ROM when the dump is on hand, the synthetic
    // one otherwise: System 7's video driver use goes past what the
    // synthetic sResource carries (measured 2026-09-15: the same System 7.0
    // volume draws a clean desktop on the Mac II with the real ROM and
    // dithered bands on this board with the synthetic one).
    std::string toby;
    for (const char* p : { "roms/archive/macroms/Misc/Video cards/Apple Macintosh II Video Card/342-0008-a.bin",
                           "tests/data/342-0008-a.bin", "roms/342-0008-a.bin" })
        if (toby.empty() && std::ifstream(p, std::ios::binary)) toby = p;
    if (forceSynthetic) toby.clear();
    if (toby.empty() && !testasset::overrideImage().empty() && !forceSynthetic) {
        // A System 7 boot on the synthetic sResource draws no desktop: the
        // agent variant is an asset gate, and says so.
        std::printf("SKIP: a System 7 boot on this board needs the Toby 342-0008-a declaration ROM\n");
        return 0;
    }
    if (!mem.installTobyVideo(toby)) { std::fprintf(stderr, "FAIL: bad Toby declaration ROM\n"); return 1; }
    std::printf("Toby declaration ROM: %s\n", toby.empty() ? "synthetic" : toby.c_str());
    Cpu020 cpu(mem, jit::defaultResolvedConfig(),
               pom68k::defaultCoreConfig().cpu,
               /*withFpu=*/true, /*is030=*/!force020);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk\n"); return 1; }
    if (!agentboot::install(mem)) return 1;

    const int64_t kFrame = 800 * 525;
    const long kFrames = getenv("POM68K_FRAMES") ? atol(getenv("POM68K_FRAMES"))
                                                 : 20000;
    bool diag = getenv("POM68K_DIAG");
    // On a System 7 image (the agent variant's POM68K_BEYOND_IMG) the boot
    // stops at the AppleTalk CautionAlerts a Sys7 volume raises on a
    // machine with no EtherTalk card — macii_sys7_boot_etalon's loop,
    // verbatim: a modal (CurActivate bit 31) on a stalled SCSI count is
    // dismissed with a real ADB Return, at most six times. System 6 never
    // trips it.
    const bool sys7 = !testasset::overrideImage().empty();
    auto p32 = [&](uint32_t a) {
        return uint32_t(mem.peek8(a)) << 24 | uint32_t(mem.peek8(a + 1)) << 16
             | uint32_t(mem.peek8(a + 2)) << 8 | mem.peek8(a + 3);
    };
    long stall = 0, lastCmds = -1;
    int posts = 0, cool = 0, keyUpIn = 0;
    for (long f = 0; f < kFrames && !cpu.isHalted(); f++) {
        cpu.runCycles(kFrame);
        if (diag && (f < 400 ? !(f % 40) : !(f % 800)))
            std::fprintf(stderr, "[diag] f=%ld pc=$%08X SCSI=%ld\n",
                         f, cpu.getPC(), mem.scsi().commands);
        if (!sys7) continue;
        if (keyUpIn && !--keyUpIn) mem.keyEvent(0x24, false);   // Return up
        if (cool > 0) { cool--; continue; }
        const long cmds = mem.scsi().commands;
        stall = (cmds == lastCmds && cmds > 200) ? stall + 1 : 0;
        lastCmds = cmds;
        const bool modal = (p32(0xA64) & 0x80000000u) != 0;     // CurActivate
        if (modal && stall >= 45 && posts < 6) {
            mem.keyEvent(0x24, true);                           // Return down
            keyUpIn = 6;                                        // ~100 ms hold
            posts++; cool = 90; stall = 0;
        }
    }

    if (cpu.isHalted()) {
        std::fprintf(stderr, "FAIL: CPU halted\n");
        return 1;
    }

    TobyVideo* tv = mem.toby();
    std::vector<uint32_t> fb;
    tv->decode(fb);
    if (const char* d = getenv("POM68K_DUMP")) {   // the screen the verdict reads
        if (std::FILE* f = std::fopen(d, "wb")) {
            std::fprintf(f, "P6\n%d %d\n255\n", tv->hres(), tv->vres());
            for (uint32_t p : fb) { unsigned char c[3] = {(unsigned char)(p >> 16), (unsigned char)(p >> 8), (unsigned char)p}; std::fwrite(c, 1, 3, f); }
            std::fclose(f);
        }
    }
    const int W = tv->hres();
    const int H = tv->vres();
    auto blackRatio = [&](int x0, int x1, int y0, int y1) {
        long black = 0;
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++)
                if (x < W && y < H && (fb[y * W + x] & 0xFF) < 0x80) black++;
        return double(black) / double(x1 - x0) / double(y1 - y0);
    };
    double menuBar = blackRatio(0, W, 2, 20);
    double desktop = blackRatio(W / 2, W, 40, H - 40);

    const int menuRun = findersig::menuBarRun(fb, W, H);
    const std::string app = findersig::curApName(mem);

    std::printf("%s: menu bar black %.2f, desktop %.2f, menu run %d, "
                "CurApName \"%s\", SCSI commands %ld — Toby mode $%02X %dx%d, "
                "VRAM writes %ld, TFB register writes %ld, VBL enable writes %ld, VBL %s\n",
                iicx ? "IIcx" : "IIx", menuBar, desktop, menuRun,
                app.c_str(), mem.scsi().commands, tv->mode(), tv->hres(), tv->vres(),
                tv->vramWrites, tv->tfbWrites, tv->vblEnableWrites,
                tv->vblDisabled() ? "disabled" : "enabled");

    // The SCSI floor this replaces was a fixture reading, not boot
    // progress: HD20SC issues 295 commands while its `drVolAtrb` bit 8 is
    // clear and 178 once it is set, for the identical boot to the
    // identical desktop. `FinderSignature.h` carries the whole story.
    if (diag) {                            // where the guest is, when it is not a Finder
        char line[128];
        uint32_t pc = cpu.getPC();
        for (int i = 0; i < 12; i++) { int n = cpu.disassemble(line, pc); std::printf("[diag] $%08X  %s\n", pc, line); pc += uint32_t(n); }
        std::printf("[diag] A7=$%08X\n", cpu.getSP());
    }
    bool ok = menuBar < 0.35 && desktop > 0.20 && desktop < 0.70
           && menuRun > findersig::menuBarRunFloor(W)
           && agentboot::finderOrAgent(app);
    std::printf("%s\n", ok ? "PASSED — booted to Finder" : "FAILED");
    ok = agentboot::check(mem, cpu, kFrame, ok);
    return ok ? 0 : 1;
}
