include(${CMAKE_CURRENT_LIST_DIR}/Pom68kPb030Gates.cmake)
foreach(gate m740_test m50753_test m50753_firmware_etalon pb030_product_test pb030_board_test pb160_board_test pb140_boot_etalon)
    add_executable(${gate} tests/${gate}.cpp)
    target_link_libraries(${gate} PRIVATE pom68k_core)
    add_test(NAME ${gate} COMMAND ${gate}
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endforeach()
add_test(NAME interp_pb140_boot_etalon COMMAND pb140_boot_etalon)
set_tests_properties(interp_pb140_boot_etalon PROPERTIES ENVIRONMENT "POM68K_CPU_ENGINE=interp" WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
