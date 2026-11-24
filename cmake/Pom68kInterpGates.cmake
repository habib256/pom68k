# Same Moira sources, separate ABI: no acceleration storage or hot-path probes.
add_library(moira_interp STATIC ${MOIRA_SOURCES})
# The Macintosh settings of `moira` (CMakeLists.txt), so that the two builds
# differ by the JIT seam alone.
target_compile_definitions(moira_interp PUBLIC MOIRA_ENABLE_JIT=0 ${POM68K_MOIRA_DEFS})
target_include_directories(moira_interp SYSTEM PUBLIC ${MOIRA_DIR})
target_link_libraries(moira_interp PUBLIC softfloat68k)
if(NOT MSVC)
    target_compile_options(moira_interp PRIVATE -Wno-unused-parameter -Wno-unused-variable)
endif()
if(EMSCRIPTEN)
    target_compile_options(moira_interp PUBLIC -fwasm-exceptions)
    target_link_options(moira_interp PUBLIC -fwasm-exceptions)
endif()
foreach(proof IN ITEMS berr020_test berr030_test fpu_sanity fpu040_test fpu_context_test)
    add_executable(moira_interp_${proof} tests/${proof}.cpp)
    target_link_libraries(moira_interp_${proof} PRIVATE moira_interp)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/known/${proof}.txt)
        pom68k_known_test(moira_interp_${proof} moira_interp_${proof} ${proof})
    else()
        add_test(NAME moira_interp_${proof} COMMAND moira_interp_${proof})
    endif()
    set_tests_properties(moira_interp_${proof} PROPERTIES LABELS interp-only)
endforeach()
target_link_libraries(moira_interp_fpu_context_test PRIVATE ${POM68K_CPU_CONTEXT_THREADS})
foreach(variant IN ITEMS full bare)
    add_executable(moira_interp_${variant} tests/moira_interp_test.cpp)
    if(variant STREQUAL "bare")
        target_link_libraries(moira_interp_${variant} PRIVATE moira_interp)
    else()
        target_link_libraries(moira_interp_${variant} PRIVATE moira)
    endif()
endforeach()
add_test(NAME moira_interp_test COMMAND ${CMAKE_COMMAND}
    -DFULL=$<TARGET_FILE:moira_interp_full> -DBARE=$<TARGET_FILE:moira_interp_bare>
    "-DEMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/moira_interp_compare.cmake)
set_tests_properties(moira_interp_test PROPERTIES LABELS interp-only TIMEOUT 60)
# The same sources under upstream Moira's differing settings (its 68020 i-cache on), the ones
# its own Runner (against Musashi) uses; the cell runs under both configurations.
add_library(moira_upstream_config STATIC ${MOIRA_SOURCES})
# On Dirk's core the fork's MOIRA_MIMIC_MUSASHI is MOIRA_COMPATIBILITY=MUSASHI.
target_compile_definitions(moira_upstream_config PUBLIC
    MOIRA_PRECISE_TIMING=false MOIRA_EMULATE_ADDRESS_ERROR=false MOIRA_COMPATIBILITY=MUSASHI
    MOIRA_EMULATE_ICACHE=true)
target_include_directories(moira_upstream_config SYSTEM PUBLIC ${MOIRA_DIR})
target_link_libraries(moira_upstream_config PUBLIC softfloat68k)
if(NOT MSVC)
    target_compile_options(moira_upstream_config PRIVATE -Wno-unused-parameter -Wno-unused-variable)
endif()
if(EMSCRIPTEN)
    target_compile_options(moira_upstream_config PUBLIC -fwasm-exceptions)
    target_link_options(moira_upstream_config PUBLIC -fwasm-exceptions)
endif()
foreach(cfg IN ITEMS upstream pom68k)
    add_executable(moira_${cfg}_config_test tests/moira_upstream_config_test.cpp)
    add_test(NAME moira_${cfg}_config_test COMMAND moira_${cfg}_config_test)
endforeach()
target_link_libraries(moira_upstream_config_test PRIVATE moira_upstream_config)
target_link_libraries(moira_pom68k_config_test PRIVATE moira)
set_tests_properties(moira_upstream_config_test PROPERTIES LABELS interp-only)
# Upstream's 68020 i-cache (MOIRA_EMULATE_ICACHE) under upstream's settings;
# POM68K's own configuration has it on too (moira_state_test holds it there).
add_executable(icache020_test tests/icache020_test.cpp)
target_link_libraries(icache020_test PRIVATE moira_upstream_config)
target_include_directories(icache020_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
pom68k_known_test(icache020_test icache020_test icache020_test)
set_tests_properties(icache020_test PROPERTIES LABELS interp-only)
