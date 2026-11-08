# Bare-Moira CPU gates: each links `moira` (and softfloat) against a flat RAM,
# with no Macintosh object, no ROM and no JIT engine. They are the part of the
# CPU proof that can travel with the core (TODO.md § Moteur › Socle). Included
# from Pom68kJitGates.cmake at the position these registrations always had.

# M1-M3 gate (docs/CACHE_040.md): 68040 cache geometry and line data,
# CACR/CM/TTR policy, copyback/CPUSH/CINV, snooping and hit/fill timing.
add_executable(cache040_test tests/cache040_test.cpp)
target_link_libraries(cache040_test PRIVATE moira)
add_test(NAME cache040_test COMMAND cache040_test)

# O5: 68882 smoke, format-$9 IRQ + BUSY resume, detached F-line.
# Differential coverage lives in sst68030's FPU cells.
add_executable(fpu_sanity tests/fpu_sanity.cpp)
target_link_libraries(fpu_sanity PRIVATE moira)
add_test(NAME fpu_sanity COMMAND fpu_sanity)

add_executable(fpu040_test tests/fpu040_test.cpp)
target_link_libraries(fpu040_test PRIVATE moira)
add_test(NAME fpu040_test COMMAND fpu040_test)

add_executable(callm_rtm_test tests/callm_rtm_test.cpp)
target_link_libraries(callm_rtm_test PRIVATE moira)
add_test(NAME callm_rtm_test COMMAND callm_rtm_test)

# Rules no corpus reaches: e.g. DIVS.W of $80000000 on six models.
add_executable(moira_rules_test tests/moira_rules_test.cpp)
target_link_libraries(moira_rules_test PRIVATE moira)
add_test(NAME moira_rules_test COMMAND moira_rules_test)

# Instruction-boundary CPU state, pending FPU result and PMMU retry.
add_executable(moira_state_test tests/moira_state_test.cpp)
target_link_libraries(moira_state_test PRIVATE moira)
add_test(NAME moira_state_test COMMAND moira_state_test)

# The 68030 against WinUAE alone, all fourteen cells: the pinned sst68030
# keeps, in several cells, only the vectors a retired second oracle agreed
# on, and has no mmu_tt or random_tt cell.
set(POM68K_SST030_SOLO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/sst68030_solo"
    CACHE PATH "Directory of WinUAE-solo SST030 vectors (all cells)")
add_test(NAME sst68030_solo COMMAND sst68030 "${POM68K_SST030_SOLO_DIR}")

# The raw F-line window on the 68030 + 68882, against the oracle in its
# strict 6888x setting. The vectors of rulings D23b and D26-D29 are listed by
# name (tests/SstKnown.h).
set(POM68K_SST030_FPURAW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/sst68030_fpuraw"
    CACHE PATH "Directory of strict-oracle SST030 vectors, raw F-line window")
add_test(NAME sst68030_fpuraw
         COMMAND sst68030 "${POM68K_SST030_FPURAW_DIR}"
                 --known "${CMAKE_CURRENT_SOURCE_DIR}/tests/sst68030_fpuraw.known")

# Two instructions per vector on the 68030 + 68882, strict oracle: the
# posted-exception model of the 6888x across an instruction boundary.
set(POM68K_SST030_FPU2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/sst68030_fpu2"
    CACHE PATH "Directory of two-instruction SST030 vectors, strict oracle")
add_test(NAME sst68030_fpu2
         COMMAND sst68030 "${POM68K_SST030_FPU2_DIR}"
                 --known "${CMAKE_CURRENT_SOURCE_DIR}/tests/sst68030_fpu2.known")

# Write faults on the last access of read-modify-write instructions, 68030:
# the short frame $A, the next instruction's PC, the flags already final.
set(POM68K_SST030_RMW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/sst68030_rmw"
    CACHE PATH "Directory of SST030 vectors, write faults on memory destinations")
add_test(NAME sst68030_rmw
         COMMAND sst68030 "${POM68K_SST030_RMW_DIR}"
                 --known "${CMAKE_CURRENT_SOURCE_DIR}/tests/sst68030_rmw.known")

# The 68040 with 8 KB pages (TC.P = 1): the cell the pinned corpus never had.
# Same harness as sst68040, its own directory so that a host without these
# files soft-skips instead of failing the corpus manifest.
set(POM68K_SST040_8K_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/sst68040_8k"
    CACHE PATH "Directory of oracle-generated SST040 vectors with 8 KB pages")
add_test(NAME sst68040_8k COMMAND sst68040 "${POM68K_SST040_8K_DIR}")

# The raw F-line window on the FPU-less 68LC040: which garbage takes a plain
# Line-F, which the illegal vector and which the "no FPU" format $4 frame.
set(POM68K_SST040_FLINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/sst68040_fline"
    CACHE PATH "Directory of oracle-generated SST040 vectors, raw F-line window, no FPU")
add_test(NAME sst68040_fline COMMAND sst68040 "${POM68K_SST040_FLINE_DIR}")

# The full 68040 with its integrated FPU, differentially. The vectors of the
# open rulings D24-D27 are listed by name: one of them passing, or missing,
# fails the gate as surely as an unlisted divergence.
set(POM68K_SST040_FPU_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/sst68040_fpu"
    CACHE PATH "Directory of oracle-generated SST040 vectors, integrated FPU")
add_test(NAME sst68040_fpu
         COMMAND sst68040 "${POM68K_SST040_FPU_DIR}" --fpu
                 --known "${CMAKE_CURRENT_SOURCE_DIR}/tests/sst68040_fpu.known")

# Two instructions per vector on the full 68040: the first posts an FPU
# exception, the second decides when it is delivered (ruling D24).
set(POM68K_SST040_FPU2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/sst68040_fpu2"
    CACHE PATH "Directory of two-instruction SST040 vectors, integrated FPU")
add_test(NAME sst68040_fpu2
         COMMAND sst68040 "${POM68K_SST040_FPU2_DIR}" --fpu
                 --known "${CMAKE_CURRENT_SOURCE_DIR}/tests/sst68040_fpu2.known")

# Three steps per vector on the full 68040, FSAVE as the first word of the
# FPU exception handlers: the BUSY and UNIMP state frames the FPSP reads.
set(POM68K_SST040_FPUH_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/sst68040_fpuh"
    CACHE PATH "Directory of three-step SST040 vectors, FSAVE in the FPU handlers")
add_test(NAME sst68040_fpuh
         COMMAND sst68040 "${POM68K_SST040_FPUH_DIR}" --fpu
                 --known "${CMAKE_CURRENT_SOURCE_DIR}/tests/sst68040_fpuh.known")

# Evidence grades in the core (CLAUDE.md § Evidence grades): every comment
# block of extern/moira/Moira that cites WinUAE or SingleStepTests carries
# its own `[D]` revision (pinned oracle tree / corpus lock and fetcher).
# Every WinUAE file:line exists in the oracle tree; checker negative
# controls run before scanning the core. Repository-only, no asset.
add_test(NAME core_grades_test
         COMMAND python3 tests/core_grades_test.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

# Corpus identity: present files must match tests/sst_corpora.lock; absent ones skip.
add_test(NAME sst_corpora_lock_test
         COMMAND python3 tools/verify_sst_corpora.py
                 --dir sst68000=${POM68K_SST_DIR}
                 --dir sst68030=${POM68K_SST030_DIR}
                 --dir sst68030_solo=${POM68K_SST030_SOLO_DIR}
                 --dir sst68030_fpuraw=${POM68K_SST030_FPURAW_DIR}
                 --dir sst68030_fpu2=${POM68K_SST030_FPU2_DIR}
                 --dir sst68030_rmw=${POM68K_SST030_RMW_DIR}
                 --dir sst68040=${POM68K_SST040_DIR}
                 --dir sst68040_8k=${POM68K_SST040_8K_DIR}
                 --dir sst68040_fline=${POM68K_SST040_FLINE_DIR}
                 --dir sst68040_fpu=${POM68K_SST040_FPU_DIR}
                 --dir sst68040_fpu2=${POM68K_SST040_FPU2_DIR}
                 --dir sst68040_fpuh=${POM68K_SST040_FPUH_DIR}
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
