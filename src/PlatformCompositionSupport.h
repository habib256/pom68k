// POM68K — shared includes and the hot network-quantum seam for composers
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#pragma once

// Shared by every composer, and nothing else: the host contract, the catalogue,
// the typed facades and the process services. The HARDWARE headers used to live
// here too -- all twelve families' memories, CPUs and video decoders in one
// umbrella, which is what the 2026-08-26 review called the composition fan-in
// (TODO.md 0.B item 6). Each Platform*.cpp now includes its own family, so
// adding a family touches its own composer instead of a header every composer
// reads.
#include "MachineHost.h"
#include "MachineCatalog.h"
#include "PlatformFamilyComposers.h"
#include "GuiHostServices.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

using pom68k::gui::GuiHostServices;
using pom68k::MachineKind;

template <class Mem, class Cpu, class OnSlice>
void runQuantumWithWire(GuiHostServices& services, Mem& mem, Cpu& cpu,
                        std::int64_t frameCycles, OnSlice&& onSlice) {
    services.runNetworkQuantum(
        mem, cpu, frameCycles, std::forward<OnSlice>(onSlice));
}

template <class Mem, class Cpu>
void runQuantumWithWire(GuiHostServices& services, Mem& mem, Cpu& cpu,
                        std::int64_t frameCycles) {
    services.runNetworkQuantum(mem, cpu, frameCycles);
}
