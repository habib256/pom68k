// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Oracle shim replacing Hatari's vdi.h — empty/no-op glue for the vendored
// WinUAE CPU core (see ../VENDOR.md).

#ifndef POM68K_SHIM_VDI_H
#define POM68K_SHIM_VDI_H
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
extern bool bVdiAesIntercept;
extern uint32_t VDI_OldPC;
static inline bool VDI_AES_Entry(void) { return false; }
#ifdef __cplusplus
}
#endif
#endif
