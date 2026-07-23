// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// FloppySound — mechanical sounds for the Sony 3.5" drives and (as a
// coarse proxy) the SCSI hard disks: head step/seek, spindle spin-up /
// loop / spin-down, insert-eject click. Port of MAME's
// `floppy.cpp::floppy_sound_device` via POM2's FloppySoundDevice,
// sample-based playback from MAME's `samples/floppy/` WAV set
// (BSD-3-Clause; see assets/floppy_samples/README.txt).
//
// Threading: the emulator (or GUI) thread posts events through the
// FloppySoundSink API into a mutex-guarded command queue; MacAudioHost's
// miniaudio callback pulls fillAudioBuffer(). Events are rare (<100/s
// during a full seek), so a lock-free ring would be over-engineered.
//
// Step / seek decision (MAME parity, floppy.cpp floppy_sound_device):
//   * First step OR gap > kSeekJoinMs since the last one → single
//     `step_1_1` click.
//   * Rapid steps → seek mode: pick the seek sample whose nominal
//     cadence (2/6/12/20 ms) best matches, pitch-scaled to the observed
//     rate. Cadence is measured in **emulated** microseconds when the
//     caller stamps events (SonyDrive), or wall-clock audio frames for
//     kNoStamp callers (ScsiDisk).
//   * kSeekTimeoutMs without steps → exit seek, land with `step_1_1`.
//
// Additions over POM2: setAutoMotorOff() — the HDD proxy has no real
// motor line, so the device retires its own spin loop after an idle
// window; step events keep it alive.

#pragma once
#include "FloppySoundSink.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

class FloppySound : public FloppySoundSink {
public:
    enum class FormFactor { FF35, FF525 };

    FloppySound() = default;
    ~FloppySound() override = default;

    /// Load the 10 WAVs for `ff` from `dir` (assets/floppy_samples).
    /// On partial failure the device degrades gracefully (silence).
    bool loadSamples(const std::string& dir, FormFactor ff);
    bool isLoaded() const { return samplesLoaded_.load(std::memory_order_acquire); }

    /// Host DAC rate (MacAudioHost runs 22 254 Hz). Sources resample on
    /// the fly (linear interpolation) from their native 44.1 kHz.
    void setSampleRate(uint32_t hz) { if (hz) outputSampleRate_ = hz; }

    // ── FloppySoundSink (emulator thread) ───────────────────────────
    void motor(bool on, bool withDisk) override;
    void step(int newTrack, uint64_t emuMicros) override;
    void click() override;

    // ── UI thread ───────────────────────────────────────────────────
    void  setVolume(float v);
    float getVolume() const { return volume_.load(std::memory_order_relaxed); }
    void  setMuted(bool m) { muted_.store(m, std::memory_order_relaxed); }
    bool  isMuted() const { return muted_.load(std::memory_order_relaxed); }
    /// >0: silence the spin loop after this many ms without a step
    /// event (HDD proxy — no explicit motor-off ever arrives).
    void setAutoMotorOff(double ms) { autoOffMs_ = ms; }

    /// Drop all in-flight playback (hard reset / machine switch).
    void reset();

    // ── Audio thread ────────────────────────────────────────────────
    /// Mix additively into `output` (mono, caller-zeroed).
    void fillAudioBuffer(float* output, int frameCount);

    // Diagnostics (gate: tests/floppy_sound_test.cpp).
    bool audioMotorOn() const { return audioMotorOn_; }
    bool audioInSeek() const { return audioInSeek_; }

private:
    enum SampleIdx {
        SEEK_2MS = 0, SEEK_6MS, SEEK_12MS, SEEK_20MS,
        SPIN_EMPTY, SPIN_LOADED,
        SPIN_START_EMPTY, SPIN_START_LOADED,
        SPIN_END,
        STEP_1_1,
        SAMPLE_COUNT
    };

    struct Sample {
        std::vector<float> data;              // mono float32 at sourceRate
        uint32_t sourceRate = 44100;
        double nominalMs = 0.0;               // seek samples: click cadence
    };
    std::array<Sample, SAMPLE_COUNT> samples_{};
    std::atomic<bool> samplesLoaded_{false};
    FormFactor formFactor_ = FormFactor::FF35;

    // ── Command queue (emulator → audio) ────────────────────────────
    enum class CmdKind : uint8_t { MotorOn, MotorOff, Step, Click };
    struct Cmd {
        CmdKind kind;
        bool withDisk;                        // MotorOn / MotorOff
        uint64_t emuMicros;                   // Step (kNoStamp = wall clock)
    };
    mutable std::mutex cmdMtx_;
    std::vector<Cmd> cmdQueue_;

    // ── Audio-thread state ──────────────────────────────────────────
    std::atomic<uint64_t> audioFrameCounter_{0};
    uint32_t outputSampleRate_ = 22254;

    bool audioMotorOn_ = false;
    bool audioWithDisk_ = false;
    // Wall-clock hold-off before spin-down so turbo'd emulated delays
    // still leave an audible whirr (POM2 lesson).
    bool pendingMotorOff_ = false;
    uint64_t motorOffDeadline_ = 0;
    int startIdx_ = -1;                       // spin_start one-shot
    double startPos_ = 0.0;
    int spinLoopIdx_ = -1;                    // spin loop
    double spinLoopPos_ = 0.0;
    int endIdx_ = -1;                         // spin_end one-shot
    double endPos_ = 0.0;

    int stepSampleIdx_ = -1;
    double stepPos_ = 0.0;
    double stepPitch_ = 1.0;
    bool audioInSeek_ = false;
    uint64_t lastStepMicros_ = 0;             // emulated-stamp cadence
    uint64_t lastStepFrame_ = 0;              // wall-clock cadence fallback
    uint64_t seekTimeoutFrame_ = 0;
    bool anyStepSeen_ = false;

    double clickPos_ = 0.0;
    bool clickActive_ = false;

    // Auto motor-off (HDD proxy).
    double autoOffMs_ = 0.0;
    uint64_t lastActivityFrame_ = 0;

    std::atomic<float> volume_{0.6f};
    std::atomic<bool> muted_{false};

    bool loadOneWav(const std::string& path, Sample& out);
    void mixOneShot(int sampleIdx, double& pos, double pitch,
                    float* out, int frames, float gain);
    void mixLoop(int sampleIdx, double& pos, double pitch,
                 float* out, int frames, float gain);
    void drainCommands();
    static int pickSeekSample(double rateMs);

    // Thresholds (ms) — see POM2 FloppySoundDevice.h for the rationale;
    // kSeekTimeoutMs must stay strictly greater than kSeekJoinMs.
    static constexpr double kSeekJoinMs = 50.0;
    static constexpr double kSeekTimeoutMs = 100.0;
    static constexpr double kMotorOffHoldMs = 800.0;
};
