// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Audio host (miniaudio) ──
// Plays the Mac Plus PWM sample stream on the host speakers. A lock-free
// SPSC ring carries samples from the emulator thread (producer, per frame)
// to miniaudio's callback (consumer, real time at 22 254.5 Hz). Only
// non-silent frames are pushed, so the ~1x-rate ring stays drained while
// the machine turbos through the silent RAM test — the startup chime and
// system beeps still play at the right pitch (just slightly delayed).
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
        cfg.playback.channels = 1;
        cfg.sampleRate        = 22254;              // Mac Plus native rate
        cfg.dataCallback      = &MacAudioHost::callback;
        cfg.pUserData         = this;
        if (ma_device_init(nullptr, &cfg, &device_) != MA_SUCCESS) return false;
        started_ = ma_device_start(&device_) == MA_SUCCESS;
        return started_;
    }
    void stop() { if (started_) ma_device_uninit(&device_); started_ = false; }

    // Push a frame's samples — but only if it carries real sound, so the
    // ring doesn't fill with silence while the machine runs fast.
    void pushFrame(const std::vector<float>& s, size_t begin) {
        float peak = 0;
        for (size_t i = begin; i < s.size(); i++) { float a = s[i] < 0 ? -s[i] : s[i]; if (a > peak) peak = a; }
        if (peak < 0.01f) return;                   // silent frame → skip
        for (size_t i = begin; i < s.size(); i++) {
            size_t w = write_.load(std::memory_order_relaxed);
            size_t next = (w + 1) % kRing;
            if (next == read_.load(std::memory_order_acquire)) break;   // full: drop rest
            ring_[w] = s[i];
            write_.store(next, std::memory_order_release);
        }
    }

private:
    static void callback(ma_device* d, void* out, const void*, ma_uint32 frames) {
        static_cast<MacAudioHost*>(d->pUserData)->fill(static_cast<float*>(out), frames);
    }
    void fill(float* out, ma_uint32 frames) {
        for (ma_uint32 i = 0; i < frames; i++) {
            size_t r = read_.load(std::memory_order_relaxed);
            if (r == write_.load(std::memory_order_acquire)) { out[i] = 0; continue; }
            out[i] = ring_[r];
            read_.store((r + 1) % kRing, std::memory_order_release);
        }
    }

    static constexpr size_t kRing = 1 << 16;        // 64k samples (~3 s)
    float ring_[kRing] = {};
    std::atomic<size_t> read_{0}, write_{0};
    ma_device device_{};
    bool started_ = false;
};
