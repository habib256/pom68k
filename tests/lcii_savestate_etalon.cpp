// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Save-state gate under a REAL OS: boot the LC II to the System 7 Finder
// off a SCSI image, snapshot, run N frames of mouse-driven activity, hash
// the machine; restore the snapshot, run the SAME N frames, and require
// the identical hash. savestate_v8_test proves the same property over a
// synthetic counter-loop ROM — this etalon is what upgrades the device
// chunks (SCSI, SWIM, ASC, SCC, the Egret LLE MCU mid-transaction) from
// compile-verified to behaviour-verified: a field omitted from a visit()
// round-trips silently but diverges the moment the Finder runs on it.
//
// The scenario injects a deterministic mouse wiggle + one desktop click
// so the restored run exercises the whole autopoll→MCU→VIA-SR→ADB-Manager
// chain and cursor redraw, not just an idle spin. Injection happens at
// frame boundaries, i.e. at identical machine times in both runs.
//
// Soft-skips without the LC II ROM + a bootable hdv/ image (same assets
// as lcii_boot_etalon). Exit 0 = pass / soft-skip, 1 = fail.

#include "AssetFingerprint.h"
#include "Cpu030.h"
#include "SaveState.h"
#include "SaveStateMachines.h"
#include "V8Memory.h"
#include "V8Video.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

static std::string find(const char* rel) {
    for (const std::string base : { std::string(), std::string("../") }) {
        std::string p = base + rel;
        if (std::ifstream(p, std::ios::binary)) return p;
    }
    return {};
}

// Same DDM fixup as lcii_boot_etalon: the LC II ROM's boot scan only loads
// a driver whose ddType is $6A. In-memory patch, before any guest write, so
// the ScsiDisk write log stays rooted at the patched image.
static void ensureBootDriverType(std::vector<uint8_t>& img) {
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

static const int64_t kFrame = 640 * 407;     // 60.15 Hz @ 15.6672 MHz

// N frames of deterministic desktop activity: a bounded mouse wiggle every
// third frame and one click-release on bare desktop mid-run. Both the
// direct and the restored run call this with the machine in (supposedly)
// the same state — any device whose snapshot missed a field diverges here.
static void runScenario(V8Memory& mem, Cpu030& cpu, long frames) {
    for (long f = 0; f < frames && !cpu.isHalted(); f++) {
        if (f % 3 == 0) {
            int dx = (f / 3) % 7 - 3;        // −3…+3, sums to ~0
            int dy = (f / 5) % 5 - 2;
            mem.mouseMove(dx, dy);
        }
        if (f == frames / 2)      mem.mouseButton(true);
        if (f == frames / 2 + 12) mem.mouseButton(false);
        cpu.runCycles(kFrame);
    }
}

int main() {
    std::string rom = find("roms/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM");
    if (rom.empty()) rom = find("docs/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM");
    std::string img = find("hdv/lcii-boot.vhd");
    if (img.empty()) img = find("hdv/boot.vhd");
    if (img.empty()) img = find("hdv/GISTPERSO-boot.vhd");
    if (img.empty()) img = find("hdv/System 7.5 HD.dsk");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs the 512 KB LC II ROM + a bootable hdv/ image\n");
        return 0;
    }
    testasset::report({ rom, img });

    std::ifstream in(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
    if (romData.size() != V8Memory::kRomSize) {
        std::fprintf(stderr, "FAIL: ROM is %zu bytes, want 512 KB\n", romData.size());
        return 1;
    }

    V8Memory mem;
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    Cpu030 cpu(mem, /*withFpu=*/true);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk image\n"); return 1; }
    ensureBootDriverType(mem.scsiDisk().image());

    // ── Boot to the Finder (lcii_boot_etalon's window and signature) ────
    while (mem.cpuHeld()) mem.tick(1000);
    for (long f = 0; f < 16000 && !cpu.isHalted(); f++)
        cpu.runCycles(kFrame);
    if (cpu.isHalted()) { std::fprintf(stderr, "FAIL: CPU halted during boot\n"); return 1; }

    V8Video video(mem);
    std::vector<uint32_t> fb;
    const int W = 512;
    auto blackRatio = [&](int x0, int x1, int y0, int y1) {
        long black = 0;
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++)
                if ((fb[y * W + x] & 0xFF) < 0x80) black++;
        return double(black) / (double(x1 - x0) * (y1 - y0));
    };
    video.decode(fb);
    double menuBar = blackRatio(0, W, 2, 16);
    double desktop = blackRatio(400, W, 40, 340);
    std::printf("boot: menu bar %.2f, desktop %.2f, SCSI commands %ld\n",
                menuBar, desktop, mem.scsi().commands);
    if (!(menuBar < 0.30 && desktop > 0.35 && desktop < 0.65
          && mem.scsi().commands > 50)) {
        std::fprintf(stderr, "FAIL: no Finder to snapshot\n");
        return 1;
    }

    // ── Snapshot the live Finder ────────────────────────────────────────
    using Blob = std::vector<uint8_t>;
    Blob start;
    pom68k::save(mem, cpu, pom68k::SnapMachine::LcII, start);
    std::printf("snapshot: %zu bytes, %zu dirty SCSI block(s)\n",
                start.size(), mem.scsiDisk().dirtyBlocks());
    if (start.size() < 64) { std::fprintf(stderr, "FAIL: empty snapshot\n"); return 1; }

    // ── Direct run: N frames of activity from the snapshot state ────────
    const long kScenarioFrames = 1200;       // ≈20 s emulated
    runScenario(mem, cpu, kScenarioFrames);
    if (cpu.isHalted()) { std::fprintf(stderr, "FAIL: CPU halted (direct run)\n"); return 1; }
    Blob direct;
    pom68k::save(mem, cpu, pom68k::SnapMachine::LcII, direct);

    // ── Restore, and first require the re-save to be byte-identical ─────
    std::string err;
    if (!pom68k::load(mem, cpu, pom68k::SnapMachine::LcII,
                      start.data(), start.size(), err)) {
        std::fprintf(stderr, "FAIL: load refused its own snapshot: %s\n", err.c_str());
        return 1;
    }
    if (!err.empty()) { std::fprintf(stderr, "FAIL: load warned: %s\n", err.c_str()); return 1; }
    Blob resaved;
    pom68k::save(mem, cpu, pom68k::SnapMachine::LcII, resaved);
    if (resaved != start) {
        size_t i = 0;
        const size_t n = std::min(resaved.size(), start.size());
        while (i < n && resaved[i] == start[i]) i++;
        std::fprintf(stderr, "FAIL: load→save not byte-identical "
                     "(first divergence at byte %zu of %zu vs %zu)\n",
                     i, resaved.size(), start.size());
        return 1;
    }
    std::printf("restore: load→save byte-identical\n");

    // ── Restored run: the same N frames must produce the same machine ───
    runScenario(mem, cpu, kScenarioFrames);
    if (cpu.isHalted()) { std::fprintf(stderr, "FAIL: CPU halted (restored run)\n"); return 1; }
    Blob restored;
    pom68k::save(mem, cpu, pom68k::SnapMachine::LcII, restored);

    std::printf("determinism: direct %zu bytes (hash %016llx), "
                "restored %zu bytes (hash %016llx)\n",
                direct.size(), (unsigned long long)sav::hash(direct),
                restored.size(), (unsigned long long)sav::hash(restored));
    if (restored != direct) {
        size_t i = 0;
        const size_t n = std::min(restored.size(), direct.size());
        while (i < n && restored[i] == direct[i]) i++;
        std::fprintf(stderr, "FAIL: %ld frames after a restore diverge from the "
                     "same frames without one (first divergence at byte %zu)\n",
                     kScenarioFrames, i);
        return 1;
    }

    // The pair could match with both runs wedged — require the restored
    // machine to still be a living Finder, not just a deterministic corpse.
    video.decode(fb);
    menuBar = blackRatio(0, W, 2, 16);
    if (menuBar >= 0.30) {
        std::fprintf(stderr, "FAIL: menu bar gone after the restored run (%.2f)\n", menuBar);
        return 1;
    }

    std::printf("PASSED — restore is bit-deterministic under the Finder\n");
    return 0;
}
