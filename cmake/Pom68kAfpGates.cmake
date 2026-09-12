# Real Mac OS 8.1 Finder transfers: clean reconnect and interruptions in each
# fork. All three own run/afp-live, so CTest must serialize them even with -j.
add_executable(q605_afp_live_etalon tests/q605_afp_live_etalon.cpp)
target_link_libraries(q605_afp_live_etalon PRIVATE pom68k_core)
add_test(NAME q605_afp_live_etalon COMMAND q605_afp_live_etalon
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(q605_afp_live_etalon PROPERTIES
                     ENVIRONMENT "POM68K_AFP_OUTAGE=;POM68K_AFP_PHASE=99")
set(pom68k_afp_gates q605_afp_live_etalon)
foreach(fork data resource)
    set(gate "q605_afp_outage_${fork}_etalon")
    add_test(NAME ${gate} COMMAND q605_afp_live_etalon
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(${gate} PROPERTIES
                         ENVIRONMENT "POM68K_AFP_OUTAGE=${fork};POM68K_AFP_PHASE=99")
    list(APPEND pom68k_afp_gates ${gate})
endforeach()
set_tests_properties(${pom68k_afp_gates} PROPERTIES
                     TIMEOUT 1800 RESOURCE_LOCK afp_live_share)
