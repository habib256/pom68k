// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Audio host (miniaudio) ──
// Plays the Mac Plus PWM sample stream on the host speakers. A lock-free
// SPSC ring carries stereo frames from the emulator thread (producer)
// to miniaudio's callback (consumer, real time at 22 254.5 Hz). Only
// non-silent frames are pushed, so the ~1x-rate ring stays drained while
// the machine turbos through the silent RAM test — the startup chime and
// system beeps still play at the right pitch. Mono Plus/LC II streams are
// duplicated; PrimeTime/IOSB keeps distinct Quadra left/right channels.
// Pattern: POMIIGS AudioOut. GUI-only (not built for headless/WASM here).

#pragma once
#include "MacAudio.h"
#include "third_party/miniaudio.h"
#include <atomic>
#include <vector>

class MacAudioHost {
public:
    bool start() {
        ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
        cfg.playback.format   = ma_format_f32;
        cfg.playback.channels = 2;
        cfg.sampleRate        = 22254;              // Mac Plus native rate
        cfg.dataCallback      = &MacAudioHost::callback;
        cfg.pUserData         = this;
        if (ma_device_init(nullptr, &cfg, &device_) != MA_SUCCESS) return false;
        started_ = ma_device_start(&device_) == MA_SUCCESS;
        return started_;
    }
    void stop() { if (started_) ma_device_uninit(&device_); started_ = false; }
    bool started() const { return started_; }

    // Samples queued and not yet played — the LC II frame loop uses this
    // as its clock when sound is streaming (audio-clocked pacing): it
    // emulates just enough frames to keep this near its target, so the
    // tempo is locked to the host DAC instead of the emulation speed.
    size_t buffered() const {
        return (write_.load(std::memory_order_acquire) + kRing
              - read_.load(std::memory_order_acquire)) % kRing;
    }

    // Unconditional push (no silence gate): while music streams, silence
    // BETWEEN notes is part of the timeline — dropping it would make the
    // pacing loop run extra frames and speed the tempo up.
    void pushRaw(const std::vector<float>& s, size_t begin) {
        for (size_t i = begin; i < s.size(); i++) {
            if (!push(s[i], s[i])) break;
        }
    }

    // Interleaved L/R frames from the IOSB ASC.
    void pushRawStereo(const std::vector<float>& s, size_t begin) {
        begin += begin & 1;                    // keep channel alignment
        for (size_t i = begin; i + 1 < s.size(); i += 2)
            if (!push(s[i], s[i + 1])) break;
    }

    // Push a frame's samples — but only if it carries real sound, so the
    // ring doesn't fill with silence while the machine runs fast. The
    // gate is on AC amplitude (min/max span), not absolute peak: an
    // underrun ASC FIFO repeats its stale byte (MAME-faithful), which is
    // a full-scale DC stream a peak gate would happily push, filling
    // the ring with a pop-inducing constant (review 2026-07-16).
    void pushFrame(const std::vector<float>& s, size_t begin) {
        if (begin >= s.size()) return;
        float lo = s[begin], hi = s[begin];
        for (size_t i = begin; i < s.size(); i++) {
            if (s[i] < lo) lo = s[i];
            if (s[i] > hi) hi = s[i];
        }
        if (hi - lo < 0.02f) return;                // silence or DC → skip
        for (size_t i = begin; i < s.size(); i++) {
            if (!push(s[i], s[i])) break;
        }
    }

    void pushFrameStereo(const std::vector<float>& s, size_t begin) {
        begin += begin & 1;
        if (begin + 1 >= s.size()) return;
        float loL = s[begin], hiL = s[begin];
        float loR = s[begin + 1], hiR = s[begin + 1];
        for (size_t i = begin; i + 1 < s.size(); i += 2) {
            if (s[i] < loL) loL = s[i];
            if (s[i] > hiL) hiL = s[i];
            if (s[i + 1] < loR) loR = s[i + 1];
            if (s[i + 1] > hiR) hiR = s[i + 1];
        }
        if (hiL - loL < 0.02f && hiR - loR < 0.02f) return;
        pushRawStereo(s, begin);
    }

private:
    struct Frame { float left, right; };
    bool push(float left, float right) {
        size_t w = write_.load(std::memory_order_relaxed);
        size_t next = (w + 1) % kRing;
        if (next == read_.load(std::memory_order_acquire)) return false;
        ring_[w] = { left, right };
        write_.store(next, std::memory_order_release);
        return true;
    }
    static void callback(ma_device* d, void* out, const void*, ma_uint32 frames) {
        static_cast<MacAudioHost*>(d->pUserData)->fill(static_cast<float*>(out), frames);
    }
    void fill(float* out, ma_uint32 frames) {
        for (ma_uint32 i = 0; i < frames; i++) {
            size_t r = read_.load(std::memory_order_relaxed);
            if (r == write_.load(std::memory_order_acquire)) {
                out[i * 2] = out[i * 2 + 1] = 0;
                continue;
            }
            out[i * 2] = ring_[r].left;
            out[i * 2 + 1] = ring_[r].right;
            read_.store((r + 1) % kRing, std::memory_order_release);
        }
    }

    static constexpr size_t kRing = 1 << 16;        // 64k stereo frames (~3 s)
    Frame ring_[kRing] = {};
    std::atomic<size_t> read_{0}, write_{0};
    ma_device device_{};
    bool started_ = false;
};
