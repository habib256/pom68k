// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Definitions for the Hatari-side globals and functions the vendored WinUAE
// core links against. Everything here is inert: the oracle has no machine
// around the CPU (no MFP/DSP/blitter/savestates, no interrupts pending).

#include "main.h"
#include "sysconfig.h"
#include "sysdeps.h"
#include "options_cpu.h"
#include "maccess.h"
#include "memory.h"
#include "newcpu.h"
#include "savestate.h"
#include "m68000.h"
#include "blitter.h"

// main.h
bool bQuitProgram = false;

// hatari-glue.c defines these in Hatari; the oracle owns them here.
struct uae_prefs currprefs, changed_prefs;

// bios.h / xbios.h — TOS trap intercepts, never taken in the oracle
bool Bios(void) { return false; }
bool XBios(void) { return false; }

// debug.c (DEBUGGER disabled): disasm.c still reads memory through the
// debug accessors; route them to the oracle buffer.
uae_u32 get_byte_debug(uaecptr addr) { return memory_get_byte(addr); }
uae_u32 get_word_debug(uaecptr addr) { return memory_get_word(addr); }
uae_u32 get_long_debug(uaecptr addr) { return memory_get_long(addr); }
uae_u32 get_iword_debug(uaecptr addr) { return memory_get_word(addr); }
uae_u32 get_ilong_debug(uaecptr addr) { return memory_get_long(addr); }

// log.h
uint64_t LogTraceFlags = 0;
FILE *TraceFile = NULL;
int ExceptionDebugMask = 0;

// m68000.h
int  WaitStateCycles = 0;
int  BusMode = 0; /* BUS_MODE_CPU */
bool CPU_IACK = false;
int64_t pom_oracle_cycles = 0;

// cycles.h
int      nCyclesMainCounter = 0;
uint64_t CyclesGlobalClockCounter = 0;

// cycInt.h
int PendingInterruptCount = 0;

// cycles.h
int CurrentInstrCycles = 0;

// mfp.h / dsp.h / vdi.h
bool MFP_UpdateNeeded = false;
bool bDspEnabled = false;
uint64_t DSP_CyclesGlobalClockCounter = 0;
bool bVdiAesIntercept = false;
uint32_t VDI_OldPC = 0;

// hatari-glue.c: error_log (declared in options_cpu.h)
void error_log(const TCHAR *format, ...) { (void)format; }

// hatari-glue.h
int pendingInterrupts = 0;
void customreset(void) {}
int intlev(void) { return -1; } /* no interrupt source in the oracle */
void UAE_Set_Quit_Reset(bool hard) { (void)hard; }
void UAE_Set_State_Save(void) {}
void UAE_Set_State_Restore(void) {}

// dialog.h is inline, but cpu_halt() reaches us through Dialog_HaltDlg()
// only on a double fault (see the logged newcpu.c patch): expose a flag.
int pom_oracle_halted = 0;

// stMemory.h — reads routed to the oracle buffer (trace-only paths)
extern uae_u8 *pom_mem;
extern uae_u32 pom_mask;
uint32_t STMemory_ReadLong(uint32_t addr)
{
	if (!pom_mem)
		return 0;
	return ((uint32_t)pom_mem[addr & pom_mask] << 24) |
	       ((uint32_t)pom_mem[(addr + 1) & pom_mask] << 16) |
	       ((uint32_t)pom_mem[(addr + 2) & pom_mask] << 8) |
	        (uint32_t)pom_mem[(addr + 3) & pom_mask];
}

// symbols.h (disasm.c)
const char *Symbols_GetByCpuAddress(uae_u32 addr, int symtype)
{
	(void)addr; (void)symtype;
	return NULL;
}

// savestate.h — never exercised (savestate_state stays 0, defined in the
// vendored custom.c), but these must link.
int save_state(const TCHAR *filename, const TCHAR *description) { (void)filename; (void)description; return 0; }
void restore_state(const TCHAR *filename) { (void)filename; }
bool savestate_restore_finish(void) { return false; }
void savestate_restore_final(void) {}
void save_u64(uae_u64 data) { (void)data; }
void save_u32(uae_u32 data) { (void)data; }
void save_u16(uae_u16 data) { (void)data; }
void save_u8(uae_u8 data) { (void)data; }
void save_s8(uae_s8 data) { (void)data; }
uae_u64 restore_u64(void) { return 0; }
uae_u32 restore_u32(void) { return 0; }
uae_u16 restore_u16(void) { return 0; }
uae_u8 restore_u8(void) { return 0; }
uae_s8 restore_s8(void) { return 0; }

// m68000.h shim extras
bool CpuRunCycleExact = false;
bool CpuRunFuncNoret = false;
cpu_instruction_t CpuInstruction;

// blitter.h shim extras
uint16_t BlitterPhase = 0;
