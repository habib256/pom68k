# Headless Portable/PB100 board and its shared real-firmware Power Manager.
foreach(gate portable_pmu_test portable_pmu_firmware_etalon portable_board_test portable_product_test portable_boot_etalon)
    add_executable(${gate} tests/${gate}.cpp)
    target_link_libraries(${gate} PRIVATE pom68k_core)
    add_test(NAME ${gate} COMMAND ${gate} WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endforeach()
add_test(NAME pb100_boot_etalon COMMAND portable_boot_etalon --pb100)
add_test(NAME jit_portable_boot_etalon COMMAND portable_boot_etalon)
add_test(NAME jit_pb100_boot_etalon COMMAND portable_boot_etalon --pb100)
set_tests_properties(pb100_boot_etalon jit_portable_boot_etalon jit_pb100_boot_etalon
    PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(jit_portable_boot_etalon jit_pb100_boot_etalon
    PROPERTIES ENVIRONMENT "POM68K_CPU_ENGINE=jit;POM68K_JIT_BACKEND=threaded")
add_test(NAME portable_agent_boot_etalon COMMAND portable_boot_etalon)
add_test(NAME pb100_agent_boot_etalon COMMAND portable_boot_etalon --pb100)
set_tests_properties(portable_agent_boot_etalon pb100_agent_boot_etalon PROPERTIES
    ENVIRONMENT "POM68K_TEST_AGENT=1" WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
