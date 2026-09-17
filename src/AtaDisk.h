// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── AtaDisk: the IDE drive a Quadra 630 has instead of a SCSI one ────────
// The F108 machines (Quadra 630, LC/Performa 580) are the only 68k Macs
// whose internal disk is ATA, and their ROM carries the driver for it: the
// strings `ATA_MGR`, `ATABusReset`, `ATARegAccess`, `ATATaskFile`, `ATALOAD`
// and the partition type `APPLE_DRIVER_ATA` are all in the Q630 dump and
// none of them is in the LC II's. Until now POM68K's port read back zero,
// which is BSY=0 / DRDY=0 — "no device" — and the ROM fell back to SCSI.
// That was behaviourally right and completely useless.
//
// What the guest actually does, captured from `q630_boot_etalon` on
// 2026-09-17: a software-reset pulse on Device Control (`$0E` then `$0A`),
// then 3 994 reads of Status waiting for the drive to come ready, then a
// read of registers 1-6 for the post-reset signature. So the minimum a
// useful drive owes is: BSY clearing after SRST, the ATA signature
// (`01 01 00 00 00`), DRDY in Status, IDENTIFY DEVICE, and READ/WRITE
// SECTORS. That is what this is — PIO only, no DMA, no ATAPI.
//
// Deliberately NOT here: the bus wiring. Which byte of a sector lands on
// D15-D8 is the board's business (`Q630Memory`), not the drive's; this
// class serves ATA's own little-endian word order and lets the machine map
// it. Nor is the ATA Manager emulated: the ROM's own driver runs.

#pragma once
#include "SaveState.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

class AtaDisk {
public:
    // ── Task-file registers, by their ATA index ─────────────────────────
    // The board decides where these live; on the F108 port they are four
    // bytes apart from `+$1A000`, with the control block's Device Control /
    // Alternate Status at index 14.
    enum Reg : uint8_t {
        kData = 0, kError = 1, kFeatures = 1, kSectorCount = 2,
        kLbaLow = 3, kLbaMid = 4, kLbaHigh = 5, kDevice = 6,
        kStatus = 7, kCommand = 7, kAltStatus = 14, kDeviceControl = 14
    };
    // Status bits (ATA-4 § 7.15).
    enum Status : uint8_t {
        kBsy = 0x80, kDrdy = 0x40, kDf = 0x20, kDsc = 0x10,
        kDrq = 0x08, kErr = 0x01
    };
    // Error bits (ATA-4 § 7.6).
    enum Error : uint8_t { kAbrt = 0x04, kIdnf = 0x10, kUnc = 0x40 };

    // A raw image of 512-byte sectors, exactly like the SCSI side: the
    // partition map and the driver descriptor are the IMAGE's business.
    bool open(const std::string& path, bool writeBack = false) {
        close();
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        in.seekg(0, std::ios::end);
        const std::streamoff n = in.tellg();
        if (n <= 0 || n % 512) return false;
        in.seekg(0, std::ios::beg);
        image_.resize(size_t(n));
        if (!in.read(reinterpret_cast<char*>(image_.data()), std::streamsize(n)))
            { image_.clear(); return false; }
        sectors_ = uint32_t(image_.size() / 512);
        path_ = path;
        writeBack_ = writeBack;
        // A geometry that covers the image; the ROM's driver uses LBA, and
        // CHS exists only so an IDENTIFY that reports none looks broken.
        heads_ = 16; sectorsPerTrack_ = 63;
        cylinders_ = sectors_ / (heads_ * sectorsPerTrack_);
        if (!cylinders_) { heads_ = 1; sectorsPerTrack_ = 1; cylinders_ = sectors_; }
        reset();
        return true;
    }
    void close() {
        image_.clear(); path_.clear(); sectors_ = 0; writeBack_ = false;
        reset();
    }
    bool present() const { return sectors_ > 0; }
    uint32_t sectorCount() const { return sectors_; }

    // ── The reset the guest performs before anything else ────────────────
    // SRST leaves the task file holding the device signature, which is how
    // a driver tells an ATA drive (01 01 00 00) from an ATAPI one
    // (01 01 14 EB) from an empty bus (all zeroes).
    void reset() {
        status_ = present() ? uint8_t(kDrdy | kDsc) : 0;
        error_ = present() ? 1 : 0;              // 1 = device passed
        sectorCountReg_ = present() ? 1 : 0;
        lbaLow_ = present() ? 1 : 0;
        lbaMid_ = lbaHigh_ = 0;
        device_ = 0;
        features_ = 0;
        buffer_.clear();
        bufferAt_ = 0;
        pending_ = 0;
        writing_ = false;
        irq_ = false;
    }

    bool irq() const { return irq_; }
    void clearIrq() { irq_ = false; }

    // ── Register access ─────────────────────────────────────────────────
    uint8_t readRegister(uint8_t reg) {
        switch (reg) {
            case kError:       return error_;
            case kSectorCount: return sectorCountReg_;
            case kLbaLow:      return lbaLow_;
            case kLbaMid:      return lbaMid_;
            case kLbaHigh:     return lbaHigh_;
            case kDevice:      return device_;
            case kStatus:      irq_ = false; return status_;   // reading clears INTRQ
            case kAltStatus:   return status_;                 // does NOT clear it
            default:           return 0;
        }
    }

    void writeRegister(uint8_t reg, uint8_t v) {
        switch (reg) {
            case kFeatures:    features_ = v; break;
            case kSectorCount: sectorCountReg_ = v; break;
            case kLbaLow:      lbaLow_ = v; break;
            case kLbaMid:      lbaMid_ = v; break;
            case kLbaHigh:     lbaHigh_ = v; break;
            case kDevice:      device_ = v; break;
            case kCommand:     command(v); break;
            case kDeviceControl:
                // Bit 2 is SRST: held then released is the reset pulse the
                // guest sends at every boot ($0E then $0A observed).
                if (v & 0x04) srstHeld_ = true;
                else if (srstHeld_) { srstHeld_ = false; reset(); }
                nIen_ = (v & 0x02) != 0;
                break;
            default: break;
        }
    }

    // ── The 16-bit data register ────────────────────────────────────────
    // ATA word order: the sector's first byte is the low half. The board
    // decides how that reaches the 68k bus.
    uint16_t readData() {
        if (bufferAt_ + 1 >= buffer_.size()) {
            if (buffer_.empty()) return 0;
        }
        dataWords++;
        uint16_t w = 0;
        if (bufferAt_ + 1 < buffer_.size())
            w = uint16_t(buffer_[bufferAt_] | (buffer_[bufferAt_ + 1] << 8));
        bufferAt_ += 2;
        if (bufferAt_ >= buffer_.size()) nextReadSector();
        return w;
    }

    void writeData(uint16_t w) {
        if (!writing_ || buffer_.empty()) return;
        if (bufferAt_ + 1 < buffer_.size()) {
            buffer_[bufferAt_] = uint8_t(w & 0xFF);
            buffer_[bufferAt_ + 1] = uint8_t(w >> 8);
        }
        bufferAt_ += 2;
        if (bufferAt_ >= buffer_.size()) commitWrittenSector();
    }

    // ── Save states ─────────────────────────────────────────────────────
    // The task file and the sector buffer are guest state; the image is
    // host-owned, exactly as on the SCSI side.
    template <class Ar> void visit(Ar& ar) {
        ar(status_, error_, features_, sectorCountReg_, lbaLow_, lbaMid_,
           lbaHigh_, device_, pending_, writing_, irq_, nIen_, srstHeld_,
           bufferAt_, currentLba_);
    }

    // Diagnostics (gate: tests/ata_disk_test.cpp). `setTrace` prints every
    // command the guest issues, which is how the F108 port's protocol was
    // captured in the first place.
    long commands = 0, sectorsRead = 0, sectorsWritten = 0, dataWords = 0;
    void setTrace(bool on) { trace_ = on; }

private:
    // Where the next transfer starts, from the task file. LBA mode when the
    // device register's bit 6 is set; CHS otherwise, which the ROM does not
    // use but a formatter might.
    uint32_t address() const {
        if (device_ & 0x40)
            return (uint32_t(device_ & 0x0F) << 24) | (uint32_t(lbaHigh_) << 16)
                 | (uint32_t(lbaMid_) << 8) | lbaLow_;
        const uint32_t c = (uint32_t(lbaHigh_) << 8) | lbaMid_;
        const uint32_t h = device_ & 0x0F;
        const uint32_t s = lbaLow_ ? lbaLow_ : 1;       // sectors count from 1
        return (c * heads_ + h) * sectorsPerTrack_ + (s - 1);
    }
    void setAddress(uint32_t lba) {
        if (device_ & 0x40) {
            lbaLow_ = uint8_t(lba);
            lbaMid_ = uint8_t(lba >> 8);
            lbaHigh_ = uint8_t(lba >> 16);
            device_ = uint8_t((device_ & 0xF0) | ((lba >> 24) & 0x0F));
        }
    }
    void raiseIrq() { if (!nIen_) irq_ = true; }
    void fail(uint8_t err) {
        status_ = uint8_t((status_ & ~(kDrq | kBsy)) | kErr | kDrdy);
        error_ = err;
        buffer_.clear(); bufferAt_ = 0; pending_ = 0; writing_ = false;
        raiseIrq();
    }

    void command(uint8_t op) {
        commands++;
        if (trace_)
            std::fprintf(stderr, "[ata] cmd $%02X dev $%02X cnt %u lba %u\n",
                         op, device_, unsigned(sectorCountReg_),
                         unsigned(address()));
        error_ = 0;
        status_ = uint8_t(status_ & ~kErr);
        if (!present()) { fail(kAbrt); return; }
        switch (op) {
            case 0xEC: identify(); break;
            case 0x20: case 0x21: startRead(); break;
            case 0x30: case 0x31: startWrite(); break;
            case 0x40: case 0x41:                    // READ VERIFY SECTORS
                status_ = uint8_t(kDrdy | kDsc);
                raiseIrq();
                break;
            case 0x91:                               // INIT DEVICE PARAMETERS
                heads_ = uint8_t((device_ & 0x0F) + 1);
                sectorsPerTrack_ = sectorCountReg_ ? sectorCountReg_ : 1;
                cylinders_ = sectors_ / (uint32_t(heads_) * sectorsPerTrack_);
                status_ = uint8_t(kDrdy | kDsc);
                raiseIrq();
                break;
            case 0xE4:                               // READ BUFFER
                // The drive's sector buffer, no media access. A driver uses
                // the pair below to prove the data path works before it
                // trusts the disk — the Quadra 630's ROM sends WRITE BUFFER
                // right after its first READ, and aborting it sent the whole
                // probe round again (observed 2026-09-17).
                bufferAt_ = 0;
                if (buffer_.size() != 512) buffer_.assign(512, 0);
                writing_ = false;
                pending_ = 0;
                status_ = uint8_t(kDrdy | kDsc | kDrq);
                raiseIrq();
                break;
            case 0xE8:                               // WRITE BUFFER
                buffer_.assign(512, 0);
                bufferAt_ = 0;
                writing_ = true;
                pending_ = 0;                        // buffer only: no media
                status_ = uint8_t(kDrdy | kDsc | kDrq);
                break;
            case 0x90:                               // EXECUTE DEVICE DIAGNOSTIC
                error_ = 1;                          // device 0 passed
                status_ = uint8_t(kDrdy | kDsc);
                raiseIrq();
                break;
            case 0xE7: case 0xEA:                    // FLUSH CACHE
            case 0xEF:                               // SET FEATURES
                status_ = uint8_t(kDrdy | kDsc);
                raiseIrq();
                break;
            default:
                // An unknown command is ABORTED, which is what lets a driver
                // discover what the drive does not do.
                fail(kAbrt);
                break;
        }
    }

    void identify() {
        buffer_.assign(512, 0);
        auto word = [&](int i, uint16_t v) {
            buffer_[size_t(i) * 2] = uint8_t(v & 0xFF);
            buffer_[size_t(i) * 2 + 1] = uint8_t(v >> 8);
        };
        // ATA strings are byte-swapped pairs (ATA-4 § 8.12.8).
        auto text = [&](int at, int words, const char* s) {
            const size_t n = std::strlen(s);
            for (int i = 0; i < words; i++) {
                const size_t j = size_t(i) * 2;
                const char a = j < n ? s[j] : ' ';         // space-padded to
                const char b = j + 1 < n ? s[j + 1] : ' '; // the fixed width
                word(at + i, uint16_t((uint8_t(a) << 8) | uint8_t(b)));
            }
        };
        word(0, 0x0040);                        // non-removable ATA device
        word(1, uint16_t(cylinders_ > 65535 ? 65535 : cylinders_));
        word(3, heads_);
        word(6, sectorsPerTrack_);
        text(10, 10, "POM68K-ATA-0001");        // serial number
        text(23, 4, "1.0");                     // firmware revision
        text(27, 20, "POM68K ATA Disk");        // model number
        word(47, 0x8001);                       // 1 sector per interrupt
        word(49, 0x0200);                       // LBA supported
        word(53, 0x0003);                       // words 54-58 and 64-70 valid
        word(54, uint16_t(cylinders_ > 65535 ? 65535 : cylinders_));
        word(55, heads_);
        word(56, sectorsPerTrack_);
        word(57, uint16_t(sectors_ & 0xFFFF));
        word(58, uint16_t(sectors_ >> 16));
        word(60, uint16_t(sectors_ & 0xFFFF));  // total addressable sectors
        word(61, uint16_t(sectors_ >> 16));
        bufferAt_ = 0;
        pending_ = 0;
        writing_ = false;
        status_ = uint8_t(kDrdy | kDsc | kDrq);
        raiseIrq();
    }

    void startRead() {
        currentLba_ = address();
        pending_ = sectorCountReg_ ? sectorCountReg_ : 256;
        if (uint64_t(currentLba_) + pending_ > sectors_) { fail(kIdnf); return; }
        writing_ = false;
        loadSector();
    }
    void loadSector() {
        buffer_.assign(image_.begin() + size_t(currentLba_) * 512,
                       image_.begin() + size_t(currentLba_) * 512 + 512);
        bufferAt_ = 0;
        sectorsRead++;
        status_ = uint8_t(kDrdy | kDsc | kDrq);
        raiseIrq();
    }
    void nextReadSector() {
        if (writing_) return;
        if (pending_) pending_--;
        if (!pending_) {
            buffer_.clear(); bufferAt_ = 0;
            status_ = uint8_t(kDrdy | kDsc);
            setAddress(currentLba_);
            return;
        }
        currentLba_++;
        setAddress(currentLba_);
        loadSector();
    }

    void startWrite() {
        currentLba_ = address();
        pending_ = sectorCountReg_ ? sectorCountReg_ : 256;
        if (uint64_t(currentLba_) + pending_ > sectors_) { fail(kIdnf); return; }
        writing_ = true;
        buffer_.assign(512, 0);
        bufferAt_ = 0;
        // A write's first DRQ comes with NO interrupt: the driver is told to
        // send data by the status bit alone (ATA-4 § 9.4).
        status_ = uint8_t(kDrdy | kDsc | kDrq);
    }
    void commitWrittenSector() {
        if (!pending_) {
            // WRITE BUFFER: the bytes stay in the drive's buffer, where
            // READ BUFFER can hand them back. Nothing reaches the medium.
            bufferAt_ = 0;
            writing_ = false;
            status_ = uint8_t(kDrdy | kDsc);
            raiseIrq();
            return;
        }
        std::memcpy(image_.data() + size_t(currentLba_) * 512,
                    buffer_.data(), 512);
        if (writeBack_ && !path_.empty()) {
            std::fstream f(path_, std::ios::binary | std::ios::in | std::ios::out);
            if (f) {
                f.seekp(std::streamoff(currentLba_) * 512);
                f.write(reinterpret_cast<const char*>(buffer_.data()), 512);
            }
        }
        sectorsWritten++;
        if (pending_) pending_--;
        if (!pending_) {
            buffer_.clear(); bufferAt_ = 0; writing_ = false;
            status_ = uint8_t(kDrdy | kDsc);
            setAddress(currentLba_);
            raiseIrq();
            return;
        }
        currentLba_++;
        setAddress(currentLba_);
        buffer_.assign(512, 0);
        bufferAt_ = 0;
        status_ = uint8_t(kDrdy | kDsc | kDrq);
        raiseIrq();
    }

    std::vector<uint8_t> image_;
    std::string path_;
    bool writeBack_ = false;
    uint32_t sectors_ = 0, cylinders_ = 0;
    uint8_t heads_ = 16, sectorsPerTrack_ = 63;

    uint8_t status_ = 0, error_ = 0, features_ = 0, sectorCountReg_ = 0;
    uint8_t lbaLow_ = 0, lbaMid_ = 0, lbaHigh_ = 0, device_ = 0;
    bool irq_ = false, nIen_ = false, srstHeld_ = false, writing_ = false;
    uint32_t pending_ = 0, currentLba_ = 0;
    std::vector<uint8_t> buffer_;
    size_t bufferAt_ = 0;
    bool trace_ = false;
};
