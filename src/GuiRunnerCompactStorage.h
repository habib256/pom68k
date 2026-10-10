// POM68K — compact 68000 disk-bay bindings
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#pragma once

#include "GuiFloppyBays.h"
#include "GuiShellCommon.h"

namespace pom68k::gui {

template <class MachineT, class Ctx>
pom68k::DiskBaysHost compactDiskBaysHost(Ctx& ctx) {
    pom68k::DiskBaysHost host =
        diskBaysHostFor<MachineT>(ctx.machine, &ctx.spec.extraDisks);
    // The compacts relaunch with the FLOPPY path where the others carry the
    // boot disk: on a 128K/512K there may be no SCSI at all.
    host.relaunch = [&ctx](const std::string& boot,
                           const std::vector<std::string>& extras) {
        std::vector<std::string> scsiMedia;
        scsiMedia.reserve(extras.size() + 1);
        scsiMedia.push_back(boot);
        scsiMedia.insert(scsiMedia.end(), extras.begin(), extras.end());
        ctx.services.requestRelaunch(
            ctx.window, ctx.spec.romName, ctx.spec.floppyPath, scsiMedia);
    };
    // The second internal mechanism is fitted at boot (the ROM's
    // DiskSelect probes PA4 once): staged, then the same relaunch line.
    if (host.secondFloppyFittable)
        host.stageSecondFloppy = [&ctx](bool fitted) {
            ctx.services.relaunchWithSecondFloppy(fitted);
        };
    return host;
}

} // namespace pom68k::gui
