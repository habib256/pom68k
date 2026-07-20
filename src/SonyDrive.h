// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Sony 3.5" drive (800K GCR + SuperDrive 1.44 MB MFM) ──
// Double-sided GCR: 80 tracks × 2 sides, 5 speed zones (12..8
// sectors/track). SuperDrive HD: 80×2×18×512 @ 300 RPM (IBM System 34
// MFM). Sense/command CA protocol addressed by CA2..CA0+SEL, stepped by
// LSTRB. Images: raw .dsk (819200 / 409600 / 1474560) or DiskCopy 4.2.
// Source of truth: MAME floppy.cpp (mac_floppy / mfd75w) + flopimg.cpp +
// ap_dsk35.cpp; DEV.md. Gate: tests/gcr_test.cpp, swim2_media_test.

#pragma once
#include <cstdint>
#include <string>
#include <vector>

class SonyDrive {
public:
    static constexpr size_t kSize800K = 819200;
    static constexpr size_t kSize400K = 409600;
    static constexpr size_t kSize1440K = 1474560;

    void reset();
    bool insert(const std::string& path);        // raw .dsk / DiskCopy 4.2
    bool insertImage(std::vector<uint8_t> data); // in-memory image
    void eject();                                // clear image (sense CSTIN)
    bool hasDisk() const { return !image_.empty(); }
    bool doubleSided() const { return doubleSided_; }
    bool isHd() const { return hd_; }
    bool isSuperDrive() const { return superDrive_; }
    void setSuperDrive(bool on) { superDrive_ = on; }
    bool mfmMode() const { return mfmMode_; }
    void setMfmMode(bool on);
    bool isWriteProtected() const { return writeProtected_; }
    void setWriteProtected(bool on) { writeProtected_ = on; }

    // Sense bit for (CA2,CA1,CA0,SEL) — returned on IWM status bit 7 /
    // SWIM2 handshake bit 3.
    bool sense(int addr) const;
    // LSTRB rising-edge command; CA2 = value, (CA1,CA0,SEL) = address
    void command(int addr);
    // SWIM2/mac_floppy uses the direct phase register:
    // phase[2:0] | (HDSEL << 3), unlike the classic IWM CA packing above.
    bool senseSwim(int reg) const;
    void commandSwim(int reg);
    void setMotor(bool on) { motorOn_ = on; }
    bool motorOn() const { return motorOn_; }

    // GCR nibble stream for the IWM (SEL selects the side)
    uint8_t nextNibble(bool side1);
    // Byte stream for SWIM2: low 8 bits = data; bit 8 = MARK (MFM A1)
    uint16_t nextByte(bool side1);
    // Write path: feed SWIM2-serialized bytes (MARK bit optional)
    void writeByte(uint16_t value);
    // Direct sector write-back (tests / host tools)
    bool writeSector(int track, int side, int sector, const uint8_t data[512]);
    bool readSector(int track, int side, int sector, uint8_t data[512]) const;

    int currentTrack() const { return track_; }

    void tick(int cpuCycles);

    static int sectorsInTrack(int track);        // GCR zone table
    static int sectorsInTrackHd() { return 18; }

    bool debug = false;
    long nibblesRead = 0;

private:
    void encodeTrack();
    void encodeTrackGcr();
    void encodeTrackMfm();
    size_t imageOffset(int track, int side, int sector) const;
    void selectSide(bool side1);
    int sectorsOnCurrentTrack() const;

    std::vector<uint8_t> image_;                 // raw sector data
    std::vector<uint16_t> stream_;               // encoded current track/side
    size_t streamPos_ = 0;
    int track_ = 0;
    bool side1_ = false;
    bool doubleSided_ = true;
    bool hd_ = false;                            // 1.44 MB geometry
    bool superDrive_ = false;                    // MFD-75W capability
    bool mfmMode_ = false;
    bool writeProtected_ = false;
    bool motorOn_ = false, dirToZero_ = false, switched_ = false;
    int64_t spin_ = 0;                           // motor-on time, CPU cycles

    // MFM write assembler (address + data field)
    int wrState_ = 0;                            // 0 idle, 1 sync, 2 addr, 3 data
    int wrSync_ = 0;
    int wrAddrPos_ = 0;
    int wrDataPos_ = 0;
    int wrTrack_ = 0, wrHead_ = 0, wrSector_ = 0;
    uint8_t wrData_[512] = {};
};
