# The passive Ethernet observer and its asset-free format/lifecycle gate.
add_executable(ethernet_capture_test tests/ethernet_capture_test.cpp)
target_link_libraries(ethernet_capture_test PRIVATE pom68k_core)
add_test(NAME ethernet_capture_test COMMAND ethernet_capture_test)
