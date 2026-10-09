# Shared-ROM profiles retain separate hardware and save-state identities.
add_executable(macii_boot_etalon tests/macii_boot_etalon.cpp)
target_link_libraries(macii_boot_etalon PRIVATE pom68k_core)
add_test(NAME macii_boot_etalon COMMAND macii_boot_etalon
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME maciifdhd_boot_etalon COMMAND macii_boot_etalon --fdhd
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(maciifdhd_boot_etalon PROPERTIES TIMEOUT 1800)

add_executable(early_profile_test tests/early_profile_test.cpp)
target_link_libraries(early_profile_test PRIVATE pom68k_core)
add_test(NAME early_profile_test COMMAND early_profile_test)

add_test(NAME mac512ke_boot_etalon COMMAND system_boot_etalon --512ke "disks35/System 3.3.dsk"
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME mac512ke_external_boot_etalon COMMAND system_boot_etalon --512ke --external "disks35/System 3.3.dsk"
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(mac512ke_boot_etalon mac512ke_external_boot_etalon PROPERTIES TIMEOUT 600)

add_test(NAME mac512ke_input_etalon COMMAND input_etalon --512ke
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME maciifdhd_floppy_boot_etalon COMMAND macii_boot_etalon --fdhd --floppy
         "disks35/System 6.0.8 - System Startup.img"
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(mac512ke_input_etalon PROPERTIES TIMEOUT 600)
set_tests_properties(maciifdhd_floppy_boot_etalon PROPERTIES TIMEOUT 1800)

# Same older guest driver on the Plus: isolates its mouse gain from the RAM profile.
add_test(NAME plus_system33_input_etalon COMMAND input_etalon --system33
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(plus_system33_input_etalon PROPERTIES TIMEOUT 600)
