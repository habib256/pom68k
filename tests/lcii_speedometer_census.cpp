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
    // This image auto-opens several overlapping Finder windows at boot. A
    // Cmd-Up used to try to make GIST PERSO the type-select scope, but an
    // already-open sibling can remain frontmost: the 2026-09-06 run selected
    // Prince of Persia in JEUX and profiled its Read Me as "cpu-test".
    //
    // Reset the scope instead of inferring the window stack. Cmd-Option-W
    // closes every Finder window, then type-selecting the volume icon on the
    // desktop and Cmd-O establishes GIST PERSO as the one known root. ADB
    // codes are physical and this volume uses a French layout, where W is
    // code $06 rather than QWERTY's $0D; send both while the chord is held.
    // The non-W key is Z on either layout, and Cmd-Option-Z is harmless in
    // the Finder. This is the same guest-level reset used by the AIO gates on
    // this exact volume.
    for (uint8_t w : {uint8_t(0x06), uint8_t(0x0D)}) {
        mem.keyEvent(0x37, true);              // Cmd
        runFrames(12);
        mem.keyEvent(0x3A, true);              // Option
        runFrames(12);
        keyHold(w, 75);                        // W (AZERTY, then QWERTY)
        mem.keyEvent(0x3A, false);
        mem.keyEvent(0x37, false);
        runFrames(300);
    }
    dump("lcii_speedometer_desktop.ppm");
    open("gist", 900, "open-root", "lcii_speedometer_root.ppm");
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
    auto command = [&](uint8_t shortcut, long settle) {
        mem.keyEvent(0x37, true);              // Cmd
        runFrames(6);
        keyHold(shortcut, 30);
        mem.keyEvent(0x37, false);
        runFrames(settle);
    };
    steer(130, 8);                             // Tests menu
    mem.mouseButton(true);
    runFrames(60);
    dump("lcii_speedometer_tests_menu.ppm");
    mem.mouseButton(false);
    runFrames(60);

    // Speedometer is a suite, not just the aggregate CPU rating. Keep a
    // dump-only discovery pass for the other three test-family dialogs so a
    // new phase can be automated from the guest's actual controls instead of
    // guessed screen coordinates. Each dialog is cancelled before the next;
    // normal census runs do not pay for or observe this branch.
    if (std::getenv("POM68K_SPEEDO_DISCOVER")) {
        auto inspectDialog = [&](uint8_t shortcut, const char* ppm) {
            command(shortcut, 300);
            dump(ppm);
            keyHold(0x35, 30);                 // Escape: Cancel
            runFrames(120);
        };
        inspectDialog(0x0B, "lcii_speedometer_benchmark_mix.ppm"); // Cmd-B
        inspectDialog(0x03, "lcii_speedometer_fpu.ppm");           // Cmd-F
        inspectDialog(0x05, "lcii_speedometer_graphics.ppm");      // Cmd-G
    }

    const std::string mode = std::getenv("POM68K_SPEEDO_MODE")
                           ? std::getenv("POM68K_SPEEDO_MODE") : "cpu";
    uint8_t shortcut = 0;
    if (mode == "cpu") shortcut = 0x0F;        // Cmd-R: Performance Rating
    else if (mode == "mix") shortcut = 0x0B;   // Cmd-B: Benchmark Mix
    else if (mode == "fpu") shortcut = 0x03;   // Cmd-F: FPU Benchmarks
    else if (mode == "graphics") shortcut = 0x05; // Cmd-G: Color QuickDraw
    else {
        std::fprintf(stderr,
                     "FAIL: unknown POM68K_SPEEDO_MODE '%s' "
                     "(want cpu, mix, fpu or graphics)\n", mode.c_str());
        return 1;
    }
    command(shortcut, 900);

    if (mode == "cpu") {
        dump("lcii_speedometer_performance.ppm");
        // Performance Rating defaults to CPU+Graphics+Disk+Math. Keep only
        // CPU so this phase cannot be dominated by QuickDraw or SCSI.
        click(194, 148);                       // Graphics off
        click(194, 170);                       // Disk off
        click(194, 190);                       // Math off
    } else if (mode == "graphics") {
        // The Color QuickDraw dialog defaults to Monochrome only even on the
        // LC II's 8-bit display. Its other tests create their own 2/4/8/16-bit
        // drawing worlds, so select every advertised depth: a phase called
        // "graphics" must not silently mean just the first of five rows.
        click(183, 145);                       // 2 bits/pixel on
        click(183, 166);                       // 4 bits/pixel on
        click(183, 187);                       // 8 bits/pixel on
        click(183, 208);                       // 16 bits/pixel on
    }
    const std::string setupPpm = "lcii_speedometer_" + mode + "_setup.ppm";
    dump(setupPpm.c_str());
    const std::string phaseStart = mode + "-test-start";
    cpu.jit().censusPhase(phaseStart.c_str());

    const auto testStart = std::chrono::steady_clock::now();
    keyHold(0x24, 30);                         // default OK / Run Set
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
    // The historical CPU cap was a flat 600 frames — 20 guest seconds — and
    // the census had been reporting `done=0` against it since at least
    // 2026-09-02, which made its cpu-test phase a name for boot, launch and a PARTIAL test
    // (the 2026-09-02 (sixth) honesty note says so). Two different failures
    // hide behind one flag, so both are now observable: `POM68K_SPEEDO_FRAMES`
    // moves the cap, and the per-poll shape trace says whether the result
    // window is CONVERGING and merely slow, or whether the structural check
    // never matches at all.
    const long testCap = std::getenv("POM68K_SPEEDO_FRAMES")
                       ? std::strtol(std::getenv("POM68K_SPEEDO_FRAMES"), nullptr, 10)
                       : (mode == "cpu" ? 600 : 6000);
    const bool shapeTrace = std::getenv("POM68K_SPEEDO_TRACE") != nullptr;
    long testFrames = 0;
    std::array<double, 3> shape{};
    std::array<double, 3> firstShape{};
    bool testDone = false, shapeMoved = false;
    while (testFrames < testCap && !testDone) {
        runFrames(30);                          // poll twice per guest second
        testFrames += 30;
        if (mode == "cpu") {
            shape = resultShape();
            testDone = shape[0] > 0.25 && shape[1] < 0.25 && shape[2] > 0.25;
        } else {
            std::vector<uint32_t> fb;
            screen(fb);
            // Every Speedometer family ends with the same centred
            // "The tests are done!" alert. A black-density detector is not
            // enough: Color QuickDraw's diagonal crossed the old OK-button
            // rectangle at frame 60 and manufactured a false completion.
            // Hash the monochrome mask of the alert's opaque icon/text area
            // instead. It is byte-identical in the observed CPU and FPU
            // results and excludes the family-specific result window behind
            // it. The ratios remain in the trace only as drift diagnostics.
            uint64_t alert = 1469598103934665603ull;
            for (int y = 90; y < 140; y++) {
                for (int x = 205; x < 350; x++) {
                    const size_t i = size_t(y) * 512 + x;
                    alert ^= i < fb.size() && (fb[i] & 0xFF) < 0x80;
                    alert *= 1099511628211ull;
                }
            }
            shape = {blackRatio(fb, 205, 350, 90, 140),
                     blackRatio(fb, 154, 360, 82, 188),
                     alert == 0x664dbcad34d11c29ull ? 1.0 : 0.0};
            testDone = shape[2] == 1.0;
        }
        if (testFrames == 30) firstShape = shape;
        for (int i = 0; i < 3; i++)
            if (std::fabs(shape[i] - firstShape[i]) > 0.002) shapeMoved = true;
        if (shapeTrace)
            std::printf("  [speedo:%s] frames=%-6ld shape=%.3f/%.3f/%.3f\n",
                        mode.c_str(), testFrames, shape[0], shape[1], shape[2]);
    }
    const double testWall = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - testStart).count();
    const std::string resultPpm = "lcii_speedometer_" + mode + "_result.ppm";
    dump(resultPpm.c_str());
    std::printf("speedometer-test: mode=%s done=%d frames=%ld wall=%.6fs "
                "shape=%.3f/%.3f/%.3f fp=%016llx screen=%016llx\n",
                mode.c_str(), testDone, testFrames, testWall,
                shape[0], shape[1], shape[2],
                (unsigned long long)bench::fingerprint(cpu),
                (unsigned long long)screenFingerprint());
    const std::string phaseDone = mode + "-test";
    cpu.jit().censusPhase(phaseDone.c_str());
    if (!testDone) {
        // Two different failures used to share one `done=0`, and the
        // difference decides whether any number this census produced is
        // usable. If the sampled regions never moved at all, the guest was
        // not running the selected family: on 2026-09-06 the type-select
        // navigation landed in Prince of Persia's Read Me and this phase
        // profiled a SimpleText window for 200 guest seconds under the name
        // "cpu-test". Say which one happened, because a slow test is a
        // budget problem and a frozen screen is a lie.
        if (!shapeMoved)
            std::fprintf(stderr,
                "FAIL: Speedometer '%s' regions never changed in %ld frames — "
                "the guest is not running that test family, so this phase "
                "names some other program. Check the "
                "navigation (POM68K_DUMP=1) before trusting any phase here.\n",
                mode.c_str(), testFrames);
        else
            std::fprintf(stderr,
                "FAIL: Speedometer '%s' result never appeared in %ld frames "
                "(the screen did move, so this is a budget question — raise "
                "POM68K_SPEEDO_FRAMES)\n", mode.c_str(), testFrames);
        return 1;
    }

    keyHold(0x24, 30);                         // dismiss "tests are done"
    runFrames(300);
    if (mode == "cpu") {
        command(0x0B, 900);                    // preserve historical end state
        dump("lcii_speedometer_mix.ppm");
    }

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
