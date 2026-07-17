// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Oracle shim replacing Hatari's xbios.h — empty/no-op glue for the vendored
// WinUAE CPU core (see ../VENDOR.md).

#ifndef POM68K_SHIM_XBIOS_H
#define POM68K_SHIM_XBIOS_H
#include <stdbool.h>
extern bool XBios(void);
#endif
