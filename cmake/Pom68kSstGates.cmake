# SST harness contracts and optional architecture corpora.
# Payloads are external; the scoped-rule contract uses a repository-owned fixture.
# The 68030 against WinUAE alone, all fourteen cells: the pinned sst68030
# keeps, in several cells, only the vectors a retired second oracle agreed
# on, and has no mmu_tt or random_tt cell.
set(POM68K_SST030_SOLO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/sst68030_solo"
    CACHE PATH "Directory of WinUAE-solo SST030 vectors (all cells)")
add_test(NAME sst68030_solo COMMAND sst68030 "${POM68K_SST030_SOLO_DIR}"
         --known "${CMAKE_CURRENT_SOURCE_DIR}/tests/sst68030_solo.known" --strict-ram)

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
         COMMAND sst68030 "${POM68K_SST030_FPUH_DIR}"
                 --known "${CMAKE_CURRENT_SOURCE_DIR}/tests/sst68030_fpuh.known" --strict-ram)

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

add_test(NAME sst_known_contract_test
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tests/sst_known_contract_test.py
        --runner $<TARGET_FILE:sst68030> --emulator "${CMAKE_CROSSCOMPILING_EMULATOR}")
set_tests_properties(sst_known_contract_test PROPERTIES LABELS cpu-vectors TIMEOUT 120)
