// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── The Color Classic's boot chime, as SOUND, with the DFAC2 ACK-only ──
//
// The Color Classic's audio leaves the Sonora-class ASC and, on the real
// board, crosses a DFAC2 the Cuda programs over I2C. POM68K's DFAC2 is a
// bus slave that ACKs and discards (CudaLle.h: interpreting its payload
// would need register semantics still being reverse-engineered
// upstream), and no analog stage is synthesised — the samples are the
// ASC's. The TODO asked whether that is a default or a contract, "avec un
// observable invité": this gate is the observable. It pulls the ASC like
// the audio host does across power-on and judges the rendered samples
// the way lcii_asc_chime_etalon does — an audible span, tonal, bounded,
// in band, with a pitch spread. If the chime is heard with the ACK-only
// DFAC2, the machine is not mute and the contract holds; the figures are
// pinned from the first capture (POM68K_DUMP=1 writes cclassic_chime.wav).
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
    std::string rom = testasset::find("roms/cclassic.rom");
    if (rom.empty())
        rom = testasset::find("roms/1MB ROMs/1993-02 - ECD99DC0 - Color Classic.ROM");
    std::string img = testasset::overrideImage();
    if (img.empty()) img = testasset::find("hdv/System 7.5 HD.dsk");
    if (img.empty()) img = testasset::find("hdv/boot.vhd");
    if (rom.empty() || img.empty()) {
        std::printf("SKIP: needs the 1 MB Color Classic ROM + a bootable hdv/ image\n");
        return 0;
    }
    testasset::report({rom, img});
    std::ifstream in(rom, std::ios::binary);
    const std::vector<uint8_t> romData((std::istreambuf_iterator<char>(in)),
                                       std::istreambuf_iterator<char>());
    V8Memory mem(pom68k::defaultCoreConfig(), 0xA00000, V8Memory::Model::ColorClassic);
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
    const int rate = mem.asc().drainHz();   // the fixed 22 257 Hz drain, Sonora-class ASC alike
    std::vector<int16_t> samples;
    const long frames = std::getenv("POM68K_CHIME_FRAMES")
        ? std::strtol(std::getenv("POM68K_CHIME_FRAMES"), nullptr, 10) : 900;
    for (long f = 0; f < frames && !cpu.isHalted(); f++) {
        cpu.runCycles(V8Memory::kCpuHz / 60);
        while (mem.ascAvailable() > 0) samples.push_back(mem.ascPop());
    }
    if (std::getenv("POM68K_DUMP")) writeWav("cclassic_chime.wav", samples, rate);
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

    // The verdict, from the first capture on 2026-09-16 (cclassic_chime.wav,
    // scratchpad): the same shape as the LC II's — one audible span at the
    // very start, a DC park then a chord read as a 300-1 500 Hz band with a
    // spread by the zero-crossing estimator — so the same tolerances: at
    // least 600 ms of tonal windows, no span longer than 3 s, the band, a
    // spread of at least 100 Hz (measured: 1 860 ms span, RMS 12 746,
    // 1 420 ms tonal, 500-1 250 Hz). And the machine's own word on the stage:
    // no original DFAC on this model (the DFAC2 is the Cuda's I2C slave).
    long tonalWindows = 0, longest = 0;
    double pitchLo = 1e9, pitchHi = 0.0;
    for (const Span& s : spans) {
        longest = std::max<long>(longest, long(s.pitch.size()));
        for (double p : s.pitch) {
            // First capture: the window straddling the DC park and the
            // chord's onset reads one crossing (25 Hz) — an estimator
            // artefact, not a note. Tonal means at least four crossings.
            if (p < 100.0) continue;
            tonalWindows++;
            pitchLo = std::min(pitchLo, p);
            pitchHi = std::max(pitchHi, p);
        }
    }
    const bool audible = !spans.empty();
    const bool noOriginalDfac = !mem.hasOriginalDfac();
    const bool tonal = tonalWindows >= 30;
    const bool bounded = longest <= 150;
    // The Color Classic's chord opens at 500-1 250 Hz and its decay reads
    // 100-450 Hz through the crossings estimator (first capture): the band
    // is 100-1 500 Hz here, not the LC II's 300-1 500.
    const bool inBand = tonal && pitchLo >= 100.0 && pitchHi <= 1500.0;
    const bool varied = tonal && pitchHi - pitchLo >= 100.0;
    std::printf("verdict: audible=%d tonal %ld ms (want >= 600) longest %ld ms "
                "(want <= 3000) pitch %.0f-%.0f Hz (want 100-1500, spread >= 100)\n",
                audible, tonalWindows * 20, longest * 20,
                tonal ? pitchLo : 0.0, tonal ? pitchHi : 0.0);
    std::printf("DFAC2: ACK-only I2C slave, no analog stage — samples are the ASC's "
                "(original DFAC absent: %d)\n", noOriginalDfac);
    const bool ok = audible && tonal && bounded && inBand && varied && noOriginalDfac;
    std::printf("%s — Color Classic boot chime rendered through the Sonora-class ASC, DFAC2 ACK-only\n",
                ok ? "PASSED" : "FAILED");
    return ok ? 0 : 1;
}
