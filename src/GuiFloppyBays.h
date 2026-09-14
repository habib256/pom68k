// POM68K — shared two-drive GUI bindings
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#pragma once

#include "DiskBays.h"

namespace pom68k::gui {

template <class Machine>
void bindFloppyBays(DiskBaysHost& host, Machine& machine) {
    host.hasFloppyDrive = true;
    host.floppyInserted = [&machine] { return machine.floppyInserted(0); };
    host.insertFloppy = [&machine](const std::string& path) {
        machine.requestInsertFloppy(path, 0);
    };
    host.ejectFloppy = [&machine] { machine.requestEjectFloppy(0); };

    host.hasExternalFloppyDrive = true;
    host.externalFloppyInserted = [&machine] {
        return machine.floppyInserted(1);
    };
    host.insertExternalFloppy = [&machine](const std::string& path) {
        machine.requestInsertFloppy(path, 1);
    };
    host.ejectExternalFloppy = [&machine] {
        machine.requestEjectFloppy(1);
    };
}

// Live attach/detach and the guest's bus view (docs/SCSI_HOTPLUG.md § 3,
// § 7). Both are queued; `true` means requested, the outcome is
// bayMessage's.
template <class Machine>
void bindScsiBays(DiskBaysHost& host, Machine& machine) {
    host.attachBay = [&machine](int id, const std::string& path) {
        if (id < 1 || id > 6 || path.empty()) return false;
        machine.requestAttachDisk(id, path);
        return true;
    };
    host.detachBay = [&machine](int id) {
        if (id < 1 || id > 6) return false;
        machine.requestDetachDisk(id);
        return true;
    };
    host.guestView = [&machine] { return machine.guestScsiView(); };
    host.bayMessage = [&machine] { return machine.bayMessage(); };
    host.agentPresent = [&machine] { return machine.agent().present; };
    host.agentReport = [&machine] { return machine.agent().mailbox; };
    host.agentMount = [&machine](int id) { machine.requestAgentMount(id); };
    host.agentUnmount = [&machine](int id) { machine.requestAgentUnmount(id); };
}

template <class Machine>
void refreshFloppyBays(DiskBaysHost& host, const Machine& machine) {
    host.floppyPath = machine.floppyPath(0);
    host.externalFloppyPath = machine.floppyPath(1);
}

template <class Mem>
void configureFloppyWriteBack(Mem& mem, bool enabled) {
    mem.internalDrive().setWriteBack(enabled);
    mem.externalDrive().setWriteBack(enabled);
}

template <class Mem>
void flushFloppyDrives(Mem& mem) {
    mem.internalDrive().flushToFile();
    mem.externalDrive().flushToFile();
}

} // namespace pom68k::gui
