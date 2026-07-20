// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── SCSI direct-access target (hard disk) ──
// A minimal SCSI-1 disk: the command subset classic Mac OS needs — TEST
// UNIT READY, REQUEST SENSE, INQUIRY, READ CAPACITY, READ(6)/(10),
// WRITE(6)/(10), MODE SENSE. Backing store is a raw 512-byte-block image
// (.vhd/.hda/.img/.dsk) loaded whole into memory; with `writeBack` each WRITE
// is also written through to the backing file immediately (crash-safe,
// no exit-time flush). Tests attach without it so reference images are
// never modified.
//
// Bare HFS volumes (boot blocks 'LK' at LBA 0 — Infinite Mac / Basilisk
// flat `.dsk`) are auto-wrapped in memory: a DDM ('ER') + partition map +
// Apple_Driver43 template is prepended so ROM StartBoot sees a real SCSI
// disk. The HFS payload stays at LBA 96; write-back maps those LBAs back
// onto the original file. Template search: $POM68K_SCSI_DDM_TEMPLATE, then
// HD20SC.vhd / boot.vhd beside the image or under hdv/. Offline alternative:
// tools/wrap_hfs.py.
// Source of truth: MAME nscsi_hd.cpp; SCSI-1 (SASI) spec; DEV.md § SCSI.
// Gate: tests/scsi_boot_etalon.cpp, tests/scsi_pdma_test.cpp,
//       tests/scsi_hfs_facade_test.cpp.

#pragma once
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

class ScsiDisk {
public:
    bool open(const std::string& path, bool writeBack = false);
    bool present() const { return blocks_ > 0; }
    uint32_t blocks() const { return blocks_; }

    // True when open() applied the in-memory HFS-flat → SCSI façade.
    bool flatHfsFacade() const { return hfsPrefixBlocks_ != 0; }
    uint32_t hfsPrefixBlocks() const { return hfsPrefixBlocks_; }

    // In-memory image access — direct pokes bypass the write-back stream
    // (never reach the backing file). Used by tests to inject a $6A DDM
    // driver entry so an otherwise-bootable disk passes the LC II ROM's
    // boot scan ($A07264).
    std::vector<uint8_t>& image() { return image_; }

    // Execute a CDB. Fills `dataOut` with the bytes to return to the
    // initiator (DATA IN phase) and returns the SCSI status byte (0 = GOOD,
    // 2 = CHECK CONDITION). `dataIn` carries WRITE payload (unused for now).
    uint8_t command(const uint8_t* cdb, int cdbLen,
                    std::vector<uint8_t>& dataOut,
                    const std::vector<uint8_t>& dataIn);

private:
    void read(uint32_t lba, uint32_t count, std::vector<uint8_t>& out);
    void write(uint32_t lba, uint32_t count, const std::vector<uint8_t>& in);
    void setSense(uint8_t key, uint8_t asc);
    bool applyFlatHfsFacade(const std::string& imagePath);

    std::vector<uint8_t> image_;     // raw sectors (possibly façade-prefixed)
    std::fstream file_;              // write-back stream (open iff writeBack_)
    bool writeBack_ = false;
    uint32_t blocks_ = 0;
    // Non-zero when image_ has a synthetic DDM/PM/driver prefix; HFS file
    // bytes begin at this LBA and write-back subtracts it from the LBA.
    uint32_t hfsPrefixBlocks_ = 0;
    uint8_t senseKey_ = 0, senseAsc_ = 0;
};
