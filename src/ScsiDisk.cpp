// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "ScsiDisk.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace {
constexpr int kBlockSize = 512;
// SCSI status bytes
constexpr uint8_t kGood = 0x00, kCheck = 0x02;
// Sense keys
constexpr uint8_t kNoSense = 0x00, kIllegalRequest = 0x05;

// wrap_hfs.py layout: 96-block head (DDM + map + Apple_Driver43) then HFS.
constexpr uint32_t kFacadePrefixBlocks = 96;
constexpr uint32_t kFacadePrefixBytes = kFacadePrefixBlocks * kBlockSize;

static void be32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16);
    p[2] = uint8_t(v >> 8);  p[3] = uint8_t(v);
}
static void be16(uint8_t* p, uint16_t v) {
    p[0] = uint8_t(v >> 8); p[1] = uint8_t(v);
}

static bool loadTemplateHead(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.resize(kFacadePrefixBytes);
    in.read(reinterpret_cast<char*>(out.data()), std::streamsize(out.size()));
    if (in.gcount() != std::streamsize(out.size())) return false;
    return out.size() >= 2 && out[0] == 'E' && out[1] == 'R';
}

// Same candidate order as tools/wrap_hfs.py (+ env + beside the .dsk).
static bool findDdmTemplate(const std::string& imagePath, std::vector<uint8_t>& head) {
    std::vector<std::string> cands;
    if (const char* env = std::getenv("POM68K_SCSI_DDM_TEMPLATE"))
        if (env[0]) cands.emplace_back(env);

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path img = fs::absolute(imagePath, ec);
    if (!ec && img.has_parent_path()) {
        fs::path dir = img.parent_path();
        cands.push_back((dir / "HD20SC.vhd").string());
        cands.push_back((dir / "boot.vhd").string());
        cands.push_back((dir / "scsi_ddm_template.vhd").string());
    }
    for (const char* rel : {
             "hdv/HD20SC.vhd", "../hdv/HD20SC.vhd",
             "hdv/boot.vhd", "../hdv/boot.vhd",
             "hdv/scsi_ddm_template.vhd", "../hdv/scsi_ddm_template.vhd" })
        cands.emplace_back(rel);

    for (const auto& p : cands) {
        if (loadTemplateHead(p, head)) {
            std::fprintf(stderr, "SCSI: flat HFS façade — DDM template from %s\n",
                         p.c_str());
            return true;
        }
    }
    return false;
}
} // namespace

// Bare-HFS detection: bootable volumes carry 'LK' boot blocks at 0; a
// data-only volume (tools/dir2hfs.py bake) has ZERO boot blocks and is
// recognized by the MDB signature 'BD' at $400. Data-only volumes must NOT
// be given fake 'LK' blocks: StartBoot scans SCSI 6→0, sees the higher ID
// first, believes the LK, jumps into zeroed boot blocks and lands in the
// ROM serial-debugger stub ($408BA0EA on FF7439EE) before video is up.
static bool looksBareHfs(const std::vector<uint8_t>& img) {
    if (img.size() >= 2 && img[0] == 'L' && img[1] == 'K') return true;
    return img.size() >= 0x402 && img[0x400] == 'B' && img[0x401] == 'D'
        && !(img[0] == 'E' && img[1] == 'R');       // already partitioned
}

bool ScsiDisk::applyFlatHfsFacade(const std::string& imagePath) {
    if (!looksBareHfs(image_)) return false;
    if (image_.size() % kBlockSize) {
        std::fprintf(stderr, "SCSI: %s: bare HFS not a multiple of 512 — "
                     "no façade\n", imagePath.c_str());
        return false;
    }

    std::vector<uint8_t> head;
    if (!findDdmTemplate(imagePath, head)) {
        std::fprintf(stderr, "SCSI: %s looks like flat HFS ('LK') but no DDM "
                     "template found (set POM68K_SCSI_DDM_TEMPLATE or place "
                     "HD20SC.vhd / boot.vhd in hdv/) — leaving raw\n",
                     imagePath.c_str());
        return false;
    }

    const uint32_t hfsBlocks = uint32_t(image_.size() / kBlockSize);
    const uint32_t total = kFacadePrefixBlocks + hfsBlocks;

    // DDM sbBlkCount
    be32(head.data() + 4, total);

    // Ensure a ddType $6A driver entry (LC II StartBoot @ $A07264) alongside
    // the template's $0001 entry (Plus / Mac II wantType).
    int count = (head[0x10] << 8) | head[0x11];
    bool has6A = false;
    for (int i = 0; i < count && 0x12 + i * 8 + 8 <= kBlockSize; i++) {
        int e = 0x12 + i * 8;
        if (((head[e + 6] << 8) | head[e + 7]) == 0x6A) has6A = true;
    }
    if (!has6A && count >= 1 && 0x12 + count * 8 + 8 <= kBlockSize) {
        int src = 0x12, dst = 0x12 + count * 8;
        std::memcpy(head.data() + dst, head.data() + src, 8);
        head[dst + 6] = 0x00;
        head[dst + 7] = 0x6A;
        be16(head.data() + 0x10, uint16_t(count + 1));
    }

    // Patch Apple_HFS partition to cover the appended volume at LBA 96.
    for (int i = 1; i < 64; i++) {
        size_t b = size_t(i) * kBlockSize;
        if (b + 88 > head.size() || head[b] != 'P' || head[b + 1] != 'M') break;
        char typ[33] = {};
        std::memcpy(typ, head.data() + b + 48, 32);
        if (std::strcmp(typ, "Apple_HFS") == 0) {
            be32(head.data() + b + 8, kFacadePrefixBlocks);  // pmPyPartStart
            be32(head.data() + b + 12, hfsBlocks);           // pmPartBlkCnt
            be32(head.data() + b + 84, hfsBlocks);           // pmDataCnt
        }
    }

    std::vector<uint8_t> wrapped;
    wrapped.reserve(head.size() + image_.size());
    wrapped.insert(wrapped.end(), head.begin(), head.end());
    wrapped.insert(wrapped.end(), image_.begin(), image_.end());
    image_.swap(wrapped);
    blocks_ = total;
    hfsPrefixBlocks_ = kFacadePrefixBlocks;
    std::fprintf(stderr, "SCSI: flat HFS → façade (%u + %u = %u blocks)\n",
                 kFacadePrefixBlocks, hfsBlocks, total);
    return true;
}

bool ScsiDisk::open(const std::string& path, bool writeBack) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    image_.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    hfsPrefixBlocks_ = 0;
    blocks_ = uint32_t(image_.size() / kBlockSize);
    if (file_.is_open()) file_.close();
    writeBack_ = false;

    if (blocks_ && looksBareHfs(image_))
        applyFlatHfsFacade(path);

    if (blocks_ && writeBack) {
        file_.open(path, std::ios::in | std::ios::out | std::ios::binary);
        writeBack_ = file_.is_open();
        if (!writeBack_)
            std::fprintf(stderr, "SCSI: %s not writable — session writes "
                         "will be lost on exit\n", path.c_str());
        else if (hfsPrefixBlocks_)
            std::fprintf(stderr, "SCSI: write-back maps LBA≥%u onto flat HFS file\n",
                         hfsPrefixBlocks_);
    }
    return blocks_ > 0;
}

void ScsiDisk::read(uint32_t lba, uint32_t count, std::vector<uint8_t>& out) {
    out.clear();
    out.resize(size_t(count) * kBlockSize, 0);
    uint64_t off = uint64_t(lba) * kBlockSize;
    uint64_t n = uint64_t(count) * kBlockSize;
    if (off < image_.size()) {
        uint64_t avail = image_.size() - off;
        std::memcpy(out.data(), image_.data() + off, size_t(n < avail ? n : avail));
    }
}

// Writes land in the in-memory image; with write-back each one is also
// written through to the backing file immediately, so nothing is lost
// even if the process dies (no exit-time flush to miss). Flat-HFS façade:
// only LBAs past the synthetic prefix hit the original file.
void ScsiDisk::write(uint32_t lba, uint32_t count, const std::vector<uint8_t>& in) {
    uint64_t off = uint64_t(lba) * kBlockSize;
    uint64_t n = uint64_t(count) * kBlockSize;
    if (off >= image_.size()) return;
    uint64_t avail = image_.size() - off;
    uint64_t w = n < avail ? n : avail;
    if (w > in.size()) w = in.size();
    std::memcpy(image_.data() + off, in.data(), size_t(w));
    if (!writeBack_ || !w) return;

    // Façade prefix is synthetic — never write it into the flat .dsk.
    if (hfsPrefixBlocks_ && lba < hfsPrefixBlocks_) {
        uint32_t skip = hfsPrefixBlocks_ - lba;
        if (skip >= count) return;
        uint64_t skipBytes = uint64_t(skip) * kBlockSize;
        if (skipBytes >= w) return;
        const char* src = reinterpret_cast<const char*>(in.data()) + skipBytes;
        uint64_t fileOff = 0;
        uint64_t fileW = w - skipBytes;
        file_.seekp(std::streamoff(fileOff));
        file_.write(src, std::streamoff(fileW));
    } else {
        uint64_t fileOff = hfsPrefixBlocks_
            ? uint64_t(lba - hfsPrefixBlocks_) * kBlockSize
            : off;
        file_.seekp(std::streamoff(fileOff));
        file_.write(reinterpret_cast<const char*>(in.data()), std::streamoff(w));
    }
    file_.flush();
    if (!file_) {
        std::fprintf(stderr, "SCSI: write-back failed at block %u — "
                     "disabling (session writes stay in memory)\n", lba);
        writeBack_ = false;
    }
}

void ScsiDisk::setSense(uint8_t key, uint8_t asc) { senseKey_ = key; senseAsc_ = asc; }

uint8_t ScsiDisk::command(const uint8_t* cdb, int /*cdbLen*/,
                          std::vector<uint8_t>& dataOut,
                          const std::vector<uint8_t>& dataIn) {
    dataOut.clear();
    switch (cdb[0]) {
        case 0x00:                                   // TEST UNIT READY
            return kGood;

        case 0x03: {                                 // REQUEST SENSE
            uint8_t alloc = cdb[4] ? cdb[4] : 4;
            dataOut.assign(alloc, 0);
            dataOut[0] = 0x70;                       // current error, fixed format
            if (dataOut.size() > 2) dataOut[2] = senseKey_ & 0x0F;
            if (dataOut.size() > 7) {                // additional length =
                size_t addl = dataOut.size() - 8;    // min(10, alloc-8) per SCSI-1
                dataOut[7] = uint8_t(addl < 10 ? addl : 10);
            }
            if (dataOut.size() > 12) dataOut[12] = senseAsc_;
            setSense(kNoSense, 0);
            return kGood;
        }

        case 0x12: {                                 // INQUIRY
            uint8_t alloc = cdb[4] ? cdb[4] : 36;
            dataOut.assign(alloc, 0);
            if (dataOut.size() > 0) dataOut[0] = 0x00;   // direct-access device
            if (dataOut.size() > 1) dataOut[1] = 0x00;   // not removable
            if (dataOut.size() > 2) dataOut[2] = 0x01;   // SCSI-1 (ANSI)
            if (dataOut.size() > 4) dataOut[4] = 31;     // additional length
            static const char vendor[] = "POM68K   POM68K HD DISK   1.0 ";
            for (size_t i = 8; i < dataOut.size() && i - 8 < sizeof(vendor) - 1; i++)
                dataOut[i] = uint8_t(vendor[i - 8]);
            return kGood;
        }

        case 0x25: {                                 // READ CAPACITY (10)
            uint32_t last = blocks_ ? blocks_ - 1 : 0;
            dataOut = { uint8_t(last >> 24), uint8_t(last >> 16), uint8_t(last >> 8),
                        uint8_t(last), 0, 0, uint8_t(kBlockSize >> 8), uint8_t(kBlockSize) };
            return kGood;
        }

        case 0x08: {                                 // READ(6)
            uint32_t lba = (uint32_t(cdb[1] & 0x1F) << 16) | (uint32_t(cdb[2]) << 8) | cdb[3];
            uint32_t cnt = cdb[4] ? cdb[4] : 256;
            read(lba, cnt, dataOut);
            return kGood;
        }

        case 0x28: {                                 // READ(10)
            uint32_t lba = (uint32_t(cdb[2]) << 24) | (uint32_t(cdb[3]) << 16)
                         | (uint32_t(cdb[4]) << 8) | cdb[5];
            uint32_t cnt = (uint32_t(cdb[7]) << 8) | cdb[8];
            read(lba, cnt, dataOut);
            return kGood;
        }

        case 0x1A: {                                 // MODE SENSE(6)
            uint8_t alloc = cdb[4] ? cdb[4] : 4;
            dataOut.assign(alloc, 0);
            if (dataOut.size() > 0)                   // mode data length = n-1
                dataOut[0] = uint8_t(dataOut.size() - 1);
            if (dataOut.size() > 3) dataOut[3] = 8;  // block descriptor length
            if (dataOut.size() > 11) {               // descriptor block length = 512
                dataOut[9]  = uint8_t(kBlockSize >> 16);
                dataOut[10] = uint8_t(kBlockSize >> 8);
                dataOut[11] = uint8_t(kBlockSize);
            }
            return kGood;
        }

        case 0x0A: {                                 // WRITE(6)
            uint32_t lba = (uint32_t(cdb[1] & 0x1F) << 16) | (uint32_t(cdb[2]) << 8) | cdb[3];
            uint32_t cnt = cdb[4] ? cdb[4] : 256;
            write(lba, cnt, dataIn);
            return kGood;
        }
        case 0x2A: {                                 // WRITE(10)
            uint32_t lba = (uint32_t(cdb[2]) << 24) | (uint32_t(cdb[3]) << 16)
                         | (uint32_t(cdb[4]) << 8) | cdb[5];
            uint32_t cnt = (uint32_t(cdb[7]) << 8) | cdb[8];
            write(lba, cnt, dataIn);
            return kGood;
        }

        default:
            setSense(kIllegalRequest, 0x20);         // invalid command
            return kCheck;
    }
}
