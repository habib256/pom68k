// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Oracle shim replacing Hatari's hatari-glue.h (see ../VENDOR.md).

#ifndef POM68K_SHIM_HATARI_GLUE_H
#define POM68K_SHIM_HATARI_GLUE_H

#include "sysdeps.h"
#include "options_cpu.h"
#include "cycles.h"

#ifdef __cplusplus
extern "C" {
#endif
extern int pendingInterrupts;
extern void customreset(void);
extern int intlev(void);
extern void UAE_Set_Quit_Reset(bool hard);
extern void UAE_Set_State_Save(void);
extern void UAE_Set_State_Restore(void);
extern uae_u32 extra_cycle; /* from vendored cpu custom.c */
#ifdef __cplusplus
}
#endif
#endif
