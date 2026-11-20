// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "ZipReader.h"

#include "Crc32.h"

#include <algorithm>
#include <array>

namespace pom68k::zip {
namespace {

std::uint32_t le16(const std::vector<std::uint8_t>& b, std::size_t o) {
    return std::uint32_t(b[o]) | std::uint32_t(b[o + 1]) << 8;
}

std::uint32_t le32(const std::vector<std::uint8_t>& b, std::size_t o) {
    return std::uint32_t(b[o]) | std::uint32_t(b[o + 1]) << 8 |
           std::uint32_t(b[o + 2]) << 16 | std::uint32_t(b[o + 3]) << 24;
}

// [B] APPNOTE § 4.3.16: 22 fixed bytes then a comment of up to 65535. A
// comment may itself contain the signature, so only a record whose declared
// comment length reaches exactly the end of the file is accepted.
bool findEnd(const std::vector<std::uint8_t>& z, std::size_t& eocd) {
    if (z.size() < 22) return false;
    const std::size_t maxBack = std::min<std::size_t>(z.size(), 22u + 65535u);
    for (std::size_t back = 22; back <= maxBack; ++back) {
        const std::size_t o = z.size() - back;
        if (z[o] == 0x50 && z[o + 1] == 0x4B && z[o + 2] == 0x05 && z[o + 3] == 0x06 &&
            le16(z, o + 20) == back - 22) {
            eocd = o;
            return true;
        }
    }
    return false;
}

// ── RFC 1951 inflate ─────────────────────────────────────────────────────
// Canonical Huffman decoding by code length counts: small, table-free and
// fast enough for megabyte ROMs.

struct Huffman {
    std::array<std::uint16_t, 16> counts{};
    std::array<std::uint16_t, 320> symbols{};
};

class Inflater {
public:
    Inflater(const std::uint8_t* in, std::size_t n, std::vector<std::uint8_t>& out,
             std::size_t maxBytes)
        : in_(in), n_(n), out_(out), max_(maxBytes) {}

    bool run(std::string& err) {
        bool last = false;
        while (!last) {
            last = bits(1) != 0;
            const unsigned type = bits(2);
            bool ok = false;
            if (type == 0) ok = stored();
            else if (type == 1) ok = fixed();
            else if (type == 2) ok = dynamic();
            else fail("reserved block type");
            if (!ok || error_) {
                if (!error_) fail("corrupt deflate stream");
                err = why_;
                return false;
            }
        }
        return true;
    }

private:
    void fail(const char* why) {
        if (!error_) { error_ = true; why_ = why; }
    }

    unsigned bits(int count) {
        unsigned v = 0;
        for (int i = 0; i < count; ++i) {
            if (bitCount_ == 0) {
                if (pos_ >= n_) { fail("truncated deflate stream"); return 0; }
                bitBuf_ = in_[pos_++];
                bitCount_ = 8;
            }
            v |= (bitBuf_ & 1u) << i;
            bitBuf_ >>= 1;
            --bitCount_;
        }
        return v;
    }

    bool emit(std::uint8_t b) {
        if (out_.size() >= max_) { fail("member inflates past the size cap"); return false; }
        out_.push_back(b);
        return true;
    }

    // [B] RFC 1951 § 3.2.2. An over-subscribed set of lengths is refused;
    // an incomplete one is legal (a one-code distance tree).
    bool build(Huffman& h, const std::uint8_t* lengths, std::size_t n) {
        h.counts.fill(0);
        for (std::size_t i = 0; i < n; ++i) h.counts[lengths[i]]++;
        h.counts[0] = 0;
        int left = 1;
        for (int len = 1; len < 16; ++len) {
            left <<= 1;
            left -= h.counts[std::size_t(len)];
            if (left < 0) { fail("over-subscribed Huffman code"); return false; }
        }
        std::array<std::uint16_t, 16> offs{};
        for (std::size_t len = 1; len < 16; ++len)
            offs[len] = std::uint16_t(offs[len - 1] + h.counts[len - 1]);
        // counts[0] is zero, so offs[1] == 0 and the walk below is exact.
        for (std::size_t i = 0; i < n; ++i)
            if (lengths[i]) h.symbols[offs[lengths[i]]++] = std::uint16_t(i);
        return true;
    }

    int decode(const Huffman& h) {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; ++len) {
            code |= int(bits(1));
            if (error_) return -1;
            const int count = h.counts[std::size_t(len)];
            if (code - count < first) return h.symbols[std::size_t(index + (code - first))];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        fail("invalid Huffman code");
        return -1;
    }

    bool stored() {
        bitBuf_ = 0;
        bitCount_ = 0;   // to a byte boundary
        if (pos_ > n_ || n_ - pos_ < 4) { fail("truncated stored block"); return false; }
        const unsigned len = unsigned(in_[pos_]) | unsigned(in_[pos_ + 1]) << 8;
        const unsigned nlen = unsigned(in_[pos_ + 2]) | unsigned(in_[pos_ + 3]) << 8;
        pos_ += 4;
        if ((len ^ 0xFFFFu) != nlen) { fail("stored block length check failed"); return false; }
        if (n_ - pos_ < len) { fail("truncated stored block"); return false; }
        for (unsigned i = 0; i < len; ++i)
            if (!emit(in_[pos_ + i])) return false;
        pos_ += len;
        return true;
    }

    bool codes(const Huffman& lit, const Huffman& dist) {
        static constexpr std::uint16_t kLenBase[29] = {
            3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
            35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static constexpr std::uint8_t kLenExtra[29] = {
            0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
            3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static constexpr std::uint16_t kDistBase[30] = {
            1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
            257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
            8193, 12289, 16385, 24577};
        static constexpr std::uint8_t kDistExtra[30] = {
            0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
            7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        for (;;) {
            const int sym = decode(lit);
            if (sym < 0) return false;
            if (sym < 256) {
                if (!emit(std::uint8_t(sym))) return false;
                continue;
            }
            if (sym == 256) return true;
            const int li = sym - 257;
            if (li >= 29) { fail("invalid length symbol"); return false; }
            const std::size_t len = kLenBase[li] + bits(kLenExtra[li]);
            const int ds = decode(dist);
            if (ds < 0) return false;
            if (ds >= 30) { fail("invalid distance symbol"); return false; }
            const std::size_t d = kDistBase[ds] + bits(kDistExtra[ds]);
            if (error_) return false;
            if (d > out_.size()) { fail("distance reaches before the output"); return false; }
            for (std::size_t i = 0; i < len; ++i)
                if (!emit(out_[out_.size() - d])) return false;
        }
    }

    bool fixed() {
        std::uint8_t l[288];
        std::fill(l, l + 144, std::uint8_t(8));
        std::fill(l + 144, l + 256, std::uint8_t(9));
        std::fill(l + 256, l + 280, std::uint8_t(7));
        std::fill(l + 280, l + 288, std::uint8_t(8));
        std::uint8_t d[30];
        std::fill(d, d + 30, std::uint8_t(5));
        Huffman lit, dist;
        return build(lit, l, 288) && build(dist, d, 30) && codes(lit, dist);
    }

    bool dynamic() {
        static constexpr std::uint8_t kOrder[19] = {
            16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        const unsigned hlit = bits(5) + 257, hdist = bits(5) + 1, hclen = bits(4) + 4;
        if (error_) return false;
        if (hlit > 286 || hdist > 30) { fail("bad dynamic block header"); return false; }
        std::uint8_t cl[19] = {};
        for (unsigned i = 0; i < hclen; ++i) cl[kOrder[i]] = std::uint8_t(bits(3));
        Huffman clh;
        if (error_ || !build(clh, cl, 19)) return false;
        std::uint8_t lengths[286 + 30] = {};
        unsigned n = 0;
        while (n < hlit + hdist) {
            const int sym = decode(clh);
            if (sym < 0) return false;
            unsigned repeat = 0;
            std::uint8_t value = 0;
            if (sym < 16) { lengths[n++] = std::uint8_t(sym); continue; }
            if (sym == 16) {
                if (n == 0) { fail("repeat with no previous length"); return false; }
                value = lengths[n - 1];
                repeat = 3 + bits(2);
            } else if (sym == 17) {
                repeat = 3 + bits(3);
            } else {
                repeat = 11 + bits(7);
            }
            if (error_) return false;
            if (n + repeat > hlit + hdist) { fail("code lengths overflow"); return false; }
            while (repeat--) lengths[n++] = value;
        }
        if (lengths[256] == 0) { fail("no end-of-block code"); return false; }
        Huffman lit, dist;
        return build(lit, lengths, hlit) && build(dist, lengths + hlit, hdist) &&
               codes(lit, dist);
    }

    const std::uint8_t* in_;
    std::size_t n_;
    std::size_t pos_ = 0;
    unsigned bitBuf_ = 0;
    int bitCount_ = 0;
    std::vector<std::uint8_t>& out_;
    std::size_t max_;
    bool error_ = false;
    const char* why_ = "";
};

} // namespace

bool inflate(const std::uint8_t* in, std::size_t n, std::vector<std::uint8_t>& out,
             std::size_t maxBytes, std::string& err) {
    out.clear();
    Inflater inf(in, n, out, maxBytes);
    return inf.run(err);
}

bool looksLikeZip(const std::vector<std::uint8_t>& bytes) {
    std::size_t eocd = 0;
    return findEnd(bytes, eocd);
}

bool listEntries(const std::vector<std::uint8_t>& z, std::vector<Entry>& entries,
                 std::string& err) {
    entries.clear();
    std::size_t eocd = 0;
    if (!findEnd(z, eocd)) {
        err = "not a zip archive (no end-of-central-directory record)";
        return false;
    }
    const std::uint32_t count = le16(z, eocd + 10);
    const std::uint32_t cdSize = le32(z, eocd + 12);
    std::size_t cd = le32(z, eocd + 16);
    if (count == 0xFFFFu || cdSize == 0xFFFFFFFFu || cd == 0xFFFFFFFFu) {
        err = "zip64 archives are not supported";
        return false;
    }
    if (cd > eocd || eocd - cd < cdSize) {
        err = "zip central directory lies outside the archive";
        return false;
    }
    entries.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        if (cd > z.size() || z.size() - cd < 46 || le32(z, cd) != 0x02014B50u) {
            err = "malformed zip central directory";
            return false;
        }
        Entry e;
        e.flags = std::uint16_t(le16(z, cd + 8));
        e.method = std::uint16_t(le16(z, cd + 10));
        e.crc32 = le32(z, cd + 16);
        e.compressedSize = le32(z, cd + 20);
        e.size = le32(z, cd + 24);
        const std::size_t nameLen = le16(z, cd + 28);
        const std::size_t extraLen = le16(z, cd + 30);
        const std::size_t commentLen = le16(z, cd + 32);
        e.localHeader = le32(z, cd + 42);
        if (e.compressedSize == 0xFFFFFFFFu || e.size == 0xFFFFFFFFu ||
            e.localHeader == 0xFFFFFFFFu) {
            err = "zip64 members are not supported";
            return false;
        }
        const std::size_t tail = nameLen + extraLen + commentLen;
        if (z.size() - cd - 46 < tail) {
            err = "malformed zip central directory";
            return false;
        }
        e.name.assign(reinterpret_cast<const char*>(z.data() + cd + 46), nameLen);
        cd += 46 + tail;
        entries.push_back(std::move(e));
    }
    return true;
}

std::uint64_t totalUncompressed(const std::vector<Entry>& entries) {
    std::uint64_t total = 0;
    for (const Entry& e : entries) total += e.size;
    return total;
}

bool readEntry(const std::vector<std::uint8_t>& z, const Entry& e,
               std::vector<std::uint8_t>& out, std::string& err,
               std::size_t maxBytes) {
    out.clear();
    if (e.encrypted()) { err = e.name + ": encrypted zip member"; return false; }
    if (e.method != 0 && e.method != 8) {
        err = e.name + ": unsupported compression method " + std::to_string(e.method) +
              " (stored and deflate only)";
        return false;
    }
    if (e.size > maxBytes) {
        err = e.name + ": declares " + std::to_string(e.size) +
              " bytes, more than the " + std::to_string(maxBytes) + "-byte cap";
        return false;
    }
    const std::size_t lh = e.localHeader;
    if (lh > z.size() || z.size() - lh < 30 || le32(z, lh) != 0x04034B50u) {
        err = e.name + ": bad local header";
        return false;
    }
    // The local header's name/extra lengths may differ from the central
    // directory's (§ 4.4.11): only the local ones locate the data.
    const std::size_t skip = 30 + std::size_t(le16(z, lh + 26)) + le16(z, lh + 28);
    if (z.size() - lh < skip || z.size() - lh - skip < e.compressedSize) {
        err = e.name + ": truncated member";
        return false;
    }
    const std::uint8_t* data = z.data() + lh + skip;
    const std::size_t clen = std::size_t(e.compressedSize);
    if (e.method == 0) {
        if (clen != e.size) { err = e.name + ": stored sizes disagree"; return false; }
        out.assign(data, data + clen);
    } else {
        std::string why;
        out.reserve(std::size_t(e.size));
        if (!inflate(data, clen, out, std::size_t(e.size), why)) {
            err = e.name + ": " + why;
            out.clear();
            return false;
        }
    }
    if (out.size() != e.size) {
        err = e.name + ": inflated to " + std::to_string(out.size()) +
              " bytes, directory says " + std::to_string(e.size);
        out.clear();
        return false;
    }
    if (crc32(out) != e.crc32) {
        err = e.name + ": CRC-32 mismatch";
        out.clear();
        return false;
    }
    return true;
}

bool readMember(const std::vector<std::uint8_t>& zip, const std::string& name,
                std::vector<std::uint8_t>& out, std::string& err,
                std::size_t maxBytes) {
    std::vector<Entry> entries;
    if (!listEntries(zip, entries, err)) return false;
    for (const Entry& e : entries)
        if (e.name == name) return readEntry(zip, e, out, err, maxBytes);
    err = name + ": not in the archive";
    return false;
}

} // namespace pom68k::zip
