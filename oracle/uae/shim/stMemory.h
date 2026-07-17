// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Oracle shim replacing Hatari's stMemory.h — empty/no-op glue for the vendored
// WinUAE CPU core (see ../VENDOR.md).

#ifndef POM68K_SHIM_STMEMORY_H
#define POM68K_SHIM_STMEMORY_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
uint32_t STMemory_ReadLong(uint32_t addr);   /* routed to the oracle buffer (stubs.c) */
#ifdef __cplusplus
}
#endif
#endif
