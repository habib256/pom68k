// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Archive primitives: LEB128 varints, the zero-run codec for bulk buffers,
// the chunk container and the state fingerprint. Design notes in SaveState.h.

#include "SaveState.h"

namespace sav {

namespace {
// Below this, a zero run costs more to encode than to store verbatim: it
// ends the current literal run (2 varints ≈ 2 bytes) only to start another.
constexpr std::size_t kMinZeroRun = 4;

// Upper bound on a decoded bulk buffer, as a guard against a corrupt length
// turning into a wild allocation. It has to be ABSOLUTE, not a ratio of the
// encoded size: a zero run costs a varint regardless of length, so 64 KB of
// zeros encodes in 7 bytes (~9000:1) and any "encoded << k" bound rejects
// exactly the best-compressed data — which is what the gate caught. The
// value is well above the largest RAM any emulated machine carries (128 MB
// on a Quadra), so it constrains only corrupt input.
constexpr std::uint64_t kMaxBlob = 1ull << 30;
}

// ── Varints (LEB128) ────────────────────────────────────────────────────
void Writer::varint(u64 v) {
    do {
        u8 byte = static_cast<u8>(v & 0x7F);
        v >>= 7;
        if (v) byte |= 0x80;
        b_.push_back(byte);
    } while (v);
}

u64 Reader::varint() {
    u64 v = 0;
    for (int shift = 0; shift < 64; shift += 7) {
        if (!take(1)) return 0;
        const u8 byte = p_[at_ - 1];
        v |= static_cast<u64>(byte & 0x7F) << shift;
        if (!(byte & 0x80)) return v;
    }
    fail();                      // malformed: more than ten continuation bytes
    return 0;
}

// ── Bulk buffers: zero-run codec ────────────────────────────────────────
// Layout: total length, then (zeroRun, literalRun, literalBytes) triples
// until the total is reconstructed. Mac RAM after boot is largely zeros —
// unallocated heap, unused video pages, the gap above the System heap — so
// this does most of the work a general-purpose compressor would, at a
// fraction of the code and with no external dependency in the build.
void Writer::blob(const std::vector<u8>& v) {
    const std::size_t n = v.size();
    varint(n);

    std::size_t i = 0;
    while (i < n) {
        std::size_t z = 0;
        while (i + z < n && v[i + z] == 0) ++z;
        i += z;

        const std::size_t litStart = i;
        while (i < n) {
            if (v[i] == 0) {
                std::size_t k = 0;
                while (i + k < n && v[i + k] == 0) ++k;
                if (k >= kMinZeroRun) break;   // worth breaking the literal run
                i += k;                        // short gap: keep it verbatim
            } else {
                ++i;
            }
        }

        const std::size_t lit = i - litStart;
        varint(z);
        varint(lit);
        bytes(v.data() + litStart, lit);
    }
}

void Reader::blob(std::vector<u8>& v) {
    const u64 total = varint();
    // A corrupt length must not become a wild allocation (see kMaxBlob).
    if (!ok_ || total > kMaxBlob) { fail(); v.clear(); return; }

    v.assign(static_cast<std::size_t>(total), 0);
    std::size_t out = 0;
    while (out < v.size()) {
        const u64 z   = varint();
        const u64 lit = varint();
        if (!ok_) { v.clear(); return; }
        if (z > v.size() - out) { fail(); v.clear(); return; }
        out += static_cast<std::size_t>(z);          // zeros: already there
        if (lit > v.size() - out) { fail(); v.clear(); return; }
        bytes(v.data() + out, static_cast<std::size_t>(lit));
        out += static_cast<std::size_t>(lit);
        if (!ok_) { v.clear(); return; }
        if (z == 0 && lit == 0) { fail(); v.clear(); return; }   // no progress
    }
}

// ── Chunk container ─────────────────────────────────────────────────────
Chunk::Chunk(std::vector<u8>& out, const char tag[4]) : b_(out) {
    b_.insert(b_.end(), tag, tag + 4);
    lenAt_ = b_.size();
    b_.insert(b_.end(), 4, 0);           // patched by the destructor
}

Chunk::~Chunk() {
    const std::uint32_t len = static_cast<std::uint32_t>(b_.size() - lenAt_ - 4);
    for (std::size_t i = 0; i < 4; ++i)
        b_[lenAt_ + i] = static_cast<u8>(len >> (8 * i));
}

bool nextChunk(const u8*& cur, const u8* end, ChunkView& out) noexcept {
    if (end - cur < 8) return false;                  // no room for tag+length
    for (int i = 0; i < 4; ++i) out.tag[i] = static_cast<char>(cur[i]);

    std::uint32_t len = 0;
    for (int i = 0; i < 4; ++i)
        len |= static_cast<std::uint32_t>(cur[4 + i]) << (8 * i);

    if (static_cast<std::size_t>(end - cur - 8) < len) return false;   // truncated
    out.data = cur + 8;
    out.len  = len;
    cur      = out.data + len;
    return true;
}

// ── State fingerprint ───────────────────────────────────────────────────
u64 hash(const void* p, std::size_t n) noexcept {
    const u8* q = static_cast<const u8*>(p);
    u64 h = 1469598103934665603ull;                  // FNV-1a 64 offset basis
    for (std::size_t i = 0; i < n; ++i) {
        h ^= q[i];
        h *= 1099511628211ull;
    }
    return h;
}

} // namespace sav
