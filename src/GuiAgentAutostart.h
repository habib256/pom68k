// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// « POM68K Disques » without a gesture in the Mac (docs/SCSI_HOTPLUG.md
// § 8): at launch, once the boot disk is attached, the agent's MacBinary
// is put into the blessed System Folder's Startup Items of the SESSION
// image — the work clone when the boot volume is a reference fixture, the
// user's own image otherwise, always through ScsiDisk's write path so the
// write log and write-back apply. The Finder then launches the agent with
// the desktop, and the Disques window offers « Monter / Démonter » from
// the first frame. Idempotent: a copy already there is left alone. Every
// outcome is printed; nothing is silent about a byte written into a
// System Folder. `POM68K_NO_AGENT_AUTOSTART=1` opts out.

#pragma once

#include "GuiHostServices.h"
#include "HfsInject.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace pom68k::gui {

// Where the agent's MacBinary lives: the Retro68 build directory of
// dev/scsiagent, or an installed copy beside the other assets.
inline std::string locateAgentBinary(const GuiHostServices& services) {
    for (const char* rel : {"dev/scsiagent/build/POM68KDisques.bin",
                            "share/POM68KDisques.bin"}) {
        const std::string p = services.locate(rel);
        if (!p.empty()) return p;
    }
    return {};
}

// `disk` is the machine's boot target after attachScsi. Returns true when
// the agent is in Startup Items at the end (installed now, or already).
inline bool installAgentStartupItem(ScsiDisk& disk, const GuiHostServices& services) {
    if (!services.config().core().storage.agentAutostart) return false;
    if (!disk.present() || disk.cdrom()) return false;
    const std::string bin = locateAgentBinary(services);
    if (bin.empty()) return false;
    std::ifstream in(bin, std::ios::binary);
    const std::vector<uint8_t> raw((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
    hfsinject::MacBinary app;
    std::string err;
    if (!hfsinject::decodeMacBinary(raw, app, err)) {
        std::fprintf(stderr, "Startup Items: %s: %s\n", bin.c_str(), err.c_str());
        return false;
    }
    hfsinject::ScsiDiskIo io(disk);
    const hfsinject::Outcome o =
        hfsinject::installStartupItem(io, app, services.hostMacSeconds());
    std::printf("Startup Items: %s\n", o.message.c_str());
    return o.kind == hfsinject::Outcome::Installed ||
           o.kind == hfsinject::Outcome::AlreadyPresent;
}

} // namespace pom68k::gui
