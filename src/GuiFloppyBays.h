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

// ── One binding for every runner's disk bays ────────────────────────────
// Six runners each built this by hand, four of them character for character
// the same and the other two differing only in which members the context
// happened to be called. That is five copies of a contract, and a contract
// no gate could reach: the bodies lived inside the runner functions, which
// cannot be instantiated without GL. Here it is one function, taking the
// machine directly, so `gui_disk_bindings_test` can hold it to account.
//
// `relaunch` stays the caller's: it is the one hook that genuinely differs
// (the compacts carry the floppy path where the others carry the boot disk).
struct DiskBaysBinding {
    bool floppyDrives = true;    // the board has internal/external floppies
    bool removableBays = true;   // bays whose medium can change live (a CD)
};

template <class MachineT, class Machine>
DiskBaysHost diskBaysHostFor(Machine& machine, std::vector<std::string>* extras,
                             DiskBaysBinding options = {}) {
    DiskBaysHost host;
    host.extras = extras;
    host.hardReset = [&machine] { machine.push({MachineT::Cmd::HardReset}); };
    if (options.removableBays) {
        host.bayIsCd = [&machine](int id) { return machine.bayIsCdrom(id); };
        // A bay that is not a CD refuses a live swap: its medium cannot
        // change without a reboot, and saying so beats a silent no-op.
        host.insertBay = [&machine](int id, const std::string& disk) {
            if (!machine.bayIsCdrom(id)) return false;
            machine.requestInsertBay(id, disk);
            return true;
        };
        host.ejectBay = [&machine](int id) { machine.requestEjectBay(id); };
    } else {
        host.supportsEmptyCdDrive = false;
    }
    if (options.floppyDrives) bindFloppyBays(host, machine);
    else host.hasFloppyDrive = false;
    bindScsiBays(host, machine);
    return host;
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
