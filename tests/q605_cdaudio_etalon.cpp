// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Gate: an AUDIO CD in front of a real Mac OS. `cd_audio_test` pins the
// target against hand-written CDBs; this one puts a disc with nothing but
// music on it into a booted Finder and watches what the guest's own Apple
// CD-ROM extension does with it.
//
// The disc is SYNTHESIZED here, not found: a flat 2048-byte image cannot
// carry an audio track, and real audio discs are other people's music. Two
// tone tracks, 44.1 kHz 16-bit stereo, raw 2352-byte sectors — which is the
// only form CD-DA has.
//
// The consumer is the guest, not us: POM68K sends no commands of its own.
// What is measured is what Mac OS asks the drive, and whether the drive's
// own audio lead carries sectors as a result (CdAudioSink.h — a real
// AppleCD decodes the disc itself and sends analog to the logic board).

#include "AssetFingerprint.h"
#include "CdAudioSink.h"
#include "Cpu040.h"
#include "Q605Memory.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {
std::string findAsset(std::initializer_list<const char*> names) {
    return testasset::findAny(names);
}

uint32_t peek32(const Q605Memory& mem, uint32_t addr) {
    return uint32_t(mem.peek8(addr)) << 24 | uint32_t(mem.peek8(addr + 1)) << 16 |
           uint32_t(mem.peek8(addr + 2)) << 8 | mem.peek8(addr + 3);
}


struct Screen { int width = 0, height = 0, depth = 0; uint32_t stride = 0, offset = 0;
                std::vector<uint32_t> pixels; };

Screen decodeScreen(const Q605Memory& mem) {
    Screen s;
    uint32_t scrnBase = peek32(mem, 0x0824);
    uint32_t mainDevH = peek32(mem, 0x08A4);
    uint32_t mainDev = mainDevH ? peek32(mem, mainDevH) : 0;
    uint32_t pmapH = mainDev ? peek32(mem, mainDev + 0x16) : 0;
    uint32_t pmap = pmapH ? peek32(mem, pmapH) : 0;
    if (!pmap) return s;
    uint32_t pmBase = peek32(mem, pmap);
    uint32_t boundsA = peek32(mem, pmap + 0x06), boundsB = peek32(mem, pmap + 0x0A);
    int top = int(boundsA >> 16), left = int(boundsA & 0xFFFF);
    int bottom = int(boundsB >> 16), right = int(boundsB & 0xFFFF);
    s.width = right - left; s.height = bottom - top;
    s.depth = mem.dafbDepth(); s.stride = mem.dafbStride();
    s.offset = (pmBase ? pmBase : scrnBase) & (Q605Memory::kVramSize - 1);
    if (s.width <= 0 || s.width > 1600 || s.height <= 0 || s.height > 1200 ||
        (s.depth != 1 && s.depth != 2 && s.depth != 4 && s.depth != 8) ||
        uint64_t(s.offset) + uint64_t(s.height) * s.stride > Q605Memory::kVramSize)
        return Screen{};
    const uint8_t* vram = mem.vram();
    const uint8_t (*clut)[3] = mem.clut();
    s.pixels.resize(size_t(s.width) * s.height);
    for (int y = 0; y < s.height; y++) {
        uint32_t row = s.offset + uint32_t(y) * s.stride;
        for (int x = 0; x < s.width; x++) {
            uint8_t packed = vram[row + uint32_t(x * s.depth / 8)], pen;
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
Stats luminance(const Screen& s, int x0, int x1, int y0, int y1) {
    if (x1 > s.width) x1 = s.width;
    if (y1 > s.height) y1 = s.height;
    if (x0 >= x1 || y0 >= y1) return {};
    double sum = 0, sum2 = 0; long n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            uint32_t p = s.pixels[size_t(y) * s.width + x];
            double l = ((p >> 16) * 54 + ((p >> 8) & 0xFF) * 183 + (p & 0xFF) * 19) / 256.0;
            sum += l; sum2 += l * l; n++;
        }
    Stats r;
    if (n) { r.mean = sum / n; r.deviation = std::sqrt(sum2 / n - r.mean * r.mean); }
    return r;
}

// POM68K_CDAUDIO_DUMP=1 writes the screen for calibration by eye.
void dump(const char* name, const Screen& s) {
    if (!std::getenv("POM68K_CDAUDIO_DUMP") || s.pixels.empty()) return;
    FILE* fp = std::fopen(name, "wb");
    if (!fp) return;
    std::fprintf(fp, "P6\n%d %d\n255\n", s.width, s.height);
    for (uint32_t p : s.pixels) {
        uint8_t rgb[3] = { uint8_t(p >> 16), uint8_t(p >> 8), uint8_t(p) };
        std::fwrite(rgb, 1, 3, fp);
    }
    std::fclose(fp);
}

// Counts what the lead carries, and keeps the first sector so the gate can
// say the music is the disc's and not silence.
struct CountingLead : CdAudioSink {
    long sectors = 0, stops = 0;
    int volumeLeft = -1, volumeRight = -1;
    double peak = 0.0;
    void cdAudioSector(const uint8_t* raw) override {
        sectors++;
        for (int i = 0; i < 588; i++) {
            const auto l = int16_t(raw[i * 4] | (raw[i * 4 + 1] << 8));
            peak = std::max(peak, std::fabs(double(l)) / 32768.0);
        }
    }
    void cdAudioStopped() override { stops++; }
    void cdAudioVolume(uint8_t l, uint8_t r) override {
        volumeLeft = l; volumeRight = r;
    }
};

// A pure audio CD: two tone tracks, nothing else on the disc.
bool writeAudioDisc(const std::string& cue, const std::string& bin,
                    uint32_t secondsPerTrack) {
    std::ofstream b(bin, std::ios::binary);
    if (!b) return false;
    const uint32_t perTrack = 75 * secondsPerTrack;
    std::ofstream c(cue);
    if (!c) return false;
    const std::string leaf = bin.substr(bin.find_last_of('/') + 1);
    c << "FILE \"" << leaf << "\" BINARY\n";
    uint32_t lba = 0;
    for (int track = 0; track < 2; track++) {
        const double hz = track == 0 ? 440.0 : 660.0;
        c << "  TRACK 0" << (track + 1) << " AUDIO\n    INDEX 01 "
          << std::string(lba / (60 * 75) < 10 ? "0" : "")
          << lba / (60 * 75) << ":"
          << std::string((lba / 75) % 60 < 10 ? "0" : "") << (lba / 75) % 60
          << ":" << std::string(lba % 75 < 10 ? "0" : "") << lba % 75 << "\n";
        for (uint32_t s = 0; s < perTrack; s++, lba++) {
            uint8_t sector[2352];
            for (int f = 0; f < 588; f++) {
                const double t = double((s * 588 + f)) / 44100.0;
                const auto v = int16_t(20000.0 * std::sin(2 * M_PI * hz * t));
                sector[f * 4]     = uint8_t(v & 0xFF);
                sector[f * 4 + 1] = uint8_t((v >> 8) & 0xFF);
                sector[f * 4 + 2] = uint8_t(v & 0xFF);
                sector[f * 4 + 3] = uint8_t((v >> 8) & 0xFF);
            }
            b.write(reinterpret_cast<const char*>(sector), 2352);
        }
    }
    return bool(b) && bool(c);
}
} // namespace

int main() {
    std::string romPath = findAsset({
        "roms/1MB ROMs/1993-10 - FF7439EE - LC475,575,Quadra 605,Performa 475,476,575,577,578.ROM",
        "roms/mame/macqd605/ff7439ee.bin", "roms/quadra605.rom" });
    std::string diskPath = findAsset({ "hdv/MacOS-8.1-boot.vhd" });
    if (romPath.empty() || diskPath.empty()) {
        std::printf("SKIP: needs FF7439EE ROM + hdv/MacOS-8.1-boot.vhd\n");
        return 0;
    }
    testasset::report({ romPath, diskPath });

    const std::string cue = "q605_cdaudio.cue", bin = "q605_cdaudio.bin";
    if (!writeAudioDisc(cue, bin, 20)) {
        std::fprintf(stderr, "FAIL: could not synthesize the audio disc\n");
        return 1;
    }

    std::ifstream in(romPath, std::ios::binary);
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    // The CDB log is the measurement: what MAC OS asks the drive, opcode by
    // opcode. Injected rather than read from the environment — nothing below
    // the startup boundary may call getenv.
    pom68k::CoreConfig core = pom68k::defaultCoreConfig();
    core.storage.cdTrace = std::getenv("POM68K_CDAUDIO_CDB") != nullptr;
    Q605Memory mem(core, 32u << 20);
    if (!mem.loadRom(rom)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    if (!mem.attachScsi(diskPath, false, 6)) {
        std::fprintf(stderr, "FAIL: could not load the boot disk\n");
        return 1;
    }
    if (!mem.attachCdromEmpty(3)) {
        std::fprintf(stderr, "FAIL: could not attach the empty CD drive\n");
        return 1;
    }
    CountingLead lead;
    mem.attachCdAudioSink(&lead);

    Cpu040 cpu(mem, jit::defaultResolvedConfig(), core.cpu, core.diagnostics);
    mem.setCpu(&cpu);
    cpu.hardReset();
    while (mem.cpuHeld()) mem.tick(1000);

    constexpr int kFrameCycles = 416667;      // 25 MHz / ~60 Hz
    for (long f = 0; f < 9000 && !cpu.isHalted(); f++) cpu.runCycles(kFrameCycles);
    if (cpu.isHalted()) { std::fprintf(stderr, "FAIL: CPU halted before insert\n"); return 1; }

    const bool noInsert = std::getenv("POM68K_CDAUDIO_NOINSERT") != nullptr;
    if (!noInsert && !mem.insertBayMedia(3, cue)) {
        std::fprintf(stderr, "FAIL: the audio disc was refused by the bay\n");
        return 1;
    }
    std::printf(noInsert ? "control run: the tray stays EMPTY\n"
                        : "audio disc inserted after boot\n");
    for (long f = 0; f < 12000 && !cpu.isHalted(); f++) cpu.runCycles(kFrameCycles);
    if (cpu.isHalted()) { std::fprintf(stderr, "FAIL: CPU halted after insert\n"); return 1; }

    // ── What the guest did, with no command from us ─────────────────────
    ScsiDisk& drive = mem.scsiDiskAt(3);
    Screen screen = decodeScreen(mem);
    dump("q605_cdaudio.ppm", screen);
    if (screen.pixels.empty()) { std::fprintf(stderr, "FAIL: no PixMap\n"); return 1; }
    std::printf("%dx%d@%dbpp; %u tracks; audio state %d; lead %ld sectors "
                "(%.1f s, peak %.2f); drive volume %d/%d\n",
                screen.width, screen.height, screen.depth, drive.trackCount(),
                drive.audioState(), lead.sectors, lead.sectors / 75.0,
                lead.peak, lead.volumeLeft, lead.volumeRight);

    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %-56s %s\n", what, ok ? "ok" : "FAIL");
        if (!ok) failures++;
    };
    if (noInsert) {
        // The control arm: the same run with an EMPTY tray. Without it,
        // "the guest played the disc" cannot be told from "something in this
        // machine emits sectors whatever is in the drive".
        check(lead.sectors == 0, "an empty tray puts nothing on the audio lead");
        check(drive.audioState() == 0, "and starts no play");
        std::printf(failures ? "FAILED\n" : "PASSED — control arm silent\n");
        std::remove(cue.c_str());
        std::remove(bin.c_str());
        return failures ? 1 : 0;
    }

    check(drive.trackCount() == 2, "the drive holds the two-track audio disc");
    // The load-bearing one: MAC OS started this play. POM68K sent no PLAY
    // AUDIO — every command the drive saw came from the guest's own Apple
    // CD-ROM extension, which mounted the disc as "Audio CD 1" and played it.
    check(lead.sectors > 75 * 5,
          "the guest played the disc: seconds of audio crossed the lead");
    check(lead.peak > 0.10,
          "and it is the disc's music, not silence");
    check(drive.audioState() != 0, "the transport left its stopped state");
    check(lead.volumeLeft > 0 && lead.volumeRight > 0,
          "the guest set the drive's own level (MODE SELECT page $0E)");
    Stats menu = luminance(screen, 0, screen.width, 2, 16);
    check(menu.mean > 100.0, "and the Finder is still up behind it");

    // An audio disc has no user data, so a READ served here would mean the
    // drive handed back something that is not on the disc.
    check(drive.readCommands == 0, "and no user data was served: there is none");

    std::remove(cue.c_str());
    std::remove(bin.c_str());
    std::printf(failures ? "FAILED\n"
                         : "PASSED — Mac OS mounted the audio CD and played it\n");
    return failures ? 1 : 0;
}
