// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── AudioFxSource: sound that is NOT in the guest's sample stream ────────
// `MacAudioHost` mixes two things. The first is the machine's own ring, the
// samples the ASC (or the Sony chip) produced because the guest wrote them.
// The second is everything a real Macintosh made noise with WITHOUT the CPU
// knowing: the floppy drive's stepper, the hard disk's spindle — and CD
// audio, which on a real machine never touches the sound chip at all. The
// AppleCD drive decodes the disc itself and its ANALOG output is cabled to
// the logic board's audio mixer, on a cable of its own beside the SCSI
// one (Macintosh Quadra 900 Developer Note); the guest
// starts the play with a SCSI command and then hears music it cannot read.
//
// Both slots used to be typed `FloppySound*`, which said "the only sound
// outside the guest stream is a mechanism". That was true until the drive
// learned to play. The interface is what the realtime callback needs and
// nothing else — sources are mixed additively, on the audio thread, with no
// locks and no allocation.
//
// Stereo, not mono: a mechanism is centred, but a CD is not, and folding a
// stereo disc down to one channel is a change to the recording.

#pragma once
#include <cstdint>

class AudioFxSource {
public:
    virtual ~AudioFxSource() = default;

    // The host DAC rate, set by MacAudioHost::attachFx before start().
    // Sources resample from their own native rate (44.1 kHz for both the
    // WAV mechanisms and a CD).
    virtual void setSampleRate(std::uint32_t hz) = 0;

    // Audio thread. Mix additively into `out`, `frames` interleaved
    // stereo frames. The caller has already written the machine's own
    // samples there; a source with nothing to play adds nothing.
    virtual void mixStereo(float* out, int frames) = 0;
};
