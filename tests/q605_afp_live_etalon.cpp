// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The LIVE AppleShare gate (CHANGELOG 2026-08-28 (tenth), first green and
// calibrated 2026-09-01 (ninth)):
// a real guest EXCHANGES FILES with the in-process AFP server, end to end,
// the way a person would — no protocol-level shortcut anywhere:
//
//   boot Mac OS 8.1 → Apple menu → Chooser → AppleShare → the stack's own
//   NBP answer ("POM68K") → guest login → mount the shared volume → open
//   it on the desktop → Cmd-N → select BONJOUR.txt → Cmd-D. The proof is
//   a guest-created directory AND a byte-exact copy of both file forks,
//   transferred through LLAP/DDP/ATP/ASP/AFP over the emulated SCC.
//   `afp_server_test` proves the protocol from the
//   host side; `q605_ot_bind_etalon` proves the guest binds .MPP; this gate
//   is the missing middle — the whole wire under a user's own gestures.
//
// Chooser navigation is mouse-driven and therefore calibrated to THIS
// image (hdv/MacOS-8.1-boot.vhd, pinned by the roster) — the same accepted
// trade as the LC II launch leg. Every phase dumps a PPM under POM68K_DUMP
// and POM68K_AFP_PHASE=N stops after phase N, which is how the positions
// were calibrated in the first place. The pointer is steered CLOSED-LOOP
// on the guest's own Mouse global ($0830/$0832) — the LC II launch leg's
// lesson: open-loop stepping fights System 7's mouse scaling and loses.
// Soft-skips when the ROM or image is absent.

#include "AssetFingerprint.h"
#include "AtalkHub.h"
#include "BenchHarness.h"
#include "Cpu040.h"
#include "FinderSignature.h"
#include "JitTestConfig.h"
#include "Q605Memory.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
#include "afp_live_transfer.h"

namespace {

uint32_t peek32(const Q605Memory& mem, uint32_t addr) {
    return uint32_t(mem.peek8(addr)) << 24 | uint32_t(mem.peek8(addr + 1)) << 16 |
           uint32_t(mem.peek8(addr + 2)) << 8 | mem.peek8(addr + 3);
}

struct Screen {
    int width = 0, height = 0, depth = 0;
    uint32_t stride = 0, offset = 0;
    std::vector<uint32_t> pixels;
};

Screen decodeScreen(const Q605Memory& mem) {
    Screen s;
    uint32_t scrnBase = peek32(mem, 0x0824);
    uint32_t mainDevH = peek32(mem, 0x08A4);
    uint32_t mainDev = mainDevH ? peek32(mem, mainDevH) : 0;
    uint32_t pmapH = mainDev ? peek32(mem, mainDev + 0x16) : 0;
    uint32_t pmap = pmapH ? peek32(mem, pmapH) : 0;
    if (!pmap) return s;
    uint32_t pmBase = peek32(mem, pmap);
    uint32_t boundsA = peek32(mem, pmap + 0x06);
    uint32_t boundsB = peek32(mem, pmap + 0x0A);
    s.width = int(boundsB & 0xFFFF) - int(boundsA & 0xFFFF);
    s.height = int(boundsB >> 16) - int(boundsA >> 16);
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

void dumpPpm(const char* name, const Screen& s) {
    if (!getenv("POM68K_DUMP") || s.pixels.empty()) return;
    FILE* fp = fopen(name, "wb");
    if (!fp) return;
    fprintf(fp, "P6\n%d %d\n255\n", s.width, s.height);
    for (uint32_t p : s.pixels) {
        uint8_t rgb[3] = { uint8_t(p >> 16), uint8_t(p >> 8), uint8_t(p) };
        fwrite(rgb, 1, 3, fp);
    }
    fclose(fp);
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

} // namespace

int main() {
    std::string romPath = testasset::findAny({
        "roms/1MB ROMs/1993-10 - FF7439EE - LC475,575,Quadra 605,Performa 475,476,575,577,578.ROM",
        "roms/mame/macqd605/ff7439ee.bin",
        "roms/quadra605.rom", "roms/q605.rom" });
    std::string diskPath = testasset::findAny({
        "hdv/MacOS-8.1-boot.vhd", "hdv/q605-boot.vhd" });
    if (romPath.empty() || diskPath.empty()) {
        std::printf("SKIP: needs FF7439EE ROM + hdv/MacOS-8.1-boot.vhd\n");
        return 0;
    }
    testasset::report({ romPath, diskPath });
    std::fflush(stdout);

    const int stopPhase = getenv("POM68K_AFP_PHASE")
                        ? atoi(getenv("POM68K_AFP_PHASE")) : 99;
    const std::string outageMode = getenv("POM68K_AFP_OUTAGE") ? getenv("POM68K_AFP_OUTAGE") : "";
    if (!outageMode.empty() && outageMode != "data" && outageMode != "resource") {
        std::fprintf(stderr, "FAIL: POM68K_AFP_OUTAGE must be data or resource\n"); return 1;
    }

    // ── The share the guest will write into: fresh every run ─────────────
    // The folder's NAME is the AFP volume name (AtalkHub::folderName), so
    // the guest mounts a volume called "Echange".
    const fs::path shareRoot = fs::path("run") / "afp-live";
    const fs::path shareDir = shareRoot / "Echange";
    std::error_code ec;
    fs::remove_all(shareRoot, ec);
    fs::create_directories(shareDir, ec);
    if (!fs::is_directory(shareDir)) {
        std::fprintf(stderr, "FAIL: cannot create %s\n", shareDir.c_str());
        return 1;
    }
    if (!afplive::seed(shareDir)) {
        std::fprintf(stderr, "FAIL: cannot seed two-fork transfer fixture\n");
        return 1;
    }

    std::ifstream in(romPath, std::ios::binary);
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    if (rom.size() != Q605Memory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM is %zu bytes, want 1 MB\n", rom.size());
        return 1;
    }

    Q605Memory mem(pom68k::defaultCoreConfig(), 32u << 20);
    if (!mem.loadRom(rom) || !mem.attachScsi(diskPath)) {
        std::fprintf(stderr, "FAIL: could not load ROM/disk\n");
        return 1;
    }
    const jit::ResolvedConfig jitConfig = testjit::resolveFromEnvironment();
    Cpu040 cpu(mem, jitConfig, pom68k::defaultCoreConfig().cpu,
               pom68k::defaultCoreConfig().diagnostics);
    mem.setCpu(&cpu);

    // ── The full stack, exactly main.cpp's wiring (ot_bind's rig + AFP) ──
    AtalkHub hub;
    hub.setDefaultShareDir(shareDir.string());
    hub.setService("pap", false);
    hub.setService("macip", false);
    const int byteCycles = int(mem.cpuHz() / 28800);
    const int64_t hubHz = int64_t(byteCycles) * 28800;
    mem.scc().setByteCycles(byteCycles);
    mem.scc().setWirePace(std::max(byteCycles / 8, 64));
    mem.scc().setLosslessRx(true);
    hub.attach(mem, hubHz, nullptr);
    mem.scc().onTxFrame = [&](int ch, const uint8_t* d, size_t n) {
        if (ch != 0) return;
        if (n == 3 && d[2] == 0x84) {              // lapRTS → prompt lapCTS
            if (d[0] != 0xFF) {
                const uint8_t cts[3] = { d[1], d[0], 0x85 };
                mem.scc().injectRxFrame(
                    0, cts, 3, Scc8530::RxFrameKind::CtsReply);
            }
            return;
        }
        if (n == 3 && d[2] == 0x85) return;
        if (!outageMode.empty() && n >= 16) {
            const size_t atp = d[2] == 1 ? 8 : 16;
            if (n >= atp + 8 && d[atp - 1] == 3 && (d[atp] & 0xc0) == 0x40 &&
                (d[atp + 4] == 1 || d[atp + 4] == 4 || d[atp + 4] == 5)) {
                std::printf("ASP wire: function=%u source=%u destination=%u sid/WSS=%u\n",
                            d[atp + 4], d[atp - 2], d[atp - 3], d[atp + 5]);
                std::fflush(stdout);
            }
        }
        hub.onGuestFrame(d, n);
    };

    cpu.hardReset();
    while (mem.cpuHeld()) mem.tick(1000);

    constexpr int kFrameCycles = 416667;          // 25 MHz / ~60 Hz
    constexpr int kSlices = 64;                   // main.cpp runQuantumWithWire
    long cutAfterBytes = -1;
    bool cutObserved = false;
    auto frames = [&](long n) {
        for (long f = 0; f < n && !cpu.isHalted(); f++) {
            for (int s = 0; s < kSlices; s++) {
                cpu.runCycles(kFrameCycles / kSlices);
                hub.tick(cpu.machineClock());
                if (cutAfterBytes >= 0 && hub.snapshot().afp.bytesWritten > cutAfterBytes) {
                    const auto state = hub.snapshot().afp;
                    hub.setService("afp", false);
                    cutAfterBytes = -1;
                    cutObserved = true;
                    std::printf("outage: AFP stopped with %ld bytes written, %d open forks\n",
                                state.bytesWritten, state.openForks);
                    std::fflush(stdout);
                }
            }
        }
    };

    // Closed-loop pointer steering on the guest's own Mouse global.
    auto pointer = [&](int& x, int& y) {
        x = int16_t(uint16_t(mem.peek8(0x832)) << 8 | mem.peek8(0x833));
        y = int16_t(uint16_t(mem.peek8(0x830)) << 8 | mem.peek8(0x831));
    };
    auto steer = [&](int tx, int ty) {
        int px = 0, py = 0;
        for (int it = 0; it < 800; it++) {
            pointer(px, py);
            int dx = tx - px, dy = ty - py;
            if (!dx && !dy) break;
            auto step = [](int d) {
                int s = d / 2; if (!s) s = d > 0 ? 1 : (d < 0 ? -1 : 0);
                return std::max(-8, std::min(8, s));
            };
            mem.mouseMove(step(dx), step(dy));
            frames(1);
        }
        pointer(px, py);
        const bool ok = std::abs(px - tx) <= 2 && std::abs(py - ty) <= 2;
        if (!ok)
            std::fprintf(stderr, "steer: wanted (%d,%d), reached (%d,%d)\n",
                         tx, ty, px, py);
        return ok;
    };
    auto click = [&](int tx, int ty, int clicks = 1) {
        if (!steer(tx, ty)) return false;
        for (int c = 0; c < clicks; c++) {
            mem.mouseButton(true);
            frames(6);
            mem.mouseButton(false);
            frames(6);
        }
        frames(30);
        return true;
    };
    auto key = [&](uint8_t code, long hold = 8, long settle = 30) {
        mem.keyEvent(code, true);
        frames(hold);
        mem.keyEvent(code, false);
        frames(settle);
    };
    // Every phase boundary snaps, so every boundary also prints a trace: the
    // machine clock, the architectural fingerprint jit_bench compares across
    // engines, and the cumulative network and wire counters (wire = deepest
    // injection backlog / longest hold / frames refused at the queue cap).
    // Two runs of this image that disagree first at boundary N diverged
    // before N — how a guest-time difference between hosts is localised
    // (CHANGELOG 2026-09-11 (fourth)). Guest RAM is deliberately not digested:
    // the FPGetSrvrParms reply the guest keeps in it carries the host's wall
    // clock, so it differs between two runs of the same binary.
    auto snap = [&](const char* name) {
        Screen s = decodeScreen(mem);
        dumpPpm(name, s);
        const auto hs = hub.snapshot();
        std::printf("trace: %s clock=%lld fp=%016llx afp=%ld refused=%ld/%s "
                    "frames=%ld/%ld ddp=%ld/%ld atp=%ld dup=%ld/%ld lagmax=%ldms "
                    "wire=%zu/%ldms/%ld\n",
                    name, (long long)cpu.machineClock(),
                    (unsigned long long)bench::fingerprint(cpu), hs.afp.cmdCount,
                    hs.afp.refusedCount,
                    hs.afp.lastRefused.empty() ? "-" : hs.afp.lastRefused.c_str(),
                    hs.net.framesIn, hs.net.framesOut, hs.net.ddpIn, hs.net.ddpOut,
                    hs.net.atpReqIn, hs.net.atpDupReqs, hs.net.atpDupPending,
                    hs.net.atpDupLagMaxMs, hs.wire.backlogMax, hs.wireHoldMaxMs,
                    hs.wire.drops);
        std::fflush(stdout);
        return s;
    };

    // ── Phase 0: boot to the 8.1 Finder ──────────────────────────────────
    bool finder = false;
    for (int frame = 0; frame < 12000 && !cpu.isHalted(); frame++) {
        frames(1);
        if (frame >= 3600 && !(frame % 60) && mem.scsi().commands > 4000) {
            Screen s = decodeScreen(mem);
            if (s.width == 640 && s.height == 480 && s.depth == 8) {
                Stats m = luminanceStats(s, 0, s.width, 2, 16);
                Stats d = luminanceStats(s, 520, 630, 40, 430);
                if (m.mean > 170 && m.mean < 235 && d.mean > 100 &&
                    d.mean < 190 && m.mean - d.mean > 35) { finder = true; break; }
            }
        }
    }
    if (!finder || cpu.isHalted()) {
        std::fprintf(stderr, "FAIL: no Finder (halted=%d SCSI=%ld)\n",
                     cpu.isHalted(), mem.scsi().commands);
        return 1;
    }
    // The luminance match fires on the FIRST frame that looks like a
    // desktop — on a CLEAN volume (no dirty-mount disk check slowing the
    // boot) that is the menu bar over the default pattern, before the
    // wallpaper, the icons or even the Finder process itself (measured
    // 2026-09-01: every calibrated click then landed inside Finder
    // startup and nothing opened). So wait for the guest's own word that
    // the Finder is current, then for the desktop's right-hand strip —
    // the volume-icon column phase 7 diffs against — to stop redrawing.
    for (int poll = 0; poll < 60 && !cpu.isHalted(); poll++) {
        const std::string app = findersig::curApName(mem);
        if (app == "Finder") {
            Screen a = decodeScreen(mem);
            frames(120);
            Screen b = decodeScreen(mem);
            if (!a.pixels.empty() && a.width == 640 && b.width == 640) {
                bool stable = true;
                for (int y = 30; stable && y < 460; y++)
                    for (int x = 480; x < 640; x++)
                        if (a.pixels[size_t(y) * 640 + x] !=
                            b.pixels[size_t(y) * 640 + x]) { stable = false; break; }
                if (stable) break;
            }
        } else {
            frames(120);
        }
    }
    frames(300);
    Screen desk0 = snap("afp_live_0_finder.ppm");
    std::printf("phase 0: Finder up, SCSI=%ld, front \"%s\"\n",
                mem.scsi().commands, findersig::curApName(mem).c_str());
    std::fflush(stdout);
    if (stopPhase <= 0) return 0;

    std::vector<fs::path> verifiedCopies;
    std::vector<fs::path> interruptedCopies;
    for (int cycle = 0; cycle < 2; ++cycle) {
        const auto cycleStart = hub.snapshot();
        std::printf("connection cycle %d\n", cycle + 1);
        // ── Phase 1: Apple menu → Chooser ────────────────────────────────────
        // Positions calibrated on this image's 8.1 (see header). The Apple menu
        // drops from (10,8); the Chooser item is clicked by its row. The drop
        // is VERIFIED, not assumed: the open menu is a white panel with black
        // item text where the wallpaper has neither, and a click that lands
        // during a late Finder redraw simply gets retried.
        auto menuDropped = [&]() {
            Screen s = decodeScreen(mem);
            if (s.pixels.empty()) return false;
            long white = 0, dark = 0, total = 0;
            for (int y = 24; y < 130 && y < s.height; y++)
                for (int x = 4; x < 130 && x < s.width; x++) {
                    uint32_t p = s.pixels[size_t(y) * s.width + x];
                    double lum = ((p >> 16) * 54 + ((p >> 8) & 0xFF) * 183 +
                                  (p & 0xFF) * 19) / 256.0;
                    // Platinum menu paper is (231,231,231) — a 0xE8 cut missed
                    // every panel pixel by ONE luminance point and read an open
                    // menu as a desktop (2026-09-01). The wallpaper's brightest
                    // sky is ~210, so 0xE0 separates cleanly.
                    if (lum >= 0xE0) white++;
                    else if (lum < 0x30) dark++;
                    total++;
                }
            std::fprintf(stderr, "menu probe: white %ld dark %ld of %ld\n",
                         white, dark, total);
            return total > 0 && white > total * 55 / 100 && dark > total / 100;
        };
        bool appleOpen = false;
        for (int attempt = 0; attempt < 6 && !appleOpen; attempt++) {
            if (!click(10, 8)) { std::fprintf(stderr, "FAIL: apple menu\n"); return 1; }
            frames(60);
            appleOpen = menuDropped();
            if (!appleOpen) {
                char name[64];
                std::snprintf(name, sizeof name, "afp_live_1_attempt%d.ppm", attempt);
                snap(name);
                frames(540);
            }
        }
        snap("afp_live_1_applemenu.ppm");
        if (!appleOpen) {
            std::fprintf(stderr, "FAIL: the Apple menu never dropped\n");
            return 1;
        }
        if (stopPhase <= 1) return 0;
        // Chooser row (calibrated): itemY below. A miss leaves the menu open,
        // which the phase-2 dump makes obvious.
        const int chooserY = getenv("POM68K_AFP_CHOOSER_Y")
                           ? atoi(getenv("POM68K_AFP_CHOOSER_Y")) : 139;
        if (!click(60, chooserY)) { std::fprintf(stderr, "FAIL: chooser item\n"); return 1; }
        frames(600);                              // the DA loads from disk
        snap("afp_live_2_chooser.ppm");
        std::printf("phase 2: Chooser open\n");
        std::fflush(stdout);
        if (stopPhase <= 2) return 0;

        // ── Phase 3: AppleShare device → NBP lookup → server list ────────────
        const int asX = getenv("POM68K_AFP_AS_X") ? atoi(getenv("POM68K_AFP_AS_X")) : 88;
        const int asY = getenv("POM68K_AFP_AS_Y") ? atoi(getenv("POM68K_AFP_AS_Y")) : 84;
        if (!click(asX, asY)) { std::fprintf(stderr, "FAIL: AppleShare icon\n"); return 1; }
        frames(900);                              // NBP lookup + list fill
        snap("afp_live_3_servers.ppm");
        const long nbpLookups = hub.snapshot().net.nbpLookups - cycleStart.net.nbpLookups;
        std::printf("phase 3: server list (NBP lookups=%ld)\n", nbpLookups);
        std::fflush(stdout);
        if (nbpLookups < 1) {
            std::fprintf(stderr, "FAIL: AppleShare did not issue an NBP lookup\n");
            return 1;
        }
        if (stopPhase <= 3) return 0;

        // ── Phase 4: pick "POM68K", OK → login dialog ────────────────────────
        const int svX = getenv("POM68K_AFP_SV_X") ? atoi(getenv("POM68K_AFP_SV_X")) : 300;
        const int svY = getenv("POM68K_AFP_SV_Y") ? atoi(getenv("POM68K_AFP_SV_Y")) : 88;
        if (!click(svX, svY, 2)) { std::fprintf(stderr, "FAIL: server row\n"); return 1; }
        frames(600);
        snap("afp_live_4_login.ppm");
        std::printf("phase 4: login dialog\n");
        std::fflush(stdout);
        if (stopPhase <= 4) return 0;

        // ── Phase 5: Guest radio, OK → volume list ───────────────────────────
        const int guX = getenv("POM68K_AFP_GUEST_X") ? atoi(getenv("POM68K_AFP_GUEST_X")) : 146;
        const int guY = getenv("POM68K_AFP_GUEST_Y") ? atoi(getenv("POM68K_AFP_GUEST_Y")) : 153;
        if (!click(guX, guY)) { std::fprintf(stderr, "FAIL: guest radio\n"); return 1; }
        frames(60);
        key(0x24, 8, 90);                         // Return = Connect/OK
        frames(600);
        snap("afp_live_5_volumes.ppm");
        const int afpSessions = hub.snapshot().afp.sessions;
        std::printf("phase 5: volume list (AFP sessions=%d)\n", afpSessions);
        std::fflush(stdout);
        if (afpSessions < 1) {
            std::fprintf(stderr, "FAIL: guest login did not open an AFP session\n");
            return 1;
        }
        if (stopPhase <= 5) return 0;

        // ── Phase 6: mount, close the Chooser ────────────────────────────────
        key(0x24, 8, 90);                         // Return = OK on "Echange"
        frames(600);
        // Close box of the Chooser window (calibrated).
        const int cbX = getenv("POM68K_AFP_CLOSE_X") ? atoi(getenv("POM68K_AFP_CLOSE_X")) : 34;
        const int cbY = getenv("POM68K_AFP_CLOSE_Y") ? atoi(getenv("POM68K_AFP_CLOSE_Y")) : 40;
        if (!click(cbX, cbY)) { std::fprintf(stderr, "FAIL: chooser close\n"); return 1; }
        frames(300);
        Screen desk1 = snap("afp_live_6_mounted.ppm");
        const auto mounted = hub.snapshot();
        std::printf("phase 6: chooser closed, AFP sessions=%d mounted=%d commands=%ld\n",
                    mounted.afp.sessions, mounted.afp.volMounted,
                    mounted.afp.cmdCount);
        std::fflush(stdout);
        if (!mounted.afp.volMounted) {
            std::fprintf(stderr, "FAIL: the guest did not open the AFP volume\n");
            return 1;
        }
        if (stopPhase <= 6) return 0;

        // ── Phase 7: open the mounted volume — it is already SELECTED ────────
        // The Finder selects a freshly mounted volume (desk1 shows "Echange"
        // inverted), so Cmd-O opens it without any pixel hunt. Two icon-diff
        // schemes were tried first and both mis-clicked (densest cell:
        // "Monitors & Sound", displaced; topmost cluster: the boot volume's
        // own redraw) — the guest's selection is the one pointer the Finder
        // maintains for us. desk0/desk1 stay captured for the dumps.
        (void)desk0; (void)desk1;
        mem.keyEvent(0x37, true);                 // Cmd
        frames(12);
        key(0x1F, 75, 12);                        // O (held past Slow Keys)
        mem.keyEvent(0x37, false);
        frames(900);                              // enumerate over AFP, draw window
        snap("afp_live_7_window.ppm");
        std::printf("phase 7: Cmd-O on the selected volume\n");
        std::fflush(stdout);
        if (stopPhase <= 7) return 0;

        // ── Phase 8: Cmd-N in the volume window → a directory on the HOST ────
        std::set<std::string> before;
        for (auto& e : fs::directory_iterator(shareDir))
            before.insert(e.path().filename().string());
        mem.keyEvent(0x37, true);                 // Cmd
        frames(12);
        key(0x2D, 75, 12);                        // N (held past Slow Keys)
        mem.keyEvent(0x37, false);
        std::string created;
        for (int poll = 0; poll < 120 && created.empty(); poll++) {
            frames(30);
            for (auto& e : fs::directory_iterator(shareDir))
                if (!before.count(e.path().filename().string()) &&
                    fs::is_directory(e.path()))
                    created = e.path().filename().string();
        }
        snap("afp_live_8_created.ppm");
        auto st = hub.snapshot();
        std::printf("phase 8: host saw %s; AFP sessions=%d, mounted=%d, "
                    "commands=%ld, DDP in=%ld\n",
                    created.empty() ? "NOTHING" : ("\"" + created + "\"").c_str(),
                    st.afp.sessions, st.afp.volMounted, st.afp.cmdCount,
                    st.net.ddpIn);

        const bool ok = !cpu.isHalted() && !created.empty() &&
                        st.afp.sessions >= 1 && st.afp.volMounted &&
                        st.afp.cmdCount > mounted.afp.cmdCount;
        if (!ok) { std::fprintf(stderr, "FAIL: guest folder creation\n"); return 1; }
        if (stopPhase <= 8) return 0;

        // ── Phase 9: Finder duplicate → actual read/write of BOTH forks ──────
        // The fixture is the first icon in this calibrated Finder window. Return
        // would START editing the new folder name, not end it; click the file.
        frames(180);
        if (!click(46, 88)) { std::fprintf(stderr, "FAIL: select transfer fixture\n"); return 1; }
        frames(120);
        snap("afp_live_9_selected.ppm");
        std::set<std::string> existingCopies;
        for (const auto& entry : fs::directory_iterator(shareDir))
            existingCopies.insert(entry.path().filename().string());
        const auto transferStart = hub.snapshot().afp;
        const long retransmitsBefore = hub.snapshot().net.atpDupReqs;
        // Guest time, not host wall clock: what this emulator makes
        // deterministic is the machine's own clock, so the rate below
        // repeats run to run and is comparable with the same fixture's
        // rate over EtherTalk (q605_dayna_driver_etalon).
        const int64_t transferClock0 = cpu.machineClock();
        if (cycle == 0 && !outageMode.empty())
            cutAfterBytes = transferStart.bytesWritten +
                (outageMode == "resource" ? long(afplive::data.size()) : 0);
        mem.keyEvent(0x37, true); frames(12);
        key(0x02, 75, 12);                       // Cmd-D: Duplicate
        mem.keyEvent(0x37, false);
        if (cycle == 0 && !outageMode.empty()) {
            for (int poll = 0; poll < 18000 && !cutObserved; ++poll) frames(1);
            if (!cutObserved) { std::fprintf(stderr, "FAIL: outage was not reached\n"); return 1; }
            frames(900);
            snap("afp_outage_offline.ppm");
            const auto stopped = hub.snapshot().afp;
            const long partialBytes = stopped.bytesWritten - transferStart.bytesWritten;
            const long lower = outageMode == "resource" ? long(afplive::data.size()) : 0;
            const long upper = outageMode == "resource" ?
                long(afplive::data.size() + afplive::resource.size()) : long(afplive::data.size());
            if (stopped.sessions || stopped.openForks || stopped.volMounted ||
                partialBytes <= lower || partialBytes >= upper ||
                !afplive::exactCopy(shareDir / "BONJOUR.txt")) {
                std::fprintf(stderr, "FAIL: outage retained sessions or changed source\n"); return 1;
            }
            std::map<fs::path, std::vector<uint8_t>> partialSnapshot;
            for (const auto& entry : fs::directory_iterator(shareDir)) {
                const auto name = entry.path().filename().string();
                if (!existingCopies.count(name) && entry.is_regular_file()) {
                    if (afplive::exactCopy(entry.path())) {
                        std::fprintf(stderr, "FAIL: cut happened after a complete copy\n"); return 1;
                    }
                    const auto bytes = afplive::read(entry.path());
                    const bool phaseMatches = outageMode == "resource" ? bytes == afplive::data :
                        bytes != afplive::data && bytes.size() >= size_t(partialBytes) &&
                        std::equal(bytes.begin(), bytes.begin() + partialBytes, afplive::data.begin());
                    if (!phaseMatches) {
                        std::fprintf(stderr, "FAIL: host bytes do not match the selected cut phase\n"); return 1;
                    }
                    partialSnapshot[entry.path()] = bytes;
                    interruptedCopies.push_back(entry.path());
                    const auto sidecar = shareDir / ".AppleDouble" / name;
                    partialSnapshot[sidecar] = afplive::read(sidecar);
                }
            }
            if (partialSnapshot.empty()) {
                std::fprintf(stderr, "FAIL: no interrupted destination\n"); return 1;
            }
            hub.setService("afp", true);
            // Allow the guest ASP timeout without letting its screen saver
            // swallow the first acknowledgement key. Pointer movement is not
            // a network shortcut and leaves guest/session timers untouched.
            for (int wait = 0; wait < 250; ++wait) {
                mem.mouseMove(1, 0); frames(30);
                mem.mouseMove(-1, 0); frames(30);
            }
            snap("afp_outage_online.ppm");
            // This guest's connection-loss notification can consume a key
            // merely bringing its modal window forward. Use its actual OK.
            if (!click(318, 245)) return 1;
            frames(600);
            snap("afp_outage_first_ack.ppm");
            if (!click(464, 168)) return 1;     // Finder's follow-up copy error
            frames(600);
            snap("afp_outage_acknowledged.ppm");
            // How many alerts the Finder raises depends on where the copy was
            // when the link dropped: two when this was calibrated (the wire
            // still stalled on FCS residue), a third once it stopped — "You
            // cannot duplicate in the shared disk", its OK in the same place
            // (CHANGELOG 2026-09-11 (fourth)). Ask the guest what is in front:
            // WindowList ($9D6) → windowKind (WindowRecord +108), 2 = dialog.
            auto frontIsDialog = [&]() {
                const uint32_t w = peek32(mem, 0x09D6);
                return w && (mem.peek8(w + 108) << 8 | mem.peek8(w + 109)) == 2;
            };
            for (int more = 0; more < 3 && frontIsDialog(); ++more) {
                if (!click(464, 168)) return 1;
                frames(600);
            }
            if (frontIsDialog()) {
                std::fprintf(stderr, "FAIL: an alert is still in front after the outage\n");
                return 1;
            }
            const auto recovered = hub.snapshot().afp;
            if (recovered.sessions || recovered.openForks || recovered.volMounted ||
                recovered.bytesWritten != stopped.bytesWritten ||
                !afplive::exactCopy(shareDir / "BONJOUR.txt")) {
                std::fprintf(stderr, "FAIL: stale session wrote after outage\n"); return 1;
            }
            for (const auto& [path, bytes] : partialSnapshot)
                if (afplive::read(path) != bytes) {
                    std::fprintf(stderr, "FAIL: interrupted destination changed after restart\n"); return 1;
                }
            std::printf("outage: guest acknowledged disconnect; reconnecting through Chooser\n");
            std::fflush(stdout);
            continue;
        }
        std::string copied;
        for (int poll = 0; poll < 600 && copied.empty() && !cpu.isHalted(); ++poll) {
            frames(30);
            for (const auto& entry : fs::directory_iterator(shareDir)) {
                const auto name = entry.path().filename().string();
                if (!name.empty() && name.front() != '.' && !existingCopies.count(name) &&
                    entry.is_regular_file() && afplive::exactCopy(entry.path()) &&
                    hub.snapshot().afp.openForks == 0) copied = name;
            }
        }
        frames(120);                            // receive final replies and redraw
        snap("afp_live_9_copied.ppm");
        st = hub.snapshot();
        const long readBytes = st.afp.bytesRead - transferStart.bytesRead;
        const long writtenBytes = st.afp.bytesWritten - transferStart.bytesWritten;
        const long expected = long(afplive::data.size() + afplive::resource.size());
        const bool transferred = !cpu.isHalted() && !copied.empty() &&
            readBytes >= expected && writtenBytes >= expected && st.afp.volMounted &&
            st.afp.openForks == 0 &&
            afplive::exactCopy(shareDir / "BONJOUR.txt");
        const double transferSeconds =
            double(cpu.machineClock() - transferClock0) / double(mem.cpuHz());
        std::printf("phase 9: duplicate=\"%s\" data=%zu resource=%zu read=%ld written=%ld\n",
            copied.c_str(), afplive::data.size(), afplive::resource.size(), readBytes, writtenBytes);
        std::printf("phase 9: %ld bytes over LocalTalk in %.2f s of guest time "
                    "(%.1f KiB/s)\n", writtenBytes, transferSeconds,
                    transferSeconds > 0 ? double(writtenBytes) / 1024.0 / transferSeconds : 0.0);
        if (!transferred) {
            std::fprintf(stderr, "FAILED — guest two-fork transfer did not match the host oracle\n");
            return 1;
        }
        // The lossless wire's own health criterion (docs/APPLETALK.md § 0.4):
        // a client retransmit means a reply reached the guest too late. Before
        // the FCS-residue fix every copy here cost ~80 of them, each a stall
        // on the guest's ATP timer (llap_loop_test, CHANGELOG 2026-09-11
        // (fourth)).
        const long retransmits = st.net.atpDupReqs - retransmitsBefore;
        std::printf("phase 9: %ld client retransmissions during the copy\n", retransmits);
        if (retransmits) {
            std::fprintf(stderr, "FAILED — the guest retransmitted %ld ATP requests "
                         "during the copy\n", retransmits);
            return 1;
        }
        verifiedCopies.push_back(shareDir / copied);
        if (stopPhase <= 9) return 0;
        if (cycle == 0) {
            // Put Away the mounted volume through the guest; do not tear down
            // server sessions from the host. The second pass must log in afresh.
            frames(600);
            mem.keyEvent(0x37, true); frames(12);
            key(0x0D, 75, 12);                  // Cmd-W: close share window
            mem.keyEvent(0x37, false); frames(180);
            if (!click(590, 116)) { std::fprintf(stderr, "FAIL: select mounted volume\n"); return 1; }
            mem.keyEvent(0x37, true); frames(12);
            key(0x10, 75, 12);                  // Cmd-Y: Put Away Echange
            mem.keyEvent(0x37, false);
            for (int poll = 0; poll < 120; ++poll) {
                frames(30);
                const auto state = hub.snapshot().afp;
                if (!state.volMounted && state.sessions == 0) break;
            }
            const auto disconnected = hub.snapshot().afp;
            snap("afp_live_10_disconnected.ppm");
            std::printf("phase 10: mounted=%d sessions=%d forks=%d\n",
                disconnected.volMounted, disconnected.sessions, disconnected.openForks);
            std::fflush(stdout);
            if (disconnected.volMounted || disconnected.sessions || disconnected.openForks ||
                !afplive::exactCopy(shareDir / copied)) {
                std::fprintf(stderr, "FAIL: guest disconnect or retained transfer contents\n"); return 1;
            }
            if (stopPhase <= 10) return 0;
            frames(300);
        }
    }
    if (!interruptedCopies.empty()) {
        // Finder offsets duplicates by 16 px: the incomplete copy's icon is
        // between the source and the newly completed copy (both stay intact).
        if (interruptedCopies.size() != 1 || !click(62, 100)) return 1;
        mem.keyEvent(0x37, true); frames(12);
        key(0x33, 75, 12);                     // Cmd-Delete: remove failed copy
        mem.keyEvent(0x37, false); frames(300);
        snap("afp_outage_delete_prompt.ppm");
        key(0x24, 75, 120); frames(300);        // confirm server-side deletion
        snap("afp_outage_cleaned.ppm");
        const auto& partial = interruptedCopies.front();
        if (fs::exists(partial) || fs::exists(shareDir / ".AppleDouble" / partial.filename()) ||
            !afplive::exactCopy(shareDir / "BONJOUR.txt")) {
            std::fprintf(stderr, "FAIL: guest cleanup of interrupted copy\n"); return 1;
        }
        std::printf("outage: Finder removed the incomplete copy and its sidecar\n");
    }
    for (const auto& path : verifiedCopies)
        if (!afplive::exactCopy(path)) {
            std::fprintf(stderr, "FAIL: prior copy changed across reconnect\n"); return 1;
        }
    for (const auto& entry : fs::recursive_directory_iterator(shareDir)) {
        const auto name = entry.path().filename().string();
        if (name == ".pom68k-afp-catalog.tmp" || name == ".pom68k-afp-sidecar.tmp") {
            std::fprintf(stderr, "FAIL: abandoned AFP staging file after retry\n"); return 1;
        }
    }
    std::printf("PASSED — Finder transferred both forks after guest reconnection\n");
    return 0;
}
