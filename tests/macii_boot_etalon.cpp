// POM68K — Mac II boot gate: SCSI → Finder (640×480 menu/desktop metrics).
// Soft-skips without ROM + bootable hdv/ image.

#include "AssetFingerprint.h"
#include "AgentBootProbe.h"
#include "DaynaBootProbe.h"
#include "FinderSignature.h"
#include "MacIIMemory.h"
#include "TobyVideo.h"
#include "Cpu020.h"
#include "JitTestConfig.h"
#include "PixelPin.h"
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

static std::string find(const char* rel) {
    return testasset::find(rel);
}

// The ROM runs unmodified: the Rtc's extended-XPRAM protocol lets the
// ROM cold-init its own PRAM and boot SCSI unaided (CHANGELOG 2026-07-21).

int main(int argc, char** argv) {
    const bool fdhd = argc > 1 && std::string(argv[1]) == "--fdhd";
    const bool floppy = fdhd && argc > 3 && std::string(argv[2]) == "--floppy";
    std::string rom = find("roms/256KB ROMs/1987-12 - 9779D2C4 - MacII (800k v2).ROM");
    if (rom.empty()) rom = find("roms/256KB ROMs/1987-03 - 97851DB6 - MacII (800k v1).ROM");
    if (fdhd) rom = find("roms/256KB ROMs/1988-09 - 97221136 - Mac II FDHD & IIx & IIcx.ROM");
    // Prefer System 6 (HD20SC) — original Mac II target; System 7.5 next.
    std::string img = testasset::overrideImage();   // POM68K_BEYOND_IMG, the agent variant's
    if (img.empty()) img = find("hdv/HD20SC.vhd");
    if (img.empty()) img = find("hdv/GISTPERSO-boot.vhd");
    if (img.empty()) img = find("hdv/boot.vhd");
    if (floppy) img = find(argv[3]);
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs Mac II ROM + bootable hdv/ image\n");
        return 0;
    }
    testasset::report({ rom, img });

    std::ifstream rin(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(rin)), {});
    if (romData.size() != MacIIMemory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM size\n");
        return 1;
    }

    MacIIMemory mem(daynaboot::config(), 0x800000,
                    fdhd ? MacIIMemory::Model::MacIIFDHD : MacIIMemory::Model::MacII);
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    mem.installTobyVideo();
    const jit::ResolvedConfig jitConfig = testjit::resolveFromEnvironment();
    Cpu020 cpu(mem, jitConfig, pom68k::defaultCoreConfig().cpu, true, false);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!(floppy ? mem.insertDisk(img) : mem.attachScsi(img))) {
        std::fprintf(stderr, "FAIL: bad disk\n"); return 1;
    }
    if (!agentboot::install(mem)) return 1;

    const int64_t kFrame = 800 * 525;
    const long kFrames = 20000;
    for (long f = 0; f < kFrames && !cpu.isHalted(); f++)
        cpu.runCycles(kFrame);

    if (cpu.isHalted()) {
        std::fprintf(stderr, "FAIL: CPU halted\n");
        return 1;
    }

    TobyVideo* tv = mem.toby();
    std::vector<uint32_t> fb;
    tv->decode(fb);
    const int W = tv->hres();
    const int H = tv->vres();
    if (floppy) {
        const auto& stats = mem.swim().ismStats();
        std::printf("HD boot: PC=$%08X inserted=%d track=%d ISM=%d mode=$%02X setup=$%02X "
                    "nibbles=%ld pops=%ld errors=%ld\n", cpu.getPC(),
                    mem.internalDrive().hasDisk(), mem.internalDrive().currentTrack(),
                    mem.swim().ism(), mem.swim().ismModeReg(), mem.swim().ismSetupReg(),
                    mem.internalDrive().nibblesRead, stats.dataPops, stats.errorReads);
    }
    if (const char* dump = std::getenv("POM68K_DUMP")) {
        std::ofstream out(dump, std::ios::binary);
        out << "P6\n" << W << ' ' << H << "\n255\n";
        for (uint32_t pixel : fb) {
            out.put(char(pixel >> 16)); out.put(char(pixel >> 8)); out.put(char(pixel));
        }
    }
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

    std::printf("menu bar black %.2f, desktop %.2f, menu run %d, "
                "CurApName \"%s\", SCSI commands %ld\n",
                menuBar, desktop, menuRun, app.c_str(), mem.scsi().commands);

    // Jailbars after a stalled Welcome score ~0.22/0.22, which the two
    // ratios accept — so the discriminating term is the other two, NOT a
    // floor on `scsi().commands`. That floor was a fixture reading: the
    // same boot to the same desktop issues 295 commands off HD20SC while
    // its `drVolAtrb` bit 8 is clear and 178 once it is set, and 200 sat
    // between them (and above the stall's ~235 besides). The count is
    // printed, not asserted — `FinderSignature.h` carries the whole story.
    // …and the same screen pinned by its PIXELS (tests/PixelPin.h).
    const pixelpin::Result pin = pixelpin::settleAndHash(
        [&](std::vector<uint32_t>& out) { tv->decode(out); },
        [&](long frames) {
            for (long f = 0; f < frames && !cpu.isHalted(); f++)
                cpu.runCycles(kFrame);
        },
        [&] { return floppy ? mem.internalDrive().nibblesRead : mem.scsi().commands; },
        W, H, /*menuRows=*/20);
    const bool pinOk = pixelpin::check(fdhd ? "maciifdhd_boot_etalon" : "macii_boot_etalon", pin);

    bool ok = menuBar < 0.35 && desktop > 0.20 && desktop < 0.70
           && menuRun > findersig::menuBarRunFloor(W)
           && app == "Finder" && pinOk;
    if (fdhd && ok) {
        auto rd16 = [&](uint32_t at) { return uint16_t(mem.peek8(at) << 8 | mem.peek8(at + 1)); };
        const auto x = rd16(0x82e), y = rd16(0x82c);
        mem.mouseMove(30, 20);
        for (int f = 0; f < 120; ++f) cpu.runCycles(kFrame);
        const bool moved = rd16(0x82e) != x && rd16(0x82c) != y;
        std::vector<uint8_t> keys;
        for (int i = 0; i < 8; ++i) keys.push_back(mem.peek8(0x174 + i));
        mem.keyEvent(0, true);
        for (int f = 0; f < 60; ++f) cpu.runCycles(kFrame);
        bool keyDown = false;
        for (int i = 0; i < 8; ++i) keyDown |= mem.peek8(0x174 + i) != keys[size_t(i)];
        mem.keyEvent(0, false);
        for (int f = 0; f < 60; ++f) cpu.runCycles(kFrame);
        bool released = true;
        for (int i = 0; i < 8; ++i) released &= mem.peek8(0x174 + i) == keys[size_t(i)];
        const bool medium = !floppy || (mem.internalDrive().hasDisk() &&
            mem.internalDrive().isHd() && mem.swim().ism() && mem.internalDrive().nibblesRead > 0);
        std::printf("II FDHD: ADB mouse=%d key-down=%d released=%d HD boot=%d\n",
                    moved, keyDown, released, medium);
        ok &= moved && keyDown && released && medium;
    }
    ok = daynaboot::check(mem, ok);
    ok = agentboot::check(mem, cpu, kFrame, ok);
    std::printf("%s\n", ok ? "PASSED — booted to Finder" : "FAILED");
    return ok ? 0 : 1;
}
