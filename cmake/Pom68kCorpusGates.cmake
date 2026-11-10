# Public oracle output, shipped with the tree: these gates must never soft-skip.
foreach(model IN ITEMS 030 040)
    add_test(NAME cpu_vectors${model}_test
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/cpu_vectors.py replay
            --model ${model} --runner $<TARGET_FILE:sst68${model}>
            --emulator "${CMAKE_CROSSCOMPILING_EMULATOR}")
    set_tests_properties(cpu_vectors${model}_test PROPERTIES
        LABELS cpu-vectors TIMEOUT 180)
endforeach()
add_test(NAME cpu_vectors_contract_test
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tests/cpu_vectors_contract_test.py)
set_tests_properties(cpu_vectors_contract_test PROPERTIES LABELS cpu-vectors TIMEOUT 30)
