// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── SCSI direct-access target (hard disk) ──
// A minimal SCSI-1 disk: the command subset the Mac Plus ROM needs to read
// the Driver Descriptor Map, partition map, disk driver and HFS boot
// blocks — TEST UNIT READY, REQUEST SENSE, INQUIRY, READ CAPACITY, READ(6),
// READ(10), MODE SENSE. Backing store is a raw 512-byte-block image
// (.vhd/.hda/.img), memory-mapped read-only for now.
// Source of truth: MAME nscsi_hd.cpp; SCSI-1 (SASI) spec; DEV.md § SCSI.
// Gate: tests/scsi_boot_etalon.cpp.

#pragma once
#include <cstdint>
#include <string>
#include <vector>

class ScsiDisk {
public:
    bool open(const std::string& path);
    bool present() const { return blocks_ > 0; }
    uint32_t blocks() const { return blocks_; }

    // Execute a CDB. Fills `dataOut` with the bytes to return to the
    // initiator (DATA IN phase) and returns the SCSI status byte (0 = GOOD,
    // 2 = CHECK CONDITION). `dataIn` carries WRITE payload (unused for now).
    uint8_t command(const uint8_t* cdb, int cdbLen,
                    std::vector<uint8_t>& dataOut,
                    const std::vector<uint8_t>& dataIn);

private:
    void read(uint32_t lba, uint32_t count, std::vector<uint8_t>& out);
    void setSense(uint8_t key, uint8_t asc);

    std::vector<uint8_t> image_;     // raw sectors
    uint32_t blocks_ = 0;
    uint8_t senseKey_ = 0, senseAsc_ = 0;
};
