// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// MfsVolume -- a host-side reader for the Macintosh File System, the flat
// file system of the 400 K System floppies the Macintosh 128K and 512K boot
// (System 1.x to 2.x; HFS replaced it with System 3.0 / the Plus).
//
// Layout, from Inside Macintosh II "The File Manager: Data Organization on
// Volumes" (II-119..II-123) and cross-checked on the two reference floppies
// of assets.lock (tools/mfs_ls.py, 2026-09-16):
//
//   sector 0-1      boot blocks
//   sector 2        volume information, 64 bytes:
//                     drSigWord $D2D7, drCrDate, drLsBkUp, drAtrb, drNmFls,
//                     drDirSt, drBlLen, drNmAlBlks, drAlBlkSiz, drClpSiz,
//                     drAlBlSt, drNxtFNum, drFreeBks, drVN (Str27)
//                   then the block map: one 12-bit entry per allocation
//                   block, numbered from 2; 0 = free, 1 = last block of a
//                   file, else the next block of the chain
//   drDirSt         the file directory, drBlLen sectors of packed entries
//                   that never straddle a sector; an entry whose flFlags
//                   bit 7 is clear ends the entries of that sector
//   drAlBlSt        the first sector of allocation block 2
//
// A file directory entry: flFlags (bit 7 used, bit 0 locked), flTyp,
// flUsrWds (16-byte Finder info: type, creator, flags, location, folder),
// flFlNum, flStBlk/flLgLen/flPyLen (data fork), flRStBlk/flRLgLen/flRPyLen
// (resource fork), flCrDat, flMdDat, flNam (Str255) — 51 bytes plus the
// name, padded to an even length.
//
// The reader never trusts a field it has not bounded: every chain step is
// checked against drNmAlBlks and a loop guard, every directory sector
// against the image, so a guest that corrupts its own floppy yields an
// `error`, not a crash. It exists for the gates: what the guest wrote to a
// 400 K floppy is verified here, on the in-memory image, the way HfsInject
// and folderprobe verify the HFS volumes.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace pom68k::mfs {

constexpr std::uint16_t kSignature = 0xD2D7;
constexpr std::size_t kSector = 512;

struct File {
    std::string name;            // MacRoman bytes as stored
    std::uint8_t flags = 0;      // flFlags: bit 7 used, bit 0 locked
    std::uint32_t type = 0;      // FInfo fdType
    std::uint32_t creator = 0;   // FInfo fdCreator
    std::uint16_t finderFlags = 0;
    std::uint32_t fileNumber = 0;
    std::uint16_t dataStart = 0; // first allocation block, 0 = empty fork
    std::uint32_t dataLength = 0;
    std::uint32_t dataPhysical = 0;
    std::uint16_t rsrcStart = 0;
    std::uint32_t rsrcLength = 0;
    std::uint32_t rsrcPhysical = 0;
    std::uint32_t created = 0;   // seconds since 1904-01-01
    std::uint32_t modified = 0;
    bool locked() const { return flags & 0x01; }
};

struct Volume {
    bool valid = false;
    std::string error;           // why `valid` is false, or a consistency note
    std::string name;
    std::uint32_t created = 0;
    std::uint32_t lastBackup = 0;
    std::uint16_t attributes = 0;
    std::uint16_t fileCount = 0;         // drNmFls, as the volume claims
    std::uint16_t directoryStart = 0;    // sector
    std::uint16_t directoryLength = 0;   // sectors
    std::uint16_t allocationBlocks = 0;  // drNmAlBlks
    std::uint32_t allocationBlockSize = 0;
    std::uint32_t clumpSize = 0;
    std::uint16_t allocationStart = 0;   // sector of block 2
    std::uint32_t nextFileNumber = 0;
    std::uint16_t freeBlocks = 0;
    std::vector<File> files;

    const File* find(std::string_view fileName) const {
        for (const File& f : files)
            if (f.name == fileName) return &f;
        return nullptr;
    }
};

namespace detail {
inline std::uint16_t be16(const std::uint8_t* p) {
    return std::uint16_t((p[0] << 8) | p[1]);
}
inline std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
           (std::uint32_t(p[2]) << 8) | p[3];
}
} // namespace detail

// The block map entry of allocation block `block` (2-based), or 0 when the
// block is outside the map. The map follows the 64-byte volume information
// in sector 2 and may run into the following sectors.
inline std::uint16_t mapEntry(const std::uint8_t* image, std::size_t size,
                              const Volume& v, std::uint32_t block) {
    if (block < 2 || block >= std::uint32_t(v.allocationBlocks) + 2) return 0;
    const std::size_t i = block - 2;
    const std::size_t base = 2 * kSector + 64 + (i / 2) * 3;
    if (base + 3 > size) return 0;
    const std::uint8_t* p = image + base;
    return (i % 2 == 0) ? std::uint16_t((p[0] << 4) | (p[1] >> 4))
                        : std::uint16_t(((p[1] & 0x0F) << 8) | p[2]);
}

inline Volume parse(const std::uint8_t* image, std::size_t size) {
    using detail::be16;
    using detail::be32;
    Volume v;
    if (!image || size < 3 * kSector) {
        v.error = "image shorter than three sectors";
        return v;
    }
    const std::uint8_t* vib = image + 2 * kSector;
    if (be16(vib) != kSignature) {
        v.error = "no MFS signature ($D2D7) in sector 2";
        return v;
    }
    v.created = be32(vib + 2);
    v.lastBackup = be32(vib + 6);
    v.attributes = be16(vib + 10);
    v.fileCount = be16(vib + 12);
    v.directoryStart = be16(vib + 14);
    v.directoryLength = be16(vib + 16);
    v.allocationBlocks = be16(vib + 18);
    v.allocationBlockSize = be32(vib + 20);
    v.clumpSize = be32(vib + 24);
    v.allocationStart = be16(vib + 28);
    v.nextFileNumber = be32(vib + 30);
    v.freeBlocks = be16(vib + 34);
    const std::uint8_t nameLen = vib[36] > 27 ? 27 : vib[36];
    v.name.assign(reinterpret_cast<const char*>(vib + 37), nameLen);

    if (v.allocationBlockSize == 0 || v.allocationBlockSize % kSector != 0) {
        v.error = "allocation block size is not a multiple of 512";
        return v;
    }
    const std::size_t dirEnd =
        (std::size_t(v.directoryStart) + v.directoryLength) * kSector;
    if (v.directoryLength == 0 || dirEnd > size) {
        v.error = "file directory outside the image";
        return v;
    }
    const std::size_t allocEnd =
        std::size_t(v.allocationStart) * kSector +
        std::size_t(v.allocationBlocks) * v.allocationBlockSize;
    if (allocEnd > size) {
        v.error = "allocation blocks outside the image";
        return v;
    }

    for (std::uint32_t s = v.directoryStart; s < v.directoryStart + v.directoryLength; s++) {
        std::size_t off = std::size_t(s) * kSector;
        const std::size_t end = off + kSector;
        while (off + 51 <= end) {
            const std::uint8_t* e = image + off;
            if (!(e[0] & 0x80)) break;           // no more entries this sector
            const std::uint8_t nameLen = e[50];
            const std::size_t entry = 51 + nameLen;
            if (off + entry > end) {
                v.error = "directory entry straddles a sector";
                return v;
            }
            File f;
            f.flags = e[0];
            f.type = be32(e + 2);
            f.creator = be32(e + 6);
            f.finderFlags = be16(e + 10);
            f.fileNumber = be32(e + 18);
            f.dataStart = be16(e + 22);
            f.dataLength = be32(e + 24);
            f.dataPhysical = be32(e + 28);
            f.rsrcStart = be16(e + 32);
            f.rsrcLength = be32(e + 34);
            f.rsrcPhysical = be32(e + 38);
            f.created = be32(e + 42);
            f.modified = be32(e + 46);
            f.name.assign(reinterpret_cast<const char*>(e + 51), nameLen);
            v.files.push_back(std::move(f));
            off += entry + (entry & 1);
        }
    }
    v.valid = true;
    if (v.files.size() != v.fileCount)
        v.error = "drNmFls disagrees with the directory";
    return v;
}

inline Volume parse(const std::vector<std::uint8_t>& image) {
    return parse(image.data(), image.size());
}

// A fork's bytes, following the block map from `start` for `length` bytes.
// Empty on any inconsistency (a chain that leaves the volume, loops, or ends
// before `length`), so a caller comparing forks never mistakes a broken
// chain for an empty file: check `fork.size() == length`.
inline std::vector<std::uint8_t> readFork(const std::uint8_t* image, std::size_t size,
                                          const Volume& v, std::uint16_t start,
                                          std::uint32_t length) {
    std::vector<std::uint8_t> out;
    if (!v.valid) return out;
    if (length == 0) return out;
    std::uint32_t block = start;
    std::uint32_t steps = 0;
    while (block > 1 && out.size() < length) {
        if (block >= std::uint32_t(v.allocationBlocks) + 2) return {};
        if (++steps > v.allocationBlocks) return {};      // a loop
        const std::size_t off = std::size_t(v.allocationStart) * kSector +
                                std::size_t(block - 2) * v.allocationBlockSize;
        if (off + v.allocationBlockSize > size) return {};
        out.insert(out.end(), image + off, image + off + v.allocationBlockSize);
        block = mapEntry(image, size, v, block);
    }
    if (out.size() < length) return {};
    out.resize(length);
    return out;
}

inline std::vector<std::uint8_t> dataFork(const std::vector<std::uint8_t>& image,
                                          const Volume& v, const File& f) {
    return readFork(image.data(), image.size(), v, f.dataStart, f.dataLength);
}

inline std::vector<std::uint8_t> resourceFork(const std::vector<std::uint8_t>& image,
                                              const Volume& v, const File& f) {
    return readFork(image.data(), image.size(), v, f.rsrcStart, f.rsrcLength);
}

} // namespace pom68k::mfs
