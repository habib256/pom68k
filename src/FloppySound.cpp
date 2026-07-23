// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "FloppySound.h"

#include "third_party/miniaudio.h"   // IMPLEMENTATION lives in miniaudio_impl.cpp

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace {
// MAME's floppy samples aren't designed for seamless looping — every
// looping clip has a 0.4-2.7% amplitude jump between last and first
// sample, an audible click each loop (~200 ms cadence). Blend the last
// ~3 ms into the first at load time so the wraparound is continuous
// (POM2 applyLoopCrossfade).
void applyLoopCrossfade(std::vector<float>& data) {
    if (data.size() < 8) return;
    const size_t n = data.size();
    const size_t window = std::min<size_t>(132, n / 4);
    for (size_t i = 0; i < window; ++i) {
        const float alpha = float(i + 1) / float(window);
        const size_t k = n - window + i;
        data[k] = data[k] * (1.0f - alpha) + data[i] * alpha;
    }
}
} // namespace

bool FloppySound::loadSamples(const std::string& dir, FormFactor ff) {
    namespace fs = std::filesystem;
    formFactor_ = ff;
    const char* p = (ff == FormFactor::FF35) ? "35" : "525";
    struct Entry { SampleIdx idx; const char* stem; double nominalMs; };
    static constexpr Entry kSamples[] = {
        { SEEK_2MS,          "seek_2ms",           2.0 },
        { SEEK_6MS,          "seek_6ms",           6.0 },
        { SEEK_12MS,         "seek_12ms",         12.0 },
        { SEEK_20MS,         "seek_20ms",         20.0 },
        { SPIN_EMPTY,        "spin_empty",         0.0 },
        { SPIN_LOADED,       "spin_loaded",        0.0 },
        { SPIN_START_EMPTY,  "spin_start_empty",   0.0 },
        { SPIN_START_LOADED, "spin_start_loaded",  0.0 },
        { SPIN_END,          "spin_end",           0.0 },
        { STEP_1_1,          "step_1_1",           0.0 },
    };

    int loaded = 0;
    for (const Entry& e : kSamples) {
        const fs::path full = fs::path(dir) / (std::string(p) + "_" + e.stem + ".wav");
        Sample s;
        if (loadOneWav(full.string(), s)) {
            s.nominalMs = e.nominalMs;
            const bool isLooping =
                (e.idx == SPIN_EMPTY || e.idx == SPIN_LOADED ||
                 e.idx == SEEK_2MS || e.idx == SEEK_6MS ||
                 e.idx == SEEK_12MS || e.idx == SEEK_20MS);
            if (isLooping) applyLoopCrossfade(s.data);
            samples_[e.idx] = std::move(s);
            ++loaded;
        }
    }
    const bool allLoaded = (loaded == SAMPLE_COUNT);
    // Release-store publishes samples_ to the audio thread.
    samplesLoaded_.store(allLoaded, std::memory_order_release);
    if (!allLoaded && loaded)
        std::fprintf(stderr, "[sfx] floppy samples incomplete in %s (%d/10)\n",
                     dir.c_str(), loaded);
    return allLoaded;
}

bool FloppySound::loadOneWav(const std::string& path, Sample& out) {
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 1, 0);
    ma_decoder dec;
    if (ma_decoder_init_file(path.c_str(), &cfg, &dec) != MA_SUCCESS) return false;
    out.sourceRate = dec.outputSampleRate;
    ma_uint64 totalFrames = 0;
    if (ma_decoder_get_length_in_pcm_frames(&dec, &totalFrames) != MA_SUCCESS ||
        totalFrames == 0) {
        ma_decoder_uninit(&dec);
        return false;
    }
    out.data.assign(size_t(totalFrames), 0.0f);
    ma_uint64 framesRead = 0;
    ma_result r = ma_decoder_read_pcm_frames(&dec, out.data.data(),
                                             totalFrames, &framesRead);
    ma_decoder_uninit(&dec);
    if (r != MA_SUCCESS && r != MA_AT_END) return false;
    out.data.resize(size_t(framesRead));
    return !out.data.empty();
}

void FloppySound::setVolume(float v) {
    volume_.store(std::clamp(v, 0.0f, 2.0f), std::memory_order_relaxed);
}

void FloppySound::reset() {
    std::lock_guard<std::mutex> lk(cmdMtx_);
    cmdQueue_.clear();
    // Audio-thread state stays untouched (racy); a queued MotorOff
    // brings the next fillAudioBuffer to silence within ~one buffer.
    cmdQueue_.push_back({CmdKind::MotorOff, false, 0});
}

// ── Emulator-thread API ─────────────────────────────────────────────

void FloppySound::motor(bool on, bool withDisk) {
    if (!samplesLoaded_.load(std::memory_order_acquire)) return;
    std::lock_guard<std::mutex> lk(cmdMtx_);
    cmdQueue_.push_back({on ? CmdKind::MotorOn : CmdKind::MotorOff, withDisk, 0});
}

void FloppySound::step(int /*newTrack*/, uint64_t emuMicros) {
    if (!samplesLoaded_.load(std::memory_order_acquire)) return;
    std::lock_guard<std::mutex> lk(cmdMtx_);
    cmdQueue_.push_back({CmdKind::Step, false, emuMicros});
}

void FloppySound::click() {
    if (!samplesLoaded_.load(std::memory_order_acquire)) return;
    std::lock_guard<std::mutex> lk(cmdMtx_);
    cmdQueue_.push_back({CmdKind::Click, false, 0});
}

// ── Audio-thread internals ──────────────────────────────────────────

int FloppySound::pickSeekSample(double rateMs) {
    if (rateMs <= 3.0) return SEEK_2MS;
    if (rateMs <= 9.0) return SEEK_6MS;
    if (rateMs <= 15.0) return SEEK_12MS;
    if (rateMs <= 50.0) return SEEK_20MS;
    return SAMPLE_COUNT;                      // isolated step → click
}

void FloppySound::drainCommands() {
    std::vector<Cmd> local;
    {
        std::lock_guard<std::mutex> lk(cmdMtx_);
        local.swap(cmdQueue_);
    }
    for (const Cmd& c : local) {
        switch (c.kind) {
        case CmdKind::MotorOn:
            pendingMotorOff_ = false;
            lastActivityFrame_ = audioFrameCounter_.load(std::memory_order_relaxed);
            if (!audioMotorOn_) {
                audioMotorOn_ = true;
                audioWithDisk_ = c.withDisk;
                startIdx_ = c.withDisk ? SPIN_START_LOADED : SPIN_START_EMPTY;
                startPos_ = 0.0;
                spinLoopIdx_ = c.withDisk ? SPIN_LOADED : SPIN_EMPTY;
                spinLoopPos_ = 0.0;
                endIdx_ = -1;
            } else {
                audioWithDisk_ = c.withDisk;
                spinLoopIdx_ = c.withDisk ? SPIN_LOADED : SPIN_EMPTY;
            }
            break;
        case CmdKind::MotorOff:
            // Wall-clock hold-off: at turbo speed the controller's
            // emulated spin-down delay is too short to hear the loop.
            if (audioMotorOn_ && !pendingMotorOff_) {
                const double sr = double(outputSampleRate_);
                pendingMotorOff_ = true;
                motorOffDeadline_ =
                    audioFrameCounter_.load(std::memory_order_relaxed) +
                    uint64_t(kMotorOffHoldMs * sr / 1000.0);
            }
            break;
        case CmdKind::Step: {
            const uint64_t now = audioFrameCounter_.load(std::memory_order_relaxed);
            const double sr = double(outputSampleRate_);
            lastActivityFrame_ = now;
            // Inter-step gap: emulated micros when stamped (turbo-safe,
            // mirrors MAME's machine().time() cadence), else wall-clock
            // audio frames (kNoStamp callers).
            double gapMs = 1e9;
            if (anyStepSeen_) {
                if (c.emuMicros != kNoStamp) {
                    gapMs = (c.emuMicros > lastStepMicros_)
                        ? double(c.emuMicros - lastStepMicros_) / 1000.0
                        : 0.0;
                } else {
                    gapMs = double(now - lastStepFrame_) * 1000.0 / sr;
                }
            }
            anyStepSeen_ = true;
            // Floor at 1 ms — defends mixLoop against INF pitch rates.
            if (gapMs < 1.0) gapMs = 1.0;
            if (c.emuMicros != kNoStamp) lastStepMicros_ = c.emuMicros;
            lastStepFrame_ = now;
            if (gapMs <= kSeekJoinMs) {
                const int seekIdx = pickSeekSample(gapMs);
                if (seekIdx < SAMPLE_COUNT && !samples_[seekIdx].data.empty() &&
                    samples_[seekIdx].nominalMs > 0.0) {
                    audioInSeek_ = true;
                    if (stepSampleIdx_ != seekIdx) {
                        stepSampleIdx_ = seekIdx;
                        stepPos_ = 0.0;
                    }
                    stepPitch_ = samples_[seekIdx].nominalMs / gapMs;
                    if (!(stepPitch_ > 0.0) || stepPitch_ > 4.0) stepPitch_ = 1.0;
                    seekTimeoutFrame_ = now + uint64_t(kSeekTimeoutMs * sr / 1000.0);
                    break;
                }
            }
            audioInSeek_ = false;
            stepSampleIdx_ = STEP_1_1;
            stepPos_ = 0.0;
            stepPitch_ = 1.0;
            break;
        }
        case CmdKind::Click:
            clickActive_ = true;
            clickPos_ = 0.0;
            break;
        }
    }
}

void FloppySound::mixOneShot(int sampleIdx, double& pos, double pitch,
                             float* out, int frames, float gain) {
    if (sampleIdx < 0 || sampleIdx >= SAMPLE_COUNT) return;
    const Sample& s = samples_[sampleIdx];
    if (s.data.empty()) return;
    const double rate = pitch * double(s.sourceRate) / double(outputSampleRate_);
    if (!(rate > 0.0) || rate > 1e6) { pos = double(s.data.size()); return; }
    const size_t n = s.data.size();
    for (int i = 0; i < frames; ++i) {
        if (pos < 0.0 || pos >= double(n - 1)) {
            pos = double(n);                  // mark done
            break;
        }
        const size_t k = size_t(pos);
        const float f = float(pos - double(k));
        out[i] += (s.data[k] + f * (s.data[k + 1] - s.data[k])) * gain;
        pos += rate;
    }
}

void FloppySound::mixLoop(int sampleIdx, double& pos, double pitch,
                          float* out, int frames, float gain) {
    if (sampleIdx < 0 || sampleIdx >= SAMPLE_COUNT) return;
    const Sample& s = samples_[sampleIdx];
    if (s.data.size() < 2) return;
    const double rate = pitch * double(s.sourceRate) / double(outputSampleRate_);
    if (!(rate > 0.0) || rate > 1e6) return;  // INF would spin the wrap loop
    const double len = double(s.data.size());
    for (int i = 0; i < frames; ++i) {
        while (pos >= len) pos -= len;
        while (pos < 0.0) pos += len;
        const size_t k = size_t(pos);
        const size_t k1 = (k + 1 >= s.data.size()) ? 0 : k + 1;
        const float f = float(pos - double(k));
        out[i] += (s.data[k] + f * (s.data[k1] - s.data[k])) * gain;
        pos += rate;
    }
}

void FloppySound::fillAudioBuffer(float* output, int frameCount) {
    if (frameCount <= 0) return;
    drainCommands();

    if (!samplesLoaded_.load(std::memory_order_acquire) ||
        muted_.load(std::memory_order_relaxed)) {
        // Keep the frame counter moving so cadence stays consistent
        // across mute toggles.
        audioFrameCounter_.fetch_add(uint64_t(frameCount), std::memory_order_relaxed);
        return;
    }

    const float gain = volume_.load(std::memory_order_relaxed);
    const uint64_t nowStart = audioFrameCounter_.load(std::memory_order_relaxed);

    // Auto motor-off (HDD proxy): retire the spin loop after an idle
    // window with no step events.
    if (autoOffMs_ > 0.0 && audioMotorOn_ && !pendingMotorOff_ &&
        nowStart > lastActivityFrame_ +
            uint64_t(autoOffMs_ * double(outputSampleRate_) / 1000.0)) {
        pendingMotorOff_ = true;
        motorOffDeadline_ = nowStart;
    }

    // Deferred spin-down (wall-clock hold elapsed).
    if (pendingMotorOff_ && nowStart >= motorOffDeadline_) {
        pendingMotorOff_ = false;
        audioMotorOn_ = false;
        spinLoopIdx_ = -1;
        startIdx_ = -1;
        endIdx_ = SPIN_END;
        endPos_ = 0.0;
    }

    // Motor: start one-shot → loop → end one-shot.
    if (startIdx_ >= 0) {
        if (startPos_ >= double(samples_[startIdx_].data.size())) startIdx_ = -1;
        else mixOneShot(startIdx_, startPos_, 1.0, output, frameCount, gain);
    } else if (spinLoopIdx_ >= 0 && audioMotorOn_) {
        mixLoop(spinLoopIdx_, spinLoopPos_, 1.0, output, frameCount, gain);
    }
    if (endIdx_ >= 0) {
        if (endPos_ >= double(samples_[endIdx_].data.size())) endIdx_ = -1;
        else mixOneShot(endIdx_, endPos_, 1.0, output, frameCount, gain);
    }

    // Step / seek.
    const uint64_t nowEnd = nowStart + uint64_t(frameCount);
    if (audioInSeek_ && nowEnd >= seekTimeoutFrame_) {
        audioInSeek_ = false;                 // land with a final click
        stepSampleIdx_ = STEP_1_1;
        stepPos_ = 0.0;
        stepPitch_ = 1.0;
    }
    if (stepSampleIdx_ >= 0) {
        const Sample& s = samples_[stepSampleIdx_];
        if (audioInSeek_ && stepSampleIdx_ >= SEEK_2MS && stepSampleIdx_ <= SEEK_20MS) {
            mixLoop(stepSampleIdx_, stepPos_, stepPitch_, output, frameCount,
                    gain * 0.9f);
        } else if (stepPos_ < double(s.data.size())) {
            mixOneShot(stepSampleIdx_, stepPos_, stepPitch_, output, frameCount,
                       gain * 0.9f);
        } else {
            stepSampleIdx_ = -1;
        }
    }

    // Insert / eject click.
    if (clickActive_) {
        if (clickPos_ >= double(samples_[STEP_1_1].data.size())) clickActive_ = false;
        else mixOneShot(STEP_1_1, clickPos_, 1.0, output, frameCount, gain * 1.1f);
    }

    audioFrameCounter_.fetch_add(uint64_t(frameCount), std::memory_order_relaxed);
}
