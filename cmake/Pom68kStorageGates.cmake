# Storage cross-section: catalogue capabilities, early IWM/SWIM wiring and
# the compact profiles' explicit no-floppy SCSI boots.
add_executable(storage_profile_test tests/storage_profile_test.cpp)
target_link_libraries(storage_profile_test PRIVATE pom68k_core)
add_test(NAME storage_profile_test COMMAND storage_profile_test)

foreach(model se sefdhd classic)
    add_test(NAME ${model}_scsi_boot_etalon COMMAND scsi_boot_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(${model}_scsi_boot_etalon PROPERTIES
                         ENVIRONMENT "POM68K_COMPACT_MODEL=${model}"
                         TIMEOUT 1800)
endforeach()
