// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
//
// oracle_api.h implementation over the vendored WinUAE/Hatari 68030 core
// (plain C: the vendored core is compiled as C, C linkage throughout).
// (O1 — primary oracle, TODO.md § Phase 2). Single-stepping is done through
// the official entry point m68k_go() -> m68k_run_mmu030():
//  - SPCFLAG_BRK makes do_specialties() return 1 after the instruction,
//  - the M68000_AddCycles() shim raises bQuitProgram as soon as any cycles
//    are consumed, which breaks the m68k_go() loop (and, via the logged
//    newcpu.c patch, the mmu030 run loop after exception processing),
//  - Dialog_HaltDlg() (reached from cpu_halt()) flags a double fault.
// MMU faults thus run the full upstream path: format $A/$B frame pushed
// through mmu030 translation into the host buffer, vector fetch included.

#include "main.h"
#include "sysconfig.h"
#include "sysdeps.h"
#include "options_cpu.h"
#include "maccess.h"
#include "memory.h"
#include "newcpu.h"
#include "cpummu.h"
#include "cpummu030.h"
#include "fpp.h"

#include "../oracle_api.h"

/* Built with -fvisibility=hidden; only the API is exported (oracle_uae.map). */
#define ORACLE_EXPORT __attribute__((visibility("default")))

#include <string.h>

// shim/memglue.c
/* C core: C linkage throughout */
extern uae_u8 *pom_mem;
extern uae_u32 pom_mask;
extern int pom_mem_frozen;
void pom_memory_init(uae_u8 *mem, uae_u32 size);
// shim/stubs.c
extern int64_t pom_oracle_cycles;
extern int pom_oracle_halted;
// upstream/fpp.c (POM68K patch 7): resets the static fsave_data block
void pom_fpu_clear_internal(void);

static int pom_inited = 0;

// Q1: model selected before oracle_init (68030+68882 default = the O1-O5
// LC II pairing; 68040 + fpu 0 = the 68LC040 of the LC 475/Quadra 605).
static int pom_cpu_model = 68030;
static int pom_fpu_model = 68882;

ORACLE_EXPORT void oracle_set_model(int cpu_model, int fpu_model)
{
	if (pom_inited)
		return;
	if (cpu_model == 68030 || cpu_model == 68040)
		pom_cpu_model = cpu_model;
	if (fpu_model == 0 || fpu_model == 68881 || fpu_model == 68882 ||
	    fpu_model == 68040)
		pom_fpu_model = fpu_model;
}

// Called from the M68000_AddCycles() shim: cycles are only consumed while an
// instruction executes, so request the run loop to stop right after it.
// SPCFLAG_BRK is re-armed here because m68k_reset2() (init) clears spcflags.
void pom_request_break(void)
{
	bQuitProgram = true;
	set_special(SPCFLAG_BRK);
}

ORACLE_EXPORT const char *oracle_name(void)
{
	return pom_cpu_model == 68040
	    ? "winuae mmu040 softfloat (hatari 2026-07-06 e77819f7)"
	    : "winuae mmu030+68882 softfloat (hatari 2026-07-06 e77819f7)";
}

static void pom_default_prefs(struct uae_prefs *p)
{
	memset(p, 0, sizeof *p);
	p->cpu_model = pom_cpu_model;
	p->mmu_model = pom_cpu_model;
	p->mmu_ec = false;
	// O5: 68882 fitted, SOFTFLOAT backend (fpu_mode > 0 selects
	// fp_init_softfloat in fpp.c fpu_reset() — deterministic across hosts,
	// unlike fpp_native's host doubles). The oracle pair models
	// 68030+68882 even though the real LC II has no FPU socket.
	// Q1: fpu_model 0 = 68LC040 (no FPU; F-line traps unimplemented).
	p->fpu_model = pom_fpu_model;
	p->fpu_mode = pom_fpu_model ? 1 : 0;
	p->cpu_compatible = false;   // mode 5: op_smalltbl_32 (Previous MMU030)
	                             // / 040: op_smalltbl_31 (Aranym MMU040)
	p->cpu_cycle_exact = false;
	p->cpu_memory_cycle_exact = false;
	p->blitter_cycle_exact = false;
	p->cpu_data_cache = false;
	p->address_space_24 = false;
	p->cachesize = 0;
	p->m68k_speed = 0;
	p->cpu_frequency = 0;
	p->cpu_clock_multiplier = 0;
	p->int_no_unimplemented = false;
	p->fpu_no_unimplemented = false;
}

ORACLE_EXPORT int oracle_init(uint8_t *mem, uint32_t size)
{
	if (!mem || size == 0 || (size & (size - 1)) != 0)
		return -1;

	pom_memory_init(mem, size);

	pom_default_prefs(&currprefs);
	pom_default_prefs(&changed_prefs);

	init_m68k();

	// One controlled boot pass through m68k_go(): quit_program==UAE_RESET
	// runs prefs_changed_cpu()/build_cpufunctbl()/set_x_funcs()/m68k_reset2()
	// (all static in newcpu.c). The memory shim is frozen so the reset vector
	// reads 0 (one harmless ORI.B #0,D0 executes) and nothing in the host
	// buffer is touched. bQuitProgram+SPCFLAG_BRK stop after that instruction.
	pom_mem_frozen = 1;
	pom_oracle_cycles = 0;
	pom_oracle_halted = 0;
	bQuitProgram = false;
	quit_program = UAE_RESET;
	set_special(SPCFLAG_BRK);
	m68k_go(1);
	pom_mem_frozen = 0;

	pom_inited = 1;
	return 0;
}

ORACLE_EXPORT void oracle_set_state(const OracleState *st)
{
	if (!pom_inited)
		return;

	pom_oracle_halted = 0;
	regs.halted = 0;
	regs.spcflags = 0;

	for (int i = 0; i < 8; i++) {
		regs.regs[i] = st->d[i];
		regs.regs[8 + i] = st->a[i];
	}
	regs.usp = st->usp;
	regs.isp = st->isp;
	regs.msp = st->msp;

	// Pre-sync S/M so MakeFromSR() doesn't swap the already-correct A7,
	// then let it recompute T1/T0/intmask, CCR flags and trace spcflags.
	//
	// Q2 fix: T1/T0 are ZEROED first, not left stale. MakeFromSR_x keeps
	// the architectural "an SR write that clears Tx still traces once"
	// rule (newcpu.c oldt0/oldt1 -> activate_trace()); with Tx bits left
	// over from the PREVIOUS vector it armed a one-shot SPCFLAG_DOTRACE
	// on a plain state load, producing phantom vector-9 exceptions whose
	// format-$2 frame carried the previous vector's trace_pc (seen as
	// sequence-order-dependent corpus vectors: an untraced RTS stacking
	// a trace frame). Zeroed Tx also defeats MakeFromSR's early-return,
	// so a loaded SR with T1=1 still arms SPCFLAG_TRACE for the step.
	regs.sr = st->sr;
	regs.s = (st->sr >> 13) & 1;
	regs.m = (st->sr >> 12) & 1;
	regs.t1 = regs.t0 = 0;
	regs.trace_pc = 0;
	MakeFromSR();

	regs.vbr = st->vbr;
	regs.sfc = st->sfc & 7;
	regs.dfc = st->dfc & 7;
	regs.cacr = st->cacr;
	regs.caar = st->caar;

	if (pom_fpu_model) {
	// 68882 softfloat FPU (O5): install the raw 96-bit extended registers
	// through the active backend (fpp_softfloat.c to_exten_fmovem:
	// fpx.high = wrd1 >> 16, fpx.low = wrd2:wrd3 — exactly the
	// OracleState.fp layout), then reset every FPU-internal latch so a
	// step is deterministic from this state alone.
	regs.fpu_state = 1;          // idle, not null: registers are live
	regs.fpu_exp_state = 0;
	regs.fp_exp_pend = regs.fp_unimp_pend = 0;
	regs.fp_opword = 0;
	regs.fp_ea = 0;
	regs.fp_ea_set = false;
	regs.fpu_exp_pre = false;
	regs.fp_unimp_ins = false;
	regs.fp_exception = false;
	regs.fp_branch = false;
	pom_fpu_clear_internal();    // fpp.c fsave_data (VENDOR.md patch 7)
	fpu_clearstatus();           // softfloat host status flags
	for (int i = 0; i < 8; i++)
		fpp_to_exten_fmovem(&regs.fp[i], st->fp[i][0], st->fp[i][1], st->fp[i][2]);
	fpp_set_fpcr(st->fpcr);      // masks fpcr_mask = 0xfff0 (6888x, fpp.c get_features)
	fpp_set_fpsr(st->fpsr);      // masks fpsr_mask = 0x0ffffff8
	fpp_set_fpiar(st->fpiar);
	}

	if (pom_cpu_model == 68040) {
		// 68040 MMU (Q1): registers + full re-arm — the exact sequence of
		// the upstream savestate/prefs-change paths (newcpu.c
		// prefs_changed_cpu(): mmu_reset, mmu_set_tc, mmu_set_super,
		// mmu_tt_modified; mmu_reset also flushes the ATC).
		regs.urp = st->urp040;
		regs.srp = st->srp040;
		regs.tcr = st->tc040 & 0xffff;
		regs.itt0 = st->itt0;
		regs.itt1 = st->itt1;
		regs.dtt0 = st->dtt0;
		regs.dtt1 = st->dtt1;
		regs.mmusr = st->mmusr040;
		mmu_reset();
		mmu_set_tc(regs.tcr);
		mmu_set_super(((st->sr >> 13) & 1) != 0);
		mmu_tt_modified();
		// Q2 fix: the MOVEM restart latch (cpummu.c globals) survives a
		// faulted MOVEM from the PREVIOUS vector — the next MOVEM would
		// silently reuse the stale saved EA (cpuemu_31: `if (mmu040_movem)
		// srca = mmu040_movem_ea`), another sequence-order corpus poison.
		mmu040_movem = 0;
		mmu040_movem_ea = 0;
		// Q3 fix: two more stale format-$7 frame sources. WinUAE only
		// writes regs.mmu_effective_addr on MOVEM/MOVE16 faults, so an
		// ordinary fault stacks whatever the PREVIOUS vector left there
		// (frame offset +8); mmu040_move16[] is the PD0-3 field block
		// (frame offsets +44..+59) and holds the previous vector's line
		// buffer on read faults. Zero both for deterministic frames.
		regs.mmu_effective_addr = 0;
		regs.wb2_address = 0;
		regs.wb3_data = 0;
		memset(mmu040_move16, 0, sizeof mmu040_move16);
	} else {
	// 68030 MMU: registers + full re-arm (same sequence as the upstream
	// savestate/prefs-change paths: newcpu.c prefs_changed_cpu()).
	mmu030_reset(-1);            // keeps registers, resets ATC/state, set_funcs
	crp_030 = st->crp;
	srp_030 = st->srp;
	tc_030 = st->tc;
	tt0_030 = st->tt0;
	tt1_030 = st->tt1;
	mmusr_030 = st->mmusr;
	mmu030_flush_atc_all();
	restore_mmu030_finish();     // decode TT0/TT1
	mmu030_decode_tc(tc_030, false); // TC.E=1 really enables translation
	}

	regs.stopped = st->stopped ? 1 : 0;
	if (regs.stopped)
		set_special(SPCFLAG_STOP);

	m68k_setpc_normal(st->pc);
	regs.instruction_pc = st->pc;
	regs.ir = regs.irc = 0;      // pipeline refilled by x_prefetch() at step
	mmu030_opcode_stageb = -1;
	mmu030_fake_prefetch = -1;

	// POM68K O4 slice 3: zero the instruction-restart globals that leak
	// into $A/$B bus-fault frames (access log, data buffer, disp store,
	// 040-style wb registers, prefetch pipe words). Upstream only clears
	// mmu030_state[] per instruction; the rest persists across vectors,
	// which would make fault-frame internals depend on fuzz-run history —
	// a single-step oracle must be deterministic from the initial state.
	memset(mmu030_ad, 0, sizeof mmu030_ad);
	mmu030_idx = mmu030_idx_done = 0;
	mmu030_state[0] = mmu030_state[1] = mmu030_state[2] = 0;
	mmu030_data_buffer_out = 0;
	mmu030_disp_store[0] = mmu030_disp_store[1] = 0;
	mmu030_fmovem_store[0] = mmu030_fmovem_store[1] = 0;
	mm030_stageb_address = 0;
	regs.wb2_address = 0;
	regs.wb2_status = regs.wb3_status = 0;
	regs.wb3_data = 0;
	regs.mmu_fault_addr = 0;
	regs.mmu_ssw = 0;
	regs.prefetch020[0] = regs.prefetch020[1] = regs.prefetch020[2] = 0;
	regs.prefetch020_valid[0] = regs.prefetch020_valid[1] = regs.prefetch020_valid[2] = 0;
	regs.pipeline_pos = regs.pipeline_stop = 0;
	regs.pipeline_r8[0] = regs.pipeline_r8[1] = 0;
}

ORACLE_EXPORT int32_t oracle_step(void)
{
	if (!pom_inited)
		return -2;

	pom_oracle_cycles = 0;
	pom_oracle_halted = 0;
	bQuitProgram = false;
	quit_program = 0;
	set_special(SPCFLAG_BRK);

	m68k_go(1);

	if (pom_oracle_halted || regs.halted)
		return -1;

	int64_t c = pom_oracle_cycles;
	if (c < 1)
		c = 1;
	if (c > 0x7fffffff)
		c = 0x7fffffff;
	return (int32_t)c;
}

ORACLE_EXPORT void oracle_get_state(OracleState *st)
{
	memset(st, 0, sizeof *st);
	if (!pom_inited)
		return;

	MakeSR();

	for (int i = 0; i < 8; i++) {
		st->d[i] = regs.regs[i];
		st->a[i] = regs.regs[8 + i];
	}
	// regs.regs[15] is the active stack pointer: mirror it into the
	// architectural register selected by SR (S/M), like MOVEC would see.
	st->usp = regs.usp;
	st->isp = regs.isp;
	st->msp = regs.msp;
	if (regs.s) {
		if (regs.m)
			st->msp = regs.regs[15];
		else
			st->isp = regs.regs[15];
	} else {
		st->usp = regs.regs[15];
	}

	st->pc = m68k_getpc();
	st->sr = regs.sr;
	st->vbr = regs.vbr;
	st->sfc = regs.sfc;
	st->dfc = regs.dfc;
	st->cacr = regs.cacr;
	st->caar = regs.caar;

	if (pom_cpu_model == 68040) {
		st->urp040 = regs.urp;
		st->srp040 = regs.srp;
		st->tc040 = regs.tcr & 0xffff;
		st->itt0 = regs.itt0;
		st->itt1 = regs.itt1;
		st->dtt0 = regs.dtt0;
		st->dtt1 = regs.dtt1;
		st->mmusr040 = regs.mmusr;
	} else {
		st->crp = crp_030;
		st->srp = srp_030;
		st->tc = tc_030;
		st->tt0 = tt0_030;
		st->tt1 = tt1_030;
		st->mmusr = mmusr_030;
	}

	if (pom_fpu_model) {
	// 68882 FPU (O5): raw extended out via the softfloat backend
	// (fpp_softfloat.c from_exten_fmovem, inverse of set_state).
	for (int i = 0; i < 8; i++)
		fpp_from_exten_fmovem(&regs.fp[i], &st->fp[i][0], &st->fp[i][1], &st->fp[i][2]);
	st->fpcr = regs.fpcr;
	st->fpsr = fpp_get_fpsr();   // plain regs.fpsr in this non-JIT build
	st->fpiar = regs.fpiar;
	}

	st->stopped = regs.stopped ? 1 : 0;
}
