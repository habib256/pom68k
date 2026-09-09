// POM68K — compact 68000 disk-bay bindings
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#pragma once

#include "GuiShellCommon.h"

namespace pom68k::gui {

template <class MachineT, class Ctx>
pom68k::DiskBaysHost compactDiskBaysHost(Ctx& ctx) {
    pom68k::DiskBaysHost host;
    host.extras = &ctx.spec.extraDisks;
    host.hardReset = [&ctx] {
        ctx.machine.push({MachineT::Cmd::HardReset});
    };
    host.relaunch = [&ctx](const std::string& boot,
                           const std::vector<std::string>& extras) {
        std::vector<std::string> scsiMedia;
        scsiMedia.reserve(extras.size() + 1);
        scsiMedia.push_back(boot);
        scsiMedia.insert(scsiMedia.end(), extras.begin(), extras.end());
        ctx.services.requestRelaunch(
            ctx.window, ctx.spec.romName, ctx.spec.floppyPath, scsiMedia);
    };
    host.bayIsCd = [&ctx](int id) {
        return ctx.machine.bayIsCdrom(id);
    };
    host.insertBay = [&ctx](int id, const std::string& disk) {
        if (!ctx.machine.bayIsCdrom(id)) return false;
        ctx.machine.requestInsertBay(id, disk);
        return true;
    };
    host.ejectBay = [&ctx](int id) { ctx.machine.requestEjectBay(id); };
    host.hasFloppyDrive = true;
    host.floppyInserted = [&ctx] { return ctx.machine.floppyInserted(); };
    host.insertFloppy = [&ctx](const std::string& disk) {
        ctx.machine.requestInsertFloppy(disk);
        ctx.spec.floppyPath = disk;
    };
    host.ejectFloppy = [&ctx] {
        ctx.machine.requestEjectFloppy();
        ctx.spec.floppyPath.clear();
    };
    return host;
}

} // namespace pom68k::gui
