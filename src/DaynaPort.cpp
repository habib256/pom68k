// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// DaynaPort SCSI/Link command set. Wire reference: SLINKCMD.TXT as
// implemented by PiSCSI cpp/devices/scsi_daynaport.cpp (BSD-3-Clause) —
// see DaynaPort.h for the provenance note and the list of deliberate gaps.

#include "DaynaPort.h"
#include <cstring>

namespace {
constexpr std::uint8_t kGood = 0x00, kCheck = 0x02;
constexpr std::uint8_t kNoSense = 0x00, kIllegalRequest = 0x05;

// Control byte of READ(6)/WRITE(6) selects the DaynaPort meaning of the
// opcode. A CDB without one was not written by the SCSI/Link driver, and
// answering it as if it were would hand a disk driver a frame header.
constexpr std::uint8_t kReadCtlA = 0xC0, kReadCtlB = 0x80;
constexpr std::uint8_t kWriteFmtRaw = 0x00, kWriteFmtHdr = 0x80;
// Sub-function of SET INTERFACE MODE ($0C), in the control byte.
constexpr std::uint8_t kSetMode = 0x80, kSetMac = 0x40;

// READ(6) reply header: 2-byte big-endian length, 4-byte big-endian flags.
constexpr std::size_t kReadHeader = 6;
constexpr std::uint32_t kNoMoreData = 0x00000000;
constexpr std::uint32_t kMoreData   = 0x00000010;

// A real Ethernet MAC pads a short frame to 60 bytes and computes the FCS
// over the padded frame; the SCSI/Link hands both to the driver, and the
// length field counts the FCS. PiSCSI instead pads to 128 and lets the
// checksum go wrong (its comment says no known driver checks it) — padding
// to the real minimum keeps the FCS correct and costs nothing.
constexpr std::size_t kMinFrame = 60;

// Ethernet FCS: CRC-32, reflected, poly $EDB88320, init/final $FFFFFFFF,
// transmitted least-significant byte first.
std::uint32_t ethCrc32(const std::uint8_t* p, std::size_t n) {
    static std::uint32_t table[256];
    static bool built = false;
    if (!built) {
        for (std::uint32_t i = 0; i < 256; i++) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        built = true;
    }
    std::uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < n; i++)
        c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

std::uint16_t be16(const std::uint8_t* p) {
    return std::uint16_t((p[0] << 8) | p[1]);
}
} // namespace

void DaynaPort::setSense(std::uint8_t key, std::uint8_t asc) {
    senseKey_ = key; senseAsc_ = asc;
}

void DaynaPort::receiveFrame(const std::uint8_t* d, std::size_t n) {
    if (!attached_ || !enabled_ || !d || n < 14 || n > kMaxFrame) return;
    // Full ring: drop the ARRIVING frame, not the oldest. A guest that has
    // stopped polling is better served by the frames it already has (its TCP
    // will retransmit what it missed) than by a ring that keeps rewriting
    // itself under it.
    if (rxBytes_ + n > kRxRingBytes) { framesDropped++; return; }
    rx_.emplace_back(d, d + n);
    rxBytes_ += n;
}

// ── DATA OUT sizing ─────────────────────────────────────────────────────
int DaynaPort::writeByteCount(const std::uint8_t* cdb, int cdbLen) const {
    if (!cdb || cdbLen < 6) return 0;
    switch (cdb[0]) {
        case 0x0A:                                   // WRITE(6): send a frame
            // Format $00: the CDB length IS the frame length. Format $80:
            // the payload is a 4-byte header + frame + 4 trailing zeros, so
            // the CDB length is short by 8 (SLINKCMD, PiSCSI Write6).
            if (cdb[5] == kWriteFmtRaw) return be16(cdb + 3);
            if (cdb[5] == kWriteFmtHdr) return be16(cdb + 3) + 8;
            return 0;
        case 0x0C:                                   // SET MAC ADDRESS
            return cdb[5] == kSetMac ? 6 : 0;
        case 0x0D:                                   // SET MULTICAST ADDRESS
            return cdb[4];
        default:
            return 0;
    }
}

std::uint8_t DaynaPort::command(const std::uint8_t* cdb, int cdbLen,
                                std::vector<std::uint8_t>& dataOut,
                                const std::vector<std::uint8_t>& dataIn) {
    dataOut.clear();
    if (!cdb || cdbLen < 6) { setSense(kIllegalRequest, 0x20); return kCheck; }

    switch (cdb[0]) {
        case 0x00:                                   // TEST UNIT READY
            return kGood;

        case 0x03: {                                 // REQUEST SENSE
            std::uint8_t alloc = cdb[4] ? cdb[4] : 4;
            dataOut.assign(alloc, 0);
            dataOut[0] = 0x70;                       // current error, fixed
            if (dataOut.size() > 2) dataOut[2] = senseKey_ & 0x0F;
            if (dataOut.size() > 7) {
                std::size_t addl = dataOut.size() - 8;
                dataOut[7] = std::uint8_t(addl < 10 ? addl : 10);
            }
            if (dataOut.size() > 12) dataOut[12] = senseAsc_;
            setSense(kNoSense, 0);
            return kGood;
        }

        case 0x12: {                                 // INQUIRY
            // The Mac SCSI/Link driver wants 37 bytes, not the usual 36:
            // additional length $20 and one vendor-specific byte past the
            // revision (PiSCSI InquiryInternal — the driver reads the extra
            // byte and rejects a 36-byte reply).
            std::uint8_t alloc = cdb[4] ? cdb[4] : 37;
            dataOut.assign(alloc, 0);
            if (dataOut.size() > 0) dataOut[0] = 0x03;   // processor device
            if (dataOut.size() > 1) dataOut[1] = 0x00;   // not removable
            if (dataOut.size() > 2) dataOut[2] = 0x02;   // SCSI-2
            if (dataOut.size() > 3) dataOut[3] = 0x02;   // response format
            if (dataOut.size() > 4) dataOut[4] = 0x20;   // additional length
            static const char id[] = "Dayna   SCSI/Link       1.4a";
            for (std::size_t i = 8; i < dataOut.size() && i - 8 < 28; i++)
                dataOut[i] = std::uint8_t(id[i - 8]);
            return kGood;
        }

        case 0x08: {                                 // READ(6): get a frame
            // The control byte is the driver's signature. Without it this is
            // a disk driver probing the bus, and it must not be handed a
            // frame header that looks like a boot block.
            if (cdb[5] != kReadCtlA && cdb[5] != kReadCtlB) {
                setSense(kIllegalRequest, 0x24);     // INVALID FIELD IN CDB
                return kCheck;
            }
            // At startup the driver issues a one-block READ to see whether
            // something disk-shaped answers. Nothing comes back, on purpose.
            if (cdb[4] == 1) return kGood;

            dataOut.assign(kReadHeader, 0);
            if (rx_.empty()) {
                // Length 0 + "no more data" is how the SCSI/Link says the
                // ring is empty. The driver polls this constantly.
                return kGood;
            }
            std::vector<std::uint8_t> f = std::move(rx_.front());
            rx_.pop_front();
            rxBytes_ -= f.size();
            if (f.size() < kMinFrame) f.resize(kMinFrame, 0);   // MAC padding
            const std::uint32_t crc = ethCrc32(f.data(), f.size());
            f.push_back(std::uint8_t(crc));                     // FCS, LSB first
            f.push_back(std::uint8_t(crc >> 8));
            f.push_back(std::uint8_t(crc >> 16));
            f.push_back(std::uint8_t(crc >> 24));

            dataOut[0] = std::uint8_t(f.size() >> 8);           // length incl. FCS
            dataOut[1] = std::uint8_t(f.size());
            const std::uint32_t flags = rx_.empty() ? kNoMoreData : kMoreData;
            dataOut[2] = std::uint8_t(flags >> 24);
            dataOut[3] = std::uint8_t(flags >> 16);
            dataOut[4] = std::uint8_t(flags >> 8);
            dataOut[5] = std::uint8_t(flags);
            dataOut.insert(dataOut.end(), f.begin(), f.end());
            framesToGuest++;
            bytesToGuest += long(f.size());
            return kGood;
        }

        case 0x0A: {                                 // WRITE(6): send a frame
            const std::uint8_t* frame = nullptr;
            std::size_t len = 0;
            if (cdb[5] == kWriteFmtRaw) {
                len = be16(cdb + 3);
                frame = dataIn.data();
            } else if (cdb[5] == kWriteFmtHdr) {
                // PP PP 00 00 <frame> 00 00 00 00 — the real length is in
                // the first two bytes, not in the CDB.
                if (dataIn.size() < 4) { setSense(kIllegalRequest, 0x24); return kCheck; }
                len = be16(dataIn.data());
                frame = dataIn.data() + 4;
                if (len + 4 > dataIn.size()) len = dataIn.size() - 4;
            } else {
                setSense(kIllegalRequest, 0x24);
                return kCheck;
            }
            if (len > dataIn.size()) len = dataIn.size();
            // A driver that appends its own FCS hands us four bytes that are
            // not part of the datagram. Strip them: what is on the other end
            // of sendFrame is a software gateway, and a stray FCS shows up
            // there as four bytes of trailing garbage on every packet.
            if (len > kMinFrame + 4) {
                const std::uint32_t crc = ethCrc32(frame, len - 4);
                const std::uint8_t* t = frame + len - 4;
                if (t[0] == std::uint8_t(crc) && t[1] == std::uint8_t(crc >> 8) &&
                    t[2] == std::uint8_t(crc >> 16) && t[3] == std::uint8_t(crc >> 24))
                    len -= 4;
            }
            if (len >= 14 && len <= kMaxFrame && enabled_) {
                framesFromGuest++;
                bytesFromGuest += long(len);
                if (sendFrame) sendFrame(frame, len);
            }
            return kGood;
        }

        case 0x09: {                                 // RETRIEVE STATISTICS
            // 6-byte MAC then three 4-byte counters (frame alignment errors,
            // CRC errors, frames lost). Ours are honest zeros — nothing in
            // this path can produce an alignment or CRC error.
            std::vector<std::uint8_t> stats(18, 0);
            std::memcpy(stats.data(), mac_.data(), 6);
            std::size_t alloc = be16(cdb + 3);
            if (!alloc || alloc > stats.size()) alloc = stats.size();
            dataOut.assign(stats.begin(), stats.begin() + long(alloc));
            return kGood;
        }

        case 0x0C:                                   // SET INTERFACE MODE / MAC
            if (cdb[5] == kSetMode) return kGood;    // broadcast mode: no-op
            if (cdb[5] == kSetMac) {
                if (dataIn.size() >= 6) std::memcpy(mac_.data(), dataIn.data(), 6);
                return kGood;
            }
            setSense(kIllegalRequest, 0x20);
            return kCheck;

        case 0x0D:                                   // SET MULTICAST ADDRESS
            // The list is accepted and discarded — see the header note.
            if (!cdb[4]) { setSense(kIllegalRequest, 0x24); return kCheck; }
            return kGood;

        case 0x0E:                                   // ENABLE / DISABLE
            if (cdb[5] & 0x80) {
                enabled_ = true;
            } else {
                enabled_ = false;
                // Disabling restores the built-in address and empties the
                // ring: that is what the hardware does, and a driver that
                // re-enables expects a clean interface, not yesterday's
                // frames.
                mac_ = kBuiltinMac;
                rx_.clear();
                rxBytes_ = 0;
            }
            return kGood;

        default:
            setSense(kIllegalRequest, 0x20);         // invalid command
            return kCheck;
    }
}
