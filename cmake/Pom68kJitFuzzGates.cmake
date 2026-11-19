# Program-fuzzer gates (tests/jit_program_fuzz.cpp): random multi-instruction
# programs, interpreter against translator. Included from Pom68kJitGates.cmake
# at the position these registrations always had; split out on 2026-11-19
# when the trace gates took that file past its size ceiling.
# Random multi-instruction programs, interpreter against translator
# (tests/jit_program_fuzz.cpp): blocks, links, loop laps, random slice
# boundaries, exceptions inside generated code and self-modifying stores,
# none of which a one-instruction SST vector reaches. The gates run a fixed
# seed range plus the seeds that found the 2026-11-18 defects (trace that
# rewrites its own footprint, TST #imm sign bit on a64, the link-table
# empty tag, JSR to an odd target, the a64 exact-thunk store past a tripped
# guard). The 68030 arms disable the a64 profit
# score: under it a64 compiles almost none of this code (0.8 % native), and
# the gate would judge the interpreter against itself.
add_executable(jit_program_fuzz tests/jit_program_fuzz.cpp)
target_link_libraries(jit_program_fuzz PRIVATE pom68k_core)
add_test(NAME jit_program_fuzz_040_test
         COMMAND jit_program_fuzz --model 40 --seeds 400
                 --extra 1072,3476,19257,21450)
add_test(NAME jit_program_fuzz_030_test
         COMMAND jit_program_fuzz --model 30 --seeds 400)
# --smc points four address registers into the program: stores rewrite
# translated code constantly. Seed 1324 (68030) is the a64 exact-thunk
# store that ran on past a tripped write guard.
add_test(NAME jit_program_fuzz_040_smc_test
         COMMAND jit_program_fuzz --model 40 --smc --seeds 300)
add_test(NAME jit_program_fuzz_030_smc_test
         COMMAND jit_program_fuzz --model 30 --smc --seeds 300 --extra 1324)
# --ext --fpu --irq-timed: calls/returns, traps, CAS/CMP2/MOVEP/MOVE16,
# CACR cache enables, the FPU (68040 integrated, 68882 on the 68030) and
# interrupt lines raised at guest-clock deadlines inside generated blocks
# through Engine::setPeriphDeadline, as the board wrappers do; --savestate
# continues the translated side on a fresh core and engine reloaded through
# Moira::pomVisitState at random slice boundaries.
add_test(NAME jit_program_fuzz_040_ext_test
         COMMAND jit_program_fuzz --model 40 --ext --fpu --irq-timed --savestate
                 --slices 20 --seeds 250)
add_test(NAME jit_program_fuzz_030_ext_test
         COMMAND jit_program_fuzz --model 30 --ext --fpu --irq-timed --savestate
                 --slices 20 --seeds 250)
# --dcache: the opt-in architectural 68040 caches (POM68K_040_DCACHE), both
# on from the first instruction; the comparison adds every cache line.
add_test(NAME jit_program_fuzz_040_dcache_test
         COMMAND jit_program_fuzz --model 40 --dcache --ext --smc --savestate
                 --slices 12 --seeds 250)
# The same without the board wrappers' JIT drop on CACR writes: the engine
# must notice a CACR change itself (Flush::Cache040Mode).
add_test(NAME jit_program_fuzz_040_dcache_cacr_test
         COMMAND jit_program_fuzz --model 40 --dcache --ext --fpu --smc
                 --irq-timed --savestate --slices 20 --seeds 200
                 --extra 4863,6822,8743)
# …and under translation, where a MOVEC clearing DE left native cache-line
# entries published (Moira::setCACR bumps them since 2026-11-18).
add_test(NAME jit_program_fuzz_040_mmu_dcache_test
         COMMAND jit_program_fuzz --model 40 --mmu --dcache --ext --smc
                 --savestate --slices 12 --seeds 200 --extra 959)
set_tests_properties(jit_program_fuzz_040_dcache_cacr_test
                     jit_program_fuzz_040_mmu_dcache_test PROPERTIES
                     ENVIRONMENT "FUZZ_CACR_FLUSH=0")
# --sys: SR writes into user mode, T1 tracing and the master bit, USP
# moves, RTE through a built frame, STOP. Seed 2788 is a user-mode trace
# that rewrote its own opcode and ended in a supervisor exception.
add_test(NAME jit_program_fuzz_040_sys_test
         COMMAND jit_program_fuzz --model 40 --sys --ext --fpu --smc
                 --seeds 300 --extra 2788)
add_test(NAME jit_program_fuzz_030_sys_test
         COMMAND jit_program_fuzz --model 30 --sys --mmu --ext --fpu --smc
                 --irq-timed --savestate --slices 20 --seeds 250)
# Seed 3499: a JSR push fault with no stack left for the frame halts the
# core; the halted queue must be the one the interpreter would leave.
add_test(NAME jit_program_fuzz_040_sys_mmu_test
         COMMAND jit_program_fuzz --model 40 --sys --mmu --ext --fpu --smc
                 --irq-timed --savestate --slices 20 --seeds 150 --extra 3499)
set_tests_properties(jit_program_fuzz_030_sys_test PROPERTIES
                     ENVIRONMENT "POM68K_JIT_PROFIT_SCORE=0")
set_tests_properties(jit_program_fuzz_040_sys_test jit_program_fuzz_030_sys_test
                     jit_program_fuzz_040_sys_mmu_test
                     PROPERTIES LABELS "jit;unit" TIMEOUT 300)
# --mmuprot: a seeded quarter of the data and stack pages write-protected
# (and, on the 68040, another quarter supervisor-only), so stores and
# exception frames fault inside blocks. Seeds are per option set (the
# options change the generated program). Combined: 112, 274 (data buffer
# stacked by a TAS fault), 192 (restart latches across a save state taken
# while STOPped). Plain --mmuprot: 278 (data buffer), 1072 (stale (An)±
# fixup in a JIT thunk).
add_test(NAME jit_program_fuzz_030_mmuprot_test
         COMMAND jit_program_fuzz --model 30 --mmuprot --sys --ext --fpu --smc
                 --irq-timed --savestate --slices 20 --seeds 200 --extra 112,274,192)
add_test(NAME jit_program_fuzz_030_mmuprot_plain_test
         COMMAND jit_program_fuzz --model 30 --mmuprot --seeds 100 --extra 278,1072)
add_test(NAME jit_program_fuzz_040_mmuprot_test
         COMMAND jit_program_fuzz --model 40 --mmuprot --sys --ext --fpu --smc
                 --irq-timed --savestate --slices 20 --seeds 200)
# 68040 seed 807209 (--mmuprot without --irq-timed/--savestate): a split
# CAS.L faulting on its locked read stacked a stale WB3D.
add_test(NAME jit_program_fuzz_040_mmuprot_cas_test
         COMMAND jit_program_fuzz --model 40 --mmuprot --sys --ext --smc --fpu
                 --seeds 100 --extra 807209)
# The 68020 on the threaded backend with timed interrupts at every level,
# 7 included: a held level 7 used to wedge the interpreter at constant
# clock (seed 3) until level 7 became transition sensitive (Moira.h,
# nmiLatch). Window mode and blocks.
add_test(NAME jit_program_fuzz_020_irq7_test
         COMMAND jit_program_fuzz --model 20 --irq-timed --ext --sys --seeds 300 --extra 3)
add_test(NAME jit_program_fuzz_020_irq7_blocks_test
         COMMAND jit_program_fuzz --model 20 --irq-timed --ext --sys --seeds 300 --extra 3)
set_tests_properties(jit_program_fuzz_020_irq7_test PROPERTIES
                     ENVIRONMENT "POM68K_CPU_ENGINE=jit;POM68K_JIT_BACKEND=threaded")
set_tests_properties(jit_program_fuzz_020_irq7_blocks_test PROPERTIES
                     ENVIRONMENT "POM68K_CPU_ENGINE=jit;POM68K_JIT_BACKEND=threaded;POM68K_JIT_BLOCKS=1;POM68K_JIT_HOT=1")
set_tests_properties(jit_program_fuzz_020_irq7_test jit_program_fuzz_020_irq7_blocks_test
                     PROPERTIES LABELS "jit;unit" TIMEOUT 300)
# --ext2: CAS2, MOVES (SFC/DFC loaded), TAS, 68040 CINV/CPUSH and, under
# --mmu, PFLUSH/PTEST — the locked, alternate-space and cache/ATC
# maintenance accesses --ext leaves out.
add_test(NAME jit_program_fuzz_030_ext2_test
         COMMAND jit_program_fuzz --model 30 --ext2 --ext --sys --smc --mmu
                 --irq-timed --savestate --slices 20 --seeds 150)
add_test(NAME jit_program_fuzz_040_ext2_test
         COMMAND jit_program_fuzz --model 40 --ext2 --ext --sys --smc --mmu --dcache
                 --irq-timed --savestate --slices 20 --seeds 150)
add_test(NAME jit_program_fuzz_020_ext2_test
         COMMAND jit_program_fuzz --model 20 --ext2 --ext --sys --smc --irq-timed
                 --savestate --slices 20 --seeds 200)
set_tests_properties(jit_program_fuzz_030_ext2_test PROPERTIES
                     ENVIRONMENT "POM68K_JIT_PROFIT_SCORE=0")
set_tests_properties(jit_program_fuzz_020_ext2_test PROPERTIES
                     ENVIRONMENT "POM68K_JIT_BACKEND=threaded;POM68K_JIT_BLOCKS=1;POM68K_JIT_HOT=1")
set_tests_properties(jit_program_fuzz_030_ext2_test jit_program_fuzz_040_ext2_test
                     jit_program_fuzz_020_ext2_test
                     PROPERTIES LABELS "jit;unit" TIMEOUT 300)
# The 68000 (compact Macs) on the threaded backend, the one engine valid
# for it: window mode and blocks, with SR/USP/RTE/STOP, self-modifying
# stores, timed interrupts and save states.
add_test(NAME jit_program_fuzz_000_test
         COMMAND jit_program_fuzz --model 0 --sys --smc --irq-timed --savestate
                 --slices 20 --seeds 300)
add_test(NAME jit_program_fuzz_000_blocks_test
         COMMAND jit_program_fuzz --model 0 --sys --smc --irq-timed --savestate
                 --slices 20 --seeds 300)
set_tests_properties(jit_program_fuzz_000_test PROPERTIES
                     ENVIRONMENT "POM68K_CPU_ENGINE=jit;POM68K_JIT_BACKEND=threaded")
set_tests_properties(jit_program_fuzz_000_blocks_test PROPERTIES
                     ENVIRONMENT "POM68K_CPU_ENGINE=jit;POM68K_JIT_BACKEND=threaded;POM68K_JIT_BLOCKS=1;POM68K_JIT_HOT=1")
set_tests_properties(jit_program_fuzz_000_test jit_program_fuzz_000_blocks_test
                     PROPERTIES LABELS "jit;unit" TIMEOUT 300)
# 68040 seed 9121429 (--dcache under the MMU, timed interrupts): a native
# RTS counted its stack read's D-cache hit, found an odd return address and
# left for the slow path, whose exact replay counted it again. Native hit
# counts are now staged until the instruction retires.
add_test(NAME jit_program_fuzz_040_dcache_replay_test
         COMMAND jit_program_fuzz --model 40 --fpu --ext --ext2 --dcache --mmu
                 --irq-timed --slices 20 --len 120 --seeds 60 --extra 9121429)
set_tests_properties(jit_program_fuzz_040_dcache_replay_test PROPERTIES
                     LABELS "jit;unit" TIMEOUT 300)
# --vmrte: the virtual-memory pattern — a protection fault's handler makes
# the data space transparent and RTEs, so the faulted instruction continues
# (68030 format-$B access-log replay under RTE_REPLAY, 68040 format-$7
# write-backs and rerun), interpreter against translator.
add_test(NAME jit_program_fuzz_030_vmrte_test
         COMMAND jit_program_fuzz --model 30 --vmrte --devport --sys --ext --ext2 --smc
                 --irq-timed --slices 20 --seeds 200)
add_test(NAME jit_program_fuzz_040_vmrte_test
         COMMAND jit_program_fuzz --model 40 --vmrte --devport --sys --ext --ext2 --smc
                 --irq-timed --slices 20 --seeds 200)
set_tests_properties(jit_program_fuzz_030_vmrte_test PROPERTIES
                     ENVIRONMENT "POM68K_JIT_PROFIT_SCORE=0")
set_tests_properties(jit_program_fuzz_030_vmrte_test jit_program_fuzz_040_vmrte_test
                     PROPERTIES LABELS "jit;unit" TIMEOUT 300)
# --tracebit: SR.T1 rises at slice boundaries, wherever the program is; the
# trace handler counts and RTEs, so every instruction takes vector 9 until
# an exception or a MOVE to SR ends the run. Judges the staged trace, its
# format-$2 frame and the engine's hand-over of a traced core to execute().
add_test(NAME jit_program_fuzz_030_trace_test
         COMMAND jit_program_fuzz --model 30 --tracebit --ext --ext2 --sys --smc --mmu --fpu
                 --irq-timed --savestate --slices 20 --seeds 200)
add_test(NAME jit_program_fuzz_040_trace_test
         COMMAND jit_program_fuzz --model 40 --tracebit --ext --ext2 --sys --smc --mmu --fpu
                 --irq-timed --savestate --slices 20 --seeds 200)
add_test(NAME jit_program_fuzz_020_trace_blocks_test
         COMMAND jit_program_fuzz --model 20 --tracebit --ext --sys --smc --irq-timed
                 --savestate --slices 20 --seeds 200)
add_test(NAME jit_program_fuzz_000_trace_blocks_test
         COMMAND jit_program_fuzz --model 0 --tracebit --sys --smc --irq-timed
                 --savestate --slices 20 --seeds 200)
set_tests_properties(jit_program_fuzz_030_trace_test PROPERTIES
                     ENVIRONMENT "POM68K_JIT_PROFIT_SCORE=0")
set_tests_properties(jit_program_fuzz_020_trace_blocks_test jit_program_fuzz_000_trace_blocks_test
                     PROPERTIES
                     ENVIRONMENT "POM68K_CPU_ENGINE=jit;POM68K_JIT_BACKEND=threaded;POM68K_JIT_BLOCKS=1;POM68K_JIT_HOT=1")
set_tests_properties(jit_program_fuzz_030_trace_test jit_program_fuzz_040_trace_test
                     jit_program_fuzz_020_trace_blocks_test jit_program_fuzz_000_trace_blocks_test
                     PROPERTIES LABELS "jit;unit" TIMEOUT 300)
# --devport: a 4 KB page whose every read advances a counter, refused to
# the direct views as a board's I/O is. A read performed once too often on
# either side shows. Seed 50005621: a stack moved onto the port, and the
# native RTS read its return address through an exact thunk before the
# odd-target bail, whose replay read it again.
add_test(NAME jit_program_fuzz_030_devport_test
         COMMAND jit_program_fuzz --model 30 --devport --ext --ext2 --sys --irq-timed
                 --savestate --slices 20 --seeds 200 --extra 50005621)
add_test(NAME jit_program_fuzz_040_devport_test
         COMMAND jit_program_fuzz --model 40 --devport --ext --ext2 --sys --irq-timed
                 --savestate --slices 20 --seeds 200 --extra 50005621)
set_tests_properties(jit_program_fuzz_030_devport_test PROPERTIES
                     ENVIRONMENT "POM68K_JIT_PROFIT_SCORE=0")
set_tests_properties(jit_program_fuzz_030_devport_test jit_program_fuzz_040_devport_test
                     PROPERTIES LABELS "jit;unit" TIMEOUT 300)
# --lc040: the Quadra 605 / LC 475 CPU with the FPU templates on, so every
# FPU instruction takes the 68LC040 F-line (vector 11, format $4) inside
# blocks, under the MMU and across save states.
add_test(NAME jit_program_fuzz_lc040_test
         COMMAND jit_program_fuzz --model 40 --lc040 --fpu --fpexc --mmuprot --sys --ext
                 --smc --irq-timed --savestate --slices 20 --seeds 200)
set_tests_properties(jit_program_fuzz_lc040_test PROPERTIES LABELS "jit;unit" TIMEOUT 300)
# --mmupage: a seeded page size (68030 256 B to 32 KB, 68040 4 or 8 KB),
# so DTLB slices, refusals and code-window bounds meet pages other than
# 4 KB.
add_test(NAME jit_program_fuzz_030_mmupage_test
         COMMAND jit_program_fuzz --model 30 --mmupage --mmuprot --mmutt --sys --ext
                 --smc --fpu --irq-timed --savestate --slices 20 --seeds 150)
add_test(NAME jit_program_fuzz_040_mmupage_test
         COMMAND jit_program_fuzz --model 40 --mmupage --mmuprot --mmutt --sys --ext
                 --smc --fpu --irq-timed --savestate --slices 20 --seeds 150)
set_tests_properties(jit_program_fuzz_030_mmupage_test PROPERTIES
                     ENVIRONMENT "POM68K_JIT_PROFIT_SCORE=0")
set_tests_properties(jit_program_fuzz_040_mmuprot_cas_test jit_program_fuzz_030_mmupage_test
                     jit_program_fuzz_040_mmupage_test
                     PROPERTIES LABELS "jit;unit" TIMEOUT 300)
# --mmutt: seeded transparent-translation registers over the whole bus
# (68030 TTx direction-specific, 68040 DTTx/ITTx with S, W and CM). 68030
# seeds 10, 508, 516 (TT0/TT1 writes only, user or supervisor data): a
# write-only TT made a native RMW's read transparent too.
add_test(NAME jit_program_fuzz_030_mmutt_test
         COMMAND jit_program_fuzz --model 30 --mmutt --mmuprot --sys --ext --smc
                 --seeds 200 --extra 10,508,516)
add_test(NAME jit_program_fuzz_040_mmutt_test
         COMMAND jit_program_fuzz --model 40 --mmutt --mmuprot --sys --ext --smc
                 --dcache --seeds 200)
set_tests_properties(jit_program_fuzz_030_mmuprot_test jit_program_fuzz_030_mmutt_test
                     jit_program_fuzz_030_mmuprot_plain_test
                     PROPERTIES ENVIRONMENT "POM68K_JIT_PROFIT_SCORE=0")
set_tests_properties(jit_program_fuzz_030_mmuprot_test jit_program_fuzz_040_mmuprot_test
                     jit_program_fuzz_030_mmutt_test jit_program_fuzz_040_mmutt_test
                     jit_program_fuzz_030_mmuprot_plain_test
                     PROPERTIES LABELS "jit;unit" TIMEOUT 300)
# --fpexc: FPCR also arms the FP exception enables, so posted 6888x
# exceptions and 68040 FP frames (vectors 48-55) occur inside blocks, across
# interrupts and save states.
add_test(NAME jit_program_fuzz_040_fpexc_test
         COMMAND jit_program_fuzz --model 40 --fpexc --savestate --irq-timed
                 --slices 20 --seeds 200)
add_test(NAME jit_program_fuzz_030_fpexc_test
         COMMAND jit_program_fuzz --model 30 --fpexc --savestate --irq-timed
                 --slices 20 --seeds 200)
set_tests_properties(jit_program_fuzz_030_fpexc_test PROPERTIES
                     ENVIRONMENT "POM68K_JIT_PROFIT_SCORE=0")
set_tests_properties(jit_program_fuzz_040_fpexc_test jit_program_fuzz_030_fpexc_test
                     PROPERTIES LABELS "jit;unit" TIMEOUT 300)
set_tests_properties(jit_program_fuzz_040_test jit_program_fuzz_030_test
                     jit_program_fuzz_040_smc_test jit_program_fuzz_030_smc_test
                     jit_program_fuzz_040_ext_test jit_program_fuzz_030_ext_test
                     jit_program_fuzz_040_dcache_test
                     jit_program_fuzz_040_dcache_cacr_test
                     jit_program_fuzz_040_mmu_dcache_test
                     PROPERTIES LABELS "jit;unit" TIMEOUT 300)
set_tests_properties(jit_program_fuzz_030_test jit_program_fuzz_030_smc_test
                     jit_program_fuzz_030_ext_test
                     PROPERTIES ENVIRONMENT "POM68K_JIT_PROFIT_SCORE=0")
