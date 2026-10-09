# Original program captures remain user-provided and never ship in the repo.
add_executable(plus_oids_moof_etalon tests/plus_oids_moof_etalon.cpp)
target_link_libraries(plus_oids_moof_etalon PRIVATE pom68k_core)
foreach(engine interp jit)
    add_test(NAME ${engine}_plus_oids_moof_etalon COMMAND plus_oids_moof_etalon
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(${engine}_plus_oids_moof_etalon PROPERTIES
        ENVIRONMENT "POM68K_CPU_ENGINE=${engine}" TIMEOUT 900)
endforeach()
