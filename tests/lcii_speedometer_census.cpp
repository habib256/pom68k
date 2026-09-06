// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Real-application benchmark harness for Speedometer 4.02 on the LC II.

#include "LciiApplicationHarness.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

using namespace lciiapp;

int main() {
    const std::string rom =
        find("roms/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM");
    std::string img = testasset::overrideImage();
    if (img.empty()) img = find("hdv/GISTPERSO-boot.vhd");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs the LC II ROM + hdv/GISTPERSO-boot.vhd "
                    "(the volume that carries Speedometer 4.02)\n");
        return 0;
    }
    testasset::report({rom, img});

    std::ifstream in(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
    V8Memory mem(pom68k::defaultCoreConfig());
    if (!mem.loadRom(romData)) {
        std::fprintf(stderr, "FAIL: bad ROM\n");
        return 1;
    }
    const jit::ResolvedConfig jitConfig = testjit::resolveFromEnvironment();
    Cpu030 cpu(mem, jitConfig, pom68k::defaultCoreConfig().cpu,
               /*withFpu=*/true);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(img)) {
        std::fprintf(stderr, "FAIL: bad disk\n");
        return 1;
    }
    ensureBootDriverType(mem.scsiDisk().image());
    while (mem.cpuHeld()) mem.tick(1000);
    gMem = &mem;
    gCpu = &cpu;

    runFrames(16000);
    for (int poll = 0; poll < 20 && !finderUp(); poll++) {
        keyHold(0x24, 150);
        runFrames(600);
    }
    const bool up = finderUp();
    std::printf("boot: Finder %s, SCSI %ld commands\n",
                up ? "up" : "NOT UP", mem.scsi().commands);
    dump("lcii_speedometer_boot.ppm");
    cpu.jit().censusPhase("boot");
    if (!up) {
        std::fprintf(stderr, "FAIL: no Finder — nothing to launch\n");
        return 1;
    }

    auto open = [&](const char* prefix, long settle, const char* phase,
                    const char* ppm) {
        typeText(prefix);
        runFrames(30);
        mem.keyEvent(0x37, true);
        runFrames(6);
        keyHold(0x1F, 60);
        mem.keyEvent(0x37, false);
        runFrames(settle);
        dump(ppm);
        cpu.jit().censusPhase(phase);
    };

    const long scsi0 = mem.scsi().commands;
    std::vector<uint32_t> beforeSession;
    screen(beforeSession);
    // This image leaves the JEUX window active over the volume root. Finder's
    // Cmd-Up opens the enclosing folder and deterministically makes GIST PERSO
    // the type-select scope; without it "logiciels" silently selects a game
    // in JEUX and the run ends in that application's Open dialog.
    mem.keyEvent(0x37, true);                  // Cmd
    runFrames(6);
    keyHold(0x3E, 30);                        // Up Arrow: enclosing folder
    mem.keyEvent(0x37, false);
    runFrames(900);
    dump("lcii_speedometer_root.ppm");
    cpu.jit().censusPhase("open-root");
    open("logiciels", 900, "open-software", "lcii_speedometer_software.ppm");
    open("speedo", 900, "open-folder", "lcii_speedometer_folder.ppm");
    open("speedometer", 2400, "launch-dialog", "lcii_speedometer_launch.ppm");
    // Speedometer asks the printer driver for Page Setup on first launch. This
    // disk image has no printer endpoint, so accepting the default opens a
    // second connection-error dialog. Escape activates Cancel and reaches the
    // benchmark without depending on a printer driver or localized geometry.
    keyHold(0x35, 30);                         // Escape: Cancel Page Setup
    runFrames(300);
    keyHold(0x24, 30);                         // dismiss Speedometer splash
    runFrames(300);
    keyHold(0x35, 30);                         // Cancel registration form
    runFrames(900);
    dump("lcii_speedometer_ready.ppm");
    cpu.jit().censusPhase("ready");

    // Discover the benchmark commands from its own menu. Steer closed-loop
    // against the classic Mac low-memory Mouse global: raw relative motion is
    // accelerated by the guest and cannot reliably target a menu coordinate.
    auto pointer = [&](int& x, int& y) {
        x = int16_t(uint16_t(mem.peek8(0x832)) << 8 | mem.peek8(0x833));
        y = int16_t(uint16_t(mem.peek8(0x830)) << 8 | mem.peek8(0x831));
    };
    auto steer = [&](int tx, int ty) {
        int px = 0, py = 0;
        for (int it = 0; it < 800; it++) {
            pointer(px, py);
            const int dx = tx - px, dy = ty - py;
            if (!dx && !dy) break;
            auto step = [](int d) {
                int s = d / 2;
                if (!s) s = d > 0 ? 1 : (d < 0 ? -1 : 0);
                return std::max(-8, std::min(8, s));
            };
            mem.mouseMove(step(dx), step(dy));
            runFrames(1);
        }
        pointer(px, py);
        if (std::abs(px - tx) > 2 || std::abs(py - ty) > 2)
            std::fprintf(stderr, "steer: wanted (%d,%d), reached (%d,%d)\n",
                         tx, ty, px, py);
    };
    auto click = [&](int tx, int ty) {
        steer(tx, ty);
        mem.mouseButton(true);
        runFrames(6);
        mem.mouseButton(false);
        runFrames(30);
    };
    steer(130, 8);                             // Tests menu
    mem.mouseButton(true);
    runFrames(60);
    dump("lcii_speedometer_tests_menu.ppm");
    mem.mouseButton(false);
    runFrames(60);

    mem.keyEvent(0x37, true);                  // Cmd-R: Performance Rating
    runFrames(6);
    keyHold(0x0F, 30);
    mem.keyEvent(0x37, false);
    runFrames(900);
    dump("lcii_speedometer_performance.ppm");

    // Performance Rating defaults to the aggregate CPU+Graphics+Disk+Math
    // score. Keep only CPU so the measured phase names one JIT workload and
    // cannot be dominated by QuickDraw or SCSI service time.
    click(194, 148);                           // Graphics off
    click(194, 170);                           // Disk off
    click(194, 190);                           // Math off
    dump("lcii_speedometer_cpu_setup.ppm");
    cpu.jit().censusPhase("cpu-test-start");

    const auto cpuStart = std::chrono::steady_clock::now();
    keyHold(0x24, 30);                         // default OK: run CPU test
    auto resultShape = [&]() {
        std::vector<uint32_t> fb;
        screen(fb);
        // The completed Performance Rating window has two black seven-segment
        // score panels flanking a light modal "The tests are done" dialog.
        // This structural check ignores its text, score and menu-bar clock.
        const double left = blackRatio(fb, 12, 150, 105, 205);
        const double centre = blackRatio(fb, 155, 360, 98, 190);
        const double right = blackRatio(fb, 365, 450, 105, 205);
        return std::array<double, 3>{left, centre, right};
    };
    // The cap was a flat 600 frames — 20 guest seconds — and the census had
    // been reporting `done=0` against it since at least 2026-09-02, which
    // makes its cpu-test phase a name for boot, launch and a PARTIAL test
    // (the 2026-09-02 (sixth) honesty note says so). Two different failures
    // hide behind one flag, so both are now observable: `POM68K_SPEEDO_FRAMES`
    // moves the cap, and the per-poll shape trace says whether the result
    // window is CONVERGING and merely slow, or whether the structural check
    // never matches at all.
    const long cpuCap = std::getenv("POM68K_SPEEDO_FRAMES")
                      ? std::strtol(std::getenv("POM68K_SPEEDO_FRAMES"), nullptr, 10)
                      : 600;
    const bool shapeTrace = std::getenv("POM68K_SPEEDO_TRACE") != nullptr;
    long cpuFrames = 0;
    std::array<double, 3> shape{};
    std::array<double, 3> firstShape{};
    bool cpuDone = false, shapeMoved = false;
    while (cpuFrames < cpuCap && !cpuDone) {
        runFrames(30);                          // poll twice per guest second
        cpuFrames += 30;
        shape = resultShape();
        if (cpuFrames == 30) firstShape = shape;
        for (int i = 0; i < 3; i++)
            if (std::fabs(shape[i] - firstShape[i]) > 0.002) shapeMoved = true;
        if (shapeTrace)
            std::printf("  [speedo] frames=%-6ld shape=%.3f/%.3f/%.3f\n",
                        cpuFrames, shape[0], shape[1], shape[2]);
        cpuDone = shape[0] > 0.25 && shape[1] < 0.25 && shape[2] > 0.25;
    }
    const double cpuWall = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - cpuStart).count();
    dump("lcii_speedometer_cpu_result.ppm");
    std::printf("cpu-test: done=%d frames=%ld wall=%.6fs "
                "shape=%.3f/%.3f/%.3f fp=%016llx screen=%016llx\n",
                cpuDone, cpuFrames, cpuWall, shape[0], shape[1], shape[2],
                (unsigned long long)bench::fingerprint(cpu),
                (unsigned long long)screenFingerprint());
    cpu.jit().censusPhase("cpu-test");
    if (!cpuDone) {
        // Two different failures used to share one `done=0`, and the
        // difference decides whether any number this census produced is
        // usable. If the sampled regions never moved at all, the guest was
        // not running a CPU benchmark: on 2026-09-06 the type-select
        // navigation landed in Prince of Persia's Read Me and this phase
        // profiled a SimpleText window for 200 guest seconds under the name
        // "cpu-test". Say which one happened, because a slow test is a
        // budget problem and a frozen screen is a lie.
        if (!shapeMoved)
            std::fprintf(stderr,
                "FAIL: the cpu-test regions never changed in %ld frames — the "
                "guest is not running Speedometer's CPU test, so this run's "
                "'cpu-test' phase names some other program. Check the "
                "navigation (POM68K_DUMP=1) before trusting any phase here.\n",
                cpuFrames);
        else
            std::fprintf(stderr,
                "FAIL: Speedometer CPU result never appeared in %ld frames "
                "(the screen did move, so this is a budget question — raise "
                "POM68K_SPEEDO_FRAMES)\n", cpuFrames);
        return 1;
    }

    keyHold(0x24, 30);                         // dismiss "tests are done"
    runFrames(300);
    mem.keyEvent(0x37, true);                  // Cmd-B: Benchmark Mix
    runFrames(6);
    keyHold(0x0B, 30);
    mem.keyEvent(0x37, false);
    runFrames(900);
    dump("lcii_speedometer_mix.ppm");

    std::vector<uint32_t> afterSession;
    screen(afterSession);
    const double moved = changed(beforeSession, afterSession);
    std::printf("session: %.1f%% of the screen changed, SCSI +%ld, "
                "fp=%016llx screen=%016llx\n",
                moved * 100.0, mem.scsi().commands - scsi0,
                (unsigned long long)bench::fingerprint(cpu),
                (unsigned long long)screenFingerprint());
    std::printf("halted=%d, SCSI %ld commands total\n",
                gCpu->isHalted(), mem.scsi().commands);
    if (moved < 0.05) {
        std::fprintf(stderr, "FAIL: Speedometer did not visibly launch\n");
        return 1;
    }
    return 0;
}
