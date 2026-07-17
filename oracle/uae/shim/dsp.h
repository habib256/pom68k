// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Oracle shim replacing Hatari's dsp.h — empty/no-op glue for the vendored
// WinUAE CPU core (see ../VENDOR.md).

#ifndef POM68K_SHIM_DSP_H
#define POM68K_SHIM_DSP_H
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
#define DSP_CPU_FREQ_RATIO 2
extern bool bDspEnabled;
extern uint64_t DSP_CyclesGlobalClockCounter;
static inline void DSP_Run(int nCycles) { (void)nCycles; }
static inline int  DSP_ProcessIACK(void) { return -1; }
static inline int  DSP_GetHREQ(void) { return 0; }
#ifdef __cplusplus
}
#endif
#endif
