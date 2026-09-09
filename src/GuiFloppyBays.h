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
