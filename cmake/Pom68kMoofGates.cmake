# Native physical-track storage and MOOF import/export consumers.
add_executable(moof_image_test tests/moof_image_test.cpp)
target_link_libraries(moof_image_test PRIVATE pom68k_core)
add_test(NAME moof_image_test COMMAND moof_image_test)
add_test(NAME moof_media_state_etalon COMMAND moof_image_test "disks35/Disk605.moof"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(moof_media_state_etalon PROPERTIES TIMEOUT 600)
add_executable(moof_fixture tests/moof_fixture.cpp)
target_link_libraries(moof_fixture PRIVATE pom68k_core)
include(${CMAKE_CURRENT_LIST_DIR}/Pom68kProtectedFloppyGates.cmake)
foreach(location internal external)
    if(location STREQUAL "external")
        set(moof_drive_argument --external)
    else()
        set(moof_drive_argument)
    endif()
    add_test(NAME plus_moof_${location}_boot_etalon COMMAND system_boot_etalon
        ${moof_drive_argument} "disks35/Disk605.moof"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(plus_moof_${location}_boot_etalon PROPERTIES TIMEOUT 600)
endforeach()
add_test(NAME maciifdhd_moof_boot_etalon
    COMMAND ${POM68K_DART_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/tests/moof_boot_etalon.py
        $<TARGET_FILE:moof_fixture> $<TARGET_FILE:macii_boot_etalon>
        "${CMAKE_CURRENT_SOURCE_DIR}/disks35/System 6.0.8 - System Startup.img"
        --fdhd --floppy WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(maciifdhd_moof_boot_etalon PROPERTIES TIMEOUT 1800)
add_test(NAME plus_moof_export_boot_etalon
    COMMAND ${POM68K_DART_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/tests/moof_boot_etalon.py
        $<TARGET_FILE:moof_fixture> $<TARGET_FILE:system_boot_etalon>
        "${CMAKE_CURRENT_SOURCE_DIR}/disks35/Disk605.moof"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(plus_moof_export_boot_etalon PROPERTIES TIMEOUT 600)
