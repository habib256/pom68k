// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Oracle shim replacing Hatari's symbols.h (see ../VENDOR.md).

#ifndef POM68K_SHIM_SYMBOLS_H
#define POM68K_SHIM_SYMBOLS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { SYMTYPE_TEXT = 1, SYMTYPE_WEAK = 2, SYMTYPE_DATA = 4, SYMTYPE_BSS = 8, SYMTYPE_ABS = 16, SYMTYPE_ALL = 31 } symtype_t;
const char *Symbols_GetByCpuAddress(uint32_t addr, int symtype);
#ifdef __cplusplus
}
#endif
#endif
