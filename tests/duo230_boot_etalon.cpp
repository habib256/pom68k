// POM68K — PowerBook Duo 230 boot gate (docs/DUO_BRINGUP.md milestone 3):
// the real ECFA989B ROM on the MSC platform, PG&E power manager LLE
// (roms/pge/pge_boot.bin + the mid-boot BORG v2 firmware upload), System
// 7.5.5 → Finder on the GSC 640×400 grayscale panel. The PMU boots first
// and releases the 68030; three ADBReInits must complete over the VIA1
// shifter / /PMU_INT level path (CHANGELOG 2026-07-31 night). Soft-skips
// without the ROM, the PG&E dump or the 7.5.5 image.
//
// POM68K_DUO_MODEL=duo210|duo250 boots the other two profiles of the same
// board and ROM (duo210_boot_etalon, duo250_boot_etalon): the clock and the
// $5FFFFFFC CPU ID come from the catalogue's BoardIdentity.h, and every
// model must name itself in BoxFlag — the ROM's own decode of that ID
// (mac-rom Internal/Asm/InternalOnlyEqu.a:1043-1052: boxPowerBookDuo210 =
// 23, 230 = 26, 250 = 32; Gestalt machineType = BoxFlag + 6).

#include "AssetFingerprint.h"
#include "BoardIdentity.h"
#include "AgentBootProbe.h"
#include "DaynaBootProbe.h"
#include "MscCpu.h"
#include "JitTestConfig.h"
#include "PixelPin.h"
#include "InfiniteHdCompanion.h"
#include "MscMemory.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

static std::string find(const char* rel) {
    return testasset::find(rel);
}

int main() {
    struct Model {
        const char* slug;
        pom68k::SnapMachine profile;
        unsigned boxFlag;
        const char* name;
    };
    static const Model kModels[] = {
        {"duo210", pom68k::SnapMachine::Duo210, 23, "Duo 210"},
        {"duo230", pom68k::SnapMachine::Duo230, 26, "Duo 230"},
        {"duo250", pom68k::SnapMachine::Duo250, 32, "Duo 250"},
        {"duo270c", pom68k::SnapMachine::Duo270c, 71, "Duo 270c"},
    };
    const char* wanted = getenv("POM68K_DUO_MODEL");
    const Model* model = &kModels[1];
    if (wanted) {
        model = nullptr;
        for (const Model& m : kModels)
            if (!std::strcmp(m.slug, wanted)) model = &m;
        if (!model) {
            std::fprintf(stderr, "FAIL: POM68K_DUO_MODEL=%s is not a Duo "
                         "profile (duo210, duo230, duo250, duo270c)\n", wanted);
            return 1;
        }
    }
    const pom68k::MscBoardIdentity identity =
        pom68k::mscBoardIdentity(model->profile);

    std::string rom =
        find(model->profile == pom68k::SnapMachine::Duo270c
            ? "roms/1MB ROMs/1993-10 - 0024D346 - Powerbook Duo 270C.ROM"
            : "roms/1MB ROMs/1992-10 - ECFA989B - Powerbook 210 & 230 & 250.ROM");
    std::string pge = find("roms/pge/pge_boot.bin");
    std::string img = find("hdv/System 7.5.5 HD.dsk");
    if (rom.empty() || pge.empty() || img.empty()) {
        std::printf("SKIP: needs the selected Duo ROM, roms/pge/pge_boot.bin "
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

    MscMemory mem(daynaboot::config(), 8u << 20,
                  identity.cpuHz, identity.boxId);
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    if (!mem.pgeActive()) { std::fprintf(stderr, "FAIL: PG&E inactive\n"); return 1; }
    const jit::ResolvedConfig jitConfig = testjit::resolveFromEnvironment();
    MscCpu cpu(mem, jitConfig, pom68k::defaultCoreConfig().cpu, mem.hasCsc());
    mem.setCpu(&cpu);
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk\n"); return 1; }
    if (mem.hasCsc() && !infinitehd::attach(mem, img)) return 1;
    if (!agentboot::install(mem)) return 1;
    cpu.hardReset();

    // The PMU boots first: run the machine until the PG&E releases the
    // 68030 (port E bit 2); cap the wait so a wedged stub fails loudly.
    long heldTicks = 0;
    while (mem.cpuHeld() && heldTicks < 400000) { mem.tick(1000); heldTicks++; }
    if (mem.cpuHeld()) {
        std::fprintf(stderr, "FAIL: PG&E never released the CPU\n");
        return 1;
    }

    // ~200 machine-seconds: the Duo 230 had the Finder up well before
    // 6.0 G cycles (CHANGELOG 2026-07-31 night, SCSI 3448 cmds). Frames
    // are a sixtieth of the model's own clock, so the 210 gets the same
    // guest time at 25 MHz.
    const int64_t kFrame = identity.cpuHz / 60;
    const long kFrames = getenv("POM68K_FRAMES") ? atol(getenv("POM68K_FRAMES"))
                                                 : 12000;
    bool diag = getenv("POM68K_DIAG");
    for (long f = 0; f < kFrames && !cpu.isHalted(); f++) {
        cpu.runCycles(kFrame);
        if (diag && !(f % 600))
            std::fprintf(stderr, "[diag] f=%ld pc=$%08X SCSI=%ld lcd=$%02X\n",
                         f, cpu.getPC0(), mem.scsi().commands, mem.videoMode());
    }

    if (cpu.isHalted()) {
        std::fprintf(stderr, "FAIL: CPU halted\n");
        return 1;
    }

    std::vector<uint32_t> fb;
    mem.decodeScreen(fb);
    const int W = MscMemory::kScreenW;
    const int H = mem.screenHeight();
    auto darkRatio = [&](int x0, int x1, int y0, int y1) {
        long dark = 0;
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++)
                if ((fb[size_t(y) * W + x] & 0xFF) < (mem.hasCsc() ? 0xC0u : 0x80u)) dark++;
        return double(dark) / double(x1 - x0) / double(y1 - y0);
    };
    double menuBar = darkRatio(0, W, 2, 18);
    double desktop = darkRatio(W / 2, W, 40, H - 40);

    std::printf("%s: menu bar dark %.2f, desktop %.2f, SCSI commands %ld, "
                "LCD mode %d\n", model->name,
                menuBar, desktop, mem.scsi().commands, mem.videoMode() & 3);

    // …and the same screen pinned by its PIXELS (tests/PixelPin.h).
    const pixelpin::Result pin = pixelpin::settleAndHash(
        [&](std::vector<uint32_t>& out) { mem.decodeScreen(out); },
        [&](long frames) {
            for (long f = 0; f < frames && !cpu.isHalted(); f++)
                cpu.runCycles(kFrame);
        },
        [&] { return mem.scsi().commands; },
        W, H, /*menuRows=*/20);
    // The Duo 230 keeps its historical key; the variants add their slug.
    const std::string pinKey =
        model->profile == pom68k::SnapMachine::Duo230
            ? std::string("duo230_boot_etalon")
            : std::string("duo230_boot_etalon/") + model->slug;
    const bool pinOk = pixelpin::check(pinKey, pin);

    // The guest's own word for the machine: BoxFlag ($0CB3), decoded by the
    // ROM from the CPU ID register.
    const unsigned boxFlag = mem.peek8(0x0CB3);
    std::printf("BoxFlag %u (Gestalt machineType %u, want %u)\n", boxFlag,
                boxFlag + 6, model->boxFlag + 6);
    const bool identityOk = boxFlag == model->boxFlag;

    bool ok = menuBar < 0.35 && desktop > 0.20 && desktop < 0.80
           && mem.scsi().commands > 500 && pinOk && identityOk;
    ok = daynaboot::check(mem, ok);
    ok = agentboot::check(mem, cpu, kFrame, ok);
    std::printf("%s\n", ok ? "PASSED — booted to Finder" : "FAILED");
    return ok ? 0 : 1;
}
