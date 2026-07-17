// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Oracle shim replacing Hatari's m68000.h — empty/no-op glue for the vendored
// WinUAE CPU core (see ../VENDOR.md).

#ifndef POM68K_SHIM_M68000_H
#define POM68K_SHIM_M68000_H
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
#define BUS_ERROR_WRITE        0
#define BUS_ERROR_READ         1
#define BUS_ERROR_SIZE_BYTE    1
#define BUS_ERROR_SIZE_WORD    2
#define BUS_ERROR_SIZE_LONG    4
#define BUS_ERROR_ACCESS_INSTR 0
#define BUS_ERROR_ACCESS_DATA  1
#define BUS_MODE_CPU      0
#define BUS_MODE_BLITTER  1
#define BUS_MODE_DEBUGGER 2
#define CPU_IACK_CYCLES_MFP_CE   12
#define CPU_IACK_CYCLES_VIDEO_CE 10
#define CPU_IACK_CYCLES_START    12
#define CPU_IACK_CYCLES_MFP      12
#define CPU_IACK_CYCLES_VIDEO    12
extern int  WaitStateCycles;
extern int  BusMode;
extern bool CPU_IACK;
extern bool CpuRunCycleExact;
extern bool CpuRunFuncNoret;
typedef struct {
	int I_Cache_miss;
	int I_Cache_hit;
	int D_Cache_miss;
	int D_Cache_hit;
} cpu_instruction_t;
extern cpu_instruction_t CpuInstruction;
/* Cycle sink: the oracle accumulates cycles per oracle_step() here.
 * Side effect: cycles are only ever added while an instruction executes,
 * so this also requests the m68k_go() loop to stop after the current
 * instruction (bQuitProgram is re-armed false by glue.c on every step). */
extern int64_t pom_oracle_cycles;
extern void pom_request_break(void);  /* glue.c: bQuitProgram + SPCFLAG_BRK */
static inline void M68000_AddCycles(int cycles)            { pom_oracle_cycles += cycles; pom_request_break(); }
static inline void M68000_AddCycles_CE(int cycles)         { pom_oracle_cycles += cycles; pom_request_break(); }
static inline void M68000_AddCyclesWithPairing(int cycles) { pom_oracle_cycles += cycles; pom_request_break(); }
static inline void M68000_SetDebugger(bool debug) { (void)debug; }
static inline void M68000_RestoreDebugger(void) {}
static inline void M68000_PatchCpuTables(void) {}
static inline bool M68000_IsVerboseBusError(uint32_t pc, uint32_t addr) { (void)pc; (void)addr; return false; }
static inline void M68000_BusError(uint32_t addr, int rw, int size, int accessType, uint32_t val)
	{ (void)addr; (void)rw; (void)size; (void)accessType; (void)val; }
static inline void M68000_ClearIRQ(int intNr) { (void)intNr; }
static inline void M68000_Update_intlev(void) {}
static inline int  M68000_WaitEClock(void) { return 0; }
#ifdef __cplusplus
}
#endif
#endif
