// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The JIT under a REAL application load: SimCity 2000 on the LC II.
//
// Why this exists (TODO.md § 3, named 2026-08-27). Every JIT number this
// project quotes comes from one of three places — a fixed-cycle bench, a boot
// etalon, or the Rogue census. None of them is what a person does with the
// machine. Rogue draws tiles; SimCity 2000 runs a sustained integer simulation
// over large structures, redraws dense QuickDraw scenes, and pages from disk,
// for minutes rather than seconds. The 030 fallback histogram under THAT is
// the evidence the engine's next round of work should be chosen from.
//
// It is a MEASUREMENT, not a gate: EXCLUDE_FROM_ALL, no CTest entry, and no
// threshold — a performance verdict belongs in performance_budgets.tsv, per
// host, or nowhere. It still exits 1 when the app demonstrably never started,
// because an idle census reported as an application census would be worse
// than no number at all.
//
// The workload is already in the tree: `hdv/GISTPERSO-boot.vhd` carries
// SimCity 2000 (33 occurrences of the name in its catalog). That volume also
// carries an open debt — the 2026-07-18 startup race, the CPU spinning in the
// ROM Memory Manager heap-walk at $40A0E148 while the Finder builds the
// desktop. This harness necessarily walks that path, so it is also the
// deterministic reproducer that item has been waiting for.
//
// Driving is keyboard-only, deliberately: the LC II beyond-boot `launch` leg
// steers the mouse to an icon at a pixel position belonging to this volume's
// window layout, and a gesture calibrated on one image is the trap this
// project has paid for twice. Finder type-select plus Cmd-O needs no
// coordinates.
//
// POM68K_DUMP=1 writes lcii_simcity_*.ppm at every phase.

#include "AssetFingerprint.h"
#include "BenchHarness.h"
#include "Cpu030.h"
#include "JitTestConfig.h"
#include "V8Memory.h"
#include "V8Video.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

V8Memory* gMem;
Cpu030* gCpu;

std::string find(const char* rel) { return testasset::find(rel); }

void runFrames(long n) {
    for (long f = 0; f < n && !gCpu->isHalted(); f++)
        gCpu->runCycles(V8Memory::kCpuHz / 60);
}

void screen(std::vector<uint32_t>& out) {
    V8Video video(*gMem);
    video.decode(out);
}

double blackRatio(const std::vector<uint32_t>& fb, int x0, int x1,
                  int y0, int y1) {
    long black = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            size_t i = size_t(y) * 512 + x;
            if (i < fb.size() && (fb[i] & 0xFF) < 0x80) black++;
        }
    return double(black) / double(x1 - x0) / double(y1 - y0);
}

bool finderUp() {
    std::vector<uint32_t> fb;
    screen(fb);
    // A light menu bar over a desktop that has SOMETHING on it. The upper
    // bound the beyond-boot gates use (desk < 0.65) belongs to their own
    // volume: GISTPERSO's desktop pattern measures 0.75 here, and a harness
    // that refuses the machine because another volume is lighter would be
    // calibrating on the wrong image — the trap this project keeps paying.
    return blackRatio(fb, 0, 512, 2, 16) < 0.30 &&
           blackRatio(fb, 400, 512, 40, 340) > 0.20;
}

void dump(const char* name) {
    if (!getenv("POM68K_DUMP")) return;
    std::vector<uint32_t> fb;
    screen(fb);
    if (fb.empty()) return;
    std::FILE* f = std::fopen(name, "wb");
    if (!f) return;
    std::fprintf(f, "P6\n512 384\n255\n");
    for (uint32_t p : fb) {
        unsigned char rgb[3] = {(unsigned char)(p >> 16), (unsigned char)(p >> 8),
                                (unsigned char)p};
        std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
}

// The driver-descriptor patch every LC II harness applies: without a type
// $6A entry the ROM refuses the volume, and the machine sits there having
// issued not one SCSI command — which is exactly what the first run of this
// harness printed (2026-08-27).
void ensureBootDriverType(std::vector<uint8_t>& img) {
    if (img.size() < 512 || img[0] != 'E' || img[1] != 'R') return;
    int count = (img[0x10] << 8) | img[0x11];
    for (int i = 0; i < count && 0x12 + i * 8 + 8 <= 512; i++) {
        int e = 0x12 + i * 8;
        if (((img[e + 6] << 8) | img[e + 7]) == 0x6A) return;
    }
    if (count >= 1 && 0x12 + count * 8 + 8 <= 512) {
        int src = 0x12, dst = 0x12 + count * 8;
        for (int k = 0; k < 8; k++) img[dst + k] = img[src + k];
        img[dst + 6] = 0x00; img[dst + 7] = 0x6A;
        img[0x10] = uint8_t((count + 1) >> 8);
        img[0x11] = uint8_t(count + 1);
    }
}

// ASCII -> ADB, enough for Finder type-select. Typing must be FAST: the
// type-select window is about a second, and the first attempt held each key
// for 30 frames, so every letter started a NEW selection and the prefix never
// accumulated — the phase dumps show it landing in the wrong folder.
uint8_t adbFor(char c) {
    switch (c) {
        case 'a': return 0x00; case 's': return 0x01; case 'd': return 0x02;
        case 'f': return 0x03; case 'h': return 0x04; case 'g': return 0x05;
        case 'z': return 0x06; case 'x': return 0x07; case 'c': return 0x08;
        case 'v': return 0x09; case 'b': return 0x0B; case 'q': return 0x0C;
        case 'w': return 0x0D; case 'e': return 0x0E; case 'r': return 0x0F;
        case 'y': return 0x10; case 't': return 0x11; case 'o': return 0x1F;
        case 'u': return 0x20; case 'i': return 0x22; case 'p': return 0x23;
        case 'l': return 0x25; case 'j': return 0x26; case 'k': return 0x28;
        case 'n': return 0x2D; case 'm': return 0x2E; case ' ': return 0x31;
        default:  return 0xFF;
    }
}

void typeText(const char* text) {
    for (const char* p = text; *p; p++) {
        const uint8_t code = adbFor(*p);
        if (code == 0xFF) continue;
        gMem->keyEvent(code, true);
        runFrames(3);
        gMem->keyEvent(code, false);
        runFrames(3);
    }
}

void keyHold(uint8_t code, long frames) {
    gMem->keyEvent(code, true);
    runFrames(frames);
    gMem->keyEvent(code, false);
    runFrames(6);
}

// How much of the screen differs — the only application-agnostic evidence
// that something actually happened.
double changed(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
    long n = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); i++)
        if (a[i] != b[i]) n++;
    return a.empty() ? 0.0 : double(n) / double(a.size());
}

uint64_t screenFingerprint() {
    std::vector<uint32_t> fb;
    screen(fb);
    uint64_t fp = 1469598103934665603ull;
    for (uint32_t pixel : fb) {
        for (int byte = 0; byte < 4; byte++) {
            fp ^= (pixel >> (byte * 8)) & 0xFF;
            fp *= 1099511628211ull;
        }
    }
    return fp;
}

}  // namespace

int main() {
    setenv("POM68K_JIT_HISTO", "1", 0);

    const std::string rom =
        find("roms/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM");
    std::string img = testasset::overrideImage();
    if (img.empty()) img = find("hdv/GISTPERSO-boot.vhd");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs the LC II ROM + hdv/GISTPERSO-boot.vhd "
                    "(the volume that carries SimCity 2000)\n");
        return 0;
    }
    testasset::report({ rom, img });

    std::ifstream in(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
    V8Memory mem(pom68k::defaultCoreConfig());
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    // resolveFromEnvironment(), not defaultResolvedConfig(): since the JIT
    // configuration became injected (2026-08-27) a fixed default ignores
    // POM68K_JIT_HISTO, and this harness then runs a perfectly good census
    // that prints NOTHING — which is what its first complete run did.
    const jit::ResolvedConfig jitConfig = testjit::resolveFromEnvironment();
    Cpu030 cpu(mem, jitConfig, pom68k::defaultCoreConfig().cpu,
               /*withFpu=*/true);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk\n"); return 1; }
    ensureBootDriverType(mem.scsiDisk().image());
    // The Egret holds the CPU at power-on. Forget this and the machine
    // runs forever without issuing ONE SCSI command — which is exactly
    // what the first two runs of this harness printed.
    while (mem.cpuHeld()) mem.tick(1000);
    gMem = &mem; gCpu = &cpu;

    // ── Phase 1: boot ────────────────────────────────────────────────────
    runFrames(16000);                          // the boot etalon's budget
    for (int poll = 0; poll < 20 && !finderUp(); poll++) {
        keyHold(0x24, 150);                    // Return: dismiss any alert
        runFrames(600);
    }
    {   // Print what the screen actually measures. The Finder thresholds came
        // from another gate's volume, and this line is how the mismatch was
        // found: GISTPERSO's desktop reads 0.75 where the other volume reads
        // under 0.65, so an uncalibrated check called a live Finder dead.
        std::vector<uint32_t> fb;
        screen(fb);
        std::fprintf(stderr, "[screen] %zu px, menu %.2f desk %.2f\n",
                     fb.size(), blackRatio(fb, 0, 512, 2, 16),
                     blackRatio(fb, 400, 512, 40, 340));
    }
    const bool up = finderUp();
    std::printf("boot: Finder %s, SCSI %ld commands\n",
                up ? "up" : "NOT UP", mem.scsi().commands);
    dump("lcii_simcity_boot.ppm");
    cpu.jit().censusPhase("boot");
    if (!up) {
        std::fprintf(stderr, "FAIL: no Finder — nothing to launch\n");
        return 1;
    }

    // ── Phase 2: the idle control ────────────────────────────────────────
    runFrames(3600);                           // 60 emulated seconds
    cpu.jit().censusPhase("idle-finder");

    // ── Phase 3: launch by type-select ───────────────────────────────────
    // The Finder selects by typed prefix, then Cmd-O opens the selection.
    std::vector<uint32_t> beforeLaunch;
    screen(beforeLaunch);
    const long scsi0 = mem.scsi().commands;
    // Two hops, because that is where the game lives on this volume: "sim"
    // selects the SimCity2000 FOLDER at the root, Cmd-O opens it; inside,
    // "simc" is needed to pick the application over its "SIM VILLES" sibling
    // (a prefix stopping at "sim" selects the wrong one — read off the window
    // in lcii_simcity_folder.ppm rather than assumed).
    auto open = [&](const char* prefix, long settle) {
        typeText(prefix);
        runFrames(30);
        mem.keyEvent(0x37, true);              // Cmd
        runFrames(6);
        keyHold(0x1F, 60);                     // 'o' — Open
        mem.keyEvent(0x37, false);
        runFrames(settle);
    };
    // Three hops, each read off the previous phase's screenshot rather than
    // guessed: the SimCity2000 folder at the volume root, its SIM VILLES
    // folder of saved cities, then the city DOCUMENT — opening a document
    // launches the application and loads the save in one gesture, which is
    // both fewer keystrokes and the only deterministic way to reach the heavy
    // workload. "black forest m" disambiguates BLACK FOREST MONSTRE from
    // black forest.rail; that city is the one CHANGELOG 2026-07-17 used to
    // size the adaptive cache boost, so it is the heaviest load this volume
    // can offer.
    open("sim", 900);
    dump("lcii_simcity_folder.ppm");
    cpu.jit().censusPhase("open-folder");
    open("sim v", 900);
    dump("lcii_simcity_cities.ppm");
    cpu.jit().censusPhase("open-cities");
    open("black forest m", 7200);              // launch + load the big city

    std::vector<uint32_t> afterLaunch;
    screen(afterLaunch);
    const double moved = changed(beforeLaunch, afterLaunch);
    std::printf("launch: %.1f%% of the screen changed, SCSI +%ld\n",
                moved * 100.0, mem.scsi().commands - scsi0);
    dump("lcii_simcity_launch.ppm");
    cpu.jit().censusPhase("launch");

    // ── Phase 4: the number this harness exists for ──────────────────────
    // Whatever is on screen now runs for two emulated minutes with the mouse
    // wiggling, which is what a person does: the redraw path is the load.
    const auto loadStart = std::chrono::steady_clock::now();
    for (int i = 0; i < 120; i++) {
        mem.mouseMove((i % 2) ? 6 : -6, (i % 3) ? 4 : -4);
        runFrames(60);
    }
    const auto loadEnd = std::chrono::steady_clock::now();
    const double loadWall =
        std::chrono::duration<double>(loadEnd - loadStart).count();
    dump("lcii_simcity_load.ppm");
    cpu.jit().censusPhase(moved < 0.05 ? "app-load(INVALID: nothing launched)"
                                       : "app-load");

    std::printf("app-load: wall=%.6fs fp=%016llx screen=%016llx\n",
                loadWall,
                (unsigned long long)bench::fingerprint(cpu),
                (unsigned long long)screenFingerprint());
    std::printf("halted=%d, SCSI %ld commands total\n",
                gCpu->isHalted(), mem.scsi().commands);
    if (moved < 0.05) {
        std::fprintf(stderr,
                     "FAIL: the screen barely moved — type-select + Cmd-O did "
                     "not start an application, so the census above is an idle "
                     "census wearing another name\n");
        return 1;
    }
    return 0;
}
