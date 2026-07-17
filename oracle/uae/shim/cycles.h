// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Oracle shim replacing Hatari's cycles.h — empty/no-op glue for the vendored
// WinUAE CPU core (see ../VENDOR.md).

#ifndef POM68K_SHIM_CYCLES_H
#define POM68K_SHIM_CYCLES_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
extern int      nCyclesMainCounter;
extern uint64_t CyclesGlobalClockCounter;
extern int      CurrentInstrCycles;
static inline uint64_t Cycles_GetClockCounterOnReadAccess(void)  { return 0; }
static inline uint64_t Cycles_GetClockCounterOnWriteAccess(void) { return 0; }
static inline uint64_t Cycles_GetClockCounterImmediate(void) { return CyclesGlobalClockCounter; }
#ifdef __cplusplus
}
#endif
#endif
