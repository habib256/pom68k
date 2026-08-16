// POM68K — Mac IIx boot gate: SCSI → Finder on the 68030 Mac II GLUE board.
// The IIx is the Mac II FDHD with a 68030 (built-in PMMU + 68882) instead of
// the 020 — same GLUE 24-bit remap, same Toby NuBus video, the shared
// mac2fdhd ROM ($97221136). POM68K_IICX=1 selects the IIcx (3 fewer NuBus
// slots + VIA1 PA6 id). Soft-skips without the ROM + a bootable hdv/ image.

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
    for (const std::string base : { std::string(), std::string("../") }) {
        std::string p = base + rel;
        if (std::ifstream(p, std::ios::binary)) return p;
    }
    return {};
}

int main() {
    std::string rom = find("roms/256KB ROMs/1988-09 - 97221136 - Mac II FDHD & IIx & IIcx.ROM");
    std::string img = find("hdv/HD20SC.vhd");
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
    MacIIMemory mem(0x800000, force020 ? MacIIMemory::Model::MacII
                    : iicx ? MacIIMemory::Model::IIcx
                           : MacIIMemory::Model::IIx);
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    mem.installTobyVideo();
    Cpu020 cpu(mem, /*withFpu=*/true, /*is030=*/!force020);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk\n"); return 1; }

    const int64_t kFrame = 800 * 525;
    const long kFrames = getenv("POM68K_FRAMES") ? atol(getenv("POM68K_FRAMES"))
                                                 : 20000;
    bool diag = getenv("POM68K_DIAG");
    for (long f = 0; f < kFrames && !cpu.isHalted(); f++) {
        cpu.runCycles(kFrame);
        if (diag && (f < 400 ? !(f % 40) : !(f % 800)))
            std::fprintf(stderr, "[diag] f=%ld pc=$%08X SCSI=%ld\n",
                         f, cpu.getPC(), mem.scsi().commands);
    }

    if (cpu.isHalted()) {
        std::fprintf(stderr, "FAIL: CPU halted\n");
        return 1;
    }

    TobyVideo* tv = mem.toby();
    std::vector<uint32_t> fb;
    tv->decode(fb);
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
                "CurApName \"%s\", SCSI commands %ld\n",
                iicx ? "IIcx" : "IIx", menuBar, desktop, menuRun,
                app.c_str(), mem.scsi().commands);

    // The SCSI floor this replaces was a fixture reading, not boot
    // progress: HD20SC issues 295 commands while its `drVolAtrb` bit 8 is
    // clear and 178 once it is set, for the identical boot to the
    // identical desktop. `FinderSignature.h` carries the whole story.
    bool ok = menuBar < 0.35 && desktop > 0.20 && desktop < 0.70
           && menuRun > findersig::menuBarRunFloor(W)
           && app == "Finder";
    std::printf("%s\n", ok ? "PASSED — booted to Finder" : "FAILED");
    return ok ? 0 : 1;
}
