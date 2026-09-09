// POM68K — compact 68000 startup-media composition
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#pragma once

#include "DiskBays.h"
#include "MacMemory.h"

#include <cstdio>
#include <string>
#include <vector>

namespace pom68k::gui {

struct CompactMountedMedia {
    std::string floppyPath;
    std::string hddPath;
    std::vector<std::string> extraDisks;
    bool floppyOk = false;
    bool hddOk = false;
};

template <class Services>
CompactMountedMedia mountCompactMedia(
    MacMemory& mem, const std::vector<std::string>& media,
    Services& services, bool demoMode) {
    CompactMountedMedia mounted;
    mounted.floppyPath = !media.empty()
        ? media[0] : services.locate("disks35/Disk605.dsk");
    mounted.floppyOk = !mounted.floppyPath.empty() &&
                        mem.insertDisk(mounted.floppyPath);
    if (mounted.floppyOk)
        std::printf("Floppy: %s\n", mounted.floppyPath.c_str());

    // A valid floppy reserves slot 0. A failed one becomes typed SCSI media;
    // an empty slot is the explicit placeholder used by GUI relaunches.
    const std::size_t scsiBegin =
        (!media.empty() && (mounted.floppyOk || media[0].empty())) ? 1 : 0;
    std::size_t bootArg = media.size();
    for (std::size_t i = scsiBegin; i < media.size(); ++i) {
        if (media[i] != pom68k::kCdBayToken &&
            !pom68k::diskBaysPathIsCd(media[i])) {
            bootArg = i;
            break;
        }
    }
    mounted.hddPath = bootArg < media.size()
        ? media[bootArg] : services.locate("hdv/HD20SC.vhd");
    mounted.hddOk = !mounted.hddPath.empty() &&
                    mem.attachScsi(mounted.hddPath, true);
    if (mounted.hddOk)
        std::printf("SCSI HD: %s (%u blocks, write-back)\n",
                    mounted.hddPath.c_str(), mem.scsiDisk().blocks());
    else
        mounted.hddPath.clear();

    for (std::size_t i = scsiBegin;
         i < media.size() && mounted.extraDisks.size() < 6; ++i) {
        const std::string& argument = media[i];
        if (i == bootArg) continue;
        const int id = int(mounted.extraDisks.size()) + 1;
        if (argument == pom68k::kCdBayToken) {
            if (mem.attachCdromEmpty(id)) mounted.extraDisks.push_back(argument);
        } else if (pom68k::diskBaysPathIsCd(argument)) {
            if (mem.attachCdrom(argument, id)) {
                mounted.extraDisks.push_back(argument);
                std::printf("SCSI CD %d: %s\n", id, argument.c_str());
            } else {
                std::fprintf(stderr, "SCSI CD %d: %s FAILED\n", id,
                             argument.c_str());
            }
        } else if (mem.attachScsi(argument, true, id)) {
            mounted.extraDisks.push_back(argument);
            std::printf("SCSI HD %d: %s (write-back)\n", id, argument.c_str());
        }
    }
    pom68k::ensureCdDrive(
        mem, mounted.extraDisks, services.config().core().storage.cdBay);
    if (!mounted.floppyOk && !mounted.hddOk && !demoMode)
        std::fprintf(stderr, "No boot media — drop a .dsk in disks35/ or a "
                     ".vhd in hdv/ (looked relative to CWD and the executable).\n");
    return mounted;
}

} // namespace pom68k::gui
