# DART conformance; generated boot archives never ship private system data.
add_executable(floppy_persist_test tests/floppy_persist_test.cpp)
target_link_libraries(floppy_persist_test PRIVATE pom68k_core)
add_test(NAME floppy_persist_test COMMAND floppy_persist_test)
include(${CMAKE_CURRENT_LIST_DIR}/Pom68kThreeDriveGates.cmake)
add_executable(dart_image_test tests/dart_image_test.cpp)
target_link_libraries(dart_image_test PRIVATE pom68k_core)
add_test(NAME dart_image_test COMMAND dart_image_test)

find_program(POM68K_DART_PYTHON NAMES python3 python REQUIRED)
foreach(location internal external)
    add_test(NAME plus_system33_dart_${location}_boot_etalon
        COMMAND ${POM68K_DART_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/tests/dart_boot_etalon.py
            $<TARGET_FILE:system_boot_etalon>
            "${CMAKE_CURRENT_SOURCE_DIR}/disks35/ref/System 3.3.dsk" ${location}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(plus_system33_dart_${location}_boot_etalon PROPERTIES TIMEOUT 600)
endforeach()
include(${CMAKE_CURRENT_LIST_DIR}/Pom68kMoofGates.cmake)
