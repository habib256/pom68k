// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── Sony 3.5" drive (800K GCR + SuperDrive 1.44 MB MFM) ──
// Double-sided GCR: 80 tracks × 2 sides, 5 speed zones (12..8
// sectors/track). SuperDrive HD: 80×2×18×512 @ 300 RPM (IBM System 34
// MFM). Sense/command CA protocol addressed by CA2..CA0+SEL, stepped by
// LSTRB. Images: raw .dsk (819200 / 409600 / 1474560) or DiskCopy 4.2.
// The track is stored as FLUX — transition times, which is what the SWIM
// read engines' FluxPll data separator consumes (§ 1.3 flux plan step 5);
// the discrete cell ring the write-back decoders read is DERIVED from it
// through that same separator, and an opt-in jitter model
// (POM68K_FLUX_JITTER) displaces edges on the way out to model the read
// channel — a property of the head amplifier, not of the medium, which is
// why it is applied per read and never stored.
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
    // The /READY line. Not the same thing as motorOn(): the mechanism needs
    // two index pulses to come up to speed (MAME floppy.cpp:825, :888-891),
    // so a Sony at 394 RPM answers ~0.25 s after the motor command.
    bool driveReady() const;

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

    // ── Write-back interface (§ 1.3 step 5: times, not cell indices) ──
    // A controller opens a write ACTION at the drive's current rotation
    // angle and closes it with the transition times its write serializer
    // produced. Both are in the same flux ticks the read side uses, so a
    // controller running off the media rate — a mismatched setup, a
    // formatter with its own spacing — lays down flux at ITS rate and the
    // store keeps it. (startWriteCells()/commitCells(), which quantized to
    // the media's cell grid on the way in, retired with the flux store.)
    int64_t startWriteFlux(bool side1);          // head angle, in ticks
    // Erase the arc [startTick, startTick+totalTicks) of the track, lay the
    // transitions down at startTick+t, then decode what is now on the
    // medium and commit every CRC-valid sector to the image.
    //
    // `cellTicks` is the controller's OWN cell period, which is not always
    // the medium's nominal one — SWIM2 setup bit 3 doubles the write
    // spacing (swim2.cpp, `halfWait_ <<= 1`), and the SWIM1 ISM takes its
    // spacing from the parameter RAM. It exists because the write-back
    // decode needs a clock and the drive has no reader of its own: on
    // silicon nothing decodes a track until a controller reads it back, so
    // the honest stand-in is to verify the write with the clock that wrote
    // it. Pass 0 for the medium's nominal rate.
    void commitFlux(int64_t startTick, int64_t totalTicks,
                    const std::vector<int64_t>& atTicks, bool mfm,
                    int64_t cellTicks = 0);
    // Clock the spin_ counter ticks in (Plus legacy default 7 833 600 Hz;
    // Q605 passes its 25 MHz machine clock) — used for rotation angle.
    void setSpinClockHz(int64_t hz) { spinClockHz_ = hz; }

    // ── Flux store (LLE_VS_HLE § 1.3, steps 2 and 5 of the flux plan) ──
    // The track, as flux-transition TIMES — the canonical representation
    // since step 5. Unit: FluxPll ticks, 1 controller (C15M) clock =
    // FluxPll::kSubCell ticks, so one NOMINAL cell of this medium is
    // fluxCellTicks() and one revolution fluxRevTicks(). Nominal is the
    // operative word: the store holds whatever spacing was written, and a
    // reader's separator is what turns it back into cells.
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
    // Test seam (gates only): re-lay the canonical track with every spacing
    // scaled by permille/1000, modelling a track written on an off-rate
    // spindle. Transitions pushed past the revolution end are dropped —
    // physically, a slow-written track runs into the index. 1000 restores
    // the canonical track. Since step 5 a controller can produce off-rate
    // flux on its own (write at a rate the media geometry does not share
    // and it lands as written) — this seam stays because it makes a WHOLE
    // track off-rate, which no single write ACTION does.
    void debugStretchFluxPermille(int permille);
    // Read-only view of the flux store, for gates that want to prove what
    // is physically on the medium rather than what a decoder made of it.
    const std::vector<int64_t>& debugFlux() const { return flux_; }

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
    // Since § 1.3 step 5 the FLUX is what travels: it is the medium now, and
    // a track the guest wrote off-rate is not re-derivable from `image_` —
    // that is the whole point of the store. It costs more than the cell ring
    // it replaces (~300 KB against ~75 KB on a GCR track, and the zero-run
    // codec cannot compress transition times the way it compressed a mostly
    // empty gap4) — noise beside the 800 KB image in the same snapshot.
    // `cells_` is NOT carried any more: it is the separator's reading of
    // `flux_`, so it rebuilds exactly. `stream_` still is — the legacy
    // nibble path indexes into it live, and rebuilding mid-sector would move
    // the head under the controller.
    template <class Ar> void visit(Ar& ar) {
        ar.blob(image_);
        ar(path_, dc42Header_, writeBack_, dirty_);
        ar(stream_, streamPos_);
        ar(flux_, fluxRev_, decodeCellTicks_);
        ar(spinClockHz_, track_, side1_, doubleSided_,
           hd_, superDrive_, mfmMode_, writeProtected_,
           motorOn_, dirToZero_, switched_, readyCounter_, spin_, cycles_,
           nibblesRead);
        ar(wrState_, wrSync_, wrAddrPos_, wrDataPos_,
           wrTrack_, wrHead_, wrSector_, wrData_);
        ar.blob(gcrWrBuf_);
        cellsDirty_ = true;                      // cells_ is derived from flux_
    }

private:
    void encodeTrack();
    void refreshStream();                        // stream_ only, flux_ intact
    void encodeTrackGcr();
    void encodeTrackMfm();
    void buildCells();
    void fluxSeedFromCells();                    // canonical track -> store
    void rebuildCellsFromFlux();                 // store -> separator -> cells
    const std::vector<uint8_t>& cellsView();     // rebuilds if dirty
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
    // THE MEDIUM (§ 1.3 step 5): transition times, sorted, in [0, fluxRev_).
    // fluxRev_ is carried rather than computed from a cell count because the
    // store no longer has one — an off-rate track holds fewer, wider cells
    // in the same revolution, and the revolution is what is fixed.
    std::vector<int64_t> flux_;
    int64_t fluxRev_ = 0;
    // The separator's reading of flux_ — derived, rebuilt lazily, consumed
    // only by the offline write-back decoders. Running it through a real
    // FluxPll rather than quantizing on the nominal grid is what lets those
    // decoders verify a track the guest wrote off-rate.
    std::vector<uint8_t> cells_;
    bool cellsDirty_ = true;
    // Cell period the derivation clocks at: the medium's nominal rate until
    // a controller writes at its own, then that one (see commitFlux).
    int64_t decodeCellTicks_ = 0;                // 0 = nominal
    // Set while commitFlux() is decoding: writeSector() then refreshes the
    // legacy nibble stream only, instead of re-laying the whole track
    // canonically over the flux the controller just wrote.
    bool inFluxCommit_ = false;
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
    // Index pulses left before /READY asserts (MAME m_ready_counter).
    int readyCounter_ = 0;
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
