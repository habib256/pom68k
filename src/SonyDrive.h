// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Sony 3.5" drive (800K GCR + SuperDrive 1.44 MB MFM) ──
// Double-sided GCR: 80 tracks × 2 sides, 5 speed zones (12..8
// sectors/track). SuperDrive HD: 80×2×18×512 @ 300 RPM (IBM System 34
// MFM). Sense/command CA protocol addressed by CA2..CA0+SEL, stepped by
// LSTRB. Images: raw .dsk (819200 / 409600 / 1474560) or DiskCopy 4.2.
// The track is stored as raw cells (the write-back decoders read them)
// and exposed to the SWIM read engines as a FLUX VIEW — transition times
// for their FluxPll data separator, with an opt-in jitter model
// (POM68K_FLUX_JITTER; § 1.3 flux plan steps 2-3, 2026-08-14).
// Source of truth: MAME floppy.cpp (mac_floppy / mfd75w) + flopimg.cpp +
// ap_dsk35.cpp; DEV.md. Gate: tests/gcr_test.cpp, swim2_media_test.

#pragma once
#include "SaveState.h"
#include "FloppySoundSink.h"
#include "FluxPll.h"
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
    const std::string& backingPath() const { return path_; }
    bool doubleSided() const { return doubleSided_; }
    bool isHd() const { return hd_; }
    bool isSuperDrive() const { return superDrive_; }
    void setSuperDrive(bool on) { superDrive_ = on; }
    bool mfmMode() const { return mfmMode_; }
    // Setup-register reflection path (guarded); the seek strobes use the
    // private commandMfmMode() — see the .cpp for the MAME split.
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

    // ── Raw-cell interface (write-back side; reads go through the flux
    // view below since § 1.3 step 3) ── the track is a discrete cell array
    // (1 = flux transition) at the media cell rate (MFM 16 / GCR 31 C15M
    // clocks), padded to one full revolution so angular position and
    // latency are real. (nextCell(), the fixed-window read entry, retired
    // with the FluxPll migration — the separator reads transitions now.)
    void syncCellsToRotation(bool side1);        // land head at spin_ angle
    int64_t startWriteCells(bool side1);         // resync + current cell
    // Write-back: blank [startCell, startCell+totalCells), set transition
    // cells, then decode the track and commit CRC-valid sectors.
    void commitCells(int64_t startCell, int64_t totalCells,
                     const std::vector<int64_t>& transitions, bool mfm);
    // Clock the spin_ counter ticks in (Plus legacy default 7 833 600 Hz;
    // Q605 passes its 25 MHz machine clock) — used for rotation angle.
    void setSpinClockHz(int64_t hz) { spinClockHz_ = hz; }

    // ── Flux view (LLE_VS_HLE § 1.3, step 2 of the flux plan) ──
    // The same track as flux-transition TIMES, for a controller running a
    // FluxPll data separator. Unit: FluxPll ticks, 1 controller (C15M)
    // clock = FluxPll::kSubCell ticks, so one nominal cell of this medium
    // is fluxCellTicks() and one revolution fluxRevTicks(). The view is
    // derived from `cells_` (transition at the CENTRE of each 1-cell, as
    // FluxPll::writeNextBit lands a written one) and rebuilt lazily, so
    // every write-back path keeps it in sync for free.
    int64_t fluxCellTicks() const;               // nominal cell, in ticks
    int64_t fluxRevTicks() const;                // one revolution, in ticks
    int64_t fluxAngleTicks(bool side1);          // head position at spin_
    // First transition at or after `tick` — MAME
    // floppy_image_device::get_next_transition. `tick` is absolute (the
    // track repeats every fluxRevTicks()); FluxPll::kNever on a blank or
    // absent track.
    int64_t nextFluxAfter(int64_t tick, bool side1);
    // Opt-in read-jitter model: edges displaced ± pct% of one nominal cell,
    // deterministically per (track, side, transition, revolution) so runs
    // and snapshots replay identically. 0 (the default, POM68K_FLUX_JITTER
    // unset) keeps ideal edges. Clamped to 45 % — half a cell would make
    // neighbouring transitions swap, which no head amplifier produces.
    void setFluxJitterPercent(int pct);
    int fluxJitterPercent() const { return fluxJitterPct_; }
    // Test seam (gates only): re-derive the flux view with every spacing
    // scaled by permille/1000, modelling a track written on an off-rate
    // spindle. Transitions pushed past the revolution end are dropped —
    // physically, a slow-written track runs into the index. 1000 restores
    // the as-written view.
    void debugStretchFluxPermille(int permille);

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

    // ── Save states (SaveState.h) ───────────────────────────────────────
    // Unlike a SCSI image, a floppy medium is at most 1.44 MB, so the whole
    // thing travels through the zero-run codec rather than needing ScsiDisk's
    // copy-on-first-write log. That matters for correctness the same way: the
    // guest's writes live in `image_` (write-back is off in tests), so a
    // snapshot that only recorded the path would restore a volume that
    // disagrees with the HFS structures cached in guest RAM.
    //
    // The encoded track (`stream_`) and the raw cell ring (`cells_`) are
    // derived from `image_` + head position and could be rebuilt — they are
    // carried anyway because the read/write engines index into them live, and
    // rebuilding mid-sector would move the head under the controller.
    template <class Ar> void visit(Ar& ar) {
        ar.blob(image_);
        ar(path_, dc42Header_, writeBack_, dirty_);
        ar(stream_, streamPos_);
        ar.blob(cells_);
        ar(cellPos_, spinClockHz_, track_, side1_, doubleSided_,
           hd_, superDrive_, mfmMode_, writeProtected_,
           motorOn_, dirToZero_, switched_, spin_, cycles_, nibblesRead);
        ar(wrState_, wrSync_, wrAddrPos_, wrDataPos_,
           wrTrack_, wrHead_, wrSector_, wrData_);
        ar.blob(gcrWrBuf_);
        fluxDirty_ = true;                       // flux_ is derived from cells_
    }

private:
    void encodeTrack();
    void encodeTrackGcr();
    void encodeTrackMfm();
    void buildCells();
    void fluxRebuild();
    int64_t fluxJitter(size_t idx, int64_t revNo) const;
    static int fluxJitterEnv();
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
    void commandMfmMode(bool on);                // seek-strobe MFM/GCR switch
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
    // Flux view over cells_ — derived, rebuilt lazily (never serialized:
    // visit() marks it dirty on both directions and the first
    // nextFluxAfter() after a restore rebuilds it from the loaded cells_).
    std::vector<int64_t> flux_;                  // transition ticks, one rev
    bool fluxDirty_ = true;
    int fluxJitterPct_ = fluxJitterEnv();        // POM68K_FLUX_JITTER
    int64_t fluxJitterTicks_ = 0;
    int fluxStretchPermille_ = 1000;             // test seam, gates only
    int64_t spinClockHz_ = 7833600;
    int track_ = 0;
    bool side1_ = false;
    bool doubleSided_ = true;
    bool hd_ = false;                            // 1.44 MB geometry
    bool superDrive_ = false;                    // MFD-75W capability
    bool mfmMode_ = false;
    bool writeProtected_ = false;
    // switched_ = the DSKCHG latch, MAME's !m_dskchg: high after eject (or
    // while empty since power-on), cleared by insert and by the DskchgClear
    // strobe (floppy.cpp:560,672-673,723,3377-3379).
    bool motorOn_ = false, dirToZero_ = false, switched_ = true;
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
