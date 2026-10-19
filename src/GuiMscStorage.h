// POM68K — the PowerBook 150's storage additions to the MSC runner.
#pragma once
#include <cstdio>
#include <string>
#include "AtaDiskIo.h"

namespace pom68k::gui {
struct MscStartupFloppy { std::string path; bool inserted = false; };

template<class Mem, class Services>
bool attachMscBootDisk(Mem& mem, std::string& path, Services& services) {
    if (!mem.isPb150()) {
        if (path.empty() || !mem.attachScsi(path, true)) return false;
        installAgentStartupItem(mem.scsiDisk(), services);
        return true;
    }
    if (const auto& ide = services.config().core().storage.ideDisk; ide) path = *ide;
    if (path.empty() || !mem.attachIde(path, true)) return false;
    hfsinject::AtaDiskIo io(mem.ide());
    installAgentStartupItem(io, services);
    return true;
}

template<class Mem, class Services>
MscStartupFloppy attachMscStorage(Mem& mem, Services& services) {
    MscStartupFloppy floppy;
    if (!mem.isPb150()) return floppy;
    floppy.path = services.config().devices().startupFloppy.value_or("");
    if (!floppy.path.empty()) floppy.inserted = mem.internalDrive().insert(floppy.path);
    mem.internalDrive().setWriteBack(services.config().devices().floppyWriteBack);
    return floppy;
}
} // namespace pom68k::gui
