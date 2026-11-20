// -----------------------------------------------------------------------------
// This file is part of Moira - A Motorola 68k emulator
//
// Copyright (C) Dirk W. Hoffmann. www.dirkwhoffmann.de
// Published under the terms of the MIT License
// -----------------------------------------------------------------------------

#pragma once

// POM68K: optional acceleration seam. Define to 0 in EVERY translation
// unit embedding an interpreter-only core. Default preserves existing hosts;
// CPU/MMU/FPU, architectural caches and their timing remain available.
#ifndef MOIRA_ENABLE_JIT
#define MOIRA_ENABLE_JIT 1
#endif

/* Set to true to enable precise timing mode (68000 and 68010 only).
 *
 * When disabled, Moira calls the 'sync' function at the end of each instruction,
 * passing the number of elapsed cycles as an argument. In precise timing mode,
 * 'sync' is called before each memory access, allowing the client to emulate
 * the surrounding hardware up to the point where the memory access occurs.
 *
 * Precise timing mode is available only for 68000 and 68010 emulation. For
 * other supported models, this setting has no effect.
 *
 * Enable to improve accuracy, disable it to enhance performance.
 */
// POM68K: the three settings that upstream ships differently can be set by
// the build; moira_upstream_config_test compiles the core with upstream's.
#ifndef MOIRA_PRECISE_TIMING
#define MOIRA_PRECISE_TIMING true // POM68K: cycle-exact Mac Plus (see POM68K_VENDOR.md)
#endif

/* Set to true to implement the CPU interface as virtual functions.
 *
 * To interact with the environment (e.g., for reading data from memory),
 * the CPU calls an appropriate function that must be implemented by the client.
 * When this option is enabled, all API functions are declared virtual, following
 * the standard object-oriented programming (OOP) paradigm. However, virtual
 * functions introduce a performance penalty. Setting this option to false allows
 * the client API to be statically linked, improving performance.
 *
 * Enable to adhere to the standard OOP paradigm, disable to gain speed.
 */
#define MOIRA_VIRTUAL_API true

/* Set to true to enable address error checking.
 *
 * The 68000 and 68010 trigger an address error violation if a word or longword
 * is accessed at an odd memory address.
 *
 * Enable to improve accuracy, disable to gain speed.
 */
#ifndef MOIRA_EMULATE_ADDRESS_ERROR
#define MOIRA_EMULATE_ADDRESS_ERROR true // POM68K: Mac software relies on address errors
#endif

/* Set to true to emulate function code pins FC0 - FC2.
 *
 * Whenever memory is accessed, the function code pins enable external hardware
 * to inspect the access type. If used, these pins are usually connected to an
 * external memory management unit (MMU).
 *
 * Enable to improve accuracy, disable to gain speed.
 */
#define MOIRA_EMULATE_FC true

/* Set to true to emulate the 68020 instruction cache.
 *
 * The 68020 caches instruction fetches in a small on-chip cache (see
 * MoiraCache.h). When this option is disabled, every instruction fetch is
 * treated as a cache miss and goes straight to the bus, which is faster to
 * emulate but does not reproduce the cycle savings a real cache provides.
 * This setting has no effect for other supported models, which have no
 * instruction cache.
 *
 * Enable to improve accuracy, disable to gain speed.
 */
// POM68K: off by default, overridable by the build like the settings above.
// This core's 68020 cycle tables are the manual's "cache case" column
// (POM68K_VENDOR.md row 129), so upstream's -2 per cache hit would count
// the cache twice, and the Mac II/LC boot gates pin today's timing. With it
// off, the core issues exactly the bus cycles and cycle counts it did
// before the cache arrived: no lookup, no latch, no dynamic bus sizing
// (dsack is never asked), no cycle budget (MoiraMacros.h, SYNC_CP).
// Turning it on for the fork is an open item: the
// tables and the per-hit discount have to be reconciled first. Upstream's
// own configuration sets it to true.
// When on, it applies to the 68020/EC020 only: the 68030/68040 keep their
// own cache models (PomIcache, MoiraCache040.h) and the EC030 has none.
#ifndef MOIRA_EMULATE_ICACHE
#define MOIRA_EMULATE_ICACHE false
#endif

// POM68K: the 68020 i-cache reads instructions behind the JIT's fetch
// window (pomJitFetch020, POM68K_VENDOR.md row 55), which would bypass it.
// No configuration needs both; refuse the combination instead of letting
// one of them be silently wrong.
#if MOIRA_EMULATE_ICACHE && MOIRA_ENABLE_JIT
#error "MOIRA_EMULATE_ICACHE requires MOIRA_ENABLE_JIT=0 (the JIT fetch window bypasses the 68020 i-cache)"
#endif

/* Set to true to enable the disassembler.
 *
 * The disassembler requires a jump table which consumes about 1MB of memory,
 * plus 0.25MB for the instruction info table storing information about the
 * instruction (Instr I), the addressing mode (Mode M), and the size attribute
 * (Size S) for all 65536 opcode words.
 *
 * Disable to save space.
 */
#define MOIRA_ENABLE_DASM true

/* Enables Musashi compatibility mode.
 *
 * When set to true, Moira runs in a special mode designed for compatibility
 * with the Musashi emulator. This mode is primarily used by the test runner
 * application to compare Moira's behavior against Musashi.
 *
 * Set to false for improved accuracy.
 */
#ifndef MOIRA_MIMIC_MUSASHI
#define MOIRA_MIMIC_MUSASHI false // POM68K: accuracy over Musashi compatibility
#endif

/* The following macro appears at the beginning of each instruction handler.
 * Moira will call 'willExecute(...)' for all listed instructions.
 */
#define MOIRA_WILL_EXECUTE    I == Instr::STOP || I == Instr::TAS || I == Instr::BKPT

/* The following macro appear at the end of each instruction handler.
 * Moira will call 'didExecute(...)' for all listed instructions.
 */
#define MOIRA_DID_EXECUTE I == Instr::RESET
