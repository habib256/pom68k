// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Oracle shim replacing Hatari's log.h — empty/no-op glue for the vendored
// WinUAE CPU core (see ../VENDOR.md).

#ifndef POM68K_SHIM_LOG_H
#define POM68K_SHIM_LOG_H
#include <stdio.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Log levels (subset of Hatari's) */
#define LOG_FATAL 0
#define LOG_ERROR 1
#define LOG_WARN  2
#define LOG_INFO  3
#define LOG_TODO  4
#define LOG_DEBUG 5
/* Trace flags: all tracing disabled in the oracle */
#define TRACE_CPU_DISASM        0
#define TRACE_CPU_EXCEPTION     0
#define TRACE_CPU_VIDEO_CYCLES  0
#define TRACE_CPU_PAIRING       0
#define TRACE_CPU_REGS          0
#define TRACE_CPU_ALL           0
#define TRACE_MFP_EXCEPTION     0
#define TRACE_MEM               0
extern uint64_t LogTraceFlags;
extern FILE *TraceFile;
extern int ExceptionDebugMask;
#define EXCEPT_NOHANDLER 0
#define LOG_TRACE_LEVEL(level) (0)
#define LOG_TRACE(level, ...)  do {} while (0)
#define LOG_TRACE_DIRECT(...)       do {} while (0)
#define LOG_TRACE_DIRECT_INIT()     do {} while (0)
#define LOG_TRACE_DIRECT_FLUSH()    do {} while (0)
static inline void Log_Printf(int level, const char *fmt, ...) { (void)level; (void)fmt; }
static inline void Log_Trace(const char *fmt, ...) { (void)fmt; }
#ifdef __cplusplus
}
#endif
#endif
