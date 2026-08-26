// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Deterministic core plus the GUI's wall-clock adapter for the real-time
// speed gauge. Keeping the arithmetic out of main.cpp makes it directly
// testable and gives Pi operators a scriptable measurement stream.

#pragma once

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace pom68k {

struct RealtimeReading {
    double ratio = 0.0;
    bool updated = false;
};

class RealtimeGauge {
public:
    RealtimeReading observe(long long machineClock, long long machineHz,
                            double wallSeconds) {
        if (!armed_ || machineClock < lastMachineClock_) {
            armed_ = true;
            lastMachineClock_ = machineClock;
            lastWall_ = wallSeconds;
            ratio_ = 0.0;
            return {ratio_, false};
        }
        const double dt = wallSeconds - lastWall_;
        if (dt < 0.5) return {ratio_, false};
        ratio_ = machineHz > 0
            ? double(machineClock - lastMachineClock_) / (dt * double(machineHz))
            : 0.0;
        lastMachineClock_ = machineClock;
        lastWall_ = wallSeconds;
        return {ratio_, true};
    }

private:
    long long lastMachineClock_ = 0;
    double lastWall_ = 0.0;
    double ratio_ = 0.0;
    bool armed_ = false;
};

class GuiSpeedGauge {
public:
    GuiSpeedGauge() = default;
    GuiSpeedGauge(bool logging, int skip, int count)
        : logging_(logging), skip_(std::max(0, skip)),
          remaining_(std::max(0, count)) {}

    double observe(long long machineClock, long long machineHz) {
        const double wall = std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        const RealtimeReading r = gauge_.observe(machineClock, machineHz, wall);
        if (!r.updated || !logging_) return r.ratio;
        if (skip_ > 0) {
            skip_--;
        } else {
            std::fprintf(stderr,
                         "[gui-speed] clock=%lld hz=%lld ratio=%.6f\n",
                         machineClock, machineHz, r.ratio);
            std::fflush(stderr);
            if (remaining_ > 0 && --remaining_ == 0) done_ = true;
        }
        return r.ratio;
    }

    bool done() const { return done_; }

private:
    RealtimeGauge gauge_;
    bool logging_ = false;
    int skip_ = 0;
    int remaining_ = 0;
    bool done_ = false;
};

}  // namespace pom68k
