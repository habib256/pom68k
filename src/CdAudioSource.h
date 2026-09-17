// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── CdAudioSource: the CD-audio lead, on the host side ──────────────────
// A real AppleCD drive decodes CD-DA itself and sends it out as analog
// through a dedicated lead to the logic board's mixer (Macintosh Quadra
// 900 Developer Note: a cable beside the SCSI one, carrying audio). The
// guest starts the play with a SCSI command and then hears music the CPU
// never reads, which is why this is an `AudioFxSource` (mixed after the
// machine's own ring) and not something pushed through the ASC.
//
// Two threads meet here, so the boundary is explicit:
//   machine thread — `cdAudioSector()` as the play head passes a sector;
//                    decodes 588 frames and resamples 44.1 kHz → the host
//                    rate, exactly like MacAudioHost does for the ASC;
//   audio thread   — `mixStereo()` drains the ring and adds it to the out
//                    buffer.
// The ring is single-producer/single-consumer with acquire/release indices:
// no locks and no allocation on either side. A full ring DROPS the newest
// frames rather than blocking the machine — a stalled audio device must
// never become a stalled emulation.
//
// Rate policy: the disc's 44.1 kHz is the input, the host DAC the output.
// The play head runs on machine time, so a machine running at half speed
// produces half the sectors per second and the ring simply runs dry: the
// music slows with the machine, which is what a real slowed-down drive
// would do. That is a consequence of "machine time is guest CPU time", not
// an oversight.

#pragma once
#include "AudioFxSource.h"
#include "CdAudioSink.h"
#include "HostAudioResampler.h"

#include <array>
#include <atomic>
#include <cstdint>

class CdAudioSource : public CdAudioSink, public AudioFxSource {
public:
    static constexpr int kFramesPerSector = 588;   // 2352 / 4
    static constexpr std::uint32_t kDiscRate = 44100;

    // ── Audio host (before start) ───────────────────────────────────────
    void setSampleRate(std::uint32_t hz) override {
        if (!hz) return;
        outputRate_ = hz;
        resampler_.configure(kDiscRate, hz);
    }

    // ── Machine thread ──────────────────────────────────────────────────
    void cdAudioSector(const std::uint8_t* raw) override {
        if (!raw || outputRate_ == 0) return;
        for (int i = 0; i < kFramesPerSector; i++) {
            const auto l = std::int16_t(raw[i * 4]     | (raw[i * 4 + 1] << 8));
            const auto r = std::int16_t(raw[i * 4 + 2] | (raw[i * 4 + 3] << 8));
            resampler_.push(float(l) / 32768.0f, float(r) / 32768.0f,
                            [this](float left, float right) {
                                return push(left, right);
                            });
        }
    }

    void cdAudioStopped() override { reset(); }

    // Drop everything in flight: STOP, an eject, a machine reset. The
    // resampler's phase goes with it — the next play starts a new stream.
    void reset() {
        read_.store(0, std::memory_order_relaxed);
        write_.store(0, std::memory_order_relaxed);
        resampler_.reset();
    }

    // ── UI thread ───────────────────────────────────────────────────────
    void  setVolume(float v) { volume_.store(v < 0 ? 0 : (v > 1 ? 1 : v),
                                             std::memory_order_relaxed); }
    float getVolume() const { return volume_.load(std::memory_order_relaxed); }
    void  setMuted(bool m) { muted_.store(m, std::memory_order_relaxed); }
    bool  isMuted() const { return muted_.load(std::memory_order_relaxed); }

    // ── Audio thread ────────────────────────────────────────────────────
    void mixStereo(float* out, int frames) override {
        const float gain = muted_.load(std::memory_order_relaxed)
                         ? 0.0f : volume_.load(std::memory_order_relaxed);
        for (int i = 0; i < frames; i++) {
            const std::size_t r = read_.load(std::memory_order_relaxed);
            if (r == write_.load(std::memory_order_acquire)) return;  // dry
            if (gain > 0.0f) {
                out[i * 2]     += ring_[r].left  * gain;
                out[i * 2 + 1] += ring_[r].right * gain;
            }
            read_.store((r + 1) % kRing, std::memory_order_release);
        }
    }

    // Diagnostics (gate: tests/cd_audio_source_test.cpp).
    std::size_t buffered() const {
        return (write_.load(std::memory_order_acquire) + kRing
              - read_.load(std::memory_order_acquire)) % kRing;
    }
    std::size_t droppedFrames() const {
        return dropped_.load(std::memory_order_relaxed);
    }

private:
    static constexpr std::size_t kRing = 1 << 15;   // ~0.74 s at 44.1 kHz
    struct Frame { float left, right; };

    bool push(float left, float right) {
        const std::size_t w = write_.load(std::memory_order_relaxed);
        const std::size_t next = (w + 1) % kRing;
        if (next == read_.load(std::memory_order_acquire)) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        ring_[w] = { left, right };
        write_.store(next, std::memory_order_release);
        return true;
    }

    std::array<Frame, kRing> ring_{};
    std::atomic<std::size_t> read_{0}, write_{0};
    std::atomic<std::size_t> dropped_{0};
    std::atomic<float> volume_{1.0f};
    std::atomic<bool>  muted_{false};
    std::uint32_t outputRate_ = 0;
    pom68k::HostAudioResampler resampler_;
};
