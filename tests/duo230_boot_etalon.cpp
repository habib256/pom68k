// POM68K — PowerBook Duo 230 boot gate (docs/DUO_BRINGUP.md milestone 3):
// the real ECFA989B ROM on the MSC platform, PG&E power manager LLE
// (roms/pge/pge_boot.bin + the mid-boot BORG v2 firmware upload), System
// 7.5.5 → Finder on the GSC 640×400 grayscale panel. The PMU boots first
// and releases the 68030; three ADBReInits must complete over the VIA1
// shifter / /PMU_INT level path (CHANGELOG 2026-07-31 night). Soft-skips
// without the ROM, the PG&E dump or the 7.5.5 image.

#include "AssetFingerprint.h"
#include "MscCpu.h"
#include "MscMemory.h"
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
    std::string rom =
        find("roms/1MB ROMs/1992-10 - ECFA989B - Powerbook 210 & 230 & 250.ROM");
    std::string pge = find("roms/pge/pge_boot.bin");
    std::string img = find("hdv/System 7.5.5 HD.dsk");
    if (rom.empty() || pge.empty() || img.empty()) {
        std::printf("SKIP: needs the ECFA989B ROM, roms/pge/pge_boot.bin "
                    "and hdv/System 7.5.5 HD.dsk\n");
        return 0;
    }
    testasset::report({ rom, pge, img });

    std::ifstream rin(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(rin)), {});
    if (romData.size() != MscMemory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM size\n");
        return 1;
    }

    MscMemory mem(8u << 20, MscMemory::kCpuHz230, MscMemory::kIdDuo230);
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    if (!mem.pgeActive()) { std::fprintf(stderr, "FAIL: PG&E inactive\n"); return 1; }
    MscCpu cpu(mem);
    mem.setCpu(&cpu);
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk\n"); return 1; }
    cpu.hardReset();

    // The PMU boots first: run the machine until the PG&E releases the
    // 68030 (port E bit 2); cap the wait so a wedged stub fails loudly.
    long heldTicks = 0;
    while (mem.cpuHeld() && heldTicks < 400000) { mem.tick(1000); heldTicks++; }
    if (mem.cpuHeld()) {
        std::fprintf(stderr, "FAIL: PG&E never released the CPU\n");
        return 1;
    }

    // ~200 machine-seconds at 33 MHz: the Finder was measured up well
    // before 6.0 G cycles (CHANGELOG 2026-07-31 night, SCSI 3448 cmds).
    const int64_t kFrame = 33000000 / 60;
    const long kFrames = getenv("POM68K_FRAMES") ? atol(getenv("POM68K_FRAMES"))
                                                 : 12000;
    bool diag = getenv("POM68K_DIAG");
    for (long f = 0; f < kFrames && !cpu.isHalted(); f++) {
        cpu.runCycles(kFrame);
        if (diag && !(f % 600))
            std::fprintf(stderr, "[diag] f=%ld pc=$%08X SCSI=%ld gsc=$%02X\n",
                         f, cpu.getPC0(), mem.scsi().commands, mem.gscReg(4));
    }

    if (cpu.isHalted()) {
        std::fprintf(stderr, "FAIL: CPU halted\n");
        return 1;
    }

    std::vector<uint32_t> fb;
    mem.decodeScreen(fb);
    const int W = MscMemory::kScreenW;
    const int H = MscMemory::kScreenH;
    auto darkRatio = [&](int x0, int x1, int y0, int y1) {
        long dark = 0;
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++)
                if ((fb[size_t(y) * W + x] & 0xFF) < 0x80) dark++;
        return double(dark) / double(x1 - x0) / double(y1 - y0);
    };
    double menuBar = darkRatio(0, W, 2, 18);
    double desktop = darkRatio(W / 2, W, 40, H - 40);

    std::printf("Duo 230: menu bar dark %.2f, desktop %.2f, SCSI commands %ld, "
                "GSC mode %d\n",
                menuBar, desktop, mem.scsi().commands, mem.gscReg(4) & 3);

    bool ok = menuBar < 0.35 && desktop > 0.20 && desktop < 0.80
           && mem.scsi().commands > 500;
    std::printf("%s\n", ok ? "PASSED — booted to Finder" : "FAILED");
    return ok ? 0 : 1;
}
