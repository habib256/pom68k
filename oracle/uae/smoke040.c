// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// liboracle_uae.so 68040 smoke test (Q1 gate) — oracle_set_model(68040, 0)
// = the 68LC040 of the LC 475/Quadra 605 (integer + 040 MMU, no FPU):
//  (a) MOVEQ #42,D0 ; ADD.L D0,D0            -> D0 == 84
//  (b) TRAP #0 through VBR                   -> format 0 frame, SR/PC vector
//  (f) MOVE16 (A0)+,(A1)+                    -> 16 bytes copied, regs bumped
//  (c) MMU translation, TC.E=1               -> MOVE lands at PHYSICAL addr
//  (d) PTESTR (A0)                           -> MMUSR resident + phys addr
//  (e) MMU fault                             -> format $7 frame + vector 2
//
// MMU tables per the MC68040 User's Manual (§3: 4K pages, RI=7/PI=7/PGI=6
// bit indexes, URP/SRP roots; the supervisor walk uses SRP).

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
	uint16_t fsr  = r16(0x7FF8);
	uint32_t fpc  = r32(0x7FFA);
	uint16_t fvo  = r16(0x7FFE);
	CHECK(fsr == 0x0700, "(b) stacked SR = %04x, expected 0700", fsr);
	CHECK(fpc == 0x1002, "(b) stacked PC = %08x, expected 00001002", fpc);
	CHECK(fvo == (0x0 << 12 | 32 * 4), "(b) frame word = %04x, expected 0080", fvo);
	printf("(b) TRAP #0: PC=%08x SR=%04x frame=[%04x %08x %04x]\n",
	       st.pc, st.sr, fsr, fpc, fvo);
}

/* ---------- (f) MOVE16 — 68040-only instruction --------------------------- */
static void test_move16(void)
{
	OracleState st;
	memset(mem, 0, sizeof mem);
	w16(0x1000, 0xF620);       /* MOVE16 (A0)+,(A1)+ */
	w16(0x1002, 0x8000 | (1 << 12)); /* ext: Ay = A1 */
	for (int i = 0; i < 16; i++)
		w8(0x3000 + i, (uint8_t)(0xA0 + i));

	base_state(&st, 0x1000);
	st.a[0] = 0x3000;
	st.a[1] = 0x5000;          /* both 16-byte aligned */
	oracle_set_state(&st);

	int32_t c = oracle_step();
	oracle_get_state(&st);

	CHECK(c > 0, "(f) cycles %d", c);
	CHECK(st.pc == 0x1004, "(f) PC = %08x, expected 00001004", st.pc);
	CHECK(st.a[0] == 0x3010, "(f) A0 = %08x, expected 00003010", st.a[0]);
	CHECK(st.a[1] == 0x5010, "(f) A1 = %08x, expected 00005010", st.a[1]);
	int ok = 1;
	for (int i = 0; i < 16; i++)
		if (r8(0x5000 + i) != (uint8_t)(0xA0 + i)) ok = 0;
	CHECK(ok, "(f) 16-byte line not copied ([5000]=%02x)", r8(0x5000));
	printf("(f) MOVE16: A0=%08x A1=%08x [5000..500F] copied\n", st.a[0], st.a[1]);
}

/* ---------- MMU table helpers -------------------------------------------- */
/*
 * Minimal 68040 translation tree (MC68040 UM §3.1, 4K pages):
 *   TC = $8000: E=1, P=0 (4K) -> logical = RI(7) : PI(7) : PGI(6) : offset(12)
 *   Root table   @ ROOT_T:  128 x 4B, entry 0 -> POINTER_T | UDT=2 (resident)
 *   Pointer table@ PTR_T:   128 x 4B, entry 0 -> PAGE_T | UDT=2
 *   Page table   @ PAGE_T:   64 x 4B, entry i -> (PHYS_BASE + i*$1000) | PDT=1
 * Logical $00000000..$0003FFFF maps linearly to PHYS_BASE.. (256 KB).
 * Supervisor accesses walk SRP; user accesses walk URP — both point at
 * the same tree here.
 */
#define ROOT_T  0x40000 /* 512-byte aligned */
#define PTR_T   0x41000 /* 512-byte aligned */
#define PAGE_T  0x42000 /* 256-byte aligned */
#define PHYS_BASE 0x80000u /* within 1 MB buffer */

static void build_mmu_tables(void)
{
	for (int i = 0; i < 128; i++)
		w32(ROOT_T + 4 * i, 0x00000000);          /* invalid (UDT=0) */
	w32(ROOT_T + 0, PTR_T | 0x2);                 /* resident pointer table */

	for (int i = 0; i < 128; i++)
		w32(PTR_T + 4 * i, 0x00000000);
	w32(PTR_T + 0, PAGE_T | 0x2);

	for (int i = 0; i < 64; i++)                  /* 64 pages = 256 KB */
		w32(PAGE_T + 4 * i, (PHYS_BASE + 0x1000u * i) | 0x1); /* resident */
}

static void mmu_state(OracleState *st, uint32_t pc)
{
	base_state(st, pc);
	st->tc040 = 0x8000;        /* E=1, 4K pages */
	st->urp040 = ROOT_T;
	st->srp040 = ROOT_T;
	st->itt0 = st->itt1 = st->dtt0 = st->dtt1 = 0; /* no transparent windows */
	st->dfc = 5;               /* supervisor data (PTEST walks per DFC) */
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
	/* The walk must have set U (bit 3) on the used descriptors and U+M on
	 * the written page (MC68040UM §3.2: U set on any walk, M on write). */
	CHECK(r32(PAGE_T + 4 * 2) & 0x8, "(c) page 2 U bit not set: %08x", r32(PAGE_T + 8));
	CHECK(r32(PAGE_T + 4 * 2) & 0x10, "(c) page 2 M bit not set: %08x", r32(PAGE_T + 8));
	printf("(c) MMU MOVE: phys[%05x]=%04x PC=%08x page2=%08x\n",
	       PHYS_BASE + 0x2000, r16(PHYS_BASE + 0x2000), st.pc, r32(PAGE_T + 8));
}

/* ---------- (d) PTESTR (A0) ----------------------------------------------- */
static void test_ptest(void)
{
	OracleState st;
	memset(mem, 0, sizeof mem);
	build_mmu_tables();

	/* PTESTR (A0) at logical $1000; A0 = $2000 (mapped R/W).
	 * 68040 encoding: 1111 0101 0110 1rrr = $F568+reg (PTESTW = $F548). */
	w16(PHYS_BASE + 0x1000, 0xF568);

	mmu_state(&st, 0x1000);
	st.a[0] = 0x2000;
	oracle_set_state(&st);
	int32_t c = oracle_step();
	oracle_get_state(&st);

	CHECK(c > 0, "(d) cycles %d", c);
	CHECK(st.pc == 0x1002, "(d) PC = %08x, expected 00001002", st.pc);
	/* MMUSR: R (bit 0) set, B (bit 11) clear, PA = physical page of $2000 */
	CHECK(st.mmusr040 & 0x1, "(d) MMUSR R clear: %08x", st.mmusr040);
	CHECK((st.mmusr040 & 0x800) == 0, "(d) MMUSR B set: %08x", st.mmusr040);
	CHECK((st.mmusr040 >> 12) == ((PHYS_BASE + 0x2000) >> 12),
	      "(d) MMUSR PA = %05x, expected %05x",
	      st.mmusr040 >> 12, (PHYS_BASE + 0x2000) >> 12);
	printf("(d) PTESTR: MMUSR=%08x PC=%08x\n", st.mmusr040, st.pc);
}

/* ---------- (e) MMU fault -> format $7 frame, vector 2 -------------------- */
static void test_mmu_fault(void)
{
	OracleState st;
	memset(mem, 0, sizeof mem);
	build_mmu_tables();

	/* Bus-error vector (2) inside mapped logical space. */
	w32(PHYS_BASE + 2 * 4, 0x00002000);

	/* MOVE.W D0,($10000000).L : root index = 8 (bits 31-25) and only
	 * entry 0 is valid -> invalid descriptor -> access error. */
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

	/* Format $7 access-error frame: 30 words = $3C bytes (MC68040UM §8):
	 * +0 SR, +2 PC, +6 $7xxx|vector, +8 EA (only valid per SSW CT/CM/CU
	 * bits — not asserted here), +$C SSW, +$14 FA = the fault address. */
	uint32_t sp_log = st.a[7];
	uint32_t sp_phys = PHYS_BASE + sp_log; /* linear map */
	uint16_t fvo = r16(sp_phys + 6);
	int fmt = fvo >> 12;
	int vec = (fvo & 0xFFF) >> 2;
	CHECK(fmt == 0x7, "(e) frame format = $%X, expected $7", fmt);
	CHECK(vec == 2, "(e) frame vector = %d, expected 2", vec);
	CHECK(sp_log == 0x8000 - 0x3C, "(e) A7 = %08x, expected %08x (format $7)",
	      sp_log, 0x8000 - 0x3C);
	uint32_t fa = r32(sp_phys + 0x14);
	CHECK(fa == 0x10000000, "(e) frame FA = %08x, expected 10000000", fa);
	uint16_t fssw = r16(sp_phys + 0xC);
	CHECK(fssw != 0, "(e) frame SSW = 0 (no fault status recorded)");
	uint16_t fsr = r16(sp_phys + 0);
	CHECK((fsr & 0xFF00) == 0x2700, "(e) stacked SR = %04x, expected 27xx", fsr);
	printf("(e) MMU fault: format $%X vector %d FA=%08x SSW=%04x PC=%08x A7=%08x\n",
	       fmt, vec, fa, fssw, st.pc, sp_log);
}

int main(void)
{
	oracle_set_model(68040, 0);   /* 68LC040: integer + MMU, no FPU */
	printf("oracle: %s\n", oracle_name());
	if (oracle_init(mem, MEM_SIZE) != 0) {
		printf("FAIL: oracle_init\n");
		return 1;
	}

	test_moveq_add();
	test_trap_vbr();
	test_move16();
	test_mmu_translation();
	test_ptest();
	test_mmu_fault();

	if (failures) {
		printf("SMOKE 040 FAILED: %d failure(s)\n", failures);
		return 1;
	}
	printf("SMOKE 040 PASSED\n");
	return 0;
}
