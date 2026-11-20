# Bare-Moira CPU gates: each links `moira` (and softfloat) against a flat RAM,
# with no Macintosh object, no ROM and no JIT engine. They are the part of the
# CPU proof that can travel with the core (TODO.md § Moteur › Socle). Included
# from Pom68kJitGates.cmake at the position these registrations always had.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Pom68kCorpusGates.cmake)
add_executable(softfloat_primitives_test tests/softfloat_primitives_test.c)
target_link_libraries(softfloat_primitives_test PRIVATE softfloat68k)
add_test(NAME softfloat_primitives_test COMMAND softfloat_primitives_test)
set_tests_properties(softfloat_primitives_test PROPERTIES LABELS cpu-vectors)
# The library's exports all carry the private prefix of softfloat-prefix.h,
# so it links next to another SoftFloat-2a (upstream Moira's runner, Musashi).
find_program(POM68K_NM NAMES nm llvm-nm)
if(POM68K_NM AND NOT MSVC)
    add_test(NAME softfloat_symbols_test
             COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tests/softfloat_symbols_test.py
                     --nm ${POM68K_NM} --lib $<TARGET_FILE:softfloat68k>)
    set_tests_properties(softfloat_symbols_test PROPERTIES LABELS cpu-vectors)
endif()

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
    moira_rules_test fault_transparency_test # rule cells; each cycle faulted, RTE, same state
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

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Pom68kSstGates.cmake)

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
# The public vectors (tests/cpu_vectors/) through the translator: the
# same replay with no optional data, so CI exercises it on every host.
foreach(model IN ITEMS 030 040)
    add_test(NAME cpu_vectors${model}_jit_test
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/cpu_vectors.py replay
            --model ${model} --runner $<TARGET_FILE:sst68${model}_jit> --jit
            --emulator "${CMAKE_CROSSCOMPILING_EMULATOR}")
    set_tests_properties(cpu_vectors${model}_jit_test PROPERTIES
        LABELS "cpu-vectors;jit" TIMEOUT 180)
endforeach()
