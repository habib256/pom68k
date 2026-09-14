// POM68K — the SCSI bus as the GUEST sees it, read from its own tables
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The Disques window used to print what the HOST attached and a guess about
// what the Finder would do with it ("n'apparaît qu'au démarrage…"). Classic
// Mac OS keeps the truth in two queues in low memory, and the host can read
// them without running a byte of guest code (docs/SCSI_HOTPLUG.md § 3,
// step 1):
//
//   DrvQHdr  ($308)  the drive queue — one DrvQEl per drive the System
//                    knows: dQDrive (drive number), dQRefNum (driver).
//                    A SCSI Manager driver's reference number is
//                    −(33 + SCSI ID) for ID 0–6 (Inside Macintosh:
//                    Devices, "SCSI Manager"), which is how a drive is
//                    tied back to a bay.
//   VCBQHdr  ($356)  the volume control blocks — one VCB per MOUNTED
//                    volume: vcbVN (name), vcbDrvNum (the drive it sits
//                    on). Inside Macintosh: Files, "Volume Control Blocks".
//
// Offsets (Inside Macintosh: Files, C summary):
//   QHdr    qFlags 0 (2)  qHead 2 (4)  qTail 6 (4)
//   DrvQEl  qLink 0 (4)  qType 4 (2)  dQDrive 6 (2)  dQRefNum 8 (2)
//   VCB     qLink 0 (4)  … vcbSigWord 8 (2) … vcbVN 44 (Str27, 28 bytes)
//           vcbDrvNum 72 (2)  vcbDRefNum 74 (2)  vcbFSID 76 (2)
//
// The read is side-effect-free and bounded: at most 64 elements per queue,
// every pointer word-aligned and non-null, every VCB carrying an HFS ('BD')
// or MFS ($D2D7) signature. A queue that fails those checks — a machine
// still in the ROM's early boot, a System that zeroed nothing yet — yields
// `valid = false`, which the window shows as "inconnu", never as "absent".
//
// Addresses are LOGICAL. The caller's `peek8` must translate: on a machine
// whose System runs behind the PMMU (RBV, Duo, IIsi) physical low memory is
// the framebuffer — the trap Mmu030Peek.h exists for. MachineHost supplies
// that translation per CPU family; the tests hand a flat image.

#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace pom68k {

struct GuestScsiBay {
    bool driver = false;        // a drive-queue entry names this target
    bool mounted = false;       // a VCB sits on that drive
    int driveNum = 0;           // Mac OS drive number (0 = none)
    std::string volume;         // vcbVN when mounted
};

struct GuestScsiView {
    bool valid = false;         // both queues were readable
    std::array<GuestScsiBay, 7> bays{};
};

// Drive numbers the guest agent reported for bays it serves itself
// (ScsiAgentMailbox.h): its driver sits in a unit above 47, so the refNum
// rule cannot place those drives; the report's drive number does.
using GuestScsiDriveHints = std::array<int, 7>;

// `peek8(logical, out)` returns false when the byte cannot be read.
template <class Peek8>
GuestScsiView readGuestScsiView(Peek8&& peek8,
                                const GuestScsiDriveHints* hints = nullptr) {
    GuestScsiView view;
    auto peek16 = [&](uint32_t a, uint16_t& out) {
        uint8_t hi = 0, lo = 0;
        if (!peek8(a, hi) || !peek8(a + 1, lo)) return false;
        out = uint16_t(uint16_t(hi) << 8 | lo);
        return true;
    };
    auto peek32 = [&](uint32_t a, uint32_t& out) {
        uint16_t hi = 0, lo = 0;
        if (!peek16(a, hi) || !peek16(a + 2, lo)) return false;
        out = uint32_t(hi) << 16 | lo;
        return true;
    };
    auto sane = [](uint32_t p) { return p != 0 && (p & 1) == 0; };

    constexpr uint32_t kDrvQHdr = 0x308, kVcbQHdr = 0x356;
    constexpr int kMaxElements = 64;

    // Drive number → SCSI ID, from the drive queue.
    std::array<int, 7> driveOfBay{};
    driveOfBay.fill(0);
    uint32_t el = 0;
    if (!peek32(kDrvQHdr + 2, el)) return view;
    for (int n = 0; n < kMaxElements && sane(el); n++) {
        uint16_t drive = 0, refNum = 0;
        if (!peek16(el + 6, drive) || !peek16(el + 8, refNum)) return view;
        const int ref = int16_t(refNum);
        int id = -1;
        if (ref <= -33 && ref >= -39) id = -33 - ref;
        else if (hints)
            for (size_t b = 0; b < hints->size(); b++)
                if ((*hints)[b] && (*hints)[b] == int16_t(drive)) id = int(b);
        if (id >= 0) {
            view.bays[size_t(id)].driver = true;
            view.bays[size_t(id)].driveNum = int16_t(drive);
            driveOfBay[size_t(id)] = int16_t(drive);
        }
        if (!peek32(el, el)) return view;
    }
    if (el & 1) return view;

    // Mounted volumes, matched to a bay by drive number.
    uint32_t vcb = 0;
    if (!peek32(kVcbQHdr + 2, vcb)) return view;
    for (int n = 0; n < kMaxElements && sane(vcb); n++) {
        uint16_t sig = 0, drive = 0;
        if (!peek16(vcb + 8, sig) || !peek16(vcb + 72, drive)) return view;
        if (sig != 0x4244 && sig != 0xD2D7) return view;   // 'BD' HFS, MFS
        for (size_t id = 0; id < view.bays.size(); id++) {
            if (!view.bays[id].driver || driveOfBay[id] != int16_t(drive))
                continue;
            uint8_t len = 0;
            if (!peek8(vcb + 44, len)) return view;
            std::string name;
            for (uint8_t i = 0; i < len && i < 27; i++) {
                uint8_t c = 0;
                if (!peek8(vcb + 45 + i, c)) return view;
                name.push_back(char(c));
            }
            view.bays[id].mounted = true;
            view.bays[id].volume = name;
        }
        if (!peek32(vcb, vcb)) return view;
    }
    if (vcb & 1) return view;
    view.valid = true;
    return view;
}

} // namespace pom68k
