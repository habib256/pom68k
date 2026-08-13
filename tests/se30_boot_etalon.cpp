// POM68K — Mac SE/30 boot gate: SCSI → Finder on the compact IIx.
// Same GLUE board and shared mac2fdhd ROM ($97221136) as the IIx/IIcx; what
// is new is the internal 512×342×1 video on pseudo-slot $E (Se30Video +
// se30vrom.uk6 declaration ROM) and the VIA1 PB6-gated slot-$E VBL. Machine
// ID = VIA1 PA $C1 + VIA2 PB $87 (MAME macse30 config). Soft-skips without
// the ROM, the video ROM and a bootable hdv/ image.

#include "AssetFingerprint.h"
#include "MacIIMemory.h"
#include "Se30Video.h"
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
    std::string vrom = find("roms/se30/se30vrom.uk6");
    std::string img = find("hdv/HD20SC.vhd");
    if (img.empty()) img = find("hdv/GISTPERSO-boot.vhd");
    if (img.empty()) img = find("hdv/boot.vhd");
    if (rom.empty() || vrom.empty() || img.empty()) {
        std::printf("SKIP: needs the mac2fdhd ($97221136) ROM, "
                    "roms/se30/se30vrom.uk6 and a bootable hdv/ image\n");
        return 0;
    }
    testasset::report({ rom, vrom, img });

    std::ifstream rin(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(rin)), {});
    if (romData.size() != MacIIMemory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM size\n");
        return 1;
    }

    MacIIMemory mem(0x800000, MacIIMemory::Model::SE30);
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    if (!mem.installSe30Video(vrom)) {
        std::fprintf(stderr, "FAIL: se30vrom install\n");
        return 1;
    }
    Cpu020 cpu(mem, /*withFpu=*/true, /*is030=*/true);
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

    std::vector<uint32_t> fb;
    mem.se30()->decode(fb);
    const int W = Se30Video::W;
    const int H = Se30Video::H;
    auto blackRatio = [&](int x0, int x1, int y0, int y1) {
        long black = 0;
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++)
                if ((fb[size_t(y) * W + x] & 0xFF) < 0x80) black++;
        return double(black) / double(x1 - x0) / double(y1 - y0);
    };
    double menuBar = blackRatio(0, W, 2, 18);
    double desktop = blackRatio(W / 2, W, 40, H - 40);

    std::printf("SE/30: menu bar black %.2f, desktop %.2f, SCSI commands %ld\n",
                menuBar, desktop, mem.scsi().commands);

    bool ok = menuBar < 0.35 && desktop > 0.20 && desktop < 0.70
           // > 200, lowered from 500 on 2026-08-13 (sixth). The old
           // figure was calibrated while `MacIIMemory::attachScsi`
           // mirrored the boot disk on all SEVEN SCSI IDs, so the ROM's
           // 7→0 scan and the System's probe both counted the same volume
           // several times over. With one ID, one target — the mirror
           // mounted the volume seven times on the desktop, which is what
           // corrupted the IIfx's volume in August — the same boot to the
           // same Finder issues 291-295 commands. The screen half of this
           // assertion is untouched and passes exactly as before; what
           // changed is an artefact that was never boot progress.
           && mem.scsi().commands > 200;
    std::printf("%s\n", ok ? "PASSED — booted to Finder" : "FAILED");
    return ok ? 0 : 1;
}
