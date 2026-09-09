// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Streaming linear sample-rate converter for the GUI audio boundary.
// The guest produces samples on its own crystal (ASC-V8: 22 257 Hz); the
// callback consumes frames on the host device's native clock (commonly
// 44.1/48 kHz).  A rational phase accumulator keeps the long-term frame
// count exact and makes the result independent of guest-frame chunking.

#pragma once

#include <cstddef>
#include <cstdint>

namespace pom68k {

class HostAudioResampler {
public:
    void configure(uint32_t inputRate, uint32_t outputRate) {
        inputRate_ = inputRate;
        outputRate_ = outputRate;
        reset();
    }

    void reset() {
        primed_ = false;
        phase_ = 0;
        previousLeft_ = previousRight_ = 0.0f;
    }

    uint32_t inputRate() const { return inputRate_; }
    uint32_t outputRate() const { return outputRate_; }

    template <class Emit>
    bool pushMono(const float* samples, size_t count, Emit&& emit) {
        bool accepting = true;
        for (size_t i = 0; i < count; i++)
            push(samples[i], samples[i], [&](float left, float right) {
                if (accepting) accepting = emit(left, right);
                return accepting;
            });
        return accepting;
    }

    template <class Emit>
    bool pushStereo(const float* interleaved, size_t frames, Emit&& emit) {
        bool accepting = true;
        for (size_t i = 0; i < frames; i++)
            push(interleaved[i * 2], interleaved[i * 2 + 1],
                 [&](float left, float right) {
                     if (accepting) accepting = emit(left, right);
                     return accepting;
                 });
        return accepting;
    }

    // Feed one source frame. `emit(left, right)` returns false when its
    // destination is full. The phase still advances past the dropped output
    // frames, so a later chunk resumes at the correct point on the host clock.
    template <class Emit>
    bool push(float left, float right, Emit&& emit) {
        if (inputRate_ == 0 || outputRate_ == 0) return true;
        if (!primed_) {
            primed_ = true;
            previousLeft_ = left;
            previousRight_ = right;
            return emit(left, right);
        }

        phase_ += outputRate_;
        bool accepting = true;
        while (phase_ >= inputRate_) {
            phase_ -= inputRate_;
            if (accepting) {
                const float alpha =
                    1.0f - float(phase_) / float(outputRate_);
                const float outLeft =
                    previousLeft_ + (left - previousLeft_) * alpha;
                const float outRight =
                    previousRight_ + (right - previousRight_) * alpha;
                accepting = emit(outLeft, outRight);
            }
        }
        previousLeft_ = left;
        previousRight_ = right;
        return accepting;
    }

private:
    uint32_t inputRate_ = 22254;
    uint32_t outputRate_ = 22254;
    // Remainder in input-rate units. Adding outputRate_ once per source
    // interval yields exactly floor(intervals * output/input) host intervals.
    uint64_t phase_ = 0;
    float previousLeft_ = 0.0f;
    float previousRight_ = 0.0f;
    bool primed_ = false;
};

}  // namespace pom68k
