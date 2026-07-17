// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Oracle shim replacing Hatari's mfp.h — empty/no-op glue for the vendored
// WinUAE CPU core (see ../VENDOR.md).

#ifndef POM68K_SHIM_MFP_H
#define POM68K_SHIM_MFP_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
extern bool MFP_UpdateNeeded;
static inline void MFP_UpdateIRQ_All(uint64_t eventTime) { (void)eventTime; }
static inline void MFP_DelayIRQ(void) {}
static inline int  MFP_ProcessIACK(int oldVecNr) { return oldVecNr; }
#ifdef __cplusplus
}
#endif
#endif
