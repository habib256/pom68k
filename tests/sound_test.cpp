// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// M6 gate: the startup chime. Boot the ROM, capture ~1 s of the PWM sound
// buffer through MacAudio, and verify it's a real decaying tone — not
// silence, not noise: a dominant frequency in the audible range whose
// energy fades over time (the Mac "bong"). Writes chime.wav for listening.
// Soft-skips without roms/macplus.rom.

#include "Cpu68k.h"
#include "MacMemory.h"
#include "MacAudio.h"
#include "MacFrame.h"
#include <cstdio>
#include <cmath>
#include <cstdint>
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

static void writeWav(const std::string& path, const std::vector<float>& s, int rate) {
    std::ofstream o(path, std::ios::binary);
    auto u32 = [&](uint32_t v){ o.put(v).put(v>>8).put(v>>16).put(v>>24); };
    auto u16 = [&](uint16_t v){ o.put(v).put(v>>8); };
    uint32_t dataBytes = uint32_t(s.size() * 2);
    o.write("RIFF", 4); u32(36 + dataBytes); o.write("WAVE", 4);
    o.write("fmt ", 4); u32(16); u16(1); u16(1); u32(rate); u32(rate*2); u16(2); u16(16);
    o.write("data", 4); u32(dataBytes);
    for (float f : s) { int v = int(f * 32767); if(v>32767)v=32767; if(v<-32768)v=-32768; u16(uint16_t(v)); }
}

#define CHECK(c, ...) do { if(!(c)){ std::fprintf(stderr,"FAIL: " __VA_ARGS__); std::fprintf(stderr,"\n"); return 1; } } while(0)

int main() {
    std::string rom = find("roms/macplus.rom");
    if (rom.empty()) { std::printf("SKIP: roms/macplus.rom not found\n"); return 0; }
    std::ifstream in(rom, std::ios::binary);
    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    MacMemory mem; mem.loadRom(romData);
    Cpu68k cpu(mem); mem.setCpu(&cpu); cpu.hardReset();
    MacFrameClock fc; fc.resync(cpu);
    MacAudio audio;

    std::vector<float> samples;
    for (long f = 0; f < 90; f++) {                 // ~1.5 s
        fc.runFrame(cpu, mem);
        audio.renderFrame(mem, samples);
    }
    writeWav("chime.wav", samples, int(MacAudio::kSampleRate));

    // Energy: RMS of the first ~0.3 s (chime) vs the tail (~1.2 s, decayed).
    auto rms = [&](size_t a, size_t b) {
        double s = 0; for (size_t i = a; i < b && i < samples.size(); i++) s += samples[i]*samples[i];
        return std::sqrt(s / double(b - a));
    };
    double head = rms(0, 6600), tail = rms(samples.size() - 6600, samples.size());
    std::printf("samples=%zu  head RMS=%.4f  tail RMS=%.4f\n", samples.size(), head, tail);
    CHECK(head > 0.02, "chime is audible (head RMS)");
    CHECK(head > tail * 1.5, "chime decays (head louder than tail)");

    // Dominant frequency by zero-crossing rate over the loud head.
    int crossings = 0;
    for (size_t i = 1; i < 6600 && i < samples.size(); i++)
        if ((samples[i-1] < 0) != (samples[i] < 0)) crossings++;
    double freq = crossings / 2.0 / (6600.0 / MacAudio::kSampleRate);
    std::printf("dominant frequency ~%.0f Hz\n", freq);
    CHECK(freq > 200 && freq < 4000, "tone in the beep range (200-4000 Hz)");

    std::printf("sound_test: startup chime captured (chime.wav), gate passed\n");
    return 0;
}
