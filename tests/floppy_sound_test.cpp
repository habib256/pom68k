// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// FloppySound gate: sample loading, motor spin-up→loop→down, step vs
// seek cadence classification (emulated-micros and wall-clock stamps),
// auto-motor-off (HDD proxy), mute. Soft-skips when the sample WAVs are
// absent (asset-dependent, like the boot etalons).

#include "FloppySound.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace {
int gFails = 0;
void check(bool ok, const char* what) {
    std::printf("  %-62s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) gFails++;
}

float energyOf(FloppySound& fx, int frames) {
    float peak = 0.0f;
    while (frames > 0) {
        float buf[256] = {};
        const int n = frames < 256 ? frames : 256;
        fx.fillAudioBuffer(buf, n);
        for (int i = 0; i < n; i++) peak = std::max(peak, std::fabs(buf[i]));
        frames -= n;
    }
    return peak;
}
} // namespace

int main(int argc, char** argv) {
    std::printf("floppy_sound_test — mechanical drive sounds\n");
    const std::string dir = argc > 1 ? argv[1] : "../assets/floppy_samples";

    FloppySound fx;
    fx.setSampleRate(22254);
    if (!fx.loadSamples(dir, FloppySound::FormFactor::FF35)) {
        std::printf("SKIP: samples not found in %s\n", dir.c_str());
        return 0;
    }
    check(fx.isLoaded(), "10 3.5\" samples loaded");

    // Motor: spin-up one-shot then loop — audible immediately.
    fx.motor(true, true);
    check(energyOf(fx, 4096) > 0.001f, "motor-on produces sound");
    check(fx.audioMotorOn(), "audio thread reached motor-on state");

    // Rapid steps at 6 ms emulated cadence enter seek mode, regardless
    // of wall-clock compression (turbo-safe stamps).
    uint64_t micros = 1000000;
    for (int i = 0; i < 8; i++) {
        fx.step(i, micros);
        micros += 6000;
    }
    energyOf(fx, 512);
    check(fx.audioInSeek(), "6 ms emulated cadence classifies as seek");

    // kSeekTimeoutMs without steps exits seek (landing click).
    energyOf(fx, 22254 / 4);                 // 250 ms of audio
    check(!fx.audioInSeek(), "seek retires after the timeout");

    // Isolated step (big emulated gap) stays a single click.
    fx.step(40, micros + 500000);
    energyOf(fx, 512);
    check(!fx.audioInSeek(), "isolated step is a click, not a seek");

    // Wall-clock stamps (kNoStamp): dense events classify as seek too.
    FloppySound hdd;
    hdd.setSampleRate(22254);
    if (hdd.loadSamples(dir, FloppySound::FormFactor::FF525)) {
        hdd.setAutoMotorOff(100.0);
        hdd.motor(true, true);
        for (int i = 0; i < 6; i++) {
            hdd.step(i, FloppySoundSink::kNoStamp);
            energyOf(hdd, 64);               // ~3 ms wall-clock between steps
        }
        check(hdd.audioInSeek(), "kNoStamp dense events classify as seek");
        check(hdd.audioMotorOn(), "HDD proxy motor alive during activity");
        // Idle past autoOff (100 ms) + spin-down hold (800 ms) → silent.
        energyOf(hdd, 22254);                // 1 s of audio
        check(!hdd.audioMotorOn(), "auto-motor-off retires the idle spin");
    } else {
        check(false, "5.25\" sample set loads");
    }

    // Mute produces silence but keeps time flowing.
    fx.motor(true, true);
    fx.setMuted(true);
    check(energyOf(fx, 2048) == 0.0f, "muted output is silent");

    std::printf("%s\n", gFails ? "FAILED" : "PASSED");
    return gFails ? 1 : 0;
}
