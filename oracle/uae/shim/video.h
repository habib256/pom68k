// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Oracle shim replacing Hatari's video.h — empty/no-op glue for the vendored
// WinUAE CPU core (see ../VENDOR.md).

#ifndef POM68K_SHIM_VIDEO_H
#define POM68K_SHIM_VIDEO_H
#ifdef __cplusplus
extern "C" {
#endif
static inline void Video_GetPosition(int *pFrameCycles, int *pHBL, int *pLineCycles)
	{ *pFrameCycles = 0; *pHBL = 0; *pLineCycles = 0; }
#ifdef __cplusplus
}
#endif
#endif
