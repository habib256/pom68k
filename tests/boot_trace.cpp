// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Dev tool (not a gate): load a real Mac Plus ROM, run it with vblank +
// one-second interrupts, sample the PC to find where it settles (hot loops,
// hangs, Sad Mac), and dump the final screen as PPM. POMIIGS boot_trace
// pattern — the microscope for the M4 real-ROM milestone.
//   boot_trace <rom> [--frames N] [--out shot.ppm]

#include "Cpu68k.h"
#include "MacMemory.h"
#include "MacVideo.h"
#include "MacFrame.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    std::string romPath, diskPath, outPath = "boot_trace.ppm";
    long frames = 600;                            // ~10 s of machine time
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) frames = std::atol(argv[++i]);
        else if (!std::strcmp(argv[i], "--out") && i + 1 < argc) outPath = argv[++i];
        else if (!std::strcmp(argv[i], "--disk") && i + 1 < argc) diskPath = argv[++i];
        else romPath = argv[i];
    }
    if (romPath.empty()) { std::fprintf(stderr, "usage: boot_trace <rom> [--frames N]\n"); return 2; }

    std::ifstream in(romPath, std::ios::binary);
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    MacMemory mem;
    if (rom.empty() || !mem.loadRom(rom)) {
        std::fprintf(stderr, "cannot load ROM: %s\n", romPath.c_str());
        return 2;
    }
    uint32_t checksum = (uint32_t(rom[0]) << 24) | (uint32_t(rom[1]) << 16)
                      | (uint32_t(rom[2]) << 8) | uint32_t(rom[3]);
    std::printf("ROM %s (%zu KB), Apple checksum $%08X\n",
                romPath.c_str(), rom.size() / 1024, checksum);
    // Known Plus ROMs: v1 $4D1EEEE1, v2 $4D1EEAE1, v3 $4D1F8172 (DEV.md § ROM)

    Cpu68k cpu(mem);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!diskPath.empty()) {
        if (!mem.insertDisk(diskPath)) { std::fprintf(stderr, "bad disk: %s\n", diskPath.c_str()); return 2; }
        mem.internalDrive().debug = true;
        std::printf("disk: %s\n", diskPath.c_str());
    }
    std::printf("reset: PC=$%06X overlay=%d\n", cpu.getPC(), mem.overlay() ? 1 : 0);

    std::map<uint32_t, long> hot, diskHot;        // PC histograms (sampled)
    long overlayOffFrame = -1, frameNo = 0;
    moira::i64 base = cpu.getClock() - (cpu.getClock() % kCyclesPerFrame);
    for (long f = 0; f < frames; f++) {
        // same phase as MacFrameClock, but chunked to sample the PC
        while (cpu.getClock() < base + kVblankStart) {
            cpu.runCycles(640);
            hot[cpu.getPC()]++;
            if (mem.internalDrive().nibblesRead > 0 && mem.internalDrive().hasDisk())
                diskHot[cpu.getPC()]++;           // PCs while the disk is being read
        }
        mem.via().raiseCa1();                     // vblank at line 342
        mem.updateIrq();
        while (cpu.getClock() < base + kCyclesPerFrame) { cpu.runCycles(640); hot[cpu.getPC()]++; }
        base += kCyclesPerFrame;
        if (++frameNo % 60 == 0) mem.tickOneSecond();
        if (overlayOffFrame < 0 && !mem.overlay()) overlayOffFrame = f;
    }

    std::printf("after %ld frames (~%.1f s): PC=$%06X SR=$%04X SSP=$%08X clock=%lld\n",
                frames, double(frames) / 60.15, cpu.getPC(), cpu.getSR(),
                cpu.getISP(), (long long)cpu.getClock());
    std::printf("overlay cleared at frame %ld\n", overlayOffFrame);
    const uint8_t* r = mem.ram();
    uint32_t memTop = (uint32_t(r[0x108]) << 24) | (uint32_t(r[0x109]) << 16)
                    | (uint32_t(r[0x10A]) << 8) | uint32_t(r[0x10B]);
    std::printf("MemTop ($0108) = $%08X (%u KB detected by the ROM)\n",
                memTop, memTop / 1024);

    std::vector<std::pair<uint32_t, long>> top(hot.begin(), hot.end());
    std::sort(top.begin(), top.end(),
              [](auto& a, auto& b) { return a.second > b.second; });
    std::printf("hottest sampled PCs:\n");
    for (size_t i = 0; i < top.size() && i < 10; i++)
        std::printf("  $%06X  %ld samples\n", top[i].first, top[i].second);
    std::printf("drive: track=%d disk=%d nibbles=%ld\n",
                mem.internalDrive().currentTrack(),
                mem.internalDrive().hasDisk() ? 1 : 0,
                mem.internalDrive().nibblesRead);
    std::printf("iwm reads per reg:");
    for (int i = 0; i < 16; i++) std::printf(" %d:%ld", i, mem.iwm().readCount[i]);
    std::printf("\n  data polls=%ld hits=%ld\n", mem.iwm().dataReads, mem.iwm().dataHits);
    std::printf("  sense reads per addr:");
    for (int i = 0; i < 16; i++)
        if (mem.iwm().senseCount[i]) std::printf(" %X:%ld", i, mem.iwm().senseCount[i]);
    std::printf("\n  overwritten unread: %ld\n  last consumed nibbles:\n   ", mem.iwm().overwritten);
    for (int i = 0; i < 256; i++) {
        int p = (mem.iwm().consumedPos + 256 + i) & 511;
        std::printf(" %02X", mem.iwm().consumed[p]);
        if ((i & 31) == 31) std::printf("\n   ");
    }
    std::printf("\n");
    if (!diskHot.empty()) {
        std::vector<std::pair<uint32_t, long>> dt(diskHot.begin(), diskHot.end());
        std::sort(dt.begin(), dt.end(), [](auto& a, auto& b) { return a.second > b.second; });
        std::printf("hottest PCs while reading the disk:\n");
        for (size_t i = 0; i < dt.size() && i < 12; i++)
            std::printf("  $%06X  %ld\n", dt[i].first, dt[i].second);
    }

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
    std::printf("screen: %ld black px → %s\n", black, outPath.c_str());
    return 0;
}
