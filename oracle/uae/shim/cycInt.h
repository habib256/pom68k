// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Oracle shim replacing Hatari's cycInt.h — empty/no-op glue for the vendored
// WinUAE CPU core (see ../VENDOR.md).

#ifndef POM68K_SHIM_CYCINT_H
#define POM68K_SHIM_CYCINT_H
#ifdef __cplusplus
extern "C" {
#endif
extern int PendingInterruptCount;
static inline void CycInt_Process(void) {}
static inline void CycInt_Process_stop(int stop) { (void)stop; }
#ifdef __cplusplus
}
#endif
#endif
