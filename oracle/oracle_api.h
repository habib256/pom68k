// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// Common C ABI for the 68030 reference oracle (TODO.md § Phase 2).
// The oracle is built as a standalone shared library exposing exactly this
// API, so the fuzzing harness (oracle/fuzz/) can drive it via ctypes and
// diff it against Moira. One oracle today (WinUAE, oracle/uae/); the ABI
// keeps a pluggable second slot (fuzz030.py --b) open for a future one.
//
// Memory model: the HOST owns a flat power-of-two buffer; the oracle
// accesses it modulo (size - 1). Big-endian byte order in the buffer
// (mem[0] = MSB of the long at 0), matching the SST RAM cell convention.
// MMU translation tables therefore live in that same buffer, which lets the
// fuzzer build real descriptor trees for PTEST/PLOAD/translated accesses.
//
// Exchange format: SingleStepTests-style JSON ("SST030"), the 68030
// extension of tests/sst68000.cpp's format — see oracle/README.md.

#ifndef POM68K_ORACLE_API_H
#define POM68K_ORACLE_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Full 68030 (+68881/2 for O5) programmer-visible state.
// All values raw/architectural; unused fields must be zeroed by oracles.
typedef struct OracleState {
    // Integer unit
    uint32_t d[8];
    uint32_t a[8];          // a[7] = SP currently active per SR (S/M bits)
    uint32_t usp, isp, msp; // the three 68030 stack pointers
    uint32_t pc;            // address of next instruction to execute
    uint16_t sr;            // full status register (T1 T0 S M I2-0 X N Z V C)
    // Control registers (68010+/68020+)
    uint32_t vbr;
    uint32_t sfc, dfc;      // 3 bits each
    uint32_t cacr, caar;
    // 68030 on-chip MMU registers
    uint64_t crp, srp;      // 64-bit root pointers (hi 32 = limit/control)
    uint32_t tc;            // translation control (E bit enables the MMU)
    uint32_t tt0, tt1;      // transparent translation
    uint16_t mmusr;         // PSR/MMUSR
    // 68881/68882 FPU (zero until O5)
    uint32_t fp[8][3];      // raw 96-bit extended: [0]=exp/sign word<<16, [1]=hi mantissa, [2]=lo
    uint32_t fpcr, fpsr, fpiar;
    // STOP state: 1 if the CPU is stopped waiting for an interrupt
    uint8_t  stopped;
    uint8_t  pad[3];
} OracleState;

// Identify the library ("winuae 5.x / previous r####").
const char* oracle_name(void);

// Bind the flat memory buffer. size must be a power of two (mask = size-1).
// Must be called before any other call. Returns 0 on success.
int oracle_init(uint8_t* mem, uint32_t size);

// Load the complete CPU state (a full reset + restore; caches invalidated).
void oracle_set_state(const OracleState* st);

// Execute exactly one instruction (including any exception processing it
// triggers: MMU translation faults push their format $A/$B frame into the
// host buffer, vectors are fetched from it, etc.).
// Returns the number of CPU cycles consumed (>= 1), or a negative value if
// the oracle could not step (double fault / halt).
int32_t oracle_step(void);

// Read back the complete CPU state.
void oracle_get_state(OracleState* st);

#ifdef __cplusplus
}
#endif

#endif // POM68K_ORACLE_API_H
