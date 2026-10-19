# First consumer of the Mitsubishi core: PowerBook 140–180 M50753 PMU.
# Keep CPU/peripheral proofs asset-free; the separate firmware etalon
# requires private Apple dumps and must not count as an asset-free pass.
foreach(gate m740_test m50753_test m50753_firmware_etalon)
    add_executable(${gate} tests/${gate}.cpp)
    target_link_libraries(${gate} PRIVATE pom68k_core)
    add_test(NAME ${gate} COMMAND ${gate}
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endforeach()
