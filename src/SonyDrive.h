// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Sony 3.5" 800K drive ──
// Double-sided GCR drive: 80 tracks × 2 sides, 5 speed zones (12..8
// sectors/track), sense/command interface addressed by CA2..CA0+SEL,
// stepped by LSTRB strobes. Disk images: raw .dsk (819200 = 800K DS,
// 409600 = 400K SS) or DiskCopy 4.2, cylinder-major/head/sector order;
// tracks are GCR-encoded on demand (byte-level nibble stream).
// Source of truth: MAME floppy.cpp + flopimg.cpp + ap_dsk35.cpp; DEV.md.
// Gate: tests/gcr_test.cpp (roundtrip), tests/disk_boot_etalon.cpp.

#pragma once
#include <cstdint>
#include <string>
#include <vector>

class SonyDrive {
public:
    void reset();
    bool insert(const std::string& path);        // raw .dsk / DiskCopy 4.2
    bool insertImage(std::vector<uint8_t> data); // in-memory image
    bool hasDisk() const { return !image_.empty(); }
    bool doubleSided() const { return doubleSided_; }

    // Sense bit for (CA2,CA1,CA0,SEL) — returned on IWM status bit 7
    bool sense(int addr) const;
    // LSTRB rising-edge command; CA2 = value, (CA1,CA0,SEL) = address
    void command(int addr);
    void setMotor(bool on) { motorOn_ = on; }

    // GCR nibble stream for the given head (SEL line selects the side)
    uint8_t nextNibble(bool side1);
    int currentTrack() const { return track_; }

    void tick(int cpuCycles);

    static int sectorsInTrack(int track);

    bool debug = false;                          // command/sense tracing
    long nibblesRead = 0;

private:
    void encodeTrack();
    size_t imageOffset(int track, int side, int sector) const;

    std::vector<uint8_t> image_;                 // raw sector data
    std::vector<uint8_t> gcr_;                   // encoded current track/side
    size_t gcrPos_ = 0;
    int track_ = 0;
    bool side1_ = false;
    bool doubleSided_ = true;
    bool motorOn_ = false, dirToZero_ = false, switched_ = false;
    int64_t spin_ = 0;                           // motor-on time, CPU cycles
};
