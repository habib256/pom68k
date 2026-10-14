# The pixel pins (tests/PixelPin.h, tools/pixel_pins.tsv) replayed by the
# WebAssembly build. One file, two halves, one table:
#
#   EMSCRIPTEN build  compiles every pin-bearing boot etalon to a node
#                     program (`make wasmpins`) that reads roms/, hdv/ and
#                     disks35/ straight from the host through NODERAWFS:
#                     nothing private is embedded, preloaded or packaged.
#   native build      registers one `wasm_<gate>` per pinned boot gate, which
#                     runs that program under node from the source tree with
#                     the native gate's argv and environment, against the
#                     SAME pin table. Label `wasm`, nothing else: the replay
#                     is a second host for the pins, not a second copy of the
#                     etalon/m030/m040 tiers.
#
# A WASM pin that differs from the native one is a finding about an engine
# or a host, never a reason to re-pin: the table has one row per key, and
# PixelPin.h's settle exists precisely so the value is engine-independent.
#
# The native half soft-skips — `SKIP:` and exit 0, which the execution
# census counts as an abstention, never as coverage — when node or the WASM
# build is absent; a missing ROM or volume is the etalon's own SKIP, under
# node exactly as natively. See DEV.md § "WebAssembly pin replay".

# <gate>|<binary>|<argv ;-separated>|<environment ;-separated as `,`>
# The rows mirror the native registrations in Pom68kComponentGates,
# Pom68kProfileGates and Pom68kMachineGates; argv/env are copied, never
# reinterpreted.
set(POM68K_WASM_PIN_GATES
    # 68000 — MacMemory
    "system_boot_etalon|system_boot_etalon||"
    "plus_system33_boot_etalon|system_boot_etalon|disks35/System 3.3.dsk|"
    "plus_system33_external_boot_etalon|system_boot_etalon|--external,disks35/System 3.3.dsk|"
    "mac512ke_boot_etalon|system_boot_etalon|--512ke,disks35/System 3.3.dsk|"
    "se_boot_etalon|compact_boot_etalon||"
    "sefdhd_boot_etalon|compact_boot_etalon||POM68K_COMPACT_MODEL=sefdhd"
    "classic_boot_etalon|compact_boot_etalon||POM68K_COMPACT_MODEL=classic"
    "mac128k_boot_etalon|mac128k_boot_etalon||"
    "mac512k_boot_etalon|mac128k_boot_etalon||POM68K_MAC128K_MODEL=mac512k"
    # 68020/68030 — GLUE
    "macii_boot_etalon|macii_boot_etalon||"
    "maciifdhd_boot_etalon|macii_boot_etalon|--fdhd|"
    "maciifdhd_floppy_boot_etalon|macii_boot_etalon|--fdhd,--floppy,disks35/System 6.0.8 - System Startup.img|"
    "iix_boot_etalon|iix_boot_etalon||"
    "iicx_boot_etalon|iix_boot_etalon||POM68K_IICX=1"
    "se30_boot_etalon|se30_boot_etalon||"
    # 68030 — OSS, RBV, V8, Sonora, VASP, MSC
    "iifx_boot_etalon|iifx_boot_etalon||"
    "iisi_boot_etalon|iisi_boot_etalon||"
    "iici_boot_etalon|iici_boot_etalon||"
    "lcii_boot_etalon|lcii_boot_etalon||"
    "lc_boot_etalon|lc_boot_etalon||"
    "classic2_boot_etalon|classic2_boot_etalon||"
    "cclassic_boot_etalon|cclassic_boot_etalon||"
    "mactv_boot_etalon|mactv_boot_etalon||"
    "lc3_boot_etalon|lc3_boot_etalon||"
    "lc3plus_boot_etalon|lc3plus_boot_etalon||"
    "lc520_boot_etalon|lc520_boot_etalon||"
    "lc550_boot_etalon|lc550_boot_etalon||"
    "cclassic2_boot_etalon|cclassic2_boot_etalon||"
    "iivx_boot_etalon|iivx_boot_etalon||"
    "iivi_boot_etalon|iivx_boot_etalon||POM68K_IIVI=1"
    "duo230_boot_etalon|duo230_boot_etalon||"
    # 68040 — MEMCjr, djMEMC, Spike/Eclipse, F108
    "q605_boot_etalon|q605_boot_etalon||"
    "lc475_boot_etalon|lc475_boot_etalon||"
    "lc575_boot_etalon|lc575_boot_etalon||"
    "centris650_boot_etalon|centris650_boot_etalon||"
    "centris610_boot_etalon|centris650_boot_etalon||POM68K_CENTRIS610=1"
    "quadra650_boot_etalon|centris650_boot_etalon||POM68K_CENTRIS_MODEL=q650"
    "quadra610_boot_etalon|centris650_boot_etalon||POM68K_CENTRIS_MODEL=q610"
    "quadra800_boot_etalon|centris650_boot_etalon||POM68K_CENTRIS_MODEL=q800"
    "q630_boot_etalon|q630_boot_etalon||"
    "lc580_boot_etalon|q630_boot_etalon||POM68K_Q630_ID=A55A225A,POM68K_Q630_ROM=lc580"
    "q700_boot_etalon|q700_boot_etalon||"
    "q900_boot_etalon|q700_boot_etalon|q900|"
    "q950_boot_etalon|q700_boot_etalon|q950|")

set(pom68k_wasm_pin_binaries "")
foreach(row IN LISTS POM68K_WASM_PIN_GATES)
    string(REPLACE "|" ";" fields "${row}")
    list(GET fields 1 bin)
    list(APPEND pom68k_wasm_pin_binaries ${bin})
endforeach()
list(REMOVE_DUPLICATES pom68k_wasm_pin_binaries)

if(EMSCRIPTEN)
    # NODERAWFS: the host filesystem, relative to node's cwd — the source
    # tree, as for the native gates. The guest machines are built on the
    # stack exactly as natively, so the stack is the POSIX main thread's
    # 16 MB, not Emscripten's 64 KB. EXIT_RUNTIME so `return 1` is node's
    # exit status. The pre-js hands node's environment to getenv(), which
    # an Emscripten program otherwise does not see.
    set(pom68k_wasm_pin_link
        -sENVIRONMENT=node -sNODERAWFS=1 -sEXIT_RUNTIME=1
        -sSTACK_SIZE=16777216 -sINITIAL_MEMORY=268435456
        -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=4294967296
        "--pre-js=${CMAKE_CURRENT_SOURCE_DIR}/tools/wasm_pin_env.js")
    foreach(bin IN LISTS pom68k_wasm_pin_binaries)
        add_executable(${bin} tests/${bin}.cpp)
        target_include_directories(${bin} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/tests ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_link_libraries(${bin} PRIVATE pom68k_core)
        target_link_options(${bin} PRIVATE ${pom68k_wasm_pin_link})
        set_property(TARGET ${bin} APPEND PROPERTY LINK_DEPENDS
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/wasm_pin_env.js)
    endforeach()
    add_custom_target(wasmpins DEPENDS ${pom68k_wasm_pin_binaries})
    return()
endif()

# ── Native half: the replay gates ─────────────────────────────────────────
set(POM68K_WASM_BUILD_DIR "${CMAKE_BINARY_DIR}/wasm" CACHE PATH
    "Emscripten build tree holding the `make wasmpins` programs")
find_program(POM68K_NODE node)
foreach(row IN LISTS POM68K_WASM_PIN_GATES)
    string(REPLACE "|" ";" fields "${row}")
    list(GET fields 0 gate)
    list(GET fields 1 bin)
    list(GET fields 2 argv)
    list(GET fields 3 env)
    string(REPLACE "," ";" argv "${argv}")
    string(REPLACE "," ";" env "${env}")
    if(NOT TEST ${gate})
        message(FATAL_ERROR "Pom68kWasmPins.cmake replays '${gate}', which "
                            "is not a registered native gate")
    endif()
    add_test(NAME wasm_${gate}
             COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/wasm_pin_gate.sh
                     "${POM68K_NODE}" "${POM68K_WASM_BUILD_DIR}/${bin}.js"
                     ${argv}
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    # The interpreter/threaded floor under node runs several times slower
    # than the native engine; the ceiling is a STOP, not a budget.
    set_tests_properties(wasm_${gate} PROPERTIES TIMEOUT 5400 LABELS wasm)
    if(env)
        set_tests_properties(wasm_${gate} PROPERTIES ENVIRONMENT "${env}")
    endif()
endforeach()
