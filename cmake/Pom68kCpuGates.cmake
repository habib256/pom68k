# Bare-Moira CPU gates: each links `moira` (and softfloat) against a flat RAM,
# with no Macintosh object, no ROM and no JIT engine. They are the part of the
# CPU proof that can travel with the core (TODO.md § Moteur › Socle). Included
# from Pom68kJitGates.cmake at the position these registrations always had.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Pom68kCorpusGates.cmake)
add_executable(softfloat_primitives_test tests/softfloat_primitives_test.c)
target_link_libraries(softfloat_primitives_test PRIVATE softfloat68k)
add_test(NAME softfloat_primitives_test COMMAND softfloat_primitives_test)
set_tests_properties(softfloat_primitives_test PROPERTIES LABELS cpu-vectors)

if(NOT EMSCRIPTEN)
    find_package(Threads REQUIRED)
    set(POM68K_CPU_CONTEXT_THREADS Threads::Threads)
endif()
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Pom68kInterpGates.cmake)
foreach(cpu_gate IN ITEMS
    cache040_test       # cache data, policy, physical management and snooping
    fpu_sanity          # external FPU, format-$9 IRQ and BUSY resume
    fpu040_test         # integrated FPU and FPSP exception frames
    fpu_context_test    # independent arithmetic state under nested/parallel CPUs
    callm_rtm_test      # 68020 module protocol
    berr020_test        # 020/EC020 software-completed format-$B cycles
    moira_rules_test    # hardware-rule cells outside random corpora
    moira_state_test    # instruction-boundary PMMU/FPU continuations
    mmu040_atc_test)    # multi-instruction I/D and FC2 translations
    add_executable(${cpu_gate} tests/${cpu_gate}.cpp)
    target_link_libraries(${cpu_gate} PRIVATE moira)
    add_test(NAME ${cpu_gate} COMMAND ${cpu_gate})
endforeach()
target_link_libraries(fpu_context_test PRIVATE ${POM68K_CPU_CONTEXT_THREADS})
# cache040_test's fault matrices take ~20 s in Release but 243 s under
# ASan/UBSan on the M4 (2026-11-10), and the Linux nightly leg (GCC ASan,
# `-j nproc`) timed out at the 600 s default (run 42802857141). 1800 is the
# instrumented cost with margin, as for jit_asset_free_lockstep_test; the
# plain tiers finish it in ~20 s.
set_tests_properties(cache040_test PROPERTIES TIMEOUT 1800)
# GCC ASan on the nightly runner needs ~2 400 s (run 43222539870; 246 s
# under AppleClang ASan on the M4). Instrumented builds only.
if(POM68K_SANITIZE)
    set_tests_properties(cache040_test PROPERTIES TIMEOUT 3600)
endif()

# The 68030 against WinUAE alone, all fourteen cells: the pinned sst68030
# keeps, in several cells, only the vectors a retired second oracle agreed
# on, and has no mmu_tt or random_tt cell.
set(POM68K_SST030_SOLO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/sst68030_solo"
    CACHE PATH "Directory of WinUAE-solo SST030 vectors (all cells)")
add_test(NAME sst68030_solo COMMAND sst68030 "${POM68K_SST030_SOLO_DIR}" --strict-ram)

# The raw F-line window on the 68030 + 68882, against the oracle in its
# strict 6888x setting. The vectors of rulings D23b and D26-D29 are listed by
# name (tests/SstKnown.h).
set(POM68K_SST030_FPURAW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/sst68030_fpuraw"
    CACHE PATH "Directory of strict-oracle SST030 vectors, raw F-line window")
add_test(NAME sst68030_fpuraw
         COMMAND sst68030 "${POM68K_SST030_FPURAW_DIR}"
                 --known "${CMAKE_CURRENT_SOURCE_DIR}/tests/sst68030_fpuraw.known"
                 --strict-ram)

# Two instructions per vector on the 68030 + 68882, strict oracle: the
# posted-exception model of the 6888x across an instruction boundary.
set(POM68K_SST030_FPU2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/sst68030_fpu2"
    CACHE PATH "Directory of two-instruction SST030 vectors, strict oracle")
add_test(NAME sst68030_fpu2
         COMMAND sst68030 "${POM68K_SST030_FPU2_DIR}"
                 --known "${CMAKE_CURRENT_SOURCE_DIR}/tests/sst68030_fpu2.known"
                 --strict-ram)

# Three steps per vector on the 68030 + 68882, strict oracle: FSAVE planted
# at the head of the FPU exception handlers puts the IDLE frame in the vector.
set(POM68K_SST030_FPUH_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/sst68030_fpuh"
    CACHE PATH "Directory of SST030 vectors whose FPU handlers FSAVE, strict oracle")
add_test(NAME sst68030_fpuh
         COMMAND sst68030 "${POM68K_SST030_FPUH_DIR}" --strict-ram)

# Write faults on the last access of read-modify-write instructions, 68030:
# the short frame $A, the next instruction's PC, the flags already final.
set(POM68K_SST030_RMW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/sst68030_rmw"
    CACHE PATH "Directory of SST030 vectors, write faults on memory destinations")
add_test(NAME sst68030_rmw
         COMMAND sst68030 "${POM68K_SST030_RMW_DIR}"
                 --known "${CMAKE_CURRENT_SOURCE_DIR}/tests/sst68030_rmw.known"
                 --strict-ram)

# The 68040 with 8 KB pages (TC.P = 1): the cell the pinned corpus never had.
# Same harness as sst68040, its own directory so that a host without these
# files soft-skips instead of failing the corpus manifest.
set(POM68K_SST040_8K_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/sst68040_8k"
    CACHE PATH "Directory of oracle-generated SST040 vectors with 8 KB pages")
add_test(NAME sst68040_8k COMMAND sst68040 "${POM68K_SST040_8K_DIR}"
         --known "${CMAKE_CURRENT_SOURCE_DIR}/tests/sst68040_8k.known" --strict-ram)

# The raw F-line window on the FPU-less 68LC040: which garbage takes a plain
# Line-F, which the illegal vector and which the "no FPU" format $4 frame.
set(POM68K_SST040_FLINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/sst68040_fline"
    CACHE PATH "Directory of oracle-generated SST040 vectors, raw F-line window, no FPU")
add_test(NAME sst68040_fline COMMAND sst68040 "${POM68K_SST040_FLINE_DIR}" --strict-ram)

# The full 68040 with its integrated FPU, differentially. The vectors of the
# open rulings D24-D27 are listed by name: one of them passing, or missing,
# fails the gate as surely as an unlisted divergence.
set(POM68K_SST040_FPU_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/sst68040_fpu"
    CACHE PATH "Directory of oracle-generated SST040 vectors, integrated FPU")
add_test(NAME sst68040_fpu
         COMMAND sst68040 "${POM68K_SST040_FPU_DIR}" --fpu
                 --known "${CMAKE_CURRENT_SOURCE_DIR}/tests/sst68040_fpu.known"
                 --strict-ram)

# Two instructions per vector on the full 68040: the first posts an FPU
# exception, the second decides when it is delivered (ruling D24).
set(POM68K_SST040_FPU2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/sst68040_fpu2"
    CACHE PATH "Directory of two-instruction SST040 vectors, integrated FPU")
add_test(NAME sst68040_fpu2
         COMMAND sst68040 "${POM68K_SST040_FPU2_DIR}" --fpu
                 --known "${CMAKE_CURRENT_SOURCE_DIR}/tests/sst68040_fpu2.known"
                 --strict-ram)

# Three steps per vector on the full 68040, FSAVE as the first word of the
# FPU exception handlers: the BUSY and UNIMP state frames the FPSP reads.
set(POM68K_SST040_FPUH_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/sst68040_fpuh"
    CACHE PATH "Directory of three-step SST040 vectors, FSAVE in the FPU handlers")
add_test(NAME sst68040_fpuh
         COMMAND sst68040 "${POM68K_SST040_FPUH_DIR}" --fpu
                 --known "${CMAKE_CURRENT_SOURCE_DIR}/tests/sst68040_fpuh.known"
                 --strict-ram)

# Evidence grades: pinned emulator/corpus revisions and checked manual editions.
# Manual B/F headers use tests/core_manuals.lock and reviewed locators;
# unknown claims name a witness. A different source's grade is insufficient.
# WinUAE file:line ranges are checked in the oracle tree; negative controls
# run before scanning the core. Repository-only: PDF payloads are optional.
add_test(NAME core_grades_test
         COMMAND python3 tests/core_grades_test.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
# The standalone Moira/JIT release builds without POM68K: its include closure.
add_test(NAME moira_release_test COMMAND python3 tools/moira_release.py --check
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

# Corpus identity: present files must match tests/sst_corpora.lock; absent ones skip.
add_test(NAME sst_corpora_lock_test
         COMMAND python3 tools/verify_sst_corpora.py
                 --dir sst68000=${POM68K_SST_DIR}
                 --dir sst68030=${POM68K_SST030_DIR}
                 --dir sst68030_solo=${POM68K_SST030_SOLO_DIR}
                 --dir sst68030_fpuraw=${POM68K_SST030_FPURAW_DIR}
                 --dir sst68030_fpu2=${POM68K_SST030_FPU2_DIR}
                 --dir sst68030_fpuh=${POM68K_SST030_FPUH_DIR}
                 --dir sst68030_rmw=${POM68K_SST030_RMW_DIR}
                 --dir sst68040=${POM68K_SST040_DIR}
                 --dir sst68040_8k=${POM68K_SST040_8K_DIR}
                 --dir sst68040_fline=${POM68K_SST040_FLINE_DIR}
                 --dir sst68040_fpu=${POM68K_SST040_FPU_DIR}
                 --dir sst68040_fpu2=${POM68K_SST040_FPU2_DIR}
                 --dir sst68040_fpuh=${POM68K_SST040_FPUH_DIR}
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

# The oracle corpora replayed through the translator (tests/SstJitLockstep.h):
# each vector's initial state run to the same clock by the interpreter and by
# jit::Engine, every register, the clock and the whole bus compared, after a
# warm-up pass so that the vector's own instruction runs as generated code.
# The plain sst gates judge the interpreter against WinUAE; these judge the
# translator against the interpreter on the same states (TODO.md § Moteur ›
# Socle). They soft-skip without the optional corpus directories.
add_executable(sst68040_jit tests/sst68040.cpp)
target_compile_definitions(sst68040_jit PRIVATE SST_JIT=1)
target_link_libraries(sst68040_jit PRIVATE pom68k_core)
add_executable(sst68030_jit tests/sst68030.cpp)
target_compile_definitions(sst68030_jit PRIVATE SST_JIT=1)
target_link_libraries(sst68030_jit PRIVATE pom68k_core)
add_test(NAME jit_sst68040 COMMAND sst68040_jit "${POM68K_SST040_DIR}")
add_test(NAME jit_sst68040_8k COMMAND sst68040_jit "${POM68K_SST040_8K_DIR}")
add_test(NAME jit_sst68040_fline COMMAND sst68040_jit "${POM68K_SST040_FLINE_DIR}")
add_test(NAME jit_sst68040_fpu COMMAND sst68040_jit "${POM68K_SST040_FPU_DIR}" --fpu)
add_test(NAME jit_sst68040_fpu2 COMMAND sst68040_jit "${POM68K_SST040_FPU2_DIR}" --fpu)
add_test(NAME jit_sst68040_fpuh COMMAND sst68040_jit "${POM68K_SST040_FPUH_DIR}" --fpu)
add_test(NAME jit_sst68030 COMMAND sst68030_jit "${POM68K_SST030_DIR}")
add_test(NAME jit_sst68030_solo COMMAND sst68030_jit "${POM68K_SST030_SOLO_DIR}")
add_test(NAME jit_sst68030_fpuraw COMMAND sst68030_jit "${POM68K_SST030_FPURAW_DIR}")
add_test(NAME jit_sst68030_fpu2 COMMAND sst68030_jit "${POM68K_SST030_FPU2_DIR}")
add_test(NAME jit_sst68030_fpuh COMMAND sst68030_jit "${POM68K_SST030_FPUH_DIR}")
add_test(NAME jit_sst68030_rmw COMMAND sst68030_jit "${POM68K_SST030_RMW_DIR}")
# The 336 public vectors (tests/cpu_vectors/) through the translator: the
# same replay with no optional data, so CI exercises it on every host.
foreach(model IN ITEMS 030 040)
    add_test(NAME cpu_vectors${model}_jit_test
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/cpu_vectors.py replay
            --model ${model} --runner $<TARGET_FILE:sst68${model}_jit> --jit
            --emulator "${CMAKE_CROSSCOMPILING_EMULATOR}")
    set_tests_properties(cpu_vectors${model}_jit_test PROPERTIES
        LABELS "cpu-vectors;jit" TIMEOUT 180)
endforeach()
