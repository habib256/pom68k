// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Dev tool (not a gate): boot a Q605 volume to the Finder, drive the GUEST
// through its own Shut Down, and write the flushed image back out. The point
// is the clean-unmount bit (MDB drAtrb bit 8): a reference fixture must be
// recorded from a volume the guest itself unmounted, and no host-side tool
// can set that bit honestly (TODO § 1.1 — the 8.1 reference drifted DIRTY,
// and the assets.lock identities are unrecoverable on this host).
//
//   POM68K_SHUTDOWN_OUT=path   where the flushed image lands (required)
//   POM68K_SHUTDOWN_MODE       power (default) | menu
//   POM68K_SHUTDOWN_PHASE=N    stop after phase N (calibration, like the
//                              AFP gate); PPMs land under POM68K_DUMP
//   POM68K_SHUTDOWN_SPECIAL_X / POM68K_SHUTDOWN_ITEM_Y
//                              menu-mode click targets (Special title /
//                              Shut Down row), calibrated off the phase-1
//                              dump exactly like q605_afp_live_etalon
//
// `power` presses the ADB power key ($7F) and accepts the caution dialog
// with Return; `menu` walks Special → Shut Down. Success is the CLEAN bit
// appearing in the in-memory image — Cuda's power-down is ack-only here, so
// the machine never halts and the bit is the only honest observable.
//
// Boot detection, screen decoding and closed-loop pointer steering are
// q605_afp_live_etalon.cpp's, unchanged.

#include "AssetFingerprint.h"
#include "AtalkHub.h"
#include "Cpu040.h"
#include "FinderSignature.h"
#include "JitTestConfig.h"
#include "Q605Memory.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

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
    const char* outPath = getenv("POM68K_SHUTDOWN_OUT");
    const int stopPhase = getenv("POM68K_SHUTDOWN_PHASE")
                        ? atoi(getenv("POM68K_SHUTDOWN_PHASE")) : 99;
    if (!outPath && stopPhase > 2) {
        std::fprintf(stderr, "usage: POM68K_SHUTDOWN_OUT=<flushed image> "
                             "[POM68K_SHUTDOWN_MODE=power|menu] "
                             "q605_shutdown_flush\n");
        return 2;
    }
    const std::string mode = getenv("POM68K_SHUTDOWN_MODE")
                           ? getenv("POM68K_SHUTDOWN_MODE") : "power";

    std::string romPath = testasset::findAny({
        "roms/1MB ROMs/1993-10 - FF7439EE - LC475,575,Quadra 605,Performa 475,476,575,577,578.ROM",
        "roms/mame/macqd605/ff7439ee.bin",
        "roms/quadra605.rom", "roms/q605.rom" });
    std::string diskPath = getenv("POM68K_SHUTDOWN_IMG")
                         ? testasset::find(getenv("POM68K_SHUTDOWN_IMG"))
                         : testasset::find("hdv/MacOS-8.1-boot.vhd");
    if (romPath.empty() || diskPath.empty()) {
        std::fprintf(stderr, "FAIL: needs FF7439EE ROM + boot image\n");
        return 1;
    }
    testasset::report({ romPath, diskPath });

    // The boot volume's MDB, located in the FILE: attachScsi() loads the
    // image bytes verbatim, so the same offset addresses the in-memory copy.
    const testasset::HfsInfo hfs = testasset::probeHfs(diskPath);
    if (!hfs.found) {
        std::fprintf(stderr, "FAIL: no HFS MDB in %s\n", diskPath.c_str());
        return 1;
    }
    std::printf("volume '%s' atrb=$%04X (%s) MDB@%llu\n", hfs.name.c_str(),
                hfs.atrb, hfs.cleanlyUnmounted() ? "clean" : "DIRTY",
                (unsigned long long)hfs.mdbOffset);
    std::fflush(stdout);

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

    // POM68K_SHUTDOWN_HUB=1 wires q605_afp_live_etalon's full AppleTalk
    // stack (verbatim) so a rig difference between the two harnesses can
    // be bisected. Not a product mode.
    std::unique_ptr<AtalkHub> hub;
    if (getenv("POM68K_SHUTDOWN_HUB")) {
        namespace fs = std::filesystem;
        const fs::path shareDir = fs::path("run") / "shutdown-probe" / "Echange";
        std::error_code ec;
        fs::create_directories(shareDir, ec);
        hub = std::make_unique<AtalkHub>();
        hub->setDefaultShareDir(shareDir.string());
        hub->setService("pap", false);
        hub->setService("macip", false);
        const int byteCycles = int(mem.cpuHz() / 28800);
        const int64_t hubHz = int64_t(byteCycles) * 28800;
        mem.scc().setByteCycles(byteCycles);
        mem.scc().setWirePace(std::max(byteCycles / 8, 64));
        mem.scc().setLosslessRx(true);
        hub->attach(mem, hubHz, nullptr);
        mem.scc().onTxFrame = [&mem, h = hub.get()](int ch, const uint8_t* d, size_t n) {
            if (ch != 0) return;
            if (n == 3 && d[2] == 0x84) {          // lapRTS → express lapCTS
                if (d[0] != 0xFF) {
                    const uint8_t cts[3] = { d[1], d[0], 0x85 };
                    mem.scc().injectRxFrame(0, cts, 3, true);
                }
                return;
            }
            if (n == 3 && d[2] == 0x85) return;
            h->onGuestFrame(d, n);
        };
    }

    cpu.hardReset();
    while (mem.cpuHeld()) mem.tick(1000);

    constexpr int kFrameCycles = 416667;          // 25 MHz / ~60 Hz
    // POM68K_SHUTDOWN_SLICED=1 reproduces q605_afp_live_etalon's 64-slice
    // frame quantum (its runQuantumWithWire cadence); with the hub wired
    // it also ticks it per slice, exactly the gate's frames().
    const bool sliced = getenv("POM68K_SHUTDOWN_SLICED") || hub;
    auto frames = [&](long n) {
        for (long f = 0; f < n && !cpu.isHalted(); f++) {
            if (sliced)
                for (int s = 0; s < 64; s++) {
                    cpu.runCycles(kFrameCycles / 64);
                    if (hub) hub->tick(cpu.machineClock());
                }
            else
                cpu.runCycles(kFrameCycles);
        }
    };
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
    auto click = [&](int tx, int ty) {
        if (!steer(tx, ty)) return false;
        mem.mouseButton(true);
        frames(6);
        mem.mouseButton(false);
        frames(36);
        return true;
    };
    auto key = [&](uint8_t code, long hold = 8, long settle = 30) {
        mem.keyEvent(code, true);
        frames(hold);
        mem.keyEvent(code, false);
        frames(settle);
    };
    auto snap = [&](const char* name) {
        Screen s = decodeScreen(mem);
        dumpPpm(name, s);
        return s;
    };
    // drAtrb is big-endian at MDB+10; bit 8 ($0100, "cleanly unmounted")
    // lives in bit 0 of its HIGH byte.
    auto cleanBit = [&]() {
        const std::vector<uint8_t>& img = mem.scsiDisk().image();
        if (hfs.mdbOffset + 12 > img.size()) return false;
        return (img[hfs.mdbOffset + 10] & 0x01) != 0;
    };

    // ── Phase 0: boot to the Finder (the AFP gate's detector) ────────────
    bool finder = false;
    for (int frame = 0; frame < 18000 && !cpu.isHalted(); frame++) {
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
    // On a CLEAN volume the luminance match fires before the Finder has
    // even launched (q605_afp_live_etalon, 2026-09-01): wait for the
    // guest's own CurApName and a desktop that stopped redrawing before
    // aiming a menu click at it.
    for (int poll = 0; poll < 60 && !cpu.isHalted(); poll++) {
        if (findersig::curApName(mem) == "Finder") {
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
    frames(600);                                  // let the boot writes settle
    snap("shutdown_0_finder.ppm");
    std::printf("phase 0: Finder up, SCSI=%ld, clean=%d\n",
                mem.scsi().commands, cleanBit());
    std::fflush(stdout);
    if (stopPhase <= 0) return 0;

    // ── Phase 1: the shutdown gesture ────────────────────────────────────
    if (mode == "power") {
        // ADB power key; the caution dialog's default button is Shut Down.
        key(0x7F, 30, 240);
        snap("shutdown_1_power.ppm");
        key(0x24, 8, 120);                        // Return = default button
    } else {
        const int spX = getenv("POM68K_SHUTDOWN_SPECIAL_X")
                      ? atoi(getenv("POM68K_SHUTDOWN_SPECIAL_X")) : 163;
        if (!click(spX, 8)) { std::fprintf(stderr, "FAIL: Special menu\n"); return 1; }
        frames(30);
        snap("shutdown_1_menu.ppm");
        if (stopPhase <= 1) return 0;
        const int itY = getenv("POM68K_SHUTDOWN_ITEM_Y")
                      ? atoi(getenv("POM68K_SHUTDOWN_ITEM_Y")) : 118;
        if (!click(spX + 12, itY)) { std::fprintf(stderr, "FAIL: Shut Down item\n"); return 1; }
    }
    snap("shutdown_1_gesture.ppm");
    std::printf("phase 1: shutdown gesture sent (%s)\n", mode.c_str());
    std::fflush(stdout);
    if (stopPhase <= 1) return 0;

    // ── Phase 2: wait for the guest's own flush to land the clean bit ────
    long waited = 0;
    for (; waited < 14400 && !cleanBit() && !cpu.isHalted(); waited += 60)
        frames(60);
    snap("shutdown_2_done.ppm");
    const std::vector<uint8_t>& img = mem.scsiDisk().image();
    const uint16_t atrb = hfs.mdbOffset + 12 <= img.size()
        ? uint16_t(uint16_t(img[hfs.mdbOffset + 10]) << 8 | img[hfs.mdbOffset + 11])
        : 0;
    std::printf("phase 2: %ld frames, atrb=$%04X (%s), halted=%d, writes=%ld\n",
                waited, atrb, (atrb & 0x0100) ? "CLEAN" : "still dirty",
                cpu.isHalted(), mem.scsiDisk().writeBlocks);
    std::fflush(stdout);
    if (!(atrb & 0x0100)) {
        std::fprintf(stderr, "FAIL: the guest never landed the clean bit\n");
        return 1;
    }
    if (stopPhase <= 2) return 0;

    // ── Phase 3: write the flushed image ─────────────────────────────────
    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(img.data()),
              std::streamsize(img.size()));
    if (!out) {
        std::fprintf(stderr, "FAIL: cannot write %s\n", outPath);
        return 1;
    }
    out.close();
    std::printf("PASSED — flushed '%s' (%zu bytes) to %s\n",
                hfs.name.c_str(), img.size(), outPath);
    return 0;
}
