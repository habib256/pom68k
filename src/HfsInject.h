// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Host-side insertion of a file into an EXISTING classic HFS volume — the
// piece that puts « POM68K Disques » into the boot volume's Startup Items
// so the guest agent runs without a gesture in the Mac
// (docs/SCSI_HOTPLUG.md § 8). HfsBlankVolume.h writes an empty volume from
// scratch; this is the other half: catalog B*-tree search and insertion
// (leaf split, index split, root split), allocation-bitmap first-fit,
// MDB bookkeeping, and the folder walk from the blessed System Folder.
//
// Layouts are Inside Macintosh: Files, chapter 2 ("Data Organization on
// Volumes"); the name collation table is the File Manager's, as `machfs`
// (the Python library the media tools use) carries it. Every change is
// staged as a set of 512-byte block writes and applied by commit(), so a
// refusal — a full catalog, no contiguous run of free blocks, a catalog
// extent beyond the MDB's three — leaves the volume untouched.
//
// What is NOT here: the extents overflow tree (a catalog whose fourth
// extent would be needed is refused), file threads (the Finder makes none
// for ordinary files), and the Desktop database (the Finder adds a new
// file to it when it first sees the folder).

#pragma once

#include "ScsiDisk.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace hfsinject {

// 512-byte block access to the disk holding the volume — a ScsiDisk in the
// emulator, a byte vector in the gates. `lba` is absolute to the disk.
struct BlockIo {
    virtual ~BlockIo() = default;
    virtual bool read(uint32_t lba, uint8_t* out) = 0;
    virtual bool write(uint32_t lba, const uint8_t* in) = 0;
    virtual uint32_t blocks() const = 0;
};

// The emulator's disk: reads come from the in-memory image, writes go
// through ScsiDisk::hostWrite (write log + write-back, no guest counters).
struct ScsiDiskIo final : BlockIo {
    explicit ScsiDiskIo(ScsiDisk& d) : disk(d) {}
    ScsiDisk& disk;
    bool read(uint32_t lba, uint8_t* out) override;
    bool write(uint32_t lba, const uint8_t* in) override;
    uint32_t blocks() const override { return disk.blocks(); }
};

// A byte vector, for the gates.
struct MemoryIo final : BlockIo {
    explicit MemoryIo(std::vector<uint8_t>& img) : image(img) {}
    std::vector<uint8_t>& image;
    bool read(uint32_t lba, uint8_t* out) override;
    bool write(uint32_t lba, const uint8_t* in) override;
    uint32_t blocks() const override { return uint32_t(image.size() / 512); }
};

// A MacBinary I/II file (what Retro68 emits beside the .dsk): name, Finder
// info and both forks. `name`, `type`, `creator` are MacRoman bytes.
struct MacBinary {
    std::string name, type, creator;
    uint16_t finderFlags = 0;
    int16_t x = 0, y = 0;
    uint32_t crDate = 0, mdDate = 0;
    std::vector<uint8_t> data, rsrc;
};
bool decodeMacBinary(const std::vector<uint8_t>& raw, MacBinary& out, std::string& err);

// The HFS volume on a disk: at the Apple_HFS partition of a partitioned
// image (DDM 'ER' + 'PM' entries — a .vhd, or ScsiDisk's façade), or at
// block 0 of a bare volume ('BD' at block 2).
bool findHfsVolume(BlockIo& io, uint32_t& startLba, uint32_t& lengthBlocks, std::string& err);

// The volume with a staged set of changes. Nothing reaches `io` before
// commit(); a failed call leaves the stage consistent for the next one
// (callers that want all-or-nothing simply do not commit).
class Volume {
public:
    Volume(BlockIo& io, uint32_t startLba, uint32_t lengthBlocks);
    bool open(std::string& err);

    std::string name() const;
    uint32_t blessedFolder() const;          // MDB drFndrInfo[0], 0 = none
    uint32_t nextCnid() const;
    uint32_t freeBlocks() const;

    // Catalog lookups by parent CNID and MacRoman name (the File Manager's
    // case-insensitive collation). 0 / false when absent.
    uint32_t findFolder(uint32_t parent, const std::string& name);
    bool fileExists(uint32_t parent, const std::string& name);
    // Both forks of an existing file, read back through its extents.
    bool readFile(uint32_t parent, const std::string& name, MacBinary& out, std::string& err);

    bool createFolder(uint32_t parent, const std::string& name, uint32_t macNow,
                      uint32_t& cnid, std::string& err);
    bool addFile(uint32_t parent, const MacBinary& file, uint32_t macNow,
                 uint32_t& cnid, std::string& err);
    void bless(uint32_t folder);

    // Consistency of the catalog tree as staged: every leaf key strictly
    // increasing along the leaf chain, record counts matching the header.
    // The gates call it; the installer does not need to.
    bool checkCatalog(std::string& err);

    bool commit(std::string& err);
    size_t stagedBlocks() const { return pending_.size(); }

private:
    struct Node;                       // a parsed 512-byte B*-tree node
    struct Path;                       // the descent that found a leaf

    bool readBlock(uint32_t lba, uint8_t* out);
    void stageBlock(uint32_t lba, const uint8_t* in);
    uint32_t allocLba(uint32_t allocBlock) const;
    bool catalogNodeLba(uint32_t node, uint32_t& lba, std::string& err);
    bool readNode(uint32_t node, Node& out, std::string& err);
    bool writeNode(uint32_t node, const Node& n, std::string& err);
    bool readHeader(std::string& err);
    bool writeHeader(std::string& err);
    bool allocNode(uint32_t& node, std::string& err);
    bool search(const std::vector<uint8_t>& key, Path& path, bool& exact, std::string& err);
    bool insert(Path& path, std::vector<uint8_t> record, std::string& err);
    bool insertInto(uint32_t nodeNum, int level, Path& path, std::vector<uint8_t> record,
                    std::string& err);
    bool allocBlocks(uint32_t count, uint32_t& first, std::string& err);
    bool writeFork(const std::vector<uint8_t>& bytes, uint32_t& startBlock, uint32_t& physLen,
                   std::string& err);
    bool readFork(const uint8_t* extents, uint32_t logicalLen, std::vector<uint8_t>& out,
                  std::string& err);
    bool bumpFolder(uint32_t folder, uint32_t macNow, int delta, std::string& err);
    bool leafRecord(const std::vector<uint8_t>& key, std::vector<uint8_t>& record,
                    uint32_t& node, int& index, std::string& err);

    BlockIo& io_;
    uint32_t start_, length_;
    std::map<uint32_t, std::vector<uint8_t>> pending_;
    uint8_t mdb_[512] = {};
    // MDB fields the code consults, decoded once and kept in sync.
    uint32_t alBlkSiz_ = 0, alBlSt_ = 0, nmAlBlks_ = 0, vbmSt_ = 0;
    uint16_t ctExt_[6] = {};
    // Catalog header record.
    uint16_t bthDepth_ = 0;
    uint32_t bthRoot_ = 0, bthNRecs_ = 0, bthFNode_ = 0, bthLNode_ = 0, bthNNodes_ = 0, bthFree_ = 0;
    std::vector<uint8_t> bitmap_;      // the volume bitmap, whole sectors
    bool bitmapDirty_ = false;
};

// Put `app` into <blessed System Folder>:Startup Items — or « Ouverture
// au démarrage » on a French System — of the volume on `io`, unless a file
// of that name is already there. Never creates the folder: a System
// without one (System 6) has no Startup Items concept, and a System in a
// third language names it otherwise; the outcome says so.
struct Outcome {
    enum Kind { Installed, AlreadyPresent, NoVolume, NoFolder, Refused } kind = Refused;
    std::string message;               // one line for the console / window
};
Outcome installStartupItem(BlockIo& io, const MacBinary& app, uint32_t macNow);

// Names the installer looks for under the blessed folder, in order.
const std::vector<std::string>& startupItemsNames();

// MacRoman case-insensitive comparison the File Manager applies to
// catalog names (<0, 0, >0).
int compareNames(const uint8_t* a, size_t alen, const uint8_t* b, size_t blen);

} // namespace hfsinject
