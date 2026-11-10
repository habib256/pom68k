# Moira (POM68K fork) — standalone build of the 68000–68040 core, its JIT
# and the gates that need no Macintosh, no ROM and no disk image.
#
# This file is the root CMakeLists.txt of a release produced by
# tools/moira_release.py; in the POM68K tree it lives under
# tools/moira_release/. Every other file of the release keeps its POM68K
# path, byte for byte, so a release can be checked against the source tree
# (MANIFEST.sha256) and every path cited in the documents still resolves.

cmake_minimum_required(VERSION 3.16)
project(MoiraPom68k LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
if(MSVC)
    # Moira.cpp's dispatch tables exceed COFF's default section count.
    add_compile_options(/bigobj)
endif()

option(MOIRA_POM68K_TESTS "Build and register the core gates" ON)
set(MOIRA_POM68K_JIT_BACKENDS "auto" CACHE STRING
    "JIT native generator: auto | threaded | x64 | a64")
set_property(CACHE MOIRA_POM68K_JIT_BACKENDS PROPERTY STRINGS auto threaded x64 a64)

# ─── softfloat: 80-bit arithmetic behind the 6888x / 68040 FPU ─────────
# GPLv2-or-later / SoftFloat-2a (extern/softfloat/VENDOR.md). Moira only
# links against it; see LICENSES.md.
add_subdirectory(extern/softfloat)

# ─── Moira ─────────────────────────────────────────────────────────────
set(MOIRA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/extern/moira/Moira)
function(moira_pom68k_core target enable_jit)
    add_library(${target} STATIC ${MOIRA_DIR}/Moira.cpp ${MOIRA_DIR}/MoiraDebugger.cpp)
    target_compile_definitions(${target} PUBLIC MOIRA_ENABLE_JIT=${enable_jit})
    target_include_directories(${target} SYSTEM PUBLIC ${MOIRA_DIR})
    target_link_libraries(${target} PUBLIC softfloat68k)
    if(NOT MSVC)
        target_compile_options(${target} PRIVATE -Wno-unused-parameter -Wno-unused-variable)
    endif()
    if(EMSCRIPTEN)
        target_compile_options(${target} PUBLIC -fwasm-exceptions)
        target_link_options(${target} PUBLIC -fwasm-exceptions)
    endif()
endfunction()
# `moira` carries the acceleration seam (fetch/data windows, DTLB, probes)
# the JIT drives; `moira_interp` is the same sources compiled without it
# (POM68K_VENDOR.md row 76) — the plain interpreter a host embeds when it
# does not want the JIT.
moira_pom68k_core(moira 1)
moira_pom68k_core(moira_interp 0)

# ─── JIT (src/jit/POM68K_JIT.md) ───────────────────────────────────────
# `threaded` generates no host code and is always built; one native
# generator is added per host. The x64 generator follows the System V ABI,
# so `auto` never selects it on Windows.
set(MOIRA_POM68K_JIT_SOURCES
    src/jit/JitEngine.cpp src/jit/JitEngineCensus.cpp src/jit/JitBlockCache.cpp
    src/jit/JitShiftVersions.cpp src/jit/JitBackend.cpp src/jit/JitCodeBuffer.cpp
    src/jit/JitRuntime.cpp src/jit/backends/JitBackendThreaded.cpp)
set(MOIRA_POM68K_JIT_DEFS "")
set(MOIRA_POM68K_JIT_NATIVE "")
set(_jit_choice "${MOIRA_POM68K_JIT_BACKENDS}")
if(EMSCRIPTEN AND NOT _jit_choice MATCHES "^(auto|threaded)$")
    message(FATAL_ERROR "Emscripten supports only the threaded JIT backend")
endif()
if(NOT EMSCRIPTEN)
    if(_jit_choice STREQUAL "x64" OR
       (_jit_choice STREQUAL "auto" AND NOT WIN32 AND
        CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$"))
        set(MOIRA_POM68K_JIT_NATIVE x64)
    elseif(_jit_choice STREQUAL "a64" OR
           (_jit_choice STREQUAL "auto" AND
            CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$"))
        set(MOIRA_POM68K_JIT_NATIVE a64)
    elseif(NOT _jit_choice MATCHES "^(auto|threaded)$")
        message(FATAL_ERROR "Unknown MOIRA_POM68K_JIT_BACKENDS='${_jit_choice}'")
    endif()
endif()
if(MOIRA_POM68K_JIT_NATIVE STREQUAL "x64")
    list(APPEND MOIRA_POM68K_JIT_SOURCES src/jit/backends/JitBackendX64.cpp)
    list(APPEND MOIRA_POM68K_JIT_DEFS POM68K_JIT_BACKEND_X64)
elseif(MOIRA_POM68K_JIT_NATIVE STREQUAL "a64")
    list(APPEND MOIRA_POM68K_JIT_SOURCES src/jit/backends/JitBackendA64.cpp)
    list(APPEND MOIRA_POM68K_JIT_DEFS POM68K_JIT_BACKEND_A64)
endif()
message(STATUS "Moira/POM68K JIT: threaded + '${MOIRA_POM68K_JIT_NATIVE}' native generator")

add_library(moira_jit STATIC ${MOIRA_POM68K_JIT_SOURCES})
target_include_directories(moira_jit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(moira_jit PUBLIC ${MOIRA_POM68K_JIT_DEFS})
target_link_libraries(moira_jit PUBLIC moira)

if(NOT MOIRA_POM68K_TESTS)
    return()
endif()

# ─── Gates ─────────────────────────────────────────────────────────────
# Labels: `unit` (asset-free, must execute), `interp-only` (the same proofs
# against moira_interp), `jit` (asset-free JIT gates), `corpus` (external
# differential vectors; each soft-skips when its directory is absent and
# says so — a skipped corpus proves nothing).
enable_testing()
find_package(Python3 COMPONENTS Interpreter)
if(NOT EMSCRIPTEN)
    find_package(Threads REQUIRED)
    set(MOIRA_POM68K_THREADS Threads::Threads)
endif()
set(T ${CMAKE_CURRENT_SOURCE_DIR}/tests)

add_executable(softfloat_primitives_test ${T}/softfloat_primitives_test.c)
target_link_libraries(softfloat_primitives_test PRIVATE softfloat68k)
add_test(NAME softfloat_primitives_test COMMAND softfloat_primitives_test)
set_tests_properties(softfloat_primitives_test PROPERTIES LABELS unit)

foreach(gate IN ITEMS
    cache040_test       # 040 cache data, policy, physical management, snooping
    fpu_sanity          # external FPU, format-$9 IRQ and BUSY resume
    fpu040_test         # integrated FPU and FPSP exception frames
    fpu_context_test    # independent arithmetic state under nested/parallel CPUs
    callm_rtm_test      # 68020 module protocol
    berr020_test        # 020/EC020 software-completed format-$B cycles
    berr030_test        # 030 bus errors through the MMU bus layer
    moira_rules_test    # hardware-rule cells outside random corpora
    moira_state_test    # instruction-boundary PMMU/FPU continuations
    mmu040_atc_test)    # multi-instruction I/D and FC2 translations
    add_executable(${gate} ${T}/${gate}.cpp)
    target_link_libraries(${gate} PRIVATE moira)
    add_test(NAME ${gate} COMMAND ${gate})
    set_tests_properties(${gate} PROPERTIES LABELS unit)
endforeach()
target_link_libraries(fpu_context_test PRIVATE ${MOIRA_POM68K_THREADS})
set_tests_properties(cache040_test PROPERTIES TIMEOUT 1800)

foreach(proof IN ITEMS berr020_test berr030_test fpu_sanity fpu040_test
                       fpu_context_test callm_rtm_test)
    add_executable(moira_interp_${proof} ${T}/${proof}.cpp)
    target_link_libraries(moira_interp_${proof} PRIVATE moira_interp)
    add_test(NAME moira_interp_${proof} COMMAND moira_interp_${proof})
    set_tests_properties(moira_interp_${proof} PROPERTIES LABELS "unit;interp-only")
endforeach()
target_link_libraries(moira_interp_fpu_context_test PRIVATE ${MOIRA_POM68K_THREADS})
add_executable(moira_interp_full ${T}/moira_interp_test.cpp)
target_link_libraries(moira_interp_full PRIVATE moira)
add_executable(moira_interp_bare ${T}/moira_interp_test.cpp)
target_link_libraries(moira_interp_bare PRIVATE moira_interp)
add_test(NAME moira_interp_test COMMAND ${CMAKE_COMMAND}
    -DFULL=$<TARGET_FILE:moira_interp_full> -DBARE=$<TARGET_FILE:moira_interp_bare>
    "-DEMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}"
    -P ${T}/moira_interp_compare.cmake)
set_tests_properties(moira_interp_test PROPERTIES LABELS "unit;interp-only" TIMEOUT 60)

# ─── Differential corpora (WinUAE oracle / SingleStepTests) ────────────
add_executable(sst68000 ${T}/sst68000.cpp)
add_executable(sst68030 ${T}/sst68030.cpp)
add_executable(sst68040 ${T}/sst68040.cpp)
foreach(runner IN ITEMS sst68000 sst68030 sst68040)
    target_link_libraries(${runner} PRIVATE moira)
endforeach()
# The repository-owned corpus: small, pinned by hash, and it must execute.
if(Python3_FOUND)
    foreach(model IN ITEMS 030 040)
        add_test(NAME cpu_vectors${model}_test
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/cpu_vectors.py replay
                --model ${model} --runner $<TARGET_FILE:sst68${model}>
                --emulator "${CMAKE_CROSSCOMPILING_EMULATOR}")
        set_tests_properties(cpu_vectors${model}_test PROPERTIES LABELS unit TIMEOUT 180)
    endforeach()
    add_test(NAME cpu_vectors_contract_test
        COMMAND ${Python3_EXECUTABLE} ${T}/cpu_vectors_contract_test.py)
    set_tests_properties(cpu_vectors_contract_test PROPERTIES LABELS unit TIMEOUT 30)
endif()
# The large corpora live outside the release (tests/sst_corpora.lock names
# each by hash and recipe). Point MOIRA_POM68K_SST_ROOT at a directory
# holding them; absent cells soft-skip.
set(MOIRA_POM68K_SST_ROOT "${T}/data" CACHE PATH "Root of the external CPU corpora")
function(moira_pom68k_corpus name runner)
    cmake_parse_arguments(C "FPU" "KNOWN" "" ${ARGN})
    set(cmd ${runner} "${MOIRA_POM68K_SST_ROOT}/${name}")
    if(C_FPU)
        list(APPEND cmd --fpu)
    endif()
    if(C_KNOWN)
        list(APPEND cmd --known "${T}/${C_KNOWN}")
    endif()
    add_test(NAME ${name} COMMAND ${cmd})
    set_tests_properties(${name} PROPERTIES LABELS corpus TIMEOUT 3600)
endfunction()
moira_pom68k_corpus(sst68000        sst68000)
moira_pom68k_corpus(sst68030        sst68030 KNOWN sst68030.known)
moira_pom68k_corpus(sst68030_solo   sst68030)
moira_pom68k_corpus(sst68030_fpuraw sst68030 KNOWN sst68030_fpuraw.known)
moira_pom68k_corpus(sst68030_fpu2   sst68030 KNOWN sst68030_fpu2.known)
moira_pom68k_corpus(sst68030_rmw    sst68030 KNOWN sst68030_rmw.known)
moira_pom68k_corpus(sst68040        sst68040)
moira_pom68k_corpus(sst68040_8k     sst68040)
moira_pom68k_corpus(sst68040_fline  sst68040)
moira_pom68k_corpus(sst68040_fpu    sst68040 FPU KNOWN sst68040_fpu.known)
moira_pom68k_corpus(sst68040_fpu2   sst68040 FPU KNOWN sst68040_fpu2.known)
moira_pom68k_corpus(sst68040_fpuh   sst68040 FPU KNOWN sst68040_fpuh.known)
if(Python3_FOUND)
    add_test(NAME sst_corpora_lock_test
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/verify_sst_corpora.py
            --dir sst68000=${MOIRA_POM68K_SST_ROOT}/sst68000
            --dir sst68030=${MOIRA_POM68K_SST_ROOT}/sst68030
            --dir sst68030_solo=${MOIRA_POM68K_SST_ROOT}/sst68030_solo
            --dir sst68030_fpuraw=${MOIRA_POM68K_SST_ROOT}/sst68030_fpuraw
            --dir sst68030_fpu2=${MOIRA_POM68K_SST_ROOT}/sst68030_fpu2
            --dir sst68030_rmw=${MOIRA_POM68K_SST_ROOT}/sst68030_rmw
            --dir sst68040=${MOIRA_POM68K_SST_ROOT}/sst68040
            --dir sst68040_8k=${MOIRA_POM68K_SST_ROOT}/sst68040_8k
            --dir sst68040_fline=${MOIRA_POM68K_SST_ROOT}/sst68040_fline
            --dir sst68040_fpu=${MOIRA_POM68K_SST_ROOT}/sst68040_fpu
            --dir sst68040_fpu2=${MOIRA_POM68K_SST_ROOT}/sst68040_fpu2
            --dir sst68040_fpuh=${MOIRA_POM68K_SST_ROOT}/sst68040_fpuh
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(sst_corpora_lock_test PROPERTIES LABELS corpus TIMEOUT 1800)
endif()

# ─── JIT gates: flat RAM, no machine ───────────────────────────────────
# Reviewed performance budgets become compile-time constants, per host.
set(MOIRA_POM68K_PERF_FILE ${CMAKE_CURRENT_SOURCE_DIR}/performance_budgets.tsv)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${MOIRA_POM68K_PERF_FILE})
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _perf_host)
if(_perf_host MATCHES "^(arm64|aarch64)$")
    set(_perf_host aarch64)
elseif(_perf_host MATCHES "^(x86_64|amd64)$")
    set(_perf_host x86_64)
else()
    set(_perf_host other)
endif()
file(STRINGS ${MOIRA_POM68K_PERF_FILE} _perf_lines)
foreach(line IN LISTS _perf_lines)
    if(line MATCHES "^[ \t]*#" OR line STREQUAL "")
        continue()
    endif()
    string(REPLACE "\t" ";" f "${line}")
    list(GET f 0 w)
    list(GET f 1 c)
    list(GET f 2 h)
    list(GET f 3 m)
    list(GET f 4 v)
    set("PERF_${w}_${c}_${h}_${m}" "${v}")
endforeach()
function(moira_pom68k_budget target workload family metric macro)
    if(DEFINED "PERF_${workload}_${family}_${_perf_host}_${metric}")
        set(v "${PERF_${workload}_${family}_${_perf_host}_${metric}}")
    elseif(DEFINED "PERF_${workload}_${family}_any_${metric}")
        set(v "${PERF_${workload}_${family}_any_${metric}}")
    else()
        message(FATAL_ERROR "performance_budgets.tsv: no ${workload}/${family}/${metric}")
    endif()
    target_compile_definitions(${target} PRIVATE "${macro}=${v}")
endfunction()

# The three gates below prove generated host code. Without a native
# generator their subject does not exist: they are absent, not green.
if(MOIRA_POM68K_JIT_NATIVE)
add_executable(jit_asset_free_lockstep_test ${T}/jit_asset_free_lockstep_test.cpp
               ${T}/jit_atc040_cases.cpp)
target_include_directories(jit_asset_free_lockstep_test PRIVATE ${T})
target_link_libraries(jit_asset_free_lockstep_test PRIVATE moira_jit)
moira_pom68k_budget(jit_asset_free_lockstep_test synthetic_68040_lockstep 68040
    min_blocks_compiled POM68K_PERF_MIN_BLOCKS_COMPILED)
moira_pom68k_budget(jit_asset_free_lockstep_test synthetic_68040_lockstep 68040
    min_blocks_run POM68K_PERF_MIN_BLOCKS_RUN)
moira_pom68k_budget(jit_asset_free_lockstep_test synthetic_68040_lockstep 68040
    min_native_share_permille POM68K_PERF_MIN_NATIVE_SHARE_PERMILLE)
moira_pom68k_budget(jit_asset_free_lockstep_test synthetic_68040_lockstep 68040
    max_slow_instrs POM68K_PERF_MAX_SLOW_INSTRUCTIONS)
add_test(NAME jit_asset_free_lockstep_test COMMAND jit_asset_free_lockstep_test)
set_tests_properties(jit_asset_free_lockstep_test PROPERTIES LABELS jit TIMEOUT 1800)

add_executable(jit_copyback_write_040_test ${T}/jit_copyback_write_040_test.cpp)
target_include_directories(jit_copyback_write_040_test PRIVATE ${T})
target_link_libraries(jit_copyback_write_040_test PRIVATE moira_jit)
moira_pom68k_budget(jit_copyback_write_040_test synthetic_68040_copyback 68040
    max_slow_instrs POM68K_PERF_COPYBACK_MAX_SLOW_INSTRUCTIONS)
moira_pom68k_budget(jit_copyback_write_040_test synthetic_68040_copyback 68040
    max_native_ratio_permille POM68K_PERF_COPYBACK_MAX_RATIO_PERMILLE)
moira_pom68k_budget(jit_copyback_write_040_test synthetic_68040_copyback 68040
    native_slack_microseconds POM68K_PERF_COPYBACK_SLACK_US)
# Same gate names as POM68K's registry (cmake/Pom68kJitGates.cmake).
add_test(NAME jit_copyback_write_040_test COMMAND jit_copyback_write_040_test)
add_test(NAME jit_copyback_write_040_control_test COMMAND jit_copyback_write_040_test --disabled)
add_test(NAME jit_copyback_bsr_040_test COMMAND jit_copyback_write_040_test --bsr)
add_test(NAME jit_copyback_pair_040_test COMMAND jit_copyback_write_040_test --pair)
add_test(NAME jit_copyback_pair_040_control_test COMMAND jit_copyback_write_040_test --pair-disabled)
set_tests_properties(jit_copyback_write_040_test jit_copyback_write_040_control_test
    jit_copyback_bsr_040_test jit_copyback_pair_040_test jit_copyback_pair_040_control_test
    PROPERTIES LABELS jit)

add_executable(jit_restart_write_030_test ${T}/jit_restart_write_030_test.cpp)
target_include_directories(jit_restart_write_030_test PRIVATE ${T})
target_link_libraries(jit_restart_write_030_test PRIVATE moira_jit)
add_test(NAME jit_restart_write_030_test COMMAND jit_restart_write_030_test)
set_tests_properties(jit_restart_write_030_test PROPERTIES LABELS jit)
endif()

# Exists only where its subject does: the a64 store guard on an AArch64 host.
if(MOIRA_POM68K_JIT_NATIVE STREQUAL "a64")
    add_executable(jit_store_guard_a64_test ${T}/jit_store_guard_a64_test.cpp)
    target_include_directories(jit_store_guard_a64_test PRIVATE ${T})
    target_link_libraries(jit_store_guard_a64_test PRIVATE moira_jit)
    add_test(NAME jit_store_guard_a64_test COMMAND jit_store_guard_a64_test)
    set_tests_properties(jit_store_guard_a64_test PROPERTIES LABELS jit)
endif()

# Both generators only emit bytes into buffers here, so the acceptance
# sweep runs on any host ISA: add whichever generator the library lacks.
set(_parity_extra "")
if(NOT MOIRA_POM68K_JIT_NATIVE STREQUAL "x64")
    list(APPEND _parity_extra src/jit/backends/JitBackendX64.cpp)
endif()
if(NOT MOIRA_POM68K_JIT_NATIVE STREQUAL "a64")
    list(APPEND _parity_extra src/jit/backends/JitBackendA64.cpp)
endif()
add_executable(jit_backend_parity_test ${T}/jit_backend_parity_test.cpp ${_parity_extra})
target_link_libraries(jit_backend_parity_test PRIVATE moira_jit)
add_test(NAME jit_backend_parity_test COMMAND jit_backend_parity_test)
set_tests_properties(jit_backend_parity_test PROPERTIES LABELS jit)
