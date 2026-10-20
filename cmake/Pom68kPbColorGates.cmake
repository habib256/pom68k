# Color Pangola uses the shared real-ROM boot/input/session harness.
add_test(NAME pb180c_boot_etalon COMMAND pb140_boot_etalon --pb180c)
add_test(NAME interp_pb180c_boot_etalon COMMAND pb140_boot_etalon --pb180c)
add_test(NAME pb180c_agent_boot_etalon COMMAND pb140_boot_etalon --pb180c)
set_tests_properties(pb180c_boot_etalon interp_pb180c_boot_etalon pb180c_agent_boot_etalon PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(interp_pb180c_boot_etalon PROPERTIES ENVIRONMENT "POM68K_CPU_ENGINE=interp")
set_tests_properties(pb180c_agent_boot_etalon PROPERTIES ENVIRONMENT "POM68K_TEST_AGENT=1" TIMEOUT 1800)
include(${CMAKE_CURRENT_LIST_DIR}/Pom68kPbVideoGates.cmake)
