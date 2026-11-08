// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Compatibility seam for the CPU wrappers. Moira owns the state inventory;
// this adapter supplies POM68K's archive and debugger base.
#pragma once
#include "MoiraDebugSeam.h"
#include "SaveState.h"

class MoiraSnapshot : public MoiraDebugSeam {
protected:
    template<class Ar> void visitCpuCommon(Ar& ar) { pomVisitState(ar); }
};
