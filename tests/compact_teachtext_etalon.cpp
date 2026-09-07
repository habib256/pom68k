// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── TeachText as the 68000 application gate (Macintosh Plus, System 6.0.8) ──
//
// The application contract the LC II (SimCity 2000) and the Quadra 605
// (SimpleText) already honour, on the family that boots the interpreter by
// default. System 6's Finder has no type-select, so the route is the
// mouse: closed-loop steering against the low-memory Mouse global, clicks
// at positions read off this volume's own saved window layout (the image
// is host-owned and never written back, so the layout is deterministic).
//
//   launch    CurApName names TeachText, the screen changed, SCSI read
//   progress  two rounds of typing each change the document window
//   artefact  Cmd-S writes blocks and the catalog gains the document name
//   quit      Cmd-Q hands the front back to the Finder
//
// Unless POM68K_CPU_ENGINE is set, the scenario runs TWICE in this
// process: interpreter, then the JIT (threaded — valid for every family),
// and the two legs must agree on the fingerprint, screen and counts.
// POM68K_DUMP=1 writes compact_teachtext_*.ppm per phase.

#include "AssetFingerprint.h"
#include "BenchHarness.h"
#include "BeyondBoot.h"
#include "Cpu68k.h"
#include "FinderSignature.h"
#include "JitTestConfig.h"
#include "MacFrame.h"
#include "MacMemory.h"
#include "MacVideo.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

MacMemory* gMem = nullptr;
Cpu68k* gCpu = nullptr;
MacFrameClock* gClock = nullptr;

void runFrames(long n) {
    for (long f = 0; f < n && !gCpu->isHalted(); f++) gClock->runFrame(*gCpu, *gMem);
}

std::vector<uint32_t> screen() {
    MacVideo video;
    const uint32_t* fb = video.render(*gMem);
    return std::vector<uint32_t>(fb, fb + 512 * 342);
}

double blackRatio(const std::vector<uint32_t>& fb, int x0, int x1, int y0, int y1) {
    long black = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            if ((fb[size_t(y) * 512 + x] & 0xFF) < 0x80) black++;
    return double(black) / (double(x1 - x0) * (y1 - y0));
}

// finder_boot_matrix's bootPlus signature: white menu bar, dithered desktop.
bool finderUp() {
    const std::vector<uint32_t> fb = screen();
    const double menu = blackRatio(fb, 0, 512, 2, 16);
    const double desk = blackRatio(fb, 0, 512, 120, 240);
    return menu < 0.30 && desk > 0.40 && desk < 0.60;
}

bool menuBarUp() { return blackRatio(screen(), 0, 512, 2, 16) < 0.30; }

void dump(const char* name) {
    if (!std::getenv("POM68K_DUMP")) return;
    beyondboot::dumpPpm(name, screen(), 512, 342);
}

uint64_t regionMaskFingerprint(int x0, int x1, int y0, int y1) {
    const std::vector<uint32_t> fb = screen();
    uint64_t fp = 1469598103934665603ull;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            fp ^= (fb[size_t(y) * 512 + x] & 0xFF) < 0x80;
            fp *= 1099511628211ull;
        }
    return fp;
}

uint64_t screenFingerprint() { return regionMaskFingerprint(0, 512, 0, 342); }

double changed(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
    long n = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); i++)
        if (a[i] != b[i]) n++;
    return a.empty() ? 0.0 : double(n) / double(a.size());
}

// Low memory is physical on a 68000: CurApName and Mouse read straight.
std::string frontApplication() { return findersig::curApName(*gMem); }

void pointer(int& x, int& y) {
    x = int16_t(uint16_t(gMem->peek8(0x832)) << 8 | gMem->peek8(0x833));
    y = int16_t(uint16_t(gMem->peek8(0x830)) << 8 | gMem->peek8(0x831));
}

void steer(int tx, int ty) {
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
        gMem->mouseMove(step(dx), step(dy));
        runFrames(1);
    }
    pointer(px, py);
    if (std::abs(px - tx) > 2 || std::abs(py - ty) > 2)
        std::fprintf(stderr, "steer: wanted (%d,%d), reached (%d,%d)\n", tx, ty, px, py);
}

void click(int tx, int ty, long settle = 30) {
    steer(tx, ty);
    const bool trace = std::getenv("POM68K_TEACHTEXT_DISCOVER") != nullptr;
    if (trace) std::fprintf(stderr, "[click] MBState before = $%02X (want $80)\n",
                            gMem->peek8(0x172));
    gMem->mouseButton(true);
    runFrames(3);
    // MBState ($172): $00 while the guest sees the button down, $80 up — the
    // ROM's VBL task samples VIA PB3 (0 = pressed). The discovery trace
    // prints it so "the Finder ignored the click" and "the click never
    // reached the guest" stay two different findings.
    if (trace) std::fprintf(stderr, "[click] MBState during press = $%02X (want $00)\n",
                            gMem->peek8(0x172));
    runFrames(3);
    gMem->mouseButton(false);
    runFrames(6);
    runFrames(settle);
}

// Two clicks inside the guest's DoubleTime (System 6 default: 8 ticks).
void doubleClick(int tx, int ty, long settle) {
    steer(tx, ty);
    for (int i = 0; i < 2; i++) {
        gMem->mouseButton(true);
        runFrames(3);
        gMem->mouseButton(false);
        runFrames(3);
    }
    runFrames(settle);
}

// M0110 virtual codes are the ADB ones for the main keys; a US layout.
uint8_t keyFor(char c) {
    switch (c) {
        case 'a': return 0x00; case 's': return 0x01; case 'd': return 0x02;
        case 'f': return 0x03; case 'h': return 0x04; case 'g': return 0x05;
        case 'z': return 0x06; case 'x': return 0x07; case 'c': return 0x08;
        case 'v': return 0x09; case 'b': return 0x0B; case 'q': return 0x0C;
        case 'w': return 0x0D; case 'e': return 0x0E; case 'r': return 0x0F;
        case 'y': return 0x10; case 't': return 0x11; case '1': return 0x12;
        case '2': return 0x13; case '3': return 0x14; case '4': return 0x15;
        case '6': return 0x16; case '5': return 0x17; case '9': return 0x19;
        case '7': return 0x1A; case '8': return 0x1C; case '0': return 0x1D;
        case 'o': return 0x1F; case 'u': return 0x20; case 'i': return 0x22;
        case 'p': return 0x23; case 'l': return 0x25; case 'j': return 0x26;
        case 'k': return 0x28; case 'n': return 0x2D; case 'm': return 0x2E;
        case ' ': return 0x31;
        default: return 0xFF;
    }
}

void keyHold(uint8_t code, long frames) {
    gMem->keyEvent(code, true);
    runFrames(frames);
    gMem->keyEvent(code, false);
    runFrames(6);
}

void typeText(const char* value) {
    for (const char* p = value; *p; p++) {
        const uint8_t code = keyFor(*p);
        if (code != 0xFF) keyHold(code, 3);
    }
}

void command(uint8_t shortcut, long settle) {
    gMem->keyEvent(0x37, true);
    runFrames(6);
    keyHold(shortcut, 12);
    gMem->keyEvent(0x37, false);
    runFrames(settle);
}

long catalogCount(const std::vector<uint8_t>& img, const char* name) {
    const size_t n = std::strlen(name);
    std::vector<uint8_t> key(n + 1);
    key[0] = uint8_t(n);
    std::memcpy(key.data() + 1, name, n);
    long count = 0;
    for (size_t i = 0; i + key.size() <= img.size(); i++)
        if (img[i] == key[0] && !std::memcmp(&img[i], key.data(), key.size())) count++;
    return count;
}

struct Leg {
    std::string engine;
    bool finder = false, launched = false, progressed = false, saved = false;
    bool quit = false, halted = true, menuUp = false;
    std::string app, appAfterQuit;
    double launchMoved = 0.0;
    long scsiBoot = 0, scsiLaunch = 0, docBefore = 0, docAfter = 0, writeBlocks = 0;
    uint64_t text0 = 0, text1 = 0, text2 = 0, fp = 0, screenFp = 0;
    bool ok() const {
        return finder && launched && progressed && saved && quit && !halted && menuUp;
    }
};

constexpr const char* kDocument = "pom proof of plus";

bool runLeg(const std::vector<uint8_t>& rom, const std::string& img,
            const jit::ResolvedConfig& jitConfig, const char* label, Leg& leg) {
    MacMemory mem(pom68k::defaultCoreConfig());
    if (!mem.loadRom(rom)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return false; }
    Cpu68k cpu(mem, jitConfig);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk\n"); return false; }
    beyondboot::ensureBootDriverType(mem.scsiDisk().image());
    MacFrameClock fc;
    fc.resync(cpu);
    gMem = &mem; gCpu = &cpu; gClock = &fc;
    leg.engine = jitConfig.engineForGuest(false) == jit::EngineKind::Jit
                     ? cpu.jit().backendName() : "interp";
    const std::string ppm = std::string("compact_teachtext_") + label + "_";
    auto dumpPhase = [&](const char* phase) { dump((ppm + phase + ".ppm").c_str()); };

    // ── boot ─────────────────────────────────────────────────────────────
    runFrames(5400);
    for (int poll = 0; poll < 18 && !finderUp() && !cpu.isHalted(); poll++) {
        keyHold(0x24, 30);                      // Return: dismiss any alert
        runFrames(570);
    }
    leg.finder = finderUp() && !cpu.isHalted();
    leg.scsiBoot = mem.scsi().commands;
    dumpPhase("boot");
    std::printf("[%s] boot: Finder %s, SCSI %ld, front app '%s'\n", label,
                leg.finder ? "up" : "NOT UP", leg.scsiBoot, frontApplication().c_str());
    if (!leg.finder) return false;
    if (const char* disc = std::getenv("POM68K_TEACHTEXT_DISCOVER")) {
        // Route discovery: "x,y;x,y;…" — click each point, Cmd-O, dump.
        std::string spec = disc;
        int step = 0;
        size_t pos = 0;
        while (pos < spec.size()) {
            const size_t semi = spec.find(';', pos);
            const std::string item = spec.substr(pos, semi == std::string::npos
                                                          ? std::string::npos : semi - pos);
            pos = semi == std::string::npos ? spec.size() : semi + 1;
            int x = 0, y = 0;
            char kind = 'c';
            if (std::sscanf(item.c_str(), "%c%d,%d", &kind, &x, &y) != 3) continue;
            char name[32];
            std::snprintf(name, sizeof name, "discover%d", ++step);
            if (kind == 'h') {                  // hold: press, dump while held
                steer(x, y);
                gMem->mouseButton(true);
                runFrames(20);
                dumpPhase(name);
                gMem->mouseButton(false);
                runFrames(60);
            } else if (kind == 'd') {           // double-click
                doubleClick(x, y, 600);
                dumpPhase(name);
            } else {                            // click + Cmd-O
                click(x, y, 30);
                command(0x1F, 600);
                dumpPhase(name);
            }
            int px = 0, py = 0;
            pointer(px, py);
            std::printf("[%s] discover %d: click (%d,%d) → pointer (%d,%d), front app '%s', "
                        "SCSI %ld\n", label, step, x, y, px, py,
                        frontApplication().c_str(), mem.scsi().commands);
        }
        return false;
    }

    // ── launch: volume → Sytem Additions (sic) → TeachText ──────────────
    // Positions read off this volume's saved window layout (discovery dumps
    // of 2026-09-08): the desktop volume icon, the folder in the volume
    // window, the application in the folder window.
    const std::vector<uint32_t> beforeLaunch = screen();
    click(470, 50, 30);
    command(0x1F, 600);                         // Cmd-O: "Macintosh HD"
    dumpPhase("volume");
    click(131, 125, 30);
    command(0x1F, 600);                         // Cmd-O: "Sytem Additions"
    dumpPhase("folder");
    click(250, 192, 30);
    command(0x1F, 1200);                        // Cmd-O: TeachText
    leg.launchMoved = changed(beforeLaunch, screen());
    leg.scsiLaunch = mem.scsi().commands;
    leg.app = frontApplication();
    leg.launched = leg.app == "TeachText" && leg.launchMoved > 0.05 &&
                   leg.scsiLaunch > leg.scsiBoot;
    dumpPhase("launch");
    std::printf("[%s] launch: front app '%s', %.1f%% of the screen changed, SCSI +%ld\n",
                label, leg.app.c_str(), leg.launchMoved * 100.0,
                leg.scsiLaunch - leg.scsiBoot);
    if (!leg.launched) return false;

    // ── progress: two rounds of typing, each visible ─────────────────────
    leg.text0 = regionMaskFingerprint(0, 512, 20, 342);
    typeText("pom68k proves the macintosh plus runs teachtext ");
    runFrames(60);
    leg.text1 = regionMaskFingerprint(0, 512, 20, 342);
    typeText("and the interpreter and the jit type the same page");
    runFrames(60);
    leg.text2 = regionMaskFingerprint(0, 512, 20, 342);
    leg.progressed = leg.text1 != leg.text0 && leg.text2 != leg.text1 && menuBarUp();
    dumpPhase("typed");
    std::printf("[%s] progress: window %016llx -> %016llx -> %016llx (%s)\n", label,
                (unsigned long long)leg.text0, (unsigned long long)leg.text1,
                (unsigned long long)leg.text2, leg.progressed ? "typed" : "STATIC");

    // ── artefact: Cmd-S, name the document, Return ───────────────────────
    std::vector<uint8_t>& disk = mem.scsiDisk().image();
    leg.docBefore = catalogCount(disk, kDocument);
    const long writes0 = mem.scsiDisk().writeBlocks;
    command(0x01, 300);                         // Cmd-S → SFPutFile
    dumpPhase("savedialog");
    typeText(kDocument);                        // replaces the selected default
    runFrames(30);
    keyHold(0x24, 12);                          // Return — Save
    runFrames(900);
    leg.docAfter = catalogCount(disk, kDocument);
    leg.writeBlocks = mem.scsiDisk().writeBlocks - writes0;
    leg.saved = leg.docAfter > leg.docBefore && leg.writeBlocks > 0;
    dumpPhase("saved");
    std::printf("[%s] artefact: '%s' x%ld -> x%ld in the catalog, %ld blocks written\n",
                label, kDocument, leg.docBefore, leg.docAfter, leg.writeBlocks);

    // ── quit ─────────────────────────────────────────────────────────────
    command(0x0C, 600);                         // Cmd-Q
    leg.appAfterQuit = frontApplication();
    leg.quit = leg.appAfterQuit == "Finder";
    dumpPhase("quit");
    leg.menuUp = menuBarUp();
    leg.halted = cpu.isHalted();
    leg.fp = bench::fingerprint(cpu);
    leg.screenFp = screenFingerprint();
    std::printf("[%s] end: engine=%s front '%s' halted=%d menu=%s fp=%016llx screen=%016llx "
                "SCSI %ld\n", label, leg.engine.c_str(), leg.appAfterQuit.c_str(),
                leg.halted, leg.menuUp ? "up" : "GONE", (unsigned long long)leg.fp,
                (unsigned long long)leg.screenFp, mem.scsi().commands);
    return leg.ok();
}

}  // namespace

int main() {
    const std::string rom = testasset::find("roms/macplus.rom");
    std::string img = testasset::overrideImage();
    if (img.empty()) img = testasset::find("hdv/System 6.0.8 HD.dsk");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs roms/macplus.rom + hdv/System 6.0.8 HD.dsk\n");
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
        jit::ResolvedConfig jitLeg = chosen;
        jitLeg.engineExplicit = true;
        jitLeg.engine = jit::EngineKind::Jit;
        legs.emplace_back("interp", interp);
        legs.emplace_back("jit", jitLeg);
    }

    std::vector<Leg> results;
    bool ok = true;
    for (const auto& [label, config] : legs) {
        Leg leg;
        const bool passed = runLeg(romData, img, config, label, leg);
        results.push_back(leg);
        if (!passed) {
            std::fprintf(stderr, "FAIL: leg '%s' — finder=%d launched=%d progressed=%d "
                         "saved=%d quit=%d halted=%d menu=%d\n", label, leg.finder,
                         leg.launched, leg.progressed, leg.saved, leg.quit, leg.halted,
                         leg.menuUp);
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
        if (!same) ok = false;
    }
    std::printf("%s — Macintosh Plus TeachText application etalon\n",
                ok ? "PASSED" : "FAILED");
    return ok ? 0 : 1;
}
