// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// DiskTimeline — contract and rationale in DiskTimeline.h.
//
// Journal file (`<image>.pomundo`), little-endian:
//   header  "POMUNDO1"  u32 blockSize  u32 blocks
//   record  'E' u64 digest                      (a save: an epoch)
//         | 'B' u32 lba  blockSize bytes        (bytes before a write)

#include "DiskTimeline.h"

#include "AtomicReplace.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <unordered_map>

namespace {

constexpr char kMagic[8] = {'P','O','M','U','N','D','O','1'};
constexpr std::size_t kHeader = 16;

std::uint64_t mix(std::uint64_t x) noexcept {        // splitmix64 finalizer
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ull;
    x ^= x >> 27; x *= 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

void put32(std::uint8_t* p, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = std::uint8_t(v >> (8 * i));
}
void put64(std::uint8_t* p, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) p[i] = std::uint8_t(v >> (8 * i));
}
std::uint32_t get32(const std::uint8_t* p) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= std::uint32_t(p[i]) << (8 * i);
    return v;
}
std::uint64_t get64(const std::uint8_t* p) {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= std::uint64_t(p[i]) << (8 * i);
    return v;
}

bool testSet(std::vector<std::uint64_t>& bits, std::uint32_t i) {
    const std::uint64_t bit = 1ull << (i & 63);
    std::uint64_t& w = bits[i >> 6];
    const bool was = (w & bit) != 0;
    w |= bit;
    return was;
}

// Walks a journal's records. `onEpoch(offset, digest)` and
// `onBlock(offset, lba, bytes-or-null)` see each complete record; returns the
// end of the last complete one (a crash can leave a torn tail).
template <class E, class B>
long walkJournal(std::FILE* f, std::uint32_t bs, std::uint32_t blocks,
                 bool wantBytes, E onEpoch, B onBlock) {
    long at = long(kHeader);
    if (std::fseek(f, at, SEEK_SET) != 0) return at;
    std::vector<std::uint8_t> buf(bs);
    for (;;) {
        const int tag = std::fgetc(f);
        std::uint8_t n[8];
        if (tag == 'E') {
            if (std::fread(n, 1, 8, f) != 8) break;
            onEpoch(at, get64(n));
            at += 9;
        } else if (tag == 'B') {
            if (std::fread(n, 1, 4, f) != 4) break;
            const std::uint32_t lba = get32(n);
            if (lba >= blocks) break;
            if (wantBytes) {
                if (std::fread(buf.data(), 1, bs, f) != bs) break;
            } else {
                // Skip without reading — but a torn final record must not
                // count, so confirm its last byte exists.
                if (std::fseek(f, long(bs) - 1, SEEK_CUR) != 0
                    || std::fgetc(f) == EOF) break;
            }
            onBlock(at, lba, wantBytes ? buf.data() : nullptr);
            at += long(5 + bs);
        } else {
            break;
        }
    }
    return at;
}

}  // namespace

std::uint64_t DiskTimeline::blockHash(std::uint32_t lba, const std::uint8_t* p,
                                      std::uint32_t n) noexcept {
    std::uint64_t h = mix(0x9e3779b97f4a7c15ull ^ lba);
    std::uint32_t i = 0;
    for (; i + 8 <= n; i += 8) {
        // Little-endian by definition: the digest is stored in states that
        // move between hosts.
        const std::uint64_t w = get64(p + i);
        h = (h ^ mix(w + i)) * 0x9fb21c651e98df25ull;
    }
    for (; i < n; ++i) h = (h ^ p[i]) * 0x100000001b3ull;
    return mix(h);
}

void DiskTimeline::attach(const std::uint8_t* image, std::uint64_t size,
                          std::uint32_t blockSize, std::string journalPath,
                          std::string name) {
    detach();
    if (!blockSize) return;
    name_ = std::move(name);
    blockSize_ = blockSize;
    blocks_ = std::uint32_t(size / blockSize);
    dirtyBits_.assign((std::size_t(blocks_) + 63) / 64, 0);
    epochBits_.assign(dirtyBits_.size(), 0);
    digest_ = fullDigest(image);
    stale_ = false;
    journalPath_ = std::move(journalPath);
    if (!journalPath_.empty()) openJournal();
}

void DiskTimeline::detach() {
    if (journal_) std::fclose(journal_);
    journal_ = nullptr;
    journalPath_.clear();
    blockSize_ = 0;
    blocks_ = 0;
    digest_ = 0;
    stale_ = false;
    dirtyBits_.clear();
    dirtyList_.clear();
    pristine_.clear();
    pristine_.shrink_to_fit();
    epochBits_.clear();
}

std::uint64_t DiskTimeline::fullDigest(const std::uint8_t* image) const {
    std::uint64_t d = 0;
    for (std::uint32_t b = 0; b < blocks_; ++b)
        d ^= blockHash(b, image + std::uint64_t(b) * blockSize_, blockSize_);
    return d;
}

std::uint64_t DiskTimeline::digest(const std::uint8_t* image) {
    if (stale_) { digest_ = fullDigest(image); stale_ = false; }
    return digest_;
}

void DiskTimeline::willWrite(std::uint32_t lba, const std::uint8_t* cur,
                             const std::uint8_t* next) {
    if (!attached() || lba >= blocks_) return;
    const std::uint32_t bs = blockSize_;
    if (!testSet(dirtyBits_, lba)) {
        dirtyList_.push_back(lba);
        pristine_.insert(pristine_.end(), cur, cur + bs);
    }
    if (journal_ && !testSet(epochBits_, lba)) {
        std::uint8_t h[5] = {'B'};
        put32(h + 1, lba);
        // Flushed before the caller touches the image: a crash may lose
        // this record's write, never the record of a write that happened.
        if (std::fwrite(h, 1, 5, journal_) != 5
            || std::fwrite(cur, 1, bs, journal_) != bs
            || std::fflush(journal_) != 0)
            dropJournal("write failed");
    }
    if (!stale_) digest_ ^= blockHash(lba, cur, bs) ^ blockHash(lba, next, bs);
}

// ── Journal file ────────────────────────────────────────────────────────
void DiskTimeline::dropJournal(const char* why) {
    std::fprintf(stderr, "DiskTimeline: %s: %s — journal off; states saved "
                 "before this point may become unrestorable\n",
                 journalPath_.c_str(), why);
    if (journal_) std::fclose(journal_);
    journal_ = nullptr;
}

void DiskTimeline::openJournal() {
    journal_ = std::fopen(journalPath_.c_str(), "r+b");
    bool fresh = journal_ == nullptr;
    if (journal_) {
        std::uint8_t h[kHeader];
        if (std::fread(h, 1, kHeader, journal_) != kHeader
            || std::memcmp(h, kMagic, 8) != 0
            || get32(h + 8) != blockSize_ || get32(h + 12) != blocks_) {
            // Another geometry (or not a journal): its history cannot
            // describe this image. Start over rather than misapply it.
            std::fprintf(stderr, "DiskTimeline: %s does not match this image "
                         "— starting a new journal\n", journalPath_.c_str());
            std::fclose(journal_);
            journal_ = nullptr;
            fresh = true;
        }
    }
    if (fresh) {
        journal_ = std::fopen(journalPath_.c_str(), "w+b");
        if (!journal_) { dropJournal("cannot create"); return; }
        std::uint8_t h[kHeader];
        std::memcpy(h, kMagic, 8);
        put32(h + 8, blockSize_);
        put32(h + 12, blocks_);
        if (std::fwrite(h, 1, kHeader, journal_) != kHeader
            || std::fflush(journal_) != 0) {
            dropJournal("cannot write header");
            return;
        }
        return;
    }
    // Drop a torn tail (a crash mid-record) so appends stay parseable, and
    // cut an oversized history at an epoch.
    const long end = walkJournal(journal_, blockSize_, blocks_, false,
                                 [](long, std::uint64_t) {},
                                 [](long, std::uint32_t, const std::uint8_t*) {});
    std::fseek(journal_, 0, SEEK_END);
    const long size = std::ftell(journal_);
    if (end != size) {
        std::fclose(journal_);
        journal_ = nullptr;
        std::error_code ec;
        std::filesystem::resize_file(journalPath_, std::uintmax_t(end), ec);
        journal_ = std::fopen(journalPath_.c_str(), "r+b");
        if (ec || !journal_) { dropJournal("cannot trim torn tail"); return; }
    }
    if (std::uint64_t(end) > journalCap_) compactJournal();
    if (journal_) std::fseek(journal_, 0, SEEK_END);
}

// Keep the newest epochs that fit in half the cap; everything before the
// cut becomes unreachable (those states will be refused, by name).
void DiskTimeline::compactJournal() {
    std::vector<long> epochs;
    const long end = walkJournal(journal_, blockSize_, blocks_, false,
        [&](long at, std::uint64_t) { epochs.push_back(at); },
        [](long, std::uint32_t, const std::uint8_t*) {});
    long cut = end;                                  // nothing fits: drop all
    for (long at : epochs)
        if (std::uint64_t(end - at) <= journalCap_ / 2) { cut = at; break; }
    const std::string tmp = journalPath_ + ".tmp";
    std::FILE* out = std::fopen(tmp.c_str(), "wb");
    bool ok = out != nullptr;
    if (ok) {
        std::uint8_t h[kHeader];
        std::memcpy(h, kMagic, 8);
        put32(h + 8, blockSize_);
        put32(h + 12, blocks_);
        ok = std::fwrite(h, 1, kHeader, out) == kHeader;
        std::vector<std::uint8_t> buf(1u << 16);
        std::fseek(journal_, cut, SEEK_SET);
        for (long left = end - cut; ok && left > 0; ) {
            const std::size_t n = std::size_t(std::min<long>(left, long(buf.size())));
            ok = std::fread(buf.data(), 1, n, journal_) == n
              && std::fwrite(buf.data(), 1, n, out) == n;
            left -= long(n);
        }
        ok = (std::fclose(out) == 0) && ok;
    }
    std::fclose(journal_);
    journal_ = nullptr;
    if (!ok || !atomicReplaceFile(tmp, journalPath_)) {
        std::remove(tmp.c_str());
        journal_ = std::fopen(journalPath_.c_str(), "r+b");
        if (!journal_) dropJournal("cannot reopen after failed compaction");
        return;
    }
    std::fprintf(stderr, "DiskTimeline: %s compacted (%ld → %ld bytes)\n",
                 journalPath_.c_str(), end, long(kHeader) + end - cut);
    journal_ = std::fopen(journalPath_.c_str(), "r+b");
    if (!journal_) dropJournal("cannot reopen after compaction");
}

void DiskTimeline::journalEpoch(std::uint64_t d) {
    if (!journal_) return;
    std::uint8_t r[9] = {'E'};
    put64(r + 1, d);
    if (std::fwrite(r, 1, 9, journal_) != 9 || std::fflush(journal_) != 0) {
        dropJournal("epoch write failed");
        return;
    }
    std::fill(epochBits_.begin(), epochBits_.end(), 0);
}

// The latest epoch whose digest is `target`; each block written after it
// takes the bytes of its first record there.
bool DiskTimeline::journalCandidate(std::uint64_t target, Map& out) const {
    if (journalPath_.empty()) return false;
    if (journal_) std::fflush(journal_);
    std::FILE* f = std::fopen(journalPath_.c_str(), "rb");
    if (!f) return false;
    long epoch = -1;
    walkJournal(f, blockSize_, blocks_, false,
        [&](long at, std::uint64_t d) { if (d == target) epoch = at; },
        [](long, std::uint32_t, const std::uint8_t*) {});
    if (epoch < 0) { std::fclose(f); return false; }
    std::vector<std::uint64_t> seen(dirtyBits_.size(), 0);
    walkJournal(f, blockSize_, blocks_, true,
        [](long, std::uint64_t) {},
        [&](long at, std::uint32_t lba, const std::uint8_t* p) {
            if (at > epoch && !testSet(seen, lba))
                out.emplace_back(lba, std::vector<std::uint8_t>(p, p + blockSize_));
        });
    std::fclose(f);
    return true;
}

// ── Save / restore ──────────────────────────────────────────────────────
std::uint64_t DiskTimeline::digestAfter(const std::uint8_t* image, const Map& m) {
    std::uint64_t d = digest(image);
    for (const auto& [lba, bytes] : m) {
        const std::uint8_t* cur = image + std::uint64_t(lba) * blockSize_;
        d ^= blockHash(lba, cur, blockSize_) ^ blockHash(lba, bytes.data(), blockSize_);
    }
    return d;
}

void DiskTimeline::load(sav::Reader& ar, const std::uint8_t* image,
                        std::vector<Block>& plan) {
    plan.clear();
    std::uint32_t bs = 0, n = 0;
    ar(bs, n);
    if (!ar.ok()) return;
    if (!bs) return;                     // no medium when saved: nothing to bind
    if (!attached()) {
        ar.fail("the state has a disk where this machine has none");
        return;
    }
    const std::string who = "disk " + name_ + ": ";
    if (bs != blockSize_ || n != blocks_) {
        ar.fail(who + "the attached image has another size than the state's");
        return;
    }
    const std::uint64_t count = ar.varint();
    if (!ar.ok() || count > blocks_) { ar.fail(); return; }
    Map snap;
    snap.reserve(std::size_t(count));
    std::vector<std::uint64_t> seen(dirtyBits_.size(), 0);
    for (std::uint64_t i = 0; i < count && ar.ok(); ++i) {
        std::uint32_t lba = 0;
        ar(lba);
        std::vector<std::uint8_t> bytes(bs);
        ar.bytes(bytes.data(), bs);
        if (!ar.ok() || lba >= blocks_ || testSet(seen, lba)) { ar.fail(); return; }
        snap.emplace_back(lba, std::move(bytes));
    }
    std::uint64_t target = 0;
    ar(target);
    if (!ar.ok()) return;

    // Candidate 1: the image as attached, plus the state's blocks.
    Map m;
    for (std::size_t i = 0; i < dirtyList_.size(); ++i)
        if (!(seen[dirtyList_[i] >> 6] & (1ull << (dirtyList_[i] & 63))))
            m.emplace_back(dirtyList_[i], std::vector<std::uint8_t>(
                pristine_.begin() + std::ptrdiff_t(i * bs),
                pristine_.begin() + std::ptrdiff_t((i + 1) * bs)));
    loadOrder_.clear();
    for (auto& e : snap) {
        loadOrder_.push_back(e.first);
        m.push_back(std::move(e));
    }
    if (digestAfter(image, m) != target) {
        // Candidate 2: rewind the backing file through its journal.
        m.clear();
        if (!journalCandidate(target, m) || digestAfter(image, m) != target) {
            ar.fail(who + (journalPath_.empty()
                ? "its content differs from the state's (the image changed "
                  "after the state was saved)"
                : "its content differs from the state's and its journal "
                  "cannot rewind it (" + journalPath_ + ")"));
            return;
        }
    }
    for (auto& [lba, bytes] : m)
        if (std::memcmp(image + std::uint64_t(lba) * bs, bytes.data(), bs) != 0)
            plan.push_back({lba, std::move(bytes)});
}

void DiskTimeline::commitLoad(const std::uint8_t* image) {
    if (!attached()) return;
    const std::uint32_t bs = blockSize_;
    // Opened content of a block: its log slot, or the image when this
    // process never wrote it (then the plan did not touch it either).
    std::unordered_map<std::uint32_t, std::size_t> slotOf;
    for (std::size_t i = 0; i < dirtyList_.size(); ++i) slotOf[dirtyList_[i]] = i;
    std::vector<std::uint32_t> list;
    std::vector<std::uint8_t> bytes;
    std::vector<std::uint64_t> bits(dirtyBits_.size(), 0);
    auto keep = [&](std::uint32_t lba, const std::uint8_t* opened) {
        testSet(bits, lba);
        list.push_back(lba);
        bytes.insert(bytes.end(), opened, opened + bs);
    };
    for (std::uint32_t lba : loadOrder_) {
        const auto it = slotOf.find(lba);
        keep(lba, it != slotOf.end() ? pristine_.data() + it->second * bs
                                     : image + std::uint64_t(lba) * bs);
    }
    for (std::size_t i = 0; i < dirtyList_.size(); ++i) {
        const std::uint32_t lba = dirtyList_[i];
        const std::uint8_t* opened = pristine_.data() + i * bs;
        if ((bits[lba >> 6] >> (lba & 63)) & 1) continue;
        if (std::memcmp(opened, image + std::uint64_t(lba) * bs, bs) == 0) continue;
        keep(lba, opened);
    }
    dirtyList_.swap(list);
    pristine_.swap(bytes);
    dirtyBits_.swap(bits);
    loadOrder_.clear();
}
