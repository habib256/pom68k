# Actual MacTCP Server configuration; no address typed into the guest.
add_test(NAME q605_dayna_rarp_etalon COMMAND q605_dayna_driver_etalon rarp
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(q605_dayna_rarp_etalon PROPERTIES TIMEOUT 2400)

# Real MacTCP UDP DNS followed by TCP against controlled host socket peers.
add_test(NAME q605_dayna_rarp_network_etalon COMMAND q605_dayna_driver_etalon rarp-net
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(q605_dayna_rarp_network_etalon PROPERTIES TIMEOUT 2400)
