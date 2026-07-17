// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Oracle shim replacing Hatari's blitter.h — empty/no-op glue for the vendored
// WinUAE CPU core (see ../VENDOR.md).

#ifndef POM68K_SHIM_BLITTER_H
#define POM68K_SHIM_BLITTER_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
extern uint16_t BlitterPhase;
static inline void Blitter_HOG_CPU_mem_access_before(int bus_count) { (void)bus_count; }
static inline void Blitter_HOG_CPU_mem_access_after(int bus_count) { (void)bus_count; }
static inline int  Blitter_Check_Simultaneous_CPU(void) { return 0; }
static inline void Blitter_HOG_CPU_do_cycles_after(int cycles) { (void)cycles; }
#ifdef __cplusplus
}
#endif
#endif
