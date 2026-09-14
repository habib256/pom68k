// POM68K — the guest SCSI bus view, on a synthetic low-memory image
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// src/GuestScsiView.h reads the System's drive queue (DrvQHdr $308) and
// VCB queue (VCBQHdr $356). This gate lays both out by hand — the offsets
// of Inside Macintosh: Files — and checks the walk: a SCSI Manager driver
// reference number −(33 + ID) ties a drive to its bay, a VCB on that drive
// names the mounted volume, a floppy (refNum −5) is ignored, and a queue
// the walker cannot trust (odd pointer, wrong signature) yields
// `valid = false` rather than a wrong answer. No ROM, no image.

#include "GuestScsiView.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s   %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

struct Image {
    std::vector<uint8_t> bytes = std::vector<uint8_t>(0x4000, 0);
    void w16(uint32_t a, uint16_t v) { bytes[a] = uint8_t(v >> 8); bytes[a + 1] = uint8_t(v); }
    void w32(uint32_t a, uint32_t v) { w16(a, uint16_t(v >> 16)); w16(a + 2, uint16_t(v)); }
    void drvQEl(uint32_t at, uint32_t next, int drive, int refNum) {
        w32(at, next); w16(at + 4, 0); w16(at + 6, uint16_t(drive));
        w16(at + 8, uint16_t(int16_t(refNum)));
    }
    void vcb(uint32_t at, uint32_t next, const char* name, int drive,
             uint16_t sig = 0x4244) {
        w32(at, next); w16(at + 8, sig);
        const size_t n = std::strlen(name);
        bytes[at + 44] = uint8_t(n);
        std::memcpy(&bytes[at + 45], name, n);
        w16(at + 72, uint16_t(drive));
    }
    pom68k::GuestScsiView read() const {
        return pom68k::readGuestScsiView([&](uint32_t a, uint8_t& out) {
            if (a >= bytes.size()) return false;
            out = bytes[a];
            return true;
        });
    }
};

} // namespace

int main() {
    // ── Empty queues: a System that has built nothing yet is a valid,
    //    empty answer.
    {
        Image img;
        const pom68k::GuestScsiView v = img.read();
        check(v.valid, "empty queues read as valid");
        bool any = false;
        for (const auto& b : v.bays) any = any || b.driver || b.mounted;
        check(!any, "empty queues name no bay");
    }

    // ── Boot volume on SCSI 0, a driver-only disk on SCSI 2, a floppy.
    {
        Image img;
        img.w32(0x308 + 2, 0x1000);
        img.drvQEl(0x1000, 0x1010, 3, -33);     // SCSI 0, drive 3
        img.drvQEl(0x1010, 0x1020, 1, -5);      // .Sony floppy, drive 1
        img.drvQEl(0x1020, 0,      5, -35);     // SCSI 2, drive 5
        img.w32(0x356 + 2, 0x2000);
        img.vcb(0x2000, 0x2100, "Macintosh HD", 3);
        img.vcb(0x2100, 0,      "Disquette", 1);
        const pom68k::GuestScsiView v = img.read();
        check(v.valid, "populated queues read as valid");
        check(v.bays[0].driver && v.bays[0].mounted &&
                  v.bays[0].volume == "Macintosh HD" && v.bays[0].driveNum == 3,
              "SCSI 0: refNum -33 → drive 3 → mounted « Macintosh HD »");
        check(v.bays[2].driver && !v.bays[2].mounted && v.bays[2].driveNum == 5,
              "SCSI 2: refNum -35 → driver installed, no volume");
        check(!v.bays[1].driver && !v.bays[1].mounted,
              "SCSI 1: nothing in the queues, nothing reported");
        bool floppyLeak = false;
        for (const auto& b : v.bays) floppyLeak = floppyLeak || b.volume == "Disquette";
        check(!floppyLeak, "the floppy's VCB is not attributed to a SCSI bay");
    }

    // ── An agent-served drive: refNum −49 says nothing, the hint does.
    {
        Image img;
        img.w32(0x308 + 2, 0x1000);
        img.drvQEl(0x1000, 0x1010, 3, -33);
        img.drvQEl(0x1010, 0,      9, -49);     // the agent's driver, unit 48
        img.w32(0x356 + 2, 0x2000);
        img.vcb(0x2000, 0, "Branche", 9);
        pom68k::GuestScsiDriveHints hints{};
        hints[2] = 9;
        const pom68k::GuestScsiView v = pom68k::readGuestScsiView(
            [&](uint32_t a, uint8_t& out) {
                if (a >= img.bytes.size()) return false;
                out = img.bytes[a];
                return true;
            }, &hints);
        check(v.valid && v.bays[2].driver && v.bays[2].mounted && v.bays[2].volume == "Branche",
              "a drive the agent reported for SCSI 2 is placed by its number");
        check(!img.read().bays[2].driver, "without the hint the same queue names no SCSI 2");
    }

    // ── Bounds: an odd head pointer, a VCB without a file-system
    //    signature, and a self-linked element all stop the walk safely.
    {
        Image img;
        img.w32(0x308 + 2, 0x1001);
        check(!img.read().valid, "odd drive-queue head → not valid");
    }
    {
        Image img;
        img.w32(0x308 + 2, 0x1000);
        img.drvQEl(0x1000, 0, 3, -33);
        img.w32(0x356 + 2, 0x2000);
        img.vcb(0x2000, 0, "Garbage", 3, 0x1234);
        check(!img.read().valid, "VCB without HFS/MFS signature → not valid");
    }
    {
        Image img;
        img.w32(0x308 + 2, 0x1000);
        img.drvQEl(0x1000, 0x1000, 3, -33);     // loops on itself
        const pom68k::GuestScsiView v = img.read();
        check(v.valid && v.bays[0].driver, "self-linked element terminates the walk");
    }
    {
        Image img;
        img.w32(0x308 + 2, 0x3FFC);             // element runs off the image
        check(!img.read().valid, "element past the readable range → not valid");
    }

    std::printf(failures ? "FAILED\n" : "PASS\n");
    return failures ? 1 : 0;
}
