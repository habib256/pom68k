// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "ScsiDisk.h"
#include <cstdio>
#include <cstring>

namespace {
constexpr int kBlockSize = 512;
// SCSI status bytes
constexpr uint8_t kGood = 0x00, kCheck = 0x02;
// Sense keys
constexpr uint8_t kNoSense = 0x00, kIllegalRequest = 0x05;
}

bool ScsiDisk::open(const std::string& path, bool writeBack) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    image_.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    blocks_ = uint32_t(image_.size() / kBlockSize);
    if (file_.is_open()) file_.close();
    writeBack_ = false;
    if (blocks_ && writeBack) {
        file_.open(path, std::ios::in | std::ios::out | std::ios::binary);
        writeBack_ = file_.is_open();
        if (!writeBack_)
            std::fprintf(stderr, "SCSI: %s not writable — session writes "
                         "will be lost on exit\n", path.c_str());
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
// even if the process dies (no exit-time flush to miss).
void ScsiDisk::write(uint32_t lba, uint32_t count, const std::vector<uint8_t>& in) {
    uint64_t off = uint64_t(lba) * kBlockSize;
    uint64_t n = uint64_t(count) * kBlockSize;
    if (off >= image_.size()) return;
    uint64_t avail = image_.size() - off;
    uint64_t w = n < avail ? n : avail;
    if (w > in.size()) w = in.size();
    std::memcpy(image_.data() + off, in.data(), size_t(w));
    if (writeBack_ && w) {
        file_.seekp(std::streamoff(off));
        file_.write(reinterpret_cast<const char*>(in.data()), std::streamoff(w));
        file_.flush();
        if (!file_) {                        // disk full / I/O error: warn once
            std::fprintf(stderr, "SCSI: write-back failed at block %u — "
                         "disabling (session writes stay in memory)\n", lba);
            writeBack_ = false;
        }
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
            if (dataOut.size() > 7) dataOut[7] = 10; // additional length
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
            if (dataOut.size() > 3) dataOut[3] = 8;  // block descriptor length
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
