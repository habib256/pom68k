# Display Card 8•24 (DisplayCard824.h): the asset-free register, scan-out,
# VBL and save-state contracts, then the card's 341-0868 ROM and driver
# taking the System 7 Finder to each offered colour monitor, in one and
# eight bits, on the 68020 Mac II and the 68030 IIcx.
add_executable(display_card824_test tests/display_card824_test.cpp)
target_link_libraries(display_card824_test PRIVATE pom68k_core)
add_test(NAME display_card824_test COMMAND display_card824_test)

add_executable(macii_824_etalon tests/macii_824_etalon.cpp)
target_link_libraries(macii_824_etalon PRIVATE pom68k_core)
add_test(NAME macii_824_etalon COMMAND macii_824_etalon)
add_test(NAME macii_824_8bit_etalon COMMAND macii_824_etalon --depth 8)
add_test(NAME macii_824_rgb12_etalon COMMAND macii_824_etalon --monitor rgb12)
add_test(NAME macii_824_rgb21_etalon COMMAND macii_824_etalon --monitor rgb21)
add_test(NAME iicx_824_8bit_etalon COMMAND macii_824_etalon --iicx --depth 8)
set_tests_properties(macii_824_etalon macii_824_8bit_etalon
    macii_824_rgb12_etalon macii_824_rgb21_etalon iicx_824_8bit_etalon
    PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

