// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// The configuration the q605_* rigs build their MEMCjr board from: the
// catalogue's Quadra 605 profile ($A55A2225 board ID, 68040 with its on-die
// FPU) through the same BoardIdentity.h mapping the product applies. Before
// 2026-10-13 the rigs passed defaultCoreConfig(), whose board ID was the
// LC 475's beside the 68040 FPU — a hybrid no Macintosh ever was.

#pragma once
#include "BoardIdentity.h"

inline const pom68k::CoreConfig& q605Core() {
    static const pom68k::CoreConfig config =
        pom68k::profileCoreConfig(pom68k::SnapMachine::Q605);
    return config;
}
