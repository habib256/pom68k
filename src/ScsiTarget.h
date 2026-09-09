// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// ── ScsiTarget: what a SCSI controller is allowed to know about a device ──
// Both controllers (`Ncr5380`, `Ncr53c96`) used to hold `ScsiDisk*` directly,
// which quietly meant "the only thing that can live on a Mac's SCSI bus is a
// disk". It is not: the era's most useful non-disk target is the DaynaPort
// SCSI/Link, an Ethernet card that answers SCSI commands (`DaynaPort`), and
// scanners and tape drives sat on the same bus.
//
// The interface is exactly what the phase engines need and nothing more:
//   present()         — does this ID answer selection at all
//   command()         — run a CDB, fill DATA IN, return the status byte
//   writeByteCount()  — how many DATA OUT bytes to gather FIRST (the count
//                       lives in the CDB, differently for every command, and
//                       getting it wrong hangs the bus rather than corrupting
//                       anything)
//   extendDataOut()   — for the commands that carry their real length inside
//                       the first bytes rather than in the CDB
//   selectLun()       — which logical unit this connection addresses
//
// Deliberately absent: block size, media, geometry. A controller that needs
// to know those is a controller that has grown a device model, which is how
// `writeByteCount` ended up duplicated and half-wrong in both engines before
// this header existed.

#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

class ScsiTarget {
public:
    virtual ~ScsiTarget() = default;

    // Does this target answer selection? A CD-ROM with no disc still does
    // (it reports NOT READY); an unopened disk image does not exist at all.
    virtual bool present() const = 0;

    // ── The logical unit this connection addresses ──────────────────────
    // SCSI-2 § 5.6.7: the IDENTIFY message an initiator sends right after
    // selection establishes the I_T_L nexus, and its LUN governs every
    // command of that connection; the CDB's byte-1 LUN field is the SCSI-1
    // form, consulted only when no IDENTIFY was sent. MAME encodes exactly
    // that ordering in `nscsi_full_device::get_lun(default)` (bus/nscsi/
    // nscsi.cpp), which returns the identified LUN when one arrived and the
    // caller's CDB-derived default otherwise.
    //
    // Controllers call this once per selection: with the IDENTIFY's LUN when
    // the initiator sent one, with `kNoIdentify` when it did not (the
    // Ncr5380 path — the Plus/SE ROM never leaves SCSI-1). Which LUNs exist
    // is the TARGET's answer, not the controller's; a controller that
    // decided it would be a controller with a device model in it.
    static constexpr std::uint8_t kNoIdentify = 0xFF;
    virtual void selectLun(std::uint8_t /*lun*/) {}

    // Execute a CDB. `dataOut` receives the DATA IN payload, `dataIn` carries
    // the DATA OUT payload the controller already gathered. Returns the SCSI
    // status byte (0 = GOOD, 2 = CHECK CONDITION).
    virtual std::uint8_t command(const std::uint8_t* cdb, int cdbLen,
                                 std::vector<std::uint8_t>& dataOut,
                                 const std::vector<std::uint8_t>& dataIn) = 0;

    // DATA OUT bytes this CDB owes the target before command() can run.
    virtual int writeByteCount(const std::uint8_t* cdb, int cdbLen) const = 0;

    // Called each time the gather reaches the current expectation; returns
    // the new total. Default: the CDB said everything there was to say.
    virtual std::size_t extendDataOut(const std::uint8_t* /*cdb*/, int /*cdbLen*/,
                                      const std::vector<std::uint8_t>& /*sofar*/,
                                      std::size_t expected) const {
        return expected;
    }
};
