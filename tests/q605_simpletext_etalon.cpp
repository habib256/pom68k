// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── SimpleText as the 68040 application gate (Quadra 605, Mac OS 8.1) ──
//
// The LC II has SimCity 2000 (`lcii_simcity_etalon`); this is the same
// contract on the 68040 family, with the application every Mac OS 8.1
// volume carries. It boots `hdv/MacOS-8.1-boot.vhd`, makes the keyboard
// usable (the image ships with Slow Keys on — Q605ApplicationHarness.h),
// navigates the Finder deterministically by type-select, launches
// SimpleText, types a document, saves it and quits, asserting the guest's
// own observable at each step:
//
//   launch    CurApName samples name a running SimpleText process that was
//             not there at boot, the screen changed, SCSI read
//   progress  the typed text changes the window twice in a row (a redraw
//             would leave the second half alone), menu bar still up
//   artefact  Cmd-S wrote blocks and the catalog gained the document's
//             name — a file on the host-owned volume
//   quit      after Cmd-Q the process is gone and the Finder still runs
//
// Unless POM68K_CPU_ENGINE is set, the scenario runs TWICE in this process
// — interpreter first, then the family's default engine — and the two legs
// must agree on the architectural fingerprint, the screen, the SCSI counts
// and every observable. POM68K_DUMP=1 writes q605_simpletext_*.ppm.

#include "Q605ApplicationHarness.h"

using namespace q605app;

namespace {

struct Leg {
    std::string engine;
    bool finder = false, fastKeys = false, launched = false, progressed = false;
    bool saved = false, quit = false, halted = true, menuUp = false;
    std::string procsBoot, procsLaunch, procsQuit;
    double launchMoved = 0.0;
    long scsiBoot = 0, scsiLaunch = 0;
    long docBefore = 0, docAfter = 0, writeBlocks = 0;
    uint64_t text0 = 0, text1 = 0, text2 = 0;
    uint64_t fp = 0, screenFp = 0;
    bool ok() const {
        return finder && fastKeys && launched && progressed && saved && quit &&
               !halted && menuUp;
    }
};

// Letters and spaces only: the image types on an AZERTY layout, where digits
// are shifted (Q605ApplicationHarness.h).
constexpr const char* kDocument = "pom proof of quadra";

bool runLeg(const std::vector<uint8_t>& rom, const std::string& img,
            const jit::ResolvedConfig& jitConfig, const char* label, Leg& leg) {
    Q605Memory mem(pom68k::defaultCoreConfig(), 32u << 20);
    if (!mem.loadRom(rom) || !mem.attachScsi(img)) {
        std::fprintf(stderr, "FAIL: could not load ROM/disk\n");
        return false;
    }
    Cpu040 cpu(mem, jitConfig, pom68k::defaultCoreConfig().cpu,
               pom68k::defaultCoreConfig().diagnostics);
    mem.setCpu(&cpu);
    cpu.hardReset();
    gMem = &mem;
    gCpu = &cpu;
    leg.engine = jitConfig.engineForGuest(true) == jit::EngineKind::Jit
                     ? cpu.jit().backendName() : "interp";
    const std::string ppm = std::string("q605_simpletext_") + label + "_";
    auto dumpPhase = [&](const char* phase) { dump((ppm + phase + ".ppm").c_str()); };

    // ── boot ─────────────────────────────────────────────────────────────
    leg.finder = bootToFinder(12000);
    leg.scsiBoot = mem.scsi().commands;
    dumpPhase("boot");
    const auto bootProcs = runningProcesses();
    leg.procsBoot = describe(bootProcs);
    std::printf("[%s] boot: Finder %s, SCSI %ld, processes %s\n", label,
                leg.finder ? "up" : "NOT UP", leg.scsiBoot, leg.procsBoot.c_str());
    if (!leg.finder || cpu.isHalted() || !processRuns(bootProcs, "Finder") ||
        processRuns(bootProcs, "SimpleText"))
        return false;

    // ── keyboard: the guest must accept ordinary taps ────────────────────
    leg.fastKeys = ensureFastKeys();
    std::printf("[%s] keys: short taps %s; \"applic\" types as %s\n", label,
                leg.fastKeys ? "accepted" : "REJECTED", describeTyping("applic").c_str());
    if (!leg.fastKeys) return false;

    // ── launch: volume → Applications → SimpleText ───────────────────────
    std::vector<uint32_t> beforeLaunch = decodeScreen().pixels;
    closeAllFinderWindows();
    dumpPhase("desktop");
    openByClick(592, 50, 600);                  // the volume "Mac-8.1-US" (top right)
    dumpPhase("root");
    openBySelect("applic", 600);                // Applications
    dumpPhase("apps");
    openBySelect("simplet", 1500);              // SimpleText → untitled window
    leg.launchMoved = changed(beforeLaunch, decodeScreen().pixels);
    leg.scsiLaunch = mem.scsi().commands;
    const auto launchProcs = runningProcesses();
    leg.procsLaunch = describe(launchProcs);
    leg.launched = processRuns(launchProcs, "SimpleText") && leg.launchMoved > 0.05 &&
                   leg.scsiLaunch > leg.scsiBoot;
    dumpPhase("launch");
    std::printf("[%s] launch: processes %s, %.1f%% of the screen changed, "
                "SCSI +%ld\n", label, leg.procsLaunch.c_str(), leg.launchMoved * 100.0,
                leg.scsiLaunch - leg.scsiBoot);
    if (!leg.launched) return false;

    // ── progress: two rounds of typing, each visible ─────────────────────
    leg.text0 = regionMaskFingerprint(0, 640, 20, 480);
    typeText("pom proves the quadra runs simpletext ");
    runFrames(60);
    leg.text1 = regionMaskFingerprint(0, 640, 20, 480);
    typeText("and the interpreter and the jit type the same page");
    runFrames(60);
    leg.text2 = regionMaskFingerprint(0, 640, 20, 480);
    leg.progressed = leg.text1 != leg.text0 && leg.text2 != leg.text1 &&
                     menuBarUp(decodeScreen());
    dumpPhase("typed");
    std::printf("[%s] progress: window %016llx -> %016llx -> %016llx (%s)\n",
                label, (unsigned long long)leg.text0, (unsigned long long)leg.text1,
                (unsigned long long)leg.text2, leg.progressed ? "typed" : "STATIC");

    // ── artefact: Cmd-S, name the document, Return ───────────────────────
    std::vector<uint8_t>& disk = mem.scsiDisk().image();
    leg.docBefore = catalogCount(disk, kDocument);
    const long writes0 = mem.scsiDisk().writeBlocks;
    command(adbFor('s'), 300);                  // Save… (Standard File)
    dumpPhase("savedialog");
    typeText(kDocument);                        // replaces the selected "untitled"
    runFrames(30);
    // A TAP, not a hold: the first Return takes the Save button, and a held
    // key auto-repeats into the document behind the dialog — the first full
    // run left a dirty document and Cmd-Q answered with "Save changes?"
    // (2026-09-08).
    keyHold(0x24, 3);                           // Return — Save
    runFrames(900);                             // write + catalog flush
    leg.docAfter = catalogCount(disk, kDocument);
    leg.writeBlocks = mem.scsiDisk().writeBlocks - writes0;
    leg.saved = leg.docAfter > leg.docBefore && leg.writeBlocks > 0;
    dumpPhase("saved");
    std::printf("[%s] artefact: '%s' x%ld -> x%ld in the catalog, %ld blocks "
                "written\n", label, kDocument, leg.docBefore, leg.docAfter,
                leg.writeBlocks);

    // ── quit ─────────────────────────────────────────────────────────────
    command(adbFor('q'), 600);                  // Quit
    const auto quitProcs = runningProcesses();
    leg.procsQuit = describe(quitProcs);
    leg.quit = !processRuns(quitProcs, "SimpleText") && processRuns(quitProcs, "Finder");
    dumpPhase("quit");

    Screen end = decodeScreen();
    leg.menuUp = menuBarUp(end);
    leg.halted = cpu.isHalted();
    leg.fp = bench::fingerprint(cpu);
    leg.screenFp = screenFingerprint();
    std::printf("[%s] end: engine=%s processes %s halted=%d menu=%s fp=%016llx "
                "screen=%016llx SCSI %ld\n", label, leg.engine.c_str(),
                leg.procsQuit.c_str(), leg.halted, leg.menuUp ? "up" : "GONE",
                (unsigned long long)leg.fp, (unsigned long long)leg.screenFp,
                mem.scsi().commands);
    return leg.ok();
}

}  // namespace

int main() {
    const std::string rom = find(
        "roms/1MB ROMs/1993-10 - FF7439EE - LC475,575,Quadra 605,Performa 475,476,575,577,578.ROM");
    std::string img = testasset::overrideImage();
    if (img.empty()) img = find("hdv/MacOS-8.1-boot.vhd");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs the FF7439EE ROM + hdv/MacOS-8.1-boot.vhd\n");
        return 0;
    }
    testasset::report({rom, img});
    std::ifstream in(rom, std::ios::binary);
    const std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                       std::istreambuf_iterator<char>());
    if (romData.size() != Q605Memory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM is %zu bytes, want 1 MB\n", romData.size());
        return 1;
    }

    const jit::ResolvedConfig chosen = testjit::resolveFromEnvironment();
    std::vector<std::pair<const char*, jit::ResolvedConfig>> legs;
    if (chosen.engineExplicit) {
        legs.emplace_back("engine", chosen);
    } else {
        jit::ResolvedConfig interp = chosen;
        interp.engineExplicit = true;
        interp.engine = jit::EngineKind::Interp;
        legs.emplace_back("interp", interp);
        legs.emplace_back("default", chosen);
    }

    std::vector<Leg> results;
    bool ok = true;
    for (const auto& [label, config] : legs) {
        Leg leg;
        const bool passed = runLeg(romData, img, config, label, leg);
        results.push_back(leg);
        if (!passed) {
            std::fprintf(stderr, "FAIL: leg '%s' — finder=%d keys=%d launched=%d "
                         "progressed=%d saved=%d quit=%d halted=%d menu=%d\n",
                         label, leg.finder, leg.fastKeys, leg.launched,
                         leg.progressed, leg.saved, leg.quit, leg.halted, leg.menuUp);
            ok = false;
        }
    }
    if (ok && results.size() == 2) {
        const Leg& a = results[0];
        const Leg& b = results[1];
        const bool same = a.fp == b.fp && a.screenFp == b.screenFp &&
            a.scsiBoot == b.scsiBoot && a.scsiLaunch == b.scsiLaunch &&
            a.text1 == b.text1 && a.text2 == b.text2 &&
            a.docAfter == b.docAfter && a.writeBlocks == b.writeBlocks;
        std::printf("identity: %s vs %s — %s\n", a.engine.c_str(), b.engine.c_str(),
                    same ? "IDENTICAL" : "DIVERGENT");
        if (!same) {
            std::fprintf(stderr, "FAIL: the interpreter and the %s engine disagree "
                         "on the SimpleText session\n", b.engine.c_str());
            ok = false;
        }
    }
    std::printf("%s — Quadra 605 SimpleText application etalon\n",
                ok ? "PASSED" : "FAILED");
    return ok ? 0 : 1;
}
