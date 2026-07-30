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
#include "FloppySoundSink.h"
#include "SaveState.h"
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

class ScsiDisk {
public:
    // Two personalities on one target, because both SCSI controllers
    // (`Ncr5380`, `Ncr53c96`) and all 32 machines route to `ScsiDisk*`:
    // a CD-ROM differs from a hard disk in its INQUIRY type, its 2048-byte
    // blocks, being removable and read-only, and a handful of extra
    // commands — not in how it is wired. MAME derives its cdrom from a
    // shared base for the same reason (`bus/nscsi/cd.cpp`).
    enum class Kind { Disk, Cdrom };

    bool open(const std::string& path, bool writeBack = false);
    // Mount a CD image (.iso/.cdr/.toast — raw 2048-byte MODE1 sectors).
    // Always read-only; `eject()` empties the drive but keeps the target
    // present, so the guest sees an empty drive rather than no device.
    // Accepts: raw MODE1 (.iso/.cdr/.toast, 2048-byte sectors), raw
    // MODE1/2352 (.bin — sync+header+data+ECC, user data extracted), and
    // a .cue sheet naming a .bin (first data track only; audio tracks are
    // catalogued for READ TOC but not played — see the CDDA TODO).
    bool openCdrom(const std::string& path);
    void eject();
    bool cdrom() const { return kind_ == Kind::Cdrom; }
    bool mediumPresent() const { return blocks_ > 0; }
    uint32_t blockSize() const { return kind_ == Kind::Cdrom ? 2048u : 512u; }

    // A CD-ROM target exists even with no disc in it; a hard disk does not.
    bool present() const { return kind_ == Kind::Cdrom ? attached_ : blocks_ > 0; }
    uint32_t blocks() const { return blocks_; }
    // Per-target traffic. A gate that asserts on the CONTROLLER's total
    // cannot tell a mounted CD from an ignored one — the boot volume's
    // traffic drowns it (measured: 9619 vs 9618).
    long readCommands = 0, readBlocks = 0;

    // True when open() applied the in-memory HFS-flat → SCSI façade.
    bool flatHfsFacade() const { return hfsPrefixBlocks_ != 0; }
    uint32_t hfsPrefixBlocks() const { return hfsPrefixBlocks_; }

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
                    const std::vector<uint8_t>& dataIn);

    // Mechanical-sound consumer (GUI only; tests leave it null). READs
    // and WRITEs post kNoStamp step events — the sink's auto-motor-off
    // retires the spin loop once the disk goes idle.
    void setSoundSink(FloppySoundSink* s) { sound_ = s; }

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
        ar(blocks_, hfsPrefixBlocks_, senseKey_, senseAsc_, readCommands, readBlocks);
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
    // Copy-on-first-write log (see visit()).
    void resetWriteLog();
    void markDirty(uint32_t lba, uint32_t count);
    void revertToPristine();
    void applySnapshotBlock(uint32_t blk, const uint8_t* data);
    void read(uint32_t lba, uint32_t count, std::vector<uint8_t>& out);
    void write(uint32_t lba, uint32_t count, const std::vector<uint8_t>& in);
    void setSense(uint8_t key, uint8_t asc);
    bool applyFlatHfsFacade(const std::string& imagePath);

    std::vector<uint8_t> image_;     // raw sectors (possibly façade-prefixed)
    std::fstream file_;              // write-back stream (open iff writeBack_)
    bool writeBack_ = false;
    Kind kind_ = Kind::Disk;
    bool attached_ = false;          // CD drive exists (disc may be absent)
    uint32_t blocks_ = 0;
    // Non-zero when image_ has a synthetic DDM/PM/driver prefix; HFS file
    // bytes begin at this LBA and write-back subtracts it from the LBA.
    uint32_t hfsPrefixBlocks_ = 0;
    uint8_t senseKey_ = 0, senseAsc_ = 0;
    FloppySoundSink* sound_ = nullptr;

    // Save-state write log. `dirtyBits_` answers "already logged?" in O(1);
    // `dirtyList_` keeps the block indices in first-write order; `pristine_`
    // holds their pre-write contents, one blockSize() slot per list entry.
    std::vector<uint64_t> dirtyBits_;
    std::vector<uint32_t> dirtyList_;
    std::vector<uint8_t>  pristine_;
};
