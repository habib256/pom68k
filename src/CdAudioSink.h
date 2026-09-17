// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── CdAudioSink: where a played CD sector goes ──────────────────────────
// The drive advances its play head on MACHINE time (ScsiDisk::advanceAudio,
// 75 sectors a second). Every sector it passes over carries 588 stereo
// frames of CD-DA, and on a real Macintosh those leave the drive through
// its own DAC and the analog CD-audio lead — the guest never sees them.
// This is that lead: the machine thread hands the raw sector over, and the
// GUI's `CdAudioSource` turns it into host samples.
//
// The lead is real hardware, not a modelling convenience: the Macintosh
// Quadra 900 Developer Note describes an additional cable that "takes the
// audio signals from the CD-ROM drive to the main logic board", beside the
// SCSI cable that carries the commands. The Apple Sound Chip never sees
// those samples, which is why this is not an ASC path.
//
// Deliberately raw: no decoding, no rate, no volume. The disc's own bytes,
// so a consumer that wants to write them to a file instead of playing them
// (a gate, a ripper) gets exactly what the disc holds.

#pragma once
#include <cstdint>

class CdAudioSink {
public:
    virtual ~CdAudioSink() = default;
    // One CD sector: 588 interleaved 16-bit little-endian stereo frames,
    // 2352 bytes, verbatim from the medium. Called from the machine thread.
    virtual void cdAudioSector(const std::uint8_t* raw2352) = 0;

    // The disc left the tray. Whatever is still in flight on the host side
    // is music from a disc that is no longer in the machine, so it is
    // dropped rather than played out. A play that simply REACHES ITS END
    // does not call this: those last frames are part of the recording.
    virtual void cdAudioStopped() {}
};
