# The VIA1 PA4 internal-connector line: three distinct floppies mount on
# the dual-floppy SE and SE FDHD; the single-floppy SE's and the Classic's
# PA4-high connector stays empty (tests/se_three_drive_etalon.cpp).
add_executable(se_three_drive_etalon tests/se_three_drive_etalon.cpp)
target_link_libraries(se_three_drive_etalon PRIVATE pom68k_core)
add_test(NAME se_three_drive_etalon COMMAND se_three_drive_etalon
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME sefdhd_three_drive_etalon COMMAND se_three_drive_etalon
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME classic_pa4_drive_etalon COMMAND se_three_drive_etalon
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME se_single_floppy_pa4_etalon COMMAND se_three_drive_etalon --single
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(sefdhd_three_drive_etalon PROPERTIES
                     ENVIRONMENT "POM68K_COMPACT_MODEL=sefdhd")
set_tests_properties(classic_pa4_drive_etalon PROPERTIES
                     ENVIRONMENT "POM68K_COMPACT_MODEL=classic")
add_executable(se_drive_select_test tests/se_drive_select_test.cpp)
target_link_libraries(se_drive_select_test PRIVATE pom68k_core)
add_test(NAME se_drive_select_test COMMAND se_drive_select_test)
