// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Oracle shim replacing Hatari's dialog.h. cpu_halt() under
// WINUAE_FOR_HATARI ends in Dialog_HaltDlg(); the oracle records the halt
// (double fault) so oracle_step() can return a negative value.

#ifndef POM68K_SHIM_DIALOG_H
#define POM68K_SHIM_DIALOG_H
#ifdef __cplusplus
extern "C" {
#endif
extern int pom_oracle_halted;
static inline void Dialog_HaltDlg(void) { pom_oracle_halted = 1; }
#ifdef __cplusplus
}
#endif
#endif
