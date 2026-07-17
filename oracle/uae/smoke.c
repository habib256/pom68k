// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// liboracle_uae.so smoke test (O1 gate):
//  (a) MOVEQ #42,D0 ; ADD.L D0,D0            -> D0 == 84
//  (b) TRAP #0 through VBR                   -> format 0 frame, SR/PC vector
//  (c) MMU translation, TC.E=1               -> MOVE lands at PHYSICAL addr
//  (d) PTEST                                 -> executes, MMUSR plausible
//  (e) MMU fault                             -> format $A/$B frame + vector 2
//
// MMU tables built per the MC68030 User's Manual (§9: short-format page
// descriptors, function-code lookup disabled, IS=0).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "oracle_api.h"

#define MEM_SIZE (1u << 20) /* 1 MB, power of two */
static uint8_t mem[MEM_SIZE];

static int failures = 0;

#define CHECK(cond, ...) do { \
	if (!(cond)) { \
		failures++; \
		printf("FAIL(%s:%d): ", __FILE__, __LINE__); \
		printf(__VA_ARGS__); \
		printf("\n"); \
	} \
} while (0)

static void w8(uint32_t a, uint8_t v)  { mem[a & (MEM_SIZE - 1)] = v; }
static void w16(uint32_t a, uint16_t v) { w8(a, v >> 8); w8(a + 1, v & 0xff); }
static void w32(uint32_t a, uint32_t v) { w16(a, v >> 16); w16(a + 2, v & 0xffff); }
static uint8_t  r8(uint32_t a)  { return mem[a & (MEM_SIZE - 1)]; }
static uint16_t r16(uint32_t a) { return (uint16_t)((r8(a) << 8) | r8(a + 1)); }
static uint32_t r32(uint32_t a) { return ((uint32_t)r16(a) << 16) | r16(a + 2); }

static void base_state(OracleState *st, uint32_t pc)
{
	memset(st, 0, sizeof *st);
	st->sr = 0x2700;      /* S=1, M=0, IPL7, trace off */
	st->pc = pc;
	st->a[7] = 0x8000;    /* active ISP */
	st->isp = 0x8000;
	st->usp = 0x7000;
	st->msp = 0x6000;
}

/* ---------- (a) integer stepping ---------------------------------------- */
static void test_moveq_add(void)
{
	OracleState st;
	memset(mem, 0, sizeof mem);
	w16(0x1000, 0x702A);  /* MOVEQ #42,D0 */
	w16(0x1002, 0xD080);  /* ADD.L D0,D0  */

	base_state(&st, 0x1000);
	oracle_set_state(&st);

	int32_t c1 = oracle_step();
	int32_t c2 = oracle_step();
	oracle_get_state(&st);

	CHECK(c1 > 0 && c2 > 0, "(a) cycles: %d, %d", c1, c2);
	CHECK(st.d[0] == 84, "(a) D0 = %u, expected 84", st.d[0]);
	CHECK(st.pc == 0x1004, "(a) PC = %08x, expected 00001004", st.pc);
	CHECK((st.sr & 0x000F) == 0, "(a) NZVC = %x, expected 0", st.sr & 0xF);
	printf("(a) MOVEQ/ADD.L: D0=%u PC=%08x cycles=%d+%d\n", st.d[0], st.pc, c1, c2);
}

/* ---------- (b) TRAP #0 via VBR ------------------------------------------ */
static void test_trap_vbr(void)
{
	OracleState st;
	memset(mem, 0, sizeof mem);
	w16(0x1000, 0x4E40);       /* TRAP #0 */
	w32(0x2000 + 32 * 4, 0x00003000); /* vector 32 in a RELOCATED vector base */

	base_state(&st, 0x1000);
	st.sr = 0x0700;            /* start in USER mode: S must rise, A7 -> ISP */
	st.a[7] = 0x7000;          /* active USP */
	st.usp = 0x7000;
	st.vbr = 0x2000;
	oracle_set_state(&st);

	int32_t c = oracle_step();
	oracle_get_state(&st);

	CHECK(c > 0, "(b) cycles %d", c);
	CHECK(st.pc == 0x3000, "(b) PC = %08x, expected 00003000 (via VBR)", st.pc);
	CHECK(st.sr & 0x2000, "(b) SR = %04x, S not set", st.sr);
	CHECK(st.usp == 0x7000, "(b) USP clobbered: %08x", st.usp);
	CHECK(st.a[7] == 0x8000 - 8, "(b) A7 = %08x, expected 00007FF8", st.a[7]);
	/* Format 0 four-word frame: SR, PC(hi,lo), format|vector */
	uint16_t fsr  = r16(0x7FF8);
	uint32_t fpc  = r32(0x7FFA);
	uint16_t fvo  = r16(0x7FFE);
	CHECK(fsr == 0x0700, "(b) stacked SR = %04x, expected 0700", fsr);
	CHECK(fpc == 0x1002, "(b) stacked PC = %08x, expected 00001002", fpc);
	CHECK(fvo == (0x0 << 12 | 32 * 4), "(b) frame word = %04x, expected 0080", fvo);
	printf("(b) TRAP #0: PC=%08x SR=%04x frame=[%04x %08x %04x]\n",
	       st.pc, st.sr, fsr, fpc, fvo);
}

/* ---------- MMU table helpers -------------------------------------------- */
/*
 * Minimal translation tree (MC68030 UM §9.5, short-format):
 *   TC = $80F04445: E=1, SRE=0, FCL=0, PS=$F (32K pages), IS=0,
 *        TIA=4 TIB=4 TIC=4 TID=5  (4+4+4+5+15 = 32 bits)
 *   CRP: limit $7FFF, DT=3 (8-byte descriptors? no: DT=2 short) -> we use
 *        DT=2 (valid short), table address TABLE_A.
 *   Table A (16 short descriptors @ TABLE_A): entry 0 -> DT=2 -> TABLE_B
 *   Table B entry 0 -> DT=2 -> TABLE_C
 *   Table C entry 0 -> DT=1 (early termination page descriptor):
 *        all remaining levels map linearly from PHYS_BASE.
 * Logical $00000000..$0000FFFF then maps to PHYS_BASE..PHYS_BASE+$FFFF.
 */
#define TABLE_A 0x40000
#define TABLE_B 0x41000
#define TABLE_C 0x42000
#define PHYS_BASE 0x80000u /* within 1 MB buffer */

static void build_mmu_tables(void)
{
	/* Table A: 16 entries (TIA=4), short format descriptors */
	for (int i = 0; i < 16; i++)
		w32(TABLE_A + 4 * i, 0x00000000);         /* invalid (DT=0) */
	w32(TABLE_A + 0, TABLE_B | 0x2);              /* valid short table */

	/* Table B: 16 entries (TIB=4) */
	for (int i = 0; i < 16; i++)
		w32(TABLE_B + 4 * i, 0x00000000);
	w32(TABLE_B + 0, TABLE_C | 0x2);

	/* Table C: 16 entries (TIC=4): entry 0 = early-termination page
	 * descriptor (DT=1): maps the whole remaining 1 MB range linearly. */
	for (int i = 0; i < 16; i++)
		w32(TABLE_C + 4 * i, 0x00000000);
	w32(TABLE_C + 0, PHYS_BASE | 0x1);            /* page descriptor, RW */
}

static void mmu_state(OracleState *st, uint32_t pc)
{
	base_state(st, pc);
	st->tc = 0x80F04445;
	st->crp = ((uint64_t)0x7FFF0002u << 32) | TABLE_A; /* limit $7FFF, DT=2 */
	st->srp = 0;
	st->tt0 = 0;
	st->tt1 = 0;
}

/* ---------- (c) translated MOVE ------------------------------------------ */
static void test_mmu_translation(void)
{
	OracleState st;
	memset(mem, 0, sizeof mem);
	build_mmu_tables();

	/* Code at logical $1000 = physical PHYS_BASE+$1000 (fetches translate!) */
	w16(PHYS_BASE + 0x1000, 0x33FC);  /* MOVE.W #$CAFE,($2000).L */
	w16(PHYS_BASE + 0x1002, 0xCAFE);
	w32(PHYS_BASE + 0x1004, 0x00002000);

	mmu_state(&st, 0x1000);
	oracle_set_state(&st);
	int32_t c = oracle_step();
	oracle_get_state(&st);

	CHECK(c > 0, "(c) cycles %d", c);
	CHECK(st.pc == 0x1008, "(c) PC = %08x, expected 00001008", st.pc);
	CHECK(r16(PHYS_BASE + 0x2000) == 0xCAFE,
	      "(c) physical %08x = %04x, expected CAFE (translation inactive?)",
	      PHYS_BASE + 0x2000, r16(PHYS_BASE + 0x2000));
	CHECK(r16(0x2000) == 0, "(c) logical alias written: mem[2000]=%04x", r16(0x2000));
	printf("(c) MMU MOVE: phys[%05x]=%04x PC=%08x\n",
	       PHYS_BASE + 0x2000, r16(PHYS_BASE + 0x2000), st.pc);
}

/* ---------- (d) PTEST ----------------------------------------------------- */
static void test_ptest(void)
{
	OracleState st;
	memset(mem, 0, sizeof mem);
	build_mmu_tables();

	/* PTESTR #1,(A0),#7  at logical $1000; A0 = $2000 (mapped R/W) */
	w16(PHYS_BASE + 0x1000, 0xF010);
	w16(PHYS_BASE + 0x1002, 0x8210 | (7 << 10)); /* PTESTR fc=#1, 3 levels */

	mmu_state(&st, 0x1000);
	st.a[0] = 0x2000;
	oracle_set_state(&st);
	int32_t c = oracle_step();
	oracle_get_state(&st);

	CHECK(c > 0, "(d) cycles %d", c);
	CHECK(st.pc == 0x1004, "(d) PC = %08x, expected 00001004", st.pc);
	/* Plausible MMUSR: no bus error (B=0), no invalid (I=0), no supervisor
	 * violation, N (levels) == 3. Level count is bits 2..0. */
	CHECK((st.mmusr & 0x8000) == 0, "(d) MMUSR B set: %04x", st.mmusr);
	CHECK((st.mmusr & 0x0400) == 0, "(d) MMUSR I set: %04x", st.mmusr);
	CHECK((st.mmusr & 0x0007) == 3, "(d) MMUSR N = %d, expected 3 (MMUSR=%04x)",
	      st.mmusr & 7, st.mmusr);
	printf("(d) PTESTR: MMUSR=%04x PC=%08x\n", st.mmusr, st.pc);
}

/* ---------- (e) MMU fault -> format $A/$B frame, vector 2 ---------------- */
static void test_mmu_fault(void)
{
	OracleState st;
	memset(mem, 0, sizeof mem);
	build_mmu_tables();

	/* Bus-error vector (2) inside mapped logical space: logical $2000 holds
	 * the handler address; vector table at logical 0 -> phys PHYS_BASE. */
	w32(PHYS_BASE + 2 * 4, 0x00002000);

	/* MOVE.W D0,($10000000).L : table A index = 1 (bits 31-28) and only
	 * entry 0 is valid -> invalid descriptor -> MMU translation fault. */
	w16(PHYS_BASE + 0x1000, 0x33C0);
	w32(PHYS_BASE + 0x1002, 0x10000000);

	mmu_state(&st, 0x1000);
	st.d[0] = 0xBEEF;
	oracle_set_state(&st);
	int32_t c = oracle_step();
	oracle_get_state(&st);

	CHECK(c > 0, "(e) cycles %d", c);
	CHECK(st.pc == 0x2000, "(e) PC = %08x, expected 00002000 (bus-error vector)", st.pc);
	CHECK(st.sr & 0x2000, "(e) SR = %04x, S not set", st.sr);

	/* The frame was pushed at logical ISP (mapped): read it back through
	 * the physical alias. Format word is frame[6..7]: $A or $B, vector 2. */
	uint32_t sp_log = st.a[7];
	uint32_t sp_phys = PHYS_BASE + sp_log; /* linear map */
	uint16_t fvo = r16(sp_phys + 6);
	int fmt = fvo >> 12;
	int vec = (fvo & 0xFFF) >> 2;
	CHECK(fmt == 0xA || fmt == 0xB, "(e) frame format = $%X, expected $A/$B", fmt);
	CHECK(vec == 2, "(e) frame vector = %d, expected 2", vec);
	/* CCR may already reflect the aborted MOVE (write pending in the frame):
	 * 68030 write faults complete the flag update before the fault. */
	uint16_t fsr = r16(sp_phys + 0);
	CHECK((fsr & 0xFF00) == 0x2700, "(e) stacked SR = %04x, expected 27xx", fsr);
	int fsize = (fmt == 0xA) ? 32 : 92;
	CHECK(sp_log == 0x8000 - fsize, "(e) A7 = %08x, expected %08x (format $%X)",
	      sp_log, 0x8000 - fsize, fmt);
	printf("(e) MMU fault: format $%X vector %d PC=%08x A7=%08x\n",
	       fmt, vec, st.pc, sp_log);
}

int main(void)
{
	printf("oracle: %s\n", oracle_name());
	if (oracle_init(mem, MEM_SIZE) != 0) {
		printf("FAIL: oracle_init\n");
		return 1;
	}

	test_moveq_add();
	test_trap_vbr();
	test_mmu_translation();
	test_ptest();
	test_mmu_fault();

	if (failures) {
		printf("SMOKE FAILED: %d failure(s)\n", failures);
		return 1;
	}
	printf("SMOKE PASSED\n");
	return 0;
}
