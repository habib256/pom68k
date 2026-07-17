// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Oracle shim replacing Hatari's scc.h — empty/no-op glue for the vendored
// WinUAE CPU core (see ../VENDOR.md).

#ifndef POM68K_SHIM_SCC_H
#define POM68K_SHIM_SCC_H
#ifdef __cplusplus
extern "C" {
#endif
static inline int SCC_Process_IACK(void) { return -1; }
#ifdef __cplusplus
}
#endif
#endif
