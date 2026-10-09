// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── DiskTimeline: which disk content a save state belongs to ──
// A save state carries RAM full of HFS structures the guest cached from its
// disk. Restoring that RAM over different disk content is a corruption no
// boot screenshot can see, so a writable block medium (ScsiDisk, AtaDisk)
// keeps its guest-visible history here, outside the serializer's
// CPU/device visitor:
//
//   * a content DIGEST, the XOR over every block of a keyed hash of
//     (LBA, bytes), updated on each write in O(block). The state records it;
//     a restore must reproduce it exactly or it is refused, by name.
//   * the copy-on-first-write log since attach: the pre-write bytes of every
//     block written since the image was opened. A state carries those
//     blocks' current bytes; a restore reverts the log and replays them. This
//     is exact whenever the backing file did not change underneath — every
//     write-back-off session and any same-process restore.
//   * with write-back, a persistent REVERSE JOURNAL beside the image
//     (`<image>.pomundo`). Each save appends an epoch marker holding the
//     digest; every later write first appends the block's previous bytes
//     (once per block per epoch, flushed before the image is touched). The
//     file is therefore a linear reverse-delta history of the image, and
//     restoring the state with digest D in any later process is: take the
//     latest epoch marked D, give each block written after it its first
//     recorded old bytes. A restore's own writes are journalled like any
//     other, so the history stays linear and every older state stays
//     reachable.
//
// Restore is planned before anything is touched: each candidate (log
// replay, then journal) yields a block map whose resulting digest is
// computed from the live one; only a candidate that lands exactly on the
// state's digest is applied, through the owner's normal write path (so the
// backing file follows). Nothing found → the Reader fails with the reason
// and the transactional load leaves the machine as it was.
//
// The journal is capped (kDefaultJournalCap): at attach an oversized one is
// cut at an epoch boundary, so the oldest states become refusable, never
// silently wrong. The base image stays the user's working file; this is the
// "immutable history, mutable working copy" variant of
// docs/SNOW_IMPLEMENTATION_PLAN.md § Backing images and rewind, chosen
// because it costs I/O proportional to guest writes, not to the image.
// Gate: tests/media_timeline_test.cpp.

#pragma once
#include "SaveState.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <type_traits>
#include <vector>

class DiskTimeline {
public:
    static constexpr std::uint64_t kDefaultJournalCap = 256ull << 20;

    DiskTimeline() = default;
    DiskTimeline(const DiskTimeline&) = delete;
    DiskTimeline& operator=(const DiskTimeline&) = delete;
    ~DiskTimeline() { detach(); }

    // Start a history for an image already in memory. `journalPath` empty =
    // no persistent journal (write-back off, read-only media, tests).
    // `name` identifies the medium in a refusal ("SCSI boot.vhd").
    void attach(const std::uint8_t* image, std::uint64_t size,
                std::uint32_t blockSize, std::string journalPath,
                std::string name);
    void detach();
    bool attached() const noexcept { return blockSize_ != 0; }

    // The owner is about to replace block `lba`, whose current bytes are
    // `cur`, with `next` (both blockSize() long). Must precede the copy.
    void willWrite(std::uint32_t lba, const std::uint8_t* cur,
                   const std::uint8_t* next);
    // The owner's image was poked directly (test hooks): the incremental
    // digest is recomputed on its next use.
    void imageTouched() noexcept { stale_ = true; }

    std::uint64_t digest(const std::uint8_t* image);
    std::size_t dirtyBlocks() const noexcept { return dirtyList_.size(); }
    std::uint32_t blockSize() const noexcept { return blockSize_; }
    const std::string& journalPath() const noexcept { return journalPath_; }
    bool journalling() const noexcept { return journal_ != nullptr; }
    void setJournalCap(std::uint64_t bytes) noexcept { journalCap_ = bytes; }

    // One block the restore must write, in application order.
    struct Block { std::uint32_t lba; std::vector<std::uint8_t> data; };

    // Writer: the blocks written since attach, the digest, and an epoch in
    // the journal. Reader: parse, plan, and either fill `plan` (empty when
    // nothing differs) or fail the Reader with the refusal's reason.
    // Any non-loading archive works (V8Memory's lockstep device hash walks
    // the same body); only a real sav::Writer marks a journal epoch.
    template <class Ar> void save(Ar& ar, const std::uint8_t* image) {
        std::uint32_t bs = blockSize_, n = blocks_;
        ar(bs, n);
        if (!attached()) return;
        ar.varint(dirtyList_.size());
        for (std::uint32_t blk : dirtyList_) {
            ar(blk);
            ar.bytes(image + std::uint64_t(blk) * bs, bs);
        }
        std::uint64_t d = digest(image);
        ar(d);
        if constexpr (std::is_same_v<Ar, sav::Writer>) journalEpoch(d);
    }
    void load(sav::Reader& ar, const std::uint8_t* image,
              std::vector<Block>& plan);
    // After the owner applied `plan`: rebuild the since-open log in the
    // state's own block order, so load→save is byte-identical
    // (q605_savestate_etalon). Blocks this process wrote that the restore
    // put back to their opened content leave the log.
    void commitLoad(const std::uint8_t* image);

    // Keyed block hash, exposed for the gate.
    static std::uint64_t blockHash(std::uint32_t lba, const std::uint8_t* p,
                                   std::uint32_t n) noexcept;

private:
    using Map = std::vector<std::pair<std::uint32_t, std::vector<std::uint8_t>>>;
    std::uint64_t fullDigest(const std::uint8_t* image) const;
    // Digest the image would have after `m` were applied to it.
    std::uint64_t digestAfter(const std::uint8_t* image, const Map& m);
    bool journalCandidate(std::uint64_t target, Map& out) const;
    void openJournal();
    void compactJournal();
    void journalEpoch(std::uint64_t d);
    void dropJournal(const char* why);

    std::uint32_t blockSize_ = 0;
    std::uint32_t blocks_ = 0;
    std::uint64_t digest_ = 0;
    bool stale_ = false;

    // Since-attach log: first-write order, one blockSize_ slot per entry.
    std::vector<std::uint64_t> dirtyBits_;
    std::vector<std::uint32_t> dirtyList_;
    std::vector<std::uint8_t>  pristine_;
    std::vector<std::uint32_t> loadOrder_;   // the state's, until commitLoad

    // Reverse journal.
    std::string name_;
    std::string journalPath_;
    std::FILE* journal_ = nullptr;
    std::vector<std::uint64_t> epochBits_;   // logged since the last epoch
    std::uint64_t journalCap_ = kDefaultJournalCap;
};
