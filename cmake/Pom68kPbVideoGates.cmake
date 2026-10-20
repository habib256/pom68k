foreach(gate pangola_video_test pb030_external_video_test)
    add_executable(${gate} tests/${gate}.cpp)
    target_link_libraries(${gate} PRIVATE pom68k_core)
    add_test(NAME ${gate} COMMAND ${gate})
endforeach()

# Real slot-E driver, both LCD families and all clocks/FPU variants.
foreach(model pb160 pb165 pb180 pb165c pb180c)
    add_test(NAME ${model}_external_video_etalon COMMAND pb140_boot_etalon --${model} --external)
    add_test(NAME interp_${model}_external_video_etalon COMMAND pb140_boot_etalon --${model} --external)
    set_tests_properties(${model}_external_video_etalon interp_${model}_external_video_etalon PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(interp_${model}_external_video_etalon PROPERTIES ENVIRONMENT "POM68K_CPU_ENGINE=interp")
endforeach()
