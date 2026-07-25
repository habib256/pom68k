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
#include "FloppySoundSink.h"
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
    void setMotor(bool on) { setMotorState(on); }
    bool motorOn() const { return motorOn_; }

    // GCR nibble stream for the IWM (SEL selects the side)
    uint8_t nextNibble(bool side1);
    // Byte stream for SWIM2: low 8 bits = data; bit 8 = MARK (MFM A1)
    uint16_t nextByte(bool side1);
    // Write path: feed SWIM2-serialized bytes (MARK bit optional)
    void writeByte(uint16_t value);
    // IWM write path (Plus / LC II SWIM1, GCR): buffer written nibbles,
    // then decode + commit checksum-valid data fields on flushWrite (the
    // IWM leaving write mode). Side is sampled at flush time (VIA SEL).
    void writeNibble(uint8_t nibble);
    void flushWrite(bool side1);

    // ── Raw-cell interface (SWIM2 bit engine, LLE MFM cell timing) ──
    // The track is a discrete cell array (1 = flux transition) at the
    // SWIM2 cell rate (MFM 16 / GCR 31 C15M clocks), padded to one full
    // revolution so angular position and latency are real.
    int nextCell(bool side1);                    // advance one cell
    void syncCellsToRotation(bool side1);        // land head at spin_ angle
    int64_t startWriteCells(bool side1);         // resync + current cell
    // Write-back: blank [startCell, startCell+totalCells), set transition
    // cells, then decode the track and commit CRC-valid sectors.
    void commitCells(int64_t startCell, int64_t totalCells,
                     const std::vector<int64_t>& transitions, bool mfm);
    // Clock the spin_ counter ticks in (Plus legacy default 7 833 600 Hz;
    // Q605 passes its 25 MHz machine clock) — used for rotation angle.
    void setSpinClockHz(int64_t hz) { spinClockHz_ = hz; }

    // Mechanical-sound consumer (GUI only; tests leave it null).
    void setSoundSink(FloppySoundSink* s) { sound_ = s; }
    // Direct sector write-back (tests / host tools)
    bool writeSector(int track, int side, int sector, const uint8_t data[512]);
    bool readSector(int track, int side, int sector, uint8_t data[512]) const;

    // ── Host-file write persistence (opt-in, GUI enables; tests stay
    // in-memory) ── committed sectors mark the image dirty; flushToFile
    // writes the image back to the inserted path (temp file + rename;
    // DiskCopy 4.2 gets its header + data checksum regenerated). Called
    // automatically on eject — the moment Mac OS has flushed its caches —
    // and by the GUI on exit.
    void setWriteBack(bool on) { writeBack_ = on; }
    bool writeBackEnabled() const { return writeBack_; }
    bool dirty() const { return dirty_; }
    bool flushToFile();

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
    void buildCells();
    void decodeMfmCells();
    void decodeGcrCells();
    // Scan a decoded GCR nibble sequence for D5 AA AD data fields and
    // commit every checksum-valid sector; returns the commit count.
    int decodeGcrBytes(const uint8_t* nib, size_t n, bool side1);
    int rpmNow() const;
    int64_t nominalCells() const;
    int64_t spinCyclesPerRev() const;
    size_t imageOffset(int track, int side, int sector) const;
    void selectSide(bool side1);
    int sectorsOnCurrentTrack() const;
    void setMotorState(bool on);
    uint64_t soundMicros() const;

    std::vector<uint8_t> image_;                 // raw sector data
    std::string path_;                           // inserted file (persistence)
    std::vector<uint8_t> dc42Header_;            // 0x54-byte DC42 prefix
    bool writeBack_ = false;
    bool dirty_ = false;
    std::vector<uint16_t> stream_;               // encoded current track/side
    size_t streamPos_ = 0;
    std::vector<uint8_t> cells_;                 // raw cells, one revolution
    size_t cellPos_ = 0;
    int64_t spinClockHz_ = 7833600;
    int track_ = 0;
    bool side1_ = false;
    bool doubleSided_ = true;
    bool hd_ = false;                            // 1.44 MB geometry
    bool superDrive_ = false;                    // MFD-75W capability
    bool mfmMode_ = false;
    bool writeProtected_ = false;
    bool motorOn_ = false, dirToZero_ = false, switched_ = false;
    int64_t spin_ = 0;                           // motor-on time, CPU cycles
    int64_t cycles_ = 0;                         // free-running tick time
    FloppySoundSink* sound_ = nullptr;

    // MFM write assembler (address + data field)
    int wrState_ = 0;                            // 0 idle, 1 sync, 2 addr, 3 data
    int wrSync_ = 0;
    int wrAddrPos_ = 0;
    int wrDataPos_ = 0;
    int wrTrack_ = 0, wrHead_ = 0, wrSector_ = 0;
    uint8_t wrData_[512] = {};

    // IWM GCR write buffer (nibbles accumulated until flushWrite)
    std::vector<uint8_t> gcrWrBuf_;
};
