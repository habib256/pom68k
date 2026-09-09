// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Audio host (miniaudio) ──
// Plays the Macintosh sample stream on the host speakers. A lock-free SPSC
// ring carries stereo frames from the emulator thread (producer) to
// miniaudio's callback (consumer, on the output device's native clock). A
// streaming linear converter crosses from the guest crystal to that clock.
// Only
// non-silent frames are pushed, so the ~1x-rate ring stays drained while
// the machine turbos through the silent RAM test — the startup chime and
// system beeps still play at the right pitch. Mono Plus/LC II streams are
// duplicated; PrimeTime/IOSB keeps distinct Quadra left/right channels.
// Pattern: POMIIGS AudioOut. GUI-only (not built for headless/WASM here).

#pragma once
#include "FloppySound.h"
#include "HostAudioResampler.h"
#include "MacAudio.h"
#include "third_party/miniaudio.h"
#include <algorithm>
#include <atomic>
#include <vector>

class MacAudioHost {
public:
    explicit MacAudioHost(bool enabled = true) : enabled_(enabled) {}

    bool start() {
        if (!enabled_) return false;
        ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
        cfg.playback.format   = ma_format_f32;
        cfg.playback.channels = 2;
        // Zero asks miniaudio for the device's native callback rate instead
        // of hiding a second conversion behind a fixed 22 254 Hz callback.
        cfg.sampleRate        = 0;
        cfg.dataCallback      = &MacAudioHost::callback;
        cfg.pUserData         = this;
        if (ma_device_init(nullptr, &cfg, &device_) != MA_SUCCESS) return false;
        outputRate_ = device_.sampleRate ? device_.sampleRate : inputRate_;
        resampler_.configure(inputRate_, outputRate_);
        read_.store(0, std::memory_order_relaxed);
        write_.store(0, std::memory_order_relaxed);
        for (FloppySound* fx : fx_)
            if (fx) fx->setSampleRate(int(outputRate_));
        // An init'd-but-not-started device still owns backend handles: without
        // this uninit they leak for the process lifetime, since stop() is
        // gated on started_.
        if (ma_device_start(&device_) != MA_SUCCESS) {
            ma_device_uninit(&device_);
            return false;
        }
        started_ = true;
        return true;
    }
    // The realtime callback dereferences fx_ unconditionally. Session teardown
    // destroys this host before its FloppySound sources, while stop() also
    // protects abnormal paths that still execute C++ destructors.
    ~MacAudioHost() { stop(); }
    void stop() {
        if (started_) ma_device_uninit(&device_);
        started_ = false;
        fx_[0] = fx_[1] = nullptr;         // any in-flight callback mixes nothing
    }
    bool started() const { return started_; }

    // Guest sample clock, configured by the platform before start(). V8,
    // Eagle, Spice and Tinker Bell all expose 22 257 Hz at this boundary.
    void setInputSampleRate(uint32_t rate) {
        if (!started_ && rate != 0) inputRate_ = rate;
    }
    uint32_t outputSampleRate() const { return outputRate_; }

    // Mechanical-sound sources (FloppySound), mixed into the callback
    // after the machine's sample ring — they play even while the ring
    // is silent (a seeking drive on a quiet desktop). Attach BEFORE
    // start(); the callback reads the array without locks.
    void attachFx(FloppySound* fx) {
        for (FloppySound*& slot : fx_)
            if (!slot) {
                slot = fx;
                fx->setSampleRate(int(outputRate_));
                return;
            }
    }

    // Samples queued and not yet played — the LC II frame loop uses this
    // as its clock when sound is streaming (audio-clocked pacing): it
    // emulates just enough frames to keep this near its target, so the
    // tempo is locked to the host DAC instead of the emulation speed.
    size_t buffered() const {
        return (write_.load(std::memory_order_acquire) + kRing
              - read_.load(std::memory_order_acquire)) % kRing;
    }
    size_t targetBuffered() const {
        return std::max<size_t>(1, size_t(outputRate_) / 10);  // about 100 ms
    }

    // Unconditional push (no silence gate): while music streams, silence
    // BETWEEN notes is part of the timeline — dropping it would make the
    // pacing loop run extra frames and speed the tempo up.
    void pushRaw(const std::vector<float>& s, size_t begin) {
        if (begin >= s.size()) return;
        resampler_.pushMono(s.data() + begin, s.size() - begin,
            [this](float left, float right) { return push(left, right); });
    }

    // Interleaved L/R frames from the IOSB ASC.
    void pushRawStereo(const std::vector<float>& s, size_t begin) {
        begin += begin & 1;                    // keep channel alignment
        if (begin + 1 >= s.size()) return;
        resampler_.pushStereo(s.data() + begin, (s.size() - begin) / 2,
            [this](float left, float right) { return push(left, right); });
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
        pushRaw(s, begin);
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
    bool enabled_ = true;
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
        // Mechanical FX overlay, chunked through a small mono scratch.
        for (FloppySound* fx : fx_) {
            if (!fx) continue;
            ma_uint32 done = 0;
            while (done < frames) {
                float buf[256] = {};
                const ma_uint32 n = std::min<ma_uint32>(256, frames - done);
                fx->fillAudioBuffer(buf, int(n));
                for (ma_uint32 i = 0; i < n; i++) {
                    out[(done + i) * 2] += buf[i];
                    out[(done + i) * 2 + 1] += buf[i];
                }
                done += n;
            }
        }
    }

    static constexpr size_t kRing = 1 << 16;        // 64k native-rate frames
    Frame ring_[kRing] = {};
    std::atomic<size_t> read_{0}, write_{0};
    ma_device device_{};
    bool started_ = false;
    uint32_t inputRate_ = 22254;             // legacy Mac Plus default
    uint32_t outputRate_ = 22254;            // replaced after device init
    pom68k::HostAudioResampler resampler_;
    FloppySound* fx_[2] = { nullptr, nullptr };     // floppy + HDD proxy
};
