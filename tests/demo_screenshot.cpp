// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// M3 gate + dev tool: run the machine headless for N frames and write the
// video output as a PPM (POMIIGS screenshot pattern). With no argument it
// boots the built-in demo ROM; pass a real ROM path to trace its boot screen.
//   demo_screenshot [rom.bin] [--frames N] [--out shot.ppm]

#include "Cpu68k.h"
#include "MacMemory.h"
#include "MacVideo.h"
#include "MacFrame.h"
#include "DemoRom.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    std::string romPath, outPath = "pom68k_screen.ppm";
    long frames = 60;
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) frames = std::atol(argv[++i]);
        else if (!std::strcmp(argv[i], "--out") && i + 1 < argc) outPath = argv[++i];
        else romPath = argv[i];
    }

    MacMemory mem;
    if (!romPath.empty()) {
        std::ifstream in(romPath, std::ios::binary);
        std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
        if (rom.empty() || !mem.loadRom(rom)) {
            std::fprintf(stderr, "cannot load ROM: %s\n", romPath.c_str());
            return 1;
        }
        std::printf("ROM: %s (%zu KB)\n", romPath.c_str(), rom.size() / 1024);
    } else {
        mem.installRom(kDemoRom, kDemoRomSize);
        std::printf("ROM: built-in demo\n");
    }

    Cpu68k cpu(mem);
    mem.setCpu(&cpu);
    cpu.hardReset();
    MacFrameClock fc;
    fc.resync(cpu);
    for (long f = 0; f < frames; f++) fc.runFrame(cpu, mem);

    MacVideo video;
    const uint32_t* fb = video.render(mem);

    std::ofstream out(outPath, std::ios::binary);
    out << "P6\n" << video.width() << " " << video.height() << "\n255\n";
    long black = 0;
    for (int i = 0; i < video.width() * video.height(); i++) {
        uint8_t rgb[3] = { uint8_t(fb[i]), uint8_t(fb[i] >> 8), uint8_t(fb[i] >> 16) };
        if (!rgb[0]) black++;
        out.write(reinterpret_cast<char*>(rgb), 3);
    }
    std::printf("wrote %s after %ld frames (%ld black px)\n",
                outPath.c_str(), frames, black);

    // Gate: the demo must have painted a mixed pattern (neither all-white
    // nor all-black screen). A real ROM boot is a dev run, not a gate.
    if (romPath.empty()) {
        long total = long(video.width()) * video.height();
        if (black == 0 || black == total) {
            std::fprintf(stderr, "FAIL: screen is uniform — nothing displayed\n");
            return 1;
        }
        std::printf("demo_screenshot: pattern present, gate passed\n");
    }
    return 0;
}
