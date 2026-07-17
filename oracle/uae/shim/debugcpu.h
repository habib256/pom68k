// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Oracle shim replacing Hatari's debugcpu.h — empty/no-op glue for the vendored
// WinUAE CPU core (see ../VENDOR.md).

#ifndef POM68K_SHIM_DEBUGCPU_H
#define POM68K_SHIM_DEBUGCPU_H
#ifdef __cplusplus
extern "C" {
#endif
static inline void DebugCpu_Check(void) {}
#ifdef __cplusplus
}
#endif
#endif
