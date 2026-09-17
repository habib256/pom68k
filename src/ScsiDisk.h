// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── SCSI direct-access target (hard disk) ──
// A minimal SCSI-1 disk: the command subset classic Mac OS needs — TEST
// UNIT READY, REQUEST SENSE, INQUIRY, READ CAPACITY, READ(6)/(10),
// WRITE(6)/(10), MODE SENSE. Backing store is a raw 512-byte-block image
// (.vhd/.hda/.img/.dsk) loaded whole into memory; with `writeBack` each WRITE
// is also written through to the backing file immediately (crash-safe,
// no exit-time flush). Tests attach without it so reference images are
// never modified.
//
// Bare HFS volumes (boot blocks 'LK' at LBA 0 — Infinite Mac / Basilisk
// flat `.dsk`) are auto-wrapped in memory: a DDM ('ER') + partition map +
// Apple_Driver43 template is prepended so ROM StartBoot sees a real SCSI
// disk. The HFS payload stays at LBA 96; write-back maps those LBAs back
// onto the original file. Template search: $POM68K_SCSI_DDM_TEMPLATE, then
// HD20SC.vhd / boot.vhd beside the image or under hdv/. Offline alternative:
// tools/wrap_hfs.py.
// The same class also serves a **CD-ROM** target (`openCdrom`): SCSI type
// $05, removable, read-only, 2048-byte blocks, plus READ TOC, START/STOP
// UNIT, PREVENT/ALLOW REMOVAL and — the load-bearing one — the Apple
// magic MODE SENSE page $30 carrying "APPLE COMPUTER, INC", which is what
// Apple's CD-ROM driver checks before it will bind to a drive
// (MAME bus/nscsi/cd.cpp:604-618).
// Source of truth: MAME nscsi_hd.cpp + bus/nscsi/cd.cpp; SCSI-1 (SASI)
// spec; DEV.md § SCSI.
// Gate: tests/scsi_boot_etalon.cpp, tests/scsi_pdma_test.cpp,
//       tests/scsi_hfs_facade_test.cpp, tests/scsi_cdrom_test.cpp.

#pragma once
#include "CoreConfig.h"
#include "FloppySoundSink.h"
#include "CdAudioSink.h"
#include "SaveState.h"
#include "ScsiTarget.h"
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

// Apple Driver Map `ER` + `sbBlkSize`, or a bare HFS `BD` at 1024. 512 means
// a Toast/DDM dump that must be a SCSI disk; 2048 means a real CD; 0 means
// the prefix does not declare one. Shared by `openCdrom` and the Disques
// router so a .toast is not sent to the CD bay just because of its name.
uint32_t scsiAppleImageBlockSize(const uint8_t* prefix, size_t n);
uint32_t scsiAppleImageBlockSize(const std::string& path);

class ScsiDisk : public ScsiTarget {
public:
    void configure(const pom68k::CoreStorageConfig& config) {
        ddmTemplate_ = config.ddmTemplate.value_or(std::string());
        trace_ = config.scsiTrace;
        cdTrace_ = config.cdTrace;
        ownInquiry_ = config.ownScsiInquiry;
    }
    // Two personalities on one target, because both SCSI controllers
    // (`Ncr5380`, `Ncr53c96`) and all 32 machines route to `ScsiDisk*`:
    // a CD-ROM differs from a hard disk in its INQUIRY type, its 2048-byte
    // blocks, being removable and read-only, and a handful of extra
    // commands — not in how it is wired. MAME derives its cdrom from a
    // shared base for the same reason (`bus/nscsi/cd.cpp`).
    // ── Three targets, and the third one exists because the images do ──
    // `Disk` is a fixed 512-byte volume; `Cdrom` is a 2048-byte read-only
    // removable. `Removable` is what an awful lot of "CD images" in
    // circulation actually are: a dump of a Mac disc taken at **512** bytes
    // per block, which says so in its Apple driver descriptor (`ER` at
    // offset 0, `sbBlkSize` at +2) or simply carries a bare HFS `BD` at
    // 1024 with no descriptor at all. Handing those to the guest through a
    // 2048-byte CD target puts every partition offset four times too far in
    // and Mac OS mounts nothing — the symptom the 2026-07-29 note in
    // `q605_cdrom_etalon` recorded as "read but not mounted, cause not yet
    // established". The cause is the block size, measured 2026-08-15: the
    // same `Apeiron_1_0_3.toast` that never appears through the CD bay
    // mounts instantly as a 512-byte target.
    // So `openCdrom` reads the medium and picks: a real 2048 disc is a
    // `Cdrom`, a 512 dump is a `Removable` — a removable direct-access
    // device, which is what it is, served by the hard-disk command set with
    // the CD's presence and medium-change behaviour.
    enum class Kind { Disk, Cdrom, Removable };

    bool open(const std::string& path, bool writeBack = false);
    // Mount a CD image (.iso/.cdr/.toast — raw 2048-byte MODE1 sectors).
    // Always read-only; `eject()` empties the drive but keeps the target
    // present, so the guest sees an empty drive rather than no device.
    // Accepts: raw MODE1 (.iso/.cdr/.toast, 2048-byte sectors), raw
    // MODE1/2352 (.bin — sync+header+data+ECC, user data extracted), and
    // a .cue sheet naming a .bin — read WHOLE since 2026-09-17, so a
    // mixed-mode disc answers READ TOC for its audio tracks. READ(10) still
    // serves the data track only, and playback is not built; audio tracks are
    // catalogued for READ TOC but not played — see the CDDA TODO).
    bool openCdrom(const std::string& path);
    // An empty CD drive on the bus: the target answers INQUIRY and reports
    // NOT READY, so the guest's driver polls it — exactly what a real drive
    // with no disc does. Media arriving later (openCdrom on this target)
    // raises UNIT ATTENTION / $28 and the Finder mounts it, no reboot.
    void attachCdromEmpty();
    void eject();
    // The reverse of open() for a FIXED disk: the image is dropped, the
    // write-back stream closed (every WRITE already reached it), the
    // write log reset, and the target is gone — present() is false, the
    // controller's selection times out, and the slot is a fresh ScsiDisk
    // for whatever attaches next. Not a medium change: a fixed disk never
    // owes UNIT ATTENTION, and this is the cable coming out, not a tray.
    // A CD bay keeps its drive across an eject(); it never closes.
    void close();
    // "This bay is the removable kind" — what the platforms' `bayIsCdrom`
    // and the Disques window ask, and the answer is the same for both
    // removable kinds: its medium can be swapped without a reboot.
    bool cdrom() const { return kind_ != Kind::Disk; }
    bool mediumPresent() const { return blocks_ > 0; }
    uint32_t blockSize() const { return kind_ == Kind::Cdrom ? 2048u : 512u; }

    // A removable target exists even with no medium in it; a fixed disk
    // does not.
    bool present() const override {
        return kind_ == Kind::Disk ? blocks_ > 0 : attached_;
    }
    uint32_t blocks() const { return blocks_; }
    // A disc is in the tray when it carries user data OR audio tracks. An
    // AUDIO CD has zero data blocks and is very much present: the Apple
    // CD-ROM extension mounts it from the TOC alone, and judging presence
    // by `blocks_` alone made every audio disc read as an empty drive.
    bool discLoaded() const { return blocks_ > 0 || audioOnly_; }
    // Per-target traffic. A gate that asserts on the CONTROLLER's total
    // cannot tell a mounted CD from an ignored one — the boot volume's
    // traffic drowns it (measured: 9619 vs 9618).
    long readCommands = 0, readBlocks = 0;
    // The write half. Added for the beyond-boot persist gates, which judge
    // on the image bytes and so cannot tell "the guest never wrote" from
    // "the guest wrote the same bytes back" — the first is an input or a
    // read-only-mount defect, the second is a Finder that did nothing.
    long writeCommands = 0, writeBlocks = 0;

    // True when open() applied the in-memory HFS-flat → SCSI façade.
    // Move a running CD-DA play on by `micros` of MACHINE time. A no-op
    // unless a play is in flight; sets Completed at the end address.
    void advanceAudio(uint64_t micros);
    // The platform form: machine time arrives as CPU cycles, and the
    // conversion carries its remainder so a long play cannot drift away
    // from 75 sectors a second. Cheap when nothing is playing.
    void advanceAudioCycles(int64_t cycles, int64_t cpuHz) {
        if (audio_ != Audio::Playing || cycles <= 0 || cpuHz <= 0) return;
        audioCycAcc_ += cycles * 1000000;
        const int64_t micros = audioCycAcc_ / cpuHz;
        audioCycAcc_ -= micros * cpuHz;
        if (micros > 0) advanceAudio(uint64_t(micros));
    }
    // Transport state for the gates: 0 stopped, 1 playing, 2 paused,
    // 3 completed — the same order READ SUBCHANNEL reports.
    int audioState() const { return int(audio_); }
    uint32_t audioLba() const { return audioLba_; }

    // The disc's tracks as the .cue described them (empty for a flat image).
    // `scsi_cdrom_test` reads these; the audio path will too.
    std::size_t trackCount() const { return tracks_.size(); }
    bool trackIsAudio(std::size_t i) const { return tracks_[i].audio; }
    uint32_t trackStartLba(std::size_t i) const { return tracks_[i].startLba; }

    bool flatHfsFacade() const { return hfsPrefixBlocks_ != 0; }
    uint32_t hfsPrefixBlocks() const { return hfsPrefixBlocks_; }

    // A HOST write: the same path as a guest WRITE — copy-on-first-write
    // log, in-memory image, write-back stream — without the guest's
    // traffic counters or drive sounds. HfsInject.h puts « POM68K
    // Disques » into the boot volume's Startup Items through it.
    void hostWrite(uint32_t lba, const uint8_t* data, uint32_t count);

    // In-memory image access — direct pokes bypass the write-back stream
    // (never reach the backing file). Used by tests to inject a $6A DDM
    // driver entry so an otherwise-bootable disk passes the LC II ROM's
    // boot scan ($A07264).
    std::vector<uint8_t>& image() { return image_; }

    // Execute a CDB. Fills `dataOut` with the bytes to return to the
    // initiator (DATA IN phase) and returns the SCSI status byte (0 = GOOD,
    // 2 = CHECK CONDITION). `dataIn` carries WRITE payload (unused for now).
    uint8_t command(const uint8_t* cdb, int cdbLen,
                    std::vector<uint8_t>& dataOut,
                    const std::vector<uint8_t>& dataIn) override;

    // ── DATA OUT sizing (asked by the controller BEFORE command() runs) ──
    // A controller has to know how many bytes to handshake out of the
    // initiator before the target can act, and that count lives in the CDB
    // — differently for every command. It belongs to the target, not to the
    // controller: both Ncr5380 and Ncr53c96 used to carry their own partial
    // copy of this table, which is why MODE SELECT worked on the Plus and
    // not on the Quadra.
    int writeByteCount(const uint8_t* cdb, int cdbLen) const override;
    // ── Logical units ───────────────────────────────────────────────────
    // This target implements LUN 0 only, which is what every Macintosh
    // drive of the era was. What matters is that it says so the way SCSI-2
    // requires rather than answering LUN 3 with LUN 0's disk: INQUIRY to an
    // unsupported LUN reports peripheral qualifier 011b + device type $1F
    // (§ 8.2.5.1, and MAME's nscsi_hd/nscsi_cd write the same `$7f` into
    // byte 0), REQUEST SENSE answers GOOD carrying ILLEGAL REQUEST /
    // LOGICAL UNIT NOT SUPPORTED (§ 8.2.14), and everything else is a
    // CHECK CONDITION with that same sense (§ 7.5.3).
    // Gate: tests/scsi_target_test.cpp § 12.
    void selectLun(std::uint8_t lun) override { identifyLun_ = lun; }
    // A few commands carry their real length INSIDE the first bytes rather
    // than in the CDB (the 4-byte defect-list header of FORMAT UNIT and
    // REASSIGN BLOCKS). Called each time the gather reaches `expected`;
    // returns the new total, or `expected` when there is nothing to extend.
    std::size_t extendDataOut(const uint8_t* cdb, int cdbLen,
                              const std::vector<uint8_t>& sofar,
                              std::size_t expected) const override;

    // Mechanical-sound consumer (GUI only; tests leave it null). READs
    // and WRITEs post kNoStamp step events — the sink's auto-motor-off
    // retires the spin loop once the disk goes idle.
    void setSoundSink(FloppySoundSink* s) { sound_ = s; }

    // The CD-audio lead (GUI only; tests attach a counting stub). Every
    // sector the play head passes over is handed over raw — see
    // CdAudioSink.h for why this is not the ASC's business.
    void setCdAudioSink(CdAudioSink* s) { cdAudio_ = s; }

    // ── Save states ─────────────────────────────────────────────────────
    // A snapshot must not carry the image: it is hundreds of megabytes and
    // it already exists on the host. It carries what the GUEST has changed
    // since open() — otherwise a restore hands the guest a disk that
    // disagrees with the HFS structures cached in its RAM, a corruption no
    // boot-signature gate can see.
    //
    // Restoring needs more than the modified blocks, though. A block the
    // guest wrote AFTER the snapshot is not in the snapshot's set, and its
    // pristine content is no longer anywhere in memory — so writes are
    // logged copy-on-first-write: `pristine_` keeps the original bytes of
    // every block written since open(). Restore reverts the whole log, then
    // replays the snapshot's blocks. Exact, and it costs only what the
    // guest actually wrote (typically a few MB), not the image size.
    //
    // Caveat, deliberate: with write-back on, the HOST FILE has already
    // received those writes and a restore does not un-write it. The file
    // lives outside the snapshot by design (tests run write-back off).
    template <class Ar> void visit(Ar& ar) {
        ar(blocks_, hfsPrefixBlocks_, senseKey_, senseAsc_, senseAscq_,
           identifyLun_, readCommands, readBlocks);
        // The CD-DA transport is guest state: a snapshot taken mid-play must
        // resume mid-play, at the same sector. The TRACK TABLE is not — it
        // came from the .cue the machine was set up with, like the path.
        ar(audio_, audioLba_, audioEnd_, audioFrac_, audioCycAcc_);
        // Attachment properties (path, kind, write-back, the backing
        // stream) belong to the machine's setup, not to guest state, and
        // are deliberately NOT restored from a snapshot.
        const std::uint32_t bs = blockSize();
        if constexpr (Ar::loading) {
            revertToPristine();                    // back to the on-open image
            const std::uint64_t n = ar.varint();
            if (!ar.ok() || n > blocks_ + 1ull) { ar.fail(); return; }
            std::vector<uint8_t> buf(bs);
            for (std::uint64_t i = 0; i < n && ar.ok(); ++i) {
                std::uint32_t blk = 0;
                ar(blk);
                ar.bytes(buf.data(), bs);
                if (ar.ok()) applySnapshotBlock(blk, buf.data());
            }
        } else {
            ar.varint(dirtyList_.size());
            for (std::uint32_t blk : dirtyList_) {
                ar(blk);
                ar.bytes(image_.data() + std::uint64_t(blk) * bs, bs);
            }
        }
    }

    // Blocks the guest has written since open() — the snapshot's payload.
    std::size_t dirtyBlocks() const { return dirtyList_.size(); }

private:
    std::string ddmTemplate_;
    bool trace_ = false;
    bool cdTrace_ = false;
    bool ownInquiry_ = false;
    // Copy-on-first-write log (see visit()).
    void resetWriteLog();
    void markDirty(uint32_t lba, uint32_t count);
    void revertToPristine();
    void applySnapshotBlock(uint32_t blk, const uint8_t* data);
    void read(uint32_t lba, uint32_t count, std::vector<uint8_t>& out);
    void write(uint32_t lba, uint32_t count, const std::vector<uint8_t>& in);
    // Shared by write() and hostWrite(): log, copy, write through.
    void store(uint32_t lba, uint32_t count, const uint8_t* in, size_t inSize);
    void setSense(uint8_t key, uint8_t asc, uint8_t ascq = 0);
    // Effective LUN for this CDB: the IDENTIFY's if the connection carried
    // one, else the CDB's SCSI-1 byte-1 field (ScsiTarget::selectLun).
    uint8_t effectiveLun(const uint8_t* cdb, int cdbLen) const;
    // MODE SENSE(6) and (10) share a body; `ten` picks the header shape.
    uint8_t modeSense(const uint8_t* cdb, bool ten, std::vector<uint8_t>& out);
    // MODE SELECT's parameter list, for the one page a CD-ROM must really
    // honour: $0E, the CD Audio Control page. Everything else is still
    // accepted and ignored — see the command handler for why.
    void modeSelect(const std::vector<uint8_t>& params, bool ten);
    bool applyFlatHfsFacade(const std::string& imagePath);

    // One entry per .cue TRACK, in sheet order: the TOC a mixed-mode disc
    // owes the guest. Empty for a flat image, which is one data track by
    // construction and answers from `blocks_` as it always did.
    struct CdTrack {
        uint8_t number = 0;
        bool audio = false;
        uint32_t startLba = 0;       // absolute, from INDEX 01
    };
    std::vector<CdTrack> tracks_;
    uint32_t discLba_ = 0;           // lead-out: sectors on the whole disc

    // CD-DA transport. The guest starts a play with PLAY AUDIO and watches
    // it with READ SUBCHANNEL, so the position has to MOVE: advanceAudio()
    // is driven by the machine's own tick, never by host wall time. No
    // samples leave the drive yet — the ASC path is the next step and the
    // header says so rather than letting a moving counter imply sound.
    enum class Audio : uint8_t { Stopped, Playing, Paused, Completed };
    Audio audio_ = Audio::Stopped;
    uint32_t audioLba_ = 0, audioEnd_ = 0;
    uint64_t audioFrac_ = 0;         // sub-sector micros carried between ticks
    int64_t  audioCycAcc_ = 0;       // cycle→micro remainder (advanceAudioCycles)
    CdAudioSink* cdAudio_ = nullptr;

    // The audio tracks are NOT in image_: open() cuts a mixed disc down to
    // the data track's extent, because de-framing audio sectors would turn
    // music into "user data". Playing them means going back to the file,
    // which is what these two are for — the path the .cue named and a
    // stream held open while a disc is loaded.
    std::string rawPath_;            // empty unless a 2352-framed .bin
    std::ifstream rawFile_;
    bool readRawSector(uint32_t lba, uint8_t* out2352);

    std::vector<uint8_t> image_;     // raw sectors (possibly façade-prefixed)
    std::fstream file_;              // write-back stream (open iff writeBack_)
    bool writeBack_ = false;
    Kind kind_ = Kind::Disk;
    bool attached_ = false;          // CD drive exists (disc may be absent)
    bool audioOnly_ = false;         // every track is AUDIO: no user data
    // One CHECK CONDITION / $28 owed on the next command after a medium
    // change (not serialized: a pending attention is a mount edge, not
    // guest state — re-inserting after a restore re-arms it).
    bool unitAttention_ = false;
    uint32_t blocks_ = 0;
    // Non-zero when image_ has a synthetic DDM/PM/driver prefix; HFS file
    // bytes begin at this LBA and write-back subtracts it from the LBA.
    uint32_t hfsPrefixBlocks_ = 0;
    // Fixed-format sense (SCSI-2 § 8.2.14): key + ASC + ASCQ, cleared by the
    // REQUEST SENSE that reads it and by the receipt of any other command.
    uint8_t senseKey_ = 0, senseAsc_ = 0, senseAscq_ = 0;
    // LUN of the current connection's IDENTIFY, or kNoIdentify when the
    // initiator sent none. Set at selection by the controller; part of a
    // mid-connection snapshot exactly as the controllers' phase_ is.
    uint8_t identifyLun_ = kNoIdentify;
    FloppySoundSink* sound_ = nullptr;

    // Save-state write log. `dirtyBits_` answers "already logged?" in O(1);
    // `dirtyList_` keeps the block indices in first-write order; `pristine_`
    // holds their pre-write contents, one blockSize() slot per list entry.
    std::vector<uint64_t> dirtyBits_;
    std::vector<uint32_t> dirtyList_;
    std::vector<uint8_t>  pristine_;
};
