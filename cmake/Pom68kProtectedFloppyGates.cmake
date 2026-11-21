# Original program captures remain user-provided and never ship in the repo.
add_executable(plus_oids_moof_etalon tests/plus_oids_moof_etalon.cpp)
target_link_libraries(plus_oids_moof_etalon PRIVATE pom68k_core)
# The interpreter only: the JIT does not execute a 68000 (CHANGELOG
# 2026-11-21 (eighth)); the test name keeps its interp_ prefix for history.
add_test(NAME interp_plus_oids_moof_etalon COMMAND plus_oids_moof_etalon
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(interp_plus_oids_moof_etalon PROPERTIES
    ENVIRONMENT "POM68K_CPU_ENGINE=interp" TIMEOUT 900)
