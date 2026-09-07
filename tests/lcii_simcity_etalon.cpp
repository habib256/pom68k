// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── SimCity 2000 as a sustained application gate on the LC II ──
//
// The boot etalons prove the Finder appears and the beyond-boot legs prove
// the machine is usable; `lcii_simcity_census` measured the JIT under a real
// application but was a census, not a gate. This is the gate (TODO § C.1):
// it boots `hdv/GISTPERSO-boot.vhd`, navigates the Finder deterministically,
// opens the heaviest saved city on the volume — which launches the game and
// loads the save in one gesture — lets the simulation run under a fixed
// guest budget with the mouse moving, saves the city, and asserts a
// functional observable at every step:
//
//   launch      CurApName names SimCity 2000, the screen changed, SCSI read
//   simulation  the city's title-bar date/funds text changed over the play
//               budget (the simulation advanced, not just a redraw), the
//               menu bar is still up, the CPU never halted
//   save        Cmd-S wrote blocks and the (in-memory, host-owned) volume
//               differs from its pre-save bytes — a persistent artefact
//
// Unless POM68K_CPU_ENGINE is set explicitly, the scenario runs TWICE in
// this process — interpreter first, then the family's default engine — and
// the two legs must agree on the architectural fingerprint, the screen, the
// SCSI counts and every observable above. That is the CPU-sensitive
// contract TODO § C names: the same result under interpreter and
// accelerated engine. With POM68K_CPU_ENGINE set, one leg runs (the
// `interp_`/`jit_` registrations elsewhere use that shape).
//
// The volume carries the 2026-07-18 GISTPERSO startup-race debt; every run
// of this gate walks that boot path with no held keys and must reach the
// Finder, so it is also that item's deterministic reproducer.
// POM68K_DUMP=1 writes lcii_simcity_gate_*.ppm per phase.

#include "LciiApplicationHarness.h"

using namespace lciiapp;

namespace {

struct Leg {
    std::string engine;
    bool finder = false, launched = false, halted = true, menuUp = false;
    std::string app;
    double launchMoved = 0.0;
    long scsiBoot = 0, scsiLaunch = 0, scsiPlay = 0;
    long writeBlocksBeforeSave = 0, writeBlocksAfterSave = 0;
    bool simulated = false, saved = false;
    uint64_t titleBefore = 0, titleAfter = 0;
    uint64_t fp = 0, screenFp = 0;
    bool ok() const {
        return finder && launched && !halted && menuUp && simulated && saved;
    }
};

constexpr long kBootFrames = 16000;
constexpr long kLaunchSettle = 7200;      // launch + load the big city
constexpr long kPlayFrames = 3600;        // 60 guest seconds of simulation

bool runLeg(const std::vector<uint8_t>& romData, const std::string& img,
            const jit::ResolvedConfig& jitConfig, const char* label,
            Leg& leg) {
    V8Memory mem(pom68k::defaultCoreConfig());
    if (!mem.loadRom(romData)) {
        std::fprintf(stderr, "FAIL: bad ROM\n");
        return false;
    }
    Cpu030 cpu(mem, jitConfig, pom68k::defaultCoreConfig().cpu,
               /*withFpu=*/true);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(img)) {
        std::fprintf(stderr, "FAIL: bad disk\n");
        return false;
    }
    ensureBootDriverType(mem.scsiDisk().image());
    while (mem.cpuHeld()) mem.tick(1000);
    gMem = &mem;
    gCpu = &cpu;
    leg.engine = jitConfig.engineForGuest(true) == jit::EngineKind::Jit
                     ? cpu.jit().backendName() : "interp";
    const std::string ppm = std::string("lcii_simcity_gate_") + label + "_";
    auto dumpPhase = [&](const char* phase) {
        dump((ppm + phase + ".ppm").c_str());
    };

    // ── boot ─────────────────────────────────────────────────────────────
    runFrames(kBootFrames);
    for (int poll = 0; poll < 20 && !finderUp(); poll++) {
        keyHold(0x24, 150);                    // Return: dismiss any alert
        runFrames(600);
    }
    leg.finder = finderUp();
    leg.scsiBoot = mem.scsi().commands;
    dumpPhase("boot");
    std::printf("[%s] boot: Finder %s, SCSI %ld, front app '%s'\n", label,
                leg.finder ? "up" : "NOT UP", leg.scsiBoot,
                frontApplication().c_str());
    if (!leg.finder || cpu.isHalted()) return false;

    // ── launch by opening the saved city ─────────────────────────────────
    std::vector<uint32_t> beforeLaunch;
    screen(beforeLaunch);
    closeAllFinderWindows();
    dumpPhase("desktop");
    // The route, read off the phase screenshots rather than assumed: the
    // volume root holds no SimCity folder — it is under JEUX. A prefix
    // that matches nothing selects the alphabetical neighbour, and the first
    // run of this gate opened TRAVAIL, then a Works document, and launched
    // Works 3.0; CurApName refused it, on both engines, identically.
    openBySelect("gist", 900);                 // GIST PERSO root
    dumpPhase("root");
    openBySelect("jeux", 900);                 // JEUX: the games folder
    dumpPhase("games");
    openBySelect("simcity", 900);              // SimCity2000 folder
    dumpPhase("folder");
    openBySelect("sim v", 900);                // SIM VILLES: the saved cities
    dumpPhase("cities");
    // "black forest m" disambiguates BLACK FOREST MONSTRE from
    // black forest.rail; that city sized the adaptive cache boost on
    // 2026-07-17 and is the heaviest load this volume offers.
    openBySelect("black forest m", kLaunchSettle);
    std::vector<uint32_t> afterLaunch;
    screen(afterLaunch);
    leg.launchMoved = changed(beforeLaunch, afterLaunch);
    leg.scsiLaunch = mem.scsi().commands;
    leg.app = frontApplication();
    leg.launched = leg.app.rfind("SimCity 2000", 0) == 0 &&
                   leg.launchMoved > 0.30 && leg.scsiLaunch > leg.scsiBoot;
    dumpPhase("launch");
    std::printf("[%s] launch: front app '%s', %.1f%% of the screen changed, "
                "SCSI +%ld\n", label, leg.app.c_str(),
                leg.launchMoved * 100.0, leg.scsiLaunch - leg.scsiBoot);
    if (!leg.launched) return false;

    // ── simulate under a fixed guest budget ──────────────────────────────
    // The January budget window opens over a freshly loaded city; Return
    // takes its default button. The city's own title bar carries the date
    // and the funds, and that text is the progression observable: a redraw
    // leaves it alone, a running simulation moves the month and the money.
    keyHold(0x24, 30);
    runFrames(300);
    leg.titleBefore = regionMaskFingerprint(120, 400, 20, 36);
    dumpPhase("play-start");
    for (int i = 0; i < kPlayFrames / 60; i++) {
        mem.mouseMove((i % 2) ? 6 : -6, (i % 3) ? 4 : -4);
        runFrames(60);
    }
    leg.titleAfter = regionMaskFingerprint(120, 400, 20, 36);
    leg.scsiPlay = mem.scsi().commands;
    leg.simulated = leg.titleAfter != leg.titleBefore;
    dumpPhase("play-end");
    std::printf("[%s] simulation: title %016llx -> %016llx (%s), SCSI +%ld\n",
                label, (unsigned long long)leg.titleBefore,
                (unsigned long long)leg.titleAfter,
                leg.simulated ? "advanced" : "STATIC",
                leg.scsiPlay - leg.scsiLaunch);

    // ── save: the persistent artefact ────────────────────────────────────
    std::vector<uint8_t>& disk = mem.scsiDisk().image();
    const std::vector<uint8_t> snapshot = disk;
    leg.writeBlocksBeforeSave = mem.scsiDisk().writeBlocks;
    mem.keyEvent(0x37, true);                  // Cmd
    runFrames(6);
    keyHold(0x01, 30);                         // 's' — Save
    mem.keyEvent(0x37, false);
    runFrames(900);                            // write + catalog flush
    leg.writeBlocksAfterSave = mem.scsiDisk().writeBlocks;
    leg.saved = leg.writeBlocksAfterSave > leg.writeBlocksBeforeSave &&
                disk != snapshot;
    dumpPhase("saved");
    std::printf("[%s] save: %ld blocks written, volume %s\n", label,
                leg.writeBlocksAfterSave - leg.writeBlocksBeforeSave,
                disk != snapshot ? "modified" : "UNCHANGED");

    // ── end state ────────────────────────────────────────────────────────
    std::vector<uint32_t> fb;
    screen(fb);
    leg.menuUp = blackRatio(fb, 0, 512, 2, 16) < 0.30;
    leg.halted = cpu.isHalted();
    leg.fp = bench::fingerprint(cpu);
    leg.screenFp = screenFingerprint();
    std::printf("[%s] end: engine=%s halted=%d menu=%s fp=%016llx "
                "screen=%016llx SCSI %ld commands\n", label,
                leg.engine.c_str(), leg.halted, leg.menuUp ? "up" : "GONE",
                (unsigned long long)leg.fp, (unsigned long long)leg.screenFp,
                mem.scsi().commands);
    return leg.ok();
}

}  // namespace

int main() {
    const std::string rom =
        find("roms/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM");
    std::string img = testasset::overrideImage();
    if (img.empty()) img = find("hdv/GISTPERSO-boot.vhd");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs the LC II ROM + hdv/GISTPERSO-boot.vhd "
                    "(the volume that carries SimCity 2000)\n");
        return 0;
    }
    testasset::report({rom, img});
    std::ifstream in(rom, std::ios::binary);
    const std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                       std::istreambuf_iterator<char>());

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
            std::fprintf(stderr, "FAIL: leg '%s' — finder=%d launched=%d "
                         "simulated=%d saved=%d halted=%d menu=%d\n", label,
                         leg.finder, leg.launched, leg.simulated, leg.saved,
                         leg.halted, leg.menuUp);
            ok = false;
        }
    }
    if (ok && results.size() == 2) {
        const Leg& a = results[0];
        const Leg& b = results[1];
        const bool same = a.fp == b.fp && a.screenFp == b.screenFp &&
            a.scsiBoot == b.scsiBoot && a.scsiLaunch == b.scsiLaunch &&
            a.scsiPlay == b.scsiPlay &&
            a.titleBefore == b.titleBefore && a.titleAfter == b.titleAfter &&
            a.writeBlocksAfterSave - a.writeBlocksBeforeSave ==
                b.writeBlocksAfterSave - b.writeBlocksBeforeSave;
        std::printf("identity: %s vs %s — %s\n", a.engine.c_str(),
                    b.engine.c_str(), same ? "IDENTICAL" : "DIVERGENT");
        if (!same) {
            std::fprintf(stderr, "FAIL: the interpreter and the %s engine "
                         "disagree on the SimCity session\n",
                         b.engine.c_str());
            ok = false;
        }
    }
    std::printf("%s — LC II SimCity 2000 application etalon\n",
                ok ? "PASSED" : "FAILED");
    return ok ? 0 : 1;
}
