// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Host-clock audio gate: exact rational frame count, chunk independence and
// ten minutes of ASC-V8 tempo at a native 48 kHz device rate.

#include "HostAudioResampler.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {
int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-64s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}
}

int main() {
    std::printf("audio_resampler_test — guest crystal to host DAC clock\n");

    {
        pom68k::HostAudioResampler r;
        r.configure(22257, 22257);
        const float input[] = {0, 0, 1, -1, 2, -2, 3, -3, 4, -4};
        std::vector<float> out;
        r.pushStereo(input, 5, [&](float left, float right) {
                out.push_back(left);
                out.push_back(right);
                return true;
            });
        check(out.size() == 10 && out[6] == 3.0f && out[7] == -3.0f,
              "equal-rate conversion preserves every stereo frame exactly");
    }

    // Feeding source frames in irregular GUI quanta must be byte-identical to
    // one continuous feed. The converter carries interpolation and rational
    // phase across calls; no video-frame boundary may become an audio seam.
    {
        constexpr int kIn = 22257, kOut = 48000, kFrames = 3 * kIn;
        std::vector<float> input(kFrames);
        for (int i = 0; i < kFrames; i++)
            input[size_t(i)] = float((i % 257) - 128) / 128.0f;
        auto render = [&](bool chunked) {
            pom68k::HostAudioResampler r;
            r.configure(kIn, kOut);
            std::vector<float> out;
            size_t pos = 0;
            while (pos < input.size()) {
                const size_t count = chunked
                    ? std::min(input.size() - pos, size_t(317 + pos % 71))
                    : input.size();
                r.pushMono(input.data() + pos, count,
                    [&](float left, float right) {
                        out.push_back(left);
                        out.push_back(right);
                        return true;
                    });
                pos += count;
            }
            return out;
        };
        const std::vector<float> continuous = render(false);
        const std::vector<float> chunked = render(true);
        check(continuous == chunked,
              "irregular GUI-frame chunks produce an identical host stream");
    }

    // Ten minutes is long enough for the old 22 254-vs-22 257 assumption to
    // accumulate a plainly measurable tempo error. Verify both duration and a
    // non-bin-centred 997 Hz tone after conversion to the usual 48 kHz clock.
    {
        constexpr uint32_t kIn = 22257, kOut = 48000;
        constexpr uint64_t kSeconds = 600;
        constexpr double kTone = 997.0;
        constexpr double kPi = 3.14159265358979323846;
        const uint64_t sourceFrames = uint64_t(kIn) * kSeconds;
        const uint64_t expected = 1 + (sourceFrames - 1) * kOut / kIn;

        pom68k::HostAudioResampler r;
        r.configure(kIn, kOut);
        uint64_t outputFrames = 0;
        uint64_t crossings = 0;
        float previous = 0.0f;
        bool havePrevious = false;
        uint64_t sourceAt = 0, quantum = 0;
        std::vector<float> guestFrame;
        guestFrame.reserve(371);
        while (sourceAt < sourceFrames) {
            // V8 GUI quanta carry about 370 source samples. The occasional
            // 371-sample buffer models the fractional crystal/video phase.
            const size_t count = size_t(std::min<uint64_t>(
                370 + ((quantum++ % 20) == 19), sourceFrames - sourceAt));
            guestFrame.clear();
            for (size_t i = 0; i < count; i++) {
                const uint64_t at = sourceAt + i;
                guestFrame.push_back(float(std::sin(
                    2.0 * kPi * kTone * double(at) / double(kIn))));
            }
            r.pushMono(guestFrame.data(), guestFrame.size(),
                [&](float left, float) {
                    if (havePrevious && ((previous < 0.0f) != (left < 0.0f)))
                        crossings++;
                    previous = left;
                    havePrevious = true;
                    outputFrames++;
                    return true;
                });
            sourceAt += count;
        }
        const double sourceDuration = double(sourceFrames - 1) / kIn;
        const double hostDuration = double(outputFrames - 1) / kOut;
        const double measuredHz = double(crossings) / (2.0 * hostDuration);
        check(outputFrames == expected,
              "ten-minute output frame count follows the native host clock");
        check(std::abs(hostDuration - sourceDuration) < 1.0 / kOut,
              "ten-minute guest and host timelines differ by under one frame");
        check(std::abs(measuredHz - kTone) < 0.02,
              "ten-minute resampled tone keeps its pitch within 0.02 Hz");
        std::printf("  600 s: %llu source -> %llu host frames, tone %.5f Hz\n",
                    (unsigned long long)sourceFrames,
                    (unsigned long long)outputFrames, measuredHz);
    }

    std::printf("%s\n", gFails ? "FAILED" : "PASSED");
    return gFails ? 1 : 0;
}
