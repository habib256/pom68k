// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── CdAudioPump: how often a board looks at its CD transports ───────────
// A board's tick() is not a periodic timer. On the 68000 boards it is
// `Cpu68k::catchUp()`, called from the bus-access path, so anything put
// there runs in the emulator's hottest loop. Walking seven SCSI targets on
// every bus access to ask "is a CD playing?" would charge every machine,
// forever, for a disc almost none of them have in the tray.
//
// It is also unnecessary: CD-DA is 75 sectors a second. A grain of one
// millisecond of MACHINE time is thirteen times finer than the thing being
// modelled, and the accumulator carries the leftover cycles, so nothing is
// lost — the play head lands on exactly the same sector it would have, just
// decided less often. Host time is never consulted.
//
// Not serialized, deliberately: the grain holds at most one millisecond of
// machine time, while the play POSITION itself is snapshot state
// (ScsiDisk::visit). A restore therefore resumes on the right sector and
// re-starts the grain.

#pragma once
#include <cstdint>

class CdAudioPump {
public:
    template <class Disks>
    void advance(Disks& disks, int cpuCycles, std::int64_t cpuHz) {
        if (cpuCycles <= 0 || cpuHz <= 0) return;
        acc_ += cpuCycles;
        const std::int64_t grain = cpuHz / 1000;      // 1 ms of machine time
        if (acc_ < grain) return;
        const std::int64_t due = acc_;
        acc_ = 0;
        for (auto& disk : disks) disk.advanceAudioCycles(due, cpuHz);
    }

private:
    std::int64_t acc_ = 0;
};
