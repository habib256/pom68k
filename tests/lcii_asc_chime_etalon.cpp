// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── The LC II's boot chime, as SOUND ──
//
// `asc_test` pins the ASC-V8's registers, FIFO status and IRQ against MAME;
// none of that says whether a listener hears anything (TODO § C.3). This
// gate pulls the ASC's output ring exactly as the audio host does — every
// frame, at the fixed 22 257 Hz drain — across the ROM's power-on sequence,
// and judges the rendered samples: an audible span exists (RMS above the
// silence floor for long enough to be a chime, not a click), its pitch —
// zero crossings per window — sits where the ROM puts it, and the pitch is
// not flat across the span if the chime is a chord or a sweep. The exact
// figures were read off the first capture, not assumed
// (scratchpad/2026-09-07/audio/), and are pinned as tolerances.
// POM68K_DUMP=1 writes the captured samples as lcii_chime.wav.

#include "AssetFingerprint.h"
#include "Cpu030.h"
#include "JitTestConfig.h"
#include "V8Memory.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct Span {
    long first = 0, last = 0;     // sample indices
    double rms = 0.0;
    std::vector<double> pitch;    // Hz per 20 ms window
};

void writeWav(const char* path, const std::vector<int16_t>& s, int rate) {
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return;
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    const uint32_t bytes = uint32_t(s.size() * 2);
    std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f); u32(16); u16(1); u16(1); u32(uint32_t(rate));
    u32(uint32_t(rate) * 2); u16(2); u16(16);
    std::fwrite("data", 1, 4, f); u32(bytes);
    std::fwrite(s.data(), 2, s.size(), f);
    std::fclose(f);
}

}  // namespace

int main() {
    const std::string rom =
        testasset::find("roms/512KB ROMs/1992-03 - 35C28F5F - Mac LC II.ROM");
    std::string img = testasset::overrideImage();
    if (img.empty()) img = testasset::find("hdv/GISTPERSO-boot.vhd");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs the LC II ROM + hdv/GISTPERSO-boot.vhd\n");
        return 0;
    }
    testasset::report({rom, img});
    std::ifstream in(rom, std::ios::binary);
    const std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                       std::istreambuf_iterator<char>());
    V8Memory mem(pom68k::defaultCoreConfig());
    if (!mem.loadRom(romData)) { std::fprintf(stderr, "FAIL: bad ROM\n"); return 1; }
    const jit::ResolvedConfig jitConfig = testjit::resolveFromEnvironment();
    Cpu030 cpu(mem, jitConfig, pom68k::defaultCoreConfig().cpu,
               /*withFpu=*/true);
    mem.setCpu(&cpu);
    cpu.hardReset();
    if (!mem.attachScsi(img)) { std::fprintf(stderr, "FAIL: bad disk\n"); return 1; }
    while (mem.cpuHeld()) mem.tick(1000);

    // Pull the ring every frame, like the host's audio callback: the ASC
    // drops what nobody consumes, so a sparser pull would lose the chime.
    const int rate = mem.asc().drainHz();
    std::vector<int16_t> samples;
    const long frames = std::getenv("POM68K_CHIME_FRAMES")
        ? std::strtol(std::getenv("POM68K_CHIME_FRAMES"), nullptr, 10) : 900;
    for (long f = 0; f < frames && !cpu.isHalted(); f++) {
        cpu.runCycles(V8Memory::kCpuHz / 60);
        while (mem.asc().available() > 0) samples.push_back(mem.asc().pop());
    }
    if (std::getenv("POM68K_DUMP")) writeWav("lcii_chime.wav", samples, rate);
    std::printf("capture: %zu samples at %d Hz over %ld frames (%.2f s of "
                "audio), halted=%d\n", samples.size(), rate, frames,
                double(samples.size()) / rate, cpu.isHalted());

    // 20 ms windows: RMS and zero-crossing pitch.
    const int win = rate / 50;
    std::vector<double> rms, zcr;
    for (size_t i = 0; i + size_t(win) <= samples.size(); i += size_t(win)) {
        double acc = 0.0;
        int crossings = 0;
        for (int k = 0; k < win; k++) {
            const double v = samples[i + size_t(k)];
            acc += v * v;
            if (k > 0 && ((samples[i + size_t(k) - 1] < 0) != (v < 0))) crossings++;
        }
        rms.push_back(std::sqrt(acc / win));
        zcr.push_back(crossings * 25.0);      // crossings/2 per 20 ms → Hz
    }
    const double floorRms = 64.0;             // the ring idles at exactly 0
    std::vector<Span> spans;
    for (size_t w = 0; w < rms.size(); w++) {
        if (rms[w] <= floorRms) continue;
        if (spans.empty() || spans.back().last + 2 < long(w)) {
            spans.push_back({});
            spans.back().first = long(w);
        }
        spans.back().last = long(w);
        spans.back().rms += rms[w];
        spans.back().pitch.push_back(zcr[w]);
    }
    for (Span& s : spans) s.rms /= double(s.pitch.size());
    std::printf("spans: %zu audible span(s) above RMS %.0f\n", spans.size(),
                floorRms);
    for (const Span& s : spans) {
        double lo = 1e9, hi = 0.0;
        for (double p : s.pitch) { lo = std::min(lo, p); hi = std::max(hi, p); }
        std::printf("  %.2f s .. %.2f s (%zu ms) rms %.0f pitch %.0f-%.0f Hz:",
                    s.first * 0.02, (s.last + 1) * 0.02,
                    s.pitch.size() * 20, s.rms, lo, hi);
        for (size_t i = 0; i < s.pitch.size() && i < 24; i++)
            std::printf(" %.0f", s.pitch[i]);
        std::printf("%s\n", s.pitch.size() > 24 ? " …" : "");
    }

    // The verdict, from the first capture (scratchpad/2026-09-07/audio/
    // chime_probe1.log, lcii_chime.wav): one audible span of 1 160 ms at
    // the very start of the boot, RMS ≈ 13 000. Its first 340 ms carry a
    // constant level with no zero crossing — the ROM parks the FIFO on a
    // DC value before the chord starts — and the tonal part that follows
    // reads 500–1 125 Hz through the zero-crossing estimator: the LC II's
    // chime is a chord, so the crossings count mixes its notes, and what
    // the estimator can honestly assert is a band and a spread, not one
    // frequency. So: at least 600 ms of TONAL windows (crossings present),
    // no span longer than 3 s (a chime, not a stuck oscillator), the tonal
    // pitch inside 300–1 500 Hz, and a spread of at least 100 Hz between
    // windows — the "variation de hauteur" the item asks for.
    long tonalWindows = 0, longest = 0;
    double pitchLo = 1e9, pitchHi = 0.0;
    for (const Span& s : spans) {
        longest = std::max<long>(longest, long(s.pitch.size()));
        for (double p : s.pitch) {
            if (p <= 0.0) continue;
            tonalWindows++;
            pitchLo = std::min(pitchLo, p);
            pitchHi = std::max(pitchHi, p);
        }
    }
    const bool audible = !spans.empty();
    const bool tonal = tonalWindows >= 30;
    const bool bounded = longest <= 150;
    const bool inBand = tonal && pitchLo >= 300.0 && pitchHi <= 1500.0;
    const bool varied = tonal && pitchHi - pitchLo >= 100.0;
    std::printf("verdict: audible=%d tonal %ld ms (want >= 600) longest %ld ms "
                "(want <= 3000) pitch %.0f-%.0f Hz (want 300-1500, spread >= 100)\n",
                audible, tonalWindows * 20, longest * 20,
                tonal ? pitchLo : 0.0, tonal ? pitchHi : 0.0);
    const bool ok = audible && tonal && bounded && inBand && varied;
    std::printf("%s — LC II boot chime rendered through the ASC\n",
                ok ? "PASSED" : "FAILED");
    return ok ? 0 : 1;
}
