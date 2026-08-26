// POM68K — shared includes and the hot network-quantum seam for composers
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#pragma once

#include "MachineHost.h"
#include "MachineCatalog.h"
#include "PlatformFamilyComposers.h"
#include "GuiHostServices.h"
#include "Cpu68k.h"
#include "MacMemory.h"
#include "MacVideo.h"
#include "MacFrame.h"
#include "MacAudio.h"
#include "MacAudioHost.h"
#include "DemoRom.h"
#include "Cpu030.h"
#include "V8Memory.h"
#include "V8Video.h"
#include "SonoraMemory.h"
#include "SonoraVideo.h"
#include "SonoraCpu.h"
#include "VaspMemory.h"
#include "VaspVideo.h"
#include "VaspCpu.h"
#include "RbvMemory.h"
#include "RbvVideo.h"
#include "RbvCpu.h"
#include "Cpu040.h"
#include "Q605Memory.h"
#include "CentrisMemory.h"
#include "CentrisCpu.h"
#include "Q700Memory.h"
#include "Q700Cpu.h"
#include "Q630Memory.h"
#include "Q630Cpu.h"
#include "Cpu020.h"
#include "IIfxCpu.h"
#include "IIfxMemory.h"
#include "MscCpu.h"
#include "MscMemory.h"
#include "MacIIMemory.h"
#include "TobyVideo.h"

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
