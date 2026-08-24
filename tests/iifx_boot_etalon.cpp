// POM68K — Mac IIfx boot gate (docs/IOP_BRINGUP.md milestone 5): SCSI →
// Finder on platform #12 (OSS + dual IOPs), video on the slot-9 Toby
// card, ADB bit-banged by the SWIM IOP's real firmware against the
// `AdbLine` LLE devices. Menu/desktop metrics on the decoded 640×480
// framebuffer, the macii_boot_etalon pattern. Soft-skips without the IIfx
// ROM, the real Toby 342-0008-a declaration ROM, and a bootable hdv/ image
// (System ≤ 7.6 — the IIfx ROM is 32-bit dirty, so no 8.x). A synthetic
// declaration is suitable for card unit tests, not this machine oracle:
// without the real slot resource the ROM never reaches StartBoot.

#include "AssetFingerprint.h"
#include "IIfxMemory.h"
#include "IIfxCpu.h"
#include "TobyVideo.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

static std::string find(const char* rel) {
    return testasset::find(rel);
}

int main() {
    std::string rom = find("roms/512KB ROMs/1990-03 - 4147DD77 - Mac IIfx.ROM");
    std::string toby = find("roms/342-0008-a.bin");
    if (toby.empty())
        toby = find("roms/archive/macroms/Misc/Video cards/Apple Macintosh II Video Card/342-0008-a.bin");
    if (toby.empty())
        toby = find("roms/archive/macroms/68k/256k/Macintosh II/342-0008-a.bin");
    // This is the recorded IIfx reference (f=539 / SCSI 512). The 7.6
    // image may be a mutable GUI work volume, so it is only a fallback.
    std::string img = find("hdv/GISTPERSO-boot.vhd");
    if (img.empty()) img = find("hdv/MacOS-7.6-boot.vhd");
    if (img.empty()) img = find("hdv/boot.vhd");
    if (rom.empty() || toby.empty() || img.empty()) {
        std::printf("SKIP: needs the Mac IIfx ROM ($4147DD77), Toby 342-0008-a, and a bootable hdv/ image\n");
        return 0;
    }
    testasset::report({ { "rom", rom }, { "declrom", toby }, { "disk", img } });

    std::ifstream rin(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(rin)), {});
    if (romData.size() != IIfxMemory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM size\n");
        return 1;
    }

    IIfxMemory mem;
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    if (!mem.installTobyVideo(toby)) {
        std::fprintf(stderr, "FAIL: bad Toby declaration ROM\n");
        return 1;
    }
    IIfxCpu cpu(mem, /*withFpu=*/true);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk\n"); return 1; }

    // One 60.15 Hz frame at 40 MHz; metrics polled every 60 frames with
    // an early exit once the Finder shape holds.
    const int64_t kFrame = 40000000LL * 100 / 6015;
    const long kFrames = std::getenv("POM68K_FRAMES")
        ? std::atol(std::getenv("POM68K_FRAMES")) : 18000;
    const bool diag = std::getenv("POM68K_DIAG") != nullptr;

    std::vector<uint32_t> fb;
    const int W = TobyVideo::W, H = TobyVideo::H;
    auto blackRatio = [&](int x0, int x1, int y0, int y1) {
        long black = 0;
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++)
                if ((fb[size_t(y) * W + x] & 0xFF) < 0x80) black++;
        return double(black) / double(x1 - x0) / double(y1 - y0);
    };
    double menuBar = 1.0, desktop = 0.0;
    auto finderShape = [&]() {
        mem.toby()->decode(fb);
        menuBar = blackRatio(0, W, 2, 20);
        desktop = blackRatio(W / 2, W, 40, H - 40);
        // Desktop bound measured on the real Finder (2026-08-01 screenshot,
        // System 7.6 FR): this image's desktop pattern reads 0.79 black —
        // denser than the 50 % gray the sibling etalons assume. All-black
        // (unpainted = 1.00) must still fail.
        return menuBar < 0.35 && desktop > 0.20 && desktop < 0.90
            && mem.scsi().commands > 500;
    };

    long f = 0;
    bool ok = false;
    for (; f < kFrames && !cpu.isHalted(); f++) {
        cpu.runCycles(kFrame);
        if (f % 60 == 59) {
            if ((ok = finderShape()))
                break;
            if (diag && f % 600 == 599)
                std::fprintf(stderr,
                    "[diag] f=%ld pc=$%08X scsi=%ld menu=%.2f desk=%.2f\n",
                    f, cpu.getPC(), mem.scsi().commands, menuBar, desktop);
        }
    }
    if (cpu.isHalted()) { std::fprintf(stderr, "FAIL: CPU halted\n"); return 1; }
    if (!ok) ok = finderShape();

    // Debug screenshot (POM68K_IIFX_SHOT=path.ppm): eyeball the decoded
    // frame before trusting or retuning any metric threshold.
    if (const char* shot = std::getenv("POM68K_IIFX_SHOT")) {
        if (FILE* fp = std::fopen(shot, "wb")) {
            std::fprintf(fp, "P6\n%d %d\n255\n", W, H);
            for (size_t i = 0; i < fb.size(); i++) {
                const uint8_t rgb[3] = { uint8_t(fb[i] >> 16), uint8_t(fb[i] >> 8),
                                         uint8_t(fb[i]) };
                std::fwrite(rgb, 1, 3, fp);
            }
            std::fclose(fp);
        }
    }

    std::printf("IIfx: f=%ld menu bar black %.2f, desktop %.2f, SCSI commands %ld\n",
                f, menuBar, desktop, mem.scsi().commands);
    std::printf("%s\n", ok ? "PASSED — booted to Finder" : "FAILED");
    return ok ? 0 : 1;
}
