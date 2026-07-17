# POM68K — Macintosh 68k emulator
# VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)
#
# O1 — WinUAE 68030+MMU oracle (vendored via Hatari, see VENDOR.md), built
# standalone:  cmake -S oracle/uae -B build-oracle-uae && cmake --build ...
# Produces liboracle_uae.so exposing exactly oracle/oracle_api.h.

cmake_minimum_required(VERSION 3.16)
project(oracle_uae C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(UP  ${CMAKE_CURRENT_SOURCE_DIR}/upstream)
set(GEN ${CMAKE_CURRENT_SOURCE_DIR}/generated)
set(SHIM ${CMAKE_CURRENT_SOURCE_DIR}/shim)

# gencpu output, as vendored (regenerate with upstream/build68k.c + gencpu.c
# from upstream/table68k — see VENDOR.md).
set(CPUEMU_SRCS
    ${GEN}/cpustbl.c
    ${GEN}/cpuemu_0.c  ${GEN}/cpuemu_11.c ${GEN}/cpuemu_13.c
    ${GEN}/cpuemu_20.c ${GEN}/cpuemu_21.c ${GEN}/cpuemu_22.c
    ${GEN}/cpuemu_23.c ${GEN}/cpuemu_24.c
    ${GEN}/cpuemu_31.c ${GEN}/cpuemu_32.c ${GEN}/cpuemu_33.c
    ${GEN}/cpuemu_34.c ${GEN}/cpuemu_35.c
    ${GEN}/cpuemu_40.c ${GEN}/cpuemu_50.c)

set(CORE_SRCS
    ${GEN}/cpudefs.c
    ${UP}/newcpu.c
    ${UP}/newcpu_common.c
    ${UP}/cpummu.c
    ${UP}/cpummu030.c
    ${UP}/readcpu.c
    ${UP}/fpp.c
    ${UP}/fpp_native.c
    ${UP}/fpp_softfloat.c
    ${UP}/softfloat/softfloat.c
    ${UP}/softfloat/softfloat_decimal.c
    ${UP}/softfloat/softfloat_fpsp.c
    ${UP}/machdep/m68k.c
    ${UP}/disasm.c
    ${UP}/writelog.c
    ${UP}/events.c
    ${UP}/custom.c)

add_library(oracle_uae SHARED
    glue.c
    ${SHIM}/memglue.c
    ${SHIM}/stubs.c
    ${CORE_SRCS}
    ${CPUEMU_SRCS})

# Shim first so our replacement headers win over any stray name, then the
# vendored dirs (quoted includes resolve to the includer's dir first anyway).
target_include_directories(oracle_uae PRIVATE
    ${SHIM} ${UP} ${GEN} ${UP}/softfloat ${UP}/machdep
    ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(oracle_uae PRIVATE POM68K_ORACLE)
target_link_libraries(oracle_uae PRIVATE m)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(oracle_uae PRIVATE
        -fwrapv
        -Wno-unused-variable -Wno-unused-function -Wno-unused-label
        -Wno-sign-compare -Wno-missing-braces -Wno-implicit-fallthrough
        -Wno-unused-but-set-variable
        -fvisibility=hidden
        -ffunction-sections -fdata-sections)
    target_link_options(oracle_uae PRIVATE -Wl,--gc-sections)
endif()

# API-only visibility: export exactly the oracle_api.h entry points.
set_target_properties(oracle_uae PROPERTIES
    LINK_FLAGS "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/oracle_uae.map")

# ---- smoke test ----------------------------------------------------------
enable_testing()
add_executable(oracle_uae_smoke smoke.c)
target_include_directories(oracle_uae_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(oracle_uae_smoke PRIVATE oracle_uae)
add_test(NAME oracle_uae_smoke COMMAND oracle_uae_smoke)
