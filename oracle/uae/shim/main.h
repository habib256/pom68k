// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Oracle shim replacing Hatari's main.h — empty/no-op glue for the vendored
// WinUAE CPU core (see ../VENDOR.md).

#ifndef POM68K_SHIM_MAIN_H
#define POM68K_SHIM_MAIN_H
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __cplusplus
extern "C" {
#endif
extern bool bQuitProgram;   /* m68k_go() exits its loop when set */

/* Branch prediction hints (as in Hatari's main.h) */
#ifndef likely
#if __GNUC__ >= 3
# define likely(x)   __builtin_expect(!!(x), 1)
# define unlikely(x) __builtin_expect(!!(x), 0)
#else
# define likely(x)   (x)
# define unlikely(x) (x)
#endif
#endif
#ifdef __cplusplus
}
#endif
#endif
