// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The drawing-phase fallback census — the instrument POM68K_JIT.md § 3.5
// says the 68020 indexed-mode question is waiting on. The idle-Finder
// census put the indexed modes at ~167 k fallbacks and was refused as
// evidence because that workload does not draw; QuickDraw's blitters —
// what the indexed modes were motivated by — only run when something
// paints. This harness makes something paint, on the shipping
// configuration (Quadra 605, `jit/auto`, native backend), with a workload
// the tree owns: dev/mac-rogue's Rogue.dsk, mounted off the SWIM2 and
// driven by keyboard for ~two emulated minutes of tile blitting.
//
// It is a measurement, not a gate: EXCLUDE_FROM_ALL, no CTest entry. It
// still FAILS (exit 1) when the game demonstrably never ran — an idle
// census reported as a drawing census would be worse than no number.
//
// Engine::censusPhase() splits the run into separately-dumped censuses:
//   boot          power-on to the settled Finder (reference)
//   idle-finder   60 emulated seconds of nothing (the § 3.5 control)
//   mount+launch  floppy mount, window, app load off GCR
//   rogue-title   title + briefing keys, dungeon generation
//   (destructor)  the gameplay phase — THE number this exists for
//
// POM68K_DUMP=1 writes q605_rogue_*.ppm at every step for calibration.
// Soft-skips without the FF7439EE ROM + hdv/MacOS-8.1-boot.vhd +
// dev/mac-rogue/build/Rogue.dsk.

#include "AssetFingerprint.h"
#include "Cpu040.h"
#include "Q605Memory.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {
std::string findAsset(std::initializer_list<const char*> names) {
    for (const char* name : names)
        for (const std::string& base : { std::string(), std::string("../") }) {
            std::string path = base + name;
            if (std::ifstream(path, std::ios::binary)) return path;
        }
    return {};
}

Q605Memory* gMem;
Cpu040* gCpu;
constexpr int kFrameCycles = 416667;          // 25 MHz / ~60 Hz

void runFrames(long n) {
    for (long f = 0; f < n && !gCpu->isHalted(); f++)
        gCpu->runCycles(kFrameCycles);
}

uint32_t peek32(uint32_t addr) {
    return uint32_t(gMem->peek8(addr)) << 24 |
           uint32_t(gMem->peek8(addr + 1)) << 16 |
           uint32_t(gMem->peek8(addr + 2)) << 8 |
           gMem->peek8(addr + 3);
}

struct Screen {
    int width = 0, height = 0, depth = 0;
    uint32_t stride = 0, offset = 0;
    std::vector<uint32_t> pixels;
};

// Same decode as q605_boot_etalon: the main GDevice's PixMap through the
// DAFB CLUT — the guest's own idea of the screen, not a hardcoded mode.
Screen decodeScreen(const Q605Memory& mem) {
    Screen s;
    uint32_t scrnBase = peek32(0x0824);
    uint32_t mainDevH = peek32(0x08A4);
    uint32_t mainDev = mainDevH ? peek32(mainDevH) : 0;
    uint32_t pmapH = mainDev ? peek32(mainDev + 0x16) : 0;
    uint32_t pmap = pmapH ? peek32(pmapH) : 0;
    if (!pmap) return s;

    uint32_t pmBase = peek32(pmap);
    uint32_t boundsA = peek32(pmap + 0x06);
    uint32_t boundsB = peek32(pmap + 0x0A);
    int top = int(boundsA >> 16), left = int(boundsA & 0xFFFF);
    int bottom = int(boundsB >> 16), right = int(boundsB & 0xFFFF);
    s.width = right - left;
    s.height = bottom - top;
    s.depth = mem.dafbDepth();
    s.stride = mem.dafbStride();
    s.offset = (pmBase ? pmBase : scrnBase) & (Q605Memory::kVramSize - 1);
    if (s.width <= 0 || s.width > 1600 || s.height <= 0 || s.height > 1200 ||
        (s.depth != 1 && s.depth != 2 && s.depth != 4 && s.depth != 8) ||
        s.stride < uint32_t((s.width * s.depth + 7) / 8) ||
        uint64_t(s.offset) + uint64_t(s.height) * s.stride > Q605Memory::kVramSize) {
        s = {};
        return s;
    }

    const uint8_t* vram = mem.vram();
    const uint8_t (*clut)[3] = mem.clut();
    s.pixels.resize(size_t(s.width) * s.height);
    for (int y = 0; y < s.height; y++) {
        uint32_t row = s.offset + uint32_t(y) * s.stride;
        for (int x = 0; x < s.width; x++) {
            uint8_t packed = vram[row + uint32_t(x * s.depth / 8)];
            uint8_t pen;
            if (s.depth == 1) pen = (packed >> (7 - (x & 7))) & 1;
            else if (s.depth == 2) pen = (packed >> (6 - 2 * (x & 3))) & 3;
            else if (s.depth == 4) pen = (x & 1) ? packed & 0x0F : packed >> 4;
            else pen = packed;
            const uint8_t* c = clut[pen];
            s.pixels[size_t(y) * s.width + x] =
                uint32_t(c[0]) << 16 | uint32_t(c[1]) << 8 | c[2];
        }
    }
    return s;
}

struct Stats { double mean = 0, deviation = 0; };
Stats luminanceStats(const Screen& s, int x0, int x1, int y0, int y1) {
    if (x1 > s.width) x1 = s.width;
    if (y1 > s.height) y1 = s.height;
    if (x0 >= x1 || y0 >= y1) return {};
    double sum = 0, sum2 = 0;
    long count = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            uint32_t p = s.pixels[size_t(y) * s.width + x];
            double lum = ((p >> 16) * 54 + ((p >> 8) & 0xFF) * 183 +
                          (p & 0xFF) * 19) / 256.0;
            sum += lum; sum2 += lum * lum; count++;
        }
    Stats r;
    if (count) {
        r.mean = sum / count;
        r.deviation = std::sqrt(sum2 / count - r.mean * r.mean);
    }
    return r;
}

// The full q605_boot_etalon Finder signature (menu bar AND desktop).
bool finderUp(const Screen& s) {
    if (s.width != 640 || s.height != 480 || s.depth != 8) return false;
    Stats m = luminanceStats(s, 0, s.width, 2, 16);
    Stats d = luminanceStats(s, 520, 630, 40, 430);
    return m.mean > 170 && m.mean < 235 &&
           m.deviation > 40 && m.deviation < 100 &&
           d.mean > 100 && d.mean < 190 &&
           d.deviation > 30 && d.deviation < 90 &&
           m.mean - d.mean > 35;
}

double diffRatio(const Screen& a, const Screen& b) {
    if (a.pixels.empty() || a.pixels.size() != b.pixels.size()) return 1.0;
    size_t changed = 0;
    for (size_t i = 0; i < a.pixels.size(); i++)
        if (a.pixels[i] != b.pixels[i]) changed++;
    return double(changed) / double(a.pixels.size());
}

void dump(const char* name, const Screen& s) {
    if (!getenv("POM68K_DUMP") || s.pixels.empty()) return;
    FILE* fp = fopen(name, "wb");
    std::fprintf(fp, "P6\n%d %d\n255\n", s.width, s.height);
    for (uint32_t p : s.pixels) {
        uint8_t rgb[3] = { uint8_t(p >> 16), uint8_t(p >> 8), uint8_t(p) };
        fwrite(rgb, 1, 3, fp);
    }
    fclose(fp);
}

bool bootToFinder(int maxFrames) {
    while (gMem->cpuHeld()) gMem->tick(1000);
    long scsi0 = gMem->scsi().commands;
    for (int frame = 0; frame < maxFrames && !gCpu->isHalted(); frame++) {
        gCpu->runCycles(kFrameCycles);
        if (frame >= 3600 && !(frame % 60) &&
            gMem->scsi().commands - scsi0 > 4000) {
            Screen s = decodeScreen(*gMem);
            if (finderUp(s)) {
                runFrames(300);
                return !gCpu->isHalted();
            }
        }
    }
    return false;
}
} // namespace

int main() {
    // The census is the point: arm it before the engine is constructed.
    setenv("POM68K_JIT_HISTO", "1", 0);

    std::string romPath = findAsset({
        "roms/1MB ROMs/1993-10 - FF7439EE - LC475,575,Quadra 605,Performa 475,476,575,577,578.ROM",
        "roms/mame/macqd605/ff7439ee.bin",
        "roms/quadra605.rom", "roms/q605.rom"
    });
    std::string diskPath = findAsset({
        "hdv/MacOS-8.1-boot.vhd", "hdv/q605-boot.vhd"
    });
    std::string roguePath = findAsset({
        "dev/mac-rogue/build/Rogue.dsk", "disks35/Rogue.dsk"
    });
    if (romPath.empty() || diskPath.empty() || roguePath.empty()) {
        std::printf("SKIP: needs FF7439EE ROM + hdv/MacOS-8.1-boot.vhd + "
                    "dev/mac-rogue/build/Rogue.dsk\n");
        return 0;
    }
    testasset::report({ romPath, diskPath, roguePath });

    std::ifstream in(romPath, std::ios::binary);
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    if (rom.size() != Q605Memory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM is %zu bytes, want 1 MB\n", rom.size());
        return 1;
    }

    // Private floppy copy, normalized to a cleanly-unmounted volume
    // (drAtrb bit 8 at $40A) so the mount is deterministic and the asset
    // is never the thing that changes.
    std::string rogueCopy = "q605_rogue_census.dsk";
    {
        std::ifstream fin(roguePath, std::ios::binary);
        std::vector<uint8_t> img((std::istreambuf_iterator<char>(fin)),
                                 std::istreambuf_iterator<char>());
        if (img.size() >= 0x40C) img[0x40A] = uint8_t(img[0x40A] | 0x01);
        std::ofstream fout(rogueCopy, std::ios::binary | std::ios::trunc);
        fout.write(reinterpret_cast<const char*>(img.data()),
                   std::streamsize(img.size()));
    }

    Q605Memory mem(32u << 20);
    if (!mem.loadRom(rom) || !mem.attachScsi(diskPath)) {
        std::fprintf(stderr, "FAIL: could not load ROM/disk\n");
        return 1;
    }
    Cpu040 cpu(mem);
    mem.setCpu(&cpu);
    cpu.hardReset();
    gMem = &mem; gCpu = &cpu;

    std::printf("q605_rogue_census — engine %s, backend %s\n",
                cpu.jit().enabled() ? "jit" : "interp",
                cpu.jit().backendName());

    if (!bootToFinder(12000)) {
        std::fprintf(stderr, "FAIL: no Finder after boot (halted=%d SCSI=%ld)\n",
                     cpu.isHalted(), mem.scsi().commands);
        return 1;
    }
    std::printf("Finder up (SCSI %ld)\n", mem.scsi().commands);
    dump("q605_rogue_finder.ppm", decodeScreen(mem));
    cpu.jit().censusPhase("boot");

    // ── The § 3.5 control: an idle Finder, same run, same counters. ──
    runFrames(3600);                          // 60 emulated seconds
    cpu.jit().censusPhase("idle-finder");

    auto keyTap = [&](uint8_t code, int hold = 4, int gap = 8) {
        mem.keyEvent(code, true);
        runFrames(hold);
        mem.keyEvent(code, false);
        runFrames(gap);
    };
    // The guest's own KeyMap ($174, bit N = virtual code N): what the System
    // believes is held down. The project's rule is that a gesture which does
    // not land gets this read BEFORE anything else is suspected.
    auto keymapHas = [&](uint8_t code) {
        return (gMem->peek8(0x174 + (code >> 3)) >> (code & 7)) & 1;
    };
    auto cmdTap = [&](uint8_t code, const char* what) {
        mem.keyEvent(0x37, true);             // Cmd down
        runFrames(6);
        const bool cmdSeen = keymapHas(0x37);
        mem.keyEvent(code, true);
        runFrames(6);
        const bool keySeen = keymapHas(code);
        mem.keyEvent(code, false);
        runFrames(8);
        mem.keyEvent(0x37, false);
        runFrames(30);
        std::printf("  Cmd-%s: KeyMap saw Cmd=%d key=%d\n", what,
                    cmdSeen, keySeen);
    };

    // ── Mount: the insert EVENT after the Finder is up is the real user
    // gesture, and what makes the System poll and mount. ──
    Screen preInsert = decodeScreen(mem);
    if (!mem.insertDisk(rogueCopy)) {
        std::fprintf(stderr, "FAIL: could not insert %s\n", rogueCopy.c_str());
        return 1;
    }
    double mountDelta = 0;
    for (int i = 0; i < 60; i++) {            // up to ~60 s to mount GCR
        runFrames(60);
        mountDelta = diffRatio(preInsert, decodeScreen(mem));
        if (mountDelta > 0.001) break;        // desktop icon appeared
    }
    runFrames(300);                           // settle
    std::printf("mount: screen delta %.4f after insert\n", mountDelta);
    dump("q605_rogue_mounted.ppm", decodeScreen(mem));
    if (mountDelta <= 0.0005) {
        std::fprintf(stderr, "FAIL: no screen change after insert — "
                     "the floppy never mounted\n");
        return 1;
    }

    // ── Launch, keyboard only (position-independent): type-select the
    // volume icon on the desktop, Cmd-O opens its window, then type-select
    // the app INSIDE that window and Cmd-O again. Type-select rather than
    // Cmd-A/select-all, because the first cut used Cmd-A and the icon came
    // back unselected — the same five letters that steer the desktop steer
    // a window, so this gesture reuses a mechanism already proven in this
    // very run instead of a second, unproven one.
    auto typeRogue = [&]() {
        keyTap(0x0F); keyTap(0x1F); keyTap(0x05); // r o g
        keyTap(0x20); keyTap(0x0E);               // u e
        runFrames(60);
    };
    typeRogue();
    Screen preOpen = decodeScreen(mem);
    cmdTap(0x1F, "O (open volume)");
    runFrames(600);
    Screen postOpen = decodeScreen(mem);
    double openDelta = diffRatio(preOpen, postOpen);
    std::printf("open: screen delta %.4f after Cmd-O\n", openDelta);
    dump("q605_rogue_volume.ppm", postOpen);

    typeRogue();                              // select the app in the window
    Screen preLaunch = decodeScreen(mem);
    std::printf("select: screen delta %.4f after typing in the window "
                "(want >0 — the icon highlights)\n",
                diffRatio(postOpen, preLaunch));
    cmdTap(0x1F, "O (launch)");               // Cmd-O — launch the app
    // The app loads off 800K GCR through the SWIM2: give it real time,
    // and poll for the big paint (the game window is 512x384 of tiles).
    double launchDelta = 0;
    for (int i = 0; i < 120; i++) {           // up to ~2 emulated minutes
        runFrames(60);
        launchDelta = diffRatio(preLaunch, decodeScreen(mem));
        if (launchDelta > 0.25) break;
    }
    runFrames(600);                           // let the title finish painting
    std::printf("launch: screen delta %.4f\n", launchDelta);
    dump("q605_rogue_title.ppm", decodeScreen(mem));
    if (launchDelta <= 0.10) {
        std::fprintf(stderr, "FAIL: screen never changed enough after the "
                     "launch Cmd-O — the app did not come up\n");
        return 1;
    }
    cpu.jit().censusPhase("mount+launch");

    // ── Title (any non-B key starts; B is the boss cheat), briefing. ──
    keyTap(0x31, 8, 60);                      // Space at the title
    runFrames(300);
    keyTap(0x31, 8, 60);                      // Space at the briefing
    runFrames(600);                           // dungeon gen + first render
    dump("q605_rogue_game0.ppm", decodeScreen(mem));
    cpu.jit().censusPhase("rogue-title");

    // ── The phase this instrument exists for: sustained tile blitting.
    // Every accepted move repaints the map + sprites through CopyBits;
    // walking into walls still redraws the player turn. I/L/K/J with
    // rests, ~15 frames per key, ~2 emulated minutes total. ──
    Screen g0 = decodeScreen(mem);
    static const uint8_t kMoves[] = {
        0x25, 0x25, 0x28, 0x28, 0x26, 0x26, 0x22, 0x22,   // L L K K J J I I
        0x25, 0x28, 0x26, 0x22, 0x2F,                     // L K J I .
    };
    // What proves the game is drawing is that EVERY round repaints, not
    // that the picture ends up different: a roguelike moves the player one
    // tile and redraws the map over itself, so a whole round of turns
    // changes ~0.1 % of the pixels while running CopyBits over the full
    // 512x384 frame each time. The first cut asked for a cumulative 0.10
    // and called a run that visibly played the game (monster killed, XP
    // 002, HP 13/14) an INVALID census. Count the rounds that moved
    // anything instead — that is the signal, and it is why FolderProbe's
    // rule ("the signal is the count that changes") is written down.
    // Sample after every KEY, not every round: the move list is a closed
    // walk (LL KK JJ II then one of each), so the player ends a round where
    // it started and a per-round diff reads ~0 while every single step in
    // between repainted. Measured on the same run: 13/36 rounds against
    // 441/468 keys — the round view reported a 94 % signal as a 36 % one,
    // one point above the floor it was being judged against.
    int drewKeys = 0, keys = 0;
    double moved = 0;
    Screen prev = g0;
    for (int rep = 0; rep < 36 && !cpu.isHalted(); rep++) {
        for (uint8_t code : kMoves) {
            keyTap(code, 4, 11);
            Screen now = decodeScreen(mem);
            const double d = diffRatio(prev, now);
            if (d > 0.0002) drewKeys++;      // ~60 pixels: one tile moved
            moved += d;
            prev = now;
            keys++;
        }
    }
    dump("q605_rogue_game1.ppm", prev);
    std::printf("gameplay: %d/%d keys repainted (want >=25%%), cumulative "
                "delta %.2f, halted=%d\n", drewKeys, keys, moved,
                cpu.isHalted());
    if (cpu.isHalted() || drewKeys * 4 < keys) {
        std::fprintf(stderr, "FAIL: the game never drew during the gameplay "
                     "phase — this census is not a drawing census\n");
        cpu.jit().censusPhase("rogue-gameplay(INVALID)");
        return 1;
    }
    cpu.jit().censusPhase("rogue-gameplay");
    std::printf("PASS: censuses dumped per phase above (stderr)\n");
    std::remove(rogueCopy.c_str());
    return 0;
}
