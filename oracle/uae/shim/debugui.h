// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Oracle shim replacing Hatari's debugui.h — empty/no-op glue for the vendored
// WinUAE CPU core (see ../VENDOR.md).

#ifndef POM68K_SHIM_DEBUGUI_H
#define POM68K_SHIM_DEBUGUI_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define REASON_CPU_EXCEPTION 0
static inline void DebugUI(int reason) { (void)reason; }
static inline void DebugUI_Exceptions(int nr, uint32_t pc) { (void)nr; (void)pc; }
#ifdef __cplusplus
}
#endif
#endif
